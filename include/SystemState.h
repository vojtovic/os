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
};