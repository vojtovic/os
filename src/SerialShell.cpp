#include "SerialShell.h"

namespace {
constexpr size_t kSerialCommandBufferSize = 160;
String serialLine;
bool promptShown = false;
}  // namespace

void printPrompt(Stream &out) {
    out.print("os>");
    promptShown = true;
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
            if (executeShellCommand(state, taskManager, line, out)) {
                printPrompt(out);
            }
            continue;
        }

        if (serialLine.length() < kSerialCommandBufferSize) {
            serialLine += ch;
        }
    }
}