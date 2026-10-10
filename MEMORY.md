# Cover Flow - project memory

Durable facts about Cover Flow: what it is, how it is built, and the lessons
learned on the real console. Read this before changing anything. The current
state of work is in [HANDOFF.md](HANDOFF.md).

## What it is
- A Cover Flow game library for a jailbroken PS5, title id `PPSA99123`, installed
  in `/data/homebrew/PPSA99123/` and mounted by ShadowMountPlus.
- Built on [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate)
  (PS5_Vulkan platform layer + RADV). `source/coverflow.cpp` is the template sample.
- Two parts:
  - **Cover Flow** (`eboot.bin`): the app. It runs in the app sandbox.
  - **cflaunch** (`cflaunch.elf`): a payload that does what the sandbox cannot. Cover
    Flow talks to it over loopback TCP port **9112**, and loads it through the payload
    loader on port **9021** when the running launcher is older or missing.
- Updates come from this repo's releases: `eboot.bin`, `cflaunch.elf`, `SHA256SUMS`.
  The full title folder is also attached as `PPSA99123_vNN.zip`.

## Test console
- PS5 on firmware 13.60, onionHen v0.9.0 beta 2, ShadowMountPlus, nanodns.
- On the LAN at <PS5 address>: FTP (ftpsrv) on 2121, payload loader on 9021.
- Launcher log: `/data/cflaunch.log` (read it with
  `curl.exe ftp://<PS5 address>:2121/data/cflaunch.log`).
- Play time: `/data/cflaunch-playtime.txt`, one line per game: `ID seconds last count`.

## Rules (from the template's AGENTS.md - keep them)
- Nothing private leaves the machine. Never commit, upload or ship console firmware or
  anything taken from it, game data, BIOS files, keys or saves.
- Call only exported functions of the console's system software.
- Never load third-party payloads. Never change the console's settings or system
  software.
- Only assets with a clear licence ship (see `licenses/`, `assets/NOTICES.txt`).
- Never disable TLS certificate verification. Every update file is checked against
  `SHA256SUMS` before anything is replaced.
- Declined on purpose: store/catalog/download features, FFPKG install, app.db renames,
  other games' icons, moving games to USB, scraping art online, game dumping, trainers,
  uninstall, save tools, Home-menu redirect, Logitech wheel support.

## Launcher protocol (cflaunch 12)
- One command per connection, one answer. `ping` -> `ok cflaunch 12 pid=N`.
- Listens on **127.0.0.1:9112 only** (since 11): Cover Flow is on the same console and
  nothing on the network may send commands. Older launchers listened on every
  address; a new launcher asks any running one to `quit` before binding.
- Title ids must match `AAAA00000` (`valid_title`): they are used in paths under
  `/data/homebrew`, and launchers up to 10 accepted any 9 characters (`../../usr`).
- Library: `list`, `icon ID`, `art ID`, `size ID`, `where ID`, `storage`, `playtime`.
- `icon` and `art` answer `ok <bytes>\n` followed by the raw file. **The reader must
  read by that length**, not stop at the first newline (that bug hid all artwork
  until v15).
- `switch FROM TO [return]`: Cover Flow quits, the game starts, the game is timed;
  with `return`, Cover Flow is started again once the game has **closed**.
  `switch X X` (a title restarting itself) is refused.
- Backup wizard (v18), one file per command so Cover Flow shows progress and can stop:
  `backups` (one line: `usbN|free|total|state|date|covers|names|playseconds;...`, state
  none / complete / incomplete / old), `backup-begin TITLE usbN` (writes incomplete.txt,
  drops old custom covers), `backup-file` / `restore-file TITLE usbN ITEM` (copy, read
  both back, compare SHA-256; `ok <bytes>`), `backup-finish TITLE usbN COVERS NAMES`
  (manifest.txt), `backup-files usbN` (`ok files a;b;c`). ITEM is only coverflow.ini,
  games.txt, playtime.txt, custom/names.txt or custom/<TITLEID>.png|.jpg|.jpeg; the
  drive only usb0-usb7. The old one-shot `backup` / `restore` still answer.
- `drives`: one line, `ok drives Internal|free|total;USB 1|free|total`.
- `import TITLE`: copies `/mnt/usbN/CoverFlowCovers/<TITLEID>.png|.jpg` (8 MB max) into
  `covers/custom/`, and `names.txt` as `covers/custom/names-import.txt`; Cover Flow
  merges that into `covers/custom/names.txt` (`TITLEID | Name`).
- `fixperms TITLE`: chmod 0777 and chown root everything under `/data/homebrew/TITLE`.
- Also: `launch`, `kill`, `restart`, `status`, `users`, `procs`, `log`, `quit`,
  `backup TITLE`, `restore TITLE`, `update-check REPO`, `update-apply REPO TITLE`.
- A newer launcher replaces an older one by sending it `quit`.

## Declined: game dumping, also as a UI
- A "Dump Game / App" menu with simulated progress was asked for (from a ChatGPT
  brief) and declined: it is the front end of a dumping tool, and a fake button in a
  public release misleads users. The Back up and Restore wizard (v18) was built instead.
- Pull request #1 (a simulated dump screen) was merged into main on 10 October 2026
  and replaced by the v22 source. Pull request #2 / branch `feature/real-game-app-backup`
  ("v22 candidate") sends titles to EchoStretch/ps5-app-dumper through the payload
  loader: real dumping plus a third-party payload. Declined; never build or release it.

## Custom names and covers
- Kept only in Cover Flow's folder: `covers/custom/<TITLEID>.png|.jpg` and
  `covers/custom/names.txt`. They win over `games.txt`; the game menu's "Use the
  original name and cover" deletes them. Never edit `app.db` or other games'
  appmeta (declined: system data, risk to the home screen).

## Lessons from the console
- The app sandbox cannot read `/user/appmeta` or call `sceLncUtilLaunchApp` (it
  crashed); that is why cflaunch exists.
- HTTPS from the payload fails (`0x8095f00c`, SSL layer) in both sceHttp2 and sceHttp;
  HTTPS from inside the app works. So updates download inside Cover Flow (v12+).
- GitHub `/releases/latest` returns 404 if every release is a pre-release; Cover Flow
  falls back to `/releases?per_page=10`. Publish releases as Latest anyway.
- **Files written by Cover Flow come out `-rw-rw-rw-` owned by uid 1 (the app).** The
  console will not start an `eboot.bin` like that ("Can't start the game or app",
  CE-107750-0). FTP uploads are `-rwxrwxrwx` owned by root and work. Since v16 the
  launcher's `fixperms TITLE` makes the title's files rwxrwxrwx/root, and the updater
  checks `eboot.bin.new` is executable and root-owned before replacing anything.
- **Never restart a title from inside itself.** The v13 freeze: the relaunch failed with `0x80aa001a`
  (most likely the unstartable eboot.bin above) and was retried 8 times in a row. Since v14
  updates and USB restores close Cover Flow and the user opens it again.
- `0x8094000c` from a launch means "already running" and is not proof the title is
  up: check `sceLncUtilGetAppIdOfRunningBigApp` instead (v15).
- Going Home with the PS button does not close a game; it keeps running in the
  background, so "return after a game" only fires when the game is closed.
- The home-screen tile uses the console's copy of the icon in
  `/user/appmeta/PPSA99123/icon0.png`, not the one in the title folder.
- clang 18 drops hidden visibility on the PS5 target; the toolchain is clang 20.

## Search (v22)
- R3 opens the keyboard; `searchKey()` keeps lower-case letters and digits only;
  `searchMatch()` checks the shown name, original name and title id. A search shows
  every non-hidden match whatever the filter and is not saved.

## Look (v18)
- Navy panels with a blue edge and glow (`panel()`), controller icon by the heading,
  icon menu with the game's cover. Covers are drawn inside the overlay: base/
  VulkanUIOverlay.cpp binds `ImDrawCmd::TextureId` as a descriptor set (Cover Flow's
  cover sets use a layout identical to the overlay's). Keep that patch when updating
  the template.

## Building
- Stack: PS5_Vulkan, PS5_Mesa, PS5_PayloadSDK beside the title folder; console build
  `ps5/tools/build.sh` -> `dist/PPSA99123/eboot.bin`.
- Launcher: `cd payload/cflaunch && PS5_PAYLOAD_SDK=../../.deps/native/ps5-payload-sdk make`.
- Shaders and `sce_sys/icon0.png` are embedded in the eboot by `make_files.py`;
  rerun it after changing either. Shaders compile with
  `glslangValidator -V --target-env vulkan1.3`.
- PC check: the host reference build with lavapipe, plus a fake launcher on 9112.
  The `COVERFLOW_TEST` variable (host build only) opens screens: `sort=N filter=N menu
  category collections settings update notes grid letter launcher toast fav=ID select=ID
  search=TEXT keyboard key=RC wizard=...`.
- Bump `coverFlowVersion` and `launcherVersion` together with each release that
  changes them.
