#include "ConfigStore.h"

#include <LittleFS.h>

namespace {
constexpr const char *kConfigPath = "/config.ini";

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