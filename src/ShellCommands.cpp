#include "ShellCommands.h"

#include "AudioManager.h"
#include "DisplayManager.h"
#include "HardwareManager.h"
#include "WebUploadManager.h"

namespace {
String trimCopy(String value) {
    value.trim();
    return value;
}

constexpr const char *kLauncherAppIds[] = {
    "settings",
    "file-manager",
    "music-player",
    "notes",
    "web-upload",
    "apps",
    "serial",
    "about",
};

constexpr uint8_t kLauncherAppCount = sizeof(kLauncherAppIds) / sizeof(kLauncherAppIds[0]);

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
        invalidateLauncherSdAppCache();
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

void handleWebUploadCommand(SystemState &state, const String &args, Stream &out) {
    if (args.isEmpty() || args == "status") {
        printWebUploadStatus(state, out);
        return;
    }

    if (args == "start") {
        if (startWebUploadServer(state, out)) {
            out.println("Web upload: started");
        }
        return;
    }

    if (args == "stop") {
        stopWebUploadServer(out);
        out.println("Web upload: stopped");
        return;
    }

    out.println("Web upload: unknown subcommand");
}

void handleA2dpCommand(SystemState &state, const String &args, Stream &out) {
    if (args.isEmpty() || args == "status") {
        printA2dpStatus(state, out);
        return;
    }

    if (args == "stop") {
        stopA2dpSource(state, out);
        return;
    }

    if (args.startsWith("start")) {
        String target = args.length() > 5 ? trimCopy(args.substring(5)) : String("");
        if (target.isEmpty()) {
            target = state.config.a2dpTargetName;
        }
        if (target.isEmpty()) {
            out.println("A2DP: missing target; use a2dp start <bt-name>");
            return;
        }

        if (startA2dpSource(state, target, out)) {
            // Persist target name for future starts.
            saveConfig(state.config);
        }
        return;
    }

    out.println("A2DP: unknown subcommand");
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
    out.println("web-upload status|start|stop");
    out.println("a2dp status|start <bt-name>|stop");
    out.println("display info");
    out.println("launcher show");
    out.println("launcher next");
    out.println("launcher prev");
    out.println("launcher refresh");
    out.println("launcher open <settings|file-manager|music-player|notes|web-upload|apps|serial|about>");
    out.println("launcher list");
    out.println("settings show");
    out.println("oled test");
    out.println("eink test [text]");
    out.println("hw info");
    out.println("hw scan");
    out.println("hw beep [count]");
    out.println("hw rtc");
    out.println("kb read");
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

    if (line.startsWith("web-upload")) {
        String args = line.length() > 10 ? trimCopy(line.substring(10)) : String("");
        handleWebUploadCommand(state, args, out);
        return true;
    }

    if (line.startsWith("a2dp")) {
        String args = line.length() > 4 ? trimCopy(line.substring(4)) : String("");
        handleA2dpCommand(state, args, out);
        return true;
    }

    if (line == "display info") {
        out.println("--- Displays ---");
        printDisplayInfo(state, out);
        return true;
    }

    if (line.startsWith("launcher")) {
        String args = line.length() > 8 ? trimCopy(line.substring(8)) : String("");

        if (args.isEmpty() || args == "show") {
            renderLauncherScreen(state, false, out);
            return true;
        }

        if (args == "refresh" || args == "rescan") {
            invalidateLauncherSdAppCache();
            renderLauncherScreen(state, false, out);
            return true;
        }

        if (args == "list") {
            printLauncherInfo(state, out);
            return true;
        }

        if (args == "next" || args == "prev") {
            if (state.launcher.activeAppId.equalsIgnoreCase("web-upload")) {
                stopWebUploadServer(out);
            }
            const int direction = args == "next" ? 1 : -1;
            int nextIndex = static_cast<int>(state.launcher.selectedIndex) + direction;
            if (nextIndex < 0) {
                nextIndex = kLauncherAppCount - 1;
            }
            if (nextIndex >= kLauncherAppCount) {
                nextIndex = 0;
            }
            state.launcher.selectedIndex = static_cast<uint8_t>(nextIndex);
            state.launcher.activeAppId = kLauncherAppIds[state.launcher.selectedIndex];
            renderLauncherScreen(state, false, out);
            return true;
        }

        if (args.startsWith("open ")) {
            String appId = trimCopy(args.substring(5));
            if (appId.isEmpty()) {
                out.println("launcher open: missing app id");
                return true;
            }

            if (state.launcher.activeAppId.equalsIgnoreCase("web-upload") && !appId.equalsIgnoreCase("web-upload")) {
                stopWebUploadServer(out);
            }

            bool found = false;
            for (uint8_t index = 0; index < kLauncherAppCount; ++index) {
                if (appId.equalsIgnoreCase(kLauncherAppIds[index])) {
                    state.launcher.selectedIndex = index;
                    state.launcher.activeAppId = kLauncherAppIds[index];
                    found = true;
                    break;
                }
            }

            if (!found) {
                out.println("launcher open: unknown app id");
                return true;
            }

            renderActiveApp(state, false, out);
            return true;
        }

        out.println("launcher: unknown subcommand");
        return true;
    }

    if (line == "settings show") {
        if (state.launcher.activeAppId.equalsIgnoreCase("web-upload")) {
            stopWebUploadServer(out);
        }
        state.launcher.activeAppId = "settings";
        renderSettingsScreen(state, false, out);
        return true;
    }

    if (line == "oled test") {
        renderOledStatus(state, out);
        return true;
    }

    if (line.startsWith("eink test")) {
        String text = line.length() > 9 ? trimCopy(line.substring(9)) : String("E-ink test");
        renderEinkMessage(state, text, false, out);
        return true;
    }

    if (line == "hw info") {
        out.println("--- Hardware ---");
        printHardwareInfo(state, out);
        return true;
    }

    if (line == "hw scan") {
        scanI2cBus(out);
        return true;
    }

    if (line == "hw rtc") {
        printRtcNow(state, out);
        return true;
    }

    if (line.startsWith("hw beep")) {
        String arg = line.length() > 7 ? trimCopy(line.substring(7)) : String("");
        uint8_t count = 2;
        if (!arg.isEmpty()) {
            const long parsed = arg.toInt();
            if (parsed <= 0 || parsed > 10) {
                out.println("hw beep: count must be 1..10");
                return true;
            }
            count = static_cast<uint8_t>(parsed);
        }
        testBuzzer(state, out, count);
        return true;
    }

    if (line == "kb read") {
        char ch = 0;
        if (tryReadCardKb(state, ch)) {
            out.print("CardKB: 0x");
            out.print(static_cast<uint8_t>(ch), HEX);
            out.print(" '");
            if (ch >= 32 && ch <= 126) {
                out.print(ch);
            } else {
                out.print(".");
            }
            out.println("'");
        } else {
            out.println("CardKB: no key");
        }
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