# Google Flow v29 — Engineering Baseline and Upstream Feature Inventory

> Status: initial source-backed inventory, not a completed repository-wide or local WSL audit. Documentation-only proposal; **no v29 implementation or PS5 deployment**.

## Current baseline

- Public source: [awaisa123/coverflow](https://github.com/awaisa123/coverflow), title `PPSA99123`; public README marks v28 in development.
- Physical-console results supplied by the developer: game launch and return work; copying an existing 3 GB `.ffpfsc` image to USB completed and passed full destination SHA-256; front/rear transfer ~2.5–2.6 MB/s; a later 25 ms pacing candidate reached a fluctuating **peak** 3.7 MB/s. These are **user-reported hardware observations**, not independently reproducible from public GitHub.
- The newest local WSL v28 files, control payload and deployed executable may not be represented on the public main branch. Do not equate the public repository with the installed PS5 build.
- Keep the known-good `eboot.bin`, `cflaunch` v15 and authenticated `ps5vkctl` intact until an isolated candidate passes regression tests, integrity verification and manual approval.

## Sources inspected directly

| Project | Source file or documentation | What it establishes |
|---|---|---|
| [EchoStretch/ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) | [source/utils.c](https://github.com/EchoStretch/ps5-app-dumper/blob/main/source/utils.c) | Includes `sys/aio.h`; defines progress counters, progress thread and cooperative abort controls. GPL-3.0-or-later header; code reuse needs licence review. |
| [EchoStretch/ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) | [source/ps5_dumper.c](https://github.com/EchoStretch/ps5-app-dumper/blob/main/source/ps5_dumper.c) | `dump_ps5_ppsa_app()` accepts sandbox, app folder and USB mount path, with USB-oriented path/log setup. Dumping accessible application files is **not** the same operation as copying an existing `.ffpfsc` image. |
| [EchoStretch/ps5-app-dumper](https://github.com/EchoStretch/ps5-app-dumper) | [README.md](https://github.com/EchoStretch/ps5-app-dumper/blob/main/README.md) | Describes USB file outputs and web UI control; the web UI is not a network file-transfer path. |
| [LightningMods/Itemzflow](https://github.com/LightningMods/Itemzflow) | [README.md](https://github.com/LightningMods/Itemzflow/blob/main/README.md) | Lists filesystem/NFS, SQLite, INI settings, audio and logging dependencies. A dependency list does **not** prove how each feature works or whether the same code works on PS5. |
| [mihawk-99/PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) | [README.md](https://github.com/mihawk-99/PS5_VulkanTemplate/blob/main/README.md) | Describes PS5-native Vulkan samples, controller input, themes/UI kit and console testing. Individual sample APIs require verification. |

This is an evidence register, not a claim to have opened every file. For each adopted feature, add exact upstream commit, source function, licence, local counterpart, tests and observed console result.

## Whole-application workstreams

| Workstream | Verify in actual local Google Flow source | Next engineering outcome |
|---|---|---|
| Installed games and library | Title enumeration, icon/cover loading, cache invalidation, sorting, favorites and missing entries | Accurate indexing with bounded memory and graceful fallbacks |
| Launch and return | `cflaunch` protocol, acknowledgements, user context, timeout and foreground handoff | Preserve confirmed working game launch/return; test errors and restart |
| DLC / updates / metadata | Available platform APIs, installed DLC enumeration, versions and per-title associations | Read-only inventory first; no unsupported promises |
| Backups and USB | Mount discovery, free space, destination selection, filesystem types, copy and verify semantics | Separate transfer worker from UI scheduling |
| Backup library and queue | Completed/incomplete items, collision policy, repair/resume semantics | Durable job IDs, explicit cleanup, safe queue design |
| File browser | Path authorization, symlinks, rename/move/delete semantics | Read-only inspection before mutation support |
| Settings and themes | Schema migration, backup/restore, font scaling, focus, media and audio preferences | Recoverable settings and accessible UI |
| Controller / Vulkan | Input mapping, focus order, GPU descriptor/resource lifetime, long-session stability | Repeatable console frame/input regressions |
| Networking | LAN status, NFS possibility, authorization, cancellation, telemetry and logs | Read-only diagnostics and later per-action approved control |
| Windows monitor | WSL diagnostic-client output, subprocess timeouts, redacted export, UI responsiveness | Standalone Windows viewer before control |
| Build/release/security | Notices, key isolation, deterministic artifacts, deployment rollback and hashes | Traceable, approval-gated releases |

## First v29 implementation candidate — background image copy

The local Codex audit (not yet synced to public source) found frontend-driven `image-step` calls that advance one 256 KiB chunk at a time. A 100 ms polling interval imposed a nominal ~2.62 MB/s ceiling; reducing it to 25 ms raised peak throughput but did not remove synchronous command overhead.

Build an **isolated** launcher-worker implementation that continues independently of the frontend. An inexpensive read-only `image-status` should expose phase, byte counts and errors. Maintain one active job unless an explicit queue design and concurrency tests justify more.

Preserve exclusive destination creation, source path and symlink validation, device checks, cancellation, partial-file handling, SHA-256 hashing while copying, **complete destination read-back SHA-256 verification**, and final-file publication only after success. Test read/write short counts, EINTR, storage removal, ENOSPC, disconnects, verifier mismatch, stalled polling, shutdown, repeated starts and protocol mismatch.

Benchmark 1/4/8 MiB buffers on host and hardware; do not claim a speed target from buffer sizes alone. Reduce per-chunk logging only after correct failure/transition logs remain. The UI must stay responsive and should not drive progress.

## Required release gates

1. Snapshot exact local v28 source, binaries, SHA-256 values, toolchain and rollback archives.
2. Pin upstream commit revisions and inspect relevant functions (not README features alone).
3. Produce a source-to-source feature-gap matrix and identify unsupported/PS4-only APIs.
4. Make changes in isolated v29 artifacts with host correctness/security tests.
5. Test physical PS5 rendering, launch/return, cancellation, interruption and end-to-end SHA-256.
6. Require explicit approval before uploading a new frontend/launcher, changing GitHub main or publishing a release.

## Immediate next steps

- Sync the latest local WSL Google Flow implementation with a **reviewable branch** without exposing Ed25519 private keys, console logs or identifiers.
- Finish the upstream feature audit for Itemzflow, PS5 App Dumper, PS5 Vulkan and related repositories, recording exact commits and functions.
- Implement the standalone transfer engine with regression fixtures, then expand to library, settings, DLC and connected-monitoring enhancements.

**Do not mark an upstream capability as implemented in Google Flow until source, host tests and required PS5 verification support it.**
