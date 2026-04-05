#include "AppTasks.h"

void systemTask(void *context) {
    auto *state = static_cast<SystemState *>(context);
    static uint32_t tick = 0;

    Serial.print("[core ");
    Serial.print(xPortGetCoreID());
    Serial.print("] systemTask ");
    Serial.print(++tick);
    Serial.print(" device=");
    Serial.println(state->config.deviceName);
}

void uiTask(void *context) {
    auto *state = static_cast<SystemState *>(context);
    static uint32_t tick = 0;

    Serial.print("[core ");
    Serial.print(xPortGetCoreID());
    Serial.print("] uiTask ");
    Serial.print(++tick);
    Serial.print(" sd=");
    Serial.println(state->config.sdEnabled ? "on" : "off");
}