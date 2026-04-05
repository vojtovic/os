#pragma once

#include <Arduino.h>

#include "SystemState.h"

bool initDisplays(SystemState &state, Stream &out = Serial);
void printDisplayInfo(const SystemState &state, Stream &out = Serial);
void renderOledStatus(const SystemState &state, Stream &out = Serial);
bool renderEinkMessage(SystemState &state, const String &text, bool forceFullRefresh = false, Stream &out = Serial);
