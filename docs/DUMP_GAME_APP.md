# Dump Game / App — v22 candidate

This document describes the current Cover Flow feature-branch integration. It is
not a claim that a PS5 dump has been completed or that this candidate is release-ready.

## Current implementation

- The game menu includes **Dump Game / App** and **Manage Dumps**.
- The Dump Game / App flow includes destination selection, confirmation, progress,
  and a result screen.
- The UI starts a background task and calls the external dumper integration in
  `source/coverflow.cpp`.
- The client looks for a separately installed `ps5-app-dumper.elf`, attempts to
  load it using the configured local payload loader when needed, discovers the
  dumper API, asks it for available devices, selects a writable USB destination,
  and submits the selected title to the dumper queue using the documented
  `q0`, `target`, and `replace` fields.
- The client treats queue acceptance as **Queued**, not completed. The external
  dumper may launch the selected title and close Cover Flow, so live byte-level
  progress and final completion must currently be followed in the dumper's own
  web interface.
- Manage Dumps now requests `GET /api/dumps` and renders real entries returned
  by the external dumper, including title ID/name, output folder, destination,
  state, and size when supplied. The screen can refresh and browse the list.
- Delete/export/rename actions are not connected in Cover Flow. In particular,
  do not present queue acceptance as proof that a finished dump exists.

## Expected setup

1. Build and install Cover Flow using the PS5 Vulkan Template.
2. Obtain and install a compatible PS5 App Dumper payload separately; it is not
   included in this repository.
3. Ensure the local payload loader is available when the integration needs it.
4. Connect a writable internal or supported USB destination.
5. Confirm that the external dumper build exposes the API endpoints and response
   fields expected by the current client. The client currently discovers the
   service and requests its device list before submitting a queue job.

## Important status semantics

- **Preparing** means the client is attempting to find/start the dumper and choose
  a destination.
- **Queued** means the external dumper accepted a request for later processing.
  It does not mean the game/app has been fully extracted or verified.
- **Complete** must only be shown when the external dumper reports a terminal
  success result. If the external service only confirms queue submission, show
  “Queued” instead.
- **Failed** should display the error returned by the integration, such as a
  missing payload, unavailable service, unsupported destination, or invalid title ID.
- Never manufacture a percentage when the backend does not provide actual progress.

## Remaining work before calling v22 complete

- Build the main app and launcher in the intended PS5 Vulkan Template environment.
- Confirm the exact dumper API and queue/result semantics against the compatible
  external dumper version.
- Add safe management actions only after the backend's constraints are explicit.
  The upstream delete endpoint only removes incomplete dumps; it must not be
  exposed as a generic delete for completed dump folders.
- Display timestamps and richer job metadata if the external API exposes them.
- Test cancel/error/retry paths and verify that UI status matches backend status.
- Produce new release artifacts and checksums only from the successful build outputs.

## Scope

This integration is separate from Cover Flow's existing USB backup/restore feature,
which backs up Cover Flow settings, collections, names/covers, and play time. It
does not replace that workflow.

The external dumper payload is not bundled here. Use only content you are authorized
to back up, and keep the external tool's license and compatibility requirements.
