#pragma once

#include <Arduino.h>

#include "StorageManager.h"
#include "TaskManager.h"

void printPrompt(Stream &out = Serial);
void printHelp(Stream &out = Serial);
void processSerialInput(SystemState &state, TaskManager &taskManager, Stream &out = Serial);