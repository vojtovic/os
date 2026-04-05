#pragma once

#include <Arduino.h>

#include "SystemState.h"

bool initHardware(SystemState &state, Stream &out = Serial);
void printHardwareInfo(const SystemState &state, Stream &out = Serial);
void scanI2cBus(Stream &out = Serial);
void testBuzzer(const SystemState &state, Stream &out = Serial, uint8_t beeps = 2);
bool tryReadCardKb(SystemState &state, char &ch);
bool printRtcNow(const SystemState &state, Stream &out = Serial);
