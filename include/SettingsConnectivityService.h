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
bool settingsServiceForgetWifi(SystemState &state, Stream &out);

bool settingsServiceToggleBluetooth(SystemState &state, Stream &out);
bool settingsServiceDisconnectBluetooth(SystemState &state, Stream &out);
bool settingsServiceSelectBluetoothDevice(SystemState &state, const String &deviceName, const String &deviceAddress, int8_t selectedIndex, Stream &out);
bool settingsServiceForgetBluetoothDevice(SystemState &state, Stream &out);
size_t settingsServiceScanBluetoothDevices(
    String deviceList[],
    String addressList[],
    size_t maxDevices,
    bool &bleInitialized,
    Stream &out);
