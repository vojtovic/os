#include "StorageManager.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

namespace {
// SD karta je zapojena stejne jako v mp3_test.
constexpr int kSdMosi = 38;
constexpr int kSdSck = 39;
constexpr int kSdMiso = 40;
constexpr int kSdCs = 46;
constexpr const char *kSdAppsManifest = "/apps/apps.txt";

SPIClass sdSpi(HSPI);
}  // namespace

bool initLittleFs(SystemState &state) {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS: mount selhal");
        state.littleFsReady = false;
        return false;
    }

    Serial.println("LittleFS: OK");
    state.littleFsReady = loadConfig(state.config);
    if (!state.littleFsReady) {
        return false;
    }

    printConfig(state.config);
    return true;
}

bool initSdCard(SystemState &state) {
    if (!state.config.sdEnabled) {
        Serial.println("SD: vypnuto v config.ini");
        state.sdReady = false;
        return true;
    }

    pinMode(kSdCs, OUTPUT);
    digitalWrite(kSdCs, HIGH);

    sdSpi.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);

    Serial.printf("SD: init na %lu Hz\n", static_cast<unsigned long>(state.config.sdProbeSpeed));
    if (SD.begin(kSdCs, sdSpi, state.config.sdProbeSpeed)) {
        Serial.println("SD: OK");
        state.sdReady = true;
        return true;
    }

    Serial.println("SD: fallback na 1 MHz");
    if (SD.begin(kSdCs, sdSpi, 1000000)) {
        Serial.println("SD: OK po fallbacku");
        state.sdReady = true;
        return true;
    }

    Serial.println("SD: init selhal");
    state.sdReady = false;
    return false;
}

void printFsInfo(const SystemState &state, Stream &out) {
    out.println("--- LittleFS ---");
    out.print("ready: ");
    out.println(state.littleFsReady ? "yes" : "no");
    if (!state.littleFsReady) {
        return;
    }

    out.print("total bytes: ");
    out.println(LittleFS.totalBytes());
    out.print("used bytes: ");
    out.println(LittleFS.usedBytes());
}

void printSdInfo(const SystemState &state, Stream &out) {
    out.println("--- SD ---");
    out.print("ready: ");
    out.println(state.sdReady ? "yes" : "no");
    if (!state.sdReady) {
        return;
    }

    uint8_t cardType = SD.cardType();
    out.print("card type: ");
    if (cardType == CARD_NONE) {
        out.println("NONE");
    } else if (cardType == CARD_MMC) {
        out.println("MMC");
    } else if (cardType == CARD_SD) {
        out.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        out.println("SDHC/SDXC");
    } else {
        out.println("UNKNOWN");
    }

    out.print("card size MB: ");
    out.println(SD.cardSize() / (1024 * 1024));
    out.print("total bytes: ");
    out.println(SD.totalBytes());
    out.print("used bytes: ");
    out.println(SD.usedBytes());
}

void listLittleFs(const SystemState &state, const String &path, Stream &out) {
    if (!state.littleFsReady) {
        out.println("LittleFS neni ready");
        return;
    }

    File root = LittleFS.open(path.length() ? path : "/", FILE_READ);
    if (!root) {
        out.println("LittleFS: nelze otevrit cestu");
        return;
    }

    if (!root.isDirectory()) {
        out.println("LittleFS: cesta neni adresar");
        root.close();
        return;
    }

    out.println("--- LittleFS ls ---");
    File entry = root.openNextFile();
    while (entry) {
        out.print(entry.isDirectory() ? "[D] " : "[F] ");
        out.print(entry.name());
        if (!entry.isDirectory()) {
            out.print(" ");
            out.print(entry.size());
            out.print(" B");
        }
        out.println();
        entry = root.openNextFile();
    }
    root.close();
}

void listSd(const SystemState &state, const String &path, Stream &out) {
    if (!state.sdReady) {
        out.println("SD neni ready");
        return;
    }

    File root = SD.open(path.length() ? path : "/", FILE_READ);
    if (!root) {
        out.println("SD: nelze otevrit cestu");
        return;
    }

    if (!root.isDirectory()) {
        out.println("SD: cesta neni adresar");
        root.close();
        return;
    }

    out.println("--- SD ls ---");
    File entry = root.openNextFile();
    while (entry) {
        out.print(entry.isDirectory() ? "[D] " : "[F] ");
        out.print(entry.name());
        if (!entry.isDirectory()) {
            out.print(" ");
            out.print(entry.size());
            out.print(" B");
        }
        out.println();
        entry = root.openNextFile();
    }
    root.close();
}

size_t loadSdAppManifest(const SystemState &state, String *appIds, size_t maxApps, Stream &out) {
    if (!state.sdReady || appIds == nullptr || maxApps == 0) {
        return 0;
    }

    File file = SD.open(kSdAppsManifest, FILE_READ);
    if (!file) {
        out.println("SD apps: manifest not found (/apps/apps.txt)");
        return 0;
    }

    size_t count = 0;
    while (file.available() && count < maxApps) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) {
            continue;
        }

        appIds[count++] = line;
    }

    file.close();
    out.print("SD apps loaded: ");
    out.println(count);
    return count;
}