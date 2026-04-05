#pragma once

#include <Arduino.h>

struct AppConfig {
    String deviceName = "os";
    bool sdEnabled = true;
    uint32_t sdProbeSpeed = 4000000;
};

struct LauncherState {
    uint8_t selectedIndex = 0;
    String activeAppId = "launcher";
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
    String wifiIp = "";
};

struct SystemState {
    AppConfig config;
    LauncherState launcher;
    SettingsState settings;
    bool littleFsReady = false;
    bool sdReady = false;
    bool i2cReady = false;
    bool rtcReady = false;
    bool cardKbReady = false;
    bool buzzerReady = false;
    bool oledReady = false;
    bool einkReady = false;
};