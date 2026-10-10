/*
 * Cover Flow - a game library browsed as a row of covers that turn as they pass.
 *
 * The covers stand on a floor with their reflections below them. The one in the
 * middle faces the screen; the others turn away to each side. Moving the
 * selection slides the whole row smoothly.
 *
 * The library, first found wins:
 *  1. live from the Cover Flow launcher payload (payload/cflaunch, port 9112 on
 *     this console), which can read the console's installed titles and icons
 *     where Cover Flow's sandbox cannot; the list and icons are cached in
 *     /app0/covers/auto/ for when the launcher is not loaded;
 *  2. that cache;
 *  3. /app0/covers/games.txt, "image | title | subtitle | TITLEID" a line
 *     (ps5_library_sync.py on a PC writes one);
 *  4. demonstration covers.
 * games.txt entries also override the live list by title id: their image
 * replaces the game's icon, their title its name.
 *
 * Sections: every title is a PS5 game, a PS4 game, an emulator or homebrew.
 * Emulators are told by name (RetroArch, PPSSPP, DuckStation...), homebrew by
 * its title or content id (made-up prefixes, IV/HB content ids, PPSA99xxx);
 * the game menu moves any title to another section, and that is remembered.
 *
 * Sizes are measured by the launcher when a game's menu is first opened (in
 * the background) and kept in /app0/covers/auto/sizes.txt.
 *
 * Sorting (SQUARE: name, title id, recently installed) and filtering (TOUCHPAD:
 * all, favourites, PS5, PS4, emulators, homebrew, hidden) are kept in /app0/coverflow.ini,
 * with the favourites and the games hidden from Cover Flow (hiding only takes a
 * game out of Cover Flow's row; nothing on the console changes). TRIANGLE opens
 * a game's menu: its details, start, favourite, hide. CROSS starts the game
 * through the launcher: Cover Flow asks it to switch, quits, and the launcher
 * starts the game.
 *
 * Themes: /app0/theme/theme.ini, every key optional:
 *   heading = Library            the title at the top
 *   accent = 77,115,242          the backdrop glow (R,G,B 0-255)
 *   text = 235,238,245           titles
 *   dim = 150,158,175            subtitles and hints
 *   background = background.jpg  a picture in theme/ behind everything (PNG/JPG)
 *   dimming = 0.35               how much the picture is darkened (0-1)
 *   font = font.ttf              a TrueType font in theme/
 *   music = music.wav            looped background music in theme/ (PCM WAV)
 *   volume = 0.5                 0-1
 *   reflection = 0.32            reflection strength (0 turns them off)
 *   angle = 70                   degrees the side covers turn
 *
 * OPTIONS (a tap) opens the settings: coming back to Cover Flow when a game
 * closes (the launcher watches for it), music volume, reflections, cover angle,
 * the theme, update checks, reloading the launcher, how to quit, and quitting.
 * Quitting by button is a held combination, OPTIONS + SHARE by default, so a
 * single press never closes Cover Flow; the settings offer L1 + R1 + OPTIONS,
 * holding CIRCLE, or the settings menu only.
 *
 * The launcher is loaded by Cover Flow itself when it is missing or older than
 * the one in the title folder (/app0/cflaunch.elf, sent to the console's
 * payload loader on port 9021).
 *
 * Updates: with update_repo = owner/repo in /app0/coverflow.ini, Cover Flow asks
 * the launcher for that repository's latest GitHub release a few seconds after
 * it opens; a release with eboot.bin, cflaunch.elf and SHA256SUMS can be
 * installed from the dialog. The launcher downloads both files, checks them
 * against SHA256SUMS, and only then replaces the old ones and restarts Cover Flow.
 *
 * Nothing is read from or written to anywhere but this console, except the
 * update check, which only reads from GitHub.
 *
 * Copyright (C) 2026
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#include "vulkanexamplebase.h"
#include "coverflow_files.h" // embeddedFiles: the shaders and the icon (made by make_files.py)

#if defined(VK_EXAMPLE_PS5) && !defined(PS5_HOST_REFERENCE)
// The console's network, TLS and HTTP/2 libraries, for downloading updates from
// inside Cover Flow (an app can use the console's certificates; a payload could
// not). Named through asm labels so the declarations do not depend on the
// namespace they sit in.
extern "C" int cf_sceNetInit(void) __asm__("sceNetInit");
extern "C" int cf_sceNetPoolCreate(const char *, int, int) __asm__("sceNetPoolCreate");
extern "C" int cf_sceSslInit(size_t) __asm__("sceSslInit");
extern "C" int cf_sceHttp2Init(int, int, size_t, int) __asm__("sceHttp2Init");
extern "C" int cf_sceHttp2CreateTemplate(int, const char *, int, int) __asm__("sceHttp2CreateTemplate");
extern "C" int cf_sceHttp2CreateRequestWithURL(int, const char *, const char *, uint64_t) __asm__("sceHttp2CreateRequestWithURL");
extern "C" int cf_sceHttp2DeleteRequest(int) __asm__("sceHttp2DeleteRequest");
extern "C" int cf_sceHttp2SendRequest(int, const void *, size_t) __asm__("sceHttp2SendRequest");
extern "C" int cf_sceHttp2GetStatusCode(int, int *) __asm__("sceHttp2GetStatusCode");
extern "C" int cf_sceHttp2ReadData(int, void *, size_t) __asm__("sceHttp2ReadData");
// The console's time zone and clock format, for the clock in the header
extern "C" int cf_sceSystemServiceParamGetInt(int, int *) __asm__("sceSystemServiceParamGetInt");
#endif

// SHA-256, for checking every downloaded file against the release's SHA256SUMS
struct Sha256 {
	uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	uint64_t length = 0;
	unsigned char block[64];
	size_t used = 0;
	static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
	void compress(const unsigned char *p)
	{
		static const uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
			0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
			0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
			0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
			0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
			0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
		uint32_t w[64];
		for (int i = 0; i < 16; i++) {
			w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
		}
		for (int i = 16; i < 64; i++) {
			w[i] = w[i - 16] + (ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3)) + w[i - 7] +
				(ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10));
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++) {
			const uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
			const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
			hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}
	void update(const void *data, size_t size)
	{
		const unsigned char *p = (const unsigned char *)data;
		length += size;
		while (size > 0) {
			const size_t take = std::min(size, 64 - used);
			std::memcpy(block + used, p, take);
			used += take;
			p += take;
			size -= take;
			if (used == 64) {
				compress(block);
				used = 0;
			}
		}
	}
	std::string hex()
	{
		const uint64_t bits = length * 8;
		const unsigned char pad = 0x80, zero = 0;
		update(&pad, 1);
		while (used != 56) {
			update(&zero, 1);
		}
		unsigned char len[8];
		for (int i = 0; i < 8; i++) {
			len[i] = (unsigned char)(bits >> (56 - i * 8));
		}
		update(len, 8);
		char out[65];
		for (int i = 0; i < 8; i++) {
			std::snprintf(out + i * 8, 9, "%08x", h[i]);
		}
		return std::string(out, 64);
	}
};

static constexpr const char *coverFlowVersion = "v14";
static constexpr int launcherVersion = 8; // the launcher in the title folder

class VulkanExample : public VulkanExampleBase
{
public:
	static constexpr uint32_t coverSize = 512;
	static constexpr uint16_t launcherPort = 9112;

	struct Game {
		std::string title;
		std::string subtitle;
		std::string titleId;
		std::string platform;    // PS5, PS4 or "": from the title id
		std::string section;     // PS5, PS4, Emulators, Homebrew or Other: what the filters use
		std::string contentId;
		std::string coverFrom;   // where the cover came from, for the details panel
		long long bytes{ 0 };
		long long installed{ 0 };
		vks::Texture2D texture;
		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
		bool loaded{ false };   // its cover is on the GPU; until then the placeholder card shows
		// Where its cover comes from, tried in this order by the cover loader
		std::string customPath, cachePath, fetchId;
		int demoIndex{ 0 };
	};
	vks::Texture2D placeholderTexture;
	VkDescriptorSet placeholderSet{ VK_NULL_HANDLE };

	// Covers decoded in the background, uploaded a few a frame
	struct CoverResults {
		std::mutex mutex;
		std::vector<std::pair<int, std::pair<std::vector<uint8_t>, std::string>>> ready; // game, (rgba, where from)
		int done{ 0 };
		int fetched{ 0 };
	};
	std::shared_ptr<CoverResults> coverResults = std::make_shared<CoverResults>();
	int coversLoaded{ 0 };
	float loadingFade{ 1.0f };
	std::vector<Game> games;
	std::vector<int> view;       // the games shown, in order: indices into games
	std::string listSource;

	// Where the row is: 'position' slides towards 'selected' (both index view)
	int selected{ 0 };
	float position{ 0.0f };
	float repeatTimer{ 0.0f };
	float circleHeld{ 0.0f };
	float vibrateTimer{ 0.0f };
	std::string message;
	float messageTimer{ 0.0f };
	std::string scanStatus;
	std::string ownTitleId;
	bool menuOpen{ false };
	int menuIndex{ 0 };
	bool categoryOpen{ false }; // the category list inside the game menu
	bool collectionsOpen{ false }; // the collections checklist inside the game menu
	struct Pending;
	struct DumperProgress;
	bool dumpOpen{ false }; // Game/app file backup workflow
	std::shared_ptr<DumperProgress> dumpJob;
	std::string dumpResult;
	bool dumpSucceeded{ false };
	bool dumpQueued{ false };
	int dumpIndex{ 0 };
	int dumpDestination{ 0 }; // 0: internal storage, 1: USB drive
	int dumpStage{ 0 }; // 0: destination, 1: confirmation, 2: dumping, 3: result
	float dumpProgress{ 0.0f };
	std::string dumpStatus;
	int collectionIndex{ 0 };
	static constexpr int gridCols = 7;
	float gridScroll{ 0.0f };     // the grid's row at the middle, sliding towards the selection's
	float stickTimer{ 0.0f }, stickYTimer{ 0.0f };
	bool l2Down{ false }, r2Down{ false };
	std::string letterShown;      // the letter a jump landed on, shown large for a moment
	float letterTimer{ 0.0f };
	int categoryIndex{ 0 };
	static constexpr const char *categoryKeys[5] = { "", "PS5", "PS4", "Emulators", "Homebrew" }; // "" = automatic

	// Settings (coverflow.ini), changed from the settings screen
	struct Settings {
		bool returnAfter{ true };
		float volume{ -1.0f };     // -1: the theme's
		float reflection{ -1.0f };
		float angle{ -1.0f };
		bool themeOn{ true };
		bool updateCheck{ true };
		std::string updateRepo;
		int quitWith{ 0 }; // 0 OPTIONS + SHARE, 1 L1 + R1 + OPTIONS, 2 hold CIRCLE, 3 settings menu only
		std::string lastGame; // the game selected when Cover Flow last closed
		bool artBackgrounds{ true };
		bool sounds{ true };
		bool grid{ false };
		std::string themeName; // a built-in theme, a folder in themes/, or "" for theme/ (the custom one) or Default
	} settings;
	static constexpr const char *quitNames[4] = { "hold OPTIONS + SHARE", "hold L1 + R1 + OPTIONS", "hold CIRCLE (2 seconds)",
		"Settings menu only" };
	static constexpr uint32_t padShare = 0x000001; // the SHARE / CREATE button
	uint32_t lastHeld{ 0 };
	float comboHeld{ 0.0f };
	bool optionsInCombo{ false };
	bool settingsOpen{ false };
	int settingsIndex{ 0 };
	std::string launcherStatus{ "not checked" };

	// A launcher answer that arrives in the background
	struct Pending {
		std::mutex mutex;
		bool done{ false };
		std::string answer;
	};

	// Live state reported by the external PS5 App Dumper payload.
	struct DumperProgress {
		std::mutex mutex;
		bool done{ false };
		bool success{ false };
		bool queued{ false };
		float progress{ 0.0f };
		std::string stage{ "Starting dumper" };
		std::string answer;
	};

	// Small HTTP client for the PS5 App Dumper's local-only API.
	// API requests stay on loopback; the dumper itself writes to console storage/USB.
	static std::string dumperHttp(int port, const std::string &method, const std::string &path,
		const std::string &body = "", int timeoutSeconds = 3)
	{
		const int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) return "";
		timeval timeout{ timeoutSeconds, 0 };
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons((uint16_t)port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (connect(fd, (sockaddr *)&address, sizeof(address)) != 0) {
			close(fd);
			return "";
		}
		std::string request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
		if (method == "POST") {
			request += "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
				std::to_string(body.size()) + "\r\n";
		}
		request += "\r\n";
		if (method == "POST") request += body;
		bool ok = true;
		for (size_t at = 0; at < request.size();) {
			const ssize_t n = send(fd, request.data() + at, request.size() - at, 0);
			if (n <= 0) { ok = false; break; }
			at += (size_t)n;
		}
		std::string response;
		char buffer[8192];
		size_t expected = std::string::npos;
		size_t bodyStart = std::string::npos;
		while (ok && response.size() < (2u << 20)) {
			const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
			if (n <= 0) break;
			response.append(buffer, (size_t)n);
			if (bodyStart == std::string::npos) {
				bodyStart = response.find("\r\n\r\n");
				if (bodyStart != std::string::npos) {
					bodyStart += 4;
					const size_t h = response.find("Content-Length:");
					if (h != std::string::npos && h < bodyStart) {
						expected = (size_t)std::strtoull(response.c_str() + h + 15, nullptr, 10);
					}
				}
			}
			if (bodyStart != std::string::npos && expected != std::string::npos &&
				response.size() >= bodyStart + expected) break;
		}
		close(fd);
		if (!ok || bodyStart == std::string::npos || response.size() < bodyStart) return "";
		return response.substr(bodyStart, expected == std::string::npos ? std::string::npos : expected);
	}

	static std::string jsonString(const std::string &json, const std::string &key)
	{
		const std::string needle = "\"" + key + "\":";
		size_t p = json.find(needle);
		if (p == std::string::npos) return "";
		p += needle.size();
		while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) p++;
		if (p >= json.size() || json[p] != '"') return "";
		p++;
		std::string out;
		while (p < json.size()) {
			char c = json[p++];
			if (c == '"') break;
			if (c == '\\' && p < json.size()) {
				char escaped = json[p++];
				if (escaped == 'n') out += '\n';
				else if (escaped == 'r') out += '\r';
				else if (escaped == 't') out += '\t';
				else out += escaped;
			} else out += c;
		}
		return out;
	}

	static unsigned long long jsonNumber(const std::string &json, const std::string &key)
	{
		const std::string needle = "\"" + key + "\":";
		size_t p = json.find(needle);
		if (p == std::string::npos) return 0;
		p += needle.size();
		while (p < json.size() && json[p] == ' ') p++;
		return std::strtoull(json.c_str() + p, nullptr, 10);
	}

	static bool jsonTrue(const std::string &json, const std::string &key)
	{
		const std::string needle = "\"" + key + "\":";
		size_t p = json.find(needle);
		if (p == std::string::npos) return false;
		p += needle.size();
		while (p < json.size() && json[p] == ' ') p++;
		return json.compare(p, 4, "true") == 0;
	}

	static std::string urlEncode(const std::string &value)
	{
		static const char hex[] = "0123456789ABCDEF";
		std::string out;
		for (unsigned char c : value) {
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
			else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
		}
		return out;
	}

	static int findDumperPort()
	{
		for (int port = 8081; port <= 8090; port++) {
			const std::string devices = dumperHttp(port, "GET", "/api/devices", "", 1);
			if (devices.find("\"targets\":[") != std::string::npos &&
				devices.find("\"apps\":[") != std::string::npos) return port;
		}
		return 0;
	}

	static std::string chooseDumperTarget(const std::string &devices, bool internal)
	{
		const size_t array = devices.find("\"targets\":[");
		if (array == std::string::npos) return "";
		const size_t end = devices.find(']', array);
		if (end == std::string::npos) return "";
		size_t p = array;
		while ((p = devices.find('{', p)) != std::string::npos && p < end) {
			const size_t close = devices.find('}', p);
			if (close == std::string::npos || close > end) break;
			const std::string item = devices.substr(p, close - p + 1);
			if (jsonTrue(item, "writable") && jsonTrue(item, "internal") == internal) {
				const std::string mount = jsonString(item, "mount");
				if (internal || mount.rfind("/mnt/usb", 0) == 0) return mount;
			}
			p = close + 1;
		}
		return "";
	}

	static void runDumperBackend(const std::shared_ptr<DumperProgress> &job,
		const std::string &titleId, bool internal, const std::string &payloadPath)
	{
		auto finishJob = [&](bool success, bool queued, const std::string &stage, const std::string &answer) {
			std::lock_guard<std::mutex> lock(job->mutex);
			job->success = success;
			job->queued = queued;
			job->done = true;
			job->stage = stage;
			job->progress = success ? 0.0f : job->progress;
			job->answer = answer;
		};

		int port = findDumperPort();
		if (!port) {
			// Try the configured payload path and common homebrew locations.
			std::vector<std::string> candidates = {
				payloadPath,
				"/data/homebrew/PPSA99123/ps5-app-dumper.elf",
				"/mnt/usb0/homebrew/ps5-app-dumper.elf",
				"/mnt/usb1/homebrew/ps5-app-dumper.elf",
				"/mnt/usb0/ps5-app-dumper/ps5-app-dumper.elf",
				"/mnt/usb1/ps5-app-dumper/ps5-app-dumper.elf"
			};
			bool sent = false;
			for (const auto &candidate : candidates) {
				if (!candidate.empty() && sendPayload(candidate)) { sent = true; break; }
			}
			if (!sent) {
				finishJob(false, false, "Payload unavailable",
					"Dumper payload not found or payload loader on port 9021 is unavailable. Install ps5-app-dumper.elf and ensure elfldr is running.");
				return;
			}
			for (int attempt = 0; attempt < 40 && !port; attempt++) {
				usleep(500000);
				port = findDumperPort();
			}
		}
		if (!port) {
			finishJob(false, false, "Dumper unavailable",
				"PS5 App Dumper did not start. Check payload logs and firmware compatibility.");
			return;
		}

		const std::string devices = dumperHttp(port, "GET", "/api/devices", "", 5);
		if (devices.empty()) {
			finishJob(false, false, "Dumper unavailable", "Could not read PS5 App Dumper devices.");
			return;
		}
		const std::string target = chooseDumperTarget(devices, internal);
		if (target.empty()) {
			finishJob(false, false, "Destination unavailable",
				internal ? "No writable internal destination is available." :
				"No writable USB drive found. Connect a supported USB drive and retry.");
			return;
		}

		// A proper dump needs the selected title mounted. Cover Flow cannot stay
		// on screen while the PS5 launches that game, so submit the dumper's own
		// queue API; its worker launches/mounts the title and runs the dump.
		const std::string form = "titles=" + urlEncode(titleId) +
			"&target=" + urlEncode(target) + "&replace=0";
		const std::string response = dumperHttp(port, "POST", "/api/queue/start", form, 10);
		if (response.find("\\"started\\":true") == std::string::npos) {
			std::string err = jsonString(response, "error");
			if (err.empty()) err = response.empty() ?
				"PS5 App Dumper did not accept the queue request." : response;
			finishJob(false, false, "Queue rejected", err);
			return;
		}
		finishJob(true, true, "Queued",
			"Dump queued for " + titleId + " at " + target +
			". The dumper will launch the title; Cover Flow may close. Monitor progress in the PS5 App Dumper web interface.");
	}

	std::shared_ptr<Pending> updateCheckJob, updateApplyJob;
	bool updateDialog{ false };
	int updateChoice{ 0 };
	std::string updateTag, updateNotes;
	bool updating{ false };
	float closeAfterUpdate{ 0.0f }; // seconds until Cover Flow closes after an update
	float updateCheckDelay{ 3.0f };

	// Sorting and filtering, kept between runs with the favourites and hidden games
	static constexpr int sortCount = 5, baseFilters = 7, collectionCount = 6, filterCount = baseFilters + collectionCount;
	int sortMode{ 0 };   // 0 name, 1 title id, 2 recently installed, 3 recently played, 4 most played
	int filterMode{ 0 }; // 0 all, 1 favourites, 2 PS5, 3 PS4, 4 emulators, 5 homebrew, 6 hidden, 7.. collections
	static constexpr const char *sortNames[sortCount] = { "Name A-Z", "Title ID", "Recently installed", "Recently played", "Most played" };
	static constexpr const char *baseFilterNames[baseFilters] = { "All games", "Favourites", "PS5 Games", "PS4 Games", "Emulators", "Homebrew Apps", "Hidden games" };
	// Collections: six, named in coverflow.ini (collections = ...), a game in any of them
	std::vector<std::string> collectionNames{ "Racing", "Co-op", "Playing now", "Finished", "Retro", "Family" };
	std::map<std::string, int> collectionMembers; // title id -> bit per collection
	std::string filterName(int f) const
	{
		return f < baseFilters ? baseFilterNames[f] : collectionNames[f - baseFilters];
	}
	bool collectionUsed(int c) const
	{
		for (const auto &[id, bits] : collectionMembers) {
			if (bits >> c & 1) {
				return true;
			}
		}
		return false;
	}
	bool inCollection(const Game &g, int c) const
	{
		auto it = collectionMembers.find(g.titleId);
		return it != collectionMembers.end() && (it->second >> c & 1);
	}
	// Play time the launcher recorded for games started from Cover Flow
	struct PlayRecord {
		long long seconds{ 0 }, last{ 0 }, count{ 0 };
	};
	std::map<std::string, PlayRecord> playTime;
	std::string storageText;
	std::set<std::string> favourites, hidden;
	std::map<std::string, std::string> sectionOverride; // title id -> PS5, PS4, Emulators, Homebrew

	// Sizes, measured by the launcher in the background
	std::map<std::string, long long> sizeCache;
	// Finished measurements, picked up by the render loop; shared with the
	// measuring threads so one still running when Cover Flow quits is harmless
	struct SizeResults {
		std::mutex mutex;
		std::map<std::string, long long> done;
	};
	std::shared_ptr<SizeResults> sizeResults = std::make_shared<SizeResults>();
	std::set<std::string> sizePending;

	// A game installed on the console, from its application metadata
	struct Installed {
		std::string id;
		std::string name;
		std::string icon;
	};
	std::vector<Installed> installed;

	// The theme
	struct Theme {
		std::string heading{ "Library" };
		glm::vec3 accent{ 0.30f, 0.45f, 0.95f };
		ImU32 text{ IM_COL32(235, 238, 245, 255) };
		ImU32 dim{ IM_COL32(150, 158, 175, 255) };
		std::string picture;
		float dimming{ 0.35f };
		std::string music;
		float volume{ 0.5f };
		float reflection{ 0.32f };
		float sideAngle{ 70.0f };
		bool hasPicture{ false };
		float pictureAspect{ 1.0f };
	} theme;
	vks::Texture2D backgroundTexture;
	VkDescriptorSet backgroundSet{ VK_NULL_HANDLE };

	// Music: the whole track as 48 kHz stereo, looped by the audio thread
	std::vector<int16_t> musicFrames;
	std::atomic<size_t> musicCursor{ 0 };
	std::atomic<float> musicVolume{ 0.5f };
	bool musicPlaying{ false };

	// The look, tunable in code
	float sideOffset{ 0.74f };   // centre of the first side cover from the middle
	float sideSpacing{ 0.30f };  // between side covers
	float centrePop{ 0.35f };    // how far the middle cover comes forward

	struct PushConstants {
		glm::mat4 mvp;
		glm::vec4 params;
		glm::vec4 extra; // accent colour, time
	};
	float clock{ 0.0f };

	struct {
		VkPipeline art{ VK_NULL_HANDLE };
		VkPipeline background{ VK_NULL_HANDLE };
		VkPipeline reflection{ VK_NULL_HANDLE };
		VkPipeline cover{ VK_NULL_HANDLE };
	} pipelines;
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	glm::mat4 projection{ 1.0f };
	glm::mat4 view3d{ 1.0f };

	VulkanExample() : VulkanExampleBase()
	{
		title = "Cover Flow";
		apiVersion = VK_API_VERSION_1_3;
		useDynamicRendering = true;
		requiresStencil = true;
		deviceCreatepNextChain = &baseDynamicRenderingFeatures;
		defaultClearColor = { { 0.0f, 0.0f, 0.0f, 1.0f } };
#if defined(VK_EXAMPLE_PS5)
		// The program draws all its own text: no settings window
		ps5.ownOverlay = true;
#endif
	}

	~VulkanExample()
	{
		if (musicPlaying) {
			audio_stop();
		}
		rememberSelection();
		if (device) {
			vkDestroyPipeline(device, pipelines.background, nullptr);
			vkDestroyPipeline(device, pipelines.art, nullptr);
			for (auto &a : arts) {
				a->texture.destroy();
			}
			if (artPool) {
				vkDestroyDescriptorPool(device, artPool, nullptr);
			}
			vkDestroyPipeline(device, pipelines.reflection, nullptr);
			vkDestroyPipeline(device, pipelines.cover, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			for (auto &game : games) {
				if (game.loaded) {
					game.texture.destroy();
				}
			}
			placeholderTexture.destroy();
			if (theme.hasPicture) {
				backgroundTexture.destroy();
			}
		}
	}

	// ---------------------------------------------------------------- files

	static std::string appRoot()
	{
		// getAssetPath() is "<title folder>/assets/"
		std::string root = getAssetPath();
		const std::string suffix = "assets/";
		if (root.size() >= suffix.size() && root.compare(root.size() - suffix.size(), suffix.size(), suffix) == 0) {
			root.resize(root.size() - suffix.size());
		}
		return root;
	}

	static std::string trim(const std::string &s)
	{
		const size_t a = s.find_first_not_of(" \t\r\n");
		if (a == std::string::npos) {
			return "";
		}
		const size_t b = s.find_last_not_of(" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	// Crops the image to a centred square and resamples it to coverSize (bilinear)
	static std::vector<uint8_t> squareCover(const uint8_t *pixels, int w, int h)
	{
		std::vector<uint8_t> out(coverSize * coverSize * 4);
		const int side = std::min(w, h);
		const float x0 = (w - side) * 0.5f, y0 = (h - side) * 0.5f;
		const float step = (float)side / coverSize;
		for (uint32_t y = 0; y < coverSize; y++) {
			for (uint32_t x = 0; x < coverSize; x++) {
				const float sx = std::clamp(x0 + (x + 0.5f) * step - 0.5f, 0.0f, (float)(w - 1));
				const float sy = std::clamp(y0 + (y + 0.5f) * step - 0.5f, 0.0f, (float)(h - 1));
				const int ix = (int)sx, iy = (int)sy;
				const int ix1 = std::min(ix + 1, w - 1), iy1 = std::min(iy + 1, h - 1);
				const float fx = sx - ix, fy = sy - iy;
				for (int c = 0; c < 4; c++) {
					const float a = pixels[(iy * w + ix) * 4 + c] * (1 - fx) + pixels[(iy * w + ix1) * 4 + c] * fx;
					const float b = pixels[(iy1 * w + ix) * 4 + c] * (1 - fx) + pixels[(iy1 * w + ix1) * 4 + c] * fx;
					out[(y * coverSize + x) * 4 + c] = (uint8_t)std::lround(a * (1 - fy) + b * fy);
				}
			}
		}
		return out;
	}

	// A demonstration cover: a diagonal two-colour gradient with rings, a hue per index
	static std::vector<uint8_t> demoCover(int index)
	{
		std::vector<uint8_t> out(coverSize * coverSize * 4);
		auto hue = [](float h) {
			h = h - std::floor(h);
			const float r = std::clamp(std::abs(h * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
			const float g = std::clamp(2.0f - std::abs(h * 6.0f - 2.0f), 0.0f, 1.0f);
			const float b = std::clamp(2.0f - std::abs(h * 6.0f - 4.0f), 0.0f, 1.0f);
			return glm::vec3(r, g, b);
		};
		const glm::vec3 a = hue(index * 0.137f) * 0.85f;
		const glm::vec3 b = hue(index * 0.137f + 0.33f) * 0.25f;
		const glm::vec2 centre(0.3f + 0.4f * std::fmod(index * 0.61f, 1.0f), 0.35f + 0.3f * std::fmod(index * 0.37f, 1.0f));
		for (uint32_t y = 0; y < coverSize; y++) {
			for (uint32_t x = 0; x < coverSize; x++) {
				const glm::vec2 uv((x + 0.5f) / coverSize, (y + 0.5f) / coverSize);
				glm::vec3 c = glm::mix(a, b, std::clamp((uv.x + uv.y) * 0.5f, 0.0f, 1.0f));
				const float d = glm::length(uv - centre);
				const float ring = 0.5f + 0.5f * std::cos(d * 60.0f - index);
				c = glm::mix(c, glm::vec3(1.0f), 0.12f * ring * std::exp(-d * 3.0f));
				// A band near the bottom, like a cover's title strip
				if (uv.y > 0.78f && uv.y < 0.9f) {
					c = glm::mix(c, glm::vec3(0.02f), 0.55f);
				}
				for (int k = 0; k < 3; k++) {
					out[(y * coverSize + x) * 4 + k] = (uint8_t)std::lround(std::clamp(c[k], 0.0f, 1.0f) * 255.0f);
				}
				out[(y * coverSize + x) * 4 + 3] = 255;
			}
		}
		return out;
	}

	static bool readFile(const std::string &path, std::string &out)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) {
			return false;
		}
		std::stringstream ss;
		ss << f.rdbuf();
		out = ss.str();
		return true;
	}

	static std::string lower(std::string s)
	{
		for (auto &c : s) {
			c = (char)std::tolower((unsigned char)c);
		}
		return s;
	}

	// A JSON string value after "key": the first one at or after 'from'
	static std::string jsonString(const std::string &text, const std::string &key, size_t from = 0)
	{
		const size_t k = text.find("\"" + key + "\"", from);
		if (k == std::string::npos) {
			return "";
		}
		size_t q = text.find(':', k);
		if (q == std::string::npos) {
			return "";
		}
		q = text.find('"', q);
		if (q == std::string::npos) {
			return "";
		}
		std::string value;
		for (size_t i = q + 1; i < text.size(); i++) {
			const char c = text[i];
			if (c == '"') {
				return value;
			}
			if (c == '\\' && i + 1 < text.size()) {
				const char e = text[++i];
				if (e == 'n' || e == 't') {
					value += ' ';
				} else if (e == 'u' && i + 4 < text.size()) {
					// \uXXXX: BMP code point to UTF-8
					const unsigned cp = (unsigned)std::strtoul(text.substr(i + 1, 4).c_str(), nullptr, 16);
					i += 4;
					if (cp < 0x80) {
						value += (char)cp;
					} else if (cp < 0x800) {
						value += (char)(0xC0 | (cp >> 6));
						value += (char)(0x80 | (cp & 0x3F));
					} else {
						value += (char)(0xE0 | (cp >> 12));
						value += (char)(0x80 | ((cp >> 6) & 0x3F));
						value += (char)(0x80 | (cp & 0x3F));
					}
				} else {
					value += e;
				}
			} else {
				value += c;
			}
		}
		return "";
	}

	// A PS5 title's name from its param.json: English if it has it, else the first
	static std::string nameFromParamJson(const std::string &text)
	{
		const size_t en = text.find("\"en-US\"");
		std::string name = en != std::string::npos ? jsonString(text, "titleName", en) : "";
		if (name.empty()) {
			const size_t gb = text.find("\"en-GB\"");
			name = gb != std::string::npos ? jsonString(text, "titleName", gb) : "";
		}
		return name.empty() ? jsonString(text, "titleName") : name;
	}

	// A PS4 title's name from its param.sfo: the TITLE entry
	static std::string nameFromSfo(const std::string &d)
	{
		auto u16 = [&](size_t o) { return o + 2 <= d.size() ? (uint32_t)(uint8_t)d[o] | ((uint32_t)(uint8_t)d[o + 1] << 8) : 0u; };
		auto u32 = [&](size_t o) { return o + 4 <= d.size() ? u16(o) | (u16(o + 2) << 16) : 0u; };
		if (d.size() < 0x14 || d.compare(0, 4, std::string("\0PSF", 4)) != 0) {
			return "";
		}
		const uint32_t keys = u32(0x08), data = u32(0x0C), count = u32(0x10);
		for (uint32_t i = 0; i < count && i < 256; i++) {
			const size_t e = 0x14 + i * 16;
			const size_t keyAt = keys + u16(e);
			if (keyAt >= d.size()) {
				break;
			}
			if (std::strcmp(d.c_str() + keyAt, "TITLE") == 0) {
				const size_t at = data + u32(e + 12);
				const size_t len = u32(e + 4);
				if (at + len > d.size()) {
					return "";
				}
				std::string v = d.substr(at, len);
				v.resize(std::strlen(v.c_str()));
				return v;
			}
		}
		return "";
	}

	static bool isTitleId(const std::string &s)
	{
		if (s.size() != 9) {
			return false;
		}
		for (int i = 0; i < 4; i++) {
			if (s[i] < 'A' || s[i] > 'Z') {
				return false;
			}
		}
		for (int i = 4; i < 9; i++) {
			if (s[i] < '0' || s[i] > '9') {
				return false;
			}
		}
		return true;
	}

	// The games installed on the console: every title folder in its application
	// metadata, with a name. Read-only; nothing leaves the console.
	void scanInstalled()
	{
		std::string own;
		if (readFile(appRoot() + "sce_sys/param.json", own)) {
			ownTitleId = jsonString(own, "titleId");
		}
#if defined(PS5_HOST_REFERENCE)
		const std::vector<std::string> roots = { appRoot() + "test-appmeta/" };
#else
		const std::vector<std::string> roots = { "/user/appmeta/", "/system_data/priv/appmeta/" };
#endif
		std::set<std::string> seen;
		std::string tried;
		for (const auto &root : roots) {
			DIR *dir = opendir(root.c_str());
			if (!dir) {
				tried += (tried.empty() ? "" : ", ") + root + " (" + std::strerror(errno) + ")";
				continue;
			}
			int found = 0;
			while (dirent *e = readdir(dir)) {
				const std::string id = e->d_name;
				if (!isTitleId(id) || id == ownTitleId || seen.count(id)) {
					continue;
				}
				const std::string folder = root + id + "/";
				std::string text, name;
				if (readFile(folder + "param.json", text)) {
					name = nameFromParamJson(text);
				}
				if (name.empty() && readFile(folder + "param.sfo", text)) {
					name = nameFromSfo(text);
				}
				if (name.empty()) {
					continue; // not a title the console can show
				}
				seen.insert(id);
				installed.push_back({ id, name, folder + "icon0.png" });
				found++;
			}
			closedir(dir);
			tried += (tried.empty() ? "" : ", ") + root + " (" + std::to_string(found) + ")";
		}
		std::sort(installed.begin(), installed.end(), [](const Installed &a, const Installed &b) { return lower(a.name) < lower(b.name); });
		scanStatus = std::to_string(installed.size()) + " installed games found: " + tried;
		std::cerr << "Cover Flow: " << scanStatus << "\n";
	}

	const Installed *findInstalled(const std::string &id, const std::string &name) const
	{
		for (const auto &g : installed) {
			if ((!id.empty() && g.id == id) || (id.empty() && lower(g.name) == lower(name))) {
				return &g;
			}
		}
		return nullptr;
	}

	// A cover from an image file, or nullptr-equivalent (empty) when it cannot be read
	static std::vector<uint8_t> loadCover(const std::string &path)
	{
		int w = 0, h = 0, n = 0;
		uint8_t *pixels = path.empty() ? nullptr : stbi_load(path.c_str(), &w, &h, &n, 4);
		std::vector<uint8_t> out;
		if (pixels && w > 0 && h > 0) {
			out = squareCover(pixels, w, h);
		}
		if (pixels) {
			stbi_image_free(pixels);
		}
		return out;
	}

	static bool writeFile(const std::string &path, const std::string &data)
	{
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f) {
			return false;
		}
		f.write(data.data(), (std::streamsize)data.size());
		return (bool)f;
	}

	static std::string platformOf(const std::string &id)
	{
		if (id.rfind("PPSA", 0) == 0 || id.rfind("ECAS", 0) == 0) {
			return "PS5";
		}
		if (id.rfind("CUSA", 0) == 0) {
			return "PS4";
		}
		return "";
	}

	// key = value lines, # comments
	static std::map<std::string, std::string> readIni(const std::string &path)
	{
		std::map<std::string, std::string> values;
		std::ifstream f(path);
		std::string line;
		while (f && std::getline(f, line)) {
			line = trim(line);
			if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
				continue;
			}
			const size_t eq = line.find('=');
			if (eq != std::string::npos) {
				values[lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
			}
		}
		return values;
	}

	static bool parseColour(const std::string &s, int rgb[3])
	{
		int r, g, b;
		if (std::sscanf(s.c_str(), "%d , %d , %d", &r, &g, &b) == 3 || std::sscanf(s.c_str(), "%d %d %d", &r, &g, &b) == 3) {
			rgb[0] = std::clamp(r, 0, 255);
			rgb[1] = std::clamp(g, 0, 255);
			rgb[2] = std::clamp(b, 0, 255);
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------- the theme

	// The built-in themes: colours only
	struct Preset {
		const char *name;
		int accent[3], text[3], dim[3];
	};
	static constexpr Preset presets[] = {
		{ "Default", { 77, 115, 242 }, { 235, 238, 245 }, { 150, 158, 175 } },
		{ "Midnight", { 110, 90, 255 }, { 232, 230, 250 }, { 150, 145, 185 } },
		{ "Racing Red", { 230, 50, 60 }, { 245, 238, 236 }, { 185, 160, 160 } },
		{ "Emerald", { 40, 200, 140 }, { 232, 245, 240 }, { 145, 175, 165 } },
		{ "Sunset", { 250, 140, 60 }, { 250, 240, 230 }, { 190, 165, 145 } },
		{ "Mono", { 200, 200, 210 }, { 240, 240, 242 }, { 150, 150, 158 } },
	};

	// Every theme to choose from: the built-in ones, theme/ (as "Custom") and each folder in themes/
	std::vector<std::string> themeNames() const
	{
		std::vector<std::string> names;
		for (const Preset &p : presets) {
			names.push_back(p.name);
		}
		if (std::ifstream(appRoot() + "theme/theme.ini").good()) {
			names.push_back("Custom");
		}
		if (DIR *dir = opendir((appRoot() + "themes").c_str())) {
			std::vector<std::string> folders;
			while (dirent *e = readdir(dir)) {
				const std::string n = e->d_name;
				if (n[0] != '.' && std::ifstream(appRoot() + "themes/" + n + "/theme.ini").good()) {
					folders.push_back(n);
				}
			}
			closedir(dir);
			std::sort(folders.begin(), folders.end());
			names.insert(names.end(), folders.begin(), folders.end());
		}
		return names;
	}

	// The theme in use: as chosen, or "Custom" when theme/ exists and nothing was chosen (older versions)
	std::string currentTheme() const
	{
		if (!settings.themeName.empty()) {
			return settings.themeName;
		}
		return settings.themeOn && std::ifstream(appRoot() + "theme/theme.ini").good() ? "Custom" : "Default";
	}

	static std::string themeFolder(const std::string &name)
	{
		if (name == "Custom") {
			return appRoot() + "theme/";
		}
		for (const Preset &p : presets) {
			if (name == p.name) {
				return "";
			}
		}
		return appRoot() + "themes/" + name + "/";
	}

	// Switch theme: colours at once, a picture and music too when the theme has them
	void applyTheme(const std::string &name, bool live)
	{
		if (live) {
			stopAudio();
			if (theme.hasPicture) {
				vkDeviceWaitIdle(device);
				if (backgroundSet) {
					vkFreeDescriptorSets(device, artPool, 1, &backgroundSet);
					backgroundSet = VK_NULL_HANDLE;
				}
				backgroundTexture.destroy();
			}
		}
		theme = Theme{};
		musicFrames.clear();
		musicCursor.store(0);
		for (const Preset &p : presets) {
			if (name == p.name) {
				theme.accent = glm::vec3(p.accent[0], p.accent[1], p.accent[2]) / 255.0f;
				theme.text = IM_COL32(p.text[0], p.text[1], p.text[2], 255);
				theme.dim = IM_COL32(p.dim[0], p.dim[1], p.dim[2], 255);
			}
		}
		const std::string folder = themeFolder(name);
		if (!folder.empty()) {
			loadTheme(folder);
		}
		if (live && theme.hasPicture && artPool) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(artPool, &descriptorSetLayout, 1);
			if (vkAllocateDescriptorSets(device, &allocInfo, &backgroundSet) == VK_SUCCESS) {
				VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(backgroundSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &backgroundTexture.descriptor);
				vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
			} else {
				backgroundTexture.destroy();
				theme.hasPicture = false;
			}
		}
		applySettings();
		if (live) {
			startAudio();
#if defined(VK_EXAMPLE_PS5)
			pad_light_bar((uint8_t)(theme.accent.r * 255), (uint8_t)(theme.accent.g * 255), (uint8_t)(theme.accent.b * 255));
#endif
		}
	}

	void loadTheme(const std::string &folder)
	{
		auto ini = readIni(folder + "theme.ini");
		auto num = [&](const char *key, float def, float lo, float hi) {
			auto it = ini.find(key);
			return it == ini.end() ? def : std::clamp((float)std::atof(it->second.c_str()), lo, hi);
		};
		int rgb[3];
		if (ini.count("heading")) {
			theme.heading = ini["heading"];
		}
		if (ini.count("accent") && parseColour(ini["accent"], rgb)) {
			theme.accent = glm::vec3(rgb[0], rgb[1], rgb[2]) / 255.0f;
		}
		if (ini.count("text") && parseColour(ini["text"], rgb)) {
			theme.text = IM_COL32(rgb[0], rgb[1], rgb[2], 255);
		}
		if (ini.count("dim") && parseColour(ini["dim"], rgb)) {
			theme.dim = IM_COL32(rgb[0], rgb[1], rgb[2], 255);
		}
		theme.dimming = num("dimming", theme.dimming, 0.0f, 1.0f);
		theme.volume = num("volume", theme.volume, 0.0f, 1.0f);
		theme.reflection = num("reflection", theme.reflection, 0.0f, 1.0f);
		theme.sideAngle = num("angle", theme.sideAngle, 0.0f, 89.0f);
		if (ini.count("background") && !ini["background"].empty()) {
			int w = 0, h = 0, n = 0;
			uint8_t *pixels = stbi_load((folder + ini["background"]).c_str(), &w, &h, &n, 4);
			if (pixels && w > 0 && h > 0) {
				backgroundTexture.fromBuffer(pixels, (VkDeviceSize)w * h * 4, VK_FORMAT_R8G8B8A8_SRGB, w, h, vulkanDevice, queue);
				theme.hasPicture = true;
				theme.pictureAspect = (float)w / (float)h;
			} else {
				std::cerr << "Cover Flow: theme background " << ini["background"] << " could not be read\n";
			}
			if (pixels) {
				stbi_image_free(pixels);
			}
		}
		if (ini.count("music") && !ini["music"].empty() && !loadWav(folder + ini["music"])) {
			musicFrames.clear();
			std::cerr << "Cover Flow: theme music " << ini["music"] << " is not a PCM WAV file it can play\n";
		}
		if (!ini.empty()) {
			std::cerr << "Cover Flow: theme from " << folder << "theme.ini\n";
		}
	}

	// A PCM WAV (8/16/24/32-bit integer or 32-bit float, any rate, mono or
	// stereo) to 48 kHz stereo 16-bit; the volume is applied as it plays
	bool loadWav(const std::string &path)
	{
		std::string d;
		if (!readFile(path, d) || d.size() < 12 || d.compare(0, 4, "RIFF") != 0 || d.compare(8, 4, "WAVE") != 0) {
			return false;
		}
		auto u16 = [&](size_t o) { return (uint32_t)(uint8_t)d[o] | ((uint32_t)(uint8_t)d[o + 1] << 8); };
		auto u32 = [&](size_t o) { return u16(o) | (u16(o + 2) << 16); };
		uint32_t format = 0, channels = 0, rate = 0, bits = 0;
		size_t dataAt = 0, dataSize = 0;
		for (size_t o = 12; o + 8 <= d.size();) {
			const std::string id = d.substr(o, 4);
			const size_t size = u32(o + 4);
			if (id == "fmt " && o + 24 <= d.size()) {
				format = u16(o + 8);
				channels = u16(o + 10);
				rate = u32(o + 12);
				bits = u16(o + 22);
				if (format == 0xFFFE && o + 34 <= d.size()) {
					format = u16(o + 32); // WAVE_FORMAT_EXTENSIBLE: the sub-format
				}
			} else if (id == "data") {
				dataAt = o + 8;
				dataSize = std::min(size, d.size() - dataAt);
				break;
			}
			o += 8 + size + (size & 1);
		}
		const uint32_t bytes = bits / 8;
		if (!dataAt || channels < 1 || channels > 8 || rate < 8000 || rate > 192000 || bytes < 1 || bytes > 4 ||
			!((format == 1) || (format == 3 && bits == 32))) {
			return false;
		}
		const size_t frames = dataSize / (bytes * channels);
		auto sample = [&](size_t frame, uint32_t ch) -> float {
			const char *p = d.data() + dataAt + (frame * channels + std::min(ch, channels - 1)) * bytes;
			if (format == 3) {
				float f;
				std::memcpy(&f, p, 4);
				return f;
			}
			switch (bytes) {
			case 1: return ((uint8_t)p[0] - 128) / 128.0f;
			case 2: return (int16_t)((uint8_t)p[0] | ((uint8_t)p[1] << 8)) / 32768.0f;
			case 3: return (int32_t)(((uint32_t)(uint8_t)p[0] << 8) | ((uint32_t)(uint8_t)p[1] << 16) | ((uint32_t)(uint8_t)p[2] << 24)) / 2147483648.0f;
			default: return (int32_t)((uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) | ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24)) / 2147483648.0f;
			}
		};
		const double step = (double)rate / 48000.0;
		const size_t out = (size_t)(frames / step);
		if (out < 256 || out > 48000ull * 60 * 15) {
			return false; // under a few ms, or over 15 minutes
		}
		musicFrames.resize(out * 2);
		for (size_t i = 0; i < out; i++) {
			const double at = i * step;
			const size_t f0 = std::min((size_t)at, frames - 1), f1 = std::min(f0 + 1, frames - 1);
			const float t = (float)(at - f0);
			for (uint32_t ch = 0; ch < 2; ch++) {
				const float v = sample(f0, ch) * (1 - t) + sample(f1, ch) * t;
				musicFrames[i * 2 + ch] = (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
			}
		}
		return true;
	}

	// ---- sound: the theme's music and short interface sounds, mixed on the audio thread

	std::vector<int16_t> sfx[4]; // move, open / select, back, error: mono 48 kHz
	std::atomic<int> sfxPending[4]{};
	struct Voice {
		int sound{ -1 };
		size_t at{ 0 };
	} voices[4];

	// Short sounds, made here: a soft tick, a rising and a falling pair of notes, a low buzz
	void makeSounds()
	{
		auto tone = [](std::vector<int16_t> &out, float f0, float f1, float ms, float volume) {
			const int n = (int)(48.0f * ms);
			const size_t start = out.size();
			out.resize(start + n);
			float phase = 0.0f;
			for (int i = 0; i < n; i++) {
				const float t = (float)i / n;
				phase += 6.2831853f * (f0 + (f1 - f0) * t) / 48000.0f;
				const float env = std::min(1.0f, i / 96.0f) * std::exp(-t * 5.0f);
				out[start + i] = (int16_t)(std::sin(phase) * env * volume * 32767.0f);
			}
		};
		tone(sfx[0], 1900.0f, 1700.0f, 28.0f, 0.10f);
		tone(sfx[1], 660.0f, 660.0f, 60.0f, 0.13f);
		tone(sfx[1], 990.0f, 990.0f, 90.0f, 0.13f);
		tone(sfx[2], 880.0f, 880.0f, 60.0f, 0.12f);
		tone(sfx[2], 590.0f, 590.0f, 90.0f, 0.12f);
		tone(sfx[3], 180.0f, 150.0f, 160.0f, 0.16f);
	}

	void playSound(int type)
	{
		if (settings.sounds && musicPlaying && type >= 0 && type < 4) {
			sfxPending[type]++;
		}
	}

	static void fillMusic(int16_t *frames, int count, void *user)
	{
		auto *self = (VulkanExample *)user;
		const size_t total = self->musicFrames.size() / 2;
		size_t at = self->musicCursor.load();
		const float volume = self->musicVolume.load();
		for (int t = 0; t < 4; t++) {
			while (self->sfxPending[t].load() > 0) {
				self->sfxPending[t]--;
				for (Voice &v : self->voices) {
					if (v.sound < 0) {
						v.sound = t;
						v.at = 0;
						break;
					}
				}
			}
		}
		for (int i = 0; i < count; i++) {
			int l = 0, r = 0;
			if (total) {
				l = (int)(self->musicFrames[at * 2] * volume);
				r = (int)(self->musicFrames[at * 2 + 1] * volume);
				at = at + 1 < total ? at + 1 : 0;
			}
			for (Voice &v : self->voices) {
				if (v.sound >= 0) {
					const int x = self->sfx[v.sound][v.at++];
					l += x;
					r += x;
					if (v.at >= self->sfx[v.sound].size()) {
						v.sound = -1;
					}
				}
			}
			frames[i * 2] = (int16_t)std::clamp(l, -32768, 32767);
			frames[i * 2 + 1] = (int16_t)std::clamp(r, -32768, 32767);
		}
		self->musicCursor.store(at);
	}

	// The audio output runs while there is music or the sounds are on (never in test runs)
	void startAudio()
	{
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget || musicPlaying || (musicFrames.empty() && !settings.sounds)) {
			return;
		}
		musicPlaying = audio_start(fillMusic, this);
#endif
	}

	void stopAudio()
	{
		if (musicPlaying) {
			audio_stop();
			musicPlaying = false;
		}
	}

	// ---------------------------------------------------------------- the launcher

	// One command to the launcher and everything it answers; "" when it is not there
	static std::string askLauncher(const std::string &command, size_t limit = 16u << 20, int seconds = 3)
	{
		const int s = socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0) {
			return "";
		}
		timeval timeout{ seconds, 0 };
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(launcherPort);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		std::string answer;
		if (connect(s, (sockaddr *)&address, sizeof(address)) == 0) {
			const std::string line = command + "\n";
			if (send(s, line.data(), line.size(), 0) == (ssize_t)line.size()) {
				char buffer[65536];
				for (;;) {
					const ssize_t got = recv(s, buffer, sizeof(buffer), 0);
					if (got <= 0) {
						break;
					}
					answer.append(buffer, (size_t)got);
					if (answer.size() > limit) {
						break;
					}
					// One-line answers end at their newline; list ends at "end"; icon at its length
					if (answer.rfind("ok list", 0) == 0) {
						if (answer.size() >= 4 && answer.compare(answer.size() - 4, 4, "end\n") == 0) {
							break;
						}
					} else if (answer.rfind("ok ", 0) == 0 && command.rfind("icon", 0) == 0) {
						const size_t nl = answer.find('\n');
						if (nl != std::string::npos && answer.size() >= nl + 1 + std::strtoull(answer.c_str() + 3, nullptr, 10)) {
							break;
						}
					} else if (answer.find('\n') != std::string::npos) {
						break;
					}
				}
			}
		}
		close(s);
		return answer;
	}

	// Send a payload to the console's payload loader (port 9021)
	static bool sendPayload(const std::string &path)
	{
		std::string elf;
		if (!readFile(path, elf) || elf.size() < 64 || elf.compare(1, 3, "ELF") != 0) {
			return false;
		}
		const int s = socket(AF_INET, SOCK_STREAM, 0);
		if (s < 0) {
			return false;
		}
		timeval timeout{ 3, 0 };
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(9021);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		bool sent = connect(s, (sockaddr *)&address, sizeof(address)) == 0;
		for (size_t at = 0; sent && at < elf.size();) {
			const ssize_t n = send(s, elf.data() + at, elf.size() - at, 0);
			sent = n > 0;
			at += n > 0 ? (size_t)n : 0;
		}
		close(s);
		return sent;
	}

	// The running launcher's version, 0 when none answers
	static int launcherRunning()
	{
		const std::string answer = askLauncher("ping", 4096, 1);
		if (answer.rfind("ok cflaunch ", 0) == 0) {
			return std::max(1, std::atoi(answer.c_str() + 12));
		}
		return answer.rfind("ok", 0) == 0 ? 1 : 0;
	}

	// Load the launcher from the title folder when none, or an older one, runs
	void ensureLauncher(bool force = false)
	{
		const int running = launcherRunning();
		if (running >= launcherVersion && !force) {
			launcherStatus = "running (version " + std::to_string(running) + ")";
			return;
		}
		if (!sendPayload(appRoot() + "cflaunch.elf")) {
			launcherStatus = running ? "running (version " + std::to_string(running) + "), could not load the newer one"
				: "not running (no payload loader on port 9021, or no cflaunch.elf in the title folder)";
			std::cerr << "Cover Flow: launcher " << launcherStatus << "\n";
			return;
		}
		// A newer launcher asks the old one to leave before it takes the port
		for (int wait = 0; wait < 30; wait++) {
			usleep(250000);
			const int now = launcherRunning();
			if (now >= launcherVersion) {
				launcherStatus = "loaded by Cover Flow (version " + std::to_string(now) + ")";
				std::cerr << "Cover Flow: launcher " << launcherStatus << "\n";
				return;
			}
		}
		launcherStatus = "sent to the payload loader, but it did not answer";
		std::cerr << "Cover Flow: launcher " << launcherStatus << "\n";
	}

	// One launcher command in the background
	static std::shared_ptr<Pending> askInBackground(const std::string &command, int seconds)
	{
		auto job = std::make_shared<Pending>();
		std::thread([job, command, seconds]() {
			const std::string answer = trim(askLauncher(command, 1u << 20, seconds));
			std::lock_guard<std::mutex> lock(job->mutex);
			job->answer = answer;
			job->done = true;
		}).detach();
		return job;
	}

	static bool finished(const std::shared_ptr<Pending> &job, std::string &answer)
	{
		if (!job) {
			return false;
		}
		std::lock_guard<std::mutex> lock(job->mutex);
		if (!job->done) {
			return false;
		}
		answer = job->answer;
		return true;
	}

	struct Entry {
		std::string id, name, kind, contentId;
		long long bytes{ 0 }, installed{ 0 };
	};

	// "ID<TAB>SIZE<TAB>MTIME<TAB>NAME[<TAB>KIND]" lines, between "ok list" and "end"
	static std::vector<Entry> parseList(const std::string &text)
	{
		std::vector<Entry> list;
		std::stringstream ss(text);
		std::string line;
		while (std::getline(ss, line)) {
			std::vector<std::string> f;
			std::stringstream ls(line);
			std::string field;
			while (std::getline(ls, field, '\t')) {
				f.push_back(field);
			}
			if (f.size() >= 4 && isTitleId(f[0])) {
				Entry e;
				e.id = f[0];
				e.bytes = std::atoll(f[1].c_str());
				e.installed = std::atoll(f[2].c_str());
				e.name = trim(f[3]);
				e.kind = f.size() >= 5 ? trim(f[4]) : "";
				e.contentId = f.size() >= 6 ? trim(f[5]) : "";
				list.push_back(e);
			}
		}
		return list;
	}

	// ---------------------------------------------------------------- the library

	// Names as the font can draw them: symbols it lacks are dropped or simplified
	static std::string cleanName(const std::string &in)
	{
		static const std::pair<const char *, const char *> swaps[] = {
			{ "\xE2\x84\xA2", "" },         // trade mark
			{ "\xE2\x80\x98", "'" }, { "\xE2\x80\x99", "'" },
			{ "\xE2\x80\x9C", "\"" }, { "\xE2\x80\x9D", "\"" },
			{ "\xE2\x80\x93", "-" }, { "\xE2\x80\x94", "-" },
			{ "\xE2\x80\xA6", "..." }, { "\xE3\x80\x80", " " },
		};
		std::string s = in;
		for (const auto &sw : swaps) {
			for (size_t at; (at = s.find(sw.first)) != std::string::npos;) {
				s.replace(at, std::strlen(sw.first), sw.second);
			}
		}
		// Collapse doubled spaces left behind
		for (size_t at; (at = s.find("  ")) != std::string::npos;) {
			s.erase(at, 1);
		}
		return trim(s);
	}

	// A title's section from what it is
	static std::string autoSection(const std::string &id, const std::string &name, const std::string &contentId)
	{
		static const char *emulators[] = { "retroarch", "emulator", "pcsx", "ppsspp", "dolphin", "duckstation", "scummvm",
			"mame", "fbneo", "finalburn", "snes9x", "mgba", "flycast", "nestopia", "genesis plus", "picodrive", "mupen",
			"mednafen", "bsnes", "fceu", "stella", "dosbox", "melonds", "desmume", "citra", "yabause", "beetle", "rpcs3",
			"xemu", "pcsx2", "aethersx2", "nethersx2", "play!", "cemu", "ryujinx", "yuzu", "vba", "visualboy", " emu" };
		const std::string n = " " + lower(name) + " ";
		for (const char *e : emulators) {
			if (n.find(e) != std::string::npos) {
				return "Emulators";
			}
		}
		const std::string prefix = id.substr(0, 4);
		const bool sonyPrefix = prefix == "PPSA" || prefix == "CUSA" || prefix == "ECAS" || prefix == "PPSE";
		const int number = id.size() == 9 ? std::atoi(id.c_str() + 4) : 0;
		const bool homebrewContent = contentId.rfind("IV", 0) == 0 || contentId.rfind("HB", 0) == 0 ||
			contentId.find("-HOMEBREW") != std::string::npos;
		if (!id.empty() && (!sonyPrefix || homebrewContent || number >= 99000)) {
			return "Homebrew";
		}
		const std::string p = platformOf(id);
		return p.empty() ? "Other" : p;
	}

	static std::string categoryName(const std::string &section)
	{
		if (section == "PS5") {
			return "PS5 Games";
		}
		if (section == "PS4") {
			return "PS4 Games";
		}
		if (section == "Homebrew") {
			return "Homebrew Apps";
		}
		return section.empty() ? "Other" : section;
	}

	std::string sectionOf(const Game &g) const
	{
		auto it = sectionOverride.find(g.titleId);
		return it != sectionOverride.end() ? it->second : autoSection(g.titleId, g.title, g.contentId);
	}

	Game &addGame(const std::string &title, const std::string &subtitle, const std::string &titleId, long long bytes = 0,
		long long installedAt = 0)
	{
		Game game;
		game.title = title;
		game.subtitle = subtitle;
		game.titleId = titleId;
		game.platform = platformOf(titleId);
		game.coverFrom = "loading";
		game.bytes = bytes;
		game.installed = installedAt;
		game.demoIndex = (int)games.size();
		games.push_back(std::move(game));
		return games.back();
	}

	struct Custom {
		std::string image, name, subtitle, id;
	};

	std::vector<Custom> readGamesTxt()
	{
		std::vector<Custom> list;
		std::ifstream f(appRoot() + "covers/games.txt");
		std::string line;
		while (f && std::getline(f, line)) {
			line = trim(line);
			if (line.empty() || line[0] == '#') {
				continue;
			}
			std::vector<std::string> fields;
			std::stringstream ss(line);
			std::string field;
			while (std::getline(ss, field, '|')) {
				fields.push_back(trim(field));
			}
			Custom c;
			c.image = fields.size() > 0 ? fields[0] : "";
			c.name = fields.size() > 1 ? fields[1] : "";
			c.subtitle = fields.size() > 2 ? fields[2] : "";
			c.id = fields.size() > 3 ? fields[3] : "";
			list.push_back(c);
		}
		return list;
	}

	// The installed list (live or cached) with games.txt's overrides and extras;
	// covers are only planned here and loaded in the background
	void buildFromList(const std::vector<Entry> &list, bool live)
	{
		const std::string covers = appRoot() + "covers/";
		const std::string cache = covers + "auto/";
		std::vector<Custom> custom = readGamesTxt();
		std::set<std::string> used;
		for (const Entry &e : list) {
			if (e.id == ownTitleId) {
				continue;
			}
			std::string name = cleanName(e.name), subtitle, customPath;
			for (const Custom &c : custom) {
				if (c.id == e.id) {
					if (!c.name.empty()) {
						name = c.name;
					}
					if (!c.subtitle.empty()) {
						subtitle = c.subtitle;
					}
					if (!c.image.empty() && customPath.empty()) {
						customPath = covers + c.image;
					}
				}
			}
			used.insert(e.id);
			Game &g = addGame(name, subtitle, e.id, e.bytes, e.installed);
			g.contentId = e.contentId;
			g.customPath = customPath;
			g.cachePath = cache + e.id + ".png";
			g.fetchId = live ? e.id : "";
		}
		// games.txt entries that are not in the installed list
		for (const Custom &c : custom) {
			if (!c.id.empty() && used.count(c.id)) {
				continue;
			}
			Game &g = addGame(c.name.empty() ? c.image : cleanName(c.name), c.subtitle, c.id);
			g.customPath = c.image.empty() ? "" : covers + c.image;
		}
	}

	void loadLibrary()
	{
		std::string own;
		if (readFile(appRoot() + "sce_sys/param.json", own)) {
			ownTitleId = jsonString(own, "titleId");
		}
		const std::string covers = appRoot() + "covers/";
		const std::string cache = covers + "auto/";
		mkdir(covers.c_str(), 0777);
		mkdir(cache.c_str(), 0777);

		// 1. Live from the launcher
		const std::string answer = askLauncher("list");
		if (answer.rfind("ok list", 0) == 0) {
			std::vector<Entry> list = parseList(answer);
			if (!list.empty()) {
				writeFile(cache + "library.txt", answer);
				buildFromList(list, true);
				listSource = "live";
				std::cerr << "Cover Flow: " << games.size() << " games from the launcher\n";
				return;
			}
		}
		// 2. The last live list
		std::string cached;
		if (readFile(cache + "library.txt", cached)) {
			std::vector<Entry> list = parseList(cached);
			if (!list.empty()) {
				buildFromList(list, false);
				listSource = "saved list (launcher not loaded)";
				std::cerr << "Cover Flow: " << games.size() << " games from the saved list\n";
				return;
			}
		}
		// 3. games.txt alone
		buildFromList({}, false);
		if (!games.empty()) {
			listSource = "games.txt";
			return;
		}
		// 4. What the sandbox can see (the PC reference build's test metadata)
		scanInstalled();
		for (const auto &g : installed) {
			addGame(cleanName(g.name), "", g.id).customPath = g.icon;
		}
		if (!games.empty()) {
			listSource = "installed games";
			return;
		}
		// 5. Demonstration covers
		const char *names[] = { "Demo Title One", "Demo Title Two", "Demo Title Three", "Demo Title Four",
			"Demo Title Five", "Demo Title Six", "Demo Title Seven", "Demo Title Eight", "Demo Title Nine",
			"Demo Title Ten", "Demo Title Eleven", "Demo Title Twelve" };
		for (int i = 0; i < 12; i++) {
			addGame(names[i], "Load the Cover Flow launcher to show your games", "");
		}
		listSource = "demonstration covers";
	}

	// ---------------------------------------------------------------- files carried inside Cover Flow

	// Cover Flow carries its shaders and its icon inside eboot.bin and writes
	// them to the title folder when they are missing or different, so an update
	// that replaces only eboot.bin still brings everything it needs
	void installEmbedded()
	{
		int written = 0;
		for (const auto &file : embeddedFiles) {
			const std::string path = appRoot() + file.path;
			std::string have;
			if (readFile(path, have) && have.size() == file.size && std::memcmp(have.data(), file.data, file.size) == 0) {
				continue;
			}
			// Make the folders, then write beside it and swap it in
			for (size_t at = appRoot().size(); (at = path.find('/', at + 1)) != std::string::npos;) {
				mkdir(path.substr(0, at).c_str(), 0777);
			}
			if (writeFile(path + ".tmp", std::string((const char *)file.data, file.size)) && std::rename((path + ".tmp").c_str(), path.c_str()) == 0) {
				written++;
			} else {
				std::cerr << "Cover Flow: could not write " << path << "\n";
			}
		}
		if (written) {
			std::cerr << "Cover Flow: wrote " << written << " of its own files\n";
		}
	}

	// ---------------------------------------------------------------- game artwork behind the covers

	struct Art {
		std::string id;
		vks::Texture2D texture;
		VkDescriptorSet set{ VK_NULL_HANDLE };
		float aspect{ 1.0f };
	};
	std::vector<std::unique_ptr<Art>> arts; // the last few, oldest first
	VkDescriptorPool artPool{ VK_NULL_HANDLE };
	struct ArtJob {
		std::mutex mutex;
		bool done{ false };
		std::string id;
		std::vector<uint8_t> rgba;
		int w{ 0 }, h{ 0 };
	};
	std::shared_ptr<ArtJob> artJob;
	std::set<std::string> artMissing;
	std::string artShown, artPrev;
	float artAlpha{ 0.0f }, artPrevAlpha{ 0.0f }, settledFor{ 0.0f };

	Art *findArt(const std::string &id)
	{
		for (auto &a : arts) {
			if (a->id == id) {
				return a.get();
			}
		}
		return nullptr;
	}

	// Bilinear resize of RGBA pixels
	static std::vector<uint8_t> resize(const uint8_t *pixels, int w, int h, int ow, int oh)
	{
		std::vector<uint8_t> out((size_t)ow * oh * 4);
		for (int y = 0; y < oh; y++) {
			for (int x = 0; x < ow; x++) {
				const float sx = std::clamp((x + 0.5f) * w / ow - 0.5f, 0.0f, (float)(w - 1));
				const float sy = std::clamp((y + 0.5f) * h / oh - 0.5f, 0.0f, (float)(h - 1));
				const int ix = (int)sx, iy = (int)sy, ix1 = std::min(ix + 1, w - 1), iy1 = std::min(iy + 1, h - 1);
				const float fx = sx - ix, fy = sy - iy;
				for (int c = 0; c < 4; c++) {
					const float a = pixels[(iy * w + ix) * 4 + c] * (1 - fx) + pixels[(iy * w + ix1) * 4 + c] * fx;
					const float b = pixels[(iy1 * w + ix) * 4 + c] * (1 - fx) + pixels[(iy1 * w + ix1) * 4 + c] * fx;
					out[((size_t)y * ow + x) * 4 + c] = (uint8_t)std::lround(a * (1 - fy) + b * fy);
				}
			}
		}
		return out;
	}

	void requestArt(const std::string &id)
	{
		auto job = std::make_shared<ArtJob>();
		job->id = id;
		artJob = job;
		std::thread([job, id]() {
			const std::string answer = askLauncher("art " + id, 24u << 20, 5);
			const size_t nl = answer.find('\n');
			std::vector<uint8_t> rgba;
			int ow = 0, oh = 0;
			if (answer.rfind("ok ", 0) == 0 && nl != std::string::npos) {
				int w = 0, h = 0, n = 0;
				uint8_t *pixels = stbi_load_from_memory((const stbi_uc *)answer.data() + nl + 1, (int)(answer.size() - nl - 1), &w, &h, &n, 4);
				if (pixels && w > 0 && h > 0) {
					// At most 960 wide: plenty behind dimmed covers, and quick to upload
					ow = std::min(w, 960);
					oh = std::max(1, h * ow / w);
					rgba = resize(pixels, w, h, ow, oh);
				}
				if (pixels) {
					stbi_image_free(pixels);
				}
			}
			std::lock_guard<std::mutex> lock(job->mutex);
			job->rgba = std::move(rgba);
			job->w = ow;
			job->h = oh;
			job->done = true;
		}).detach();
	}

	// Which artwork shows: the selected game's, once the row has settled on it
	void updateArt(float dt)
	{
		const bool enabled = settings.artBackgrounds && listSource == "live" && !settings.grid;
		const Game *g = current();
		settledFor = (g && std::abs(selected - position) < 0.05f) ? settledFor + dt : 0.0f;
		std::string target = enabled && g && settledFor > 0.25f ? g->titleId : (enabled ? artShown : "");
		if (!enabled) {
			target = "";
		}
		// A finished download becomes a texture
		if (artJob) {
			bool done;
			{
				std::lock_guard<std::mutex> lock(artJob->mutex);
				done = artJob->done;
			}
			if (done) {
				if (artJob->rgba.empty()) {
					artMissing.insert(artJob->id);
				} else {
					if (arts.size() >= 6) {
						// Forget the oldest that is not on screen
						for (auto it = arts.begin(); it != arts.end(); ++it) {
							if ((*it)->id != artShown && (*it)->id != artPrev) {
								vkDeviceWaitIdle(device);
								vkFreeDescriptorSets(device, artPool, 1, &(*it)->set);
								(*it)->texture.destroy();
								arts.erase(it);
								break;
							}
						}
					}
					auto a = std::make_unique<Art>();
					a->id = artJob->id;
					a->texture.fromBuffer(artJob->rgba.data(), artJob->rgba.size(), VK_FORMAT_R8G8B8A8_SRGB, artJob->w, artJob->h, vulkanDevice, queue);
					a->aspect = (float)artJob->w / (float)artJob->h;
					VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(artPool, &descriptorSetLayout, 1);
					if (vkAllocateDescriptorSets(device, &allocInfo, &a->set) == VK_SUCCESS) {
						VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(a->set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &a->texture.descriptor);
						vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
						arts.push_back(std::move(a));
					} else {
						a->texture.destroy();
					}
				}
				artJob.reset();
			}
		}
		if (target != artShown) {
			if (target.empty() || findArt(target)) {
				artPrev = artShown;
				artPrevAlpha = artAlpha;
				artShown = target;
				artAlpha = 0.0f;
			} else if (!artJob && !artMissing.count(target)) {
				requestArt(target);
			} else if (artMissing.count(target)) {
				// No artwork: fade out what was there
				artPrev = artShown;
				artPrevAlpha = artAlpha;
				artShown = "";
				artAlpha = 0.0f;
			}
		}
		const float k = 1.0f - std::exp(-dt * 5.0f);
		artAlpha += ((artShown.empty() ? 0.0f : 1.0f) - artAlpha) * k;
		artPrevAlpha += (0.0f - artPrevAlpha) * k;
	}

	// ---------------------------------------------------------------- play time, storage, the clock

	void loadPlayTime()
	{
		const std::string cache = appRoot() + "covers/auto/playtime.txt";
		std::string text = listSource == "live" ? askLauncher("playtime", 1u << 20, 2) : "";
		if (text.rfind("ok playtime", 0) == 0) {
			writeFile(cache, text);
		} else {
			readFile(cache, text);
		}
		playTime.clear();
		std::stringstream ss(text);
		std::string line;
		while (std::getline(ss, line)) {
			std::stringstream ls(line);
			std::string id;
			PlayRecord r;
			if (ls >> id >> r.seconds >> r.last >> r.count && isTitleId(id)) {
				playTime[id] = r;
			}
		}
	}

	void loadStorage()
	{
		if (listSource != "live") {
			return;
		}
		const std::string answer = trim(askLauncher("storage", 4096, 2));
		long long freeBytes = 0, total = 0;
		if (std::sscanf(answer.c_str(), "ok %lld %lld", &freeBytes, &total) == 2 && total > 0) {
			storageText = formatSize(freeBytes) + " free";
		}
	}

	static std::string formatDuration(long long seconds)
	{
		if (seconds <= 0) {
			return "Not yet";
		}
		const long long h = seconds / 3600, m = (seconds % 3600) / 60;
		if (h == 0) {
			return std::to_string(std::max(1LL, m)) + " min";
		}
		return std::to_string(h) + " h " + std::to_string(m) + " min";
	}

	// The time in the console's time zone and clock format
	std::string clockText() const
	{
		const time_t now = time(nullptr);
		struct tm t;
		int zone = 0, summer = 0, format = 1;
		bool known = false;
#if defined(VK_EXAMPLE_PS5) && !defined(PS5_HOST_REFERENCE)
		known = cf_sceSystemServiceParamGetInt(4, &zone) == 0;
		if (cf_sceSystemServiceParamGetInt(5, &summer) != 0) {
			summer = 0;
		}
		if (cf_sceSystemServiceParamGetInt(3, &format) != 0) {
			format = 1;
		}
#endif
		if (known) {
			const time_t shifted = now + (time_t)(zone + (summer ? 60 : 0)) * 60;
			gmtime_r(&shifted, &t);
		} else {
			localtime_r(&now, &t);
		}
		char out[16];
		if (format == 0) {
			std::snprintf(out, sizeof(out), "%d:%02d %s", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
		} else {
			std::snprintf(out, sizeof(out), "%02d:%02d", t.tm_hour, t.tm_min);
		}
		return out;
	}

	// ---------------------------------------------------------------- the cover loader

	// Decode every game's cover on a worker thread, nearest the selection first
	void startCoverLoader()
	{
		struct Job {
			int index;
			std::string customPath, cachePath, fetchId;
			int demoIndex;
		};
		std::vector<Job> jobs;
		std::vector<int> order;
		for (int k = 0; k < (int)view.size(); k++) {
			order.push_back(view[k]);
		}
		std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
			auto at = [&](int g) {
				for (int k = 0; k < (int)view.size(); k++) {
					if (view[k] == g) {
						return std::abs(k - selected);
					}
				}
				return 1 << 20;
			};
			return at(a) < at(b);
		});
		std::set<int> queued(order.begin(), order.end());
		for (int i = 0; i < (int)games.size(); i++) {
			if (!queued.count(i)) {
				order.push_back(i); // games the filter hides, last
			}
		}
		for (int i : order) {
			const Game &g = games[i];
			jobs.push_back({ i, g.customPath, g.cachePath, g.fetchId, g.demoIndex });
		}
		auto results = coverResults;
		std::thread([results, jobs]() {
			for (const Job &job : jobs) {
				std::vector<uint8_t> rgba;
				std::string from;
				if (!job.customPath.empty()) {
					rgba = loadCover(job.customPath);
					from = "custom (games.txt)";
				}
				if (rgba.empty() && !job.cachePath.empty()) {
					rgba = loadCover(job.cachePath);
					from = "game icon";
				}
				if (rgba.empty() && !job.fetchId.empty()) {
					const std::string answer = askLauncher("icon " + job.fetchId);
					const size_t nl = answer.find('\n');
					if (answer.rfind("ok ", 0) == 0 && nl != std::string::npos) {
						const std::string png = answer.substr(nl + 1);
						int w = 0, h = 0, n = 0;
						uint8_t *pixels = stbi_load_from_memory((const stbi_uc *)png.data(), (int)png.size(), &w, &h, &n, 4);
						if (pixels && w > 0 && h > 0) {
							rgba = squareCover(pixels, w, h);
							writeFile(job.cachePath, png);
							from = "game icon";
							std::lock_guard<std::mutex> lock(results->mutex);
							results->fetched++;
						}
						if (pixels) {
							stbi_image_free(pixels);
						}
					}
				}
				if (rgba.empty()) {
					rgba = demoCover(job.demoIndex);
					from = job.fetchId.empty() && job.cachePath.empty() && job.customPath.empty() ? "demonstration" : "plain (no icon found)";
				}
				std::lock_guard<std::mutex> lock(results->mutex);
				results->ready.push_back({ job.index, { std::move(rgba), from } });
				results->done++;
			}
		}).detach();
	}

	// Upload the covers the worker has finished: a few each frame
	void uploadCovers()
	{
		std::vector<std::pair<int, std::pair<std::vector<uint8_t>, std::string>>> batch;
		{
			std::lock_guard<std::mutex> lock(coverResults->mutex);
			const size_t take = std::min<size_t>(coverResults->ready.size(), 6);
			batch.assign(std::make_move_iterator(coverResults->ready.begin()), std::make_move_iterator(coverResults->ready.begin() + take));
			coverResults->ready.erase(coverResults->ready.begin(), coverResults->ready.begin() + take);
		}
		for (auto &[index, item] : batch) {
			Game &g = games[index];
			if (g.loaded) {
				continue;
			}
			g.texture.fromBuffer((void *)item.first.data(), item.first.size(), VK_FORMAT_R8G8B8A8_SRGB, coverSize, coverSize, vulkanDevice, queue);
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &g.descriptorSet));
			VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(g.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &g.texture.descriptor);
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
			g.coverFrom = item.second;
			g.loaded = true;
			coversLoaded++;
		}
	}

	// The card shown until a cover has loaded
	void makePlaceholder()
	{
		const uint32_t n = 64;
		std::vector<uint8_t> rgba(n * n * 4);
		for (uint32_t y = 0; y < n; y++) {
			for (uint32_t x = 0; x < n; x++) {
				const float t = (float)(x + y) / (2.0f * n);
				const uint8_t v = (uint8_t)(34 + 22 * (1.0f - t));
				uint8_t *p = &rgba[(y * n + x) * 4];
				p[0] = v;
				p[1] = (uint8_t)(v + 3);
				p[2] = (uint8_t)(v + 12);
				p[3] = 255;
			}
		}
		placeholderTexture.fromBuffer(rgba.data(), rgba.size(), VK_FORMAT_R8G8B8A8_SRGB, n, n, vulkanDevice, queue);
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &placeholderSet));
		VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(placeholderSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &placeholderTexture.descriptor);
		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
	}

	VkDescriptorSet coverSet(const Game &g) const
	{
		return g.loaded ? g.descriptorSet : placeholderSet;
	}

	// ---------------------------------------------------------------- sort and filter

	void loadSettings()
	{
		auto ini = readIni(appRoot() + "coverflow.ini");
		sortMode = std::clamp(std::atoi(ini["sort"].c_str()), 0, sortCount - 1);
		filterMode = std::clamp(std::atoi(ini["filter"].c_str()), 0, filterCount - 1);
		auto ids = [](const std::string &list) {
			std::set<std::string> out;
			std::stringstream ss(list);
			std::string id;
			while (std::getline(ss, id, ',')) {
				id = trim(id);
				if (isTitleId(id)) {
					out.insert(id);
				}
			}
			return out;
		};
		favourites = ids(ini["favourites"]);
		hidden = ids(ini["hidden"]);
		auto flag = [&](const char *key, bool def) { return ini.count(key) ? ini[key] != "0" : def; };
		auto value = [&](const char *key, float lo, float hi) {
			return ini.count(key) && !ini[key].empty() ? std::clamp((float)std::atof(ini[key].c_str()), lo, hi) : -1.0f;
		};
		settings.returnAfter = flag("return_after_game", true);
		settings.themeOn = flag("theme", true);
		settings.updateCheck = flag("update_check", true);
		settings.volume = value("volume", 0.0f, 1.0f);
		settings.reflection = value("reflection", 0.0f, 1.0f);
		settings.angle = value("angle", 0.0f, 89.0f);
		// Cover Flow's own releases unless coverflow.ini names another repository
		settings.updateRepo = ini.count("update_repo") && !ini["update_repo"].empty() ? ini["update_repo"] : "awaisa123/coverflow";
		settings.quitWith = ini.count("quit_with") ? std::clamp(std::atoi(ini["quit_with"].c_str()), 0, 3) : 0;
		settings.lastGame = ini.count("last_game") ? ini["last_game"] : "";
		settings.artBackgrounds = flag("art_backgrounds", true);
		settings.sounds = flag("sounds", true);
		settings.grid = flag("grid", false);
		settings.themeName = ini.count("theme_name") ? ini["theme_name"] : "";
		if (ini.count("collections") && !ini["collections"].empty()) {
			std::stringstream cs(ini["collections"]);
			std::string name;
			for (int c = 0; c < collectionCount && std::getline(cs, name, ','); c++) {
				if (!trim(name).empty()) {
					collectionNames[c] = trim(cleanName(name)).substr(0, 24);
				}
			}
		}
		collectionMembers.clear();
		{
			std::stringstream ms(ini["collection_members"]);
			std::string pair;
			while (std::getline(ms, pair, ',')) {
				const size_t colon = pair.find(':');
				if (colon != std::string::npos && isTitleId(trim(pair.substr(0, colon)))) {
					collectionMembers[trim(pair.substr(0, colon))] = std::atoi(pair.c_str() + colon + 1) & 63;
				}
			}
		}
		sectionOverride.clear();
		std::stringstream ss(ini["sections"]);
		std::string pair;
		while (std::getline(ss, pair, ',')) {
			const size_t colon = pair.find(':');
			if (colon != std::string::npos) {
				const std::string id = trim(pair.substr(0, colon)), sec = trim(pair.substr(colon + 1));
				if (isTitleId(id) && (sec == "PS5" || sec == "PS4" || sec == "Emulators" || sec == "Homebrew")) {
					sectionOverride[id] = sec;
				}
			}
		}
	}

	void saveSettings()
	{
		auto join = [](const std::set<std::string> &ids) {
			std::string out;
			for (const auto &id : ids) {
				out += (out.empty() ? "" : ",") + id;
			}
			return out;
		};
		std::string sections;
		for (const auto &[id, sec] : sectionOverride) {
			sections += (sections.empty() ? "" : ",") + id + ":" + sec;
		}
		writeFile(appRoot() + "coverflow.ini", "sort = " + std::to_string(sortMode) + "\nfilter = " + std::to_string(filterMode) +
			"\nfavourites = " + join(favourites) + "\nhidden = " + join(hidden) + "\nsections = " + sections +
			"\nreturn_after_game = " + (settings.returnAfter ? "1" : "0") + "\ntheme = " + (settings.themeOn ? "1" : "0") +
			"\nupdate_check = " + (settings.updateCheck ? "1" : "0") + "\nupdate_repo = " + settings.updateRepo +
			"\nquit_with = " + std::to_string(settings.quitWith) + "\nlast_game = " + settings.lastGame +
			"\nvolume = " + (settings.volume >= 0 ? std::to_string(settings.volume) : "") +
			"\nreflection = " + (settings.reflection >= 0 ? std::to_string(settings.reflection) : "") +
			"\nangle = " + (settings.angle >= 0 ? std::to_string(settings.angle) : "") +
			"\nart_backgrounds = " + (settings.artBackgrounds ? "1" : "0") + "\nsounds = " + (settings.sounds ? "1" : "0") +
			"\ngrid = " + (settings.grid ? "1" : "0") + "\ntheme_name = " + settings.themeName +
			"\ncollections = " + collectionsLine() + "\ncollection_members = " + membersLine() + "\n");
	}

	std::string collectionsLine() const
	{
		std::string out;
		for (const auto &n : collectionNames) {
			out += (out.empty() ? "" : ",") + n;
		}
		return out;
	}

	std::string membersLine() const
	{
		std::string out;
		for (const auto &[id, bits] : collectionMembers) {
			if (bits) {
				out += (out.empty() ? "" : ",") + id + ":" + std::to_string(bits);
			}
		}
		return out;
	}

	// Keep the selected game for next time (not in test runs, which must stay the same)
	void rememberSelection()
	{
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget || !prepared) {
			return;
		}
#endif
		const Game *g = current();
		if (restoredRestart) {
			return; // the restored settings stay as they are
		}
		if (g && !g->titleId.empty() && g->titleId != settings.lastGame) {
			settings.lastGame = g->titleId;
			saveSettings();
		}
	}

	bool isFavourite(const Game &g) const { return !g.titleId.empty() && favourites.count(g.titleId); }
	bool isHidden(const Game &g) const { return !g.titleId.empty() && hidden.count(g.titleId); }

	const Game *current() const
	{
		return view.empty() ? nullptr : &games[view[std::clamp(selected, 0, (int)view.size() - 1)]];
	}

	// Rebuild the view, keeping the selected game where it can
	void rebuildView()
	{
		const Game *was = current();
		const int keep = was ? (int)(was - games.data()) : -1;
		const int keepSlot = selected;
		view.clear();
		for (int i = 0; i < (int)games.size(); i++) {
			const Game &g = games[i];
			const std::string &p = g.section;
			const bool shown = filterMode == 6 ? isHidden(g)
				: !isHidden(g) && (filterMode == 0 || (filterMode == 1 && isFavourite(g)) || (filterMode == 2 && p == "PS5") ||
					(filterMode == 3 && p == "PS4") || (filterMode == 4 && p == "Emulators") || (filterMode == 5 && p == "Homebrew") ||
					(filterMode >= baseFilters && inCollection(g, filterMode - baseFilters)));
			if (shown) {
				view.push_back(i);
			}
		}
		std::stable_sort(view.begin(), view.end(), [&](int a, int b) {
			const Game &x = games[a], &y = games[b];
			if (sortMode == 1 && x.titleId != y.titleId) {
				return x.titleId < y.titleId;
			}
			if (sortMode == 2 && x.installed != y.installed) {
				return x.installed > y.installed;
			}
			if (sortMode == 3 || sortMode == 4) {
				auto px = playTime.find(x.titleId), py = playTime.find(y.titleId);
				const long long ax = px == playTime.end() ? 0 : (sortMode == 3 ? px->second.last : px->second.seconds);
				const long long ay = py == playTime.end() ? 0 : (sortMode == 3 ? py->second.last : py->second.seconds);
				if (ax != ay) {
					return ax > ay;
				}
			}
			return lower(x.title) < lower(y.title);
		});
		// The same game if it is still shown, otherwise the one now in its place
		selected = view.empty() ? 0 : std::clamp(keepSlot, 0, (int)view.size() - 1);
		for (int k = 0; k < (int)view.size(); k++) {
			if (view[k] == keep) {
				selected = k;
			}
		}
		position = (float)selected;
	}

	// ---------------------------------------------------------------- Vulkan setup

	void setupDescriptors()
	{
		// One set per game (allocated as its cover arrives), the placeholder and the backdrop
		const uint32_t count = (uint32_t)games.size() + 2;
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count),
		};
		VkDescriptorPoolCreateInfo poolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, count);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));
		const std::vector<VkDescriptorSetLayoutBinding> bindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
		};
		VkDescriptorSetLayoutCreateInfo layoutInfo = vks::initializers::descriptorSetLayoutCreateInfo(bindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));
		makePlaceholder();
		// Artwork and theme pictures come and go: their own pool, whose sets can be freed
		{
			std::vector<VkDescriptorPoolSize> artSizes = { vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12) };
			VkDescriptorPoolCreateInfo artInfo = vks::initializers::descriptorPoolCreateInfo(artSizes, 12);
			artInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			VK_CHECK_RESULT(vkCreateDescriptorPool(device, &artInfo, nullptr, &artPool));
		}
		if (theme.hasPicture) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(artPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &backgroundSet));
			VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(backgroundSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &backgroundTexture.descriptor);
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}
	}

	void preparePipelines()
	{
		VkPushConstantRange pushRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PushConstants), 0);
		VkPipelineLayoutCreateInfo layoutInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));

		VkPipelineVertexInputStateCreateInfo vertexInput = vks::initializers::pipelineVertexInputStateCreateInfo();
		VkPipelineInputAssemblyStateCreateInfo inputAssembly = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterization = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blend = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_TRUE);
		blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.colorBlendOp = VK_BLEND_OP_ADD;
		blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blend.alphaBlendOp = VK_BLEND_OP_ADD;
		VkPipelineColorBlendStateCreateInfo colorBlend = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blend);
		VkPipelineDepthStencilStateCreateInfo depthStencil = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisample = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStates);
		VkPipelineRenderingCreateInfo renderingInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
			.colorAttachmentCount = 1,
			.pColorAttachmentFormats = &swapChain.colorFormat,
			.depthAttachmentFormat = depthFormat,
			.stencilAttachmentFormat = depthFormat,
		};
		std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
		VkGraphicsPipelineCreateInfo pipelineCI{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.pNext = &renderingInfo,
			.stageCount = static_cast<uint32_t>(stages.size()),
			.pStages = stages.data(),
			.pVertexInputState = &vertexInput,
			.pInputAssemblyState = &inputAssembly,
			.pViewportState = &viewportState,
			.pRasterizationState = &rasterization,
			.pMultisampleState = &multisample,
			.pDepthStencilState = &depthStencil,
			.pColorBlendState = &colorBlend,
			.pDynamicState = &dynamicState,
			.layout = pipelineLayout,
		};

		// The covers: depth tested and written
		stages[0] = loadShader(getShadersPath() + "coverflow/cover.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		stages[1] = loadShader(getShadersPath() + "coverflow/cover.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.cover));
		// The reflections: blended, depth tested but not written
		depthStencil.depthWriteEnable = VK_FALSE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.reflection));
		// The backdrop: no depth at all
		depthStencil.depthTestEnable = VK_FALSE;
		blend.blendEnable = VK_FALSE;
		stages[0] = loadShader(getShadersPath() + "coverflow/background.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		stages[1] = loadShader(getShadersPath() + "coverflow/background.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.background));
		// The game artwork layers: the backdrop's shaders, blended over it
		blend.blendEnable = VK_TRUE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.art));
	}

	void prepare() override
	{
		VulkanExampleBase::prepare();
		loadSettings();
#if defined(VK_EXAMPLE_PS5)
		if (!ps5.frameBudget) {
			ensureLauncher();
		}
#endif
		makeSounds();
		applyTheme(currentTheme(), false);
		startAudio();
		loadLibrary();
		loadSettings(); // the sections it names need the library's games
		loadSizes();
		loadPlayTime();
		loadStorage();
		for (auto &g : games) {
			g.section = sectionOf(g);
			g.subtitle = g.subtitle.empty() || g.subtitle == "PS5" || g.subtitle == "PS4" ? (g.section == "Other" ? "" : g.section) : g.subtitle;
			auto it = sizeCache.find(g.titleId);
			if (it != sizeCache.end() && g.bytes <= 0) {
				g.bytes = it->second;
			}
		}
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget) {
			// A test run's picture is the same whatever was chosen last time
			sortMode = 0;
			filterMode = 0;
		}
#endif
#if defined(PS5_HOST_REFERENCE)
		// PC reference only: COVERFLOW_TEST="sort=N filter=N info" pictures those states
		if (const char *t = std::getenv("COVERFLOW_TEST")) {
			const std::string test = t;
			if (test.find("sort=") != std::string::npos) {
				sortMode = std::clamp(std::atoi(t + test.find("sort=") + 5), 0, sortCount - 1);
			}
			if (test.find("filter=") != std::string::npos) {
				filterMode = std::clamp(std::atoi(t + test.find("filter=") + 7), 0, filterCount - 1);
			}
			menuOpen = test.find("menu") != std::string::npos;
			if (test.find("category") != std::string::npos) {
				categoryOpen = true;
				categoryIndex = 3;
			}
			settingsOpen = test.find("settings") != std::string::npos;
			if (test.find("launcher") != std::string::npos) {
				launcherPanel = true;
				logLines = { "cflaunch 7: listening", "ps5vkctl: command list", "cflaunch: listed 49 titles", "cflaunch: PPSA03541 played 5400 s (total 50400 s)" };
				launcherStatus = "running (version 7)";
			}
			if (test.find("notes") != std::string::npos) {
				updateNotes = "What's new\n- Game artwork behind the covers\n- Play time and Recently played\n- Grid view on L3\n- Collections and themes";
			}
			if (test.find("grid") != std::string::npos) {
				settings.grid = true;
			}
			if (test.find("letter") != std::string::npos) {
				letterShown = "R";
				letterTimer = 30.0f;
			}
			if (test.find("collections") != std::string::npos) {
				collectionsOpen = true;
				collectionMembers["CUSA00001"] = 1 | 4;
			}
			if (test.find("update") != std::string::npos) {
				updateDialog = true;
				updateTag = "v14";
				settings.updateRepo = "example/coverflow";
			}
			if (test.find("toast") != std::string::npos) {
				showMessage("Old Rally added to favourites", 30.0f);
			}
			if (test.find("fav=") != std::string::npos) {
				favourites.insert(test.substr(test.find("fav=") + 4, 9));
			}
		}
#endif
		rebuildView();
		// Back on the game that was selected last time
		for (int k = 0; k < (int)view.size(); k++) {
			if (!settings.lastGame.empty() && games[view[k]].titleId == settings.lastGame) {
				selected = k;
				position = (float)k;
			}
		}
		installEmbedded();
		setupDescriptors();
		preparePipelines();
#if defined(VK_EXAMPLE_PS5)
		// A test run's picture shows covers on both sides of the selection
		if (ps5.frameBudget && view.size() > 4) {
			selected = 4;
			position = 4.0f;
		}
#if defined(PS5_HOST_REFERENCE)
		if (const char *t = std::getenv("COVERFLOW_TEST")) {
			const std::string test = t;
			if (test.find("select=") != std::string::npos) {
				const std::string id = test.substr(test.find("select=") + 7, 9);
				for (int k = 0; k < (int)view.size(); k++) {
					if (games[view[k]].titleId == id) {
						selected = k;
						position = (float)k;
					}
				}
			}
		}
#endif
#endif
		gridScroll = (float)(selected / gridCols);
		startCoverLoader();
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget) {
			// A test run's picture has every cover: wait for them here
			for (int wait = 0; wait < 2000; wait++) {
				uploadCovers();
				if (coversLoaded >= (int)games.size()) {
					break;
				}
				usleep(10000);
			}
			loadingFade = 0.0f;
			introTime = 10.0f;
		} else {
			pad_light_bar((uint8_t)(theme.accent.r * 255), (uint8_t)(theme.accent.g * 255), (uint8_t)(theme.accent.b * 255));
		}
#endif
		prepared = true;
	}
	float introTime{ 0.0f }; // seconds since the first frame: the fade-in
	float menuAnim{ 0.0f }, settingsAnim{ 0.0f }, dialogAnim{ 0.0f }, loadingAnim{ 1.0f };

	// ---------------------------------------------------------------- layout

	// Where cover i stands, from its offset to the sliding position
	glm::mat4 coverModel(float offset) const
	{
		const float a = std::abs(offset);
		const float s = std::clamp(offset, -1.0f, 1.0f);
		const float x = a < 1.0f ? offset * sideOffset : (offset > 0 ? 1.0f : -1.0f) * (sideOffset + (a - 1.0f) * sideSpacing);
		const float z = (1.0f - std::min(a, 1.0f)) * centrePop;
		glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z));
		return glm::rotate(m, glm::radians(-s * theme.sideAngle), glm::vec3(0.0f, 1.0f, 0.0f));
	}

	void updateCamera()
	{
		// Y up, the floor at y = 0; the projection flips Y for Vulkan's clip space
		const float aspect = (float)width / (float)height;
		projection = glm::perspective(glm::radians(30.0f), aspect, 0.1f, 50.0f);
		projection[1][1] *= -1.0f;
		if (settings.grid) {
			// The grid: straight on
			view3d = glm::lookAt(glm::vec3(0.0f, 0.0f, 6.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		} else {
			view3d = glm::lookAt(glm::vec3(0.0f, 0.78f, 4.6f), glm::vec3(0.0f, 0.36f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}
	}

	// ---------------------------------------------------------------- launching

	void logLaunch(const std::string &line)
	{
		std::cerr << "Cover Flow: " << line << "\n";
		std::ofstream log(appRoot() + "launch.log", std::ios::app);
		if (log) {
			log << line << "\n";
		}
	}

	void showMessage(const std::string &text, float seconds)
	{
		message = text;
		messageTimer = seconds;
	}

	void launch(const Game &game)
	{
		if (game.titleId.empty()) {
			showMessage(game.title + ": no title id (add it as the 4th field in games.txt)", 5.0f);
			return;
		}
		const std::string self = ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId;
		logLaunch("launch " + game.titleId + " (" + game.title + ") through the launcher");
		const std::string answer = trim(askLauncher("switch " + self + " " + game.titleId + (settings.returnAfter ? " return" : "")));
		logLaunch("  launcher: " + (answer.empty() ? std::string("no answer on port 9112") : answer));
		if (answer.rfind("ok", 0) == 0) {
			playSound(1);
			rememberSelection();
			// The launcher starts the game once Cover Flow has gone
			showMessage("Starting " + game.title + "...", 10.0f);
#if defined(VK_EXAMPLE_PS5)
			pad_vibrate(0.0f, 0.0f);
#endif
			quit = true;
		} else if (answer.empty()) {
			showMessage("Load the Cover Flow launcher payload (cflaunch.elf) first, then press CROSS again", 6.0f);
		} else {
			showMessage("Launcher: " + answer, 8.0f);
		}
	}

	// ---------------------------------------------------------------- settings

	// The settings that override the theme
	void applySettings()
	{
		musicVolume.store(settings.volume >= 0 ? settings.volume : theme.volume);
		if (settings.reflection >= 0) {
			theme.reflection = settings.reflection;
		}
		if (settings.angle >= 0) {
			theme.sideAngle = settings.angle;
		}
	}

	static std::string percent(float v)
	{
		return std::to_string((int)std::lround(v * 100.0f)) + "%";
	}

	// The settings lines, in order (the groups start at the first of each)
	enum SettingId { S_RETURN, S_GRID, S_ART, S_THEME, S_MUSIC, S_SOUNDS, S_REFLECT, S_ANGLE, S_UPDATE_CHECK, S_UPDATE_SOURCE,
		S_CHECK_NOW, S_LAUNCHER, S_BACKUP, S_RESTORE, S_QUIT_WITH, S_QUIT, S_COUNT };

	// Each settings line: its label and its value ("" for actions)
	std::vector<std::pair<std::string, std::string>> settingsItems() const
	{
		const float volume = settings.volume >= 0 ? settings.volume : theme.volume;
		static const char *quitShort[4] = { "OPTIONS + SHARE", "L1 + R1 + OPTIONS", "Hold CIRCLE", "Settings only" };
		auto onOff = [](bool b) { return std::string(b ? "On" : "Off"); };
		return {
			{ "Return to Cover Flow after a game", onOff(settings.returnAfter) },
			{ "Grid view  (L3)", onOff(settings.grid) },
			{ "Game artwork behind the covers", onOff(settings.artBackgrounds) },
			{ "Theme", currentTheme() },
			{ "Music volume", musicFrames.empty() ? std::string("No music in theme") : percent(volume) },
			{ "Sounds", onOff(settings.sounds) },
			{ "Reflections", percent(theme.reflection) },
			{ "Cover angle", std::to_string((int)std::lround(theme.sideAngle)) + "\xC2\xB0" },
			{ "Check for updates on start", onOff(settings.updateCheck) },
			{ "Update source", "github.com/" + settings.updateRepo },
			{ "Check for updates now", "" },
			{ "Launcher", launcherStatus },
			{ "Back up settings to USB", "" },
			{ "Restore settings from USB", "" },
			{ "Quit with", quitShort[settings.quitWith] },
			{ "Quit Cover Flow", "" },
		};
	}

	// Which settings lines change with LEFT / RIGHT
	static bool settingAdjustable(int index)
	{
		return index <= S_ANGLE || index == S_UPDATE_CHECK || index == S_QUIT_WITH;
	}

	// LEFT/RIGHT (delta) or CROSS (delta 0) on a settings line
	void settingsChange(int index, int delta)
	{
		const int step = delta == 0 ? 1 : delta;
		const std::string self = ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId;
		switch (index) {
		case S_RETURN:
			settings.returnAfter = !settings.returnAfter;
			break;
		case S_GRID:
			settings.grid = !settings.grid;
			gridScroll = (float)(selected / gridCols);
			break;
		case S_ART:
			settings.artBackgrounds = !settings.artBackgrounds;
			break;
		case S_THEME: {
			const std::vector<std::string> names = themeNames();
			int at = 0;
			for (int i = 0; i < (int)names.size(); i++) {
				if (names[i] == currentTheme()) {
					at = i;
				}
			}
			at = (at + step + (int)names.size()) % (int)names.size();
			settings.themeName = names[at];
			settings.themeOn = true;
			applyTheme(names[at], true);
			showMessage("Theme: " + names[at] + (themeFolder(names[at]).empty() ? "" : "  (its font shows from the next start)"), 3.0f);
			break;
		}
		case S_MUSIC:
			if (!musicFrames.empty()) {
				const float v = settings.volume >= 0 ? settings.volume : theme.volume;
				settings.volume = std::clamp(std::round((v + 0.1f * step) * 10.0f) / 10.0f, 0.0f, 1.0f);
			}
			break;
		case S_SOUNDS:
			settings.sounds = !settings.sounds;
			startAudio();
			break;
		case S_REFLECT:
			settings.reflection = std::clamp(std::round((theme.reflection + 0.1f * step) * 10.0f) / 10.0f, 0.0f, 1.0f);
			break;
		case S_ANGLE:
			settings.angle = std::clamp(theme.sideAngle + 5.0f * step, 30.0f, 85.0f);
			break;
		case S_UPDATE_CHECK:
			settings.updateCheck = !settings.updateCheck;
			break;
		case S_UPDATE_SOURCE:
			if (delta == 0) {
				showMessage("To change it: update_repo = owner/repo in /data/homebrew/" + self + "/coverflow.ini", 6.0f);
			}
			return;
		case S_CHECK_NOW:
			if (delta == 0) {
				checkForUpdate(true);
			}
			return;
		case S_LAUNCHER:
			if (delta == 0) {
				openLauncherPanel();
			}
			return;
		case S_BACKUP:
		case S_RESTORE:
			if (delta == 0 && !toolJob) {
				saveSettings();
				toolRestore = index == S_RESTORE;
				toolJob = askInBackground((toolRestore ? "restore " : "backup ") + self, 20);
				showMessage(toolRestore ? "Restoring from USB..." : "Backing up to USB...", 20.0f);
			}
			return;
		case S_QUIT_WITH:
			settings.quitWith = (settings.quitWith + (delta < 0 ? 3 : 1)) % 4;
			showMessage(std::string("Quit with: ") + quitNames[settings.quitWith], 3.0f);
			break;
		case S_QUIT:
			if (delta == 0) {
				quit = true;
			}
			return;
		}
		playSound(1);
		applySettings();
		saveSettings();
	}

	// ---- backup, restore and the launcher panel

	std::shared_ptr<Pending> toolJob, logJob;
	bool toolRestore{ false };
	bool launcherPanel{ false };
	std::vector<std::string> logLines;

	void openLauncherPanel()
	{
		launcherPanel = true;
		logLines = { "Reading the launcher's log..." };
		logJob = askInBackground("log", 5);
		const int running = launcherRunning();
		launcherStatus = running ? "running (version " + std::to_string(running) + ")" : "not running";
	}

	void pollTools()
	{
		std::string answer;
		if (finished(toolJob, answer)) {
			toolJob.reset();
			if (answer.rfind("ok ", 0) == 0) {
				if (toolRestore) {
					// Start again with what was restored
					showMessage("Restored. Cover Flow will close - open it again to use the restored settings.", 8.0f);
					restoredRestart = true;
					closeAfterUpdate = 5.0f;
				} else {
					showMessage("Backed up to " + answer.substr(3, answer.rfind(' ') - 3), 4.0f);
				}
			} else {
				showMessage((toolRestore ? "Restore: " : "Backup: ") + (answer.empty() ? std::string("the launcher did not answer") : answer.substr(answer.rfind("err ", 0) == 0 ? 4 : 0)), 5.0f);
				playSound(3);
			}
		}
		if (finished(logJob, answer)) {
			logJob.reset();
			logLines.clear();
			std::stringstream ss(answer);
			std::string line;
			while (std::getline(ss, line)) {
				if (line != "ok log" && line != "end") {
					logLines.push_back(line);
				}
			}
			if (logLines.empty()) {
				logLines.push_back(answer.empty() ? "The launcher did not answer" : "The log is empty");
			}
		}
	}
	bool restoredRestart{ false };

	// ---------------------------------------------------------------- updates

	// ---- downloading from inside Cover Flow

	// GET a URL into a string and/or a file, hashing as it goes: the HTTP status,
	// or -1 with the console's error code in 'code'
	static int webGet(const std::string &url, std::string *out, FILE *file, Sha256 *hash, uint32_t &code)
	{
#if defined(VK_EXAMPLE_PS5) && !defined(PS5_HOST_REFERENCE)
		static std::mutex lock;
		static int tmpl = -1;
		std::lock_guard<std::mutex> guard(lock);
		if (tmpl < 0) {
			(void)cf_sceNetInit();
			const int net = cf_sceNetPoolCreate("coverflow", 64 * 1024, 0);
			const int ssl = cf_sceSslInit(512 * 1024);
			const int http = net >= 0 && ssl >= 0 ? cf_sceHttp2Init(net, ssl, 512 * 1024, 2) : -1;
			tmpl = http >= 0 ? cf_sceHttp2CreateTemplate(http, (std::string("CoverFlow/") + coverFlowVersion).c_str(), 3, 1) : -1;
			std::cerr << "Cover Flow: https net=" << net << " ssl=" << ssl << " http2=" << http << " template=" << tmpl << "\n";
			if (tmpl < 0) {
				code = (uint32_t)(http < 0 ? http : tmpl);
				return -1;
			}
		}
		const int request = cf_sceHttp2CreateRequestWithURL(tmpl, "GET", url.c_str(), 0);
		if (request < 0) {
			code = (uint32_t)request;
			return -1;
		}
		int status = -1;
		const int sent = cf_sceHttp2SendRequest(request, nullptr, 0);
		if (sent != 0 || cf_sceHttp2GetStatusCode(request, &status) != 0) {
			cf_sceHttp2DeleteRequest(request);
			code = (uint32_t)sent;
			std::cerr << "Cover Flow: GET " << url << " failed: 0x" << std::hex << (uint32_t)sent << std::dec << "\n";
			return -1;
		}
		if (status == 200) {
			char buffer[16384];
			int got;
			while ((got = cf_sceHttp2ReadData(request, buffer, sizeof(buffer))) > 0) {
				if (hash) {
					hash->update(buffer, (size_t)got);
				}
				if (file && std::fwrite(buffer, 1, (size_t)got, file) != (size_t)got) {
					status = -3;
					break;
				}
				if (out) {
					out->append(buffer, (size_t)got);
				}
			}
		}
		cf_sceHttp2DeleteRequest(request);
		std::cerr << "Cover Flow: GET " << url << " -> " << status << "\n";
		return status;
#else
		(void)url;
		(void)out;
		(void)file;
		(void)hash;
		code = 0;
		return -1; // the PC reference build does not download
#endif
	}

	static std::string hexCode(uint32_t code)
	{
		char s[16];
		std::snprintf(s, sizeof(s), "0x%08x", code);
		return s;
	}

	static int versionNumber(const std::string &tag)
	{
		return std::atoi(tag.c_str() + (tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? 1 : 0));
	}

	// The newest release's tag and its part of GitHub's JSON, or an error. The
	// "latest" release first; when there is none (every release marked as a
	// pre-release), the highest vN among the most recent releases
	static bool latestRelease(const std::string &repo, std::string &tag, std::string &json, std::string &error)
	{
		uint32_t code = 0;
		const int status = webGet("https://api.github.com/repos/" + repo + "/releases/latest", &json, nullptr, nullptr, code);
		if (status == 200) {
			tag = jsonString(json, "tag_name");
			if (!tag.empty()) {
				return true;
			}
		}
		if (status == -1) {
			error = "could not connect (" + hexCode(code) + ")";
			return false;
		}
		std::string list;
		const int listed = webGet("https://api.github.com/repos/" + repo + "/releases?per_page=10", &list, nullptr, nullptr, code);
		if (listed != 200) {
			error = "GitHub answered " + std::to_string(status == 200 ? listed : status);
			return false;
		}
		// Each release's part runs from its "tag_name" to the next one's
		std::vector<size_t> starts;
		for (size_t at = 0; (at = list.find("\"tag_name\"", at)) != std::string::npos; at += 10) {
			starts.push_back(at);
		}
		int best = -1;
		for (size_t i = 0; i < starts.size(); i++) {
			const std::string t = jsonString(list, "tag_name", starts[i]);
			if (best < 0 || versionNumber(t) > versionNumber(tag)) {
				best = (int)i;
				tag = t;
			}
		}
		if (best < 0) {
			error = "the repository has no releases";
			return false;
		}
		const size_t from = starts[best], to = (size_t)best + 1 < starts.size() ? starts[best + 1] : list.size();
		json = list.substr(from, to - from);
		return true;
	}

	// A release's description, with its line breaks, for the update dialog
	static std::string releaseNotes(const std::string &json)
	{
		const size_t k = json.find("\"body\"");
		if (k == std::string::npos) {
			return "";
		}
		size_t q = json.find(':', k);
		if (q == std::string::npos) {
			return "";
		}
		q = json.find_first_not_of(" \t\r\n", q + 1);
		if (q == std::string::npos || json[q] != '"') {
			return ""; // null: no description
		}
		std::string out;
		for (size_t i = q + 1; i < json.size() && json[i] != '"'; i++) {
			if (json[i] == '\\' && i + 1 < json.size()) {
				const char e = json[++i];
				if (e == 'n') {
					out += '\n';
				} else if (e == 'r') {
				} else if (e == 'u' && i + 4 < json.size()) {
					i += 4; // symbols the font cannot draw anyway
				} else {
					out += e;
				}
			} else {
				out += json[i];
			}
			if (out.size() > 4000) {
				break;
			}
		}
		return out;
	}

	static std::string assetUrl(const std::string &json, const std::string &name)
	{
		for (size_t at = 0; (at = json.find("\"browser_download_url\"", at)) != std::string::npos; at += 22) {
			const std::string url = jsonString(json, "browser_download_url", at);
			if (url.size() > name.size() && url[url.size() - name.size() - 1] == '/' &&
				url.compare(url.size() - name.size(), name.size(), name) == 0) {
				return url;
			}
		}
		return "";
	}

	static bool safePath(const std::string &p)
	{
		if (p.empty() || p[0] == '/' || p.size() > 160 || p.find("..") != std::string::npos) {
			return false;
		}
		for (char c : p) {
			if (!(c == '/' || c == '-' || c == '_' || c == '.' || std::isalnum((unsigned char)c))) {
				return false;
			}
		}
		return true;
	}

	// The check, from inside Cover Flow: "ok <tag>" or "err ..."
	static std::string appCheck(const std::string &repo)
	{
		std::string tag, json, error;
		if (!latestRelease(repo, tag, json, error)) {
			return "err " + error;
		}
		if (assetUrl(json, "SHA256SUMS").empty() || assetUrl(json, "eboot.bin").empty()) {
			return "err release " + tag + " lacks SHA256SUMS or eboot.bin";
		}
		return "ok " + tag + "\n" + releaseNotes(json);
	}

	// The update, from inside Cover Flow: every file SHA256SUMS lists, each
	// checked; only when all match are they put in place. "ok <tag> <n>" or "err ..."
	static std::string appApply(const std::string &repo, const std::string &root)
	{
		std::string tag, json, error, sums;
		if (!latestRelease(repo, tag, json, error)) {
			return "err " + error;
		}
		uint32_t code = 0;
		const std::string sumsUrl = assetUrl(json, "SHA256SUMS");
		if (sumsUrl.empty() || webGet(sumsUrl, &sums, nullptr, nullptr, code) != 200) {
			return "err could not download SHA256SUMS";
		}
		std::vector<std::pair<std::string, std::string>> files; // path, sha256
		std::stringstream ss(sums);
		std::string line;
		bool eboot = false;
		while (std::getline(ss, line)) {
			std::stringstream ls(line);
			std::string hex, path;
			if (!(ls >> hex >> path) || hex.size() != 64) {
				continue;
			}
			if (path[0] == '*') {
				path = path.substr(1);
			}
			if (!safePath(path)) {
				return "err SHA256SUMS names an unsafe path: " + path;
			}
			files.push_back({ path, lower(hex) });
			eboot |= path == "eboot.bin";
		}
		if (!eboot) {
			return "err SHA256SUMS does not list eboot.bin";
		}
		auto removeNew = [&](size_t upTo) {
			for (size_t j = 0; j <= upTo && j < files.size(); j++) {
				std::remove((root + files[j].first + ".new").c_str());
			}
		};
		for (size_t i = 0; i < files.size(); i++) {
			std::string asset = files[i].first;
			for (size_t at; (at = asset.find('/')) != std::string::npos;) {
				asset.replace(at, 1, "__");
			}
			const std::string target = root + files[i].first + ".new";
			for (size_t at = root.size(); (at = target.find('/', at + 1)) != std::string::npos;) {
				mkdir(target.substr(0, at).c_str(), 0777);
			}
			const std::string url = assetUrl(json, asset);
			FILE *file = url.empty() ? nullptr : std::fopen(target.c_str(), "wb");
			Sha256 hash;
			const int status = file ? webGet(url, nullptr, file, &hash, code) : -1;
			if (file) {
				std::fclose(file);
			}
			if (status != 200 || hash.hex() != files[i].second) {
				removeNew(i);
				return "err " + files[i].first + ": " + (status != 200 ? "could not download it" : "SHA-256 does not match") +
					" - nothing was changed";
			}
		}
		for (const auto &f : files) {
			if (std::rename((root + f.first + ".new").c_str(), (root + f.first).c_str()) != 0) {
				return "err could not replace " + f.first;
			}
		}
		return "ok " + tag + " " + std::to_string(files.size());
	}

	// Which way the last check reached GitHub: 1 from inside Cover Flow, 2 through the launcher
	int updateRoute{ 0 };

	void checkForUpdate(bool byHand)
	{
		if (settings.updateRepo.empty()) {
			if (byHand) {
				showMessage("No update source: set update_repo = owner/repo in coverflow.ini", 5.0f);
			}
			return;
		}
		if (updateCheckJob) {
			return;
		}
		// From inside Cover Flow first; through the launcher if that cannot connect
		auto job = std::make_shared<Pending>();
		const std::string repo = settings.updateRepo;
		std::thread([job, repo]() {
			std::string answer = appCheck(repo);
			std::string route = "1";
			if (answer.rfind("ok", 0) != 0) {
				const std::string viaLauncher = trim(askLauncher("update-check " + repo, 1u << 20, 30));
				if (viaLauncher.rfind("ok", 0) == 0) {
					answer = viaLauncher;
					route = "2";
				} else {
					answer = "err from Cover Flow: " + answer.substr(answer.rfind("err ", 0) == 0 ? 4 : 0) + "; from the launcher: " +
						(viaLauncher.empty() ? std::string("no answer") : viaLauncher.substr(viaLauncher.rfind("err ", 0) == 0 ? 4 : 0));
				}
			}
			std::lock_guard<std::mutex> lock(job->mutex);
			job->answer = route + answer;
			job->done = true;
		}).detach();
		updateCheckJob = job;
		checkByHand = byHand;
		if (byHand) {
			showMessage("Checking " + settings.updateRepo + "...", 30.0f);
		}
	}
	bool checkByHand{ false };

	void pollUpdates(float dt)
	{
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget) {
			return;
		}
#endif
		if (updateCheckDelay > 0.0f) {
			updateCheckDelay -= dt;
			if (updateCheckDelay <= 0.0f && settings.updateCheck) {
				checkForUpdate(false);
			}
		}
		std::string answer;
		if (finished(updateCheckJob, answer)) {
			updateCheckJob.reset();
			updateRoute = answer.empty() ? 0 : answer[0] - '0';
			answer = answer.empty() ? answer : answer.substr(1);
			if (answer.rfind("ok ", 0) == 0) {
				const size_t nl = answer.find('\n');
				updateTag = trim(answer.substr(3, nl == std::string::npos ? std::string::npos : nl - 3));
				updateNotes = nl == std::string::npos ? "" : answer.substr(nl + 1);
				// Only a newer version: "v12" is newer than "v11" (the numbers after the v)
				auto number = [](const std::string &tag) { return std::atoi(tag.c_str() + (tag.size() && (tag[0] == 'v' || tag[0] == 'V') ? 1 : 0)); };
				if (number(updateTag) > number(coverFlowVersion)) {
					updateDialog = true;
					updateChoice = 0;
					messageTimer = 0.0f;
				} else if (checkByHand) {
					showMessage(std::string("Cover Flow is up to date (") + coverFlowVersion + (updateRoute == 1 ? ", checked by Cover Flow)" : ", checked by the launcher)"), 4.0f);
				}
			} else if (checkByHand) {
				showMessage("Update check: " + (answer.empty() ? std::string("the launcher did not answer") : answer), 6.0f);
			}
		}
		if (finished(updateApplyJob, answer)) {
			updateApplyJob.reset();
			updating = false;
			if (answer.rfind("ok", 0) == 0) {
				// Installed. Restarting a title from inside itself froze the console
				// (v13), so Cover Flow closes on its own and is opened again by hand.
				showMessage("Updated to " + updateTag + ". Cover Flow will close - open it again from the home screen.", 8.0f);
				closeAfterUpdate = 5.0f;
			} else {
				showMessage("Update failed: " + (answer.empty() ? std::string("no answer from the launcher") : answer), 8.0f);
			}
		}
	}

	void startUpdate()
	{
		const std::string self = ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId;
		if (updateRoute == 2) {
			updateApplyJob = askInBackground("update-apply " + settings.updateRepo + " " + self, 600);
		} else {
			auto job = std::make_shared<Pending>();
			const std::string repo = settings.updateRepo, root = appRoot();
			std::thread([job, repo, root]() {
				const std::string answer = appApply(repo, root);
				std::lock_guard<std::mutex> lock(job->mutex);
				job->answer = answer;
				job->done = true;
			}).detach();
			updateApplyJob = job;
		}
		updating = true;
		updateDialog = false;
		showMessage("Downloading " + updateTag + " and checking it...", 600.0f);
	}

	// ---------------------------------------------------------------- sizes

	void loadSizes()
	{
		std::ifstream f(appRoot() + "covers/auto/sizes.txt");
		std::string id;
		long long bytes;
		while (f >> id >> bytes) {
			if (isTitleId(id) && bytes > 0) {
				sizeCache[id] = bytes;
			}
		}
	}

	void saveSizes()
	{
		std::string out;
		for (const auto &[id, bytes] : sizeCache) {
			out += id + " " + std::to_string(bytes) + "\n";
		}
		writeFile(appRoot() + "covers/auto/sizes.txt", out);
	}

	// Ask the launcher for a game's size without holding up the frame
	void requestSize(const Game &g)
	{
		if (g.titleId.empty() || g.bytes > 0 || sizePending.count(g.titleId) || listSource != "live") {
			return;
		}
		sizePending.insert(g.titleId);
		const std::string id = g.titleId;
		std::shared_ptr<SizeResults> results = sizeResults;
		std::thread([results, id]() {
			const std::string answer = trim(askLauncher("size " + id));
			const long long bytes = answer.rfind("ok ", 0) == 0 ? std::atoll(answer.c_str() + 3) : -1;
			std::lock_guard<std::mutex> lock(results->mutex);
			results->done[id] = bytes;
		}).detach();
	}

	// Measurements that have finished, into the games and the saved list
	void collectSizes()
	{
		std::map<std::string, long long> done;
		{
			std::lock_guard<std::mutex> lock(sizeResults->mutex);
			done.swap(sizeResults->done);
		}
		bool changed = false;
		for (const auto &[id, bytes] : done) {
			for (auto &g : games) {
				if (g.titleId == id) {
					g.bytes = bytes > 0 ? bytes : -1; // -1: measured, nothing found
				}
			}
			if (bytes > 0) {
				sizeCache[id] = bytes;
				changed = true;
			}
		}
		if (changed) {
			saveSizes();
		}
	}

	// ---------------------------------------------------------------- the game menu

	std::vector<std::string> menuItems(const Game &g) const
	{
		return { "Start game", isFavourite(g) ? "Remove from favourites" : "Add to favourites",
			"Category:  " + categoryName(g.section) + (sectionOverride.count(g.titleId) ? "" : "  (automatic)"),
			"Collections:  " + collectionSummary(g),
			isHidden(g) ? "Show in Cover Flow" : "Hide from Cover Flow",
			"Dump Game / App", "Close" };
	}

	std::string collectionSummary(const Game &g) const
	{
		std::string out;
		int n = 0;
		for (int c = 0; c < collectionCount; c++) {
			if (inCollection(g, c)) {
				out += (n++ ? ", " : "") + collectionNames[c];
			}
		}
		return n ? out : "none";
	}

	void toggleCollection(int c)
	{
		const Game *g = current();
		if (!g || g->titleId.empty()) {
			showMessage("Only games with a title id can join a collection", 3.0f);
			return;
		}
		int &bits = collectionMembers[g->titleId];
		bits ^= 1 << c;
		showMessage(g->title + ((bits >> c & 1) ? " added to " : " removed from ") + collectionNames[c], 2.5f);
		saveSettings();
		if (filterMode >= baseFilters) {
			rebuildView();
		}
	}

	void menuAction(int index)
	{
		const Game *g = current();
		if (!g) {
			menuOpen = false;
			return;
		}
		const std::string id = g->titleId, name = g->title;
		const Game *gp = g;
		(void)gp;
		switch (index) {
		case 0:
			menuOpen = false;
			launch(*g);
			break;
		case 1:
			if (id.empty()) {
				showMessage("Only games with a title id can be favourites", 3.0f);
			} else if (favourites.erase(id)) {
				showMessage(name + " removed from favourites", 2.5f);
			} else {
				favourites.insert(id);
				showMessage(name + " added to favourites", 2.5f);
			}
			saveSettings();
			rebuildView();
			break;
		case 2: {
			if (id.empty()) {
				showMessage("Only games with a title id can change category", 3.0f);
				break;
			}
			// Open the list on the current choice
			categoryOpen = true;
			categoryIndex = 0;
			auto it = sectionOverride.find(id);
			for (int i = 1; i < 5 && it != sectionOverride.end(); i++) {
				if (it->second == categoryKeys[i]) {
					categoryIndex = i;
				}
			}
			break;
		}
		case 3:
			collectionsOpen = true;
			collectionIndex = 0;
			break;
		case 4:
			if (id.empty()) {
				showMessage("Only games with a title id can be hidden", 3.0f);
			} else if (hidden.erase(id)) {
				showMessage(name + " is shown again", 2.5f);
			} else {
				hidden.insert(id);
				showMessage(name + " hidden - TOUCHPAD to 'Hidden games' to bring it back", 4.0f);
				menuOpen = false;
			}
			saveSettings();
			rebuildView();
			break;
		case 5:
			dumpOpen = true;
			dumpIndex = 0;
			dumpDestination = 0;
			dumpStage = 0;
			dumpProgress = 0.0f;
			break;
		default:
			menuOpen = false;
			break;
		}
	}

	// The letter a game is filed under: A-Z, or # for the rest
	std::string initialOf(int k) const
	{
		const std::string &t = games[view[k]].title;
		for (unsigned char c : t) {
			if (std::isalpha(c)) {
				return std::string(1, (char)std::toupper(c));
			}
			if (std::isdigit(c)) {
				return "#";
			}
		}
		return "#";
	}

	// R2: the first game of the next letter; L2: the first of this letter, or of the one before
	void jumpLetter(int dir)
	{
		if (view.empty()) {
			return;
		}
		int k = selected;
		const std::string here = initialOf(k);
		if (dir > 0) {
			while (k < (int)view.size() - 1 && initialOf(k) == here) {
				k++;
			}
		} else {
			int start = k;
			while (start > 0 && initialOf(start - 1) == here) {
				start--;
			}
			if (start < k) {
				k = start;
			} else if (start > 0) {
				const std::string before = initialOf(start - 1);
				k = start - 1;
				while (k > 0 && initialOf(k - 1) == before) {
					k--;
				}
			}
		}
		if (k != selected) {
			move(k - selected);
		}
		letterShown = initialOf(selected);
		letterTimer = 0.9f;
	}

	// Put the selected game in the category chosen from the list
	void applyCategory(int choice)
	{
		const Game *g = current();
		categoryOpen = false;
		if (!g || g->titleId.empty()) {
			return;
		}
		const std::string id = g->titleId, name = g->title;
		if (choice == 0) {
			sectionOverride.erase(id);
		} else {
			sectionOverride[id] = categoryKeys[choice];
		}
		for (auto &game : games) {
			if (game.titleId == id) {
				const std::string old = game.section;
				game.section = sectionOf(game);
				if (game.subtitle == old) {
					game.subtitle = game.section == "Other" ? "" : game.section;
				}
			}
		}
		saveSettings();
		showMessage(name + " is now in " + categoryName(sectionOf(*g)) + (choice == 0 ? " (automatic)" : ""), 3.0f);
		rebuildView();
	}

	// ---------------------------------------------------------------- input

	void move(int delta)
	{
		if (view.empty()) {
			return;
		}
		const int next = std::clamp(selected + delta, 0, (int)view.size() - 1);
		if (next != selected) {
			selected = next;
			playSound(0);
#if defined(VK_EXAMPLE_PS5)
			pad_vibrate(0.0f, 0.15f);
			vibrateTimer = 0.04f;
#endif
		}
	}

	void handleInput(float dt)
	{
		if (closeAfterUpdate > 0.0f) {
			return; // closing after an update or restore: nothing more to change
		}
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget) {
			return; // a test run is the same whoever holds the pad
		}
		const struct pad &p = ps5_pad();
		if (vibrateTimer > 0.0f) {
			vibrateTimer -= dt;
			if (vibrateTimer <= 0.0f) {
				pad_vibrate(0.0f, 0.0f);
			}
		}
		// Quitting by button: the chosen combination, held for half a second
		const uint32_t released = lastHeld & ~p.held;
		lastHeld = p.held;
		static const uint32_t combos[2] = { PAD_OPTIONS | padShare, PAD_L1 | PAD_R1 | PAD_OPTIONS };
		if (settings.quitWith < 2 && (p.held & combos[settings.quitWith]) == combos[settings.quitWith]) {
			optionsInCombo = true;
			comboHeld += dt;
			if (comboHeld >= 0.5f) {
				pad_vibrate(0.0f, 0.0f);
				quit = true;
			}
			return;
		}
		comboHeld = 0.0f;
		// OPTIONS opens and closes the settings when tapped on its own (on release,
		// so the start of a combination does not count)
		const bool optionsTap = (released & PAD_OPTIONS) && !optionsInCombo;
		if (released & PAD_OPTIONS) {
			optionsInCombo = false;
		}
		// The update dialog and the settings screen take the pad while open
		if (updateDialog) {
			if (p.pressed & (PAD_LEFT | PAD_RIGHT)) {
				updateChoice = 1 - updateChoice;
			}
			if (p.pressed & PAD_CROSS) {
				if (updateChoice == 0) {
					startUpdate();
				} else {
					updateDialog = false;
				}
			}
			if (p.pressed & PAD_CIRCLE) {
				updateDialog = false;
				circleHeld = -10.0f;
			}
			return;
		}
		if (launcherPanel) {
			if (p.pressed & PAD_CROSS) {
				ensureLauncher(true);
				openLauncherPanel();
				showMessage("Launcher: " + launcherStatus, 3.0f);
			}
			if (p.pressed & (PAD_CIRCLE | PAD_OPTIONS)) {
				launcherPanel = false;
				circleHeld = -10.0f;
				playSound(2);
			}
			return;
		}
		if (settingsOpen) {
			const int count = (int)settingsItems().size();
			if (p.pressed & PAD_UP) {
				settingsIndex = (settingsIndex + count - 1) % count;
				playSound(0);
			}
			if (p.pressed & PAD_DOWN) {
				settingsIndex = (settingsIndex + 1) % count;
				playSound(0);
			}
			if (p.pressed & PAD_LEFT) {
				settingsChange(settingsIndex, -1);
			}
			if (p.pressed & PAD_RIGHT) {
				settingsChange(settingsIndex, 1);
			}
			if (p.pressed & PAD_CROSS) {
				settingsChange(settingsIndex, 0);
			}
			if ((p.pressed & PAD_CIRCLE) || optionsTap) {
				settingsOpen = false;
				circleHeld = -10.0f;
				playSound(2);
			}
			return;
		}
		if (optionsTap) {
			playSound(1);
			settingsOpen = true;
			menuOpen = false;
			settingsIndex = 0;
			return;
		}
		// L3: the grid, or the row
		if (p.pressed & PAD_L3) {
			settings.grid = !settings.grid;
			saveSettings();
			gridScroll = (float)(selected / gridCols);
			showMessage(settings.grid ? "Grid view" : "Cover Flow view", 1.5f);
			playSound(1);
		}
		// L2 / R2: jump by letter
		const bool l2 = p.l2 > 0.6f, r2 = p.r2 > 0.6f;
		if (r2 && !r2Down) {
			jumpLetter(1);
		}
		if (l2 && !l2Down) {
			jumpLetter(-1);
		}
		l2Down = l2;
		r2Down = r2;
		// The right stick scrolls fast
		if (std::abs(p.right_x) > 0.5f) {
			stickTimer -= dt;
			if (stickTimer <= 0.0f) {
				move(p.right_x > 0 ? 1 : -1);
				stickTimer = 0.045f;
			}
		} else {
			stickTimer = 0.0f;
		}
		// In the grid, UP / DOWN and the stick move a row (not while a menu has them)
		if (settings.grid && !menuOpen) {
			int rows = 0;
			if (p.pressed & PAD_UP) {
				rows = -1;
			}
			if (p.pressed & PAD_DOWN) {
				rows = 1;
			}
			if (std::abs(p.left_y) > 0.6f || std::abs(p.right_y) > 0.6f) {
				const float y = std::abs(p.left_y) > std::abs(p.right_y) ? p.left_y : p.right_y;
				stickYTimer -= dt;
				if (stickYTimer <= 0.0f) {
					rows = y > 0 ? 1 : -1;
					stickYTimer = 0.18f;
				}
			} else {
				stickYTimer = 0.0f;
			}
			if (rows != 0) {
				const int target = std::clamp(selected + rows * gridCols, 0, (int)view.size() - 1);
				move(target - selected);
			}
		}
		// D-pad: a press moves one; held, it repeats after a pause
		int dir = 0;
		if (p.held & PAD_LEFT) {
			dir = -1;
		} else if (p.held & PAD_RIGHT) {
			dir = 1;
		} else if (std::abs(p.left_x) > 0.45f && (!settings.grid || std::abs(p.left_x) > std::abs(p.left_y))) {
			dir = p.left_x > 0 ? 1 : -1;
		}
		if (p.pressed & (PAD_LEFT | PAD_RIGHT)) {
			move(dir);
			repeatTimer = 0.38f;
		} else if (dir != 0) {
			repeatTimer -= dt;
			if (repeatTimer <= 0.0f) {
				move(dir);
				// The stick pushed further repeats faster
				const float push = std::abs(p.left_x) > 0.45f ? std::abs(p.left_x) : 0.6f;
				repeatTimer = 0.16f - 0.11f * std::clamp((push - 0.45f) / 0.55f, 0.0f, 1.0f);
			}
		} else {
			repeatTimer = 0.0f;
		}
		if (p.pressed & PAD_L1) {
			move(settings.grid ? -gridCols * 3 : -5);
		}
		if (p.pressed & PAD_R1) {
			move(settings.grid ? gridCols * 3 : 5);
		}
		if (p.pressed & PAD_SQUARE) {
			sortMode = (sortMode + 1) % sortCount;
			rebuildView();
			saveSettings();
			showMessage(std::string("Sorted by ") + sortNames[sortMode], 2.0f);
		}
		if (p.pressed & PAD_TOUCH_PAD) {
			// The next filter, skipping collections nothing is in
			do {
				filterMode = (filterMode + 1) % filterCount;
			} while (filterMode >= baseFilters && !collectionUsed(filterMode - baseFilters));
			rebuildView();
			saveSettings();
			showMessage(std::string("Showing ") + filterName(filterMode) + " (" + std::to_string(view.size()) + ")", 2.0f);
		}
		const Game *game = current();
		if ((p.pressed & PAD_TRIANGLE) && game) {
			menuOpen = !menuOpen;
			menuIndex = 0;
			categoryOpen = false;
			collectionsOpen = false;
			dumpOpen = false;
			playSound(menuOpen ? 1 : 2);
		}
		if (menuOpen && !game) {
			menuOpen = false;
		}
		if (menuOpen && dumpOpen) {
			// The external PS5 App Dumper reports real byte progress off the render thread.
			if (dumpStage == 2 && dumpJob) {
				bool done = false, success = false;
				std::string answer, stage;
				{
					std::lock_guard<std::mutex> lock(dumpJob->mutex);
					done = dumpJob->done;
					success = dumpJob->success;
					dumpProgress = dumpJob->progress;
					answer = dumpJob->answer;
					stage = dumpJob->stage;
				}
				dumpStatus = stage;
				if (done) {
					dumpResult = answer;
					dumpSucceeded = success;
					dumpQueued = dumpJob->queued;
					dumpProgress = dumpSucceeded ? (dumpQueued ? 0.0f : 1.0f) : dumpProgress;
					dumpJob.reset();
					dumpStage = 3;
					dumpIndex = 0;
				}
			}
			const int dumpItemCount = dumpStage == 0 ? 4 : (dumpStage == 1 || dumpStage == 3 ? 2 : 0);
			if (dumpItemCount > 0 && (p.pressed & PAD_UP)) {
				dumpIndex = (dumpIndex + dumpItemCount - 1) % dumpItemCount;
			}
			if (dumpItemCount > 0 && (p.pressed & PAD_DOWN)) {
				dumpIndex = (dumpIndex + 1) % dumpItemCount;
			}
			if (p.pressed & PAD_CROSS) {
				if (dumpStage == 0) {
					if (dumpIndex < 2) {
						dumpDestination = dumpIndex;
						showMessage(dumpDestination == 0 ? "Destination: internal storage" : "Destination: USB drive", 2.0f);
					} else if (dumpIndex == 2) {
						dumpStage = 1;
						dumpIndex = 0;
					} else {
						dumpOpen = false;
						circleHeld = -10.0f;
					}
				} else if (dumpStage == 1) {
					if (dumpIndex == 0) {
						const Game *selectedGame = current();
						if (!selectedGame || !isTitleId(selectedGame->titleId)) {
							dumpResult = "err selected item has no valid title ID";
							dumpSucceeded = false;
							dumpStage = 3;
						} else {
							dumpResult.clear();
							dumpSucceeded = false;
							dumpQueued = false;
							dumpProgress = 0.0f;
							dumpJob = std::make_shared<DumperProgress>();
							dumpStatus = "Preparing real PS5 dump";
							const auto job = dumpJob;
							const std::string titleId = selectedGame->titleId;
							const bool internal = dumpDestination == 0;
							const std::string root = appRoot();
							const std::string payloadPath = "/data/homebrew/" +
								(ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId) + "/ps5-app-dumper.elf";
							std::thread([job, titleId, internal, payloadPath, root]() {
								const std::string localPayload = std::ifstream(payloadPath, std::ios::binary).good()
									? payloadPath : root + "ps5-app-dumper.elf";
								VulkanExample::runDumperBackend(job, titleId, internal, localPayload);
							}).detach();
							dumpStage = 2;
						}
					} else {
						dumpStage = 0;
						dumpIndex = 0;
					}
				} else if (dumpStage == 3) {
					if (dumpIndex == 0) {
						dumpStage = 0;
						dumpIndex = 0;
					} else {
						dumpOpen = false;
						circleHeld = -10.0f;
					}
				}
			}
			if (p.pressed & PAD_CIRCLE) {
				if (dumpStage == 1 || dumpStage == 3) {
					dumpStage = 0;
					dumpIndex = 0;
				} else {
					dumpOpen = false;
				}
				circleHeld = -10.0f;
			}
		} else if (menuOpen && collectionsOpen) {
			if (p.pressed & PAD_UP) {
				collectionIndex = (collectionIndex + collectionCount - 1) % collectionCount;
			}
			if (p.pressed & PAD_DOWN) {
				collectionIndex = (collectionIndex + 1) % collectionCount;
			}
			if (p.pressed & PAD_CROSS) {
				toggleCollection(collectionIndex);
			}
			if (p.pressed & PAD_CIRCLE) {
				collectionsOpen = false;
				circleHeld = -10.0f;
			}
		} else if (menuOpen && categoryOpen) {
			if (p.pressed & PAD_UP) {
				categoryIndex = (categoryIndex + 4) % 5;
			}
			if (p.pressed & PAD_DOWN) {
				categoryIndex = (categoryIndex + 1) % 5;
			}
			if (p.pressed & PAD_CROSS) {
				applyCategory(categoryIndex);
			}
			if (p.pressed & PAD_CIRCLE) {
				categoryOpen = false;
				circleHeld = -10.0f;
			}
		} else if (menuOpen) {
			const int count = (int)menuItems(*game).size();
			if (p.pressed & PAD_UP) {
				menuIndex = (menuIndex + count - 1) % count;
			}
			if (p.pressed & PAD_DOWN) {
				menuIndex = (menuIndex + 1) % count;
			}
			if (p.pressed & PAD_CROSS) {
				menuAction(menuIndex);
			}
		} else if ((p.pressed & PAD_CROSS) && game && std::abs(selected - position) < 0.2f) {
			launch(*game);
		}
		if ((p.pressed & PAD_CIRCLE) && menuOpen && circleHeld > -5.0f) {
			menuOpen = false;
			circleHeld = -10.0f; // this press closed the menu: it does not count towards quitting
		}
		circleHeld = (p.held & PAD_CIRCLE) ? circleHeld + dt : 0.0f;
		if (settings.quitWith == 2 && circleHeld > 2.0f) {
			pad_vibrate(0.0f, 0.0f);
			quit = true;
		}
#endif
	}

	// ---------------------------------------------------------------- frame

	void buildCommandBuffer()
	{
		VkCommandBuffer cmd = drawCmdBuffers[currentBuffer];
		VkCommandBufferBeginInfo beginInfo = vks::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmd, &beginInfo));
		beginDynamicRendering(cmd);
		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmd, 0, 1, &scissor);
		const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		// The backdrop: the theme's picture or the glow
		const glm::vec4 floorClip = projection * view3d * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		const float horizon = settings.grid ? 0.93f : 0.5f + 0.5f * floorClip.y / floorClip.w;
		PushConstants pc{ glm::mat4(0.0f), glm::vec4(theme.accent, horizon), glm::vec4(theme.accent, clock) };
		pc.mvp[0][0] = theme.hasPicture ? 1.0f : 0.0f;
		pc.mvp[0][1] = theme.pictureAspect / ((float)width / (float)height);
		pc.mvp[0][2] = theme.dimming;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.background);
		VkDescriptorSet backdropSet = theme.hasPicture ? backgroundSet : placeholderSet;
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &backdropSet, 0, nullptr);
		vkCmdPushConstants(cmd, pipelineLayout, stages, 0, sizeof(pc), &pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);
		// The selected game's artwork over it, the last one fading out
		auto artLayer = [&](const std::string &id, float alpha) {
			Art *a = id.empty() ? nullptr : findArt(id);
			if (!a || alpha < 0.01f) {
				return;
			}
			PushConstants layer{ glm::mat4(0.0f), glm::vec4(theme.accent, horizon), glm::vec4(theme.accent, clock) };
			layer.mvp[0][0] = 2.0f;
			layer.mvp[0][1] = a->aspect / ((float)width / (float)height);
			layer.mvp[0][2] = 0.5f;
			layer.mvp[0][3] = alpha * 0.85f;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.art);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &a->set, 0, nullptr);
			vkCmdPushConstants(cmd, pipelineLayout, stages, 0, sizeof(layer), &layer);
			vkCmdDraw(cmd, 3, 1, 0, 0);
		};
		artLayer(artPrev, artPrevAlpha);
		artLayer(artShown, artAlpha);

		// The visible covers, farthest from the middle first
		std::vector<int> order;
		for (int k = 0; k < (int)view.size(); k++) {
			if (std::abs(k - position) < 9.0f) {
				order.push_back(k);
			}
		}
		std::sort(order.begin(), order.end(), [&](int a, int b) { return std::abs(a - position) > std::abs(b - position); });
		const glm::mat4 viewProjection = projection * view3d;
		auto draw = [&](const glm::mat4 &model, const glm::vec4 &params, VkDescriptorSet set) {
			pc.mvp = viewProjection * model;
			pc.params = params;
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
			vkCmdPushConstants(cmd, pipelineLayout, stages, 0, sizeof(pc), &pc);
			vkCmdDraw(cmd, 6, 1, 0, 0);
		};
		// Side covers a little darker, the far ones fading out
		auto brightnessAt = [](float a) { return 1.0f - 0.40f * std::min(a, 1.0f) - 0.05f * std::max(0.0f, a - 1.0f); };
		auto alphaAt = [](float a) { return std::clamp(8.5f - a, 0.0f, 1.0f); };

		if (settings.grid) {
			// The grid: rows of covers facing the screen, the selected one lifted and glowing
			auto cell = [&](int k, float &alpha, bool &sel) {
				const int row = k / gridCols, col = k % gridCols;
				const float d = row - gridScroll;
				sel = k == selected;
				alpha = std::clamp(2.3f - std::abs(d), 0.0f, 1.0f);
				const float size = 0.6f * (sel ? 1.1f : 1.0f);
				const float x = (col - (gridCols - 1) * 0.5f) * 0.78f, y = 0.28f - d * 0.74f;
				return glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(x, y - size * 0.5f, sel ? 0.15f : 0.0f)), glm::vec3(size, size, 1.0f));
			};
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.reflection);
			if (selected < (int)view.size()) {
				float alpha;
				bool sel;
				glm::mat4 glow = cell(selected, alpha, sel);
				glow = glm::scale(glm::translate(glow, glm::vec3(0.0f, -0.12f, -0.02f)), glm::vec3(1.25f, 1.25f, 1.0f));
				draw(glow, glm::vec4(0.0f, 1.0f, alpha, 3.0f), placeholderSet);
			}
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cover);
			for (int k = 0; k < (int)view.size(); k++) {
				if (std::abs(k / gridCols - gridScroll) > 2.6f) {
					continue;
				}
				float alpha;
				bool sel;
				const glm::mat4 model = cell(k, alpha, sel);
				draw(model, glm::vec4(0.0f, sel ? 1.0f : 0.78f, alpha, sel ? 1.0f : 0.0f), coverSet(games[view[k]]));
			}
			drawUI(cmd);
			endDynamicRendering(cmd);
			VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
			return;
		}

		// Blended, without depth writes: reflections, shadows on the floor, the glow
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.reflection);
		if (theme.reflection > 0.0f) {
			for (int k : order) {
				const float a = std::abs(k - position);
				draw(glm::scale(coverModel(k - position), glm::vec3(1.0f, -1.0f, 1.0f)),
					glm::vec4(theme.reflection, brightnessAt(a), alphaAt(a), 0.0f), coverSet(games[view[k]]));
			}
		}
		for (int k : order) {
			const float a = std::abs(k - position);
			glm::mat4 floor = coverModel(k - position);
			floor = glm::translate(floor, glm::vec3(0.0f, 0.003f, 0.0f));
			floor = glm::rotate(floor, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
			floor = glm::scale(floor, glm::vec3(1.25f, 0.7f, 1.0f));
			floor = glm::translate(floor, glm::vec3(0.0f, -0.5f, 0.0f));
			draw(floor, glm::vec4(0.0f, 1.0f, alphaAt(a), 2.0f), placeholderSet);
		}
		if (!view.empty()) {
			// The glow follows the row as it settles on the selection
			const float settle = std::clamp(1.0f - std::abs(selected - position) * 1.5f, 0.0f, 1.0f);
			glm::mat4 glow = coverModel(selected - position);
			glow = glm::translate(glow, glm::vec3(0.0f, -0.12f, -0.02f));
			glow = glm::scale(glow, glm::vec3(1.25f, 1.25f, 1.0f));
			draw(glow, glm::vec4(0.0f, 1.0f, settle, 3.0f), placeholderSet);
		}
		// The covers, farthest from the middle first, writing depth
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cover);
		for (int k : order) {
			const float a = std::abs(k - position);
			draw(coverModel(k - position), glm::vec4(0.0f, brightnessAt(a), alphaAt(a), (k == selected && a < 0.05f) ? 1.0f : 0.0f),
				coverSet(games[view[k]]));
		}

		drawUI(cmd);
		endDynamicRendering(cmd);
		VK_CHECK_RESULT(vkEndCommandBuffer(cmd));
	}

	void render() override
	{
		if (!prepared) {
			return;
		}
		VulkanExampleBase::prepareFrame();
		const float dt = std::min(frameTimer, 0.1f);
		clock += dt;
		introTime += dt;
		uploadCovers();
		updateArt(dt);
		auto ease = [dt](float &v, bool on) { v += ((on ? 1.0f : 0.0f) - v) * (1.0f - std::exp(-dt * 16.0f)); };
		ease(menuAnim, menuOpen);
		ease(settingsAnim, settingsOpen);
		ease(dialogAnim, updateDialog);
		ease(loadingAnim, coversLoaded < (int)games.size());
		handleInput(dt);
		// The row slides towards the selection, quickly at first, then settles;
		// the grid scrolls to the selection's row
		if (settings.grid) {
			gridScroll += ((float)(selected / gridCols) - gridScroll) * (1.0f - std::exp(-dt * 10.0f));
			position = (float)selected;
		}
		if (letterTimer > 0.0f) {
			letterTimer -= dt;
		}
		position += (selected - position) * (1.0f - std::exp(-dt * 11.0f));
		if (std::abs(selected - position) < 0.001f) {
			position = (float)selected;
		}
		if (messageTimer > 0.0f) {
			messageTimer -= dt;
		}
		if (closeAfterUpdate > 0.0f && (closeAfterUpdate -= dt) <= 0.0f) {
			quit = true;
		}
		collectSizes();
		pollUpdates(dt);
		pollTools();
		if (menuOpen && current()) {
			requestSize(*current());
		}
		updateCamera();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();
	}

	static std::string formatSize(long long bytes)
	{
		if (bytes < 0) {
			return "unknown";
		}
		if (bytes == 0) {
			return "measuring...";
		}
		char s[32];
		if (bytes >= (1ll << 30)) {
			std::snprintf(s, sizeof(s), "%.1f GB", bytes / 1073741824.0);
		} else {
			std::snprintf(s, sizeof(s), bytes >= (10ll << 20) ? "%.0f MB" : "%.1f MB", bytes / 1048576.0);
		}
		return s;
	}

	static std::string formatDate(long long t)
	{
		if (t <= 0) {
			return "unknown";
		}
		const time_t tt = (time_t)t;
		struct tm tmv;
		if (!gmtime_r(&tt, &tmv)) {
			return "unknown";
		}
		char s[32];
		std::strftime(s, sizeof(s), "%d %b %Y", &tmv);
		return s;
	}

	// ---------------------------------------------------------------- the interface

	enum Glyph { G_CROSS, G_CIRCLE, G_TRIANGLE, G_SQUARE, G_OPTIONS, G_SHARE, G_TOUCHPAD, G_L1, G_R1, G_DPAD_LR, G_DPAD_UD, G_L2, G_R2, G_L3, G_RSTICK };

	// Everything on screen but the covers, drawn with ImGui's draw list
	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		ImDrawList *dl = ImGui::GetOverlayDrawList();
		ImFont *font = ImGui::GetFont();
		const float s = height / 1080.0f;
		const float W = (float)width, H = (float)height;
		auto alphaOf = [](ImU32 c) { return (int)((c >> 24) & 0xFF); };
		auto withAlpha = [&](ImU32 c, float a) { return (c & 0x00FFFFFFu) | ((ImU32)std::clamp((int)(a * alphaOf(c)), 0, 255) << 24); };
		const ImU32 white = IM_COL32(255, 255, 255, 255);
		const ImU32 textC = theme.text, dimC = theme.dim;
		const ImU32 accentC = IM_COL32((int)(theme.accent.r * 255), (int)(theme.accent.g * 255), (int)(theme.accent.b * 255), 255);
		auto width_ = [&](const std::string &str, float size) { return font->CalcTextSizeA(size, FLT_MAX, 0.0f, str.c_str()).x; };
		auto text = [&](const std::string &str, float size, float x, float y, ImU32 colour, float alignX = 0.0f, bool shadow = true) {
			const float px = x - width_(str, size) * alignX;
			if (shadow) {
				dl->AddText(font, size, ImVec2(px + 1.5f * s, y + 2.0f * s), IM_COL32(0, 0, 0, alphaOf(colour) * 140 / 255), str.c_str());
			}
			dl->AddText(font, size, ImVec2(px, y), colour, str.c_str());
		};
		// A string cut to a width, with an ellipsis
		auto fit = [&](std::string str, float size, float maxW) {
			if (width_(str, size) <= maxW) {
				return str;
			}
			while (!str.empty() && width_(str + "...", size) > maxW) {
				str.pop_back();
				while (!str.empty() && ((unsigned char)str.back() & 0xC0) == 0x80) {
					str.pop_back(); // whole UTF-8 characters only
				}
			}
			return str + "...";
		};
		auto panel = [&](float x, float y, float w, float h, float a) {
			for (int i = 3; i >= 1; i--) {
				const float g = i * 10.0f * s;
				dl->AddRectFilled(ImVec2(x - g, y - g + 8.0f * s), ImVec2(x + w + g, y + h + g + 8.0f * s), IM_COL32(0, 0, 0, (int)(28 * a)), 22.0f * s + g);
			}
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(16, 18, 26, (int)(246 * a)), 20.0f * s);
			dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 255, 255, (int)(26 * a)), 20.0f * s, ImDrawCornerFlags_All, 1.5f * s);
		};
		// PlayStation button symbols, white in a soft disc, as the console's own menus draw them
		auto glyph = [&](Glyph g, float cx, float cy, float r, float a) -> float {
			const ImU32 fg = IM_COL32(255, 255, 255, (int)(235 * a)), bg = IM_COL32(255, 255, 255, (int)(34 * a));
			const float t = std::max(1.5f, 0.16f * r);
			switch (g) {
			case G_CROSS:
			case G_CIRCLE:
			case G_TRIANGLE:
			case G_SQUARE: {
				dl->AddCircleFilled(ImVec2(cx, cy), r, bg, 32);
				const float k = r * 0.48f;
				if (g == G_CROSS) {
					dl->AddLine(ImVec2(cx - k, cy - k), ImVec2(cx + k, cy + k), fg, t);
					dl->AddLine(ImVec2(cx - k, cy + k), ImVec2(cx + k, cy - k), fg, t);
				} else if (g == G_CIRCLE) {
					dl->AddCircle(ImVec2(cx, cy), k * 1.05f, fg, 28, t);
				} else if (g == G_TRIANGLE) {
					dl->AddTriangle(ImVec2(cx, cy - k * 1.05f), ImVec2(cx + k * 1.1f, cy + k * 0.8f), ImVec2(cx - k * 1.1f, cy + k * 0.8f), fg, t);
				} else {
					dl->AddRect(ImVec2(cx - k * 0.9f, cy - k * 0.9f), ImVec2(cx + k * 0.9f, cy + k * 0.9f), fg, 0.0f, ImDrawCornerFlags_All, t);
				}
				return 2.0f * r;
			}
			case G_OPTIONS:
			case G_SHARE: {
				const float w = r * 1.3f;
				dl->AddRectFilled(ImVec2(cx - w, cy - r * 0.8f), ImVec2(cx + w, cy + r * 0.8f), bg, r * 0.8f);
				for (int i = -1; i <= 1; i++) {
					if (g == G_OPTIONS) {
						dl->AddLine(ImVec2(cx - w * 0.45f, cy + i * r * 0.32f), ImVec2(cx + w * 0.45f, cy + i * r * 0.32f), fg, t * 0.8f);
					} else {
						dl->AddLine(ImVec2(cx + i * r * 0.36f - r * 0.12f, cy + r * 0.38f), ImVec2(cx + i * r * 0.36f + r * 0.12f, cy - r * 0.38f), fg, t * 0.8f);
					}
				}
				return 2.0f * w;
			}
			case G_TOUCHPAD: {
				const float w = r * 1.6f;
				dl->AddRectFilled(ImVec2(cx - w, cy - r * 0.75f), ImVec2(cx + w, cy + r * 0.75f), bg, r * 0.3f);
				dl->AddRect(ImVec2(cx - w * 0.62f, cy - r * 0.38f), ImVec2(cx + w * 0.62f, cy + r * 0.38f), fg, r * 0.15f, ImDrawCornerFlags_All, t * 0.8f);
				return 2.0f * w;
			}
			case G_L1:
			case G_R1:
			case G_L2:
			case G_R2:
			case G_L3:
			case G_RSTICK: {
				const char *label = g == G_L1 ? "L1" : g == G_R1 ? "R1" : g == G_L2 ? "L2" : g == G_R2 ? "R2" : g == G_L3 ? "L3" : "R";
				const float size = r * 1.05f, w = width_(label, size) * 0.5f + r * 0.55f;
				dl->AddRectFilled(ImVec2(cx - w, cy - r * 0.8f), ImVec2(cx + w, cy + r * 0.8f), bg, r * 0.35f);
				text(label, size, cx, cy - size * 0.52f, fg, 0.5f, false);
				return 2.0f * w;
			}
			case G_DPAD_LR:
			case G_DPAD_UD: {
				dl->AddCircleFilled(ImVec2(cx, cy), r, bg, 32);
				const float k = r * 0.5f, n = r * 0.2f;
				const bool lr = g == G_DPAD_LR;
				const ImU32 on = fg, off = IM_COL32(255, 255, 255, (int)(70 * a));
				dl->AddRectFilled(ImVec2(cx - k, cy - n), ImVec2(cx + k, cy + n), lr ? on : off, n * 0.5f);
				dl->AddRectFilled(ImVec2(cx - n, cy - k), ImVec2(cx + n, cy + k), lr ? off : on, n * 0.5f);
				return 2.0f * r;
			}
			}
			return 0.0f;
		};
		// A row of hints: symbols and their words, centred on x
		auto hints = [&](const std::vector<std::pair<std::vector<Glyph>, std::string>> &items, float cx, float y, float a) {
			const float r = 15.0f * s, size = 22.0f * s, gap = 34.0f * s, inner = 9.0f * s;
			auto glyphW = [&](Glyph g) {
				switch (g) {
				case G_OPTIONS:
				case G_SHARE: return 2.6f * r;
				case G_TOUCHPAD: return 3.2f * r;
				case G_L1:
				case G_R1:
				case G_L2:
				case G_R2:
				case G_L3:
				case G_RSTICK: return width_("L1", r * 1.05f) + 1.1f * r;
				default: return 2.0f * r;
				}
			};
			float total = 0.0f;
			for (const auto &[gs, label] : items) {
				for (Glyph g : gs) {
					total += glyphW(g) + 4.0f * s;
				}
				total += inner + width_(label, size) + gap;
			}
			float x = cx - (total - gap) * 0.5f;
			for (const auto &[gs, label] : items) {
				for (Glyph g : gs) {
					const float w = glyphW(g);
					glyph(g, x + w * 0.5f, y, r, a);
					x += w + 4.0f * s;
				}
				x += inner;
				text(label, size, x, y - size * 0.55f, withAlpha(dimC, a * 0.95f), 0.0f, false);
				x += width_(label, size) + gap;
			}
		};
		auto chip = [&](const std::string &label, float x, float y, float size, ImU32 fill, ImU32 colour) {
			const float padX = size * 0.6f, h = size * 1.55f, w = width_(label, size) + padX * 2.0f;
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), fill, h * 0.5f);
			text(label, size, x + padX, y + (h - size) * 0.48f, colour, 0.0f, false);
			return w;
		};
		auto star = [&](float cx, float cy, float r, ImU32 colour) {
			ImVec2 outer[5], inner[5];
			for (int i = 0; i < 5; i++) {
				const float ao = -1.5708f + i * 1.2566f, ai = ao + 0.6283f;
				outer[i] = ImVec2(cx + std::cos(ao) * r, cy + std::sin(ao) * r);
				inner[i] = ImVec2(cx + std::cos(ai) * r * 0.42f, cy + std::sin(ai) * r * 0.42f);
			}
			dl->AddConvexPolyFilled(inner, 5, colour);
			for (int i = 0; i < 5; i++) {
				dl->AddTriangleFilled(outer[i], inner[i], inner[(i + 4) % 5], colour);
			}
		};

		// ---- the header: heading, filter and sort; count and where the library came from
		const float margin = 72.0f * s;
		text(theme.heading, 46.0f * s, margin, 44.0f * s, textC);
		float cx = margin;
		const float chipY = 108.0f * s, chipSize = 20.0f * s;
		cx += chip(filterName(filterMode), cx, chipY, chipSize, withAlpha(accentC, 0.85f), white) + 10.0f * s;
		chip(sortNames[sortMode], cx, chipY, chipSize, IM_COL32(255, 255, 255, 30), textC);
		// Right: the clock and the position; below, where the library came from and the free space
		{
			const std::string clockStr = clockText();
			text(clockStr, 34.0f * s, W - margin, 46.0f * s, textC, 1.0f);
			if (!view.empty()) {
				const std::string count = std::to_string(selected + 1) + " / " + std::to_string(view.size());
				text(count, 24.0f * s, W - margin - width_(clockStr, 34.0f * s) - 28.0f * s, 54.0f * s, dimC, 1.0f, false);
			}
			const bool live = listSource == "live";
			std::string source = live ? "Live library" : listSource.rfind("saved", 0) == 0 ? "Saved library" :
				listSource == "games.txt" ? "From games.txt" : listSource == "demonstration covers" ? "Demo covers" : "Library";
			if (!storageText.empty()) {
				source += "   \xC2\xB7   " + storageText;
			}
			const float sx = W - margin - width_(source, 20.0f * s);
			dl->AddCircleFilled(ImVec2(sx - 14.0f * s, 111.0f * s), 5.0f * s, live ? IM_COL32(80, 220, 130, 255) : IM_COL32(240, 180, 60, 255), 16);
			text(source, 20.0f * s, W - margin, 99.0f * s, dimC, 1.0f, false);
		}
		// ---- loading progress, while covers arrive
		if (loadingAnim > 0.01f && !games.empty()) {
			const float a = loadingAnim, w = 360.0f * s, x = (W - w) * 0.5f, y = 60.0f * s;
			const std::string label = "Loading covers  " + std::to_string(coversLoaded) + " / " + std::to_string(games.size());
			text(label, 20.0f * s, W * 0.5f, y - 30.0f * s, withAlpha(dimC, a), 0.5f, false);
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 5.0f * s), IM_COL32(255, 255, 255, (int)(30 * a)), 3.0f * s);
			dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w * coversLoaded / std::max<size_t>(1, games.size()), y + 5.0f * s),
				withAlpha(accentC, a), 3.0f * s);
		}

		// ---- the selected game's name, category and favourite mark
		if (!view.empty()) {
			const Game &game = *current();
			const float fade = std::clamp(1.0f - std::abs(selected - position) * 2.0f, 0.0f, 1.0f) * (1.0f - std::max(menuAnim, settingsAnim) * 0.85f);
			const float titleSize = 54.0f * s, ty = H * 0.775f;
			text(fit(game.title, titleSize, W * 0.8f), titleSize, W * 0.5f, ty, withAlpha(textC, fade), 0.5f);
			std::string meta = game.subtitle.empty() ? categoryName(game.section) : game.subtitle;
			if (game.section == "Other" && game.subtitle.empty()) {
				meta = game.titleId;
			}
			const bool fav = isFavourite(game);
			const float metaSize = 24.0f * s, my = ty + 72.0f * s;
			const float metaW = width_(meta, metaSize) + (fav ? 34.0f * s : 0.0f);
			float mx = W * 0.5f - metaW * 0.5f;
			if (fav) {
				star(mx + 11.0f * s, my + metaSize * 0.5f, 11.0f * s, withAlpha(IM_COL32(255, 200, 70, 255), fade));
				mx += 34.0f * s;
			}
			text(meta, metaSize, mx, my, withAlpha(dimC, fade));
		} else {
			text(std::string("No ") + filterName(filterMode), 34.0f * s, W * 0.5f, H * 0.42f, textC, 0.5f);
			text("Press the touchpad to show another category", 24.0f * s, W * 0.5f, H * 0.42f + 54.0f * s, dimC, 0.5f);
		}

		// ---- the letter a jump landed on
		if (letterTimer > 0.0f && !letterShown.empty()) {
			const float a = std::clamp(letterTimer * 3.0f, 0.0f, 1.0f);
			const float size = 150.0f * s, box = 220.0f * s;
			const float cx2 = W * 0.5f, cy2 = H * 0.42f;
			dl->AddRectFilled(ImVec2(cx2 - box * 0.5f, cy2 - box * 0.5f), ImVec2(cx2 + box * 0.5f, cy2 + box * 0.5f), IM_COL32(10, 12, 18, (int)(200 * a)), 28.0f * s);
			dl->AddRect(ImVec2(cx2 - box * 0.5f, cy2 - box * 0.5f), ImVec2(cx2 + box * 0.5f, cy2 + box * 0.5f), withAlpha(accentC, a), 28.0f * s, ImDrawCornerFlags_All, 3.0f * s);
			text(letterShown, size, cx2, cy2 - size * 0.56f, withAlpha(white, a), 0.5f, false);
		}

		// ---- behind a panel, the row dims
		{
			const float dim = std::max(std::max(std::max(menuAnim, settingsAnim), dialogAnim), launcherPanel ? 1.0f : 0.0f);
			if (dim > 0.01f) {
				dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(110 * dim)));
			}
		}

		// ---- the game menu
		if (menuAnim > 0.01f && current()) {
			const Game &game = *current();
			const float a = menuAnim, w = 1160.0f * s, h = 610.0f * s;
			const float x0 = (W - w) * 0.5f, y0 = H * 0.16f + (1.0f - a) * 24.0f * s;
			panel(x0, y0, w, h, a);
			const float pad = 40.0f * s;
			text(fit(game.title, 34.0f * s, w * 0.5f - pad), 34.0f * s, x0 + pad, y0 + 30.0f * s, withAlpha(textC, a));
			chip(categoryName(game.section) + (sectionOverride.count(game.titleId) ? "" : "  (auto)"), x0 + pad, y0 + 84.0f * s, 18.0f * s,
				withAlpha(accentC, 0.8f * a), withAlpha(white, a));
			// Details
			float y = y0 + 146.0f * s;
			const float labelX = x0 + pad, valueX = x0 + pad + 170.0f * s;
			auto row = [&](const char *label, const std::string &value) {
				text(label, 23.0f * s, labelX, y, withAlpha(dimC, a), 0.0f, false);
				text(fit(value, 23.0f * s, w * 0.5f - 210.0f * s), 23.0f * s, valueX, y, withAlpha(textC, a), 0.0f, false);
				y += 44.0f * s;
			};
			row("Title ID", game.titleId.empty() ? "None" : game.titleId);
			row("Platform", game.platform.empty() ? "Other" : game.platform);
			row("Size", game.bytes == 0 && (listSource != "live" || game.titleId.empty()) ? "Unknown" : formatSize(game.bytes));
			row("Installed", formatDate(game.installed));
			{
				auto pt = playTime.find(game.titleId);
				row("Played", pt == playTime.end() ? "Not yet" : formatDuration(pt->second.seconds) + (pt->second.count > 1 ? "  (" + std::to_string(pt->second.count) + " times)" : ""));
				row("Last played", pt == playTime.end() ? "-" : formatDate(pt->second.last));
			}
			row("Cover", game.coverFrom);
			// Actions
			const std::vector<std::string> items = menuItems(game);
			const float bx = x0 + w * 0.52f, bw = w * 0.48f - pad, bh = 62.0f * s;
			float by = y0 + 34.0f * s;
			for (int i = 0; i < (int)items.size(); i++) {
				const bool on = i == menuIndex && !categoryOpen && !collectionsOpen;
				dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), on ? withAlpha(accentC, a) : IM_COL32(255, 255, 255, (int)(16 * a)), 12.0f * s);
				text(fit(items[i], 25.0f * s, bw - 44.0f * s), 25.0f * s, bx + 24.0f * s, by + 17.0f * s, withAlpha(on ? white : textC, a), 0.0f, false);
				by += bh + 12.0f * s;
			}
			hints({ { { G_DPAD_UD }, "Choose" }, { { G_CROSS }, "Select" }, { { G_CIRCLE }, "Back" } }, x0 + w * 0.5f, y0 + h - 34.0f * s, a);
			if (collectionsOpen) {
				const float cw = bw, ch = 80.0f * s + collectionCount * 64.0f * s;
				const float cx2 = bx, cy = y0 + 24.0f * s;
				dl->AddRectFilled(ImVec2(cx2 - 8.0f * s, cy - 8.0f * s), ImVec2(cx2 + cw + 8.0f * s, cy + ch + 8.0f * s), IM_COL32(10, 11, 16, (int)(255 * a)), 16.0f * s);
				dl->AddRect(ImVec2(cx2 - 8.0f * s, cy - 8.0f * s), ImVec2(cx2 + cw + 8.0f * s, cy + ch + 8.0f * s), IM_COL32(255, 255, 255, (int)(40 * a)), 16.0f * s, ImDrawCornerFlags_All, 1.5f * s);
				text("Collections  (CROSS adds or removes)", 22.0f * s, cx2 + 20.0f * s, cy + 16.0f * s, withAlpha(dimC, a), 0.0f, false);
				for (int i = 0; i < collectionCount; i++) {
					const float ry = cy + 66.0f * s + i * 64.0f * s;
					const bool on = i == collectionIndex;
					if (on) {
						dl->AddRectFilled(ImVec2(cx2, ry), ImVec2(cx2 + cw, ry + 54.0f * s), withAlpha(accentC, a), 10.0f * s);
					}
					// A box, ticked when the game is in it
					const float bx2 = cx2 + 22.0f * s, by2 = ry + 15.0f * s, bs = 24.0f * s;
					dl->AddRect(ImVec2(bx2, by2), ImVec2(bx2 + bs, by2 + bs), withAlpha(white, a), 5.0f * s, ImDrawCornerFlags_All, 2.0f * s);
					if (inCollection(game, i)) {
						dl->AddLine(ImVec2(bx2 + 5.0f * s, by2 + 12.0f * s), ImVec2(bx2 + 10.0f * s, by2 + 18.0f * s), withAlpha(white, a), 3.0f * s);
						dl->AddLine(ImVec2(bx2 + 10.0f * s, by2 + 18.0f * s), ImVec2(bx2 + 20.0f * s, by2 + 6.0f * s), withAlpha(white, a), 3.0f * s);
					}
					text(collectionNames[i], 24.0f * s, cx2 + 64.0f * s, ry + 14.0f * s, withAlpha(on ? white : textC, a), 0.0f, false);
				}
			}
			if (categoryOpen) {
				const float cw = bw, ch = 80.0f * s + 5 * 64.0f * s;
				const float cx2 = bx, cy = y0 + 24.0f * s;
				dl->AddRectFilled(ImVec2(cx2 - 8.0f * s, cy - 8.0f * s), ImVec2(cx2 + cw + 8.0f * s, cy + ch + 8.0f * s), IM_COL32(10, 11, 16, (int)(255 * a)), 16.0f * s);
				dl->AddRect(ImVec2(cx2 - 8.0f * s, cy - 8.0f * s), ImVec2(cx2 + cw + 8.0f * s, cy + ch + 8.0f * s), IM_COL32(255, 255, 255, (int)(40 * a)), 16.0f * s, ImDrawCornerFlags_All, 1.5f * s);
				text("Category", 22.0f * s, cx2 + 20.0f * s, cy + 16.0f * s, withAlpha(dimC, a), 0.0f, false);
				auto ov = sectionOverride.find(game.titleId);
				const std::string automatic = autoSection(game.titleId, game.title, game.contentId);
				for (int i = 0; i < 5; i++) {
					const float ry = cy + 66.0f * s + i * 64.0f * s;
					const bool on = i == categoryIndex;
					const bool chosen = i == 0 ? ov == sectionOverride.end() : (ov != sectionOverride.end() && ov->second == categoryKeys[i]);
					if (on) {
						dl->AddRectFilled(ImVec2(cx2, ry), ImVec2(cx2 + cw, ry + 54.0f * s), withAlpha(accentC, a), 10.0f * s);
					}
					const std::string label = i == 0 ? "Automatic  (" + categoryName(automatic) + ")" : categoryName(categoryKeys[i]);
					text(label, 24.0f * s, cx2 + 24.0f * s, ry + 14.0f * s, withAlpha(on ? white : textC, a), 0.0f, false);
					if (chosen) {
						// A tick
						const float tx = cx2 + cw - 36.0f * s, tyy = ry + 27.0f * s;
						dl->AddLine(ImVec2(tx - 9.0f * s, tyy), ImVec2(tx - 2.0f * s, tyy + 8.0f * s), withAlpha(white, a), 3.0f * s);
						dl->AddLine(ImVec2(tx - 2.0f * s, tyy + 8.0f * s), ImVec2(tx + 12.0f * s, tyy - 9.0f * s), withAlpha(white, a), 3.0f * s);
					}
				}
			}
		}

		// ---- Dump Game / App: complete navigable UI preview; no package extraction or file copying
		if (menuAnim > 0.01f && menuOpen && dumpOpen && current()) {
			const Game &game = *current();
			const float a = menuAnim, w = 900.0f * s, h = 610.0f * s;
			const float x0 = (W - w) * 0.5f, y0 = (H - h) * 0.5f;
			panel(x0, y0, w, h, a);
			const float pad = 36.0f * s;
			const float rowX = x0 + pad, rowW = w - pad * 2.0f, rowH = 48.0f * s;
			text("Dump Game / App", 32.0f * s, rowX, y0 + 26.0f * s, withAlpha(textC, a));
			text(fit(game.title, 22.0f * s, rowW), 22.0f * s, rowX, y0 + 72.0f * s, withAlpha(dimC, a), 0.0f, false);
			text("TITLE ID", 16.0f * s, rowX, y0 + 112.0f * s, withAlpha(dimC, a), 0.0f, false);
			text(game.titleId.empty() ? "Unavailable" : game.titleId, 20.0f * s, rowX, y0 + 136.0f * s, withAlpha(textC, a), 0.0f, false);

			const float rowY = y0 + 172.0f * s;
			std::vector<std::string> dumpLabels;
			if (dumpStage == 0) {
				dumpLabels = { "Internal storage", "USB drive", "Continue", "Cancel" };
			} else if (dumpStage == 1) {
				dumpLabels = { "Run UI preview", "Back to destination" };
			} else if (dumpStage == 3) {
				dumpLabels = { "Choose another destination", "Close" };
			}
			for (int i = 0; i < (int)dumpLabels.size(); i++) {
				const float ry = rowY + i * (rowH + 7.0f * s);
				const bool on = i == dumpIndex;
				dl->AddRectFilled(ImVec2(rowX, ry), ImVec2(rowX + rowW, ry + rowH),
					on ? withAlpha(accentC, a) : IM_COL32(255, 255, 255, (int)(14 * a)), 9.0f * s);
				text(dumpLabels[i], 20.0f * s, rowX + 20.0f * s, ry + 12.0f * s, withAlpha(on ? white : textC, a), 0.0f, false);
				if (dumpStage == 0 && i < 2 && dumpDestination == i) {
					text("SELECTED", 14.0f * s, rowX + rowW - 18.0f * s, ry + 15.0f * s, withAlpha(on ? white : accentC, a), 1.0f, false);
				}
			}
			const float statusY = y0 + 398.0f * s;
			if (dumpStage == 0) {
				text("DESTINATION", 15.0f * s, rowX, statusY, withAlpha(dimC, a), 0.0f, false);
				text(dumpDestination == 0 ? "Internal storage" : "USB drive", 19.0f * s, rowX, statusY + 23.0f * s, withAlpha(textC, a), 0.0f, false);
				text("Choose where an export would be saved.", 16.0f * s, rowX, statusY + 52.0f * s, withAlpha(dimC, a), 0.0f, false);
			} else if (dumpStage == 1) {
				text("READY TO BACK UP FILES", 15.0f * s, rowX, statusY, withAlpha(dimC, a), 0.0f, false);
				text("Destination: " + std::string(dumpDestination == 0 ? "Internal storage" : "USB drive"), 18.0f * s, rowX, statusY + 24.0f * s, withAlpha(textC, a), 0.0f, false);
				text("Uses the external PS5 App Dumper payload to create a game dump.", 16.0f * s, rowX, statusY + 52.0f * s, withAlpha(dimC, a), 0.0f, false);
			} else if (dumpStage == 2 || dumpStage == 3) {
				text(dumpStage == 2 ? "SUBMITTING GAME / APP DUMP…" : (dumpQueued ? "DUMP QUEUED" : (dumpSucceeded ? "DUMP FINISHED" : "DUMP FAILED")), 15.0f * s, rowX, statusY, withAlpha(dimC, a), 0.0f, false);
				const float barY = statusY + 30.0f * s;
				dl->AddRectFilled(ImVec2(rowX, barY), ImVec2(rowX + rowW, barY + 14.0f * s), IM_COL32(255, 255, 255, (int)(24 * a)), 6.0f * s);
				if (dumpProgress > 0.0f) {
					dl->AddRectFilled(ImVec2(rowX, barY), ImVec2(rowX + rowW * dumpProgress, barY + 14.0f * s), withAlpha(accentC, a), 6.0f * s);
				}
				if (dumpStage == 2) {
					text(dumpStatus.empty() ? "Waiting for dumper…" : dumpStatus, 16.0f * s, rowX, barY + 23.0f * s, withAlpha(textC, a), 0.0f, false);
					text("Live progress comes from the dumper; keep the destination connected.", 16.0f * s, rowX, barY + 52.0f * s, withAlpha(dimC, a), 0.0f, false);
				} else {
					text(dumpQueued ? "Queue accepted; this is not a completion confirmation." : (dumpSucceeded ? "Dumper reported a completed dump." : "The dumper did not confirm completion."), 16.0f * s, rowX, barY + 23.0f * s, withAlpha(textC, a), 0.0f, false);
					std::string detail = dumpResult;
					if (detail.size() > 100) detail = detail.substr(0, 97) + "...";
					text(detail, 13.0f * s, rowX, barY + 52.0f * s, withAlpha(dimC, a), 0.0f, false);
				}
			}
			const char *navigationHint = dumpStage == 2 ? "Copy in progress" : "D-pad: navigate";
			hints({ { { G_DPAD_UD }, navigationHint }, { { G_CROSS }, "Select" }, { { G_CIRCLE }, "Back" } },
				x0 + w * 0.5f, y0 + h - 28.0f * s, a);
		}

		// ---- settings
		if (settingsAnim > 0.01f) {
			const auto items = settingsItems();
			const float a = settingsAnim, rowH = 46.0f * s, w = 1240.0f * s;
			static const std::map<int, const char *> groups = { { S_RETURN, "Library" }, { S_THEME, "Look and sound" }, { S_UPDATE_CHECK, "Updates" }, { S_LAUNCHER, "Cover Flow" } };
			const float h = 130.0f * s + rowH * items.size() + 32.0f * s * groups.size();
			const float x0 = (W - w) * 0.5f, y0 = (H - h) * 0.5f + (1.0f - a) * 24.0f * s;
			panel(x0, y0, w, h, a);
			const float pad = 40.0f * s;
			text("Settings", 34.0f * s, x0 + pad, y0 + 28.0f * s, withAlpha(textC, a));
			text(std::string("Cover Flow ") + coverFlowVersion, 22.0f * s, x0 + w - pad, y0 + 38.0f * s, withAlpha(dimC, a), 1.0f, false);
			float y = y0 + 80.0f * s;
			for (int i = 0; i < (int)items.size(); i++) {
				auto group = groups.find(i);
				if (group != groups.end()) {
					text(group->second, 18.0f * s, x0 + pad, y + 7.0f * s, withAlpha(accentC, a), 0.0f, false);
					y += 32.0f * s;
				}
				const bool on = i == settingsIndex;
				if (on) {
					dl->AddRectFilled(ImVec2(x0 + 22.0f * s, y), ImVec2(x0 + w - 22.0f * s, y + rowH - 6.0f * s), withAlpha(accentC, a), 10.0f * s);
				}
				const ImU32 c = withAlpha(on ? white : textC, a);
				text(items[i].first, 23.0f * s, x0 + pad, y + 8.0f * s, c, 0.0f, false);
				if (!items[i].second.empty()) {
					std::string value = fit(items[i].second, 23.0f * s, w * 0.45f);
					float vx = x0 + w - pad;
					if (on && settingAdjustable(i)) {
						// < value > when LEFT / RIGHT change it
						text(">", 23.0f * s, vx, y + 8.0f * s, c, 1.0f, false);
						vx -= 30.0f * s;
						text(value, 23.0f * s, vx, y + 8.0f * s, c, 1.0f, false);
						text("<", 23.0f * s, vx - width_(value, 23.0f * s) - 14.0f * s, y + 8.0f * s, c, 1.0f, false);
					} else {
						text(value, 23.0f * s, vx, y + 8.0f * s, on ? c : withAlpha(dimC, a), 1.0f, false);
					}
				}
				y += rowH;
			}
			hints({ { { G_DPAD_UD }, "Choose" }, { { G_DPAD_LR }, "Change" }, { { G_CROSS }, "Select" }, { { G_CIRCLE }, "Close" } },
				x0 + w * 0.5f, y0 + h - 32.0f * s, a);
		}

		// ---- the update dialog
		if (dialogAnim > 0.01f) {
			std::vector<std::string> notes;
			{
				std::stringstream ns(updateNotes);
				std::string line;
				while (std::getline(ns, line) && notes.size() < 9) {
					line = trim(line);
					while (!line.empty() && (line[0] == '#' || line[0] == '*')) {
						line = trim(line.substr(1));
					}
					if (!line.empty()) {
						notes.push_back(line);
					}
				}
			}
			const float a = dialogAnim, w = 900.0f * s, h = 300.0f * s + notes.size() * 34.0f * s + (notes.empty() ? 0.0f : 20.0f * s);
			const float x0 = (W - w) * 0.5f, y0 = (H - h) * 0.5f + (1.0f - a) * 24.0f * s;
			panel(x0, y0, w, h, a);
			text("Update available", 34.0f * s, W * 0.5f, y0 + 34.0f * s, withAlpha(textC, a), 0.5f);
			text(std::string("Cover Flow ") + coverFlowVersion + "   ->   " + updateTag, 24.0f * s, W * 0.5f, y0 + 92.0f * s, withAlpha(dimC, a), 0.5f, false);
			for (size_t i = 0; i < notes.size(); i++) {
				text(fit(notes[i], 22.0f * s, w - 120.0f * s), 22.0f * s, x0 + 60.0f * s, y0 + 146.0f * s + i * 34.0f * s, withAlpha(textC, a), 0.0f, false);
			}
			const char *choices[2] = { "Update now", "Later" };
			for (int i = 0; i < 2; i++) {
				const float bw = 280.0f * s, bh = 64.0f * s;
				const float bx = W * 0.5f + (i == 0 ? -bw - 14.0f * s : 14.0f * s), by = y0 + h - bh - 70.0f * s;
				dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), i == updateChoice ? withAlpha(accentC, a) : IM_COL32(255, 255, 255, (int)(22 * a)), 14.0f * s);
				text(choices[i], 26.0f * s, bx + bw * 0.5f, by + 17.0f * s, withAlpha(white, a), 0.5f, false);
			}
			hints({ { { G_DPAD_LR }, "Choose" }, { { G_CROSS }, "Select" }, { { G_CIRCLE }, "Later" } }, W * 0.5f, y0 + h - 30.0f * s, a);
		}

		// ---- the launcher panel: its state and the end of its log
		if (launcherPanel) {
			const float w = 1300.0f * s, h = 720.0f * s;
			const float x0 = (W - w) * 0.5f, y0 = (H - h) * 0.5f;
			panel(x0, y0, w, h, 1.0f);
			const float pad = 40.0f * s;
			text("Launcher", 34.0f * s, x0 + pad, y0 + 28.0f * s, textC);
			text(launcherStatus, 22.0f * s, x0 + w - pad, y0 + 38.0f * s, dimC, 1.0f, false);
			dl->AddRectFilled(ImVec2(x0 + pad, y0 + 90.0f * s), ImVec2(x0 + w - pad, y0 + h - 90.0f * s), IM_COL32(0, 0, 0, 120), 12.0f * s);
			const float lineH = 28.0f * s;
			const int fits = (int)((h - 200.0f * s) / lineH);
			const int first = std::max(0, (int)logLines.size() - fits);
			float ly = y0 + 104.0f * s;
			for (int i = first; i < (int)logLines.size(); i++) {
				text(fit(logLines[i], 19.0f * s, w - pad * 2.0f - 40.0f * s), 19.0f * s, x0 + pad + 20.0f * s, ly, dimC, 0.0f, false);
				ly += lineH;
			}
			hints({ { { G_CROSS }, "Reload the launcher" }, { { G_CIRCLE }, "Close" } }, W * 0.5f, y0 + h - 44.0f * s, 1.0f);
		}

		// ---- the toast: messages slide in above the hints
		if (messageTimer > 0.0f && !message.empty()) {
			const float a = std::clamp(messageTimer * 3.0f, 0.0f, 1.0f) * std::clamp((introTime > 0.0f ? 1.0f : 0.0f), 0.0f, 1.0f);
			const float size = 24.0f * s;
			const std::string m = fit(message, size, W * 0.7f);
			const float w = width_(m, size) + 64.0f * s, h = 58.0f * s;
			const float x0 = (W - w) * 0.5f, y0 = 150.0f * s - (1.0f - a) * 16.0f * s;
			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(20, 22, 32, (int)(240 * a)), h * 0.5f);
			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + 8.0f * s, y0 + h), withAlpha(accentC, a), 4.0f * s, ImDrawCornerFlags_Left);
			text(m, size, x0 + 34.0f * s, y0 + (h - size) * 0.45f, withAlpha(textC, a), 0.0f, false);
		}

		// ---- the hints along the bottom
		if (settingsAnim < 0.5f && menuAnim < 0.5f && dialogAnim < 0.5f && !launcherPanel) {
			std::vector<std::pair<std::vector<Glyph>, std::string>> items = {
				{ { G_DPAD_LR }, "Browse" }, { { G_L2, G_R2 }, "Letter" }, { { G_CROSS }, "Start" }, { { G_TRIANGLE }, "Menu" },
				{ { G_SQUARE }, "Sort" }, { { G_TOUCHPAD }, "Filter" }, { { G_L3 }, settings.grid ? "Cover Flow" : "Grid" }, { { G_OPTIONS }, "Settings" },
			};
			if (settings.quitWith == 0) {
				items.push_back({ { G_OPTIONS, G_SHARE }, "Quit" });
			} else if (settings.quitWith == 1) {
				items.push_back({ { G_L1, G_R1, G_OPTIONS }, "Quit" });
			} else if (settings.quitWith == 2) {
				items.push_back({ { G_CIRCLE }, "Hold to quit" });
			}
			hints(items, W * 0.5f, H - 58.0f * s, 1.0f);
		}

		// ---- the fade in from black when Cover Flow opens
		if (introTime < 0.7f) {
			dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, (int)(255 * (1.0f - introTime / 0.7f))));
		}
	}
};

}  // leaves this program's namespace (ps5/src/wrap.cpp.in) for one definition the overlay looks up by name

#if defined(VK_EXAMPLE_PS5)
// The overlay's typeface: the theme's font when it names one, otherwise Roboto,
// loaded large so Cover Flow's big titles stay sharp at 4K
bool ps5OverlayFont(std::string &path, float &size)
{
	std::string root = getAssetPath();
	const std::string suffix = "assets/";
	if (root.size() >= suffix.size() && root.compare(root.size() - suffix.size(), suffix.size(), suffix) == 0) {
		root.resize(root.size() - suffix.size());
	}
	auto trimmed = [](std::string v) {
		const size_t a = v.find_first_not_of(" \t\r\n");
		const size_t b = v.find_last_not_of(" \t\r\n");
		return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
	};
	auto value = [&](const std::string &file, const std::string &key) {
		std::ifstream ini(file);
		std::string line;
		while (ini && std::getline(ini, line)) {
			const size_t eq = line.find('=');
			if (eq != std::string::npos && trimmed(line.substr(0, eq)) == key) {
				return trimmed(line.substr(eq + 1));
			}
		}
		return std::string();
	};
	// The chosen theme's folder: themes/<name>/, or theme/ for "Custom" (and older settings)
	std::string name = value(root + "coverflow.ini", "theme_name");
	std::string folder = name.empty() || name == "Custom" ? root + "theme/" : root + "themes/" + name + "/";
	const std::string font = value(folder + "theme.ini", "font");
	if (!font.empty() && std::ifstream(folder + font).good()) {
		path = folder + font;
	}
	size = 36.0f;
	return true;
}
#endif

namespace sample_coverflow {

VULKAN_EXAMPLE_MAIN()
