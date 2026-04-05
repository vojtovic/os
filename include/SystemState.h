#pragma once

#include <Arduino.h>

struct AppConfig {
    String deviceName = "os";
    bool sdEnabled = true;
    uint32_t sdProbeSpeed = 4000000;
};

struct SystemState {
    AppConfig config;
    bool littleFsReady = false;
    bool sdReady = false;
    bool i2cReady = false;
    bool rtcReady = false;
    bool cardKbReady = false;
    bool buzzerReady = false;
    bool oledReady = false;
    bool einkReady = false;
};