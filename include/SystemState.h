#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct AppConfig {
    String deviceName = "NoteWave";
    bool sdEnabled = true;
    uint32_t sdProbeSpeed = 4000000;
    String wifiSsid = "";
    String wifiPassword = "";
    String btPreferredDevice = "";
    String btPreferredAddress = "";
    String a2dpTargetName = "";
};

struct LauncherState {
    uint8_t selectedIndex = 0;
    String activeAppId = "desktop";
};

struct MusicPlayerState {
    String libraryPath = "/music-player";
    String statusMessage = "ready";
    String nowPlaying = "";
    uint8_t selectedIndex = 0;
    size_t scrollOffset = 0;
    bool playing = false;
};

struct SettingsState {
    bool wifiEnabled = false;
    uint8_t lastSelection = 0;
    String lastMessage = "ready";
    uint8_t viewMode = 0;
    int8_t selectedWifiIndex = -1;
    String selectedSsid = "";
    String wifiPassword = "";
    bool wifiConnected = false;
    String wifiConnectedSsid = "";
    String wifiIp = "";
    
    // Bluetooth state
    bool btEnabled = false;
    bool btConnected = false;
    String btConnectedDeviceName = "";
    String btConnectedDeviceAddress = "";
    int8_t selectedBluetoothIndex = -1;
};

struct FileManagerState {
    String currentPath = "/";
    String statusMessage = "ready";
    String clipboardPath = "";
    String menuTargetPath = "";
    String pendingFolderName = "";
    bool clipboardActive = false;
    bool clipboardMove = false;
    bool menuTargetIsDir = false;
    uint8_t viewMode = 0;
    uint8_t selectedIndex = 0;
    size_t scrollOffset = 0;
    uint8_t menuIndex = 0;
};

struct NotificationsState {
    uint8_t viewMode = 0;  // 0 hidden, 1 strip, 2 expanded
};

struct AudioState {
    bool a2dpActive = false;
    String a2dpTargetName = "";
    String statusMessage = "a2dp idle";
};

struct SystemState {
    AppConfig config;
    LauncherState launcher;
    MusicPlayerState musicPlayer;
    SettingsState settings;
    FileManagerState fileManager;
    NotificationsState notifications;
    AudioState audio;
    bool littleFsReady = false;
    bool sdReady = false;
    bool i2cReady = false;
    bool rtcReady = false;
    bool cardKbReady = false;
    bool buzzerReady = false;
    bool oledReady = false;
    bool einkReady = false;

    // Mutexes for thread-safe bus access
    SemaphoreHandle_t i2cMutex = nullptr;
    SemaphoreHandle_t spiMutex = nullptr;
};
