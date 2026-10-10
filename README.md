# Cover Flow for PS5

A Cover Flow game library for jailbroken PS5 consoles (homebrew), built on
[PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate).

## Features
- Your installed games as a row of covers with reflections, or a grid (L3)
- The selected game's artwork fades in behind the covers
- Starts games, and can bring Cover Flow back when the game closes (close the game,
  don't just go Home: a game left running in the background stays open)
- Play time per game, with Recently played, Most played and Largest first sorts
- Your own names and covers, shown only in Cover Flow (the console's own data is never changed)
- Favourites, hide, categories (PS5 / PS4 / Emulators / Homebrew) and six collections
- Sort and filter, letter jump (L2/R2), fast scroll with the right stick
- Details panel with size, install date, play time and storage location
- Themes: six built in, your own picture/colours/font/music, and interface sounds
- Header with clock and free storage; free space on every drive in Settings
- USB backup and restore of settings, collections and play time
- **v22 candidate:** Dump Game / App workflow with destination selection, confirmation, progress and result screens
- Dump requests are handed to a separately installed PS5 App Dumper payload; a queued request is not proof that extraction completed
- Manage Dumps screen (history/storage integration is still a work in progress)
- Launcher status and log inside Settings
- Updates itself from this repository's releases, every file checked with SHA-256

## Controls
| Button | Action |
| --- | --- |
| Left stick / D-pad | Browse |
| Right stick | Fast scroll |
| L2 / R2 | Previous / next letter |
| Cross | Start the game |
| Triangle | Menu (details, favourite, hide, category, collections) |
| Square | Sort |
| Touchpad | Filter |
| L3 | Cover Flow / grid |
| Options | Settings |
| Options + Share (held) | Quit (changeable in Settings) |

## Install
Download `PPSA99123_vNN.zip` from the [latest release](https://github.com/awaisa123/coverflow/releases/latest),
extract it and copy the `PPSA99123` folder to `/data/homebrew/` on the PS5 (with
ShadowMountPlus). The folder already contains `cflaunch.elf`; Cover Flow loads its
launcher through the payload loader on port 9021 whenever the running launcher is
older or missing.

## Dump Game / App (v22 candidate)

The feature branch contains a Dump Game / App UI and an integration client for a
separately installed PS5 App Dumper payload. It is not a self-contained dumper:
Cover Flow looks for `ps5-app-dumper.elf`, loads it through the local payload
loader when needed, discovers the dumper API, selects an available destination,
and submits the selected title to the dumper's queue.

### Workflow

1. Select a title in Cover Flow and open its menu.
2. Choose **Dump Game / App**.
3. Select internal storage or a supported USB destination.
4. Review the title and confirm the request.
5. Follow the status/result screen. Progress is meaningful only when the external
   dumper reports it; a queued request means accepted for processing, not completed.
6. **Manage Dumps** currently has an empty-state screen; persistent dump-history
   discovery and management actions are not yet connected.

### Requirements and limitations

- A compatible PS5 homebrew environment and a separately obtained, compatible
  `ps5-app-dumper.elf` are required. This repository does not include that payload.
- The payload loader must be available on port 9021 when the integration needs to
  load the dumper. The dumper must expose the API expected by this branch.
- A writable internal or USB destination must be reported by the dumper.
- Compatibility varies by firmware and dumper build. If the payload or API is
  unavailable, the UI should report an error rather than show fabricated success.
- This is a **development candidate**. It has not been compiled or tested on a
  console, and the Manage Dumps history is incomplete. Do not interpret the
  presence of the menu as proof that a dump has completed successfully.

See [docs/DUMP_GAME_APP.md](docs/DUMP_GAME_APP.md) for the integration notes.

## Your own covers and names
Make a folder called `CoverFlowCovers` on a USB drive and put in it:
- images named by title id, such as `PPSA01650.png` or `PPSA01650.jpg` (square
  looks best, up to 8 MB each), and
- optionally a `names.txt` with one game per line: `PPSA01650 | My name for it`.

Plug the drive into the PS5 and choose Settings > Import covers and names from USB.
They are copied into Cover Flow's own folder (`covers/custom/`), so the home screen
and the console's data stay as they are. To undo one, open the game's menu
(Triangle) > Use the original name and cover. Use art you made or have the right to
use.

## Updating
Settings > Check for updates. Cover Flow downloads `eboot.bin`, `cflaunch.elf` and
`SHA256SUMS` from the latest release, checks every file, and only then replaces the
old ones. It then closes itself: open it again from the home screen.

Coming from v15 or older: install v16 by hand once over FTP (older updaters left
`eboot.bin` unstartable).

## Releases
Each release has `eboot.bin`, `cflaunch.elf` and `SHA256SUMS` (used by the in-app
updater) and the full title folder as `PPSA99123_vNN.zip`, tagged with the version
(v13, v14, ...). Publish it as the latest release, not a pre-release.

## Building
Built inside PS5_VulkanTemplate: `source/coverflow.cpp` is the sample, the shaders
go in `shaders/glsl/coverflow/`, and `source/make_files.py` embeds the compiled
shaders and `sce_sys/icon0.png` in the eboot (run it after changing either).
`cflaunch` builds with the PS5 Payload SDK using `source/cflaunch.Makefile`.

## Changes
- **v22 (candidate, not yet released)** - Adds the Dump Game / App workflow, corrects queue submission to the upstream PS5 App Dumper API, and connects Manage Dumps to the dumper's real `/api/dumps` history. The upstream dumper supports USB output only; its web UI remains the source for live byte progress and final job status. This branch has not yet been compiled or console-tested.
- **v17** - Your own names and covers (imported from USB, kept in Cover Flow's own
  folder, undo per game), Largest first sort, free space on every drive in Settings.
  Launcher 11 only accepts commands from the console itself, checks title ids
  strictly, and fixes file permissions after a USB restore.
- **v16** - Updates install files the console can start: the updater wrote
  `eboot.bin` without the execute permission, so Cover Flow showed "Can't start the
  game or app" (CE-107750-0) after updating. The launcher (10) now fixes permissions
  and Cover Flow checks them before replacing anything.
- **v15** - Game artwork now loads (Cover Flow read only the first piece of each
  picture); artwork is also looked up in the game's own folder; return after a game
  waits until the game has fully closed, checks Cover Flow really started and tries
  once more if not (launcher 9).
- **v14** - Updates and USB restores close Cover Flow instead of restarting it
  (the restart could freeze the console); launcher 8 refuses self-restarts.
- **v13** - Game artwork backgrounds, play time and Recently played, grid view,
  letter jump, right-stick scrolling, collections, theme picker, clock and storage
  in the header, interface sounds, USB backup and restore, launcher log, release
  notes in the update dialog.
- **v12** - Updates download from inside Cover Flow (the launcher's HTTPS failed).
- **v11** - Visual overhaul and new icon.

## Licences
- Cover Flow (`source/coverflow.cpp`, shaders, `make_files.py`): MIT
- cflaunch (`source/cflaunch.c`): GPL-3.0-or-later, based on PS5_Vulkan's ps5vkctl
- The title links the PS5 platform layer (GPL-3.0-or-later), RADV/Mesa (MIT and
  others), Dear ImGui, KTX, tinygltf and volk; their licences are in `licenses/`.

No games, firmware, keys or anything from the console are included or ever will be.
Not affiliated with Sony Interactive Entertainment. "PlayStation" and "PS5" are
trademarks of Sony Interactive Entertainment Inc.
