#include "SettingsConnectivityService.h"

#include <BLEDevice.h>
#include <WiFi.h>

size_t settingsServiceScanWifiNetworks(
    String ssidList[],
    String bssidList[],
    int32_t rssiList[],
    size_t maxWifiNetworks,
    size_t &scrollOffset,
    Stream &out) {
    // Blocking scan is acceptable here because it runs in explicit user flow.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(50);

    out.println("WiFi: scanning...");
    const int found = WiFi.scanNetworks(false, true);
    size_t wifiCount = 0;
    if (found <= 0) {
        out.println("WiFi: no networks");
        return 0;
    }

    for (int i = 0; i < found && wifiCount < maxWifiNetworks; ++i) {
        ssidList[wifiCount] = WiFi.SSID(i);
        bssidList[wifiCount] = WiFi.BSSIDstr(i);
        rssiList[wifiCount] = WiFi.RSSI(i);
        ++wifiCount;
    }

    scrollOffset = 0;
    out.print("WiFi: found ");
    out.println(wifiCount);
    return wifiCount;
}

bool settingsServiceToggleWifi(SystemState &state, Stream &out) {
    if (state.settings.wifiEnabled) {
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        state.settings.wifiEnabled = false;
        state.settings.wifiConnected = false;
        state.settings.wifiConnectedSsid = "";
        state.settings.wifiIp = "";
        state.settings.lastMessage = "wifi disabled";
        out.println("WiFi: disabled");
        return true;
    }

    WiFi.mode(WIFI_STA);
    state.settings.wifiEnabled = true;
    state.settings.wifiConnected = (WiFi.status() == WL_CONNECTED);
    if (state.settings.wifiConnected) {
        state.settings.wifiConnectedSsid = WiFi.SSID();
        state.settings.wifiIp = WiFi.localIP().toString();
    }
    state.settings.lastMessage = "wifi enabled";
    out.println("WiFi: enabled");
    return true;
}

bool settingsServiceConnectSelectedWifi(SystemState &state, Stream &out) {
    if (state.settings.selectedSsid.isEmpty()) {
        state.settings.lastMessage = "no ssid";
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(state.settings.selectedSsid.c_str(), state.settings.wifiPassword.c_str());

    const uint32_t startMs = millis();
    // Simple connection timeout to avoid freezing settings flow.
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 15000UL) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        state.settings.wifiEnabled = true;
        state.settings.wifiConnected = true;
        state.settings.wifiConnectedSsid = state.settings.selectedSsid;
        state.settings.wifiIp = WiFi.localIP().toString();
        state.settings.lastMessage = String("connected ") + state.settings.wifiIp;
        out.print("WiFi connected: ");
        out.println(state.settings.wifiIp);
        return true;
    }

    state.settings.wifiConnected = false;
    state.settings.wifiIp = "";
    state.settings.lastMessage = "wifi connect fail";
    out.println("WiFi connect failed");
    return false;
}

bool settingsServiceToggleBluetooth(SystemState &state, Stream &out) {
    if (state.settings.btEnabled) {
        state.settings.btEnabled = false;
        state.settings.btConnected = false;
        state.settings.btConnectedDeviceName = "";
        state.settings.selectedBluetoothIndex = -1;
        state.settings.lastMessage = "bt disabled";
        out.println("Bluetooth: disabled");
        return true;
    }

    state.settings.btEnabled = true;
    state.settings.lastMessage = "bt enabled";
    out.println("Bluetooth: enabled");
    return true;
}

size_t settingsServiceScanBluetoothDevices(
    String deviceList[],
    size_t maxDevices,
    bool &bleInitialized,
    Stream &out) {
    size_t bluetoothDeviceCount = 0;
    out.println("Bluetooth: scanning (BLE)");

    if (!bleInitialized) {
        BLEDevice::init("mp3-pedia-os");
        bleInitialized = true;
    }

    BLEScan *scan = BLEDevice::getScan();
    if (scan == nullptr) {
        out.println("Bluetooth: scan init failed");
        return 0;
    }

    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);

    BLEScanResults results = scan->start(4, false);
    const int found = results.getCount();

    for (int i = 0; i < found && bluetoothDeviceCount < maxDevices; ++i) {
        BLEAdvertisedDevice device = results.getDevice(i);
        String label = "";

        std::string name = device.getName();
        if (!name.empty()) {
            label = String(name.c_str());
        } else {
            std::string addr = device.getAddress().toString();
            label = String("BLE ") + String(addr.c_str());
        }

        bool duplicate = false;
        for (size_t j = 0; j < bluetoothDeviceCount; ++j) {
            if (deviceList[j] == label) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            deviceList[bluetoothDeviceCount++] = label;
        }
    }

    scan->clearResults();

    out.print("Bluetooth: found ");
    out.println(bluetoothDeviceCount);
    return bluetoothDeviceCount;
}
