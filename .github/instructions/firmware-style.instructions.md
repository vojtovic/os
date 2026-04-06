---
applyTo: "src/**/*.cpp,include/**/*.h,platformio.ini"
description: "Project-specific style and safety rules for ESP32-S3 firmware changes."
---

- Keep changes small and module-focused.
- Preserve current boot sequence and serial shell behavior.
- Prefer explicit error handling and logs around hardware init failures.
- Avoid introducing hidden global state unless required by hardware driver constraints.
- For display changes, preserve shared SPI line safety and deterministic CS handling.
- Validate with a PlatformIO build after modifications.
