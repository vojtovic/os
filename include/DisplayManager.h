#pragma once

#include <Arduino.h>

#include "SystemState.h"
#include "TaskManager.h"

bool initDisplays(SystemState &state, Stream &out = Serial);
void printDisplayInfo(const SystemState &state, Stream &out = Serial);
void renderOledStatus(const SystemState &state, Stream &out = Serial);
void printLauncherInfo(const SystemState &state, Stream &out = Serial);
bool renderLauncherScreen(SystemState &state, bool oledOnly = false, Stream &out = Serial);
bool renderSettingsScreen(SystemState &state, bool oledOnly = false, Stream &out = Serial);
bool renderStatusScreen(SystemState &state, const String &title, const String &line1, const String &line2, const String &line3, bool oledOnly = false, Stream &out = Serial);
bool renderActiveApp(SystemState &state, bool oledOnly = false, Stream &out = Serial);
void renderActiveAppOled(const SystemState &state);
bool handleActiveAppInput(SystemState &state, char key, Stream &out = Serial);
bool wakeDisplaysOnInput(SystemState &state, Stream &out = Serial);
void noteDisplayActivity();
void invalidateLauncherSdAppCache();
bool handleEinkIdleTimeout(SystemState &state, Stream &out = Serial);
bool renderEinkMessage(SystemState &state, const String &text, bool forceFullRefresh = false, Stream &out = Serial);
void setDesktopTaskManager(TaskManager *tm);
