#pragma once

#include <Arduino.h>

#include "ConfigStore.h"
#include "StorageManager.h"
#include "TaskManager.h"

bool executeShellCommand(SystemState &state, TaskManager &taskManager, const String &line, Stream &out = Serial);
void printShellHelp(Stream &out = Serial);