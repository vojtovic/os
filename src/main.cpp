#include <Arduino.h>

#include "AppTasks.h"
#include "SerialShell.h"
#include "StorageManager.h"
#include "TaskManager.h"
#include "SystemState.h"

namespace {
TaskManager taskManager;
SystemState gState;

}  // namespace

void setup() {
    // Startup sekvence: serial -> filesystem -> SD.
    // To je zaklad, na ktery se pak da vrstvit GUI, konfig, logy nebo media.
    Serial.begin(115200);
    delay(500);

    Serial.println("os project ready");
    printPrompt();

    gState.littleFsReady = initLittleFs(gState);
    if (!gState.littleFsReady) {
        Serial.println("LittleFS: nepodarilo se nastartovat");
    }

    if (gState.config.sdEnabled) {
        gState.sdReady = initSdCard(gState);
    }

    // TaskManager je prvni krok k "OS" stylu architektury.
    // Tasky jsou rozdeleny na dve jadra, aby se pozdeji nezadrhavala UI vrstva.
    TaskManager::TaskConfig systemTaskConfig;
    systemTaskConfig.name = "system";
    systemTaskConfig.callback = systemTask;
    systemTaskConfig.context = &gState;
    systemTaskConfig.periodMs = 2500;
    systemTaskConfig.stackWords = 4096;
    systemTaskConfig.priority = 1;
    systemTaskConfig.core = 0;
    taskManager.addTask(systemTaskConfig);

    TaskManager::TaskConfig uiTaskConfig;
    uiTaskConfig.name = "ui";
    uiTaskConfig.callback = uiTask;
    uiTaskConfig.context = &gState;
    uiTaskConfig.periodMs = 3000;
    uiTaskConfig.stackWords = 4096;
    uiTaskConfig.priority = 1;
    uiTaskConfig.core = 1;
    taskManager.addTask(uiTaskConfig);

    taskManager.logStatus(Serial);
    if (!taskManager.start()) {
        Serial.println("TaskManager: nektere tasky se nepodarilo spustit");
    }
}

void loop() {
    processSerialInput(gState, taskManager, Serial);
    vTaskDelay(pdMS_TO_TICKS(10));
}
