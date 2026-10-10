# Cover Flow for PS5

A Cover Flow game library for jailbroken PS5 consoles (homebrew), built on
[PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate).

## Features
- Your installed games as a row of covers with reflections, or a grid (L3)
- The selected game's artwork fades in behind the covers
- Starts games, and can bring Cover Flow back when the game closes
- Play time per game, with Recently played and Most played sorts
- Favourites, hide, categories (PS5 / PS4 / Emulators / Homebrew) and six collections
- Sort and filter, letter jump (L2/R2), fast scroll with the right stick
- Details panel with size, install date, play time and storage location
- Themes: six built in, your own picture/colours/font/music, and interface sounds
- Header with clock and free storage
- USB backup and restore of settings, collections and play time
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
Download `PPSA99123_v14.zip` from the [latest release](https://github.com/awaisa123/coverflow/releases/latest),
extract it and copy the `PPSA99123` folder to `/data/homebrew/` on the PS5 (with
ShadowMountPlus). The folder already contains `cflaunch.elf`; Cover Flow loads its
launcher through the payload loader on port 9021 whenever the running launcher is
older or missing.

## Updating
Settings > Check for updates. Cover Flow downloads `eboot.bin`, `cflaunch.elf` and
`SHA256SUMS` from the latest release, checks every file, and only then replaces the
old ones. It then closes itself: open it again from the home screen.

Coming from v13: install v14 by hand once (v13's updater restarted itself, which
could freeze the console).

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
