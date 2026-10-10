# Cover Flow - handoff

Where the work stands, for whoever picks it up next. Durable facts are in
[MEMORY.md](MEMORY.md).

_Last updated: 10 October 2026 (v22)._

## Releases
| Version | Launcher | State |
| --- | --- | --- |
| v13-v15 | 7-9 | Their updaters leave eboot.bin unstartable. Never update from them in-app. |
| v16 | 10 | Installed by FTP; first updater that installs startable files. |
| v17 | 11 | First in-app update that worked. |
| v18 | 12 | Back up and Restore wizard. |
| v19 | 12 | New blue look; installed on the console in-app and confirmed. |
| v20-v21 | 12 | Showcase layout; the look matched to the design (bold font, pill, footer bar). Published. |
| v22 | 12 | **Search (R3, on-screen keyboard). Built; to publish and test on the console.** |

## v22
- R3 opens a keyboard panel at the bottom (digits, QWERTY, ' - : &, Space, Delete,
  Clear, Done). Cross types, Square deletes, Triangle adds a space, L1/R1 browse the
  matches, Circle/R3/Options close it. The covers filter as you type.
- Matching: lower-case letters and digits only, on the shown name, the original name
  and the title id; names that start with the search come first. While there is a
  search it shows every game that is not hidden, whatever the filter. Circle on the
  main screen clears it; the touchpad (filter) also clears it. Not saved between runs.
- Header: magnifier icon (lit while searching), "Search: ..." chip instead of the
  filter. Footer: R3 Search, and Circle Clear search while one is set.
- COVERFLOW_TEST: `search=TEXT` (underscores for spaces), `keyboard`, `key=RC`.
- Checked on the PC reference build (keyboard, matches, no matches, grid). Not yet
  checked on the console. The launcher is unchanged (12).

## Next steps
1. Publish v22 as **Latest** with `eboot.bin`, `cflaunch.elf`, `SHA256SUMS`,
   `PPSA99123_v22.zip`.
2. On the PS5: Settings > Check for updates > Update now; reopen; try R3.
3. Still to test on the console: USB backup and restore, artwork on PS5 games.

## Loose ends
- The home-screen tile still shows the first "CF" icon: the PS5 caches it at
  registration; both icon files now hold the new design. Re-registering through
  ShadowMountPlus should refresh it; editing system data is not an option.
- Declined, with reasons in MEMORY.md: app.db renames, other games' icons, moving games
  to USB, FFPKG install/store, scraping art online, game dumping (also as a demo UI, and pull request #2's
  ps5-app-dumper integration),
  Home-menu redirect.
- Not possible with the documented pad data: touchpad swipes, controller battery.
