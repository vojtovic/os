# Project State

This file is the short-term memory for the `os` project.

## Block 0: Goal
- Build a small, structured OS-like project for ESP32-S3.
- Keep the codebase readable and modular from the start.
- Use LittleFS for internal config and small state files.
- Use SD card for larger data, media, exports, and user files.
- Keep OLED as the main interactive UI.
- Keep e-ink as a slower snapshot or mirror display.
- Add audio later as a separate subsystem.

## Block 1: Known Hardware From mp3_test
This is the hardware currently known and intended for reuse in the new project.

### I2C devices
- CardKB on `0x5F`
- DS3231 RTC

### Displays and output
- OLED: U8g2 SH1106 128x64 over software SPI in the old project
- E-ink: Waveshare 3.52" class panel with custom driver
- Buzzer: GPIO4

### SD card
- separate SPI bus, not shared with the other devices

### Known pin layout from mp3_test
- `SDA_PIN = 8`
- `SCL_PIN = 9`
- `SD_MOSI = 38`
- `SD_SCK = 39`
- `SD_MISO = 40`
- `SD_CS = 46`

## Block 2: Current os Project Direction
- PlatformIO project is already created in `os/`.
- Board preset targets an ESP32-S3 WROOM-style board.
- LittleFS is enabled as the project filesystem.
- A serial command shell exists for debugging and testing.
- A first TaskManager exists and pins tasks to cores.

## Block 3: System Architecture
### Dual-core strategy
- Core 0:
  - system services
  - storage
  - SD
  - config
  - audio later
- Core 1:
  - OLED UI
  - input handling
  - e-ink mirror or snapshot rendering
  - periodic debug and UI updates

### Layering rule
- Hardware init belongs to dedicated modules.
- UI must read system state, not hardware directly.
- E-ink is a mirror of state, not the main interaction surface.
- Audio stays separate from display code.

## Block 4: TaskManager Direction
The current TaskManager is only the first layer over FreeRTOS.

What it should eventually provide:
- named tasks
- core pinning
- priorities
- periodic tasks
- enable/disable support
- status reporting
- possibly event or message dispatch later

## Block 5: Serial Shell
The shell is intended for quick testing without recompiling.

### Commands
- `help`
- `info`
- `tasks`
- `fs info`
- `fs ls [path]`
- `sd info`
- `sd ls [path]`
- `config show`
- `config reload`
- `config save`
- `config set name <value>`
- `config set sd on|off`
- `config set sd_speed <hz>`
- `reboot`
- `prompt`

### Shell purpose
- quick inspection of state
- SD and LittleFS testing
- config changes without recompiling
- task visibility during debugging

## Block 6: Display Strategy
- OLED is the live UI.
- E-ink is not the main UI.
- E-ink should only mirror the current system state as a snapshot.
- OLED should own menus, selections, and active interaction.
- E-ink should update only on meaningful state changes or after a timeout.

## Block 7: Audio Strategy
Audio is not integrated yet.

Preferred approach:
- use a dedicated audio subsystem
- keep it separate from UI and storage logic
- prefer I2S output with an external DAC or amplifier
- do not mix audio playback logic directly into main UI code

## Block 8: Storage Strategy
### LittleFS
Use for:
- config
- small system files
- state persistence
- flags, metadata, and settings

### SD card
Use for:
- music
- larger assets
- exports
- user files
- logs if needed

## Block 9: Current Implementation Notes
- `os/src/main.cpp` already contains LittleFS config loading and SD init scaffolding.
- `os/src/TaskManager.cpp` uses FreeRTOS task creation and core pinning.
- `os/NOTES.md` contains the architectural notes and command reference.
- `os/PROJECT_STATE.md` is the short-term memory for design decisions.
- The project is still a shell or framework, not a full OS yet.

## Block 10: Next Integration Steps
1. Split the serial shell into its own module.
2. Add a central `SystemState` structure.
3. Add storage helpers for LittleFS and SD.
4. Add an audio subsystem skeleton.
5. Wire OLED UI to `SystemState`.
6. Add e-ink snapshot rendering from the same state.
7. Add real menu navigation and event passing.

## Block 11: What Not To Do Yet
- Do not build a custom scheduler from scratch.
- Do not make the e-ink the main live UI.
- Do not mix audio playback logic into the display code.
- Do not scatter pin definitions across multiple files without a single source of truth.
