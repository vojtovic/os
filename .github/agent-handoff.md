# Agent Handoff Context

Use this file as the shared context between agents in this workspace.

## Current Objective
- Next focus: validate web-upload and launcher responsiveness on-device, while keeping Czech localization and SD playback MVP on the active backlog.

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
- Validate web-upload start/stop/status and launcher refresh on-device.
- Keep SD playback MVP on the backlog until requested.
- Continue Czech localization polish only if the user asks for it.

## Execution Plan (CZ Localization)
1. Strategy: define minimum safe scope for Czech input/rendering without breaking menu navigation.
2. Build: add Czech compose input path for text-entry mode and enable UTF-8 OLED rendering.
3. Verify: build with `platformio run` and validate no regressions in settings navigation.
4. Review: list residual limitations (e-ink font fallback, full keyboard layout mapping).

## Stage Status
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

## Last Update
- Date: 2026-04-06
- Updated by agent: Team Orchestrator
