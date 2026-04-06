# Copilot Instructions For mp3-pedia/os

This repository is an ESP32-S3 firmware project built with PlatformIO and Arduino.

## Project goals

- Keep architecture modular (HardwareManager, DisplayManager, StorageManager, SerialShell, TaskManager).
- Prefer small, safe changes that preserve current shell behavior and boot stability.
- Maintain clean serial diagnostics at 115200.

## Build and validation

- Primary build command: `platformio run`
- Upload command: `platformio run --target upload`
- Serial monitor command: `platformio device monitor`
- After code edits, always run at least a build.

## Hardware and runtime constraints

- Board environment: `freenove_esp32_s3_wroom`
- Do not force USB CDC macros unless explicitly requested.
- Keep OLED and e-ink shared SPI behavior safe:
  - Inactive chip-select lines must remain HIGH.
  - E-ink control lines must be initialized to safe idle states early.

## Coding guidelines

- Keep APIs and module boundaries stable unless refactor is explicitly requested.
- Avoid broad formatting-only edits.
- Add concise comments only where logic is non-obvious.
- Prefer deterministic fixes over speculative large rewrites.

## Change policy

- Do not revert unrelated user changes.
- If unexpected unrelated edits appear, stop and ask for confirmation.
