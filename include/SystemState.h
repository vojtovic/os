#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct AppConfig {
    String deviceName = "os";
    bool sdEnabled = true;
    uint32_t sdProbeSpeed = 4000000;
};

struct LauncherState {
    uint8_t selectedIndex = 0;
    String activeAppId = "desktop";
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

struct SystemState {
    AppConfig config;
    LauncherState launcher;
    SettingsState settings;
    FileManagerState fileManager;
    NotificationsState notifications;
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
