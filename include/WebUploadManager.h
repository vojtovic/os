#pragma once

#include <Arduino.h>

#include "SystemState.h"

bool renderWebUploadScreen(SystemState &state, bool oledOnly = false, Stream &out = Serial);
bool startWebUploadServer(SystemState &state, Stream &out = Serial);
void printWebUploadStatus(const SystemState &state, Stream &out = Serial);
bool isWebUploadServerRunning();
void serviceWebUploadServer(SystemState &state);
void stopWebUploadServer(Stream &out = Serial);