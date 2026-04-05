#pragma once

#include "SystemState.h"
#include "TaskManager.h"

void setupApplication(SystemState &state, TaskManager &taskManager);
void processApplicationLoop(SystemState &state, TaskManager &taskManager);