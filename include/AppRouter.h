#pragma once

#include <Arduino.h>

#include "SystemState.h"

struct AppRouterRenderContext {
    bool (*renderDesktopScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderNotificationsScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderLauncherScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderSettingsScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderFileManagerScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderWebUploadScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderPlaceholderApp)(SystemState &state, Stream &out) = nullptr;
};

struct AppRouterInputContext {
    bool (*isLeftArrowCode)(uint8_t code) = nullptr;
    bool (*isDownArrowCode)(uint8_t code) = nullptr;
    bool (*isRightArrowCode)(uint8_t code) = nullptr;
    bool (*renderDesktopScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderLauncherScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderSettingsScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderNotificationsScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    void (*stopWebUploadServer)(Stream &out) = nullptr;
};

bool appRouterRenderActiveApp(
    SystemState &state,
    bool oledOnly,
    Stream &out,
    AppRouterRenderContext &context);

bool appRouterHandleBackInput(
    SystemState &state,
    uint8_t rawCode,
    uint8_t settingsViewHome,
    uint8_t settingsViewWifiList,
    uint8_t settingsViewWifiPassword,
    uint8_t settingsViewWifiSelectList,
    uint8_t settingsViewBluetoothList,
    uint8_t settingsViewBluetoothSelectList,
    AppRouterInputContext &context,
    Stream &out);

bool appRouterHandleDesktopDirectionalInput(
    SystemState &state,
    uint8_t rawCode,
    AppRouterInputContext &context,
    Stream &out);
