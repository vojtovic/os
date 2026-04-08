#pragma once

#include <Arduino.h>

#include "SystemState.h"

size_t settingsServiceScanWifiNetworks(
    String ssidList[],
    String bssidList[],
    int32_t rssiList[],
    size_t maxWifiNetworks,
    size_t &scrollOffset,
    Stream &out);

bool settingsServiceToggleWifi(SystemState &state, Stream &out);
bool settingsServiceConnectSelectedWifi(SystemState &state, Stream &out);

bool settingsServiceToggleBluetooth(SystemState &state, Stream &out);
size_t settingsServiceScanBluetoothDevices(
    String deviceList[],
    size_t maxDevices,
    bool &bleInitialized,
    Stream &out);
