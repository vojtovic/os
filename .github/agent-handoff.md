# Agent Handoff Context

Use this file as the shared context between agents in this workspace.

## Current Objective
- Notes app: add Obsidian live-preview markdown in write mode — committed lines styled (# → H1, - → bullet, etc.), cursor line stays raw so you see syntax while typing.

## Execution Plan (Notes App Improvements)
1. Strategy: add note picker, bigger buffer, char counter, auto-save, and live-preview markdown.
2. Build: implemented all features in DisplayManager.cpp and SystemState.h.
3. Verify: platformio run.
4. Review: confirm picker nav, file create/delete, mode switching, auto-save.

## Stage Status (Notes App Improvements)
- Live preview: done, evidence: notesStyleLineForBrowse() applied to committed lines in write mode; cursor line stays raw.
- Note picker: done, evidence: new kNotesViewPicker mode with file listing, create (n), delete (d), open (enter/right).
- Buffer upgrade: done, evidence: kNotesMaxChars bumped from 1200 to 4096.
- Char counter: done, evidence: header now shows filename + chars used/max.
- Auto-save: done, evidence: write→read mode switch auto-saves dirty notes.
- Forward decl fix: done, evidence: added missing notesLineContainsTag forward declaration.
- Cursor editing: done, evidence: full cursor movement with all 4 arrows in write mode, insert/delete at cursor position.
- Raw write mode: done, evidence: write mode shows raw `# heading` syntax, read mode renders styled `H1 heading`.
- Space as text: done, evidence: space inserts a space character at cursor, no more word-commit behavior.
- Key remapping: done, evidence: Tab=toggle write/read, Esc=save+picker, arrows=cursor(write)/scroll(read).
- Verify: done, evidence: platformio run SUCCESS (2026-04-18), RAM 27.1%, Flash 28.9%.

## Execution Plan (Direct HTTPS Web Explorer)
1. Strategy: replace HTTP-only web upload serving with direct HTTPS on ESP so no always-on external proxy is required.
2. Ideas: use an ESP32-S3 compatible HTTPS server library with self-signed certificate generation and keep existing file-explorer endpoint behavior stable.
3. Build: migrate `WebUploadManager` server lifecycle and handlers to HTTPS callbacks and keep shell commands/start-stop flow unchanged.
4. Verify: run `platformio run` and ensure firmware links with HTTPS server stack.
5. Review: check for regressions in `web-upload status|start|stop`, WiFi gating, and SD/LittleFS file operations.
6. Optimize: keep fallback behavior to plain HTTP where HTTPS startup fails, so recovery remains deterministic.
7. Explain: document self-signed certificate behavior and browser warning expectation.

## Stage Status (Direct HTTPS Web Explorer)
- Strategy: done, evidence: user requested direct HTTPS without a permanently hosted proxy, next stage: ideas.
- Ideas: done, evidence: selected `jackjansen/esp32_idf5_https_server` as the IDF5-compatible path after rejecting incompatible `esp32_https_server`, next stage: build.
- Build: done, evidence: migrated `WebUploadManager.cpp` to HTTPS server API with endpoint parity for list/upload/mkdir/delete/rename/download, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-09) with final image generation after dependency conflict resolution in `platformio.ini`, next stage: review.
- Review: done, evidence: existing web-upload lifecycle and shell control surface were preserved while adding HTTPS server flow, next stage: optimize.
- Optimize: done, evidence: retained HTTP fallback server path when HTTPS init is unavailable to keep field recovery straightforward, next stage: explain.
- Explain: done, evidence: direct HTTPS is now firmware-native and requires browser acceptance of a self-signed certificate warning on first access, next stage: on-device smoke test.

## Execution Plan (WiFi Password Persistence)
1. Strategy: persist WiFi credentials in existing config storage to keep settings flow unchanged and avoid new subsystems.
2. Ideas: save credentials only after successful connect and prefill password when user re-selects the same SSID.
3. Build: extend `AppConfig` + `ConfigStore` keys and patch settings connectivity/input modules.
4. Verify: run `platformio run` after patch set.
5. Review: confirm no regressions in WiFi manager navigation and avoid plaintext password printing in shell output.

## Stage Status (WiFi Password Persistence)
- Strategy: done, evidence: user explicitly requested focus on remembering WiFi passwords, next stage: ideas.
- Ideas: done, evidence: selected minimal-risk persistence in `config.ini` with post-connect save + same-SSID prefill, next stage: build.
- Build: done, evidence: patched `SystemState`, `ConfigStore`, `SettingsConnectivityService`, `SettingsInputManager`, and default `data/config.ini`, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-10) after WiFi persistence patch set, next stage: review.
- Review: done, evidence: settings navigation remains unchanged and `printConfig` now masks password value instead of printing plaintext, next stage: on-device validation.

## Execution Plan (Web File Explorer over ESP)
1. Strategy: enable full SD/LittleFS file management through the existing web-upload server so no external SD card reader is needed.
2. Ideas: replace the simple upload page with Explorer-style layout and JSON API endpoints for list/upload/mkdir/delete/rename/download.
3. Build: implement path-safe filesystem helpers in `WebUploadManager` and serve a single-page file explorer UI with toolbar + file table interactions.
4. Verify: run `platformio run` after edits and confirm the server still starts only with active WiFi.
5. Review: ensure existing shell commands (`web-upload status|start|stop`) and launcher integration stay intact.

## Stage Status (Web File Explorer over ESP)
- Strategy: done, evidence: user has no SD reader and requested direct SD management from ESP over web, next stage: ideas.
- Ideas: done, evidence: selected Explorer-like single-page UI with API endpoints (`/api/list`, `/api/upload`, `/api/mkdir`, `/api/delete`, `/api/rename`, `/api/download`), next stage: build.
- Build: done, evidence: replaced simple upload page in `WebUploadManager.cpp` with path-safe filesystem operations and interactive file table UI, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-09) after fixing parser artifacts from large patch, next stage: review.
- Review: done, evidence: web-upload start/stop/status lifecycle remains intact and still depends on active WiFi connection, next stage: explain.

## Constraints
- Keep boot stable.
- Preserve serial shell behavior.
- Validate with platformio run after code edits.

## Decisions Made
- Investigate existing CardKB read path before changing libraries.
- Prefer protocol-aware decoding fix over broad library swap.
- Added a minimal decode guard: likely arrow scan codes are consumed and no longer normalized to numeric digits.
- Log every received CardKB key in AppBootstrap for monitor-side debugging.
- On wake event, force immediate active-screen redraw on both displays.
- In renderEinkMessage full-refresh branch, explicitly switch to GC LUT.
- Expanded arrow decode guard to include observed CardKB codes 0xB4..0xB7 and 0x94..0x97 variants.
- Implemented global left-arrow back behavior without reusing numeric '0'.
- Added WiFi manager with explicit actions: `1 toggle`, `2 connect flow`, `0 back`.
- Added connected SSID state and render it next to WiFi ON status on settings home.
- Enforced back-navigation preference for Bluetooth settings: removed numeric `0` back and switched to left-arrow back.
- Replaced Bluetooth mock list with real BLE discovery scan in Settings > Bluetooth > Scan.
- Restored WiFi manager two-step UX: entering WiFi now opens manager actions (toggle/connect) and scan starts only after explicit connect action.
- Improved Czech text input in WiFi password with both prefix dead-key compose and postfix compose fallback.
- Implemented e-ink Czech rendering path: new `Paint::DrawStringAtUtf8` decodes Czech UTF-8 chars and draws diacritics over base glyphs (acute/caron/ring).
- WiFi scan now shows hidden SSIDs as `skrytá síť <BSSID>` instead of blank labels.
- Serial monitor logging now prints human-readable CardKB key names plus raw hex codes.
- CardKB arrow mapping now treats `0x96`/`0xB6` as DOWN and `0x97`/`0xB7` as RIGHT.
- File Manager browse now handles left-arrow before generic digit decoding so `0xB4` variants do not open item `4` by accident.
- File Manager now resets to a clean browse session on launch so stale submenu state does not block input after open.
- SD apps manifest loader now checks `SD.exists("/apps/apps.txt")` before `SD.open` to avoid noisy VFS `open()` errors when the file is missing.
- File Manager now forces a redraw after handled key input so actions (e.g., `1` select/menu open) are immediately visible.
- Launcher arrow handling now runs before numeric shortcuts, and CardKB arrow variants are excluded from numeric normalization to prevent arrow->digit collisions.
- Notifications UX changed to two-step flow: first right-arrow from desktop shows a strip, second right-arrow expands to full notifications screen on e-ink; left-arrow collapses/close in reverse.
- Added a first web upload app scaffold with a `WebServer`-based upload handler and status screen.
- Web upload starts only when WiFi STA is connected; upload target prefers SD, then LittleFS.
- Launcher shortcut `2` now opens the web upload app.
- Launcher SD app list is now cached so redraws do not reopen the SD card on every render.
- Added shell commands: `web-upload status|start|stop` and `launcher refresh|rescan`.
- Web upload server now runs as a background service and can be queried or stopped from shell.
- `renderStatusScreen` now uses the SPI mutex for OLED writes to avoid display bus contention.

## Open Questions
- Confirm exact raw codes for all four arrows on the target CardKB firmware (expected 0x92/0x94/0x96/0x98 or 0xB2/0xB4/0xB6/0xB8).
- Verify on hardware whether e-ink still appears stuck after idle wake + first keypress.
- Confirm whether any remaining arrow code outside 0x94..0x98 / 0xB4..0xB8 still leaks as digits.
- Verify WiFi manager UX feels clear on OLED text density (SSID length may overflow on long names).

## Next Actions
- Resume from the current UI state and check the desktop wordmark on-device.
- Keep the current e-ink cadence at every 7th full refresh.
- Validate with `platformio run` after any follow-up edits.
- Keep SD playback MVP on the backlog until requested.
- Clarify the SD app model for music: code remains in firmware, while tracks, playlist metadata, and app config can live on SD.
- Add a minimal launcher entry and app screen for music playback only after the SD data layout is agreed.

## Execution Plan (Music Player App on SD)
1. Strategy: keep the launcher/app routing built-in and move only music content and configuration to SD, because ESP32-S3 cannot execute native app code from SD card.
2. Ideas: add a dedicated music-player screen that reads playlist/config files from a fixed SD folder and shows track/status controls without broad menu rewrites.
3. Build: wire a new music-player app ID into the launcher/router, add SD file loading helpers for playlist/config data, and keep the UI modular so audio hardware work can follow later.
4. Verify: run `platformio run` after the patch set.
5. Review: confirm the launcher still opens existing apps, SD manifest loading stays stable, and the new app does not disturb boot or shell behavior.

## Stage Status (Music Player App on SD)
- Strategy: done, evidence: user wants the music player separated from the OS menu flow and stored on SD as much as the platform allows, next stage: build.
- Ideas: done, evidence: selected SD-backed library model (`/music-player` + optional `playlist.txt`) with built-in renderer/input route to keep boot stable, next stage: build.
- Build: done, evidence: added `music-player` launcher app + `sdapp:music-player` route, SD library scanner, OLED/e-ink screen, and input handling in app flow, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-09) after crash-recovery patch set, next stage: review.
- Review: done, evidence: shell launcher IDs/help updated with `music-player`, back navigation unchanged via left-arrow router path, next stage: optimize.
- Optimize: done, evidence: kept implementation localized to existing modules without introducing new boot-time hardware init paths, next stage: explain.

## Continuation Notes
- Branding: device/project name is now `NoteWave`.
- Desktop: centered ASCII wordmark currently uses `noteWAVE` and must stay fully visible on both OLED and e-ink.
- Notifications: desktop right-arrow opens a right-side strip first, second right-arrow opens full notifications, left-arrow collapses/closes.
- File Manager: app opens cleanly, redraws after handled input, and left-arrow/back must not fall through to numeric shortcuts.
- Input: CardKB arrow scan codes are guarded before digit normalization; launcher arrows must never open apps as numbers.
- E-ink: refresh cadence helper is centralized; every 7th refresh is full GC to reduce ghosting.
- Serial debug: every CardKB press is logged with human-readable key name plus raw hex, and e-ink cadence logs DU/FULL updates.
- Shell: existing web-upload and launcher refresh commands remain available for quick checks.
- Validation: always run `platformio run` after edits; use on-device serial monitor if behavior is unclear.

## Execution Plan (File Manager)
1. Strategy: add one built-in `file-manager` app that owns browse, action menu, and paste/create-folder flows for SD card content.
2. Ideas: keep the module separate from `DisplayManager` and reuse the existing left-arrow back convention plus Enter confirmation.
3. Build: wire launcher, router, and shell app-id lists to the new app; implement SD list/copy/move/delete/create helpers in the new module.
4. Verify: run `platformio run` after the patch set and fix any compile issues before moving on.
5. Review: check that left-arrow cancel/back, page scrolling, and folder/file menus behave consistently.

## Execution Plan (Settings/WebUpload Modularity)
1. Strategy: assess whether Settings and Web Upload responsibilities are over-coupled in `DisplayManager`.
2. Ideas: choose a low-risk split that preserves current boot sequence and shell behavior.
3. Build: prepare incremental module boundaries (render vs logic vs app lifecycle) without broad rewrites.
4. Verify: require `platformio run` after each extraction step.
5. Review: check for regressions in CardKB navigation and web-upload lifecycle.

## Execution Plan (CZ Localization)
1. Strategy: define minimum safe scope for Czech input/rendering without breaking menu navigation.
2. Build: add Czech compose input path for text-entry mode and enable UTF-8 OLED rendering.
3. Verify: build with `platformio run` and validate no regressions in settings navigation.
4. Review: list residual limitations (e-ink font fallback, full keyboard layout mapping).

## Stage Status
- Strategy: done, evidence: selected a built-in `file-manager` app with browse, item menu, directory menu, create-folder, and clipboard paste flow, next stage: review residual behavior.
- Build: done, evidence: wired launcher/router/shell IDs and added SD file operations plus CardKB-driven file manager UI, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after the File Manager patch, next stage: optimize/spot-check on device if needed.
- Review: done, evidence: isolated a CardKB left-arrow decode collision in file-manager browse and routed back navigation before digit handling, next stage: on-device smoke test after wake.
- Build: done, evidence: added a clean file-manager launch reset and kept back-routing safe, next stage: verify on-device interaction after open.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-08) after the file-manager launch-reset patch, next stage: on-device smoke test.
- Build: done, evidence: patched `StorageManager::loadSdAppManifest` with an existence check before open, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-08) after manifest-check patch, next stage: optional on-device log sanity check.
- Build: done, evidence: patched file-manager input path to re-render screen on handled keys, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-08) after file-manager redraw patch, next stage: on-device keypress smoke test.
- Build: done, evidence: patched launcher input path and key normalization to avoid arrow keys opening numbered apps, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-08) after launcher arrow/digit fix, next stage: on-device launcher navigation test.
- Build: done, evidence: added notifications drawer state plus desktop strip rendering and two-stage router transitions, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-08) after notifications strip/fullscreen patch, next stage: on-device UX check.
- Strategy: done, evidence: analyzed Settings/WiFi state machine and rendering in DisplayManager, next stage: implement WiFi manager split.
- Ideas: done, evidence: selected low-risk approach with new WiFi manager actions and separate SSID select view, next stage: patch code.
- Build: done, evidence: patched SystemState + DisplayManager for toggle/connect/status/SSID behavior, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after WiFi UX update, next stage: on-device interaction test.
- Build: done, evidence: added Bluetooth manager UI/actions/status in DisplayManager + SystemState fields, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after Bluetooth extension, next stage: hardware behavior validation.
- Strategy: done, evidence: scoped CZ localization to safe path (compose input + OLED UTF-8 + e-ink fallback), next stage: patch code.
- Build: done, evidence: added Czech compose keyboard path and display fallbacks in DisplayManager, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after localization patch, next stage: on-device keyboard and rendering test.
- Build: done, evidence: patched `AppBootstrap.cpp` to print readable CardKB key names, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after keypress logging patch, next stage: on-device serial monitor test.
- Build: done, evidence: corrected DOWN/RIGHT mapping in AppBootstrap + DisplayManager, next stage: verify on device if needed.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after arrow mapping fix, next stage: on-device keypress test.
- Strategy: done, evidence: scoped new web upload app to a small launcher-integrated scaffold using existing WiFi/storage layers, next stage: implement code.
- Build: done, evidence: added WebServer-based upload module, status screen helper, and launcher/shell wiring, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after web upload app integration, next stage: on-device upload test.
- Strategy: done, evidence: identified SD manifest reload in launcher as the main hotspot, next stage: cache the manifest.
- Build: done, evidence: cached launcher SD app list and invalidated it on SD toggle, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after launcher cache fix, next stage: on-device responsiveness check.
- Strategy: done, evidence: reviewed remaining hotspots with specialist agents and picked low-risk operational fixes, next stage: implement lifecycle and command polish.
- Build: done, evidence: added web-upload shell commands, background server servicing, launcher refresh, and mutex-safe status rendering, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-06) after the final command/lifecycle polish, next stage: on-device spot check.
- Strategy: done, evidence: validated current code paths in `DisplayManager` + `WebUploadManager`, next stage: modular split recommendation.
- Ideas: done, evidence: selected incremental extraction plan (settings controller + launcher router + web-upload app boundary), next stage: propose staged implementation.
- Build: done, evidence: no code patch yet; produced safe module extraction sequence for next change set, next stage: optional implementation on request.
- Verify: done, evidence: N/A for advisory-only step (no source edits in runtime modules), next stage: review risks and acceptance criteria.
- Review: done, evidence: identified coupling hotspots (`handleActiveAppInput`, settings scans/connect, launcher app switching), next stage: explain recommended split.
- Build: done, evidence: extracted Settings input state-machine into `SettingsInputManager` and delegated from `DisplayManager`, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after modularization, next stage: quick regression review.
- Review: done, evidence: behavior-preserving delegation retained existing WiFi/Bluetooth/password flow and launcher routing, next stage: optional extraction of settings operations to service layer.
- Build: done, evidence: extracted WiFi/Bluetooth settings operations into `SettingsConnectivityService` and replaced DisplayManager implementations with wrappers, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after connectivity extraction, next stage: on-device smoke test if requested.
- Review: done, evidence: preserved settings navigation and screen update call sites via unchanged wrapper signatures, next stage: optional router extraction.
- Build: done, evidence: extracted active-app routing and global left-arrow back flow into `AppRouter`, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after router extraction, next stage: optional hardware smoke test.
- Review: done, evidence: preserved launcher/settings/web-upload transitions by delegating through context callbacks, next stage: optional shell router unification.
- Build: done, evidence: added desktop/notifications placeholder screens and arrow navigation (`down -> launcher`, `right -> notifications`, `left -> close notifications`), next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after UI navigation feature, next stage: on-device key behavior check.
- Review: done, evidence: default startup app now routes to desktop while preserving existing launcher/settings/web-upload flows, next stage: optional visual polish.
- Build: done, evidence: launcher redesigned to page 6 apps per screen with icon placeholders and `n.name` labels; down arrow now pages through launcher app pages, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after launcher paging UI update, next stage: on-device UX validation.
- Review: done, evidence: launcher supports built-in + SD manifest entries on paged screens while keeping legacy app opening behavior through app IDs, next stage: optional app icon assets.
- Build: done, evidence: replaced launcher icon placeholders with per-app icons, added title truncation with ellipsis, and added up-arrow previous-page logic with first-page return to desktop, next stage: compile verification.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-07) after launcher icon/truncation/navigation update, next stage: on-device interaction test.
- Review: done, evidence: launcher now supports `up/down` page navigation and desktop return policy from first launcher page while preserving `1..6` open behavior, next stage: optional icon polish.

## Last Update
- Date: 2026-04-09
- Updated by agent: Team Orchestrator

## Execution Plan (SD Settings Submenu)
1. Strategy: turn the two SD-related settings into a single SD manager entry from settings home.
2. Ideas: keep WiFi/Bluetooth patterns intact, add one SD view with toggle + speed actions, and use left-arrow back instead of numeric `0`.
3. Build: update settings labels, view constants, routing, and input handling in the existing settings modules.
4. Verify: run `platformio run` after the patch set.
5. Review: confirm settings home still fits on OLED/e-ink and that `0` no longer acts as a settings back key.

## Execution Plan (E-ink Block ASCII Support)
1. Strategy: keep OLED path unchanged and patch only e-ink desktop art path.
2. Ideas: use smallest e-ink font (`Font8`) and add renderer support for UTF-8 block symbols used by the existing ASCII art.
3. Build: extend `Paint::DrawStringAtUtf8` for block symbols (`▀`,`▄`,`█`,`▌`,`▐`) and switch e-ink desktop art draw to `DrawStringAtUtf8` with width-safe clipping.
4. Verify: run `platformio run`.
5. Review: confirm no regressions in desktop notifications strip layout.

## Execution Plan (WiFi Forget Network)
1. Strategy: add a phone-like "forget network" action in WiFi manager without changing existing connect flow.
2. Ideas: bind action to WiFi manager option `3`, clear persisted SSID/password, and disconnect only if the forgotten network is currently active.
3. Build: extend `SettingsConnectivityService`, `SettingsInputManager`, and `DisplayManager` (OLED/e-ink labels + callback wiring).
4. Verify: run `platformio run` after patch set.
5. Review: ensure saved-password auto-fill still works only when current SSID matches the remaining stored credentials.

## Stage Status (WiFi Forget Network)
- Strategy: done, evidence: user requested explicit "forget network" behavior similar to phone UX, next stage: ideas.
- Ideas: done, evidence: selected low-risk manager action (`3`) with config erase + conditional disconnect for active forgotten SSID, next stage: build.

## Execution Plan (Keyboard Regression Triage)
1. Strategy: identify whether recent input-path changes could break CardKB handling without changing firmware behavior yet.
2. Ideas: correlate recent commits touching key decode/routing with known arrow-code variants and app-level input gates.
3. Verify: provide a ranked list of likely regressions and exact files/functions to inspect first.
4. Review: report immediate low-risk checks the user can run in serial monitor before code edits.

## Stage Status (Keyboard Regression Triage)
- Strategy: done, evidence: user reports keyboard non-functional after recent work, next stage: ideas.
- Build: done, evidence: implemented `settingsServiceForgetWifi` and wired it through settings input context plus OLED/e-ink WiFi manager hints, next stage: verify.

## Execution Plan (Notes App Completion)
1. Strategy: define minimum complete Notes MVP in current firmware architecture (list, view/edit, save to storage, safe navigation).
2. Ideas: choose low-risk storage path and input UX compatible with CardKB and existing left-arrow back convention.
3. Build: wire notes app into launcher/router, add state + rendering + input handling, and persist notes data via existing storage facilities.
4. Verify: run `platformio run` and fix any compile/runtime-facing issues.
5. Review: check regressions in launcher navigation, serial shell behavior, and shared display/SPI safety.
6. Optimize: identify low-risk performance and UX improvements for notes list loading and redraw cadence.
7. Explain: summarize what changed, known limitations, and next validation steps.

## Stage Status (Notes App Completion)
- Strategy: in-progress, evidence: user requested to finish the notes app end-to-end in current firmware workspace, next stage: ideas.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-10) after forget-network patch set, next stage: review.
- Review: done, evidence: connect/scan/toggle paths are unchanged and saved-password prefill remains tied to exact SSID match, next stage: explain.

## Execution Plan (Bluetooth Completion)
1. Strategy: keep current BLE scan flow and add practical persistence/cleanup features without changing boot-time init ordering.
2. Ideas: persist selected Bluetooth device label in config, add explicit "forget saved" action in Bluetooth manager, and keep disconnect behavior stable.
3. Build: extend `AppConfig` + `ConfigStore`, add new operations in `SettingsConnectivityService`, and wire actions through `SettingsInputManager`/`DisplayManager`.
4. Verify: run `platformio run` after patch set.
5. Review: confirm Bluetooth scan/select/disconnect still works and saved device data can be erased from settings.

## Execution Plan (Notes Two Modes: Read + Browse)
1. Strategy: preserve existing Notes edit/save flow and add two explicit display modes without hijacking normal text typing.
2. Ideas: keep current tag filtering, add wiki-link metadata extraction, and render a style-aware preview in browse mode while read mode keeps raw text behavior.
3. Build: extend Notes state with mode flag, add mode toggle on control keys (`Tab`/`Esc`), and update OLED/e-ink Notes render sections.
4. Verify: run `platformio run` after edits.
5. Review: check regressions in save (`->`), back (`<-`), tag cycling (`up/down`), and shell/launcher launch behavior.

## Stage Status (Notes Two Modes: Read + Browse)
- Strategy: done, evidence: user requested two Notes modes (read + styled browse) with Obsidian-like tags/links behavior, next stage: ideas.
- Ideas: done, evidence: selected low-risk UI-only mode split with metadata extraction and control-key toggling to keep text entry intact, next stage: build.
- Build: done, evidence: patched `SystemState` + `DisplayManager` with notes mode state, wiki-link extraction, mode toggling (`Tab`/`Esc`), and browse-style preview rendering, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after Notes two-mode patch set, next stage: review.
- Review: done, evidence: right-arrow save, left-arrow global back, and up/down tag cycling paths remain active with mode switch isolated to control keys, next stage: optimize.
- Optimize: done, evidence: implementation stays in existing Notes helper/render path with no new module coupling or boot-time init changes, next stage: explain.
- Explain: done, evidence: Notes now supports explicit read vs style-aware browse viewing while preserving current editing workflow, next stage: on-device UX validation.

## Stage Status (Notes Read/Write Semantics)
- Strategy: done, evidence: user clarified that Browse naming/behavior is confusing and asked for Read + Write modes instead, next stage: build.
- Build: done, evidence: switched Notes mode semantics to `read`/`write`, defaulted app launch to write mode, and blocked text edits in read-only mode while keeping save/back/tag controls, next stage: verify.

## Stage Status (Notes Markdown Headings H1-H6)
- Strategy: done, evidence: user requested support for markdown heading syntax `#` through `######` in Notes, next stage: build.
- Build: done, evidence: extended notes styled-line parser to map `#..######` to heading levels `H1..H6` with longest-prefix matching, next stage: verify.

## Execution Plan (Notes Obsidian Interoperability)
1. Strategy: align Notes file format and parser behavior with Obsidian Flavored Markdown so files can be moved ESP <-> PC without manual conversion.
2. Ideas: switch default note path to `.md`, keep legacy `.txt` fallback for backward compatibility, and extend read-mode parser for core OFM structures.
3. Build: patch `SystemState` + `DisplayManager` load/save + parser helpers (line endings/BOM normalization, links, embeds, tasks, headings, callouts, ordered lists, code fences).
4. Verify: run `platformio run`.
5. Review: ensure read/write controls and tag filtering remain stable while improving markdown readability.

## Stage Status (Notes Obsidian Interoperability)
- Strategy: done, evidence: user requested cross-compatibility so ESP notes can open in Obsidian and Obsidian notes can open on ESP, next stage: ideas.
- Ideas: done, evidence: selected OFM-focused parser upgrade with `.md` default path and safe legacy fallback to `/notes/quicknote.txt`, next stage: build.
- Build: done, evidence: added `.md` default, markdown text normalization, ordered/checklist/callout/embed/link parsing, and fenced-code handling in Notes read-mode preview, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after interoperability patch set, next stage: review.
- Review: done, evidence: Notes read-only/write mode switching and save/back/tag controls remained intact while parser coverage expanded, next stage: explain.

## Stage Status (README Sync)
- Strategy: done, evidence: user requested that all recently added/changed behavior be documented in README, next stage: build.
- Build: done, evidence: updated `README.md` with current shell commands, implemented app capabilities, Notes read/write + Obsidian compatibility, and known Bluetooth/A2DP limitations, next stage: review.
- Review: done, evidence: README now reflects actual runtime behavior and near-term roadmap without changing firmware code paths, next stage: explain.

## Stage Status (Notes Tags Functional + Visual Fix)
- Strategy: done, evidence: user reported tags still seem to do nothing, next stage: build.
- Build: done, evidence: expanded tag parser to accept UTF-8/Czech bytes and slash (`/`) and added stronger active-tag rendering (dedicated active tag line + larger e-ink font), next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after tag parser/render patch, next stage: review.
- Review: done, evidence: existing controls (`up/down` tag cycling, `->` save, `<-` back, read/write mode switch) remain unchanged while tag visibility and recognition improved, next stage: explain.

## Stage Status (Notes Newline + Heading Readability)
- Strategy: done, evidence: user clarified priority is Obsidian-like headings and real multiline writing over tag mechanics, next stage: build.
- Build: done, evidence: added robust enter detection (`normalized` + high-bit stripped raw variants), replaced single-line preview flattening with two-line last-lines rendering, and updated on-screen notes hint to `enter=new line`, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after multiline/preview patch, next stage: review.
- Review: done, evidence: read/write switching and save/back behavior remain intact while line breaks are now visible in Notes preview and input path, next stage: explain.

## Stage Status (Notes Read Mode Heading Render Visibility)
- Strategy: done, evidence: user reported read mode still appears to only change label and not heading rendering, next stage: build.
- Build: done, evidence: read mode now explicitly finds the last heading in the entire note and renders a visible heading badge (`H1..H6`) with stronger font hierarchy on OLED/e-ink, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after read-mode heading visibility patch, next stage: review.
- Review: done, evidence: write/read toggle and multiline entry behavior remain unchanged while read mode now always surfaces heading detection from note content, next stage: explain.

## Stage Status (Notes Larger Text Field + Scroll Input)
- Strategy: done, evidence: user requested larger text area, up/down scrolling instead of active-tag behavior, and live e-ink writing visibility, next stage: build.
- Build: done, evidence: replaced tag-focused Notes layout with multi-line text viewport on OLED/e-ink, mapped up/down to line scrolling, kept enter as newline and right-arrow as save, and made e-ink render current writing viewport directly, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after viewport/scroll patch set, next stage: review.
- Review: done, evidence: read/write switching and back/save controls remain intact while text visibility and navigation are now content-first instead of tag-first, next stage: explain.

## Stage Status (Notes UI Visual Polish)
- Strategy: done, evidence: user requested making the Notes screen visually cleaner after functional fixes, next stage: build.
- Build: done, evidence: refined OLED/e-ink Notes layouts with framed content area, compact top bar, truncated status line, and visible scrollbars while keeping behavior unchanged, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after visual-polish patch, next stage: review.
- Review: done, evidence: input flow (up/down scroll, enter newline, right save, left back, tab/esc mode switch) remains stable with improved readability and orientation, next stage: explain.

## Stage Status (Notes E-ink Stability Optimization)
- Strategy: done, evidence: user reported multiple e-ink Notes issues and requested focused optimization, next stage: build.
- Build: done, evidence: hardened e-ink Notes line renderer with overflow guards, font-aware clipping widths, and write-mode cursor marker in the e-ink viewport, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after e-ink stabilization patch, next stage: review.
- Review: done, evidence: e-ink line drawing no longer risks overlapping footer/text frame with mixed heading fonts; write-mode current-line visibility improved, next stage: explain.

## Stage Status (Notes E-ink Refresh Optimization)
- Strategy: done, evidence: user requested additional optimization steps for e-ink notes performance, next stage: build.
- Build: done, evidence: implemented viewport-signature dirty guard to skip redundant e-ink redraws and added batched scroll refresh (threshold + timeout) to reduce refresh load during rapid up/down scrolling, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after e-ink refresh optimization patch, next stage: review.
- Review: done, evidence: scroll interaction remains responsive while unnecessary full Notes e-ink redraws are avoided when content/viewport is unchanged, next stage: explain.

## Stage Status (README Syntax Reference)
- Strategy: done, evidence: user requested README to list all implemented note-markup tags/signs, next stage: build.
- Build: done, evidence: updated `README.md` controls section to current up/down scroll behavior and added explicit markdown/Obsidian syntax cheat sheet with examples (headings, lists, tasks, callouts, hr, code fences, links, embeds, tags), next stage: review.
- Review: done, evidence: README now documents practical syntax forms users can type directly and expect to preview in Notes, next stage: explain.

## Stage Status (Bluetooth Completion)
- Strategy: done, evidence: user requested finishing Bluetooth after WiFi completion, next stage: ideas.
- Ideas: done, evidence: selected low-risk persistence + forget flow (no new background pairing logic), next stage: build.
- Build: done, evidence: added `bt_preferred_device` config persistence and BT manager actions for select/save + forget saved device, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-10) after Bluetooth completion patch set, next stage: review.
- Review: done, evidence: BT toggle/scan/disconnect path remains intact while saved-device lifecycle is now explicit, next stage: explain.

## Stage Status (Bluetooth Connect Fix)
- Strategy: done, evidence: user reported that Bluetooth still cannot connect in real usage, next stage: build.
- Build: done, evidence: replaced state-only BT select with real BLE client connect by scanned MAC address and added address persistence (`bt_preferred_address`), next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-10) after BLE connect path patch, next stage: on-device smoke test.

## Execution Plan (AirPods A2DP Feasibility on ESP32-S3)
1. Strategy: validate whether the requested `ESP32-A2DP` + Classic Bluetooth config path is technically possible on current board.
2. Ideas: compare project Bluetooth flow (BLE client path) with hardware capabilities of ESP32-S3 and A2DP requirements.
3. Verify: confirm current module behavior and user-facing diagnostics in `AudioManager` and settings Bluetooth connect flow.
4. Review: document feasible alternatives that preserve boot stability and shell behavior.

## Stage Status (AirPods A2DP Feasibility on ESP32-S3)
- Strategy: done, evidence: user asked to use `pschatzmann/ESP32-A2DP` with Classic BT/A2DP options for AirPods, next stage: ideas.
- Ideas: done, evidence: A2DP requires Bluetooth Classic (BR/EDR), but ESP32-S3 controller supports BLE only; menuconfig Classic/A2DP enable path is therefore not available for S3 targets, next stage: verify.
- Verify: done, evidence: `src/AudioManager.cpp` already reports unsupported A2DP on ESP32-S3 and `src/SettingsConnectivityService.cpp` rejects headset-class usage in BLE mode, next stage: review.
- Review: done, evidence: project should keep BLE flow on S3 and use classic-capable ESP32 silicon for AirPods/A2DP scenarios, next stage: explain.

## Execution Plan (Dual-Chip Audio Follow-Up)
1. Strategy: keep ESP32-S3 as UI/system controller and delegate AirPods A2DP to a second classic ESP32.
2. Ideas: select UART command/status link as MVP transport so integration stays simple and deterministic.
3. Build: after hardware arrives, add S3-side transport module and classic-ESP audio firmware command handlers.
4. Verify: run `platformio run` for S3 firmware and perform on-device command loop (`CONNECT/PLAY/PAUSE/STOP/STATUS`).
5. Review: confirm stable playback during e-ink/book usage and verify no regressions in shell/boot behavior.

## Stage Status (Dual-Chip Audio Follow-Up)
- Strategy: done, evidence: user wants continuous music while device is used for reading/UI tasks, next stage: ideas.
- Ideas: done, evidence: chose ESP32-DevKitC 38pin (classic ESP32) as practical audio coprocessor prototype target and UART as first transport, next stage: document and wait for hardware.
- Explain: done, evidence: saved concrete architecture, purchase guidance, and wiring/protocol notes into `NOTES.md`, next stage: resume implementation once hardware is available.

## Execution Plan (A2DP Transport Scaffold)
1. Strategy: add a dedicated A2DP control surface now, then wire SD music app playback later.
2. Ideas: integrate source start/stop/status via shell and persist preferred target name in config.
3. Build: create `AudioManager` module and add shell command routing.
4. Verify: run `platformio run`.
5. Review: check hardware compatibility with ESP32-S3 Bluetooth capabilities.

## Stage Status (A2DP Transport Scaffold)
- Strategy: done, evidence: user requested A2DP now and SD-backed music app later, next stage: build.
- Build: done, evidence: added `AudioManager` module and shell commands (`a2dp status|start|stop`) with persistent `a2dp_target_name`, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-10) after final fallback patch set, next stage: review.
- Review: done, evidence: ESP32-S3 is BLE-only (no Bluetooth Classic), so true A2DP source is unsupported on this hardware; module now reports explicit unsupported status without destabilizing boot/shell, next stage: explain.

## Execution Plan (Desktop Art Anchor Under Notifications)
1. Strategy: keep current notifications strip behavior but stop desktop art re-centering when strip opens.
2. Build: use fixed full-width content centering area on OLED and e-ink desktop paths.
3. Verify: run `platformio run` and confirm no compile regressions.
4. Review: visually confirm strip overlays right side while art baseline position remains unchanged.

## Stage Status (E-ink Block ASCII Support)
- Strategy: done, evidence: identified unsupported block glyphs in e-ink desktop path while OLED path already renders independently, next stage: ideas.
- Ideas: done, evidence: selected low-risk renderer extension + `Font8` migration only for e-ink desktop art, next stage: build.
- Build: done, evidence: patched `epdpaint.cpp` UTF-8 block glyph rendering and updated `DisplayManager.cpp` e-ink desktop art to `Font8` + UTF-8 clipping, next stage: verify.
- Verify: done, evidence: `platformio run` compiled successfully (2026-04-08) after the block-glyph/Font8 patch, next stage: review on-device readability.

## Execution Plan (AudioManager Safe Refactor)
1. Strategy: make a behavior-preserving cleanup in `AudioManager` to reduce duplicate unsupported-A2DP state writes.
2. Ideas: extract one local helper for the common ESP32-S3 unsupported status assignments and reuse in start/stop paths.
3. Build: patch only `src/AudioManager.cpp` with no public API/signature changes.
4. Verify: run `platformio run`.
5. Review: confirm shell messages and persisted target behavior remain unchanged.

## Stage Status (AudioManager Safe Refactor)
- Strategy: done, evidence: user requested refactor-first workflow before new app design, next stage: ideas.
- Ideas: done, evidence: selected a local helper extraction to unify unsupported-A2DP state writes without API changes, next stage: build.
- Build: done, evidence: patched only `src/AudioManager.cpp` with `applyUnsupportedA2dpState(...)` + shared status constant, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after refactor patch, next stage: review.
- Review: done, evidence: shell text outputs and public AudioManager signatures remain unchanged; target persistence behavior preserved, next stage: optimize.
- Optimize: done, evidence: reduced duplicate state assignment paths for easier future maintenance with no runtime feature changes, next stage: explain.
- Explain: done, evidence: refactor complete and ready to continue with new app planning when requested, next stage: user choice.

## Execution Plan (Runtime Log Performance Tuning)
1. Strategy: reduce serial monitor overhead by disabling high-frequency debug logs while preserving shell and error diagnostics.
2. Ideas: add local compile-time flags in app loop/task/render paths and default them to low-noise mode.
3. Build: patch logging callsites only in `AppBootstrap`, `AppTasks`, and e-ink cadence logging path.
4. Verify: run `platformio run`.
5. Review: confirm keyboard handling, rendering cadence, and shell command flow remain unchanged.

## Stage Status (Runtime Log Performance Tuning)
- Strategy: done, evidence: user reported sluggish/jerky serial monitor and asked whether code is optimized for speed, next stage: ideas.
- Ideas: done, evidence: selected low-risk approach to disable high-frequency non-critical logs via compile-time flags, next stage: build.
- Build: done, evidence: gated loop heartbeat, CardKB live key logs, task tick logs, and e-ink cadence logs in `AppBootstrap`, `AppTasks`, and `DisplayManager`, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after log-throttling patch set, next stage: review.
- Review: done, evidence: shell behavior and runtime logic unchanged; only verbose diagnostics frequency reduced, next stage: explain.
- Explain: done, evidence: firmware now defaults to low-noise serial output for better responsiveness, next stage: optional deeper CPU/latency optimization pass.

## Execution Plan (Music Player Skeleton Mode)
1. Strategy: keep launcher/router integration for `music-player` but freeze it as a non-functional skeleton until second ESP is available.
2. Ideas: replace active music-player input/render behavior with a lightweight placeholder-style screen and minimal status updates.
3. Build: patch only `DisplayManager.cpp` to avoid broad module changes.
4. Verify: run `platformio run`.
5. Review: confirm back-navigation and shell/launcher app IDs remain stable.

## Stage Status (Music Player Skeleton Mode)
- Strategy: done, evidence: user requested to postpone real music app work until second ESP arrives and keep only a skeleton now, next stage: ideas.
- Ideas: done, evidence: selected low-risk freeze by keeping launcher/router IDs but replacing app behavior with status-only skeleton screen, next stage: build.
- Build: done, evidence: patched `DisplayManager.cpp` launcher description + music app input/render to skeleton-only mode with no active playback/library logic, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after skeleton-mode patch, next stage: review.
- Review: done, evidence: global left-arrow back routing and shell `launcher open music-player` path remain unchanged, next stage: explain.
- Explain: done, evidence: music-player now serves as integrated placeholder until second ESP implementation starts, next stage: choose next app task.

## Execution Plan (Built-in Notes App)
1. Strategy: add a lightweight Obsidian-like notes MVP directly in firmware with CardKB text input and LittleFS persistence.
2. Ideas: integrate a new launcher app ID (`notes`) and keep controls minimal: type/edit text, right-arrow save, left-arrow back.
3. Build: patch router/shell IDs plus `DisplayManager` notes load/save/input/render flow.
4. Verify: run `platformio run`.
5. Review: confirm no regressions in launcher navigation and shell app-open behavior.

## Stage Status (Built-in Notes App)
- Strategy: done, evidence: user requested next built-in app for note-taking (Obsidian-like), next stage: ideas.
- Ideas: done, evidence: selected safe MVP with one quick-note file in LittleFS and simple controls (type/edit, right-arrow save, left-arrow back), next stage: build.
- Build: done, evidence: added `notes` app integration across `SystemState`, `AppRouter`, `ShellCommands`, and `DisplayManager` including load/save/input/render path, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after notes app patch set, next stage: review.
- Review: done, evidence: launcher open/list and back navigation conventions remain stable while notes persistence is contained to LittleFS, next stage: explain.
- Explain: done, evidence: notes MVP is ready as a built-in firmware app and can be iterated toward richer Obsidian-like behavior later, next stage: optional UX enhancements.

## Execution Plan (Notes Word-Commit E-ink Flow)
1. Strategy: keep per-key typing feedback on OLED while reducing e-ink updates to committed chunks.
2. Ideas: introduce a draft-word buffer and refresh e-ink only on space/newline/save events.
3. Build: patch notes input/render flow in `DisplayManager.cpp` only.
4. Verify: run `platformio run`.
5. Review: ensure left-arrow back and right-arrow save behavior remain unchanged.

## Stage Status (Notes Word-Commit E-ink Flow)
- Strategy: done, evidence: user requested avoiding e-ink updates per letter and committing words on space, next stage: ideas.
- Ideas: done, evidence: selected draft-word approach with OLED-only typing redraw and conditional e-ink refresh, next stage: build.
- Build: done, evidence: added `gNotesDraftWord`, word commit helper, and `oledOnly` render switching for notes input handling, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after notes word-commit patch, next stage: review.
- Review: done, evidence: notes save/back controls preserved while reducing e-ink churn during typing, next stage: explain.

## Execution Plan (Notes Tags MVP)
1. Strategy: provide Obsidian-like `#tag` usage without heavy editor complexity.
2. Ideas: parse tags from committed note buffer, track unique tags, and surface them in OLED/e-ink notes UI.
3. Build: patch `DisplayManager.cpp` notes helpers/input/render only.
4. Verify: run `platformio run`.
5. Review: ensure word-commit e-ink behavior remains intact while tags update correctly after edits.

## Stage Status (Notes Tags MVP)
- Strategy: done, evidence: user requested tag workflow similar to Obsidian, next stage: ideas.
- Ideas: done, evidence: selected lightweight unique-tag extractor (`#tag`) with draft-tag preview on OLED, next stage: build.
- Build: done, evidence: added notes tag parser/summary and integrated tag status into notes input/render paths, next stage: verify.
- Verify: done, evidence: `platformio run` SUCCESS (2026-04-16) after notes tags patch, next stage: review.
- Review: done, evidence: notes still redraw e-ink only on commit/save events while tags are refreshed on committed-buffer changes, next stage: explain.

## Stage Status (Notes Arrow Input Guard)
- Strategy: done, evidence: user reported arrow keys in notes input are again appearing as numeric characters, next stage: build.
- Build: done, evidence: added raw arrow guards in `handleNotesAppInput` so left is passed to global back handler, right remains save, and up/down are ignored for text entry, next stage: verify.

## Stage Status (Notes Tags Visual Integration)
- Strategy: done, evidence: user requested tags to render differently from note text, next stage: build.
- Build: done, evidence: replaced notes e-ink generic status rendering with a dedicated layout containing separate TAGS and TEXT sections, next stage: verify.

## Stage Status (Notes Tags Semantic Behavior)
- Strategy: done, evidence: user requested tags to have functional meaning, not only visual distinction, next stage: build.
- Build: done, evidence: added active tag filter state and up/down tag cycling so Notes preview can show only lines matching selected `#tag`, next stage: verify.

## Stage Status (Notes E-ink Text Visibility Fix)
- Strategy: done, evidence: user reported e-ink no longer shows note words after tags update, next stage: build.
- Build: done, evidence: adjusted `renderNotesScreen` e-ink lines to render committed note text preview (`notesTailPreview`) instead of tag-summary-only line, next stage: verify.
- Optimize: done, evidence: added compact UTF-8 draw path with reduced glyph advance (`4px`) and tighter line spacing (`10px`) for e-ink desktop art only, next stage: on-device visual check.

## Stage Status (Desktop Art Anchor Under Notifications)
- Strategy: done, evidence: user requested that opening notification strip must not move text beneath it, next stage: build.
- Build: done, evidence: patched desktop render centering width to fixed full width for OLED and e-ink branches, next stage: verify.

## Stage Status (SD Settings Submenu)
- Strategy: done, evidence: identified a single SD manager entry as the cleanest way to group the two SD settings.
- Build: done, evidence: added SD manager view with toggle/speed actions, remapped settings home to 1..4, and removed numeric `0` as settings back.
- Verify: done, evidence: `platformio run` SUCCESS after the final signature fix.
- Review: done, evidence: left-arrow back now covers settings submenus including SD, while `0` is left as a normal key in password entry.

## Stage Status (Bluetooth Manager Completion)
- Strategy: done, evidence: identified explicit disconnect as the remaining Bluetooth UX gap.
- Build: done, evidence: added `disconnectBluetooth` helper, Bluetooth manager action 3, and status text updates in OLED/e-ink settings views.
- Verify: done, evidence: `platformio run` SUCCESS after the Bluetooth patch.
- Review: done, evidence: Bluetooth now has toggle, scan, select, connect, and explicit disconnect paths with left-arrow back preserved.
