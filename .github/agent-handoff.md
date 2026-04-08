# Agent Handoff Context

Use this file as the shared context between agents in this workspace.

## Current Objective
- Next focus: implement a built-in File Manager app for SD card files and folders, with launcher integration and CardKB-driven action menus.

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
- Implement File Manager browse/action flow.
- Validate file-manager back navigation on-device after wake.
- Validate with `platformio run` after edits.
- Keep SD playback MVP on the backlog until requested.

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
- Date: 2026-04-07
- Updated by agent: Team Orchestrator
