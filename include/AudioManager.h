#pragma once

#include <Arduino.h>

#include "SystemState.h"

bool startA2dpSource(SystemState &state, const String &targetName, Stream &out = Serial);
bool stopA2dpSource(SystemState &state, Stream &out = Serial);
void printA2dpStatus(const SystemState &state, Stream &out = Serial);
