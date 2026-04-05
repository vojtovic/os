#pragma once

#include <Arduino.h>

#include "SystemState.h"

bool initDisplays(SystemState &state, Stream &out = Serial);
void printDisplayInfo(const SystemState &state, Stream &out = Serial);
void renderOledStatus(const SystemState &state, Stream &out = Serial);
void printLauncherInfo(const SystemState &state, Stream &out = Serial);
bool renderLauncherScreen(SystemState &state, Stream &out = Serial);
bool renderSettingsScreen(SystemState &state, Stream &out = Serial);
bool renderActiveApp(SystemState &state, Stream &out = Serial);
bool handleActiveAppInput(SystemState &state, char key, Stream &out = Serial);
bool wakeDisplaysOnInput(SystemState &state, Stream &out = Serial);
void noteDisplayActivity();
bool handleEinkIdleTimeout(SystemState &state, Stream &out = Serial);
bool renderEinkMessage(SystemState &state, const String &text, bool forceFullRefresh = false, Stream &out = Serial);
