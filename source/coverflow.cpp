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

static constexpr const char *coverFlowVersion = "v10";
static constexpr int launcherVersion = 5; // the launcher in the title folder

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
	};
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
	std::shared_ptr<Pending> updateCheckJob, updateApplyJob;
	bool updateDialog{ false };
	int updateChoice{ 0 };
	std::string updateTag;
	bool updating{ false };
	float updateCheckDelay{ 3.0f };

	// Sorting and filtering, kept between runs with the favourites and hidden games
	static constexpr int sortCount = 3, filterCount = 7;
	int sortMode{ 0 };   // 0 name, 1 title id, 2 recently installed
	int filterMode{ 0 }; // 0 all, 1 favourites, 2 PS5, 3 PS4, 4 emulators, 5 homebrew, 6 hidden
	static constexpr const char *sortNames[sortCount] = { "Name A-Z", "Title ID", "Recently installed" };
	static constexpr const char *filterNames[filterCount] = { "All games", "Favourites", "PS5 Games", "PS4 Games", "Emulators", "Homebrew Apps", "Hidden games" };
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
	};

	struct {
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
		if (device) {
			vkDestroyPipeline(device, pipelines.background, nullptr);
			vkDestroyPipeline(device, pipelines.reflection, nullptr);
			vkDestroyPipeline(device, pipelines.cover, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			for (auto &game : games) {
				game.texture.destroy();
			}
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

	void loadTheme()
	{
		const std::string folder = appRoot() + "theme/";
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
		if (ini.count("music") && !ini["music"].empty()) {
			startMusic(folder + ini["music"]);
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

	static void fillMusic(int16_t *frames, int count, void *user)
	{
		auto *self = (VulkanExample *)user;
		const size_t total = self->musicFrames.size() / 2;
		size_t at = self->musicCursor.load();
		const float volume = self->musicVolume.load();
		for (int i = 0; i < count; i++) {
			frames[i * 2] = (int16_t)(self->musicFrames[at * 2] * volume);
			frames[i * 2 + 1] = (int16_t)(self->musicFrames[at * 2 + 1] * volume);
			at = at + 1 < total ? at + 1 : 0;
		}
		self->musicCursor.store(at);
	}

	void startMusic(const std::string &path)
	{
		if (!loadWav(path)) {
			std::cerr << "Cover Flow: theme music " << path << " is not a PCM WAV file it can play\n";
			return;
		}
#if defined(VK_EXAMPLE_PS5)
		if (ps5.frameBudget) {
			return; // test runs stay silent
		}
		musicPlaying = audio_start(fillMusic, this);
#endif
		std::cerr << "Cover Flow: music " << path << (musicPlaying ? " playing" : " loaded (no audio output)") << "\n";
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

	void addGame(const std::string &title, const std::string &subtitle, const std::vector<uint8_t> &rgba, const std::string &titleId,
		const std::string &coverFrom, long long bytes = 0, long long installedAt = 0)
	{
		Game game;
		game.title = title;
		game.subtitle = subtitle;
		game.titleId = titleId;
		game.platform = platformOf(titleId);
		game.coverFrom = coverFrom;
		game.bytes = bytes;
		game.installed = installedAt;
		game.texture.fromBuffer((void *)rgba.data(), rgba.size(), VK_FORMAT_R8G8B8A8_SRGB, coverSize, coverSize, vulkanDevice, queue);
		games.push_back(std::move(game));
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

	// The installed list (live or cached) with games.txt's overrides and extras
	void buildFromList(const std::vector<Entry> &list, bool live)
	{
		const std::string covers = appRoot() + "covers/";
		const std::string cache = covers + "auto/";
		std::vector<Custom> custom = readGamesTxt();
		std::set<std::string> used;
		int fetched = 0;
		for (const Entry &e : list) {
			if (e.id == ownTitleId) {
				continue;
			}
			std::string name = cleanName(e.name), subtitle, from;
			std::vector<uint8_t> cover;
			for (const Custom &c : custom) {
				if (c.id == e.id) {
					if (!c.name.empty()) {
						name = c.name;
					}
					if (!c.subtitle.empty()) {
						subtitle = c.subtitle;
					}
					if (!c.image.empty() && cover.empty()) {
						cover = loadCover(covers + c.image);
						if (!cover.empty()) {
							from = "custom (games.txt)";
						}
					}
				}
			}
			if (cover.empty()) {
				cover = loadCover(cache + e.id + ".png");
				if (!cover.empty()) {
					from = "game icon";
				}
			}
			if (cover.empty() && live) {
				const std::string answer = askLauncher("icon " + e.id);
				const size_t nl = answer.find('\n');
				if (answer.rfind("ok ", 0) == 0 && nl != std::string::npos) {
					const std::string png = answer.substr(nl + 1);
					int w = 0, h = 0, n = 0;
					uint8_t *pixels = stbi_load_from_memory((const stbi_uc *)png.data(), (int)png.size(), &w, &h, &n, 4);
					if (pixels && w > 0 && h > 0) {
						cover = squareCover(pixels, w, h);
						writeFile(cache + e.id + ".png", png);
						from = "game icon";
						fetched++;
					}
					if (pixels) {
						stbi_image_free(pixels);
					}
				}
			}
			if (cover.empty()) {
				cover = demoCover((int)games.size());
				from = "plain (no icon found)";
			}
			used.insert(e.id);
			addGame(name, subtitle, cover, e.id, from, e.bytes, e.installed);
			games.back().contentId = e.contentId;
		}
		// games.txt entries that are not in the installed list
		for (const Custom &c : custom) {
			if (!c.id.empty() && used.count(c.id)) {
				continue;
			}
			std::vector<uint8_t> cover = c.image.empty() ? std::vector<uint8_t>() : loadCover(covers + c.image);
			const std::string from = cover.empty() ? "plain" : "custom (games.txt)";
			if (cover.empty()) {
				cover = demoCover((int)games.size());
			}
			addGame(c.name.empty() ? c.image : c.name, c.subtitle, cover, c.id, from);
		}
		if (fetched) {
			std::cerr << "Cover Flow: fetched " << fetched << " icons from the launcher\n";
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
			std::vector<uint8_t> cover = loadCover(g.icon);
			const std::string from = cover.empty() ? "plain" : "game icon";
			if (cover.empty()) {
				cover = demoCover((int)games.size());
			}
			addGame(g.name, platformOf(g.id), cover, g.id, from);
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
			addGame(names[i], "Load the Cover Flow launcher to show your games", demoCover(i), "", "demonstration");
		}
		listSource = "demonstration covers";
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
			"\nquit_with = " + std::to_string(settings.quitWith) +
			"\nvolume = " + (settings.volume >= 0 ? std::to_string(settings.volume) : "") +
			"\nreflection = " + (settings.reflection >= 0 ? std::to_string(settings.reflection) : "") +
			"\nangle = " + (settings.angle >= 0 ? std::to_string(settings.angle) : "") + "\n");
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
					(filterMode == 3 && p == "PS4") || (filterMode == 4 && p == "Emulators") || (filterMode == 5 && p == "Homebrew"));
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
		const uint32_t count = (uint32_t)games.size() + 1;
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
		for (auto &game : games) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &game.descriptorSet));
			VkWriteDescriptorSet write = vks::initializers::writeDescriptorSet(game.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &game.texture.descriptor);
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		}
		if (theme.hasPicture) {
			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
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
		if (settings.themeOn) {
			loadTheme();
		}
		applySettings();
		loadLibrary();
		loadSettings(); // the sections it names need the library's games
		loadSizes();
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
			if (test.find("update") != std::string::npos) {
				updateDialog = true;
				updateTag = "v8";
				settings.updateRepo = "example/coverflow";
			}
			if (test.find("fav=") != std::string::npos) {
				favourites.insert(test.substr(test.find("fav=") + 4, 9));
			}
		}
#endif
		rebuildView();
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
		prepared = true;
	}

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
		view3d = glm::lookAt(glm::vec3(0.0f, 0.78f, 4.6f), glm::vec3(0.0f, 0.36f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
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

	std::vector<std::string> settingsItems() const
	{
		const float volume = settings.volume >= 0 ? settings.volume : theme.volume;
		return {
			std::string("Come back to Cover Flow after a game:  ") + (settings.returnAfter ? "On" : "Off"),
			"Music volume:  " + (musicPlaying ? percent(volume) : std::string("no music in the theme")),
			"Reflections:  " + percent(theme.reflection),
			"Cover angle:  " + std::to_string((int)std::lround(theme.sideAngle)) + " degrees",
			std::string("Theme:  ") + (settings.themeOn ? "On" : "Off") + "  (applies when Cover Flow next opens)",
			std::string("Check for updates when opening:  ") + (settings.updateCheck ? "On" : "Off"),
			"Update source:  github.com/" + settings.updateRepo,
			"Check for updates now",
			"Reload the launcher  (" + launcherStatus + ")",
			std::string("Quit with:  ") + quitNames[settings.quitWith],
			"Quit Cover Flow",
		};
	}

	// LEFT/RIGHT (delta) or CROSS (delta 0) on a settings line
	void settingsChange(int index, int delta)
	{
		const int step = delta == 0 ? 1 : delta;
		switch (index) {
		case 0:
			settings.returnAfter = !settings.returnAfter;
			break;
		case 1:
			if (musicPlaying) {
				const float v = settings.volume >= 0 ? settings.volume : theme.volume;
				settings.volume = std::clamp(std::round((v + 0.1f * step) * 10.0f) / 10.0f, 0.0f, 1.0f);
			}
			break;
		case 2:
			settings.reflection = std::clamp(std::round((theme.reflection + 0.1f * step) * 10.0f) / 10.0f, 0.0f, 1.0f);
			break;
		case 3:
			settings.angle = std::clamp(theme.sideAngle + 5.0f * step, 30.0f, 85.0f);
			break;
		case 4:
			settings.themeOn = !settings.themeOn;
			break;
		case 5:
			settings.updateCheck = !settings.updateCheck;
			break;
		case 6:
			if (delta == 0) {
				showMessage("Set update_repo = owner/repo in /data/homebrew/" + (ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId) + "/coverflow.ini", 6.0f);
			}
			return;
		case 7:
			if (delta == 0) {
				checkForUpdate(true);
			}
			return;
		case 8:
			if (delta == 0) {
				ensureLauncher(true);
				showMessage("Launcher: " + launcherStatus, 4.0f);
			}
			return;
		case 9:
			settings.quitWith = (settings.quitWith + (delta < 0 ? 3 : 1)) % 4;
			showMessage(std::string("Quit with: ") + quitNames[settings.quitWith], 3.0f);
			break;
		case 10:
			if (delta == 0) {
				quit = true;
			}
			return;
		}
		applySettings();
		saveSettings();
	}

	// ---------------------------------------------------------------- updates

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
		updateCheckJob = askInBackground("update-check " + settings.updateRepo, 30);
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
			if (answer.rfind("ok ", 0) == 0) {
				updateTag = trim(answer.substr(3));
				if (updateTag != coverFlowVersion) {
					updateDialog = true;
					updateChoice = 0;
					messageTimer = 0.0f;
				} else if (checkByHand) {
					showMessage(std::string("Cover Flow is up to date (") + coverFlowVersion + ")", 3.0f);
				}
			} else if (checkByHand) {
				showMessage("Update check: " + (answer.empty() ? std::string("the launcher did not answer") : answer), 6.0f);
			}
		}
		if (finished(updateApplyJob, answer)) {
			updateApplyJob.reset();
			updating = false;
			if (answer.rfind("ok", 0) == 0) {
				// Installed: the launcher restarts Cover Flow once it has quit
				const std::string self = ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId;
				askLauncher("switch " + self + " " + self);
				showMessage("Updated to " + updateTag + " - restarting", 10.0f);
				quit = true;
			} else {
				showMessage("Update failed: " + (answer.empty() ? std::string("no answer from the launcher") : answer), 8.0f);
			}
		}
	}

	void startUpdate()
	{
		const std::string self = ownTitleId.empty() ? std::string("PPSA99123") : ownTitleId;
		updateApplyJob = askInBackground("update-apply " + settings.updateRepo + " " + self, 600);
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
			isHidden(g) ? "Show in Cover Flow" : "Hide from Cover Flow", "Close" };
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
		default:
			menuOpen = false;
			break;
		}
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
#if defined(VK_EXAMPLE_PS5)
			pad_vibrate(0.0f, 0.15f);
			vibrateTimer = 0.04f;
#endif
		}
	}

	void handleInput(float dt)
	{
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
		if (settingsOpen) {
			const int count = (int)settingsItems().size();
			if (p.pressed & PAD_UP) {
				settingsIndex = (settingsIndex + count - 1) % count;
			}
			if (p.pressed & PAD_DOWN) {
				settingsIndex = (settingsIndex + 1) % count;
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
			}
			return;
		}
		if (optionsTap) {
			settingsOpen = true;
			menuOpen = false;
			settingsIndex = 0;
			return;
		}
		// D-pad: a press moves one; held, it repeats after a pause
		int dir = 0;
		if (p.held & PAD_LEFT) {
			dir = -1;
		} else if (p.held & PAD_RIGHT) {
			dir = 1;
		} else if (std::abs(p.left_x) > 0.45f) {
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
			move(-5);
		}
		if (p.pressed & PAD_R1) {
			move(5);
		}
		if (p.pressed & PAD_SQUARE) {
			sortMode = (sortMode + 1) % sortCount;
			rebuildView();
			saveSettings();
			showMessage(std::string("Sorted by ") + sortNames[sortMode], 2.0f);
		}
		if (p.pressed & PAD_TOUCH_PAD) {
			filterMode = (filterMode + 1) % filterCount;
			rebuildView();
			saveSettings();
			showMessage(std::string("Showing ") + filterNames[filterMode] + " (" + std::to_string(view.size()) + ")", 2.0f);
		}
		const Game *game = current();
		if ((p.pressed & PAD_TRIANGLE) && game) {
			menuOpen = !menuOpen;
			menuIndex = 0;
			categoryOpen = false;
		}
		if (menuOpen && !game) {
			menuOpen = false;
		}
		if (menuOpen && categoryOpen) {
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
		const float horizon = 0.5f + 0.5f * floorClip.y / floorClip.w;
		PushConstants pc{ glm::mat4(0.0f), glm::vec4(theme.accent, horizon) };
		pc.mvp[0][0] = theme.hasPicture ? 1.0f : 0.0f;
		pc.mvp[0][1] = theme.pictureAspect / ((float)width / (float)height);
		pc.mvp[0][2] = theme.dimming;
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.background);
		VkDescriptorSet backdropSet = theme.hasPicture ? backgroundSet : games[0].descriptorSet;
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &backdropSet, 0, nullptr);
		vkCmdPushConstants(cmd, pipelineLayout, stages, 0, sizeof(pc), &pc);
		vkCmdDraw(cmd, 3, 1, 0, 0);

		// The visible covers, farthest from the middle first
		std::vector<int> order;
		for (int k = 0; k < (int)view.size(); k++) {
			if (std::abs(k - position) < 9.0f) {
				order.push_back(k);
			}
		}
		std::sort(order.begin(), order.end(), [&](int a, int b) { return std::abs(a - position) > std::abs(b - position); });
		const glm::mat4 viewProjection = projection * view3d;
		for (int pass = 0; pass < 2; pass++) {
			const bool reflection = pass == 0;
			if (reflection && theme.reflection <= 0.0f) {
				continue;
			}
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reflection ? pipelines.reflection : pipelines.cover);
			for (int k : order) {
				const float offset = k - position;
				const float a = std::abs(offset);
				glm::mat4 model = coverModel(offset);
				if (reflection) {
					model = glm::scale(model, glm::vec3(1.0f, -1.0f, 1.0f));
				}
				// Side covers a little darker, the far ones fading out
				const float brightness = 1.0f - 0.38f * std::min(a, 1.0f) - 0.05f * std::max(0.0f, a - 1.0f);
				const float alpha = std::clamp(8.5f - a, 0.0f, 1.0f);
				pc.mvp = viewProjection * model;
				pc.params = glm::vec4(reflection ? theme.reflection : 0.0f, brightness, alpha, (k == selected && a < 0.05f) ? 1.0f : 0.0f);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &games[view[k]].descriptorSet, 0, nullptr);
				vkCmdPushConstants(cmd, pipelineLayout, stages, 0, sizeof(pc), &pc);
				vkCmdDraw(cmd, 6, 1, 0, 0);
			}
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
		handleInput(dt);
		// The row slides towards the selection, quickly at first, then settles
		position += (selected - position) * (1.0f - std::exp(-dt * 11.0f));
		if (std::abs(selected - position) < 0.001f) {
			position = (float)selected;
		}
		if (messageTimer > 0.0f) {
			messageTimer -= dt;
		}
		collectSizes();
		pollUpdates(dt);
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

	// The text: drawn straight onto the screen with ImGui's font
	void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		ImDrawList *dl = ImGui::GetOverlayDrawList();
		ImFont *font = ImGui::GetFont();
		const float s = height / 1080.0f;
		auto withAlpha = [](ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)std::clamp(a, 0, 255) << 24); };
		auto text = [&](const std::string &str, float size, float y, ImU32 colour, float alignX = 0.5f, float x = -1.0f) {
			const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, str.c_str());
			const float px = (x < 0.0f ? width * 0.5f : x) - extent.x * alignX;
			dl->AddText(font, size, ImVec2(px + 2.0f * s, y + 2.0f * s), withAlpha(IM_COL32(0, 0, 0, 255), ((colour >> 24) & 0xFF) * 160 / 255), str.c_str());
			dl->AddText(font, size, ImVec2(px, y), colour, str.c_str());
		};
		text(theme.heading, 44.0f * s, 46.0f * s, theme.text, 0.0f, 70.0f * s);
		text(std::string(filterNames[filterMode]) + "  -  " + sortNames[sortMode], 24.0f * s, 104.0f * s, theme.dim, 0.0f, 72.0f * s);
		if (!view.empty()) {
			text(std::to_string(selected + 1) + " / " + std::to_string(view.size()), 30.0f * s, 56.0f * s, theme.dim, 1.0f, width - 70.0f * s);
			text(listSource, 20.0f * s, 100.0f * s, withAlpha(theme.dim, 150), 1.0f, width - 70.0f * s);
			const Game &game = *current();
			// The title fades in as the row settles on it
			const int fade = (int)(255.0f * std::clamp(1.0f - std::abs(selected - position) * 2.0f, 0.0f, 1.0f));
			text(game.title, 52.0f * s, height * 0.785f, withAlpha(theme.text, fade));
			std::string sub = !game.subtitle.empty() ? game.subtitle : game.titleId;
			if (isFavourite(game)) {
				sub += sub.empty() ? "Favourite" : "  -  Favourite";
			}
			if (!sub.empty()) {
				text(sub, 28.0f * s, height * 0.785f + 64.0f * s, withAlpha(theme.dim, fade));
			}
			if (menuOpen) {
				// The game menu: details on the left, actions on the right
				const float w = 1040.0f * s, h = 470.0f * s;
				const float x0 = (width - w) * 0.5f, y0 = height * 0.24f;
				dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(8, 10, 16, 228), 14.0f * s);
				dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), withAlpha(theme.dim, 90), 14.0f * s, ImDrawCornerFlags_All, 2.0f * s);
				float y = y0 + 26.0f * s;
				const float lx = x0 + 30.0f * s, vx = x0 + 200.0f * s;
				std::string name = game.title;
				if (name.size() > 30) {
					name = name.substr(0, 29) + "...";
				}
				text(name, 32.0f * s, y, theme.text, 0.0f, lx);
				y += 60.0f * s;
				auto row = [&](const char *label, const std::string &value) {
					text(label, 24.0f * s, y, theme.dim, 0.0f, lx);
					text(value, 24.0f * s, y, theme.text, 0.0f, vx);
					y += 40.0f * s;
				};
				row("Title ID", game.titleId.empty() ? "none" : game.titleId);
				row("Platform", game.platform.empty() ? "other" : game.platform);
				row("Category", categoryName(game.section) + (sectionOverride.count(game.titleId) ? "" : " (auto)"));
				row("Size", game.bytes == 0 && (listSource != "live" || game.titleId.empty()) ? "unknown" : formatSize(game.bytes));
				row("Installed", formatDate(game.installed));
				row("Cover", game.coverFrom);
				// The actions
				const std::vector<std::string> items = menuItems(game);
				const float bx = x0 + w * 0.55f, bw = w * 0.45f - 30.0f * s, bh = 58.0f * s;
				float by = y0 + 30.0f * s;
				for (int i = 0; i < (int)items.size(); i++) {
					const bool on = i == menuIndex;
					const ImU32 accent = IM_COL32((int)(theme.accent.r * 255), (int)(theme.accent.g * 255), (int)(theme.accent.b * 255), on ? 235 : 0);
					dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), on ? accent : IM_COL32(255, 255, 255, 18), 8.0f * s);
					text(items[i], 26.0f * s, by + 14.0f * s, on ? IM_COL32(255, 255, 255, 255) : theme.text, 0.0f, bx + 22.0f * s);
					by += bh + 14.0f * s;
				}
				text("UP / DOWN  choose      CROSS  select      CIRCLE  close", 22.0f * s, y0 + h - 44.0f * s, withAlpha(theme.dim, 200), 0.0f, lx);
				if (categoryOpen) {
					// The category list, over the actions
					const float cw = w * 0.45f, ch = 70.0f * s + 5 * 62.0f * s;
					const float cx = x0 + w * 0.55f - 20.0f * s, cy = y0 + 20.0f * s;
					dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cw, cy + ch), IM_COL32(14, 16, 24, 255), 12.0f * s);
					dl->AddRect(ImVec2(cx, cy), ImVec2(cx + cw, cy + ch), withAlpha(theme.dim, 120), 12.0f * s, ImDrawCornerFlags_All, 2.0f * s);
					text("Choose a category", 24.0f * s, cy + 18.0f * s, theme.dim, 0.0f, cx + 24.0f * s);
					auto ov = sectionOverride.find(game.titleId);
					const std::string automatic = autoSection(game.titleId, game.title, game.contentId);
					for (int i = 0; i < 5; i++) {
						const float ry = cy + 62.0f * s + i * 62.0f * s;
						const bool on = i == categoryIndex;
						const bool current = i == 0 ? ov == sectionOverride.end() : (ov != sectionOverride.end() && ov->second == categoryKeys[i]);
						if (on) {
							dl->AddRectFilled(ImVec2(cx + 12.0f * s, ry), ImVec2(cx + cw - 12.0f * s, ry + 52.0f * s),
								IM_COL32((int)(theme.accent.r * 255), (int)(theme.accent.g * 255), (int)(theme.accent.b * 255), 235), 8.0f * s);
						}
						const std::string label = i == 0 ? "Automatic  (" + categoryName(automatic) + ")" : categoryName(categoryKeys[i]);
						text((current ? "> " : "   ") + label, 25.0f * s, ry + 12.0f * s, on ? IM_COL32(255, 255, 255, 255) : theme.text, 0.0f, cx + 24.0f * s);
					}
				}
			}
		} else {
			text(std::string("No ") + filterNames[filterMode] + " - press TOUCHPAD to change the filter", 30.0f * s, height * 0.45f, theme.dim);
		}
		if (messageTimer > 0.0f) {
			const int a = (int)(255.0f * std::clamp(messageTimer, 0.0f, 1.0f));
			text(message, 28.0f * s, height * 0.9f, IM_COL32(120, 170, 255, a));
		}
		auto panel = [&](float x0, float y0, float w, float h) {
			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(8, 10, 16, 250), 14.0f * s);
			dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), withAlpha(theme.dim, 90), 14.0f * s, ImDrawCornerFlags_All, 2.0f * s);
		};
		const ImU32 accentFill = IM_COL32((int)(theme.accent.r * 255), (int)(theme.accent.g * 255), (int)(theme.accent.b * 255), 235);
		if (settingsOpen) {
			const std::vector<std::string> items = settingsItems();
			const float w = 1180.0f * s, rowH = 56.0f * s, h = 130.0f * s + rowH * items.size();
			const float x0 = (width - w) * 0.5f, y0 = (height - h) * 0.5f;
			panel(x0, y0, w, h);
			text(std::string("Settings  -  Cover Flow ") + coverFlowVersion, 32.0f * s, y0 + 24.0f * s, theme.text, 0.0f, x0 + 30.0f * s);
			float y = y0 + 80.0f * s;
			for (int i = 0; i < (int)items.size(); i++) {
				const bool on = i == settingsIndex;
				if (on) {
					dl->AddRectFilled(ImVec2(x0 + 18.0f * s, y - 6.0f * s), ImVec2(x0 + w - 18.0f * s, y + rowH - 14.0f * s), accentFill, 8.0f * s);
				}
				std::string line = items[i];
				if (line.size() > 92) {
					line = line.substr(0, 91) + "...";
				}
				text(line, 24.0f * s, y + 4.0f * s, on ? IM_COL32(255, 255, 255, 255) : theme.text, 0.0f, x0 + 34.0f * s);
				y += rowH;
			}
			text("UP / DOWN  choose      LEFT / RIGHT  change      CROSS  select      CIRCLE  close", 22.0f * s, y0 + h - 42.0f * s,
				withAlpha(theme.dim, 200), 0.0f, x0 + 30.0f * s);
		}
		if (updateDialog) {
			const float w = 760.0f * s, h = 250.0f * s;
			const float x0 = (width - w) * 0.5f, y0 = (height - h) * 0.5f;
			panel(x0, y0, w, h);
			text("UPDATE AVAILABLE", 34.0f * s, y0 + 26.0f * s, theme.text);
			text(std::string(coverFlowVersion) + "  ->  " + updateTag + "   (" + settings.updateRepo + ")", 24.0f * s, y0 + 84.0f * s, theme.dim);
			const char *choices[2] = { "Update now", "Later" };
			for (int i = 0; i < 2; i++) {
				const float bw = 250.0f * s, bh = 60.0f * s;
				const float bx = width * 0.5f + (i == 0 ? -bw - 20.0f * s : 20.0f * s), by = y0 + h - bh - 30.0f * s;
				dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), i == updateChoice ? accentFill : IM_COL32(255, 255, 255, 22), 8.0f * s);
				text(choices[i], 26.0f * s, by + 15.0f * s, IM_COL32(255, 255, 255, 255), 0.5f, bx + bw * 0.5f);
			}
		}
		const char *quitHints[4] = { "OPTIONS + SHARE  quit", "L1 + R1 + OPTIONS  quit", "hold CIRCLE  quit", "" };
		text(std::string("LEFT / RIGHT  browse    L1 / R1  jump    CROSS  start    TRIANGLE  menu    SQUARE  sort    TOUCHPAD  filter    OPTIONS  settings") +
				(settings.quitWith < 3 ? std::string("    ") + quitHints[settings.quitWith] : std::string()),
			20.0f * s, height - 52.0f * s, withAlpha(theme.dim, 190));
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
	std::ifstream ini(root + "theme/theme.ini");
	std::string line;
	while (ini && std::getline(ini, line)) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		auto trimmed = [](std::string v) {
			const size_t a = v.find_first_not_of(" \t\r\n");
			const size_t b = v.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
		};
		if (trimmed(line.substr(0, eq)) == "font") {
			const std::string file = root + "theme/" + trimmed(line.substr(eq + 1));
			if (std::ifstream(file).good()) {
				path = file;
			}
		}
	}
	size = 36.0f;
	return true;
}
#endif

namespace sample_coverflow {

VULKAN_EXAMPLE_MAIN()
