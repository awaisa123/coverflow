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
- Back up and Restore wizard: choose a USB drive, see what is saved, real progress;
  every file is read back and checked with SHA-256
- Showcase layout (tall covers) or Classic, in Settings
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
shaders, `sce_sys/icon0.png` and `assets/Roboto-Bold.ttf` in the eboot (run it after
changing any of them). `source/base/VulkanUIOverlay.cpp` replaces the template's
`base/VulkanUIOverlay.cpp`: it lets the overlay draw cover textures and loads the bold
font.
`cflaunch` builds with the PS5 Payload SDK using `source/cflaunch.Makefile`.

## Changes
- **v21** - The blue look matched to the design: bold headings, star and gear in the
  header, last played and play time in a pill, a bar behind the button guide, clearer
  backup and restore screens with time remaining.
- **v20** - Showcase layout: tall covers, gently turned, with a larger selected cover
  (Classic is in Settings).
- **v19** - New look: navy panels with a blue glow, icon game menu with the real cover.
- **v18** - Back up and Restore wizard (launcher 12): choose a drive, confirm, real
  progress with Cancel, every copy checked with SHA-256, unfinished backups never
  restored. Backups include your own covers and names.
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
- Roboto Bold (`source/assets/Roboto-Bold.ttf`): Apache-2.0, Christian Robertson /
  Google Fonts (`licenses/title/Roboto-Apache-2.0.txt`)
- The title links the PS5 platform layer (GPL-3.0-or-later), RADV/Mesa (MIT and
  others), Dear ImGui, KTX, tinygltf and volk; their licences are in `licenses/`.

No games, firmware, keys or anything from the console are included or ever will be.
Not affiliated with Sony Interactive Entertainment. "PlayStation" and "PS5" are
trademarks of Sony Interactive Entertainment Inc.
