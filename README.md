# Cover Flow for PS5

A Cover Flow game library for jailbroken PS5 consoles (homebrew), built on
[PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate).

- Your installed games as a row of covers with reflections
- Starts games, and comes back when the game closes
- Favourites, hide, categories (PS5 / PS4 / Emulators / Homebrew), sort and filter
- Themes (background, colours, font, music), settings on OPTIONS
- Updates itself from this repository's releases (checked with SHA-256)

## Install
Copy the `PPSA99123` folder to `/data/homebrew/` (with ShadowMountPlus) and put
`cflaunch.elf` in it. Cover Flow loads its launcher through the payload loader
on port 9021.

## Releases
Each release has `eboot.bin`, `cflaunch.elf` and `SHA256SUMS`, tagged with the
version (v10, v11, ...).

## Licences
- Cover Flow (`source/coverflow.cpp`, shaders): MIT
- cflaunch (`source/cflaunch.c`): GPL-3.0-or-later, based on PS5_Vulkan's ps5vkctl
- The title links the PS5 platform layer (GPL-3.0-or-later), RADV/Mesa (MIT and
  others), Dear ImGui, KTX, tinygltf and volk; their licences are in the title
  folder's `licenses/`.

No games, firmware, keys or anything from the console are included or ever will be.
Not affiliated with Sony Interactive Entertainment. "PlayStation" and "PS5" are
trademarks of Sony Interactive Entertainment Inc.
