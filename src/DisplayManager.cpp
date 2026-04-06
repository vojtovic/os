#include "DisplayManager.h"

#include <U8g2lib.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <freertos/task.h>

#include "ConfigStore.h"
#include "EPD_3in52.h"
#include "epdpaint.h"
#include "StorageManager.h"

namespace {
// Static launcher metadata rendered on both displays.
struct LauncherApp {
    const char *id;
    const char *title;
    const char *description;
};

constexpr uint8_t kOledClk = 12;
constexpr uint8_t kOledMosi = 11;
constexpr uint8_t kOledRes = 47;
constexpr uint8_t kOledDc = 21;
constexpr uint8_t kOledCs = 10;

constexpr LauncherApp kLauncherApps[] = {
    {"settings", "Settings", "Built-in config app"},
    {"apps", "SD Apps", "Loaded from manifest"},
    {"serial", "Serial", "Debug shell"},
    {"about", "About", "System info"},
};

constexpr size_t kLauncherAppCount = sizeof(kLauncherApps) / sizeof(kLauncherApps[0]);

constexpr int kColored = 0;
constexpr int kUncolored = 1;
constexpr int kEinkNativeWidth = EPD_WIDTH;
constexpr int kEinkNativeHeight = EPD_HEIGHT;
constexpr int kEinkLandscapeWidth = EPD_HEIGHT;
constexpr int kEinkLandscapeHeight = EPD_WIDTH;
constexpr uint32_t kEinkIdleTimeoutMs = 60000;

U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI gOled(
    U8G2_R0,
    kOledCs,
    kOledDc,
    kOledRes);

Epd gEink;
UBYTE gEinkBuffer[10800];
// Counts all e-ink updates (diagnostics/telemetry).
uint32_t gEinkUpdateCounter = 0;
// Counts DU (fast/partial) refreshes between GC (full) refreshes.
uint32_t gEinkPartialRefreshCounter = 0;
bool gEinkInitAttempted = false;
constexpr size_t kMaxSdApps = 8;
String gSdApps[kMaxSdApps];
size_t gSdAppCount = 0;
uint32_t gLastDisplayActivityMs = 0;
bool gEinkSleeping = false;
bool gOledSleeping = false;
bool gOledHeldInReset = false;
String gLauncherKeyMessage = "press 1 for settings";

constexpr const char *kSettingsOptions[] = {
    "1) WiFi manager",
    "2) Bluetooth manager",
    "3) SD enable toggle",
    "4) SD speed cycle",
    "5) Save config",
};

constexpr uint8_t kSettingsOptionCount = sizeof(kSettingsOptions) / sizeof(kSettingsOptions[0]);
constexpr uint8_t kSettingsViewHome = 0;
constexpr uint8_t kSettingsViewWifiList = 1;
constexpr uint8_t kSettingsViewWifiPassword = 2;
constexpr uint8_t kSettingsViewWifiSelectList = 3;
constexpr uint8_t kSettingsViewBluetoothList = 4;
constexpr uint8_t kSettingsViewBluetoothSelectList = 5;
constexpr size_t kMaxWifiNetworks = 9;
String gWifiSsidList[kMaxWifiNetworks];
int32_t gWifiRssiList[kMaxWifiNetworks];
size_t gWifiCount = 0;
// True while synchronous Wi-Fi scan is running.
bool gWifiScanInProgress = false;
constexpr size_t kMaxBluetoothDevices = 9;
String gBluetoothDeviceList[kMaxBluetoothDevices];
size_t gBluetoothDeviceCount = 0;
bool gBluetoothScanInProgress = false;
bool gBluetoothBleInitialized = false;
char gCzechComposeDeadKey = 0;

bool ensureEinkInitialized(SystemState &state, Stream &out);
void markDisplayActivity();
bool tryWakeEinkPanel(SystemState &state, Stream &out);
void forceOledSleepOff(SystemState &state);
void forceOledWakeOn(SystemState &state);
void drawSettingsOled(const SystemState &state);
String transliterateCzechToAscii(const String &input);

void forceOledSleepOff(SystemState &state) {
    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Try graceful blank first.
        gOled.clearBuffer();
        gOled.sendBuffer();
        vTaskDelay(pdMS_TO_TICKS(20));

        // Explicit SH1106 off sequence; some modules ignore generic power-save.
        gOled.sendF("c", 0xAE);
        gOled.sendF("c", 0xA4);
        gOled.setContrast(0);
        gOled.setPowerSave(1);

        // Hold controller in reset so pixels are guaranteed off.
        pinMode(kOledRes, OUTPUT);
        digitalWrite(kOledRes, LOW);
        gOledHeldInReset = true;
        xSemaphoreGive(state.spiMutex);
    }
}

void forceOledWakeOn(SystemState &state) {
    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Always issue a clean reset pulse before re-init.
        pinMode(kOledRes, OUTPUT);
        digitalWrite(kOledRes, LOW);
        vTaskDelay(pdMS_TO_TICKS(2));
        digitalWrite(kOledRes, HIGH);
        vTaskDelay(pdMS_TO_TICKS(5));

        if (gOledHeldInReset) {
            gOledHeldInReset = false;
        }

        // Re-init after reset hold to restore controller state deterministically.
        gOled.begin();
        gOled.enableUTF8Print();
        gOled.setPowerSave(0);
        gOled.sendF("c", 0xAF);
        gOled.setContrast(255);

        // Push a known blank frame before the next app redraw.
        gOled.clearBuffer();
        gOled.sendBuffer();
        xSemaphoreGive(state.spiMutex);
    }
}

bool tryWakeEinkPanel(SystemState &state, Stream &out) {
    gEink.Reset();
    vTaskDelay(pdMS_TO_TICKS(20));
    if (gEink.Init() == 0) {
        gEinkSleeping = false;
        gEinkPartialRefreshCounter = 0;
        markDisplayActivity();
        out.println("E-ink: awake");
        return true;
    }

    out.println("E-ink: retry wake...");
    gEink.Reset();
    vTaskDelay(pdMS_TO_TICKS(120));
    if (gEink.Init() == 0) {
        gEinkSleeping = false;
        gEinkPartialRefreshCounter = 0;
        markDisplayActivity();
        out.println("E-ink: awake after retry");
        return true;
    }

    state.einkReady = false;
    gEinkSleeping = false;
    gEinkInitAttempted = false;
    out.println("E-ink: wake failed");
    return false;
}

void markDisplayActivity() {
    // Shared activity marker for idle timeout logic.
    gLastDisplayActivityMs = millis();
}

void drawBootScreen(const String &deviceName) {
    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x10_tf);
    gOled.drawStr(0, 12, "os boot");
    gOled.drawStr(0, 28, "device:");
    gOled.drawStr(48, 28, deviceName.c_str());
    gOled.drawStr(0, 44, "OLED ready");
    gOled.sendBuffer();
}

const LauncherApp &launcherAppAt(uint8_t index) {
    return kLauncherApps[index % kLauncherAppCount];
}

void drawLauncherOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    const LauncherApp &selected = launcherAppAt(state.launcher.selectedIndex);
    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x10_tf);
    gOled.drawStr(0, 11, "app launcher");
    gOled.drawStr(0, 24, state.config.deviceName.c_str());

    gOled.setCursor(0, 38);
    gOled.print("active: ");
    gOled.print(state.launcher.activeAppId);

    gOled.setCursor(0, 50);
    gOled.print("> ");
    gOled.print(selected.title);

    gOled.setCursor(0, 62);
    gOled.print(gLauncherKeyMessage);
    gOled.sendBuffer();
}

void drawLauncherCard(Paint &paint, int x, int y, int width, int height, const LauncherApp &app, bool selected) {
    paint.DrawRectangle(x, y, x + width, y + height, kColored);
    if (selected) {
        paint.DrawRectangle(x + 1, y + 1, x + width - 1, y + height - 1, kColored);
    }

    paint.DrawStringAt(x + 6, y + 8, app.title, &Font16, kColored);
    paint.DrawStringAt(x + 6, y + 30, app.description, &Font12, kColored);
    paint.DrawStringAt(x + 6, y + height - 18, app.id, &Font8, kColored);
}

void drawLauncherPreview(const SystemState &state, Stream &out) {
    gSdAppCount = loadSdAppManifest(state, gSdApps, kMaxSdApps, out);

    out.println("--- Launcher ---");
    out.print("active app: ");
    out.println(state.launcher.activeAppId);
    out.print("selected: ");
    out.println(launcherAppAt(state.launcher.selectedIndex).title);

    for (size_t index = 0; index < kLauncherAppCount; ++index) {
        out.print(index == state.launcher.selectedIndex ? "* " : "  ");
        out.print(kLauncherApps[index].id);
        out.print(" - ");
        out.println(kLauncherApps[index].description);
    }

    out.println("--- SD Apps ---");
    if (gSdAppCount == 0) {
        out.println("(none)");
        return;
    }

    for (size_t i = 0; i < gSdAppCount; ++i) {
        out.print(i + 1);
        out.print(". ");
        out.println(gSdApps[i]);
    }
}

void drawActiveAppOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        drawLauncherOled(state);
        return;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        drawSettingsOled(state);
        return;
    }

    String title = String("app: ") + state.launcher.activeAppId;
    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x10_tf);
    gOled.drawStr(0, 14, "not implemented yet");
    gOled.drawStr(0, 30, title.c_str());
    gOled.drawStr(0, 46, "open settings first");
    gOled.drawStr(0, 62, "launcher show");
    gOled.sendBuffer();
}

void prepareLandscapePaint(Paint &paint) {
    paint.SetRotate(ROTATE_90);
}

void drawSettingsOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x12_te);
    gOled.drawStr(0, 11, "SETTINGS");

    if (state.settings.viewMode == kSettingsViewHome) {
        // Home view: shortcut legend + compact status line.
        char speedLine[26];
        snprintf(speedLine, sizeof(speedLine), "sd hz:%lu", state.config.sdProbeSpeed);
        String wifiLine = String("wifi:") + (state.settings.wifiEnabled ? "on" : "off");
        if (state.settings.wifiConnected && !state.settings.wifiConnectedSsid.isEmpty()) {
            wifiLine += " ";
            wifiLine += state.settings.wifiConnectedSsid;
        }
        String btLine = String("bt:") + (state.settings.btEnabled ? "on" : "off");
        if (state.settings.btConnected && !state.settings.btConnectedDeviceName.isEmpty()) {
            btLine += " ";
            btLine += state.settings.btConnectedDeviceName;
        }
        gOled.drawStr(0, 22, "1 wifi 2 bt 3 sd");
        gOled.drawStr(0, 32, "4 speed 5 save");
        gOled.setCursor(0, 42);
        gOled.print(wifiLine);
        gOled.setCursor(0, 52);
        gOled.print(btLine);
        gOled.setCursor(64, 52);
        gOled.print(speedLine);
        gOled.setCursor(0, 62);
        gOled.print("last:");
        gOled.print(state.settings.lastMessage);
    } else if (state.settings.viewMode == kSettingsViewWifiList) {
        gOled.drawStr(0, 22, "wifi manager");
        gOled.drawStr(0, 33, "1 toggle wifi");
        gOled.drawStr(0, 44, "2 connect wifi");
        gOled.drawStr(0, 55, "<- back");
        gOled.setCursor(0, 64);
        gOled.print(state.settings.wifiEnabled ? "wifi:on" : "wifi:off");
        if (state.settings.wifiConnected && !state.settings.wifiConnectedSsid.isEmpty()) {
            gOled.print(" ");
            gOled.print(state.settings.wifiConnectedSsid);
        }
    } else if (state.settings.viewMode == kSettingsViewWifiSelectList) {
        if (gWifiScanInProgress) {
            gOled.drawStr(0, 22, "wifi scanning...");
            gOled.drawStr(0, 33, "please wait");
            gOled.drawStr(0, 44, "loading list");
        } else {
            gOled.drawStr(0, 22, "wifi list on e-ink");
            gOled.drawStr(0, 33, "type 1..9 to select");
            gOled.drawStr(0, 44, "<- back to wifi");
        }
        gOled.setCursor(0, 56);
        gOled.print("found: ");
        gOled.print(gWifiCount);
        gOled.print("  last: ");
        gOled.print(state.settings.lastMessage);
    } else if (state.settings.viewMode == kSettingsViewBluetoothList) {
        if (gBluetoothScanInProgress) {
            gOled.drawStr(0, 22, "bluetooth scanning...");
            gOled.drawStr(0, 33, "please wait");
        } else {
            gOled.drawStr(0, 22, "bluetooth manager");
            gOled.drawStr(0, 33, "1 toggle bt");
            gOled.drawStr(0, 44, "2 scan devices");
            gOled.drawStr(0, 55, "<- back");
        }
        gOled.setCursor(0, 64);
        gOled.print(state.settings.btEnabled ? "bt:on" : "bt:off");
        if (state.settings.btConnected && !state.settings.btConnectedDeviceName.isEmpty()) {
            gOled.print(" ");
            gOled.print(state.settings.btConnectedDeviceName);
        }
    } else if (state.settings.viewMode == kSettingsViewBluetoothSelectList) {
        gOled.drawStr(0, 22, "bt list on e-ink");
        gOled.drawStr(0, 33, "type 1..9 to select");
        gOled.drawStr(0, 44, "<- back to bt");
        gOled.setCursor(0, 56);
        gOled.print("found: ");
        gOled.print(gBluetoothDeviceCount);
        gOled.print("  last: ");
        gOled.print(state.settings.lastMessage);
    } else {
        // Password entry view. We keep OLED as the live typing surface.
        String passwordPreview = state.settings.wifiPassword;
        if (passwordPreview.length() > 18) {
            // Keep the tail of the password visible on a 128px-wide OLED.
            passwordPreview = String("...") + passwordPreview.substring(passwordPreview.length() - 18);
        }

        gOled.drawStr(0, 22, "wifi password");
        gOled.setCursor(0, 33);
        gOled.print("ssid: ");
        gOled.print(state.settings.selectedSsid);
        gOled.setCursor(0, 44);
        gOled.print("pass: ");
        gOled.print(passwordPreview);
        gOled.print('_');
        gOled.setCursor(0, 56);
        gOled.print(state.settings.lastMessage);
        gOled.drawStr(0, 64, "enter=connect  backsp/0");
    }
    gOled.sendBuffer();
}

void drawSettingsEink(SystemState &state) {
    // E-ink is the context/overview display, not the live typing surface.
    Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
    prepareLandscapePaint(paint);
    paint.Clear(kUncolored);
    paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
    paint.DrawStringAt(10, 10, "SETTINGS", &Font16, kColored);
    paint.DrawLine(0, 32, kEinkLandscapeWidth - 1, 32, kColored);

    if (state.settings.viewMode == kSettingsViewHome) {
        // Home view: current configuration + action list.
        String line0 = String("device: ") + state.config.deviceName;
        String line1 = String("wifi: ") + (state.settings.wifiEnabled ? "on" : "off");
        if (state.settings.wifiConnected && !state.settings.wifiConnectedSsid.isEmpty()) {
            line1 += " (";
            line1 += state.settings.wifiConnectedSsid;
            line1 += ")";
        }
        String line2 = String("bt: ") + (state.settings.btEnabled ? "on" : "off");
        if (state.settings.btConnected && !state.settings.btConnectedDeviceName.isEmpty()) {
            line2 += " (";
            line2 += state.settings.btConnectedDeviceName;
            line2 += ")";
        }
        String line3 = String("sd: ") + (state.config.sdEnabled ? "enabled" : "disabled");
        String line4 = String("sd speed: ") + String(state.config.sdProbeSpeed) + " hz";

        paint.DrawStringAtUtf8(10, 42, line0.c_str(), &Font12, kColored);
        paint.DrawStringAtUtf8(10, 58, line1.c_str(), &Font12, kColored);
        paint.DrawStringAtUtf8(10, 74, line2.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 90, line3.c_str(), &Font12, kColored);
        paint.DrawStringAt(170, 90, line4.c_str(), &Font12, kColored);

        int y = 114;
        for (uint8_t i = 0; i < kSettingsOptionCount; ++i) {
            paint.DrawStringAt(10, y, kSettingsOptions[i], &Font12, kColored);
            y += 16;
        }

        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Input on CardKB: 1..4  (0 = launcher)", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewWifiList) {
        const String wifiState = String("wifi: ") + (state.settings.wifiEnabled ? "on" : "off");
        paint.DrawStringAt(10, 42, "WiFi manager", &Font16, kColored);
        paint.DrawStringAt(10, 66, wifiState.c_str(), &Font12, kColored);
        if (state.settings.wifiConnected && !state.settings.wifiConnectedSsid.isEmpty()) {
            String ssidLine = String("connected: ") + state.settings.wifiConnectedSsid;
            paint.DrawStringAtUtf8(10, 82, ssidLine.c_str(), &Font12, kColored);
        }
        paint.DrawStringAt(10, 110, "1) Toggle WiFi ON/OFF", &Font12, kColored);
        paint.DrawStringAt(10, 126, "2) Connect to WiFi", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Left arrow = back", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewWifiSelectList) {
        paint.DrawStringAt(10, 42, "Available WiFi:", &Font12, kColored);
        if (gWifiScanInProgress) {
            paint.DrawStringAt(10, 62, "Scanning networks...", &Font12, kColored);
            paint.DrawStringAt(10, 80, "Please wait", &Font12, kColored);
        } else {
            int y = 58;
            for (size_t i = 0; i < gWifiCount && i < kMaxWifiNetworks; ++i) {
                String line = String(static_cast<unsigned>(i + 1));
                line += ") ";
                line += gWifiSsidList[i];
                line += " (";
                line += String(static_cast<long>(gWifiRssiList[i]));
                line += ")";
                paint.DrawStringAtUtf8(10, y, line.c_str(), &Font12, kColored);
                y += 16;
            }

            if (gWifiCount == 0) {
                paint.DrawStringAt(10, 76, "No networks found", &Font12, kColored);
            }
        }

        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Type 1..9 to select, left back", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewBluetoothList) {
        const String btState = String("bluetooth: ") + (state.settings.btEnabled ? "on" : "off");
        paint.DrawStringAt(10, 42, "Bluetooth manager", &Font16, kColored);
        paint.DrawStringAt(10, 66, btState.c_str(), &Font12, kColored);
        if (state.settings.btConnected && !state.settings.btConnectedDeviceName.isEmpty()) {
            String deviceLine = String("connected: ") + state.settings.btConnectedDeviceName;
            paint.DrawStringAtUtf8(10, 82, deviceLine.c_str(), &Font12, kColored);
        }
        paint.DrawStringAt(10, 110, "1) Toggle Bluetooth ON/OFF", &Font12, kColored);
        paint.DrawStringAt(10, 126, "2) Scan devices", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Left arrow = back", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewBluetoothSelectList) {
        paint.DrawStringAt(10, 42, "Available Bluetooth:", &Font12, kColored);
        int y = 58;
        for (size_t i = 0; i < gBluetoothDeviceCount && i < kMaxBluetoothDevices; ++i) {
            String line = String(static_cast<unsigned>(i + 1));
            line += ") ";
            line += gBluetoothDeviceList[i];
            paint.DrawStringAtUtf8(10, y, line.c_str(), &Font12, kColored);
            y += 16;
        }
        if (gBluetoothDeviceCount == 0) {
            paint.DrawStringAt(10, 76, "No devices found", &Font12, kColored);
        }
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Type 1..9 to select, left back", &Font12, kColored);
    } else {
        // Password view: e-ink only shows context, typing remains on OLED.
        const String selectedSsidUtf8 = state.settings.selectedSsid;
        paint.DrawStringAt(10, 42, "Connect to:", &Font12, kColored);
        paint.DrawStringAtUtf8(98, 42, selectedSsidUtf8.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 60, "Password on OLED keyboard", &Font12, kColored);
        paint.DrawStringAt(10, 78, "Press ENTER to connect", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "0 cancel", &Font12, kColored);
    }

    gEink.display(paint.GetImage());
    vTaskDelay(pdMS_TO_TICKS(1));
    gEink.lut_GC();
    vTaskDelay(pdMS_TO_TICKS(1));
    gEink.refresh();
    vTaskDelay(pdMS_TO_TICKS(5));
}


size_t scanWifiNetworks(Stream &out) {
    // Blocking scan is acceptable here because it runs in explicit user flow.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(50);

    out.println("WiFi: scanning...");
    const int found = WiFi.scanNetworks(false, true);
    gWifiCount = 0;
    if (found <= 0) {
        out.println("WiFi: no networks");
        return 0;
    }

    for (int i = 0; i < found && gWifiCount < kMaxWifiNetworks; ++i) {
        gWifiSsidList[gWifiCount] = WiFi.SSID(i);
        gWifiRssiList[gWifiCount] = WiFi.RSSI(i);
        ++gWifiCount;
    }
    out.print("WiFi: found ");
    out.println(gWifiCount);
    return gWifiCount;
}

bool toggleWifiEnabled(SystemState &state, Stream &out) {
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

bool connectSelectedWifi(SystemState &state, Stream &out) {
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

bool toggleBluetoothEnabled(SystemState &state, Stream &out) {
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

size_t scanBluetoothDevices(Stream &out) {
    gBluetoothDeviceCount = 0;
    out.println("Bluetooth: scanning (BLE)");

    if (!gBluetoothBleInitialized) {
        BLEDevice::init("mp3-pedia-os");
        gBluetoothBleInitialized = true;
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

    for (int i = 0; i < found && gBluetoothDeviceCount < kMaxBluetoothDevices; ++i) {
        BLEAdvertisedDevice device = results.getDevice(i);
        String label = "";

        std::string name = device.getName();
        if (!name.empty()) {
            label = String(name.c_str());
        } else {
            std::string addr = device.getAddress().toString();
            label = String("BLE ") + String(addr.c_str());
        }

        // Avoid duplicates in the short on-screen list.
        bool duplicate = false;
        for (size_t j = 0; j < gBluetoothDeviceCount; ++j) {
            if (gBluetoothDeviceList[j] == label) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            gBluetoothDeviceList[gBluetoothDeviceCount++] = label;
        }
    }

    scan->clearResults();

    out.print("Bluetooth: found ");
    out.println(gBluetoothDeviceCount);
    return gBluetoothDeviceCount;
}

void cycleSdSpeed(AppConfig &config) {
    // Cycle through known-safe SD probe rates.
    if (config.sdProbeSpeed <= 1000000UL) {
        config.sdProbeSpeed = 4000000UL;
        return;
    }
    if (config.sdProbeSpeed <= 4000000UL) {
        config.sdProbeSpeed = 10000000UL;
        return;
    }
    config.sdProbeSpeed = 1000000UL;
}

char normalizeInputKey(char key) {
    // CardKB can send digit codes in multiple ranges; normalize to ASCII.
    if (key >= '0' && key <= '9') {
        return key;
    }

    const uint8_t code = static_cast<uint8_t>(key);
    if (code >= 0xB0 && code <= 0xB9) {
        return static_cast<char>('0' + (code - 0xB0));
    }
    if (code >= 0x90 && code <= 0x99) {
        return static_cast<char>('0' + (code - 0x90));
    }

    return key;
}

String transliterateCzechToAscii(const String &input) {
    String out;
    out.reserve(input.length());

    for (size_t i = 0; i < input.length(); ++i) {
        const uint8_t b0 = static_cast<uint8_t>(input[i]);
        if (b0 == 0xC3 && (i + 1) < input.length()) {
            const uint8_t b1 = static_cast<uint8_t>(input[i + 1]);
            if (b1 == 0xA1) { out += 'a'; ++i; continue; }
            if (b1 == 0xA9) { out += 'e'; ++i; continue; }
            if (b1 == 0xAD) { out += 'i'; ++i; continue; }
            if (b1 == 0xB3) { out += 'o'; ++i; continue; }
            if (b1 == 0xBA) { out += 'u'; ++i; continue; }
            if (b1 == 0xBD) { out += 'y'; ++i; continue; }
            if (b1 == 0x81) { out += 'A'; ++i; continue; }
            if (b1 == 0x89) { out += 'E'; ++i; continue; }
            if (b1 == 0x8D) { out += 'I'; ++i; continue; }
            if (b1 == 0x93) { out += 'O'; ++i; continue; }
            if (b1 == 0x9A) { out += 'U'; ++i; continue; }
            if (b1 == 0x9D) { out += 'Y'; ++i; continue; }
        }

        if (b0 == 0xC4 && (i + 1) < input.length()) {
            const uint8_t b1 = static_cast<uint8_t>(input[i + 1]);
            if (b1 == 0x8D) { out += 'c'; ++i; continue; }
            if (b1 == 0x87) { out += 'C'; ++i; continue; }
            if (b1 == 0x8F) { out += 'd'; ++i; continue; }
            if (b1 == 0x8E) { out += 'D'; ++i; continue; }
            if (b1 == 0x9B) { out += 'e'; ++i; continue; }
            if (b1 == 0x9A) { out += 'E'; ++i; continue; }
            if (b1 == 0xA5) { out += 't'; ++i; continue; }
            if (b1 == 0xA4) { out += 'T'; ++i; continue; }
        }

        if (b0 == 0xC5 && (i + 1) < input.length()) {
            const uint8_t b1 = static_cast<uint8_t>(input[i + 1]);
            if (b1 == 0x88) { out += 'n'; ++i; continue; }
            if (b1 == 0x87) { out += 'N'; ++i; continue; }
            if (b1 == 0x99) { out += 'r'; ++i; continue; }
            if (b1 == 0x98) { out += 'R'; ++i; continue; }
            if (b1 == 0xA1) { out += 's'; ++i; continue; }
            if (b1 == 0xA0) { out += 'S'; ++i; continue; }
            if (b1 == 0xAF) { out += 'u'; ++i; continue; }
            if (b1 == 0xAE) { out += 'U'; ++i; continue; }
            if (b1 == 0xBE) { out += 'z'; ++i; continue; }
            if (b1 == 0xBD) { out += 'Z'; ++i; continue; }
        }

        out += static_cast<char>(b0);
    }
    return out;
}

String mapCzechComposedChar(char deadKey, char base) {
    if (deadKey == '^') {
        if (base == 'c') return "č";
        if (base == 'C') return "Č";
        if (base == 'd') return "ď";
        if (base == 'D') return "Ď";
        if (base == 'e') return "ě";
        if (base == 'E') return "Ě";
        if (base == 'n') return "ň";
        if (base == 'N') return "Ň";
        if (base == 'r') return "ř";
        if (base == 'R') return "Ř";
        if (base == 's') return "š";
        if (base == 'S') return "Š";
        if (base == 't') return "ť";
        if (base == 'T') return "Ť";
        if (base == 'z') return "ž";
        if (base == 'Z') return "Ž";
    }

    if (deadKey == '\'') {
        if (base == 'a') return "á";
        if (base == 'A') return "Á";
        if (base == 'e') return "é";
        if (base == 'E') return "É";
        if (base == 'i') return "í";
        if (base == 'I') return "Í";
        if (base == 'o') return "ó";
        if (base == 'O') return "Ó";
        if (base == 'u') return "ú";
        if (base == 'U') return "Ú";
        if (base == 'y') return "ý";
        if (base == 'Y') return "Ý";
    }

    if (deadKey == '"') {
        if (base == 'u') return "ů";
        if (base == 'U') return "Ů";
    }

    String fallback = "";
    fallback += deadKey;
    fallback += base;
    return fallback;
}

bool decodeCzechComposeKey(char key, String &outputChunk) {
    if (key == '^' || key == '\'' || key == '"') {
        gCzechComposeDeadKey = key;
        outputChunk = "";
        return true;
    }

    if (gCzechComposeDeadKey != 0) {
        outputChunk = mapCzechComposedChar(gCzechComposeDeadKey, key);
        gCzechComposeDeadKey = 0;
        return true;
    }

    outputChunk = String(key);
    return true;
}

bool tryApplyPostfixCzechCompose(String &buffer, char deadKey) {
    if (buffer.isEmpty()) {
        return false;
    }

    const char base = buffer[buffer.length() - 1];
    const String mapped = mapCzechComposedChar(deadKey, base);
    if (mapped.length() == 2 && mapped[0] == deadKey && mapped[1] == base) {
        return false;
    }

    buffer.remove(buffer.length() - 1);
    buffer += mapped;
    return true;
}

bool isCardKbLeftArrowCode(uint8_t code) {
    // CardKB left-arrow observed variants.
    return code == 0x94 || code == 0xB4;
}

char decodeCardKbKey(char rawKey) {
    // Some keys arrive with high-bit set; strip it for printable/control keys.
    const char normalized = normalizeInputKey(rawKey);
    const uint8_t code = static_cast<uint8_t>(normalized);

    if (code >= 0x80) {
        const char stripped = static_cast<char>(code & 0x7F);
        if ((stripped >= 32 && stripped <= 126) || stripped == '\n' || stripped == '\r' || stripped == 8 || stripped == 127) {
            return stripped;
        }
    }

    return normalized;
}

bool applySettingsSelection(SystemState &state, uint8_t optionIndex, Stream &out) {
    state.settings.lastSelection = optionIndex;
    if (optionIndex == 0) {
        // Wi-Fi manager entry with explicit actions.
        state.settings.viewMode = kSettingsViewWifiList;
        state.settings.lastMessage = "wifi manager";
        state.settings.selectedWifiIndex = -1;
        state.settings.selectedSsid = "";
        state.settings.wifiPassword = "";
        gWifiCount = 0;
        out.println("Settings: wifi manager open");
        return true;
    }

    if (optionIndex == 1) {
        state.settings.viewMode = kSettingsViewBluetoothList;
        state.settings.lastMessage = "bluetooth manager";
        out.println("Settings: bluetooth manager open");
        return true;
    }

    if (optionIndex == 2) {
        state.config.sdEnabled = !state.config.sdEnabled;
        state.settings.lastMessage = state.config.sdEnabled ? "sd enabled" : "sd disabled";
        out.println("Settings: sd enabled toggled");
        return true;
    }

    if (optionIndex == 3) {
        cycleSdSpeed(state.config);
        state.settings.lastMessage = String("sd ") + state.config.sdProbeSpeed;
        out.print("Settings: sd speed set to ");
        out.println(state.config.sdProbeSpeed);
        return true;
    }

    if (optionIndex == 4) {
        const bool ok = saveConfig(state.config);
        state.settings.lastMessage = ok ? "config saved" : "save failed";
        out.println(ok ? "Settings: config saved" : "Settings: config save failed");
        return true;
    }

    return false;
}

bool renderPlaceholderApp(SystemState &state, Stream &out) {
    String title = String("app: ") + state.launcher.activeAppId;

    if (state.oledReady) {
        gOled.clearBuffer();
        gOled.setFont(u8g2_font_6x10_tf);
        gOled.drawStr(0, 14, "not implemented yet");
        gOled.drawStr(0, 30, title.c_str());
        gOled.drawStr(0, 46, "open settings first");
        gOled.drawStr(0, 62, "launcher show");
        gOled.sendBuffer();
    }

    if (ensureEinkInitialized(state, out)) {
        Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
        prepareLandscapePaint(paint);
        paint.Clear(kUncolored);
        paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
        paint.DrawStringAt(10, 12, "APP PLACEHOLDER", &Font16, kColored);
        paint.DrawLine(0, 34, kEinkLandscapeWidth - 1, 34, kColored);
        paint.DrawStringAt(10, 52, title.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 72, "This app is not implemented yet", &Font12, kColored);
        paint.DrawStringAt(10, 92, "Try: launcher open settings", &Font12, kColored);
        gEink.display(paint.GetImage());
        vTaskDelay(pdMS_TO_TICKS(1));
        gEink.lut_GC();
        vTaskDelay(pdMS_TO_TICKS(1));
        gEink.refresh();
        vTaskDelay(pdMS_TO_TICKS(5));
        gEinkPartialRefreshCounter = 0;
        ++gEinkUpdateCounter;
    }

    out.println("App placeholder rendered");
    return true;
}

bool ensureEinkInitialized(SystemState &state, Stream &out) {
    if (state.einkReady && !gEinkSleeping) {
        return true;
    }

    if (state.einkReady && gEinkSleeping) {
        // Wake path after idle sleep.
        out.println("E-ink: wake from sleep...");
        if (tryWakeEinkPanel(state, out)) {
            return true;
        }
        return false;
    }

    if (gEinkInitAttempted) {
        out.println("E-ink: unavailable");
        return false;
    }

    gEinkInitAttempted = true;
    // First-use lazy init keeps boot robust when panel is disconnected.
    out.println("E-ink: init on demand...");
    if (gEink.Init() == 0) {
        state.einkReady = true;
        gEinkSleeping = false;
        gEinkPartialRefreshCounter = 0;
        markDisplayActivity();
        out.println("E-ink: ready");
        return true;
    }

    state.einkReady = false;
    out.println("E-ink: init failed");
    return false;
}
}  // namespace

void renderActiveAppOled(const SystemState &state) {
    drawActiveAppOled(state);
}

bool initDisplays(SystemState &state, Stream &out) {
    out.println("--- Display init ---");

    gOled.begin();
    gOled.enableUTF8Print();
    gOledHeldInReset = false;
    state.oledReady = true;
    drawBootScreen(state.config.deviceName);
    out.println("OLED: ready");

    // E-ink init is deferred until first explicit use (shell command),
    // so boot remains responsive even when the panel is disconnected.
    state.einkReady = false;
    gEinkInitAttempted = false;
    gEinkSleeping = false;
    gOledSleeping = false;
    markDisplayActivity();
    out.println("E-ink: deferred");

    return state.oledReady || state.einkReady;
}

void printDisplayInfo(const SystemState &state, Stream &out) {
    out.print("oled: ");
    out.println(state.oledReady ? "ready" : "not ready");

    out.print("eink: ");
    if (!state.einkReady) {
        out.println("not ready");
    } else {
        out.println(gEinkSleeping ? "sleeping" : "ready");
    }

    if (state.einkReady) {
        out.print("eink updates: ");
        out.println(gEinkUpdateCounter);
        out.println("eink orientation: landscape");
    }
}

void renderOledStatus(const SystemState &state, Stream &out) {
    if (!state.oledReady) {
        out.println("OLED not ready");
        return;
    }

    char line[32];
    snprintf(line, sizeof(line), "Uptime: %lu s", millis() / 1000UL);

    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x10_tf);
    gOled.drawStr(0, 12, state.config.deviceName.c_str());
    gOled.drawStr(0, 26, line);
    gOled.drawStr(0, 40, state.sdReady ? "SD: ready" : "SD: off/fail");
    gOled.drawStr(0, 54, state.rtcReady ? "RTC: ready" : "RTC: not ready");
    gOled.sendBuffer();

    out.println("OLED: status rendered");
}

void printLauncherInfo(const SystemState &state, Stream &out) {
    drawLauncherPreview(state, out);
}

bool renderLauncherScreen(SystemState &state, bool oledOnly, Stream &out) {
    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        drawLauncherOled(state);
        xSemaphoreGive(state.spiMutex);
    }

    if (oledOnly) {
        return state.oledReady;
    }

    if (!ensureEinkInitialized(state, out)) {
        out.println("OLED: launcher rendered");
        return state.oledReady;
    }

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
        prepareLandscapePaint(paint);
        paint.Clear(kUncolored);
        paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
        paint.DrawStringAt(8, 8, "APP LAUNCHER", &Font16, kColored);
        paint.DrawStringAt(8, 26, state.config.deviceName.c_str(), &Font12, kColored);
        paint.DrawStringAt(172, 26, state.launcher.activeAppId.c_str(), &Font12, kColored);
        paint.DrawLine(0, 38, kEinkLandscapeWidth - 1, 38, kColored);

        const int cardWidth = 110;
        const int cardHeight = 82;
        const int startX = 8;
        const int startY = 46;
        const int xGap = 8;
        const int yGap = 12;

        for (size_t index = 0; index < kLauncherAppCount; ++index) {
            const int column = static_cast<int>(index % 3);
            const int row = static_cast<int>(index / 3);
            const int x = startX + column * (cardWidth + xGap);
            const int y = startY + row * (cardHeight + yGap);
            const bool selected = index == state.launcher.selectedIndex;
            drawLauncherCard(paint, x, y, cardWidth, cardHeight, kLauncherApps[index], selected);
        }

        paint.DrawStringAt(8, 208, "open settings: press 1 on CardKB", &Font12, kColored);
        gSdAppCount = loadSdAppManifest(state, gSdApps, kMaxSdApps, out);
        if (gSdAppCount > 0) {
            paint.DrawStringAt(8, 224, "sd apps:", &Font12, kColored);
            int x = 76;
            for (size_t i = 0; i < gSdAppCount && i < 4; ++i) {
                paint.DrawStringAt(x, 224, gSdApps[i].c_str(), &Font8, kColored);
                x += 70;
            }
        }

        // Launcher always uses full refresh for readability/stability.
        gEink.display(paint.GetImage());
        vTaskDelay(pdMS_TO_TICKS(1));
        gEink.lut_GC();
        vTaskDelay(pdMS_TO_TICKS(1));
        gEink.refresh();
        vTaskDelay(pdMS_TO_TICKS(5));
        gEinkPartialRefreshCounter = 0;

        ++gEinkUpdateCounter;
        xSemaphoreGive(state.spiMutex);
    }

    markDisplayActivity();
    out.println("E-ink: launcher rendered");
    return true;
}

bool renderSettingsScreen(SystemState &state, bool oledOnly, Stream &out) {
    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        drawSettingsOled(state);
        xSemaphoreGive(state.spiMutex);
    }

    if (oledOnly) {
        return state.oledReady;
    }

    if (!ensureEinkInitialized(state, out)) {
        out.println("OLED: settings rendered");
        return state.oledReady;
    }

    // Settings view transitions use full refresh for clean state changes.
    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        drawSettingsEink(state);
        gEinkPartialRefreshCounter = 0;
        ++gEinkUpdateCounter;
        xSemaphoreGive(state.spiMutex);
    }
    markDisplayActivity();
    out.println("Settings app rendered");
    return true;
}

bool renderActiveApp(SystemState &state, bool oledOnly, Stream &out) {
    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        return renderLauncherScreen(state, oledOnly, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        return renderSettingsScreen(state, oledOnly, out);
    }

    return renderPlaceholderApp(state, out);
}

bool handleActiveAppInput(SystemState &state, char key, Stream &out) {
    // Decode CardKB-specific encodings before app-level routing.
    const uint8_t rawCode = static_cast<uint8_t>(key);
    if (isCardKbLeftArrowCode(rawCode)) {
        if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
            if (state.settings.viewMode == kSettingsViewWifiPassword) {
                state.settings.viewMode = kSettingsViewWifiSelectList;
                state.settings.wifiPassword = "";
                state.settings.lastMessage = "password canceled";
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (state.settings.viewMode == kSettingsViewWifiSelectList) {
                state.settings.viewMode = kSettingsViewWifiList;
                state.settings.lastMessage = "wifi list closed";
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (state.settings.viewMode == kSettingsViewBluetoothSelectList) {
                state.settings.viewMode = kSettingsViewBluetoothList;
                state.settings.lastMessage = "device list closed";
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (state.settings.viewMode == kSettingsViewBluetoothList || state.settings.viewMode == kSettingsViewWifiList) {
                state.settings.viewMode = kSettingsViewHome;
                state.settings.lastMessage = "back";
                renderSettingsScreen(state, false, out);
                return true;
            }

            state.launcher.activeAppId = "launcher";
            state.settings.lastMessage = "back to launcher";
            renderLauncherScreen(state, false, out);
            return true;
        }

        if (!state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
            state.launcher.activeAppId = "launcher";
            renderLauncherScreen(state, false, out);
            return true;
        }
    }

    const char normalizedKey = decodeCardKbKey(key);

    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        if (normalizedKey == '1') {
            state.launcher.activeAppId = "settings";
            state.settings.lastMessage = "opened settings";
            out.println("Launcher: opening settings");
            renderSettingsScreen(state, false, out);
            return true;
        }

        if (normalizedKey >= 32 && normalizedKey <= 126) {
            gLauncherKeyMessage = String("key: ") + normalizedKey + " (1=settings)";
        } else {
            char rawHex[24];
            snprintf(rawHex, sizeof(rawHex), "key:0x%02X (1=settings)", static_cast<uint8_t>(key));
            gLauncherKeyMessage = rawHex;
        }
        // Only update OLED for feedback on unused keys.
        renderLauncherScreen(state, true, out);
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        if (state.settings.viewMode == kSettingsViewWifiList) {
            if (normalizedKey == '1') {
                toggleWifiEnabled(state, out);
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (normalizedKey == '2') {
                if (!state.settings.wifiEnabled) {
                    state.settings.lastMessage = "enable wifi first";
                    renderSettingsScreen(state, false, out);
                    return true;
                }

                state.settings.viewMode = kSettingsViewWifiSelectList;
                state.settings.selectedWifiIndex = -1;
                state.settings.selectedSsid = "";
                state.settings.wifiPassword = "";
                state.settings.lastMessage = "wifi scanning";
                gWifiCount = 0;
                gWifiScanInProgress = true;
                renderSettingsScreen(state, false, out);
                scanWifiNetworks(out);
                gWifiScanInProgress = false;
                state.settings.lastMessage = "select wifi";
                renderSettingsScreen(state, false, out);
                return true;
            }
            return false;
        }

        if (state.settings.viewMode == kSettingsViewWifiSelectList) {
            if (normalizedKey >= '1' && normalizedKey <= '9') {
                const uint8_t pick = static_cast<uint8_t>(normalizedKey - '1');
                if (pick < gWifiCount) {
                    state.settings.selectedWifiIndex = static_cast<int8_t>(pick);
                    state.settings.selectedSsid = gWifiSsidList[pick];
                    state.settings.wifiPassword = "";
                    state.settings.viewMode = kSettingsViewWifiPassword;
                    state.settings.lastMessage = String("ssid ") + state.settings.selectedSsid;
                    renderSettingsScreen(state, false, out);
                    return true;
                }
            }
            return false;
        }

        if (state.settings.viewMode == kSettingsViewBluetoothList) {
            if (normalizedKey == '1') {
                toggleBluetoothEnabled(state, out);
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (normalizedKey == '2') {
                if (!state.settings.btEnabled) {
                    state.settings.lastMessage = "enable bt first";
                    renderSettingsScreen(state, false, out);
                    return true;
                }

                state.settings.viewMode = kSettingsViewBluetoothSelectList;
                state.settings.selectedBluetoothIndex = -1;
                state.settings.btConnectedDeviceName = "";
                state.settings.btConnected = false;
                state.settings.lastMessage = "bt scanning";
                gBluetoothDeviceCount = 0;
                gBluetoothScanInProgress = true;
                renderSettingsScreen(state, false, out);
                scanBluetoothDevices(out);
                gBluetoothScanInProgress = false;
                state.settings.lastMessage = "select bt";
                renderSettingsScreen(state, false, out);
                return true;
            }

            return false;
        }

        if (state.settings.viewMode == kSettingsViewBluetoothSelectList) {
            if (normalizedKey >= '1' && normalizedKey <= '9') {
                const uint8_t pick = static_cast<uint8_t>(normalizedKey - '1');
                if (pick < gBluetoothDeviceCount) {
                    state.settings.selectedBluetoothIndex = static_cast<int8_t>(pick);
                    state.settings.btConnectedDeviceName = gBluetoothDeviceList[pick];
                    state.settings.btConnected = true;
                    state.settings.lastMessage = String("bt connected ") + state.settings.btConnectedDeviceName;
                    state.settings.viewMode = kSettingsViewBluetoothList;
                    renderSettingsScreen(state, false, out);
                    return true;
                }
            }
            return false;
        }

        if (state.settings.viewMode == kSettingsViewWifiPassword) {
            // Password mode intentionally updates only OLED on typing keys.
            if (normalizedKey == '0') {
                gCzechComposeDeadKey = 0;
                state.settings.viewMode = kSettingsViewWifiSelectList;
                state.settings.wifiPassword = "";
                state.settings.lastMessage = "password canceled";
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (normalizedKey == '\n' || normalizedKey == '\r') {
                gCzechComposeDeadKey = 0;
                // Enter commits connect action and returns to settings home.
                state.settings.lastMessage = "connecting...";
                renderSettingsScreen(state, true, out);
                connectSelectedWifi(state, out);
                state.settings.viewMode = kSettingsViewHome;
                renderSettingsScreen(state, false, out);
                return true;
            }

            if (normalizedKey == 8 || normalizedKey == 127) {
                gCzechComposeDeadKey = 0;
                if (!state.settings.wifiPassword.isEmpty()) {
                    state.settings.wifiPassword.remove(state.settings.wifiPassword.length() - 1);
                }
                state.settings.lastMessage = "key: backspace";
                renderSettingsScreen(state, true, out);
                return true;
            }

            if (normalizedKey >= 32 && normalizedKey <= 126) {
                char composeKey = normalizedKey;
                if (composeKey == '/') {
                    composeKey = '^';
                }

                if (composeKey == '^' || composeKey == '\'' || composeKey == '"') {
                    if (tryApplyPostfixCzechCompose(state.settings.wifiPassword, composeKey)) {
                        state.settings.lastMessage = "compose postfix";
                        renderSettingsScreen(state, true, out);
                        return true;
                    }
                }

                String typed;
                decodeCzechComposeKey(composeKey, typed);

                if (typed.isEmpty()) {
                    state.settings.lastMessage = "compose...";
                    renderSettingsScreen(state, true, out);
                    return true;
                }

                if (state.settings.wifiPassword.length() + typed.length() < 63) {
                    state.settings.wifiPassword += typed;
                }
                state.settings.lastMessage = String("key: ") + typed;
                renderSettingsScreen(state, true, out);
                return true;
            }

            // Keep unsupported key presses visible for debugging CardKB mapping.
            char rawHex[20];
            snprintf(rawHex, sizeof(rawHex), "key:0x%02X", static_cast<uint8_t>(key));
            state.settings.lastMessage = rawHex;
            renderSettingsScreen(state, true, out);
            return true;
        }

        if (normalizedKey >= '1' && normalizedKey <= '5') {
            const uint8_t optionIndex = static_cast<uint8_t>(normalizedKey - '1');
            if (applySettingsSelection(state, optionIndex, out)) {
                renderSettingsScreen(state, false, out);
                return true;
            }
            return false;
        }

        if (normalizedKey == '0') {
            state.launcher.activeAppId = "launcher";
            state.settings.lastMessage = "back to launcher";
            renderLauncherScreen(state, false, out);
            return true;
        }

        return false;
    }

    return false;
}

bool renderEinkMessage(SystemState &state, const String &rawText, bool forceFullRefresh, Stream &out) {
    if (!ensureEinkInitialized(state, out)) {
        return false;
    }

    String text = rawText;
    text.trim();
    if (text.isEmpty()) {
        text = "(empty)";
    }

    Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
    prepareLandscapePaint(paint);
    paint.Clear(kUncolored);
    paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
    paint.DrawStringAt(10, 10, "E-INK", &Font16, kColored);
    paint.DrawLine(0, 32, kEinkLandscapeWidth - 1, 32, kColored);

    int y = 44;
    while (text.length() > 0 && y < (kEinkLandscapeHeight - 16)) {
        const int take = min(static_cast<int>(text.length()), 44);
        String row = text.substring(0, take);
        text = text.length() > static_cast<unsigned int>(take) ? text.substring(take) : "";
        paint.DrawStringAt(10, y, row.c_str(), &Font12, kColored);
        y += 18;
    }

    // Policy: after 9 DU refreshes, the next update is forced to full GC.
    const bool partialThresholdReached = gEinkPartialRefreshCounter >= 9;
    const bool doFull = forceFullRefresh || partialThresholdReached;

    gEink.display(paint.GetImage());
    if (doFull) {
        gEinkPartialRefreshCounter = 0;
    } else {
        gEink.lut_DU();
        ++gEinkPartialRefreshCounter;
    }
    gEink.refresh();

    ++gEinkUpdateCounter;
    markDisplayActivity();
    out.println(doFull ? "E-ink: full refresh" : "E-ink: fast refresh");
    return true;
}

bool wakeDisplaysOnInput(SystemState &state, Stream &out) {
    bool wokeAny = false;

    if (state.oledReady && gOledSleeping) {
        forceOledWakeOn(state);
        gOledSleeping = false;
        wokeAny = true;
        out.println("OLED: wake");
    }

    if (state.einkReady && gEinkSleeping) {
        if (tryWakeEinkPanel(state, out)) {
            wokeAny = true;
            out.println("E-ink: wake");
        }
    }

    if (wokeAny) {
        markDisplayActivity();
    }

    return wokeAny;
}

void noteDisplayActivity() {
    // Public helper used by loop/input code.
    markDisplayActivity();
}

bool handleEinkIdleTimeout(SystemState &state, Stream &out) {
    const uint32_t nowMs = millis();
    // Sleep only after uninterrupted inactivity window.
    if (nowMs - gLastDisplayActivityMs < kEinkIdleTimeoutMs) {
        return false;
    }

    bool changed = false;
    if (state.oledReady && !gOledSleeping) {
        forceOledSleepOff(state);
        gOledSleeping = true;
        out.println("OLED: idle timeout, going to sleep");
        changed = true;
    }

    if (state.einkReady && !gEinkSleeping) {
        gEinkSleeping = true;
        out.println("E-ink: marked as sleeping for re-init on wake");
        changed = true;
    }

    return changed;
}
