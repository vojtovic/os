#include "DisplayManager.h"

#include <U8g2lib.h>
#include <WiFi.h>

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

U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI gOled(
    U8G2_R0,
    kOledClk,
    kOledMosi,
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
String gLauncherKeyMessage = "press 1 for settings";

constexpr const char *kSettingsOptions[] = {
    "1) WiFi manager",
    "2) SD enable toggle",
    "3) SD speed cycle",
    "4) Save config",
};

constexpr uint8_t kSettingsOptionCount = sizeof(kSettingsOptions) / sizeof(kSettingsOptions[0]);
constexpr uint8_t kSettingsViewHome = 0;
constexpr uint8_t kSettingsViewWifiList = 1;
constexpr uint8_t kSettingsViewWifiPassword = 2;
constexpr size_t kMaxWifiNetworks = 9;
String gWifiSsidList[kMaxWifiNetworks];
int32_t gWifiRssiList[kMaxWifiNetworks];
size_t gWifiCount = 0;
// True while synchronous Wi-Fi scan is running.
bool gWifiScanInProgress = false;

bool ensureEinkInitialized(SystemState &state, Stream &out);

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

void prepareLandscapePaint(Paint &paint) {
    paint.SetRotate(ROTATE_90);
}

void drawSettingsOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x10_tf);
    gOled.drawStr(0, 11, "SETTINGS");

    if (state.settings.viewMode == kSettingsViewHome) {
        // Home view: shortcut legend + compact status line.
        char speedLine[26];
        snprintf(speedLine, sizeof(speedLine), "sd hz:%lu", state.config.sdProbeSpeed);
        gOled.drawStr(0, 22, "1 wifi 2 sd 3 speed");
        gOled.drawStr(0, 32, "4 save 0 launcher");
        gOled.drawStr(0, 42, state.settings.wifiEnabled ? "wifi:on" : "wifi:off");
        gOled.drawStr(64, 42, state.config.sdEnabled ? "sd:on" : "sd:off");
        gOled.drawStr(0, 52, speedLine);
        gOled.setCursor(0, 62);
        gOled.print("last:");
        gOled.print(state.settings.lastMessage);
    } else if (state.settings.viewMode == kSettingsViewWifiList) {
        // During scan we show loading text, then switch to selection hint.
        if (gWifiScanInProgress) {
            gOled.drawStr(0, 22, "wifi scanning...");
            gOled.drawStr(0, 33, "please wait");
            gOled.drawStr(0, 44, "loading list");
        } else {
            gOled.drawStr(0, 22, "wifi list on e-ink");
            gOled.drawStr(0, 33, "type 1..9 to select");
            gOled.drawStr(0, 44, "0 = back to settings");
        }
        gOled.setCursor(0, 56);
        gOled.print("found: ");
        gOled.print(gWifiCount);
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
        String line2 = String("sd: ") + (state.config.sdEnabled ? "enabled" : "disabled");
        String line3 = String("sd speed: ") + String(state.config.sdProbeSpeed) + " hz";

        paint.DrawStringAt(10, 42, line0.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 58, line1.c_str(), &Font12, kColored);
        paint.DrawStringAt(170, 58, line2.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 74, line3.c_str(), &Font12, kColored);

        int y = 98;
        for (uint8_t i = 0; i < kSettingsOptionCount; ++i) {
            paint.DrawStringAt(10, y, kSettingsOptions[i], &Font12, kColored);
            y += 16;
        }

        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Input on CardKB: 1..4  (0 = launcher)", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewWifiList) {
        // List view: either scanning placeholder or scan results.
        paint.DrawStringAt(10, 42, "Available WiFi:", &Font12, kColored);
        if (gWifiScanInProgress) {
            paint.DrawStringAt(10, 62, "Scanning networks...", &Font12, kColored);
            paint.DrawStringAt(10, 80, "Please wait", &Font12, kColored);
        } else {
            int y = 58;
            for (size_t i = 0; i < gWifiCount && i < kMaxWifiNetworks; ++i) {
                char line[64];
                snprintf(line, sizeof(line), "%u) %s (%ld)", static_cast<unsigned>(i + 1), gWifiSsidList[i].c_str(), static_cast<long>(gWifiRssiList[i]));
                paint.DrawStringAt(10, y, line, &Font12, kColored);
                y += 16;
            }

            if (gWifiCount == 0) {
                paint.DrawStringAt(10, 76, "No networks found", &Font12, kColored);
            }
        }

        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Type 1..9 to select, 0 back", &Font12, kColored);
    } else {
        // Password view: e-ink only shows context, typing remains on OLED.
        paint.DrawStringAt(10, 42, "Connect to:", &Font12, kColored);
        paint.DrawStringAt(98, 42, state.settings.selectedSsid.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 60, "Password on OLED keyboard", &Font12, kColored);
        paint.DrawStringAt(10, 78, "Press ENTER to connect", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "0 cancel", &Font12, kColored);
    }

    gEink.display(paint.GetImage());
    gEink.lut_GC();
    gEink.refresh();
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
        // Wi-Fi manager entry: render loading immediately, then scan.
        state.settings.viewMode = kSettingsViewWifiList;
        state.settings.lastMessage = "wifi scanning";
        state.settings.selectedWifiIndex = -1;
        state.settings.selectedSsid = "";
        state.settings.wifiPassword = "";
        gWifiCount = 0;
        gWifiScanInProgress = true;
        renderSettingsScreen(state, out);
        scanWifiNetworks(out);
        gWifiScanInProgress = false;
        state.settings.lastMessage = "select wifi";
        out.println("Settings: wifi manager open");
        return true;
    }

    if (optionIndex == 1) {
        state.config.sdEnabled = !state.config.sdEnabled;
        state.settings.lastMessage = state.config.sdEnabled ? "sd enabled" : "sd disabled";
        out.println("Settings: sd enabled toggled");
        return true;
    }

    if (optionIndex == 2) {
        cycleSdSpeed(state.config);
        state.settings.lastMessage = String("sd ") + state.config.sdProbeSpeed;
        out.print("Settings: sd speed set to ");
        out.println(state.config.sdProbeSpeed);
        return true;
    }

    if (optionIndex == 3) {
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
        gEink.lut_GC();
        gEink.refresh();
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
        if (gEink.Init() == 0) {
            gEinkSleeping = false;
            gEinkPartialRefreshCounter = 0;
            markDisplayActivity();
            out.println("E-ink: awake");
            return true;
        }

        state.einkReady = false;
        out.println("E-ink: wake failed");
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

bool initDisplays(SystemState &state, Stream &out) {
    out.println("--- Display init ---");

    gOled.begin();
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

bool renderLauncherScreen(SystemState &state, Stream &out) {
    drawLauncherOled(state);

    if (!ensureEinkInitialized(state, out)) {
        out.println("OLED: launcher rendered");
        return state.oledReady;
    }

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
    gEink.lut_GC();
    gEink.refresh();
    gEinkPartialRefreshCounter = 0;

    ++gEinkUpdateCounter;
    markDisplayActivity();
    out.println("E-ink: launcher rendered");
    return true;
}

bool renderSettingsScreen(SystemState &state, Stream &out) {
    drawSettingsOled(state);

    if (!ensureEinkInitialized(state, out)) {
        out.println("OLED: settings rendered");
        return state.oledReady;
    }

    // Settings view transitions use full refresh for clean state changes.
    drawSettingsEink(state);
    gEinkPartialRefreshCounter = 0;
    ++gEinkUpdateCounter;
    markDisplayActivity();
    out.println("Settings app rendered");
    return true;
}

bool renderActiveApp(SystemState &state, Stream &out) {
    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        return renderLauncherScreen(state, out);
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        return renderSettingsScreen(state, out);
    }

    return renderPlaceholderApp(state, out);
}

bool handleActiveAppInput(SystemState &state, char key, Stream &out) {
    // Decode CardKB-specific encodings before app-level routing.
    const char normalizedKey = decodeCardKbKey(key);

    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        if (normalizedKey == '1') {
            state.launcher.activeAppId = "settings";
            state.settings.lastMessage = "opened settings";
            out.println("Launcher: opening settings");
            renderSettingsScreen(state, out);
            return true;
        }

        if (normalizedKey >= 32 && normalizedKey <= 126) {
            gLauncherKeyMessage = String("key: ") + normalizedKey + " (1=settings)";
        } else {
            char rawHex[24];
            snprintf(rawHex, sizeof(rawHex), "key:0x%02X (1=settings)", static_cast<uint8_t>(key));
            gLauncherKeyMessage = rawHex;
        }
        drawLauncherOled(state);
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        if (state.settings.viewMode == kSettingsViewWifiList) {
            if (normalizedKey == '0') {
                state.settings.viewMode = kSettingsViewHome;
                state.settings.lastMessage = "wifi list closed";
                renderSettingsScreen(state, out);
                return true;
            }

            if (normalizedKey >= '1' && normalizedKey <= '9') {
                // Number keys pick an SSID directly by list index.
                const uint8_t pick = static_cast<uint8_t>(normalizedKey - '1');
                if (pick < gWifiCount) {
                    state.settings.selectedWifiIndex = static_cast<int8_t>(pick);
                    state.settings.selectedSsid = gWifiSsidList[pick];
                    state.settings.wifiPassword = "";
                    state.settings.viewMode = kSettingsViewWifiPassword;
                    state.settings.lastMessage = String("ssid ") + state.settings.selectedSsid;
                    renderSettingsScreen(state, out);
                    return true;
                }
            }
            return false;
        }

        if (state.settings.viewMode == kSettingsViewWifiPassword) {
            // Password mode intentionally updates only OLED on typing keys.
            if (normalizedKey == '0') {
                state.settings.viewMode = kSettingsViewWifiList;
                state.settings.wifiPassword = "";
                state.settings.lastMessage = "password canceled";
                renderSettingsScreen(state, out);
                return true;
            }

            if (normalizedKey == '\n' || normalizedKey == '\r') {
                // Enter commits connect action and returns to settings home.
                state.settings.lastMessage = "connecting...";
                drawSettingsOled(state);
                connectSelectedWifi(state, out);
                state.settings.viewMode = kSettingsViewHome;
                renderSettingsScreen(state, out);
                return true;
            }

            if (normalizedKey == 8 || normalizedKey == 127) {
                if (!state.settings.wifiPassword.isEmpty()) {
                    state.settings.wifiPassword.remove(state.settings.wifiPassword.length() - 1);
                }
                state.settings.lastMessage = "key: backspace";
                drawSettingsOled(state);
                return true;
            }

            if (normalizedKey >= 32 && normalizedKey <= 126) {
                if (state.settings.wifiPassword.length() < 63) {
                    state.settings.wifiPassword += normalizedKey;
                }
                state.settings.lastMessage = String("key: ") + normalizedKey;
                drawSettingsOled(state);
                return true;
            }

            // Keep unsupported key presses visible for debugging CardKB mapping.
            char rawHex[20];
            snprintf(rawHex, sizeof(rawHex), "key:0x%02X", static_cast<uint8_t>(key));
            state.settings.lastMessage = rawHex;
            drawSettingsOled(state);
            return true;
        }

        if (normalizedKey >= '1' && normalizedKey <= '4') {
            const uint8_t optionIndex = static_cast<uint8_t>(normalizedKey - '1');
            if (applySettingsSelection(state, optionIndex, out)) {
                renderSettingsScreen(state, out);
                return true;
            }
            return false;
        }

        if (normalizedKey == '0') {
            state.launcher.activeAppId = "launcher";
            state.settings.lastMessage = "back to launcher";
            renderLauncherScreen(state, out);
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
        gEink.lut_GC();
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
    bool wakeAttempted = false;

    if (state.oledReady && gOledSleeping) {
        wakeAttempted = true;
        gOled.setPowerSave(0);
        gOled.setContrast(255);
        gOledSleeping = false;
        wokeAny = true;
        out.println("OLED: wake");
    }

    if (state.einkReady && gEinkSleeping) {
        wakeAttempted = true;
        out.println("E-ink: wake from sleep...");
        if (gEink.Init() == 0) {
            gEinkSleeping = false;
            gEinkPartialRefreshCounter = 0;
            wokeAny = true;
            out.println("E-ink: awake");
        } else {
            // Allow retry through the lazy init path on next render.
            state.einkReady = false;
            gEinkInitAttempted = false;
            out.println("E-ink: wake failed");
        }
    }

    if (wokeAny || wakeAttempted) {
        markDisplayActivity();
        renderActiveApp(state, out);
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
        gOled.clearBuffer();
        gOled.sendBuffer();
        gOled.setContrast(0);
        gOled.setPowerSave(1);
        gOledSleeping = true;
        out.println("OLED: idle timeout, going to sleep");
        changed = true;
    }

    if (state.einkReady && !gEinkSleeping) {
        out.println("E-ink: idle timeout, going to sleep");
        gEink.Clear();
        gEink.sleep();
        gEinkPartialRefreshCounter = 0;
        gEinkSleeping = true;
        changed = true;
    }

    return changed;
}
