#include "ShellCommands.h"

namespace {
String trimCopy(String value) {
    value.trim();
    return value;
}

bool parseBool(const String &value) {
    String normalized = value;
    normalized.toLowerCase();
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
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
        state.config.sdEnabled = parseBool(value);
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
}  // namespace

void printShellHelp(Stream &out) {
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

bool executeShellCommand(SystemState &state, TaskManager &taskManager, const String &rawLine, Stream &out) {
    String line = trimCopy(rawLine);
    if (line.isEmpty()) {
        return false;
    }

    if (line == "help" || line == "?") {
        printShellHelp(out);
        return true;
    }

    if (line == "info") {
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
        return true;
    }

    if (line == "tasks") {
        taskManager.logStatus(out);
        return true;
    }

    if (line == "fs info") {
        printFsInfo(state, out);
        return true;
    }

    if (line.startsWith("fs ls")) {
        String path = line.length() > 5 ? trimCopy(line.substring(5)) : String("/");
        listLittleFs(state, path, out);
        return true;
    }

    if (line == "sd info") {
        printSdInfo(state, out);
        return true;
    }

    if (line.startsWith("sd ls")) {
        String path = line.length() > 5 ? trimCopy(line.substring(5)) : String("/");
        listSd(state, path, out);
        return true;
    }

    if (line.startsWith("config ")) {
        handleConfigCommand(state, trimCopy(line.substring(7)), out);
        return true;
    }

    if (line == "reboot") {
        out.println("Rebooting...");
        delay(100);
        ESP.restart();
        return true;
    }

    if (line == "prompt") {
        return true;
    }

    out.println("Unknown command. Type 'help'.");
    return true;
}