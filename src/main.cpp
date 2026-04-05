#include <Arduino.h>

#include "AppBootstrap.h"
#include "SystemState.h"
#include "TaskManager.h"

namespace {
TaskManager taskManager;
SystemState gState;

}  // namespace

void setup() {
    setupApplication(gState, taskManager);
}

void loop() {
    processApplicationLoop(gState, taskManager);
}
