# Graph Report - .  (2026-04-19)

## Corpus Check
- 50 files · ~75,009 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 465 nodes · 1079 edges · 49 communities detected
- Extraction: 80% EXTRACTED · 20% INFERRED · 0% AMBIGUOUS · INFERRED: 212 edges (avg confidence: 0.8)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_File Manager & App UI|File Manager & App UI]]
- [[_COMMUNITY_E-ink Rendering & Wake|E-ink Rendering & Wake]]
- [[_COMMUNITY_Boot & Task Scheduling|Boot & Task Scheduling]]
- [[_COMMUNITY_Shell, Config & A2DP Status|Shell, Config & A2DP Status]]
- [[_COMMUNITY_Notes App Logic|Notes App Logic]]
- [[_COMMUNITY_Web Upload HTTPS Server|Web Upload HTTPS Server]]
- [[_COMMUNITY_Settings WiFiBluetooth|Settings WiFi/Bluetooth]]
- [[_COMMUNITY_E-paint Drawing Primitives|E-paint Drawing Primitives]]
- [[_COMMUNITY_App Router & Launcher|App Router & Launcher]]
- [[_COMMUNITY_Display SleepWake Bugs|Display Sleep/Wake Bugs]]
- [[_COMMUNITY_Hardware Board Reference|Hardware Board Reference]]
- [[_COMMUNITY_Serial Shell Surface|Serial Shell Surface]]
- [[_COMMUNITY_Storage Layer (LittleFSSD)|Storage Layer (LittleFS/SD)]]
- [[_COMMUNITY_Dual-Chip A2DP Audio|Dual-Chip A2DP Audio]]
- [[_COMMUNITY_Notes Markdown & Czech I18n|Notes Markdown & Czech I18n]]
- [[_COMMUNITY_FreeRTOS Core Tasks|FreeRTOS Core Tasks]]
- [[_COMMUNITY_SD Audio Pipeline TODO|SD Audio Pipeline TODO]]
- [[_COMMUNITY_E-paper Driver Interface|E-paper Driver Interface]]
- [[_COMMUNITY_TaskManager Header|TaskManager Header]]
- [[_COMMUNITY_Coding Conventions|Coding Conventions]]
- [[_COMMUNITY_Image Data Header|Image Data Header]]
- [[_COMMUNITY_Fonts Header|Fonts Header]]
- [[_COMMUNITY_Image Data Source|Image Data Source]]
- [[_COMMUNITY_SystemState Source|SystemState Source]]
- [[_COMMUNITY_Font 16 Source|Font 16 Source]]
- [[_COMMUNITY_Font 12 Source|Font 12 Source]]
- [[_COMMUNITY_Font 20 Source|Font 20 Source]]
- [[_COMMUNITY_Font 24 Source|Font 24 Source]]
- [[_COMMUNITY_Font 8 Source|Font 8 Source]]
- [[_COMMUNITY_ConfigStore Header|ConfigStore Header]]
- [[_COMMUNITY_StorageManager Header|StorageManager Header]]
- [[_COMMUNITY_SettingsInputManager Header|SettingsInputManager Header]]
- [[_COMMUNITY_DisplayManager Header|DisplayManager Header]]
- [[_COMMUNITY_Settings Connectivity Header|Settings Connectivity Header]]
- [[_COMMUNITY_SystemState Header|SystemState Header]]
- [[_COMMUNITY_AppRouter Header|AppRouter Header]]
- [[_COMMUNITY_HardwareManager Header|HardwareManager Header]]
- [[_COMMUNITY_WebUploadManager Header|WebUploadManager Header]]
- [[_COMMUNITY_AppBootstrap Header|AppBootstrap Header]]
- [[_COMMUNITY_AudioManager Header|AudioManager Header]]
- [[_COMMUNITY_ShellCommands Header|ShellCommands Header]]
- [[_COMMUNITY_SerialShell Header|SerialShell Header]]
- [[_COMMUNITY_AppTasks Header|AppTasks Header]]
- [[_COMMUNITY_Project Goals|Project Goals]]
- [[_COMMUNITY_Hardware Abstraction|Hardware Abstraction]]
- [[_COMMUNITY_Config INI Defaults|Config INI Defaults]]
- [[_COMMUNITY_Logger & Debug Levels|Logger & Debug Levels]]
- [[_COMMUNITY_Init Error Handling|Init Error Handling]]
- [[_COMMUNITY_OS-like ESP32-S3 Goal|OS-like ESP32-S3 Goal]]

## God Nodes (most connected - your core abstractions)
1. `handleNotesAppInput()` - 33 edges
2. `renderNotesScreen()` - 30 edges
3. `handleActiveAppInput()` - 28 edges
4. `executeShellCommand()` - 28 edges
5. `renderLauncherScreen()` - 23 edges
6. `renderDesktopScreen()` - 18 edges
7. `fileManagerRefreshListing()` - 15 edges
8. `DrawRectangle()` - 14 edges
9. `setupApplication()` - 14 edges
10. `handleSettingsAppInput()` - 14 edges

## Surprising Connections (you probably didn't know these)
- `processApplicationLoop()` --documented_symbol--> `processApplicationLoop()`  [INFERRED]
  CLAUDE.md → src/AppBootstrap.cpp
- `setupApplication()` --documented_symbol--> `setupApplication()`  [INFERRED]
  CLAUDE.md → src/AppBootstrap.cpp
- `SystemState (central state struct)` --documented_symbol--> `SystemState AST node (include/SystemState.h)`  [INFERRED]
  CLAUDE.md → include/SystemState.h
- `Display strategy (OLED live, e-ink snapshot)` --semantically_similar_to--> `Shared SPI bus safety rule`  [INFERRED] [semantically similar]
  PROJECT_STATE.md → CLAUDE.md
- `Missing: Czech localization (CardKB + fonts)` --semantically_similar_to--> `Notes supported markdown syntax (headings, lists, callouts, wikilinks)`  [INFERRED] [semantically similar]
  OPEN_ISSUES.md → README.md

## Hyperedges (group relationships)
- **Dual-chip audio architecture (BLE-only S3 + classic ESP32 A2DP)** — claude_ble_only_rationale, claude_audiomanager, notes_dual_chip_airpods, notes_uart_protocol, readme_bluetooth_limitation [EXTRACTED 1.00]
- **Sleep/wake subsystem (bugs + roadmap)** — open_issues_eink_wake_bug, open_issues_oled_sleep_bug, open_issues_sleep_wake_inconsistency, open_issues_unified_sleep_manager, open_issues_debug_logs, open_issues_wake_test_command, notes_power_management [EXTRACTED 1.00]
- **App routing flow (activeAppId -> render/input dispatch)** — claude_activeappid, claude_approuter, claude_displaymanager, claude_systemstate [EXTRACTED 1.00]

## Communities

### Community 0 - "File Manager & App UI"
Cohesion: 0.05
Nodes (82): desktopHeapString(), desktopUptimeString(), drawActiveAppOled(), drawFileManagerOled(), drawMusicPlayerOled(), drawSettingsOled(), fileManagerBaseName(), fileManagerCopyRecursive() (+74 more)

### Community 1 - "E-ink Rendering & Wake"
Cohesion: 0.12
Nodes (47): drawFileManagerEink(), drawLauncherIcon(), drawMusicPlayerEink(), drawSettingsEink(), ensureEinkInitialized(), forceOledWakeOn(), markDisplayActivity(), noteDisplayActivity() (+39 more)

### Community 2 - "Boot & Task Scheduling"
Cohesion: 0.06
Nodes (36): describeCardKbKey(), logCardKbInput(), processApplicationLoop(), processCardKbInput(), setupApplication(), AppBootstrap, AppTasks (systemTask + uiTask), main.cpp entry point (+28 more)

### Community 3 - "Shell, Config & A2DP Status"
Cohesion: 0.09
Nodes (34): applyUnsupportedA2dpState(), printA2dpStatus(), startA2dpSource(), stopA2dpSource(), loadConfig(), parseBool(), printConfig(), trimCopy() (+26 more)

### Community 4 - "Notes App Logic"
Cohesion: 0.09
Nodes (36): addNotesLinkUnique(), addNotesTagUnique(), clearNotesLinks(), clearNotesTags(), commitNotesDraftWord(), ensureNotesLoaded(), handleNotesAppInput(), isNotesEnterKey() (+28 more)

### Community 5 - "Web Upload HTTPS Server"
Cohesion: 0.17
Nodes (31): handleWebUploadCommand(), buildListJson(), buildPageHtml(), chooseTargetFs(), currentTargetLabel(), ensureCertificate(), ensureDirectoryPath(), ensureServerRunning() (+23 more)

### Community 6 - "Settings WiFi/Bluetooth"
Cohesion: 0.12
Nodes (26): saveConfig(), applySettingsSelection(), connectSelectedWifi(), decodeCzechComposeKey(), forgetSavedBluetoothDevice(), forgetSavedWifi(), mapCzechComposedChar(), scanBluetoothDevices() (+18 more)

### Community 7 - "E-paint Drawing Primitives"
Cohesion: 0.16
Nodes (17): Clear(), decodeUtf8BlockGlyph(), decodeUtf8CzechGlyph(), DrawAbsolutePixel(), drawBlockGlyph(), DrawCharAt(), DrawCircle(), drawDiacriticMark() (+9 more)

### Community 8 - "App Router & Launcher"
Cohesion: 0.17
Nodes (21): appRouterHandleBackInput(), appRouterHandleDesktopDirectionalInput(), appRouterRenderActiveApp(), decodeCardKbKey(), drawLauncherCard(), drawLauncherOled(), drawLauncherPreview(), fileManagerResetSession() (+13 more)

### Community 9 - "Display Sleep/Wake Bugs"
Cohesion: 0.12
Nodes (19): state.launcher.activeAppId, AppRouter module, DisplayManager module, E-ink (Waveshare 3.52), OLED (SH1106 128x64), Shared SPI bus safety rule, Event system concept, Power management (sleep/wake) (+11 more)

### Community 10 - "Hardware Board Reference"
Cohesion: 0.25
Nodes (9): ESP32-S3 (freenove_esp32_s3_wroom), HardwareManager module, CardKB at 0x5F, DS3231 RTC, Reused hardware from mp3_test, Pin layout (I2C 8/9, SD 38/39/40/46), Build / upload / monitor commands, Hardware wiring reference (+1 more)

### Community 11 - "Serial Shell Surface"
Cohesion: 0.29
Nodes (7): SerialShell module, ShellCommands module, WebUploadManager (HTTPS file server), Shell commands reference, Serial debug shell concept, TODO P2: shell display wake-test command, Shell commands (available CLI)

### Community 12 - "Storage Layer (LittleFS/SD)"
Cohesion: 0.33
Nodes (7): ConfigStore module, LittleFS (internal filesystem), /apps/apps.txt SD manifest, SD card (HSPI bus), StorageManager module, Storage layer (LittleFS vs SD card), Music player SD layout (/music-player/)

### Community 13 - "Dual-Chip A2DP Audio"
Cohesion: 0.33
Nodes (7): AudioManager (A2DP source skeleton), Rationale: ESP32-S3 is BLE-only, A2DP requires dual-chip, Dual-chip A2DP audio architecture (AirPods), Rationale: dual-chip separates audio from UI load, UART command protocol (CONNECT/PLAY/PAUSE/STOP/STATUS), Audio strategy (I2S/DAC, separate subsystem), Bluetooth audio limitation (BLE-only S3)

### Community 14 - "Notes Markdown & Czech I18n"
Cohesion: 0.33
Nodes (6): TODO P2: CZ input + render localization, Missing: Czech localization (CardKB + fonts), Notes app (Obsidian-compatible markdown), Notes supported markdown syntax (headings, lists, callouts, wikilinks), Notes read/write modes and controls, Working features list

### Community 15 - "FreeRTOS Core Tasks"
Cohesion: 0.67
Nodes (0): 

### Community 16 - "SD Audio Pipeline TODO"
Cohesion: 0.67
Nodes (3): TODO P2: SD audio player MVP, Missing: real SD audio playback pipeline, Future upgrades backlog

### Community 17 - "E-paper Driver Interface"
Cohesion: 1.0
Nodes (0): 

### Community 18 - "TaskManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 19 - "Coding Conventions"
Cohesion: 1.0
Nodes (2): Coding conventions, Layering rule (HW init in modules, UI reads state)

### Community 20 - "Image Data Header"
Cohesion: 1.0
Nodes (0): 

### Community 21 - "Fonts Header"
Cohesion: 1.0
Nodes (0): 

### Community 22 - "Image Data Source"
Cohesion: 1.0
Nodes (0): 

### Community 23 - "SystemState Source"
Cohesion: 1.0
Nodes (0): 

### Community 24 - "Font 16 Source"
Cohesion: 1.0
Nodes (0): 

### Community 25 - "Font 12 Source"
Cohesion: 1.0
Nodes (0): 

### Community 26 - "Font 20 Source"
Cohesion: 1.0
Nodes (0): 

### Community 27 - "Font 24 Source"
Cohesion: 1.0
Nodes (0): 

### Community 28 - "Font 8 Source"
Cohesion: 1.0
Nodes (0): 

### Community 29 - "ConfigStore Header"
Cohesion: 1.0
Nodes (0): 

### Community 30 - "StorageManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 31 - "SettingsInputManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 32 - "DisplayManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 33 - "Settings Connectivity Header"
Cohesion: 1.0
Nodes (0): 

### Community 34 - "SystemState Header"
Cohesion: 1.0
Nodes (0): 

### Community 35 - "AppRouter Header"
Cohesion: 1.0
Nodes (0): 

### Community 36 - "HardwareManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 37 - "WebUploadManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 38 - "AppBootstrap Header"
Cohesion: 1.0
Nodes (0): 

### Community 39 - "AudioManager Header"
Cohesion: 1.0
Nodes (0): 

### Community 40 - "ShellCommands Header"
Cohesion: 1.0
Nodes (0): 

### Community 41 - "SerialShell Header"
Cohesion: 1.0
Nodes (0): 

### Community 42 - "AppTasks Header"
Cohesion: 1.0
Nodes (0): 

### Community 43 - "Project Goals"
Cohesion: 1.0
Nodes (1): Project goals (small structured ESP32-S3 system)

### Community 44 - "Hardware Abstraction"
Cohesion: 1.0
Nodes (1): Hardware abstraction layer

### Community 45 - "Config INI Defaults"
Cohesion: 1.0
Nodes (1): Config INI with defaults + validation

### Community 46 - "Logger & Debug Levels"
Cohesion: 1.0
Nodes (1): Logger and debug levels

### Community 47 - "Init Error Handling"
Cohesion: 1.0
Nodes (1): Error handling (init returns status, fallback)

### Community 48 - "OS-like ESP32-S3 Goal"
Cohesion: 1.0
Nodes (1): Project block 0 goal (OS-like ESP32-S3)

## Knowledge Gaps
- **43 isolated node(s):** `StringStream`, `ConfigStore module`, `WebUploadManager (HTTPS file server)`, `state.launcher.activeAppId`, `/apps/apps.txt SD manifest` (+38 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **Thin community `E-paper Driver Interface`** (2 nodes): `EpdIf()`, `EPD_3in52.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `TaskManager Header`** (2 nodes): `TaskManager.h`, `TaskManager()`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Coding Conventions`** (2 nodes): `Coding conventions`, `Layering rule (HW init in modules, UI reads state)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Image Data Header`** (1 nodes): `imagedata.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Fonts Header`** (1 nodes): `fonts.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Image Data Source`** (1 nodes): `imagedata.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `SystemState Source`** (1 nodes): `SystemState.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Font 16 Source`** (1 nodes): `font16.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Font 12 Source`** (1 nodes): `font12.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Font 20 Source`** (1 nodes): `font20.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Font 24 Source`** (1 nodes): `font24.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Font 8 Source`** (1 nodes): `font8.cpp`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `ConfigStore Header`** (1 nodes): `ConfigStore.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `StorageManager Header`** (1 nodes): `StorageManager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `SettingsInputManager Header`** (1 nodes): `SettingsInputManager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `DisplayManager Header`** (1 nodes): `DisplayManager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Settings Connectivity Header`** (1 nodes): `SettingsConnectivityService.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `SystemState Header`** (1 nodes): `SystemState.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `AppRouter Header`** (1 nodes): `AppRouter.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `HardwareManager Header`** (1 nodes): `HardwareManager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `WebUploadManager Header`** (1 nodes): `WebUploadManager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `AppBootstrap Header`** (1 nodes): `AppBootstrap.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `AudioManager Header`** (1 nodes): `AudioManager.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `ShellCommands Header`** (1 nodes): `ShellCommands.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `SerialShell Header`** (1 nodes): `SerialShell.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `AppTasks Header`** (1 nodes): `AppTasks.h`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Project Goals`** (1 nodes): `Project goals (small structured ESP32-S3 system)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Hardware Abstraction`** (1 nodes): `Hardware abstraction layer`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Config INI Defaults`** (1 nodes): `Config INI with defaults + validation`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Logger & Debug Levels`** (1 nodes): `Logger and debug levels`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `Init Error Handling`** (1 nodes): `Error handling (init returns status, fallback)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.
- **Thin community `OS-like ESP32-S3 Goal`** (1 nodes): `Project block 0 goal (OS-like ESP32-S3)`
  Too small to be a meaningful cluster - may be noise or needs more connections extracted.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `executeShellCommand()` connect `Shell, Config & A2DP Status` to `File Manager & App UI`, `E-ink Rendering & Wake`, `Boot & Task Scheduling`, `Web Upload HTTPS Server`, `App Router & Launcher`?**
  _High betweenness centrality (0.092) - this node is a cross-community bridge._
- **Why does `setupApplication()` connect `Boot & Task Scheduling` to `App Router & Launcher`, `E-ink Rendering & Wake`, `Shell, Config & A2DP Status`?**
  _High betweenness centrality (0.068) - this node is a cross-community bridge._
- **Why does `renderStatusScreen()` connect `E-ink Rendering & Wake` to `File Manager & App UI`, `App Router & Launcher`, `Web Upload HTTPS Server`?**
  _High betweenness centrality (0.060) - this node is a cross-community bridge._
- **Are the 8 inferred relationships involving `renderNotesScreen()` (e.g. with `appRouterRenderActiveApp()` and `DrawLine()`) actually correct?**
  _`renderNotesScreen()` has 8 INFERRED edges - model-reasoned connections that need verification._
- **Are the 4 inferred relationships involving `handleActiveAppInput()` (e.g. with `processCardKbInput()` and `appRouterHandleDesktopDirectionalInput()`) actually correct?**
  _`handleActiveAppInput()` has 4 INFERRED edges - model-reasoned connections that need verification._
- **Are the 22 inferred relationships involving `executeShellCommand()` (e.g. with `processSerialInput()` and `handleDesktopTerminalInput()`) actually correct?**
  _`executeShellCommand()` has 22 INFERRED edges - model-reasoned connections that need verification._
- **Are the 10 inferred relationships involving `renderLauncherScreen()` (e.g. with `appRouterRenderActiveApp()` and `appRouterHandleBackInput()`) actually correct?**
  _`renderLauncherScreen()` has 10 INFERRED edges - model-reasoned connections that need verification._