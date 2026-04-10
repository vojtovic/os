#include "SettingsConnectivityService.h"

#include <BLEDevice.h>
#include <BLEClient.h>
#include <WiFi.h>

#include "ConfigStore.h"

namespace {
BLEClient *gBleClient = nullptr;
bool gBleStackInitialized = false;

bool isLikelyAudioHeadset(const String &deviceName) {
    String lower = deviceName;
    lower.toLowerCase();
    return lower.indexOf("airpods") >= 0 ||
           lower.indexOf("headset") >= 0 ||
           lower.indexOf("headphones") >= 0 ||
           lower.indexOf("buds") >= 0;
}

void ensureBleStackInitialized() {
    if (gBleStackInitialized) {
        return;
    }
    BLEDevice::init("mp3-pedia-os");
    gBleStackInitialized = true;
}

void disconnectBleClient() {
    if (gBleClient == nullptr) {
        return;
    }

    if (gBleClient->isConnected()) {
        gBleClient->disconnect();
    }

    delete gBleClient;
    gBleClient = nullptr;
}
}  // namespace

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
        state.config.wifiSsid = state.settings.selectedSsid;
        state.config.wifiPassword = state.settings.wifiPassword;
        const bool saved = saveConfig(state.config);
        state.settings.lastMessage = saved ? String("connected ") + state.settings.wifiIp : String("connected, save fail");
        out.print("WiFi connected: ");
        out.println(state.settings.wifiIp);
        if (!saved) {
            out.println("WiFi: credentials save failed");
        }
        return true;
    }

    state.settings.wifiConnected = false;
    state.settings.wifiIp = "";
    state.settings.lastMessage = "wifi connect fail";
    out.println("WiFi connect failed");
    return false;
}

bool settingsServiceForgetWifi(SystemState &state, Stream &out) {
    if (state.config.wifiSsid.isEmpty() && state.config.wifiPassword.isEmpty()) {
        state.settings.lastMessage = "no saved wifi";
        out.println("WiFi: no saved network");
        return true;
    }

    const String forgottenSsid = state.config.wifiSsid;
    const bool wasConnectedToForgotten =
        state.settings.wifiConnected &&
        !forgottenSsid.isEmpty() &&
        state.settings.wifiConnectedSsid == forgottenSsid;

    state.config.wifiSsid = "";
    state.config.wifiPassword = "";

    if (!saveConfig(state.config)) {
        state.settings.lastMessage = "forget save fail";
        out.println("WiFi: failed to persist forget action");
        return false;
    }

    if (wasConnectedToForgotten) {
        WiFi.disconnect(true, false);
        state.settings.wifiConnected = false;
        state.settings.wifiConnectedSsid = "";
        state.settings.wifiIp = "";
    }

    if (!state.settings.selectedSsid.isEmpty() && state.settings.selectedSsid == forgottenSsid) {
        state.settings.wifiPassword = "";
    }

    state.settings.lastMessage = forgottenSsid.isEmpty() ? "wifi forgotten" : String("forgot ") + forgottenSsid;
    out.print("WiFi: forgotten ");
    out.println(forgottenSsid);
    return true;
}

bool settingsServiceToggleBluetooth(SystemState &state, Stream &out) {
    if (state.settings.btEnabled) {
        disconnectBleClient();
        state.settings.btEnabled = false;
        state.settings.btConnected = false;
        state.settings.btConnectedDeviceName = "";
        state.settings.btConnectedDeviceAddress = "";
        state.settings.selectedBluetoothIndex = -1;
        state.settings.lastMessage = "bt disabled";
        out.println("Bluetooth: disabled");
        return true;
    }

    state.settings.btEnabled = true;
    ensureBleStackInitialized();
    state.settings.lastMessage = "bt enabled";
    out.println("Bluetooth: enabled");
    return true;
}

bool settingsServiceDisconnectBluetooth(SystemState &state, Stream &out) {
    if (!state.settings.btConnected && state.settings.selectedBluetoothIndex < 0 && state.settings.btConnectedDeviceName.isEmpty()) {
        state.settings.lastMessage = "bt already idle";
        out.println("Bluetooth: already idle");
        return true;
    }

    disconnectBleClient();
    state.settings.btConnected = false;
    state.settings.btConnectedDeviceName = "";
    state.settings.btConnectedDeviceAddress = "";
    state.settings.selectedBluetoothIndex = -1;
    state.settings.lastMessage = "bt disconnected";
    out.println("Bluetooth: disconnected");
    return true;
}

bool settingsServiceSelectBluetoothDevice(SystemState &state, const String &deviceName, const String &deviceAddress, int8_t selectedIndex, Stream &out) {
    if (deviceName.isEmpty() || deviceAddress.isEmpty()) {
        state.settings.lastMessage = "bt empty device";
        out.println("Bluetooth: selected device is invalid");
        return false;
    }

    if (!state.settings.btEnabled) {
        state.settings.lastMessage = "enable bt first";
        out.println("Bluetooth: enable first");
        return false;
    }

    // Current settings flow is BLE/GATT only and cannot act as an A2DP audio source.
    if (isLikelyAudioHeadset(deviceName)) {
        state.settings.lastMessage = "needs BT classic/A2DP";
        out.println("Bluetooth: audio headsets (e.g. AirPods) are not supported in BLE mode");
        return false;
    }

    ensureBleStackInitialized();
    disconnectBleClient();

    BLEAddress address(deviceAddress.c_str());
    gBleClient = BLEDevice::createClient();
    if (gBleClient == nullptr) {
        state.settings.lastMessage = "bt client fail";
        out.println("Bluetooth: client create failed");
        return false;
    }

    out.print("Bluetooth: connecting to ");
    out.println(deviceAddress);
    if (!gBleClient->connect(address)) {
        delete gBleClient;
        gBleClient = nullptr;
        state.settings.btConnected = false;
        state.settings.btConnectedDeviceName = "";
        state.settings.btConnectedDeviceAddress = "";
        state.settings.selectedBluetoothIndex = -1;
        state.settings.lastMessage = "bt connect fail";
        out.println("Bluetooth: connect failed");
        return false;
    }

    state.settings.btConnected = true;
    state.settings.btConnectedDeviceName = deviceName;
    state.settings.btConnectedDeviceAddress = deviceAddress;
    state.settings.selectedBluetoothIndex = selectedIndex;
    state.config.btPreferredDevice = deviceName;
    state.config.btPreferredAddress = deviceAddress;

    const bool saved = saveConfig(state.config);
    state.settings.lastMessage = saved ? String("bt connected ") + deviceName : String("bt connected, save fail");
    out.print("Bluetooth: selected ");
    out.println(deviceName);
    if (!saved) {
        out.println("Bluetooth: preferred device save failed");
    }
    return true;
}

bool settingsServiceForgetBluetoothDevice(SystemState &state, Stream &out) {
    if (state.config.btPreferredDevice.isEmpty()) {
        state.settings.lastMessage = "no saved bt";
        out.println("Bluetooth: no saved device");
        return true;
    }

    const String forgotten = state.config.btPreferredDevice;
    const String forgottenAddress = state.config.btPreferredAddress;
    state.config.btPreferredDevice = "";
    state.config.btPreferredAddress = "";

    if (!saveConfig(state.config)) {
        state.settings.lastMessage = "bt forget save fail";
        out.println("Bluetooth: failed to persist forget action");
        return false;
    }

    if (state.settings.btConnected &&
        (state.settings.btConnectedDeviceName == forgotten ||
         (!forgottenAddress.isEmpty() && state.settings.btConnectedDeviceAddress == forgottenAddress))) {
        disconnectBleClient();
        state.settings.btConnected = false;
        state.settings.btConnectedDeviceName = "";
        state.settings.btConnectedDeviceAddress = "";
        state.settings.selectedBluetoothIndex = -1;
    }

    state.settings.lastMessage = String("bt forgot ") + forgotten;
    out.print("Bluetooth: forgotten ");
    out.println(forgotten);
    return true;
}

size_t settingsServiceScanBluetoothDevices(
    String deviceList[],
    String addressList[],
    size_t maxDevices,
    bool &bleInitialized,
    Stream &out) {
    size_t bluetoothDeviceCount = 0;
    out.println("Bluetooth: scanning (BLE)");

    if (!bleInitialized) {
        ensureBleStackInitialized();
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
        const std::string addr = device.getAddress().toString();
        const String address = String(addr.c_str());

        std::string name = device.getName();
        if (!name.empty()) {
            label = String(name.c_str());
        } else {
            label = String("BLE ") + address;
        }

        bool duplicate = false;
        for (size_t j = 0; j < bluetoothDeviceCount; ++j) {
            if (addressList[j] == address) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            deviceList[bluetoothDeviceCount] = label;
            addressList[bluetoothDeviceCount] = address;
            ++bluetoothDeviceCount;
        }
    }

    scan->clearResults();

    out.print("Bluetooth: found ");
    out.println(bluetoothDeviceCount);
    return bluetoothDeviceCount;
}
