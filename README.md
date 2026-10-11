# Google Flow — PS5 Homebrew Game Manager

**A controller-first game library for jailbroken PlayStation 5 consoles, built with Vulkan/RADV and Dear ImGui.**

[![Latest release](https://img.shields.io/github/v/release/awaisa123/coverflow?label=latest%20release)](https://github.com/awaisa123/coverflow/releases/latest)
[![Repository license](https://img.shields.io/github/license/awaisa123/coverflow)](#licenses-and-credits)
[![Project status](https://img.shields.io/badge/v28-in%20development-orange)](#development-status)

> **Project name:** Google Flow (formerly **Cover Flow**). The repository is still named `coverflow`, and the PS5 title ID remains `PPSA99123`.
>
> **Development status:** A v28 build has passed reported local compilation and host-side tests. **That does not establish that v28 has been deployed to, or verified on, a physical PS5.** For a downloadable, published build, consult [Releases](https://github.com/awaisa123/coverflow/releases). Do not assume the latest release contains every feature described as *in development* here.

## Overview

Google Flow is a visual launcher and library manager for PS5 homebrew environments. Browse installed titles as animated covers, switch categories with your controller, review game details, customize artwork, and launch titles through a resident launcher.

The current development direction prioritizes a polished six-screen interface, reliable launcher behaviour, USB/settings management, diagnostics and careful preservation of working features.

### Features in the Cover Flow release lineage

- **Cover-based game browser** with artwork backgrounds, reflections and an optional grid.
- **Library tools:** sorting, filtering, favourites, hidden titles, categories and collections.
- **Launch and return:** launch a title and optionally return to the manager once it has fully closed.
- **Game information:** play time, installed size, installation date and storage details where available.
- **Personalization:** custom names, covers, themes, interface sounds and related settings.
- **Settings portability:** back up and restore *Google Flow settings and personalizations* to USB. This is **not** a game backup.
- **Update support:** released builds may offer checks against this repository's GitHub releases, with file-hash verification.

The exact available options depend on the installed release.

### v28 development work — not yet hardware-verified

The current local v28 development tree has reported builds and host regression tests for:

- Revised six-screen Vulkan/ImGui interface and Game Manager.
- OnionHEN-oriented service detection, including configurable FTP services.
- Resident `cflaunch` **protocol v15** and guarded launcher upgrade behaviour.
- Separate workflows for copying an **existing accessible image**, archiving **ordinary readable installed files**, and checking for a **running application's readable mount**.
- SHA-256/read-back checks, capacity checks and cancellation in applicable copy workflows.
- A read-only `gfdiag` diagnostic proxy for a separate PS5 control service and a WSL report utility.

**Limitations:** A folder archive is **not** automatically a playable or restorable PS5 game. Availability of application mounts, permissions, payload loading and the newer workflows remains dependent on the particular console environment and requires physical-console verification. Existing image copying does **not** create new FFPKG, PFS or PKG images. The diagnostic proxy also requires a compatible resident control payload; the Python utility alone cannot enable it.

## Screenshots

Screenshots of the current interface will be added here after selecting and verifying captures from the intended build. This README deliberately avoids using older screenshots as if they were v28.

## Controller guide

| Control | Action |
| --- | --- |
| D-pad / left stick | Navigate the current view |
| **Touchpad** | Switch top-level categories in the redesigned interface |
| **L1 / R1** | Browse games in the redesigned interface |
| **Triangle** | Open Game Manager |
| **Cross** | Select or launch, depending on the current screen |
| **Circle** | Back / close / cancel where supported |
| **Options** | Settings, where mapped |

Other inputs, including search, sorting and grid toggles, can differ between published Cover Flow releases and the v28 redesign. Follow the on-screen hints for the version you installed.

## Requirements

- A PS5 configured to run compatible homebrew software.
- A working PS5 Vulkan/RADV environment for the relevant build.
- A compatible payload loader and control/FTP services for functionality that needs them.
- Optional USB storage for supported settings/backup workflows.

**Compatibility note:** OnionHEN v0.9.0-beta2 is a *development compatibility target*, not an across-the-board certification. ShadowMountPlus 1.7-beta5fix1 is being researched independently; Google Flow does **not** automatically mount images or install/change ShadowMountPlus.

## Installation

**For users:** Download an appropriate archive from the [GitHub Releases page](https://github.com/awaisa123/coverflow/releases). Read that release's notes before installing; the release may be older than the local v28 development code.

The historical Cover Flow release packaging uses a `PPSA99123` title directory:

```text
/data/homebrew/PPSA99123/
    eboot.bin
    cflaunch.elf
    ...
```

Transfer the **complete directory from the chosen release** using a file-management method compatible with your homebrew environment. Do not mix the frontend and launcher from unrelated builds. Restarting the console may be necessary when upgrading a resident launcher, because an older process can remain in memory after closing the UI.

**Do not load a second resident control/launcher payload over an already-running one.** Check the release instructions and avoid changing an active PS5 session without a rollback plan.

## Custom covers and names

In published Cover Flow versions that support USB import, create a `CoverFlowCovers` directory on your USB drive with artwork named by title ID:

```text
CoverFlowCovers/
    PPSA01650.png
    PPSA01651.jpg
    names.txt
```

Optional `names.txt` entries use the form:

```text
PPSA01650 | My custom title
```

Use the application's import option when available. Custom display artwork and names are intended to stay within the manager; use artwork you own or have permission to use.

## Diagnostics and safety

The v28 development tree includes a WSL-side diagnostic client:

```bash
python3 tools/google_flow_diagnostics.py --host YOUR_PS5_IP
```

This tool reports supported service status and can generate a sanitized JSON report. More detailed `gfdiag` responses require an updated, compatible `ps5vkctl` resident payload. A TCP port responding does not by itself verify the identity or compatibility of the service.

**Security:** The historical PS5 control service on TCP port `9111` provides privileged actions. Treat that port as sensitive: use only a trusted network and do not expose it to the public internet. Authentication and responsiveness improvements are under investigation; don't assume they are already present in a release.

Never share console secrets, credentials, private identifiers, save data or game content in diagnostic reports.

## Development and building

The original project builds on [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate), using Vulkan/RADV and Dear ImGui. The resident launcher is built using the PS5 Payload SDK.

The current v28 development working tree has a different layout from the historical published repository. In particular, development reports refer to:

```text
examples/coverflow/
payload/cflaunch/
ps5/tools/
tests/
docs/
```

These paths describe **the local development tree**, not a guarantee that every directory has already been pushed to `main`. Check out the code associated with the particular release you intend to build and follow its build instructions.

## Development status and roadmap

| Area | Status |
| --- | --- |
| Published Cover Flow releases | Available on GitHub Releases |
| Google Flow v28 UI and launcher updates | Local build/host validation reported |
| OnionHEN service compatibility | Host-tested; console verification pending |
| Read-only diagnostic proxy | Host-tested; upgraded resident payload required |
| Mounted-source detection and backup workflows | Host-tested; real-console availability unverified |
| Control-service security and Vulkan stability improvements | Under investigation |
| ShadowMountPlus integration | Research / future work |

This status table describes development progress and is **not** a guarantee that a published release includes each item.

## Releases and updates

See [Releases](https://github.com/awaisa123/coverflow/releases) for downloadable versions, release-specific installation notes and packaged files.

Some historical builds support in-app updating with SHA-256 verification. Always follow the release-specific guidance, particularly when a frontend or launcher protocol changes. Keep a known-working copy before updating.

## Licenses and credits

The project includes components under different licenses:

- Original Cover Flow UI code and project-specific assets: see the corresponding source notices (historically MIT for the UI).
- Resident launcher and PS5 platform components: GPL-3.0-or-later in the historical repository.
- Vulkan/RADV/Mesa, Dear ImGui and other dependencies: see the accompanying license notices and component manifests for the actual release.

Licensing and attribution must be reviewed again before distributing new development builds or incorporating third-party source.

Thanks to contributors to the PS5 homebrew tooling, Vulkan/RADV, PS5 Payload SDK and related open-source projects.

---

**Disclaimer:** Google Flow is an independent homebrew project and is not affiliated with or endorsed by Sony Interactive Entertainment. “PlayStation” and “PS5” are trademarks of their respective owner. No games, firmware, encryption keys or proprietary console content are distributed with this repository.
