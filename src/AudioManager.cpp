#include "AudioManager.h"

bool startA2dpSource(SystemState &state, const String &targetName, Stream &out) {
    String target = targetName;
    target.trim();
    if (target.isEmpty()) {
        state.settings.lastMessage = "a2dp target missing";
        out.println("A2DP: target device name is required");
        return false;
    }

    // ESP32-S3 supports BLE only; Bluetooth Classic A2DP source is not available.
    state.audio.a2dpActive = false;
    state.audio.a2dpTargetName = target;
    state.audio.statusMessage = "unsupported on esp32-s3";
    state.config.a2dpTargetName = target;

    out.println("A2DP: unsupported on ESP32-S3 (Bluetooth Classic is not available)");
    out.println("A2DP: use ESP32 (classic) hardware for AirPods/audio output");
    return false;
}

bool stopA2dpSource(SystemState &state, Stream &out) {
    state.audio.a2dpActive = false;
    state.audio.statusMessage = "unsupported on esp32-s3";
    out.println("A2DP: no-op on ESP32-S3 (unsupported)");
    return true;
}

void printA2dpStatus(const SystemState &state, Stream &out) {
    out.println("--- A2DP ---");
    out.print("active: ");
    out.println(state.audio.a2dpActive ? "yes" : "no");
    out.print("target: ");
    if (state.audio.a2dpTargetName.isEmpty()) {
        out.println("(none)");
    } else {
        out.println(state.audio.a2dpTargetName);
    }
    out.print("status: ");
    out.println(state.audio.statusMessage);
}
