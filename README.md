# ESP32-S3 OS

Small, modular OS-style firmware for an ESP32-S3 board built with PlatformIO and Arduino.

The goal of this project is not to build a full desktop-like operating system. The focus is a clean embedded architecture with:

- a structured boot sequence
- a serial debug shell
- persistent configuration in LittleFS
- SD card support for larger files and user data
- OLED as the primary live UI
- e-ink as a slower snapshot or mirror display
- a simple task manager for periodic work across both ESP32-S3 cores

## Current State

The project already boots, builds, and exposes a working serial shell. Hardware integration is split into dedicated modules and includes the main peripherals used in the older reference project.

Working features:

- serial startup log and prompt
- reduced high-frequency runtime logs for smoother serial monitor behavior
- LittleFS mount and config loading
- SD card initialization
- I2C hardware initialization
- DS3231 RTC support
- CardKB support
- buzzer test support
- OLED rendering
- Waveshare 3.52" e-ink support
- diagnostic shell commands
- dual-core task registration
- launcher with desktop + notifications + paged app grid
- settings app with WiFi manager, Bluetooth manager, and SD manager
- file manager app with SD browse/copy/move/delete/create-folder actions
- web upload app and shell-managed background server lifecycle
- notes app with read/write modes and markdown-aware read preview

## Hardware

Known wiring and peripherals:

- ESP32-S3 WROOM-style board
- I2C SDA: GPIO 8
- I2C SCL: GPIO 9
- CardKB address: 0x5F
- DS3231 RTC
- buzzer: GPIO 4
- SD card SPI bus:
  - MOSI: GPIO 38
  - SCK: GPIO 39
  - MISO: GPIO 40
  - CS: GPIO 46

## Project Layout

- src/main.cpp - Arduino entry point
- src/AppBootstrap.cpp - boot sequence and main loop
- src/TaskManager.cpp - periodic task scheduling
- src/StorageManager.cpp - LittleFS and SD helpers
- src/HardwareManager.cpp - I2C, RTC, CardKB, buzzer
- src/DisplayManager.cpp - OLED and e-ink rendering
- src/SerialShell.cpp - serial input parsing
- src/ShellCommands.cpp - command dispatch
- include/ - shared headers
- data/ - LittleFS config files

## Shell Commands

Available commands include:

- help
- info
- tasks
- fs info
- fs ls [path]
- sd info
- sd ls [path]
- config show
- config reload
- config save
- config set name <value>
- config set sd on|off
- config set sd_speed <hz>
- web-upload status|start|stop
- a2dp status|start <bt-name>|stop
- display info
- launcher show
- launcher next
- launcher prev
- launcher refresh
- launcher open <settings|file-manager|music-player|notes|web-upload|apps|serial|about>
- launcher list
- settings show
- oled test
- eink test [text]
- hw info
- hw scan
- hw beep [count]
- hw rtc
- kb read
- reboot
- prompt

## Notes App (Obsidian-Compatible)

The Notes app is designed for practical file movement between ESP and desktop Obsidian vaults.

Storage path behavior:

- default file: /notes/quicknote.md
- legacy fallback: /notes/quicknote.txt is still readable for backward compatibility
- load/save normalization: UTF-8 BOM is removed and CRLF is normalized to LF

Modes and controls:

- write mode: text editing enabled
- read mode: read-only to avoid accidental edits
- Tab or Esc: switch read/write mode
- right arrow: save note
- left arrow: global back navigation
- up/down arrows: cycle active tag filter

Supported markdown and Obsidian-style constructs in read preview:

- headings H1-H6 (`#` to `######`)
- unordered lists (`-`, `*`, `+`)
- ordered lists (`1.` and `1)`)
- task lists (`- [ ]`, `- [x]`)
- quotes (`> ...`)
- callouts (`> [!note]` style lines)
- horizontal rules (`---`, `***`, `___`)
- fenced code blocks (``` and ~~~)
- wikilinks (`[[Note]]`)
- wikilink alias form (`[[Note|Label]]`)
- embeds (`![[File]]`)
- markdown links (`[text](url)`)
- in-note hashtag tags for filtering (`#tag`)

Practical interoperability goal:

- files written on ESP open in Obsidian without conversion
- notes authored in Obsidian remain readable on ESP with markdown-aware preview

## Music Player on SD

The music-player app is a built-in screen, but its library lives on SD card.

Expected SD layout:

- /music-player/
- /music-player/playlist.txt (optional)
- /music-player/*.mp3 (or wav/aac/m4a/ogg/flac)

If /music-player/playlist.txt exists, each non-empty line is treated as a track path.

- Absolute path example: /music-player/album/song01.mp3
- Relative path example: album/song01.mp3

If no playlist file exists, the app scans files directly inside /music-player.

Launcher behavior:

- Built-in app id: music-player
- SD manifest app id (optional): music-player in /apps/apps.txt (opens as sdapp:music-player)

Note: ESP32-S3 does not execute native firmware code from SD card in this project.
The app logic stays in firmware, while tracks and playlist/config data are loaded from SD.

Current implementation note:

- the music-player UI is currently a skeleton placeholder while dual-chip audio transport is prepared

## Build

From the project root:

```bash
platformio run
```

## Upload

```bash
platformio run --target upload
```

## Serial Monitor

```bash
platformio device monitor
```

The monitor speed is 115200.

## Notes

- This project intentionally keeps the UI and hardware layers separate.
- E-ink is treated as a snapshot display, not the main interactive surface.
- The main loop prints a heartbeat so it is easy to verify that the firmware is alive.
- The board is currently configured to use the default serial behavior for the ESP32-S3 board definition.

## Bluetooth Audio Limitation

- ESP32-S3 is BLE-only and does not support Bluetooth Classic BR/EDR.
- Classic A2DP source playback (for many headsets like AirPods) is not supported directly on this MCU.
- The current architecture direction is dual-chip: ESP32-S3 for UI/system + classic ESP32 for A2DP/audio transport.

## Status

This repository is still a work in progress, but the base hardware, storage, display, shell, and task scaffolding are already in place.

## Future Upgrades

Planned follow-up work for the next session:

- make WiFi and Bluetooth scans non-blocking so menus stay responsive
- move more long-running work out of the foreground UI path
- continue web upload and settings modularization with tighter service boundaries
- expand Notes parser coverage for additional Obsidian Flavored Markdown features as needed
- connect music-player UI skeleton to the planned dual-chip audio transport path