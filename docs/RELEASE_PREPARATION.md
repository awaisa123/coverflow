# Google Flow — Screenshot and Release Preparation

This is a staging checklist. It **does not** announce a new release or imply that development builds have been verified on PS5.

## README gallery — images needed

Capture screenshots from the **same verified Google Flow build** at 1920×1080 (or native console capture resolution), ideally without photographs of the screen:

| Suggested filename | Screen | What to show |
| --- | --- | --- |
| `docs/screenshots/home.png` | Home | Cover carousel, selected-game artwork, category tabs |
| `docs/screenshots/game-manager.png` | Game Manager | Available game-management options, no simulated success state |
| `docs/screenshots/settings.png` | Settings | Current 3-column settings screen and service/status context |

Before publishing, verify the screenshots match the build and do not contain personal data, account identifiers, network addresses or unrelated game/save content. Only use screenshots and cover artwork you may redistribute.

Once approved, add these files and replace the README's screenshot placeholder with:

```markdown
## Screenshots

| Home | Game Manager | Settings |
| --- | --- | --- |
| ![Google Flow Home screen](docs/screenshots/home.png) | ![Google Flow Game Manager](docs/screenshots/game-manager.png) | ![Google Flow Settings](docs/screenshots/settings.png) |
```

## Public releases versus local development

- **Latest published release observed:** [Cover Flow v26](https://github.com/awaisa123/coverflow/releases/tag/v26).
- **Google Flow v28:** under local development, with reported host tests and compilation; physical PS5 verification and security maintenance are still in progress.
- Do **not** upload v28 as the latest release or redirect users to a new installer until the PS5 validation steps and artifact checks complete.

## v28 release candidate checklist

- [ ] Confirm exact source revision and tested PS5 firmware/homebrew environment.
- [ ] Verify the launcher, frontend and control payload are compatible with each other.
- [ ] Check the console restart/older resident process migration path.
- [ ] Test controller navigation, artwork, game launch/return and settings operations on PS5.
- [ ] Test read-only diagnostic status and verify that exports redact private data.
- [ ] Review LAN control-service authentication and deployment/rollback plan.
- [ ] Test genuine USB detection, disconnection, capacity reporting and settings restore.
- [ ] For any backup workflows, publish only verified capabilities and clearly identify unavailable/experimental operations.
- [ ] Collect 3 genuine screenshots from that build.
- [ ] Review license notices and whether any third-party code was incorporated.
- [ ] Generate fresh SHA-256 hashes for packaged artifacts and check the complete distribution.
- [ ] Prepare release notes listing confirmed changes, known limitations and rollback instructions.
- [ ] Publish only after explicit approval.

## Draft release-note outline — NOT FOR PUBLICATION YET

**Google Flow v28 — development candidate (unreleased)**

### In development
- Updated Vulkan/ImGui game manager and controller-first dashboard.
- Revised launcher checks and diagnostic services.
- OnionHEN-oriented service compatibility checks.
- Separate verified-file copying and mounted-source discovery workflows.

### Must clarify at release time
- Which features were tested on physical hardware.
- Which firmware/homebrew environments are supported.
- Whether the PS5 diagnostic proxy has been upgraded and how access is secured.
- Whether installed-game mounted sources are actually available.
- Whether any backup output is playable or restorable (do not claim unless proved).

### Distribution
Do not attach or publish untested binaries based on this outline.
