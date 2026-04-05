#pragma once

#include <Arduino.h>

#include "ShellCommands.h"
#include "TaskManager.h"

void printPrompt(Stream &out = Serial);
void processSerialInput(SystemState &state, TaskManager &taskManager, Stream &out = Serial);