# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build firmware
platformio run

# Build and upload to connected device
platformio run --target upload

# Open serial monitor (115200 baud)
platformio device monitor
```

There are no automated tests. After any code change, validate with `platformio run`.

## Architecture Overview

This is an ESP32-S3 Arduino firmware project targeting the `freenove_esp32_s3_wroom` board, structured as a modular OS-like system.

### Boot and Loop Flow

`main.cpp` creates a single `SystemState` and `TaskManager`, then delegates entirely to `AppBootstrap.cpp`:
- `setupApplication()` — initializes hardware, filesystems, displays, FreeRTOS tasks
- `processApplicationLoop()` — polls CardKB input, serial shell, web upload server, and e-ink idle timeout each loop tick

### Central State Object

`include/SystemState.h` defines `SystemState`, the single shared struct passed by reference everywhere. It holds sub-states for every subsystem: `AppConfig`, `LauncherState`, `MusicPlayerState`, `NotesState`, `SettingsState`, `FileManagerState`, `NotificationsState`, `AudioState`, plus hardware-ready flags and FreeRTOS mutex handles.

### Module Responsibilities

| Module | Responsibility |
|---|---|
| `HardwareManager` | I2C, SPI init, DS3231 RTC, CardKB (0x5F), buzzer (GPIO 4) |
| `StorageManager` | LittleFS mount + config load, SD card init on separate SPI bus |
| `ConfigStore` | Reads/writes `AppConfig` to LittleFS (`config.ini`) |
| `DisplayManager` | All OLED and e-ink rendering; contains every app screen render function |
| `AppRouter` | Routes render calls and input handling to the correct app by `state.launcher.activeAppId` |
| `SerialShell` | Reads serial input into a line buffer, calls `executeShellCommand()` |
| `ShellCommands` | Dispatches shell commands to the relevant manager functions |
| `WebUploadManager` | HTTPS file server (jackjansen/esp32_idf5_https_server); lifecycle managed by shell or UI |
| `AudioManager` | A2DP source skeleton (dual-chip architecture, not complete) |
| `TaskManager` | Thin FreeRTOS wrapper: registers named periodic tasks with core pinning |
| `AppTasks` | `systemTask` (core 0) and `uiTask` (core 1) — currently stubs for future periodic work |

### Display Strategy

- **OLED** (SH1106 128×64 via U8g2, hardware SPI pins 11/12): primary interactive UI, all menus and active app screens
- **E-ink** (Waveshare 3.52", same SPI bus as OLED): snapshot/mirror only; updates on state changes or after 60-second idle timeout using alternating partial (DU) and full (GC16) refreshes; never the main interaction surface

Shared SPI bus safety rule: keep inactive CS lines HIGH; initialize e-ink control lines to safe idle before any SPI activity.

### App Routing

`state.launcher.activeAppId` is a string ID (`"desktop"`, `"launcher"`, `"settings"`, `"file-manager"`, `"music-player"`, `"notes"`, `"web-upload"`). `AppRouter` uses this to dispatch render and input calls via function-pointer structs (`AppRouterRenderContext`, `AppRouterInputContext`). Adding a new app requires entries in both structs and a branch in `appRouterRenderActiveApp()`.

### Storage Layout

- **LittleFS**: config (`/config.ini`), notes (`/notes/quicknote.md`), small state files
- **SD card** (HSPI bus: MOSI 38, SCK 39, MISO 40, CS 46): music library (`/music-player/`), SD app manifest (`/apps/apps.txt`), user files

### SD Apps Manifest

`/apps/apps.txt` on SD lists app IDs (one per line). These appear in the launcher as `sdapp:<id>`. The music-player logic stays in firmware; only library data lives on SD.

## Hardware Constraints

- Do not force USB CDC macros unless explicitly requested
- ESP32-S3 is BLE-only — no Bluetooth Classic/A2DP. Current audio direction is dual-chip (ESP32-S3 for UI + classic ESP32 for A2DP)
- SD uses a dedicated `SPIClass(HSPI)` instance separate from the display SPI bus

## Coding Conventions

- All module functions take `SystemState &state` and `Stream &out` by reference; `out` defaults to `Serial`
- Hardware driver globals (e.g., `gOled`, `gEink`) live in anonymous namespaces inside `.cpp` files — not in headers
- Constants use `constexpr` with `k` prefix (`kBuzzerPin`, `kSdCs`, etc.)
- Pin assignments are defined in the `.cpp` file of the owning module; there is no single pin header
- Comments mix Czech and English — both are acceptable
- Keep changes small and module-focused; avoid speculative refactors
