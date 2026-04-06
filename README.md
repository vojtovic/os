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
- loop heartbeat for runtime verification
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
- display info
- oled test
- eink test [text]
- hw info
- hw scan
- hw beep [count]
- hw rtc
- kb read
- reboot
- prompt

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

## Status

This repository is still a work in progress, but the base hardware, storage, display, shell, and task scaffolding are already in place.

## Future Upgrades

Planned follow-up work for the next session:

- make WiFi and Bluetooth scans non-blocking so menus stay responsive
- move more long-running work out of the foreground UI path
- continue the web upload app with safer background servicing and better status output
- keep SD playback as the next larger subsystem after the UI stays responsive