#include "SettingsInputManager.h"

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
    Stream &out) {
    if (state.settings.viewMode == settingsViewWifiList) {
        if (normalizedKey == '1') {
            context.toggleWifiEnabled(state, out);
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == '2') {
            if (!state.settings.wifiEnabled) {
                state.settings.lastMessage = "enable wifi first";
                context.renderSettingsScreen(state, false, out);
                return true;
            }

            state.settings.viewMode = settingsViewWifiSelectList;
            state.settings.selectedWifiIndex = -1;
            state.settings.selectedSsid = "";
            state.settings.wifiPassword = "";
            state.settings.lastMessage = "wifi scanning";
            *context.wifiCount = 0;
            *context.wifiScanInProgress = true;
            context.renderSettingsScreen(state, false, out);
            context.scanWifiNetworks(out);
            *context.wifiScanInProgress = false;
            state.settings.lastMessage = "select wifi";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == '3') {
            if (context.forgetSavedWifi != nullptr) {
                context.forgetSavedWifi(state, out);
            } else {
                state.settings.lastMessage = "forget unavailable";
            }
            context.renderSettingsScreen(state, false, out);
            return true;
        }
        return false;
    }

    if (state.settings.viewMode == settingsViewWifiSelectList) {
        const size_t maxOffset = *context.wifiCount > context.wifiListVisibleCount ? *context.wifiCount - context.wifiListVisibleCount : 0;

        if (context.isUpArrowCode(rawCode)) {
            if (*context.wifiListScrollOffset > 0) {
                *context.wifiListScrollOffset = *context.wifiListScrollOffset > context.wifiListVisibleCount ? *context.wifiListScrollOffset - context.wifiListVisibleCount : 0;
                state.settings.lastMessage = "wifi scroll up";
                context.renderSettingsScreen(state, false, out);
            }
            return true;
        }

        if (context.isDownArrowCode(rawCode)) {
            if (*context.wifiListScrollOffset < maxOffset) {
                *context.wifiListScrollOffset = min(*context.wifiListScrollOffset + context.wifiListVisibleCount, maxOffset);
                state.settings.lastMessage = "wifi scroll down";
                context.renderSettingsScreen(state, false, out);
            }
            return true;
        }

        if (normalizedKey >= '1' && normalizedKey <= '9') {
            const uint8_t pick = static_cast<uint8_t>(normalizedKey - '1');
            const size_t absoluteIndex = *context.wifiListScrollOffset + pick;
            if (absoluteIndex < *context.wifiCount) {
                if (context.wifiSsidList[absoluteIndex].isEmpty()) {
                    state.settings.lastMessage = "hidden wifi unsupported";
                    context.renderSettingsScreen(state, false, out);
                    return true;
                }
                state.settings.selectedWifiIndex = static_cast<int8_t>(absoluteIndex);
                state.settings.selectedSsid = context.wifiSsidList[absoluteIndex];
                if (!state.config.wifiSsid.isEmpty() && state.settings.selectedSsid == state.config.wifiSsid) {
                    state.settings.wifiPassword = state.config.wifiPassword;
                    state.settings.lastMessage = "saved password loaded";
                } else {
                    state.settings.wifiPassword = "";
                    state.settings.lastMessage = String("ssid ") + state.settings.selectedSsid;
                }
                state.settings.viewMode = settingsViewWifiPassword;
                context.renderSettingsScreen(state, false, out);
                return true;
            }
        }
        return false;
    }

    if (state.settings.viewMode == settingsViewBluetoothList) {
        if (normalizedKey == '1') {
            context.toggleBluetoothEnabled(state, out);
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == '2') {
            if (!state.settings.btEnabled) {
                state.settings.lastMessage = "enable bt first";
                context.renderSettingsScreen(state, false, out);
                return true;
            }

            state.settings.viewMode = settingsViewBluetoothSelectList;
            state.settings.selectedBluetoothIndex = -1;
            state.settings.btConnectedDeviceName = "";
            state.settings.btConnectedDeviceAddress = "";
            state.settings.btConnected = false;
            state.settings.lastMessage = "bt scanning";
            *context.bluetoothDeviceCount = 0;
            if (context.bluetoothListScrollOffset != nullptr) {
                *context.bluetoothListScrollOffset = 0;
            }
            *context.bluetoothScanInProgress = true;
            context.renderSettingsScreen(state, false, out);
            context.scanBluetoothDevices(out);
            *context.bluetoothScanInProgress = false;
            state.settings.lastMessage = "select bt";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == '3') {
            context.disconnectBluetooth(state, out);
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == '4') {
            if (context.forgetSavedBluetoothDevice != nullptr) {
                context.forgetSavedBluetoothDevice(state, out);
            } else {
                state.settings.lastMessage = "bt forget unavailable";
            }
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        return false;
    }

    if (state.settings.viewMode == settingsViewBluetoothSelectList) {
        const size_t maxOffset = *context.bluetoothDeviceCount > context.bluetoothListVisibleCount
                                     ? *context.bluetoothDeviceCount - context.bluetoothListVisibleCount
                                     : 0;

        if (context.isUpArrowCode(rawCode)) {
            if (context.bluetoothListScrollOffset != nullptr && *context.bluetoothListScrollOffset > 0) {
                *context.bluetoothListScrollOffset = *context.bluetoothListScrollOffset > context.bluetoothListVisibleCount
                                                        ? *context.bluetoothListScrollOffset - context.bluetoothListVisibleCount
                                                        : 0;
                state.settings.lastMessage = "bt scroll up";
                context.renderSettingsScreen(state, false, out);
            }
            return true;
        }

        if (context.isDownArrowCode(rawCode)) {
            if (context.bluetoothListScrollOffset != nullptr && *context.bluetoothListScrollOffset < maxOffset) {
                *context.bluetoothListScrollOffset = min(*context.bluetoothListScrollOffset + context.bluetoothListVisibleCount, maxOffset);
                state.settings.lastMessage = "bt scroll down";
                context.renderSettingsScreen(state, false, out);
            }
            return true;
        }

        if (normalizedKey >= '1' && normalizedKey <= '9') {
            const uint8_t pick = static_cast<uint8_t>(normalizedKey - '1');
            const size_t absoluteIndex =
                (context.bluetoothListScrollOffset != nullptr ? *context.bluetoothListScrollOffset : 0) + pick;
            if (absoluteIndex < *context.bluetoothDeviceCount) {
                if (context.selectBluetoothDevice != nullptr) {
                    context.selectBluetoothDevice(
                        state,
                        context.bluetoothDeviceList[absoluteIndex],
                        context.bluetoothDeviceAddressList[absoluteIndex],
                        static_cast<int8_t>(absoluteIndex),
                        out);
                } else {
                    state.settings.selectedBluetoothIndex = static_cast<int8_t>(absoluteIndex);
                    state.settings.btConnectedDeviceName = context.bluetoothDeviceList[absoluteIndex];
                    state.settings.btConnected = true;
                    state.settings.lastMessage = String("bt connected ") + state.settings.btConnectedDeviceName;
                }
                state.settings.viewMode = settingsViewBluetoothList;
                context.renderSettingsScreen(state, false, out);
                return true;
            }
        }
        return false;
    }

    if (state.settings.viewMode == settingsViewSdList) {
        if (normalizedKey == '1') {
            state.config.sdEnabled = !state.config.sdEnabled;
            state.settings.lastMessage = state.config.sdEnabled ? "sd enabled" : "sd disabled";
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == '2') {
            if (state.config.sdProbeSpeed <= 1000000UL) {
                state.config.sdProbeSpeed = 4000000UL;
            } else if (state.config.sdProbeSpeed <= 4000000UL) {
                state.config.sdProbeSpeed = 10000000UL;
            } else {
                state.config.sdProbeSpeed = 1000000UL;
            }
            state.settings.lastMessage = String("sd speed ") + state.config.sdProbeSpeed;
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        return false;
    }

    if (state.settings.viewMode == settingsViewWifiPassword) {
        // Password mode intentionally updates only OLED on typing keys.
        if (normalizedKey == '\n' || normalizedKey == '\r') {
            *context.czechComposeDeadKey = 0;
            // Enter commits connect action and returns to settings home.
            state.settings.lastMessage = "connecting...";
            context.renderSettingsScreen(state, true, out);
            context.connectSelectedWifi(state, out);
            state.settings.viewMode = settingsViewHome;
            context.renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey == 8 || normalizedKey == 127) {
            *context.czechComposeDeadKey = 0;
            if (!state.settings.wifiPassword.isEmpty()) {
                state.settings.wifiPassword.remove(state.settings.wifiPassword.length() - 1);
            }
            state.settings.lastMessage = "key: backspace";
            context.renderSettingsScreen(state, true, out);
            return true;
        }

        if (normalizedKey >= 32 && normalizedKey <= 126) {
            char composeKey = normalizedKey;
            if (composeKey == '/') {
                composeKey = '^';
            }

            if (composeKey == '^' || composeKey == '\'' || composeKey == '"') {
                if (context.tryApplyPostfixCzechCompose(state.settings.wifiPassword, composeKey)) {
                    state.settings.lastMessage = "compose postfix";
                    context.renderSettingsScreen(state, true, out);
                    return true;
                }
            }

            String typed;
            context.decodeCzechComposeKey(composeKey, typed);

            if (typed.isEmpty()) {
                state.settings.lastMessage = "compose...";
                context.renderSettingsScreen(state, true, out);
                return true;
            }

            if (state.settings.wifiPassword.length() + typed.length() < 63) {
                state.settings.wifiPassword += typed;
            }
            state.settings.lastMessage = String("key: ") + typed;
            context.renderSettingsScreen(state, true, out);
            return true;
        }

        // Keep unsupported key presses visible for debugging CardKB mapping.
        char rawHex[20];
        snprintf(rawHex, sizeof(rawHex), "key:0x%02X", static_cast<uint8_t>(key));
        state.settings.lastMessage = rawHex;
        context.renderSettingsScreen(state, true, out);
        return true;
    }

    if (normalizedKey >= '1' && normalizedKey <= '4') {
        const uint8_t optionIndex = static_cast<uint8_t>(normalizedKey - '1');
        if (context.applySettingsSelection(state, optionIndex, out)) {
            context.renderSettingsScreen(state, false, out);
            return true;
        }
        return false;
    }

    return false;
}
