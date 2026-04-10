#pragma once

#include <Arduino.h>

#include "SystemState.h"

struct SettingsInputContext {
    size_t wifiListVisibleCount = 0;
    size_t *wifiCount = nullptr;
    size_t *wifiListScrollOffset = nullptr;
    bool *wifiScanInProgress = nullptr;
    String *wifiSsidList = nullptr;

    size_t *bluetoothDeviceCount = nullptr;
    size_t bluetoothListVisibleCount = 0;
    size_t *bluetoothListScrollOffset = nullptr;
    bool *bluetoothScanInProgress = nullptr;
    String *bluetoothDeviceList = nullptr;
    String *bluetoothDeviceAddressList = nullptr;

    char *czechComposeDeadKey = nullptr;

    bool (*isUpArrowCode)(uint8_t code) = nullptr;
    bool (*isDownArrowCode)(uint8_t code) = nullptr;

    bool (*toggleWifiEnabled)(SystemState &state, Stream &out) = nullptr;
    size_t (*scanWifiNetworks)(Stream &out) = nullptr;
    bool (*connectSelectedWifi)(SystemState &state, Stream &out) = nullptr;
    bool (*forgetSavedWifi)(SystemState &state, Stream &out) = nullptr;

    bool (*toggleBluetoothEnabled)(SystemState &state, Stream &out) = nullptr;
    bool (*disconnectBluetooth)(SystemState &state, Stream &out) = nullptr;
    bool (*selectBluetoothDevice)(SystemState &state, const String &deviceName, const String &deviceAddress, int8_t selectedIndex, Stream &out) = nullptr;
    bool (*forgetSavedBluetoothDevice)(SystemState &state, Stream &out) = nullptr;
    size_t (*scanBluetoothDevices)(Stream &out) = nullptr;

    bool (*tryApplyPostfixCzechCompose)(String &buffer, char deadKey) = nullptr;
    bool (*decodeCzechComposeKey)(char key, String &outputChunk) = nullptr;
    bool (*applySettingsSelection)(SystemState &state, uint8_t optionIndex, Stream &out) = nullptr;

    bool (*renderSettingsScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
    bool (*renderLauncherScreen)(SystemState &state, bool oledOnly, Stream &out) = nullptr;
};

bool handleSettingsAppInput(
    SystemState &state,
    char key,
    char normalizedKey,
    uint8_t rawCode,
    uint8_t settingsViewWifiList,
    uint8_t settingsViewWifiPassword,
    uint8_t settingsViewWifiSelectList,
    uint8_t settingsViewBluetoothList,
    uint8_t settingsViewBluetoothSelectList,
    uint8_t settingsViewSdList,
    uint8_t settingsViewHome,
    SettingsInputContext &context,
    Stream &out);
