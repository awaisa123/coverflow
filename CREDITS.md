# Credits, upstream projects and third-party notices

Google Flow (formerly Cover Flow) builds on work by the PS5 homebrew and open-source graphics communities. Thank you to the authors, maintainers and contributors of the projects listed below.

This document distinguishes **components identified in this repository's existing license/source records** from **projects consulted as technical references**. Reference or inspiration does not imply that any source code was copied.

## Identified foundations and bundled components

| Project / contributors | Role in Cover Flow | License / evidence |
| --- | --- | --- |
| [Mihawk-99](https://github.com/mihawk-99) — [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) | PS5 Vulkan application template and platform foundation; identified in the original README | Source history and bundled notices |
| [Mihawk-99 — PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | RADV integration, runtime and PS5-specific bootstrap; historical launcher credits the `ps5vkctl` control payload | GPL-3.0-or-later; `licenses/README.txt`, `source/cflaunch.c` |
| [Mihawk-99 — PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | PS5 platform SDK fork used by the historical title | GPL-3.0-or-later components; `licenses/README.txt` |
| [John Törnblom](https://github.com/john-tornblom) and [ps5-payload-dev contributors](https://github.com/ps5-payload-dev) | PS5 payload SDK and platform development | License records in `licenses/platform/` |
| [Mihawk-99 — PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) and [Mesa contributors](https://www.mesa3d.org/) | RADV/Mesa graphics driver port and underlying graphics implementation | MIT and other per-file licenses; `licenses/radv/` |
| [Sascha Willems](https://github.com/SaschaWillems) — [Vulkan examples](https://github.com/SaschaWillems/Vulkan) | Vulkan example/base-class foundation named in the repository's license documentation | MIT; `licenses/README.txt` |
| [Omar Cornut](https://github.com/ocornut) and [Dear ImGui contributors](https://github.com/ocornut/imgui) | Immediate-mode UI library | MIT; see bundled component notices |
| [Khronos Group](https://www.khronos.org/) and [Vulkan-Headers contributors](https://github.com/KhronosGroup/Vulkan-Headers) | Vulkan API interfaces and headers | Apache-2.0 or MIT (as recorded in the project) |
| [Khronos — KTX-Software](https://github.com/KhronosGroup/KTX-Software) | Texture tooling/library (libktx) | Apache-2.0 |
| [syoyo — tinygltf](https://github.com/syoyo/tinygltf) and its dependency authors | glTF loader with stb_image and nlohmann/json components | Respective bundled/per-file notices |
| [volk contributors](https://github.com/zeux/volk) | Vulkan function-loader utility | MIT |
| [g-truc — GLM](https://github.com/g-truc/glm) | Graphics math library | MIT; `licenses/components.json` records upstream submodule |
| [LLVM project contributors](https://llvm.org/) | libc++, libc++abi, libunwind and compiler-rt linked in historical builds | Apache-2.0 with LLVM exception; `licenses/llvm-runtime/` |
| [musl contributors](https://musl.libc.org/) and Doug Lea's dlmalloc | Regex and memory allocator components in the platform layer | MIT/public domain as recorded in `licenses/README.txt` |

The historical repository includes more detailed attribution and identified upstream revisions in [`licenses/README.txt`](licenses/README.txt) and [`licenses/components.json`](licenses/components.json). These records should be kept with distributed binaries, together with any other applicable notices.

## Related homebrew projects and technical references

The following projects have been discussed or researched while planning the newer Google Flow work. They are **acknowledged as technical references**, **not asserted here as copied or embedded code**:

- [LightningMods — Itemzflow](https://github.com/LightningMods/Itemzflow) — game-library management and user-experience reference.
- [EchoStretch — ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) — architecture reference for PS5 application dumping and resident workers.
- [Mihawk-99 — PS5_VKHomebrewUI](https://github.com/mihawk-99/PS5_VKHomebrewUI) — Vulkan homebrew UI reference.
- Other PS5 homebrew tool authors, including those maintaining console loaders, payloads, USB and filesystem tooling.

Mentioning a project here does not indicate collaboration, endorsement or technical compatibility.

## Contributions and project maintenance

Thanks to everyone who maintains, documents, tests and improves the open-source libraries and PS5 homebrew environment that make independent projects possible.

**Maintainer:** [awaisa123](https://github.com/awaisa123).

Development has also made use of AI-assisted code review and engineering tools. AI tool use does not replace the attribution, license obligations or copyright notices of human authors and upstream projects.

## Licensing clarification

Different source components retain their own licenses and copyright notices. The project's existing [license summary](licenses/README.txt) describes the historical distributed title as GPL-3.0-or-later as a whole because of GPL-covered platform components, even though some individual Cover Flow/UI components are MIT-licensed.

**Do not treat this credits file as a substitute for the full license texts or upstream source requirements.** Review the licenses and dependency manifest for each actual binary build before distributing it, especially as the v28 development tree differs from historical published source.

If attribution is missing or inaccurate, open a GitHub issue with the component, upstream URL and relevant source/license notice so it can be corrected.
