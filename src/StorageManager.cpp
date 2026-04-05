#include "StorageManager.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

namespace {
constexpr const char *kConfigPath = "/config.ini";

// SD karta je zapojena stejne jako v mp3_test.
constexpr int kSdMosi = 38;
constexpr int kSdSck = 39;
constexpr int kSdMiso = 40;
constexpr int kSdCs = 46;

SPIClass sdSpi(HSPI);

String trimCopy(String value) {
    value.trim();
    return value;
}

bool parseBool(const String &value) {
    String normalized = value;
    normalized.toLowerCase();
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

void writeDefaultConfig(const AppConfig &config) {
    File file = LittleFS.open(kConfigPath, FILE_WRITE);
    if (!file) {
        Serial.println("LittleFS: nelze vytvorit config.ini");
        return;
    }

    file.println("# Default config for os");
    file.print("device_name=");
    file.println(config.deviceName);
    file.print("sd_enabled=");
    file.println(config.sdEnabled ? "true" : "false");
    file.print("sd_probe_speed=");
    file.println(config.sdProbeSpeed);
    file.close();
}
}  // namespace

void printConfig(const AppConfig &config, Stream &out) {
    out.println("--- Config ---");
    out.print("device_name: ");
    out.println(config.deviceName);
    out.print("sd_enabled: ");
    out.println(config.sdEnabled ? "true" : "false");
    out.print("sd_probe_speed: ");
    out.println(config.sdProbeSpeed);
}

bool loadConfig(AppConfig &config) {
    if (!LittleFS.exists(kConfigPath)) {
        Serial.println("LittleFS: config.ini chybi, vytvarim vychozi soubor");
        writeDefaultConfig(config);
        return true;
    }

    File file = LittleFS.open(kConfigPath, FILE_READ);
    if (!file) {
        Serial.println("LittleFS: config.ini nelze otevrit");
        return false;
    }

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line = trimCopy(line);
        if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) {
            continue;
        }

        const int separator = line.indexOf('=');
        if (separator < 0) {
            continue;
        }

        String key = trimCopy(line.substring(0, separator));
        String value = trimCopy(line.substring(separator + 1));

        if (key == "device_name") {
            config.deviceName = value;
        } else if (key == "sd_enabled") {
            config.sdEnabled = parseBool(value);
        } else if (key == "sd_probe_speed") {
            config.sdProbeSpeed = value.toInt();
        }
    }

    file.close();
    return true;
}

bool saveConfig(const AppConfig &config) {
    LittleFS.remove(kConfigPath);
    File file = LittleFS.open(kConfigPath, FILE_WRITE);
    if (!file) {
        Serial.println("LittleFS: config.ini nelze ulozit");
        return false;
    }

    file.println("# Default config for os");
    file.print("device_name=");
    file.println(config.deviceName);
    file.print("sd_enabled=");
    file.println(config.sdEnabled ? "true" : "false");
    file.print("sd_probe_speed=");
    file.println(config.sdProbeSpeed);
    file.close();
    return true;
}

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