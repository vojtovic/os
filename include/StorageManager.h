#pragma once

#include <Arduino.h>

#include "ConfigStore.h"
#include "SystemState.h"

bool initLittleFs(SystemState &state);
bool initSdCard(SystemState &state);
void printFsInfo(const SystemState &state, Stream &out = Serial);
void printSdInfo(const SystemState &state, Stream &out = Serial);
void listLittleFs(const SystemState &state, const String &path, Stream &out = Serial);
void listSd(const SystemState &state, const String &path, Stream &out = Serial);