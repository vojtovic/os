#include "SerialShell.h"

namespace {
constexpr size_t kSerialCommandBufferSize = 160;
String serialLine;
bool promptShown = false;

String trimCopy(String value) {
    value.trim();
    return value;
}

void handleConfigCommand(SystemState &state, const String &args, Stream &out) {
    if (args == "show") {
        printConfig(state.config, out);
        return;
    }

    if (args == "reload") {
        if (loadConfig(state.config)) {
            out.println("Config: reload OK");
            printConfig(state.config, out);
        } else {
            out.println("Config: reload failed");
        }
        return;
    }

    if (args == "save") {
        if (saveConfig(state.config)) {
            out.println("Config: save OK");
        } else {
            out.println("Config: save failed");
        }
        return;
    }

    if (args.startsWith("set name ")) {
        state.config.deviceName = args.substring(9);
        state.config.deviceName.trim();
        out.print("Config: device_name=");
        out.println(state.config.deviceName);
        return;
    }

    if (args.startsWith("set sd ")) {
        String value = args.substring(7);
        value.trim();
        String normalized = value;
        normalized.toLowerCase();
        state.config.sdEnabled = normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
        out.print("Config: sd_enabled=");
        out.println(state.config.sdEnabled ? "true" : "false");
        return;
    }

    if (args.startsWith("set sd_speed ")) {
        String value = args.substring(13);
        value.trim();
        uint32_t speed = value.toInt();
        if (speed == 0) {
            out.println("Config: sd_speed musi byt v Hz");
            return;
        }
        state.config.sdProbeSpeed = speed;
        out.print("Config: sd_probe_speed=");
        out.println(state.config.sdProbeSpeed);
        return;
    }

    out.println("Config: unknown subcommand");
}

void handleCommand(SystemState &state, TaskManager &taskManager, String line, Stream &out) {
    line = trimCopy(line);
    if (line.isEmpty()) {
        return;
    }

    if (line == "help" || line == "?") {
        printHelp(out);
    } else if (line == "info") {
        out.println("--- System ---");
        out.print("device: ");
        out.println(state.config.deviceName);
        out.print("uptime ms: ");
        out.println(millis());
        out.print("free heap: ");
        out.println(ESP.getFreeHeap());
        out.print("largest free block: ");
        out.println(ESP.getMaxAllocHeap());
        out.print("cpu frequency: ");
        out.println(ESP.getCpuFreqMHz());
        out.print("core id: ");
        out.println(xPortGetCoreID());
    } else if (line == "tasks") {
        taskManager.logStatus(out);
    } else if (line == "fs info") {
        printFsInfo(state, out);
    } else if (line.startsWith("fs ls")) {
        String path = line.length() > 5 ? trimCopy(line.substring(5)) : String("/");
        listLittleFs(state, path, out);
    } else if (line == "sd info") {
        printSdInfo(state, out);
    } else if (line.startsWith("sd ls")) {
        String path = line.length() > 5 ? trimCopy(line.substring(5)) : String("/");
        listSd(state, path, out);
    } else if (line.startsWith("config ")) {
        handleConfigCommand(state, trimCopy(line.substring(7)), out);
    } else if (line == "reboot") {
        out.println("Rebooting...");
        delay(100);
        ESP.restart();
    } else if (line == "prompt") {
        printPrompt(out);
        return;
    } else {
        out.println("Unknown command. Type 'help'.");
    }

    printPrompt(out);
}
}  // namespace

void printPrompt(Stream &out) {
    out.print("os>");
    promptShown = true;
}

void printHelp(Stream &out) {
    out.println("--- Commands ---");
    out.println("help");
    out.println("info");
    out.println("tasks");
    out.println("fs info");
    out.println("fs ls [path]");
    out.println("sd info");
    out.println("sd ls [path]");
    out.println("config show");
    out.println("config reload");
    out.println("config save");
    out.println("config set name <value>");
    out.println("config set sd on|off");
    out.println("config set sd_speed <hz>");
    out.println("reboot");
    out.println("prompt");
}

void processSerialInput(SystemState &state, TaskManager &taskManager, Stream &out) {
    if (!promptShown) {
        printPrompt(out);
    }

    while (Serial.available() > 0) {
        char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            String line = serialLine;
            serialLine = "";
            handleCommand(state, taskManager, line, out);
            continue;
        }

        if (serialLine.length() < kSerialCommandBufferSize) {
            serialLine += ch;
        }
    }
}