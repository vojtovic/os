#include "DisplayManager.h"

#include <U8g2lib.h>

#include "EPD_3in52.h"
#include "epdpaint.h"

namespace {
constexpr uint8_t kOledClk = 12;
constexpr uint8_t kOledMosi = 11;
constexpr uint8_t kOledRes = 47;
constexpr uint8_t kOledDc = 21;
constexpr uint8_t kOledCs = 10;

constexpr int kColored = 0;
constexpr int kUncolored = 1;

U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI gOled(
    U8G2_R0,
    kOledClk,
    kOledMosi,
    kOledCs,
    kOledDc,
    kOledRes);

Epd gEink;
UBYTE gEinkBuffer[10800];
uint32_t gEinkUpdateCounter = 0;
bool gEinkInitAttempted = false;

void drawBootScreen(const String &deviceName) {
    gOled.clearBuffer();
    gOled.setFont(u8g2_font_6x10_tf);
    gOled.drawStr(0, 12, "os boot");
    gOled.drawStr(0, 28, "device:");
    gOled.drawStr(48, 28, deviceName.c_str());
    gOled.drawStr(0, 44, "OLED ready");
    gOled.sendBuffer();
}

bool ensureEinkInitialized(SystemState &state, Stream &out) {
    if (state.einkReady) {
        return true;
    }

    if (gEinkInitAttempted) {
        out.println("E-ink: unavailable");
        return false;
    }

    gEinkInitAttempted = true;
    out.println("E-ink: init on demand...");
    if (gEink.Init() == 0) {
        state.einkReady = true;
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
    out.println("E-ink: deferred");

    return state.oledReady || state.einkReady;
}

void printDisplayInfo(const SystemState &state, Stream &out) {
    out.print("oled: ");
    out.println(state.oledReady ? "ready" : "not ready");

    out.print("eink: ");
    out.println(state.einkReady ? "ready" : "not ready");

    if (state.einkReady) {
        out.print("eink updates: ");
        out.println(gEinkUpdateCounter);
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

bool renderEinkMessage(SystemState &state, const String &rawText, bool forceFullRefresh, Stream &out) {
    if (!ensureEinkInitialized(state, out)) {
        return false;
    }

    String text = rawText;
    text.trim();
    if (text.isEmpty()) {
        text = "(empty)";
    }

    Paint paint(gEinkBuffer, 240, 360);
    paint.Clear(kUncolored);
    paint.DrawRectangle(0, 0, 239, 359, kColored);
    paint.DrawStringAt(10, 12, "E-INK", &Font16, kColored);
    paint.DrawLine(0, 34, 239, 34, kColored);

    int y = 48;
    while (text.length() > 0 && y < 340) {
        const int take = min(static_cast<int>(text.length()), 28);
        String row = text.substring(0, take);
        text = text.length() > static_cast<unsigned int>(take) ? text.substring(take) : "";
        paint.DrawStringAt(10, y, row.c_str(), &Font12, kColored);
        y += 18;
    }

    const bool periodicFull = (gEinkUpdateCounter > 0) && (gEinkUpdateCounter % 20 == 0);
    const bool doFull = forceFullRefresh || periodicFull;

    gEink.display(paint.GetImage());
    if (doFull) {
        gEink.lut_GC();
    } else {
        gEink.lut_DU();
    }
    gEink.refresh();

    ++gEinkUpdateCounter;
    out.println(doFull ? "E-ink: full refresh" : "E-ink: fast refresh");
    return true;
}
