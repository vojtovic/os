#pragma once

#include <Arduino.h>

#include "SystemState.h"

bool loadConfig(AppConfig &config);
bool saveConfig(const AppConfig &config);
void printConfig(const AppConfig &config, Stream &out = Serial);