#include "AppBootstrap.h"

#include "AppTasks.h"
#include "DisplayManager.h"
#include "HardwareManager.h"
#include "SerialShell.h"
#include "StorageManager.h"

namespace {
void processCardKbInput(SystemState &state, Stream &out) {
    static uint32_t lastKeyMs = 0;
    static char lastKey = 0;

    char ch = 0;
    if (!tryReadCardKb(state, ch)) {
        return;
    }

    const uint32_t nowMs = millis();
    const bool duplicateBurst = (ch == lastKey) && (nowMs - lastKeyMs < 80);
    if (duplicateBurst) {
        return;
    }

    noteDisplayActivity();
    const bool wokeDisplays = wakeDisplaysOnInput(state, out);

    if (handleActiveAppInput(state, ch, out)) {
        // Keep both displays in sync when the active app handled the key.
        renderActiveApp(state, out);
        lastKey = ch;
        lastKeyMs = nowMs;
        return;
    }

    if (wokeDisplays) {
        // After wake, restore the active screen on both displays.
        renderActiveApp(state, out);
    }

    out.print("kb key: 0x");
    if (static_cast<uint8_t>(ch) < 16) {
        out.print('0');
    }
    out.print(static_cast<uint8_t>(ch), HEX);
    out.print(" '");
    if (ch >= 32 && ch <= 126) {
        out.print(ch);
    } else {
        out.print('.');
    }
    out.println("'");

    lastKey = ch;
    lastKeyMs = nowMs;
}
}  // namespace

void setupApplication(SystemState &state, TaskManager &taskManager) {
    // Startup sekvence: serial -> filesystem -> SD.
    // To je zaklad, na ktery se pak da vrstvit GUI, konfig, logy nebo media.
    Serial.begin(115200);
    delay(500);

    Serial.println("os project ready");
    printPrompt();

    // Core safety: create mutexes before any hardware access or task starts.
    state.i2cMutex = xSemaphoreCreateMutex();
    state.spiMutex = xSemaphoreCreateMutex();

    state.i2cReady = initHardware(state);
    initDisplays(state);

    state.littleFsReady = initLittleFs(state);
    if (!state.littleFsReady) {
        Serial.println("LittleFS: nepodarilo se nastartovat");
    }

    if (state.config.sdEnabled) {
        state.sdReady = initSdCard(state);
    }

    // TaskManager je prvni krok k "OS" stylu architektury.
    // Tasky jsou rozdeleny na dve jadra, aby se pozdeji nezadrhavala UI vrstva.
    TaskManager::TaskConfig systemTaskConfig;
    systemTaskConfig.name = "system";
    systemTaskConfig.callback = systemTask;
    systemTaskConfig.context = &state;
    systemTaskConfig.periodMs = 2500;
    systemTaskConfig.stackWords = 4096;
    systemTaskConfig.priority = 1;
    systemTaskConfig.core = 0;
    taskManager.addTask(systemTaskConfig);

    TaskManager::TaskConfig uiTaskConfig;
    uiTaskConfig.name = "ui";
    uiTaskConfig.callback = uiTask;
    uiTaskConfig.context = &state;
    uiTaskConfig.periodMs = 3000;
    uiTaskConfig.stackWords = 4096;
    uiTaskConfig.priority = 1;
    uiTaskConfig.core = 1;
    taskManager.addTask(uiTaskConfig);

    taskManager.logStatus(Serial);
    if (!taskManager.start()) {
        Serial.println("TaskManager: nektere tasky se nepodarilo spustit");
    }

    if (state.buzzerReady) {
        testBuzzer(state, Serial, 1);
    }

    if (state.cardKbReady) {
        Serial.println("CardKB live logging: enabled");
    }

    renderLauncherScreen(state, Serial);
}

void processApplicationLoop(SystemState &state, TaskManager &taskManager) {
    static uint32_t lastHeartbeatMs = 0;
    const uint32_t nowMs = millis();
    if (nowMs - lastHeartbeatMs >= 2000) {
        Serial.print("loop heartbeat ms=");
        Serial.print(nowMs);
        Serial.print(" core=");
        Serial.print(xPortGetCoreID());
        Serial.print(" heap=");
        Serial.println(ESP.getFreeHeap());
        lastHeartbeatMs = nowMs;
    }

    processCardKbInput(state, Serial);

    processSerialInput(state, taskManager, Serial);
    handleEinkIdleTimeout(state, Serial);
    vTaskDelay(pdMS_TO_TICKS(10));
}