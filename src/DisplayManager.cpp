#include "DisplayManager.h"

#include <U8g2lib.h>
#include <LittleFS.h>
#include <SD.h>
#include <freertos/task.h>

#include "AppRouter.h"
#include "ConfigStore.h"
#include "EPD_3in52.h"
#include "epdpaint.h"
#include "SettingsConnectivityService.h"
#include "SettingsInputManager.h"
#include "WebUploadManager.h"
#include "StorageManager.h"
#include "HardwareManager.h"
#include "ShellCommands.h"

namespace {
constexpr bool kEnableEinkCadenceLogging = false;

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
    {"file-manager", "File Manager", "SD file browser"},
    {"music-player", "Music Player", "Skeleton for 2nd ESP audio"},
    {"notes", "Notes", "Quick notes in LittleFS"},
    {"web-upload", "Web Upload", "Browser file upload"},
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
constexpr uint8_t kEinkFullRefreshInterval = 7;

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
bool gSdAppCacheDirty = true;
bool gSdAppCachePrimed = false;
constexpr size_t kMaxMusicTracks = 24;
String gMusicTrackNames[kMaxMusicTracks];
String gMusicTrackPaths[kMaxMusicTracks];
size_t gMusicTrackCount = 0;
bool gMusicCacheDirty = true;
bool gMusicCachePrimed = false;
constexpr size_t kNotesMaxChars = 4096;
String gNotesBuffer;
String gNotesDraftWord;
constexpr uint8_t kNotesViewRead = 0;
constexpr uint8_t kNotesViewWrite = 1;
constexpr uint8_t kNotesViewPicker = 2;
constexpr size_t kNotesPickerMaxFiles = 20;
constexpr size_t kNotesPickerVisibleOled = 4;
String gNotesPickerFiles[kNotesPickerMaxFiles];
size_t gNotesPickerFileCount = 0;
constexpr size_t kNotesMaxTags = 16;
String gNotesTags[kNotesMaxTags];
size_t gNotesTagCount = 0;
constexpr size_t kNotesMaxLinks = 16;
String gNotesLinks[kNotesMaxLinks];
size_t gNotesLinkCount = 0;
constexpr size_t kNotesVisibleLinesOled = 4;
constexpr size_t kNotesVisibleLinesEink = 9;
size_t gNotesScrollLine = 0;
bool gNotesEinkDirty = true;
uint32_t gNotesLastEinkRenderMs = 0;
uint32_t gNotesLastViewportSignature = 0;
uint8_t gNotesScrollBatchSteps = 0;
constexpr uint8_t kNotesScrollBatchThreshold = 2;
constexpr uint32_t kNotesScrollBatchTimeoutMs = 320;
constexpr int kNotesTagFilterAll = -1;
int gNotesActiveTagIndex = kNotesTagFilterAll;
size_t gNotesCursorPos = 0;
constexpr size_t kNotesWrapWidthOled = 19;
constexpr size_t kNotesWrapWidthEink = 51;
// Undo: single-level snapshot
String gNotesUndoBuffer;
size_t gNotesUndoCursorPos = 0;
bool gNotesUndoAvailable = false;
// Picker: SD notes support
constexpr size_t kNotesPickerMaxSdFiles = 20;
String gNotesPickerSdFiles[kNotesPickerMaxSdFiles];
size_t gNotesPickerSdFileCount = 0;
// Picker: rename sub-mode
constexpr uint8_t kNotesPickerNormal = 0;
constexpr uint8_t kNotesPickerRename = 1;
String gNotesRenameBuffer;
// Auto-save
constexpr uint32_t kNotesAutoSaveIntervalMs = 30000;
uint32_t gNotesLastAutoSaveMs = 0;
// Search (Ctrl+F)
bool gNotesSearchActive = false;
String gNotesSearchQuery;
int gNotesSearchMatchPos = -1;
// --- Desktop terminal ---
constexpr size_t kTermMaxLines = 32;
constexpr size_t kTermVisibleOled = 6;
constexpr size_t kTermVisibleEink = 12;
constexpr size_t kTermMaxLineLen = 52;
String gTermLines[kTermMaxLines];
size_t gTermLineCount = 0;
size_t gTermScroll = 0;
String gTermInputBuffer;
bool gTermEinkDirty = true;
TaskManager *gTermTaskManager = nullptr;

// Stream adapter that captures output to a String
class StringStream : public Stream {
public:
    String buffer;
    size_t write(uint8_t c) override { buffer += static_cast<char>(c); return 1; }
    size_t write(const uint8_t *buf, size_t size) override {
        for (size_t i = 0; i < size; ++i) buffer += static_cast<char>(buf[i]);
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
};

void termAddLine(const String &line) {
    if (gTermLineCount < kTermMaxLines) {
        gTermLines[gTermLineCount++] = line;
    } else {
        // Shift up
        for (size_t i = 0; i < kTermMaxLines - 1; ++i) {
            gTermLines[i] = gTermLines[i + 1];
        }
        gTermLines[kTermMaxLines - 1] = line;
    }
    // Auto-scroll to bottom
    if (gTermLineCount > kTermVisibleOled) {
        gTermScroll = gTermLineCount - kTermVisibleOled;
    } else {
        gTermScroll = 0;
    }
}

void termAddOutput(const String &output) {
    // Split output into lines and add each
    size_t pos = 0;
    while (pos < output.length()) {
        int nl = output.indexOf('\n', pos);
        if (nl < 0) {
            String line = output.substring(pos);
            if (line.length() > 0) termAddLine(line);
            break;
        }
        termAddLine(output.substring(pos, nl));
        pos = nl + 1;
    }
}

uint32_t gLastDisplayActivityMs = 0;
bool gEinkSleeping = false;
bool gOledSleeping = false;
bool gOledHeldInReset = false;
String gLauncherKeyMessage = "1..6 open, down=next page";
constexpr size_t kLauncherPageSize = 6;

constexpr const char *kSettingsOptions[] = {
    "1) WiFi manager",
    "2) Bluetooth manager",
    "3) SD manager",
    "4) Save config",
};

constexpr uint8_t kSettingsOptionCount = sizeof(kSettingsOptions) / sizeof(kSettingsOptions[0]);
constexpr uint8_t kSettingsViewHome = 0;
constexpr uint8_t kSettingsViewWifiList = 1;
constexpr uint8_t kSettingsViewWifiPassword = 2;
constexpr uint8_t kSettingsViewWifiSelectList = 3;
constexpr uint8_t kSettingsViewBluetoothList = 4;
constexpr uint8_t kSettingsViewBluetoothSelectList = 5;
constexpr uint8_t kSettingsViewSdList = 6;
constexpr size_t kMaxWifiNetworks = 24;
constexpr size_t kWifiListVisibleCount = 9;
String gWifiSsidList[kMaxWifiNetworks];
String gWifiBssidList[kMaxWifiNetworks];
int32_t gWifiRssiList[kMaxWifiNetworks];
size_t gWifiCount = 0;
size_t gWifiListScrollOffset = 0;
// True while synchronous Wi-Fi scan is running.
bool gWifiScanInProgress = false;
constexpr size_t kMaxBluetoothDevices = 24;
constexpr size_t kBluetoothListVisibleCount = 9;
String gBluetoothDeviceList[kMaxBluetoothDevices];
String gBluetoothDeviceAddressList[kMaxBluetoothDevices];
size_t gBluetoothDeviceCount = 0;
size_t gBluetoothListScrollOffset = 0;
bool gBluetoothScanInProgress = false;
bool gBluetoothBleInitialized = false;
char gCzechComposeDeadKey = 0;

bool ensureEinkInitialized(SystemState &state, Stream &out);
void markDisplayActivity();
void refreshEinkWithCadence(bool forceFull = false);
bool tryWakeEinkPanel(SystemState &state, Stream &out);
void forceOledSleepOff(SystemState &state);
void forceOledWakeOn(SystemState &state);
void drawSettingsOled(const SystemState &state);
String transliterateCzechToAscii(const String &input);
String formatWifiDisplayLabel(size_t index);
bool isCardKbUpArrowCode(uint8_t code);
bool isCardKbDownArrowCode(uint8_t code);
bool isCardKbRightArrowCode(uint8_t code);
bool isCardKbLeftArrowCode(uint8_t code);
void prepareLandscapePaint(Paint &paint);
size_t utf8GlyphCount(const char *text);
String utf8ClipToGlyphs(const char *text, size_t maxGlyphs);
void invalidateMusicPlayerCache();
void resetMusicPlayerSession(SystemState &state);
void refreshMusicPlayerLibrary(const SystemState &state, Stream &out);
String musicPlayerTrackLabelAt(size_t index);
void drawMusicPlayerOled(const SystemState &state);
bool handleMusicPlayerAppInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out);
bool renderMusicPlayerScreen(SystemState &state, bool oledOnly, Stream &out);
void resetNotesSession(SystemState &state);
void notesRefreshPickerFiles(SystemState &state);
String notesPickerDisplayName(const String &path);
void notesOpenPickerSelection(SystemState &state, Stream &out);
void notesCreateNewNote(SystemState &state, Stream &out);
bool handleNotesAppInput(SystemState &state, char normalizedKey, uint8_t rawCode, bool &refreshEink, Stream &out);
bool renderNotesScreen(SystemState &state, bool oledOnly, Stream &out);
String notesTailPreview(size_t maxChars);
String notesStyledPreviewForActiveFilter(size_t maxChars);
String notesLinksSummary(size_t maxChars);
String notesActiveTagVisual(size_t maxChars);
bool isNotesEnterKey(char normalizedKey, uint8_t rawCode);
String notesLineFromEnd(const String &text, size_t fromEnd);
String notesClipLine(const String &line, size_t maxChars);
String notesComposeDisplayText(bool writeMode);
String notesComposeFilteredDisplayText(bool writeMode);
size_t notesCountLines(const String &text);
String notesLineAt(const String &text, size_t lineIndex);
size_t notesMaxScrollLine(const String &text, size_t visibleLines);
void notesScrollToTail(bool writeMode);
void notesCursorLineCol(size_t &outLine, size_t &outCol);
size_t notesLineStartOffset(size_t lineNum);
size_t notesLineEndOffset(size_t startOff);
void notesCursorVisualPos(size_t wrapWidth, size_t &outLine, size_t &outCol);
size_t notesWrappedLineCount(size_t wrapWidth);
String notesWrappedLineAt(size_t wrapWidth, size_t targetLine);
size_t notesBufferPosAtVisual(size_t wrapWidth, size_t targetLine, size_t targetCol);
void notesMoveCursorUp(size_t wrapWidth);
void notesMoveCursorDown(size_t wrapWidth);
void notesMoveCursorLeft();
void notesMoveCursorRight();
void notesInsertAtCursor(char ch);
void notesDeleteAtCursor();
void notesScrollToCursorVisual(size_t visibleLines, size_t wrapWidth);
uint32_t notesViewportSignature(const String &displayText, bool writeMode, size_t scrollLine);
uint8_t notesHeadingLevel(const String &line);
String notesStripHeadingMarkup(const String &line, uint8_t level);
bool notesFindLastHeading(const String &text, String &headingLine, uint8_t &headingLevel);
String normalizedNotesPath(SystemState &state);
bool notesLineContainsTag(const String &line, const String &tag);
String normalizeMarkdownText(const String &raw);
bool isOrderedListPrefix(const String &line);
bool saveNotes(SystemState &state, Stream &out);
void notesAutoSaveCheck(SystemState &state, Stream &out);
void notesSearchNext();
void notesInsertStringAtCursor(const String &s, SystemState &state);
void notesToggleChecklistAtCursor(SystemState &state);

bool isNotesTagChar(char ch) {
    const uint8_t u = static_cast<uint8_t>(ch);
    // Allow UTF-8 bytes so Czech/non-ASCII tags are detected from Obsidian files.
    if (u >= 0x80) {
        return true;
    }

    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '/';
}

void clearNotesTags() {
    gNotesTagCount = 0;
    for (size_t i = 0; i < kNotesMaxTags; ++i) {
        gNotesTags[i] = "";
    }
    gNotesActiveTagIndex = kNotesTagFilterAll;
}

void clearNotesLinks() {
    gNotesLinkCount = 0;
    for (size_t i = 0; i < kNotesMaxLinks; ++i) {
        gNotesLinks[i] = "";
    }
}

void addNotesTagUnique(const String &tag) {
    if (tag.length() <= 1 || gNotesTagCount >= kNotesMaxTags) {
        return;
    }

    for (size_t i = 0; i < gNotesTagCount; ++i) {
        if (gNotesTags[i].equalsIgnoreCase(tag)) {
            return;
        }
    }

    gNotesTags[gNotesTagCount++] = tag;
}

void addNotesLinkUnique(const String &link) {
    if (link.isEmpty() || gNotesLinkCount >= kNotesMaxLinks) {
        return;
    }

    for (size_t i = 0; i < gNotesLinkCount; ++i) {
        if (gNotesLinks[i].equalsIgnoreCase(link)) {
            return;
        }
    }

    gNotesLinks[gNotesLinkCount++] = link;
}

void refreshNotesTagsFromBuffer() {
    clearNotesTags();

    const size_t len = gNotesBuffer.length();
    for (size_t i = 0; i < len; ++i) {
        if (gNotesBuffer[i] != '#') {
            continue;
        }

        String tag = "#";
        size_t j = i + 1;
        while (j < len && isNotesTagChar(gNotesBuffer[j])) {
            tag += gNotesBuffer[j];
            ++j;
        }

        addNotesTagUnique(tag);
        i = j;
    }

    if (gNotesTagCount == 0) {
        gNotesActiveTagIndex = kNotesTagFilterAll;
    } else if (gNotesActiveTagIndex >= static_cast<int>(gNotesTagCount)) {
        gNotesActiveTagIndex = kNotesTagFilterAll;
    }
}

void refreshNotesLinksFromBuffer() {
    clearNotesLinks();

    const size_t len = gNotesBuffer.length();
    for (size_t i = 0; i + 1 < len; ++i) {
        if (gNotesBuffer[i] != '[' || gNotesBuffer[i + 1] != '[') {
            continue;
        }

        const size_t start = i + 2;
        size_t end = start;
        while (end + 1 < len && !(gNotesBuffer[end] == ']' && gNotesBuffer[end + 1] == ']')) {
            ++end;
        }

        if (end + 1 >= len) {
            break;
        }

        String link = gNotesBuffer.substring(start, end);
        link.trim();
        addNotesLinkUnique(link);
        i = end + 1;
    }
}

void refreshNotesMetadataFromBuffer() {
    refreshNotesTagsFromBuffer();
    refreshNotesLinksFromBuffer();
}

String notesCurrentDraftTag() {
    if (gNotesDraftWord.length() > 1 && gNotesDraftWord[0] == '#') {
        return gNotesDraftWord;
    }
    return String();
}

String notesTagsSummary(size_t maxChars) {
    String text;
    for (size_t i = 0; i < gNotesTagCount; ++i) {
        if (!text.isEmpty()) {
            text += ' ';
        }
        text += gNotesTags[i];
    }

    const String draftTag = notesCurrentDraftTag();
    if (!draftTag.isEmpty()) {
        if (!text.isEmpty()) {
            text += ' ';
        }
        text += draftTag;
    }

    if (text.isEmpty()) {
        return String("tags: (none)");
    }

    String prefixed = String("tags: ") + text;
    if (prefixed.length() <= maxChars) {
        return prefixed;
    }

    return prefixed.substring(0, maxChars - 3) + String("...");
}

String notesLinksSummary(size_t maxChars) {
    String text;
    for (size_t i = 0; i < gNotesLinkCount; ++i) {
        if (!text.isEmpty()) {
            text += " | ";
        }
        text += gNotesLinks[i];
    }

    if (text.isEmpty()) {
        return String("links: (none)");
    }

    String prefixed = String("links: ") + text;
    if (prefixed.length() <= maxChars) {
        return prefixed;
    }

    return prefixed.substring(0, maxChars - 3) + String("...");
}

String notesActiveTag() {
    if (gNotesActiveTagIndex < 0 || gNotesActiveTagIndex >= static_cast<int>(gNotesTagCount)) {
        return String();
    }
    return gNotesTags[gNotesActiveTagIndex];
}

String notesFilterLabel() {
    const String active = notesActiveTag();
    if (active.isEmpty()) {
        return String("filter: all");
    }
    return String("filter: ") + active;
}

String notesActiveTagVisual(size_t maxChars) {
    const String active = notesActiveTag();
    String text;
    if (active.isEmpty()) {
        text = String("ACTIVE TAG: ALL (") + String(static_cast<unsigned>(gNotesTagCount)) + String(")");
    } else {
        text = String("ACTIVE TAG: ") + active;
    }

    if (text.length() <= maxChars) {
        return text;
    }
    return text.substring(0, maxChars - 3) + String("...");
}

bool isNotesEnterKey(char normalizedKey, uint8_t rawCode) {
    if (normalizedKey == '\r' || normalizedKey == '\n') {
        return true;
    }

    const uint8_t stripped = static_cast<uint8_t>(rawCode & 0x7F);
    return stripped == '\r' || stripped == '\n';
}

String notesLineFromEnd(const String &text, size_t fromEnd) {
    if (text.isEmpty()) {
        return String();
    }

    int end = static_cast<int>(text.length());
    while (end > 0 && text[end - 1] == '\n') {
        --end;
    }

    if (end <= 0) {
        return String();
    }

    for (size_t i = 0; i < fromEnd; ++i) {
        const int prevBreak = text.lastIndexOf('\n', end - 1);
        if (prevBreak < 0) {
            return String();
        }
        end = prevBreak;
        while (end > 0 && text[end - 1] == '\n') {
            --end;
        }
        if (end <= 0) {
            return String();
        }
    }

    const int startBreak = text.lastIndexOf('\n', end - 1);
    const int start = startBreak < 0 ? 0 : startBreak + 1;
    return text.substring(start, end);
}

String notesClipLine(const String &line, size_t maxChars) {
    if (line.length() <= maxChars) {
        return line;
    }
    return line.substring(0, maxChars - 3) + String("...");
}

String notesComposeDisplayText(bool writeMode) {
    String text = gNotesBuffer;
    if (writeMode && !gNotesDraftWord.isEmpty()) {
        text += gNotesDraftWord;
    }
    return text;
}

String notesComposeFilteredDisplayText(bool writeMode) {
    const String text = notesComposeDisplayText(writeMode);
    if (writeMode) {
        return text;
    }

    const String active = notesActiveTag();
    if (active.isEmpty()) {
        return text;
    }

    String filtered;
    int start = 0;
    while (start <= static_cast<int>(text.length())) {
        int end = text.indexOf('\n', start);
        if (end < 0) {
            end = text.length();
        }

        const String line = text.substring(start, end);
        if (!line.isEmpty() && notesLineContainsTag(line, active)) {
            if (!filtered.isEmpty()) {
                filtered += '\n';
            }
            filtered += line;
        }

        if (end >= static_cast<int>(text.length())) {
            break;
        }
        start = end + 1;
    }

    if (filtered.isEmpty()) {
        return String("(no lines for ") + active + String(")");
    }
    return filtered;
}

size_t notesCountLines(const String &text) {
    if (text.isEmpty()) {
        return 1;
    }

    size_t lines = 1;
    for (size_t i = 0; i < text.length(); ++i) {
        if (text[i] == '\n') {
            ++lines;
        }
    }
    return lines;
}

String notesLineAt(const String &text, size_t lineIndex) {
    if (text.isEmpty()) {
        return lineIndex == 0 ? String() : String();
    }

    size_t currentLine = 0;
    int start = 0;
    const int len = static_cast<int>(text.length());
    for (int i = 0; i <= len; ++i) {
        const bool atBreak = (i == len) || (text[i] == '\n');
        if (!atBreak) {
            continue;
        }

        if (currentLine == lineIndex) {
            return text.substring(start, i);
        }

        ++currentLine;
        start = i + 1;
    }

    return String();
}

size_t notesMaxScrollLine(const String &text, size_t visibleLines) {
    const size_t totalLines = notesCountLines(text);
    if (totalLines <= visibleLines) {
        return 0;
    }
    return totalLines - visibleLines;
}

void notesScrollToTail(bool writeMode) {
    const String text = notesComposeDisplayText(writeMode);
    gNotesScrollLine = notesMaxScrollLine(text, kNotesVisibleLinesEink);
}

// --- Logical line helpers (for read mode / non-wrapped) ---
void notesCursorLineCol(size_t &outLine, size_t &outCol) {
    outLine = 0;
    outCol = 0;
    const size_t pos = min(gNotesCursorPos, gNotesBuffer.length());
    for (size_t i = 0; i < pos; ++i) {
        if (gNotesBuffer[i] == '\n') {
            ++outLine;
            outCol = 0;
        } else {
            ++outCol;
        }
    }
}

size_t notesLineStartOffset(size_t lineNum) {
    if (lineNum == 0) return 0;
    size_t count = 0;
    for (size_t i = 0; i < gNotesBuffer.length(); ++i) {
        if (gNotesBuffer[i] == '\n') {
            ++count;
            if (count == lineNum) return i + 1;
        }
    }
    return gNotesBuffer.length();
}

size_t notesLineEndOffset(size_t startOff) {
    for (size_t i = startOff; i < gNotesBuffer.length(); ++i) {
        if (gNotesBuffer[i] == '\n') return i;
    }
    return gNotesBuffer.length();
}

// --- Visual (wrapped) line helpers ---

// Word-wrap helper: given a logical line starting at bufStart up to bufEnd (exclusive),
// iterate visual lines of at most wrapWidth chars, breaking at last space when possible.
// Calls visitor(visualLineNum, lineStartBufIdx, lineEndBufIdx) for each visual line.
// Returns total visual lines produced.
template <typename Visitor>
size_t notesWrapLogicalLine(size_t wrapWidth, size_t bufStart, size_t bufEnd, size_t startVisualLine, Visitor visitor) {
    size_t visualLine = startVisualLine;
    size_t segStart = bufStart;

    while (segStart < bufEnd) {
        const size_t remaining = bufEnd - segStart;
        if (remaining <= wrapWidth) {
            visitor(visualLine, segStart, bufEnd);
            return visualLine - startVisualLine + 1;
        }
        // Find last space within wrapWidth chars
        size_t breakAt = segStart + wrapWidth;
        size_t lastSpace = 0;
        bool foundSpace = false;
        for (size_t j = segStart; j < breakAt; ++j) {
            if (gNotesBuffer[j] == ' ') {
                lastSpace = j;
                foundSpace = true;
            }
        }
        size_t lineEnd;
        if (foundSpace && lastSpace > segStart) {
            lineEnd = lastSpace + 1; // Include the space, next line starts after it
        } else {
            lineEnd = breakAt; // Character wrap fallback
        }
        visitor(visualLine, segStart, lineEnd);
        ++visualLine;
        segStart = lineEnd;
    }
    // Empty logical line
    if (segStart == bufStart) {
        visitor(visualLine, segStart, segStart);
        return 1;
    }
    return visualLine - startVisualLine;
}

// Map cursor buffer position to visual line and column with word wrapping.
void notesCursorVisualPos(size_t wrapWidth, size_t &outLine, size_t &outCol) {
    outLine = 0;
    outCol = 0;
    const size_t pos = min(gNotesCursorPos, gNotesBuffer.length());
    size_t visualLine = 0;
    size_t logStart = 0;

    for (size_t i = 0; i <= gNotesBuffer.length(); ++i) {
        if (i == gNotesBuffer.length() || gNotesBuffer[i] == '\n') {
            const size_t logEnd = i;
            notesWrapLogicalLine(wrapWidth, logStart, logEnd, visualLine,
                [&](size_t vl, size_t segStart, size_t segEnd) {
                    if (pos >= segStart && pos <= segEnd) {
                        // If pos == segEnd and this isn't the last segment of the logical line,
                        // the cursor is actually at the start of the next visual line
                        if (pos == segEnd && segEnd < logEnd) {
                            // Will be caught by next segment
                        } else {
                            outLine = vl;
                            outCol = pos - segStart;
                        }
                    }
                });
            // Count visual lines for this logical line
            size_t segStart2 = logStart;
            size_t vCount = 0;
            notesWrapLogicalLine(wrapWidth, logStart, logEnd, 0,
                [&](size_t, size_t, size_t) { ++vCount; });
            visualLine += vCount;
            logStart = i + 1;
            if (i < gNotesBuffer.length() && pos == i) {
                // Cursor at newline char — end of this logical line
                // Already handled above
            }
        }
    }
}

// Count total visual lines with word wrapping.
size_t notesWrappedLineCount(size_t wrapWidth) {
    if (gNotesBuffer.isEmpty()) return 1;
    size_t total = 0;
    size_t logStart = 0;
    for (size_t i = 0; i <= gNotesBuffer.length(); ++i) {
        if (i == gNotesBuffer.length() || gNotesBuffer[i] == '\n') {
            size_t count = 0;
            notesWrapLogicalLine(wrapWidth, logStart, i, 0,
                [&](size_t, size_t, size_t) { ++count; });
            total += count;
            logStart = i + 1;
        }
    }
    return total == 0 ? 1 : total;
}

// Get visual line content at given visual index with word wrapping.
String notesWrappedLineAt(size_t wrapWidth, size_t targetLine) {
    size_t visualLine = 0;
    size_t logStart = 0;
    String result;
    bool found = false;
    for (size_t i = 0; i <= gNotesBuffer.length() && !found; ++i) {
        if (i == gNotesBuffer.length() || gNotesBuffer[i] == '\n') {
            notesWrapLogicalLine(wrapWidth, logStart, i, visualLine,
                [&](size_t vl, size_t segStart, size_t segEnd) {
                    if (vl == targetLine) {
                        result = gNotesBuffer.substring(segStart, segEnd);
                        found = true;
                    }
                });
            size_t count = 0;
            notesWrapLogicalLine(wrapWidth, logStart, i, 0,
                [&](size_t, size_t, size_t) { ++count; });
            visualLine += count;
            logStart = i + 1;
        }
    }
    return result;
}

// Find buffer position for a given visual line and column with word wrapping.
size_t notesBufferPosAtVisual(size_t wrapWidth, size_t targetLine, size_t targetCol) {
    size_t visualLine = 0;
    size_t logStart = 0;
    size_t resultPos = gNotesBuffer.length();
    bool found = false;
    for (size_t i = 0; i <= gNotesBuffer.length() && !found; ++i) {
        if (i == gNotesBuffer.length() || gNotesBuffer[i] == '\n') {
            notesWrapLogicalLine(wrapWidth, logStart, i, visualLine,
                [&](size_t vl, size_t segStart, size_t segEnd) {
                    if (vl == targetLine) {
                        const size_t segLen = segEnd - segStart;
                        resultPos = segStart + min(targetCol, segLen);
                        found = true;
                    }
                });
            size_t count = 0;
            notesWrapLogicalLine(wrapWidth, logStart, i, 0,
                [&](size_t, size_t, size_t) { ++count; });
            visualLine += count;
            logStart = i + 1;
        }
    }
    return resultPos;
}

// --- Cursor movement (visual-line aware) ---

void notesMoveCursorUp(size_t wrapWidth) {
    size_t vLine, vCol;
    notesCursorVisualPos(wrapWidth, vLine, vCol);
    if (vLine == 0) return;
    gNotesCursorPos = notesBufferPosAtVisual(wrapWidth, vLine - 1, vCol);
}

void notesMoveCursorDown(size_t wrapWidth) {
    size_t vLine, vCol;
    notesCursorVisualPos(wrapWidth, vLine, vCol);
    const size_t totalVisual = notesWrappedLineCount(wrapWidth);
    if (vLine + 1 >= totalVisual) return;
    gNotesCursorPos = notesBufferPosAtVisual(wrapWidth, vLine + 1, vCol);
}

// --- Undo support ---
void notesSnapshotUndo() {
    gNotesUndoBuffer = gNotesBuffer;
    gNotesUndoCursorPos = gNotesCursorPos;
    gNotesUndoAvailable = true;
}

void notesRestoreUndo(SystemState &state) {
    if (!gNotesUndoAvailable) return;
    gNotesBuffer = gNotesUndoBuffer;
    gNotesCursorPos = min(gNotesUndoCursorPos, gNotesBuffer.length());
    gNotesUndoAvailable = false;
    state.notes.dirty = true;
    state.notes.statusMessage = "undo";
}

// --- Auto-save ---
void notesAutoSaveCheck(SystemState &state, Stream &out) {
    if (!state.notes.dirty) return;
    if (state.notes.viewMode == kNotesViewPicker) return;
    const uint32_t now = millis();
    if (now - gNotesLastAutoSaveMs >= kNotesAutoSaveIntervalMs) {
        saveNotes(state, out);
        gNotesLastAutoSaveMs = now;
        state.notes.statusMessage = "auto-saved";
    }
}

// --- Search ---
void notesSearchNext() {
    if (gNotesSearchQuery.length() == 0) {
        gNotesSearchMatchPos = -1;
        return;
    }
    // Search forward from cursor+1
    int startPos = static_cast<int>(gNotesCursorPos) + 1;
    if (startPos >= static_cast<int>(gNotesBuffer.length())) startPos = 0;

    int found = gNotesBuffer.indexOf(gNotesSearchQuery, startPos);
    if (found < 0 && startPos > 0) {
        // Wrap around
        found = gNotesBuffer.indexOf(gNotesSearchQuery, 0);
    }
    gNotesSearchMatchPos = found;
    if (found >= 0) {
        gNotesCursorPos = static_cast<size_t>(found);
    }
}

// --- Insert string at cursor ---
void notesInsertStringAtCursor(const String &s, SystemState &state) {
    if (gNotesBuffer.length() + s.length() > kNotesMaxChars) return;
    notesSnapshotUndo();
    for (size_t i = 0; i < s.length(); ++i) {
        notesInsertAtCursor(s.charAt(i));
    }
    state.notes.dirty = true;
}

// --- Checklist toggle ---
// In write mode, toggles the checkbox on the cursor's line.
// In read mode, toggles the checkbox on the line at gNotesScrollLine.
void notesToggleChecklistAtCursor(SystemState &state) {
    size_t lineStart;
    if (state.notes.viewMode == kNotesViewWrite) {
        lineStart = gNotesCursorPos;
        while (lineStart > 0 && gNotesBuffer.charAt(lineStart - 1) != '\n') {
            --lineStart;
        }
    } else {
        // Read mode: find start of the line at gNotesScrollLine
        lineStart = 0;
        size_t lineNum = 0;
        for (size_t i = 0; i < gNotesBuffer.length() && lineNum < gNotesScrollLine; ++i) {
            if (gNotesBuffer.charAt(i) == '\n') ++lineNum;
            if (lineNum < gNotesScrollLine) continue;
            lineStart = i + 1;
        }
        if (gNotesScrollLine > 0 && lineStart == 0) {
            // scrollLine beyond buffer
            state.notes.statusMessage = "no checkbox";
            return;
        }
    }
    const String rest = gNotesBuffer.substring(lineStart);
    if (rest.startsWith("- [ ] ")) {
        notesSnapshotUndo();
        gNotesBuffer = gNotesBuffer.substring(0, lineStart + 3) + "x" + gNotesBuffer.substring(lineStart + 4);
        state.notes.dirty = true;
        state.notes.statusMessage = "checked";
    } else if (rest.startsWith("- [x] ") || rest.startsWith("- [X] ")) {
        notesSnapshotUndo();
        gNotesBuffer = gNotesBuffer.substring(0, lineStart + 3) + " " + gNotesBuffer.substring(lineStart + 4);
        state.notes.dirty = true;
        state.notes.statusMessage = "unchecked";
    } else {
        state.notes.statusMessage = "no checkbox";
    }
}

// --- SD notes picker ---
void notesRefreshPickerSdFiles(SystemState &state) {
    gNotesPickerSdFileCount = 0;
    if (!state.sdReady) return;
    if (!SD.exists("/notes")) return;

    File dir = SD.open("/notes");
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return;
    }

    File entry;
    while ((entry = dir.openNextFile()) && gNotesPickerSdFileCount < kNotesPickerMaxSdFiles) {
        String name = String(entry.name());
        if (name.endsWith(".md") || name.endsWith(".txt")) {
            const int lastSlash = name.lastIndexOf('/');
            if (lastSlash >= 0) name = name.substring(lastSlash + 1);
            gNotesPickerSdFiles[gNotesPickerSdFileCount++] = name;
        }
        entry.close();
    }
    dir.close();
}

// Total picker file count (LittleFS + SD depending on current view)
size_t notesPickerCurrentFileCount(const SystemState &state) {
    return state.notes.pickerShowSd ? gNotesPickerSdFileCount : gNotesPickerFileCount;
}

const String &notesPickerCurrentFileName(const SystemState &state, size_t idx) {
    static const String empty;
    if (state.notes.pickerShowSd) {
        return idx < gNotesPickerSdFileCount ? gNotesPickerSdFiles[idx] : empty;
    }
    return idx < gNotesPickerFileCount ? gNotesPickerFiles[idx] : empty;
}

String notesPickerCurrentFilePath(const SystemState &state, size_t idx) {
    if (state.notes.pickerShowSd) {
        return idx < gNotesPickerSdFileCount ? String("/notes/") + gNotesPickerSdFiles[idx] : String();
    }
    return idx < gNotesPickerFileCount ? String("/notes/") + gNotesPickerFiles[idx] : String();
}

void notesMoveCursorLeft() {
    if (gNotesCursorPos > 0) --gNotesCursorPos;
}

void notesMoveCursorRight() {
    if (gNotesCursorPos < gNotesBuffer.length()) ++gNotesCursorPos;
}

void notesInsertAtCursor(char ch) {
    if (gNotesBuffer.length() >= kNotesMaxChars) return;
    if (gNotesCursorPos >= gNotesBuffer.length()) {
        gNotesBuffer += ch;
    } else {
        gNotesBuffer = gNotesBuffer.substring(0, gNotesCursorPos) + ch + gNotesBuffer.substring(gNotesCursorPos);
    }
    ++gNotesCursorPos;
}

void notesDeleteAtCursor() {
    if (gNotesCursorPos == 0 || gNotesBuffer.isEmpty()) return;
    --gNotesCursorPos;
    gNotesBuffer = gNotesBuffer.substring(0, gNotesCursorPos) + gNotesBuffer.substring(gNotesCursorPos + 1);
}

void notesScrollToCursorVisual(size_t visibleLines, size_t wrapWidth) {
    size_t vLine, vCol;
    notesCursorVisualPos(wrapWidth, vLine, vCol);
    if (vLine < gNotesScrollLine) {
        gNotesScrollLine = vLine;
    } else if (vLine >= gNotesScrollLine + visibleLines) {
        gNotesScrollLine = vLine - visibleLines + 1;
    }
}

uint32_t notesViewportSignature(const String &displayText, bool writeMode, size_t scrollLine) {
    uint32_t hash = 2166136261u;
    hash ^= static_cast<uint32_t>(writeMode ? 1 : 0);
    hash *= 16777619u;
    hash ^= static_cast<uint32_t>(scrollLine & 0xFFFFu);
    hash *= 16777619u;

    for (size_t i = 0; i < kNotesVisibleLinesEink; ++i) {
        String line = notesLineAt(displayText, scrollLine + i);
        for (size_t j = 0; j < line.length(); ++j) {
            hash ^= static_cast<uint8_t>(line[j]);
            hash *= 16777619u;
        }
        hash ^= 0xFFu;
        hash *= 16777619u;
    }

    return hash;
}

uint8_t notesHeadingLevel(const String &line) {
    String trimmed = line;
    trimmed.trim();
    if (trimmed.startsWith("###### ")) return 6;
    if (trimmed.startsWith("##### ")) return 5;
    if (trimmed.startsWith("#### ")) return 4;
    if (trimmed.startsWith("### ")) return 3;
    if (trimmed.startsWith("## ")) return 2;
    if (trimmed.startsWith("# ")) return 1;
    return 0;
}

String notesStripHeadingMarkup(const String &line, uint8_t level) {
    if (level == 0) {
        return line;
    }

    String trimmed = line;
    trimmed.trim();
    const size_t prefixLen = static_cast<size_t>(level) + 1;
    if (trimmed.length() > prefixLen) {
        return trimmed.substring(prefixLen);
    }
    return String();
}

bool notesFindLastHeading(const String &text, String &headingLine, uint8_t &headingLevel) {
    headingLine = "";
    headingLevel = 0;

    if (text.isEmpty()) {
        return false;
    }

    const size_t maxProbeLines = 200;
    for (size_t offset = 0; offset < maxProbeLines; ++offset) {
        String candidate = notesLineFromEnd(text, offset);
        if (candidate.isEmpty()) {
            break;
        }

        const uint8_t level = notesHeadingLevel(candidate);
        if (level == 0) {
            continue;
        }

        headingLevel = level;
        headingLine = notesStripHeadingMarkup(candidate, level);
        headingLine.trim();
        return true;
    }

    return false;
}

bool notesLineContainsTag(const String &line, const String &tag) {
    if (tag.isEmpty()) {
        return true;
    }

    String loweredLine = line;
    loweredLine.toLowerCase();
    String loweredTag = tag;
    loweredTag.toLowerCase();
    return loweredLine.indexOf(loweredTag) >= 0;
}

String notesPreviewForActiveFilter(size_t maxChars) {
    const String active = notesActiveTag();
    if (active.isEmpty()) {
        return notesTailPreview(maxChars);
    }

    String filtered;
    int start = 0;
    while (start <= static_cast<int>(gNotesBuffer.length())) {
        int end = gNotesBuffer.indexOf('\n', start);
        if (end < 0) {
            end = gNotesBuffer.length();
        }

        String line = gNotesBuffer.substring(start, end);
        line.trim();
        if (!line.isEmpty() && notesLineContainsTag(line, active)) {
            if (!filtered.isEmpty()) {
                filtered += " | ";
            }
            filtered += line;
            if (filtered.length() > maxChars + 12) {
                break;
            }
        }

        if (end >= static_cast<int>(gNotesBuffer.length())) {
            break;
        }
        start = end + 1;
    }

    if (filtered.isEmpty()) {
        return String("(no lines for ") + active + String(")");
    }

    if (filtered.length() <= maxChars) {
        return filtered;
    }
    return filtered.substring(0, maxChars - 3) + String("...");
}

String notesStyleLineForBrowse(String line) {
    line.trim();
    if (line.startsWith("###### ")) {
        line = String("H6 ") + line.substring(7);
    } else if (line.startsWith("##### ")) {
        line = String("H5 ") + line.substring(6);
    } else if (line.startsWith("#### ")) {
        line = String("H4 ") + line.substring(5);
    } else if (line.startsWith("### ")) {
        line = String("H3 ") + line.substring(4);
    } else if (line.startsWith("## ")) {
        line = String("H2 ") + line.substring(3);
    } else if (line.startsWith("# ")) {
        line = String("H1 ") + line.substring(2);
    } else if (line.startsWith("> [!")) {
        line = String("CALLOUT ") + line.substring(4);
    } else if (line.startsWith("> ")) {
        line = String("QUOTE ") + line.substring(2);
    } else if (line.startsWith("- [ ] ") || line.startsWith("* [ ] ") || line.startsWith("+ [ ] ")) {
        line = String("[ ] ") + line.substring(6);
    } else if (line.startsWith("- [x] ") || line.startsWith("* [x] ") || line.startsWith("+ [x] ") ||
               line.startsWith("- [X] ") || line.startsWith("* [X] ") || line.startsWith("+ [X] ")) {
        line = String("[x] ") + line.substring(6);
    } else if (isOrderedListPrefix(line)) {
        // Keep explicit ordered-list marker while compacting display.
    } else if (line.startsWith("- ") || line.startsWith("* ") || line.startsWith("+ ")) {
        line = String("* ") + line.substring(2);
    }

    if (line == "---" || line == "***" || line == "___") {
        line = "HR";
    }

    // Lightweight markdown emphasis fallback for low-resolution displays.
    line.replace("**", "");
    line.replace("__", "");
    line.replace("~~", "");
    line.replace("==", "");

    int embedStart = line.indexOf("![[");
    while (embedStart >= 0) {
        const int embedEnd = line.indexOf("]]", embedStart + 3);
        if (embedEnd < 0) {
            break;
        }

        String inside = line.substring(embedStart + 3, embedEnd);
        inside.trim();
        line = line.substring(0, embedStart) + String("embed:") + inside + line.substring(embedEnd + 2);
        embedStart = line.indexOf("![[", embedStart + 6);
    }

    int linkStart = line.indexOf("[[");
    while (linkStart >= 0) {
        const int linkEnd = line.indexOf("]]", linkStart + 2);
        if (linkEnd < 0) {
            break;
        }

        String inside = line.substring(linkStart + 2, linkEnd);
        inside.trim();
        String target = inside;
        String label = inside;
        const int pipe = inside.indexOf('|');
        if (pipe >= 0) {
            target = inside.substring(0, pipe);
            label = inside.substring(pipe + 1);
            target.trim();
            label.trim();
            if (label.isEmpty()) {
                label = target;
            }
        }
        line = line.substring(0, linkStart) + label + String("->") + target + line.substring(linkEnd + 2);
        linkStart = line.indexOf("[[", linkStart + 2);
    }

    int mdTextStart = line.indexOf('[');
    while (mdTextStart >= 0) {
        const int mdTextEnd = line.indexOf(']', mdTextStart + 1);
        if (mdTextEnd < 0 || mdTextEnd + 1 >= static_cast<int>(line.length()) || line[mdTextEnd + 1] != '(') {
            break;
        }

        const int mdUrlEnd = line.indexOf(')', mdTextEnd + 2);
        if (mdUrlEnd < 0) {
            break;
        }

        const String text = line.substring(mdTextStart + 1, mdTextEnd);
        const String url = line.substring(mdTextEnd + 2, mdUrlEnd);
        line = line.substring(0, mdTextStart) + text + String("->") + url + line.substring(mdUrlEnd + 1);
        mdTextStart = line.indexOf('[', mdTextStart + 2);
    }

    return line;
}

bool isOrderedListPrefix(const String &line) {
    if (line.length() < 3) {
        return false;
    }

    size_t i = 0;
    while (i < line.length() && line[i] >= '0' && line[i] <= '9') {
        ++i;
    }

    if (i == 0 || i + 1 >= line.length()) {
        return false;
    }

    const char marker = line[i];
    return (marker == '.' || marker == ')') && line[i + 1] == ' ';
}

String notesStyledPreviewForActiveFilter(size_t maxChars) {
    const String active = notesActiveTag();
    String filtered;
    bool inCodeBlock = false;
    int start = 0;
    while (start <= static_cast<int>(gNotesBuffer.length())) {
        int end = gNotesBuffer.indexOf('\n', start);
        if (end < 0) {
            end = gNotesBuffer.length();
        }

        String line = gNotesBuffer.substring(start, end);
        line.trim();
        if (!line.isEmpty() && (active.isEmpty() || notesLineContainsTag(line, active))) {
            if (!filtered.isEmpty()) {
                filtered += " | ";
            }

            if (line.startsWith("```") || line.startsWith("~~~")) {
                inCodeBlock = !inCodeBlock;
                filtered += inCodeBlock ? String("CODE BLOCK START") : String("CODE BLOCK END");
            } else if (inCodeBlock) {
                filtered += String("` ") + line;
            } else {
                filtered += notesStyleLineForBrowse(line);
            }

            if (filtered.length() > maxChars + 12) {
                break;
            }
        }

        if (end >= static_cast<int>(gNotesBuffer.length())) {
            break;
        }
        start = end + 1;
    }

    if (filtered.isEmpty()) {
        return active.isEmpty() ? String("(empty)") : String("(no lines for ") + active + String(")");
    }

    if (filtered.length() <= maxChars) {
        return filtered;
    }
    return filtered.substring(0, maxChars - 3) + String("...");
}

size_t notesTotalChars() {
    return gNotesBuffer.length() + gNotesDraftWord.length();
}

bool commitNotesDraftWord(SystemState &state, bool withTrailingSpace) {
    if (gNotesDraftWord.isEmpty()) {
        return false;
    }

    const size_t extra = gNotesDraftWord.length() + (withTrailingSpace ? 1 : 0);
    if (gNotesBuffer.length() + extra > kNotesMaxChars) {
        state.notes.statusMessage = "note full";
        return false;
    }

    gNotesBuffer += gNotesDraftWord;
    if (withTrailingSpace) {
        gNotesBuffer += ' ';
    }
    gNotesDraftWord = "";
    state.notes.dirty = true;
    refreshNotesMetadataFromBuffer();
    return true;
}

bool ensureNotesLoaded(SystemState &state, Stream &out) {
    if (state.notes.loaded) {
        return true;
    }

    state.notes.loaded = true;
    gNotesBuffer = "";
    gNotesDraftWord = "";
    gNotesUndoAvailable = false;
    clearNotesTags();
    clearNotesLinks();

    const bool useSd = state.notes.pickerShowSd;
    fs::FS &fs = useSd ? static_cast<fs::FS &>(SD) : static_cast<fs::FS &>(LittleFS);

    if (!useSd && !state.littleFsReady) {
        state.notes.statusMessage = "LittleFS off";
        return false;
    }
    if (useSd && !state.sdReady) {
        state.notes.statusMessage = "SD not ready";
        return false;
    }

    String path = normalizedNotesPath(state);
    const String legacyPath = "/notes/quicknote.txt";

    if (!fs.exists(path)) {
        if (!useSd && path == "/notes/quicknote.md" && fs.exists(legacyPath)) {
            path = legacyPath;
            state.notes.statusMessage = "legacy txt loaded";
        } else {
            state.notes.statusMessage = "new note";
            state.notes.dirty = false;
            return true;
        }
    }

    File file = fs.open(path, "r");
    if (!file) {
        state.notes.statusMessage = "open failed";
        return false;
    }

    gNotesBuffer.reserve(kNotesMaxChars + 1);
    while (file.available() && gNotesBuffer.length() < (kNotesMaxChars * 2)) {
        gNotesBuffer += static_cast<char>(file.read());
    }
    file.close();

    gNotesBuffer = normalizeMarkdownText(gNotesBuffer);
    if (gNotesBuffer.length() > kNotesMaxChars) {
        gNotesBuffer = gNotesBuffer.substring(0, kNotesMaxChars);
    }

    state.notes.statusMessage = "loaded";
    state.notes.dirty = false;
    gNotesCursorPos = gNotesBuffer.length();
    refreshNotesMetadataFromBuffer();
    notesScrollToTail(state.notes.viewMode == kNotesViewWrite);
    gNotesEinkDirty = true;
    return true;
}

bool saveNotes(SystemState &state, Stream &out) {
    const bool useSd = state.notes.pickerShowSd;
    fs::FS &fs = useSd ? static_cast<fs::FS &>(SD) : static_cast<fs::FS &>(LittleFS);

    if (!useSd && !state.littleFsReady) {
        state.notes.statusMessage = "LittleFS off";
        return false;
    }
    if (useSd && !state.sdReady) {
        state.notes.statusMessage = "SD not ready";
        return false;
    }

    String path = normalizedNotesPath(state);

    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!fs.exists(dir)) {
            fs.mkdir(dir);
        }
    }

    File file = fs.open(path, "w");
    if (!file) {
        state.notes.statusMessage = "save failed";
        return false;
    }

    file.print(normalizeMarkdownText(gNotesBuffer));
    file.close();
    state.notes.dirty = false;
    state.notes.statusMessage = "saved";
    gNotesEinkDirty = true;
    out.print("Notes: saved ");
    out.println(path);
    return true;
}

String normalizedNotesPath(SystemState &state) {
    String path = state.notes.filePath;
    path.trim();
    if (path.isEmpty() || path.equalsIgnoreCase("/notes/quicknote.txt")) {
        path = "/notes/quicknote.md";
    }

    if (!path.startsWith("/")) {
        path = String("/") + path;
    }

    state.notes.filePath = path;
    return path;
}

String normalizeMarkdownText(const String &raw) {
    String text = raw;
    if (text.length() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB && static_cast<uint8_t>(text[2]) == 0xBF) {
        text = text.substring(3);
    }

    String normalized;
    normalized.reserve(text.length());
    for (size_t i = 0; i < text.length(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            if (i + 1 < text.length() && text[i + 1] == '\n') {
                continue;
            }
            normalized += '\n';
            continue;
        }
        normalized += c;
    }

    return normalized;
}

String notesTailPreview(size_t maxChars) {
    if (gNotesBuffer.isEmpty()) {
        return String("(empty)");
    }

    if (gNotesBuffer.length() <= maxChars) {
        return gNotesBuffer;
    }

    return String("...") + gNotesBuffer.substring(gNotesBuffer.length() - maxChars);
}

size_t utf8GlyphCount(const char *text) {
    if (!text) {
        return 0;
    }

    size_t glyphs = 0;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(text);
    while (*p != 0) {
        if ((*p & 0x80) == 0x00) {
            ++p;
        } else if ((*p & 0xE0) == 0xC0 && p[1] != 0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
            p += 4;
        } else {
            ++p;
        }
        ++glyphs;
    }
    return glyphs;
}

String utf8ClipToGlyphs(const char *text, size_t maxGlyphs) {
    if (!text || maxGlyphs == 0) {
        return String();
    }

    const uint8_t *start = reinterpret_cast<const uint8_t *>(text);
    const uint8_t *p = start;
    size_t glyphs = 0;

    while (*p != 0 && glyphs < maxGlyphs) {
        if ((*p & 0x80) == 0x00) {
            ++p;
        } else if ((*p & 0xE0) == 0xC0 && p[1] != 0) {
            p += 2;
        } else if ((*p & 0xF0) == 0xE0 && p[1] != 0 && p[2] != 0) {
            p += 3;
        } else if ((*p & 0xF8) == 0xF0 && p[1] != 0 && p[2] != 0 && p[3] != 0) {
            p += 4;
        } else {
            ++p;
        }
        ++glyphs;
    }

    const size_t byteLen = static_cast<size_t>(p - start);
    return String(reinterpret_cast<const char *>(start)).substring(0, byteLen);
}

String formatWifiDisplayLabel(size_t index) {
    String ssid = gWifiSsidList[index];
    if (!ssid.isEmpty()) {
        return ssid;
    }

    String label = "skrytá síť ";
    label += gWifiBssidList[index];
    return label;
}

bool isCardKbUpArrowCode(uint8_t code) {
    return code == 0x93 || code == 0x95 || code == 0xB3 || code == 0xB5;
}

bool isCardKbDownArrowCode(uint8_t code) {
    return code == 0x96 || code == 0xB6;
}

bool isCardKbRightArrowCode(uint8_t code) {
    return code == 0x97 || code == 0x98 || code == 0xB7 || code == 0xB8;
}

constexpr uint8_t kFileManagerViewBrowse = 0;
constexpr uint8_t kFileManagerViewItemMenu = 1;
constexpr uint8_t kFileManagerViewDirectoryMenu = 2;
constexpr uint8_t kFileManagerViewCreateFolder = 3;
constexpr uint8_t kFileManagerViewRename = 4;
constexpr uint8_t kFileManagerViewPreview = 5;
constexpr size_t kFileManagerMaxEntries = 24;
constexpr size_t kFileManagerPageSize = 6;
constexpr size_t kFileManagerPreviewMaxChars = 512;
String gFileManagerPreviewBuffer;

String gFileManagerCachedPath;
bool gFileManagerCacheDirty = true;
String gFileManagerEntryNames[kFileManagerMaxEntries];
String gFileManagerEntryPaths[kFileManagerMaxEntries];
bool gFileManagerEntryIsDir[kFileManagerMaxEntries];
uint32_t gFileManagerEntrySizes[kFileManagerMaxEntries];
size_t gFileManagerEntryCount = 0;

String fileManagerFormatSize(uint32_t bytes);

String fileManagerBaseName(const String &path) {
    int slash = path.lastIndexOf('/');
    if (slash < 0) {
        slash = path.lastIndexOf('\\');
    }
    if (slash < 0) {
        return path;
    }
    return path.substring(slash + 1);
}

String fileManagerNormalizePath(String path) {
    path.trim();
    if (path.isEmpty()) {
        return String("/");
    }

    path.replace('\\', '/');
    if (!path.startsWith("/")) {
        path = String("/") + path;
    }

    while (path.length() > 1 && path.endsWith("/")) {
        path.remove(path.length() - 1);
    }

    return path;
}

String fileManagerParentPath(const String &path) {
    String normalized = fileManagerNormalizePath(path);
    if (normalized == "/") {
        return normalized;
    }

    int slash = normalized.lastIndexOf('/');
    if (slash <= 0) {
        return String("/");
    }

    return normalized.substring(0, slash);
}

String fileManagerJoinPath(const String &parentPath, const String &childName) {
    String child = childName;
    child.trim();
    if (child.isEmpty()) {
        return fileManagerNormalizePath(parentPath);
    }

    if (child.startsWith("/")) {
        return fileManagerNormalizePath(child);
    }

    if (!parentPath.isEmpty() && child.startsWith(parentPath)) {
        return fileManagerNormalizePath(child);
    }

    String joined = fileManagerNormalizePath(parentPath);
    if (joined == "/") {
        joined += child;
    } else {
        joined += "/";
        joined += child;
    }
    return fileManagerNormalizePath(joined);
}

fs::FS &fileManagerActiveFs(const SystemState &state) {
    return state.fileManager.browseLittleFs
        ? static_cast<fs::FS &>(LittleFS) : static_cast<fs::FS &>(SD);
}

bool fileManagerFsReady(const SystemState &state) {
    return state.fileManager.browseLittleFs ? state.littleFsReady : state.sdReady;
}

bool fileManagerPathExists(const SystemState &state, const String &path) {
    return fileManagerActiveFs(state).exists(fileManagerNormalizePath(path));
}

void invalidateFileManagerCache() {
    gFileManagerCacheDirty = true;
    gFileManagerCachedPath = "";
}

void fileManagerSetStatus(SystemState &state, const String &message) {
    state.fileManager.statusMessage = message;
}

void fileManagerResetBrowseSelection(SystemState &state) {
    state.fileManager.selectedIndex = 0;
    state.fileManager.scrollOffset = 0;
    state.fileManager.menuIndex = 0;
}

bool fileManagerIsInsideSourcePath(const String &sourcePath, const String &destinationPath) {
    if (destinationPath == sourcePath) {
        return true;
    }

    String sourcePrefix = sourcePath;
    if (!sourcePrefix.endsWith("/")) {
        sourcePrefix += "/";
    }
    return destinationPath.startsWith(sourcePrefix);
}

String fileManagerUniqueChildPath(const SystemState &state, const String &directoryPath, const String &baseName) {
    String candidate = fileManagerJoinPath(directoryPath, baseName);
    if (!fileManagerPathExists(state, candidate)) {
        return candidate;
    }

    String stem = baseName;
    String extension;
    int dot = baseName.lastIndexOf('.');
    if (dot > 0 && dot < static_cast<int>(baseName.length()) - 1) {
        stem = baseName.substring(0, dot);
        extension = baseName.substring(dot);
    }

    for (uint8_t suffix = 2; suffix < 100; ++suffix) {
        String name = stem + String("_copy");
        if (suffix > 2) {
            name += String(suffix);
        }
        name += extension;
        candidate = fileManagerJoinPath(directoryPath, name);
        if (!fileManagerPathExists(state, candidate)) {
            return candidate;
        }
    }

    return String();
}

bool fileManagerDeleteRecursive(const SystemState &state, const String &path, Stream &out);
bool fileManagerCopyRecursive(const SystemState &state, const String &sourcePath, const String &destinationPath, Stream &out);

size_t fileManagerRefreshListing(SystemState &state, Stream &out) {
    if (!fileManagerFsReady(state)) {
        gFileManagerEntryCount = 0;
        fileManagerSetStatus(state, state.fileManager.browseLittleFs ? "LittleFS off" : "SD not ready");
        return 0;
    }

    const String currentPath = fileManagerNormalizePath(state.fileManager.currentPath);
    if (!gFileManagerCacheDirty && gFileManagerCachedPath == currentPath) {
        return gFileManagerEntryCount;
    }

    gFileManagerCachedPath = currentPath;
    gFileManagerEntryCount = 0;

    fs::FS &fs = fileManagerActiveFs(state);
    File dir = fs.open(currentPath, FILE_READ);
    if (!dir || !dir.isDirectory()) {
        fileManagerSetStatus(state, String("open failed: ") + currentPath);
        gFileManagerCacheDirty = false;
        return 0;
    }

    if (currentPath != "/") {
        gFileManagerEntryNames[gFileManagerEntryCount] = "..";
        gFileManagerEntryPaths[gFileManagerEntryCount] = fileManagerParentPath(currentPath);
        gFileManagerEntryIsDir[gFileManagerEntryCount] = true;
        gFileManagerEntrySizes[gFileManagerEntryCount] = 0;
        ++gFileManagerEntryCount;
    }

    File entry = dir.openNextFile();
    while (entry && gFileManagerEntryCount < kFileManagerMaxEntries) {
        String entryName = entry.name();
        gFileManagerEntryNames[gFileManagerEntryCount] = fileManagerBaseName(entryName);
        gFileManagerEntryPaths[gFileManagerEntryCount] = fileManagerJoinPath(currentPath, entryName);
        gFileManagerEntryIsDir[gFileManagerEntryCount] = entry.isDirectory();
        gFileManagerEntrySizes[gFileManagerEntryCount] = static_cast<uint32_t>(entry.size());
        ++gFileManagerEntryCount;
        entry = dir.openNextFile();
    }

    dir.close();

    // Sort entries: ".." stays first, then dirs alphabetically, then files alphabetically
    const size_t sortStart = (currentPath != "/" && gFileManagerEntryCount > 0 && gFileManagerEntryNames[0] == "..") ? 1 : 0;
    if (gFileManagerEntryCount > sortStart + 1) {
        for (size_t i = sortStart; i < gFileManagerEntryCount - 1; ++i) {
            for (size_t j = i + 1; j < gFileManagerEntryCount; ++j) {
                bool swap = false;
                if (gFileManagerEntryIsDir[i] == gFileManagerEntryIsDir[j]) {
                    // Same type: alphabetical (case-insensitive)
                    String a = gFileManagerEntryNames[i];
                    String b = gFileManagerEntryNames[j];
                    a.toLowerCase();
                    b.toLowerCase();
                    swap = a > b;
                } else {
                    // Dirs before files
                    swap = !gFileManagerEntryIsDir[i] && gFileManagerEntryIsDir[j];
                }
                if (swap) {
                    std::swap(gFileManagerEntryNames[i], gFileManagerEntryNames[j]);
                    std::swap(gFileManagerEntryPaths[i], gFileManagerEntryPaths[j]);
                    std::swap(gFileManagerEntryIsDir[i], gFileManagerEntryIsDir[j]);
                    std::swap(gFileManagerEntrySizes[i], gFileManagerEntrySizes[j]);
                }
            }
        }
    }

    gFileManagerCacheDirty = false;
    if (gFileManagerEntryCount == 0) {
        fileManagerSetStatus(state, String("empty: ") + currentPath);
    }
    return gFileManagerEntryCount;
}

bool fileManagerCreateFolder(SystemState &state, const String &folderName, Stream &out) {
    String cleanName = folderName;
    cleanName.trim();
    if (cleanName.isEmpty()) {
        fileManagerSetStatus(state, "folder name empty");
        return false;
    }

    cleanName.replace('/', '_');
    cleanName.replace('\\', '_');
    const String targetPath = fileManagerJoinPath(state.fileManager.currentPath, cleanName);
    if (fileManagerPathExists(state, targetPath)) {
        fileManagerSetStatus(state, "folder exists");
        return false;
    }

    if (!fileManagerActiveFs(state).mkdir(targetPath)) {
        fileManagerSetStatus(state, "mkdir failed");
        out.print("File manager: mkdir failed ");
        out.println(targetPath);
        return false;
    }

    invalidateFileManagerCache();
    fileManagerSetStatus(state, String("created ") + cleanName);
    return true;
}

bool fileManagerDeleteRecursive(const SystemState &state, const String &path, Stream &out) {
    const String normalized = fileManagerNormalizePath(path);
    if (normalized == "/") {
        out.println("File manager: refusing to delete root");
        return false;
    }

    fs::FS &fs = fileManagerActiveFs(state);
    File entry = fs.open(normalized, FILE_READ);
    if (!entry) {
        out.print("File manager: delete open failed ");
        out.println(normalized);
        return false;
    }

    if (!entry.isDirectory()) {
        entry.close();
        if (!fs.remove(normalized)) {
            out.print("File manager: remove failed ");
            out.println(normalized);
            return false;
        }
        return true;
    }

    File child = entry.openNextFile();
    while (child) {
        const String childPath = fileManagerJoinPath(normalized, child.name());
        child.close();
        if (!fileManagerDeleteRecursive(state, childPath, out)) {
            entry.close();
            return false;
        }
        child = entry.openNextFile();
    }

    entry.close();
    if (!fs.rmdir(normalized)) {
        out.print("File manager: rmdir failed ");
        out.println(normalized);
        return false;
    }

    return true;
}

bool fileManagerCopyRecursive(const SystemState &state, const String &sourcePath, const String &destinationPath, Stream &out) {
    const String normalizedSource = fileManagerNormalizePath(sourcePath);
    const String normalizedDestination = fileManagerNormalizePath(destinationPath);

    if (fileManagerIsInsideSourcePath(normalizedSource, normalizedDestination)) {
        out.println("File manager: invalid copy target");
        return false;
    }

    fs::FS &fs = fileManagerActiveFs(state);
    File source = fs.open(normalizedSource, FILE_READ);
    if (!source) {
        out.print("File manager: copy open failed ");
        out.println(normalizedSource);
        return false;
    }

    if (!source.isDirectory()) {
        File destination = fs.open(normalizedDestination, FILE_WRITE);
        if (!destination) {
            source.close();
            out.print("File manager: copy create failed ");
            out.println(normalizedDestination);
            return false;
        }

        uint8_t buffer[512];
        while (source.available()) {
            const size_t readCount = source.read(buffer, sizeof(buffer));
            if (readCount == 0) {
                break;
            }
            if (destination.write(buffer, readCount) != readCount) {
                destination.close();
                source.close();
                out.print("File manager: copy write failed ");
                out.println(normalizedDestination);
                return false;
            }
        }

        destination.close();
        source.close();
        return true;
    }

    if (!fs.mkdir(normalizedDestination)) {
        source.close();
        out.print("File manager: mkdir copy failed ");
        out.println(normalizedDestination);
        return false;
    }

    File child = source.openNextFile();
    while (child) {
        const String childSource = fileManagerJoinPath(normalizedSource, child.name());
        const String childDestination = fileManagerJoinPath(normalizedDestination, fileManagerBaseName(child.name()));
        child.close();
        if (!fileManagerCopyRecursive(state, childSource, childDestination, out)) {
            source.close();
            return false;
        }
        child = source.openNextFile();
    }

    source.close();
    return true;
}

bool fileManagerPasteClipboard(SystemState &state, Stream &out) {
    if (!state.fileManager.clipboardActive || state.fileManager.clipboardPath.isEmpty()) {
        fileManagerSetStatus(state, "clipboard empty");
        return false;
    }

    const String sourcePath = fileManagerNormalizePath(state.fileManager.clipboardPath);
    const String sourceName = fileManagerBaseName(sourcePath);
    const String targetDir = fileManagerNormalizePath(state.fileManager.currentPath);
    const String targetPath = state.fileManager.clipboardMove
        ? fileManagerJoinPath(targetDir, sourceName)
        : fileManagerUniqueChildPath(state, targetDir, sourceName);

    if (targetPath.isEmpty()) {
        fileManagerSetStatus(state, "no free name");
        return false;
    }

    if (state.fileManager.clipboardMove) {
        if (targetPath == sourcePath) {
            fileManagerSetStatus(state, "already here");
            state.fileManager.clipboardActive = false;
            state.fileManager.clipboardPath = "";
            state.fileManager.clipboardMove = false;
            return true;
        }

        if (fileManagerPathExists(state, targetPath)) {
            fileManagerSetStatus(state, "target exists");
            return false;
        }
    }

    if (!fileManagerCopyRecursive(state, sourcePath, targetPath, out)) {
        fileManagerSetStatus(state, "paste failed");
        return false;
    }

    if (state.fileManager.clipboardMove) {
        if (!fileManagerDeleteRecursive(state, sourcePath, out)) {
            fileManagerSetStatus(state, "move cleanup failed");
            return false;
        }
    }

    state.fileManager.clipboardActive = false;
    state.fileManager.clipboardPath = "";
    state.fileManager.clipboardMove = false;
    invalidateFileManagerCache();
    fileManagerSetStatus(state, String("pasted to ") + targetDir);
    return true;
}

String fileManagerItemDisplayName(size_t index) {
    if (index >= gFileManagerEntryCount) {
        return String();
    }
    return gFileManagerEntryNames[index];
}

String fileManagerItemPath(size_t index) {
    if (index >= gFileManagerEntryCount) {
        return String();
    }
    return gFileManagerEntryPaths[index];
}

String fileManagerCurrentSelectionLabel(const SystemState &state) {
    if (state.fileManager.selectedIndex >= gFileManagerEntryCount) {
        return String();
    }

    String label = gFileManagerEntryIsDir[state.fileManager.selectedIndex] ? "[D] " : "[F] ";
    label += fileManagerItemDisplayName(state.fileManager.selectedIndex);
    return label;
}

void drawFileManagerOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    gOled.clearBuffer();
    gOled.setFont(u8g2_font_4x6_tf);

    const String fsTag = state.fileManager.browseLittleFs ? String("[FS] ") : String("[SD] ");
    gOled.drawStr(0, 7, (fsTag + String("FILE MANAGER")).c_str());
    gOled.setCursor(0, 14);
    gOled.print(state.fileManager.currentPath);

    if (state.fileManager.viewMode == kFileManagerViewCreateFolder) {
        gOled.drawStr(0, 24, "new folder");
        gOled.setCursor(0, 35);
        gOled.print("name: ");
        gOled.print(state.fileManager.pendingFolderName);
        gOled.print('_');
        gOled.drawStr(0, 48, "enter save");
        gOled.drawStr(0, 58, "<- cancel");
        gOled.sendBuffer();
        return;
    }

    if (state.fileManager.viewMode == kFileManagerViewRename) {
        gOled.drawStr(0, 24, "rename");
        gOled.setCursor(0, 35);
        gOled.print("name: ");
        gOled.print(state.fileManager.pendingRenameName);
        gOled.print('_');
        gOled.drawStr(0, 48, "enter save");
        gOled.drawStr(0, 58, "<- cancel");
        gOled.sendBuffer();
        return;
    }

    if (state.fileManager.viewMode == kFileManagerViewPreview) {
        gOled.drawStr(0, 24, "preview");
        // Show first ~4 lines of preview
        int y = 34;
        size_t pos = 0;
        for (int i = 0; i < 4 && pos < gFileManagerPreviewBuffer.length(); ++i) {
            int nl = gFileManagerPreviewBuffer.indexOf('\n', pos);
            String line;
            if (nl < 0 || nl > static_cast<int>(pos + 30)) {
                line = gFileManagerPreviewBuffer.substring(pos, min(pos + 30, gFileManagerPreviewBuffer.length()));
                pos = (nl >= 0) ? nl + 1 : gFileManagerPreviewBuffer.length();
            } else {
                line = gFileManagerPreviewBuffer.substring(pos, nl);
                pos = nl + 1;
            }
            gOled.drawStr(0, y, line.c_str());
            y += 8;
        }
        gOled.drawStr(0, 64, "any key=back");
        gOled.sendBuffer();
        return;
    }

    if (state.fileManager.viewMode == kFileManagerViewDirectoryMenu) {
        gOled.drawStr(0, 24, "directory actions");
        gOled.drawStr(0, 46, state.fileManager.menuIndex == 0 ? "> 1 new folder" : "  1 new folder");
        gOled.drawStr(0, 56, state.fileManager.menuIndex == 1 ? "> 2 refresh" : "  2 refresh");
        gOled.drawStr(80, 56, "<- cancel");
        gOled.sendBuffer();
        return;
    }

    if (state.fileManager.viewMode == kFileManagerViewItemMenu) {
        const String targetLabel = fileManagerItemDisplayName(state.fileManager.selectedIndex);
        const bool isDir = gFileManagerEntryIsDir[state.fileManager.selectedIndex];
        gOled.drawStr(0, 24, isDir ? "folder menu" : "file menu");
        gOled.setCursor(0, 33);
        gOled.print(targetLabel);
        if (isDir) {
            gOled.drawStr(0, 42, state.fileManager.menuIndex == 0 ? ">open" : " open");
            gOled.drawStr(30, 42, state.fileManager.menuIndex == 1 ? ">ren" : " ren");
            gOled.drawStr(55, 42, state.fileManager.menuIndex == 2 ? ">del" : " del");
            gOled.drawStr(0, 52, state.fileManager.menuIndex == 3 ? ">copy" : " copy");
            gOled.drawStr(30, 52, state.fileManager.menuIndex == 4 ? ">move" : " move");
        } else {
            gOled.drawStr(0, 42, state.fileManager.menuIndex == 0 ? ">ren" : " ren");
            gOled.drawStr(25, 42, state.fileManager.menuIndex == 1 ? ">del" : " del");
            gOled.drawStr(50, 42, state.fileManager.menuIndex == 2 ? ">copy" : " copy");
            gOled.drawStr(80, 42, state.fileManager.menuIndex == 3 ? ">move" : " move");
        }
        gOled.drawStr(0, 64, "<- cancel");
        gOled.sendBuffer();
        return;
    }

    // --- Browse view ---
    const size_t totalCount = gFileManagerEntryCount;
    const size_t startIndex = min(state.fileManager.scrollOffset, totalCount > kFileManagerPageSize ? totalCount - kFileManagerPageSize : size_t(0));
    const size_t endIndex = min(startIndex + kFileManagerPageSize, totalCount);

    gOled.setCursor(0, 22);
    gOled.print(totalCount);
    gOled.print(" items");

    int y = 32;
    for (size_t index = startIndex; index < endIndex; ++index) {
        const bool selected = index == state.fileManager.selectedIndex;
        String line = selected ? ">" : " ";
        line += gFileManagerEntryIsDir[index] ? "[D]" : "[F]";
        line += fileManagerItemDisplayName(index);
        // Truncate name to make room for size
        if (!gFileManagerEntryIsDir[index] && gFileManagerEntrySizes[index] > 0) {
            String sizeStr = fileManagerFormatSize(gFileManagerEntrySizes[index]);
            const size_t nameMaxLen = 26 - sizeStr.length();
            if (line.length() > nameMaxLen) {
                line = line.substring(0, nameMaxLen);
            }
            while (line.length() < nameMaxLen) line += " ";
            line += sizeStr;
        }
        gOled.drawStr(0, y, line.c_str());
        y += 8;
    }

    gOled.setCursor(0, 64);
    gOled.print(state.fileManager.clipboardActive ? "right=paste " : "s=FS p=prev ");
    gOled.print(state.fileManager.statusMessage);
    gOled.sendBuffer();
}

void drawFileManagerEink(const SystemState &state) {
    Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
    prepareLandscapePaint(paint);
    paint.Clear(kUncolored);
    paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);

    const String fsTag = state.fileManager.browseLittleFs ? String("[LittleFS]") : String("[SD]");
    String title = fsTag + String(" FILE MANAGER");
    paint.DrawStringAt(8, 10, title.c_str(), &Font16, kColored);
    paint.DrawStringAtUtf8(8, 26, state.fileManager.currentPath.c_str(), &Font12, kColored);

    if (state.fileManager.viewMode == kFileManagerViewCreateFolder) {
        paint.DrawStringAt(8, 48, "new folder", &Font12, kColored);
        String nameDisp = String("name: ") + state.fileManager.pendingFolderName + "_";
        paint.DrawStringAtUtf8(8, 66, nameDisp.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 84, "enter=save  left=cancel", &Font12, kColored);
    } else if (state.fileManager.viewMode == kFileManagerViewRename) {
        paint.DrawStringAt(8, 48, "rename", &Font12, kColored);
        String nameDisp = String("name: ") + state.fileManager.pendingRenameName + "_";
        paint.DrawStringAtUtf8(8, 66, nameDisp.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 84, "enter=save  left/esc=cancel", &Font12, kColored);
    } else if (state.fileManager.viewMode == kFileManagerViewPreview) {
        paint.DrawStringAt(8, 48, "preview", &Font16, kColored);
        paint.DrawStringAtUtf8(8, 66, state.fileManager.statusMessage.c_str(), &Font12, kColored);
        int y = 84;
        size_t pos = 0;
        for (int i = 0; i < 8 && pos < gFileManagerPreviewBuffer.length(); ++i) {
            int nl = gFileManagerPreviewBuffer.indexOf('\n', pos);
            String line;
            if (nl < 0 || nl > static_cast<int>(pos + 52)) {
                line = gFileManagerPreviewBuffer.substring(pos, min(pos + 52, gFileManagerPreviewBuffer.length()));
                pos = (nl >= 0) ? static_cast<size_t>(nl + 1) : gFileManagerPreviewBuffer.length();
            } else {
                line = gFileManagerPreviewBuffer.substring(pos, nl);
                pos = nl + 1;
            }
            paint.DrawStringAtUtf8(8, y, line.c_str(), &Font12, kColored);
            y += 14;
        }
        paint.DrawStringAt(8, 218, "any key = back to browse", &Font12, kColored);
    } else if (state.fileManager.viewMode == kFileManagerViewDirectoryMenu) {
        paint.DrawStringAt(8, 48, "directory actions", &Font12, kColored);
        paint.DrawStringAt(8, 84, state.fileManager.menuIndex == 0 ? "> 1 new folder" : "  1 new folder", &Font12, kColored);
        paint.DrawStringAt(8, 100, state.fileManager.menuIndex == 1 ? "> 2 refresh" : "  2 refresh", &Font12, kColored);
        paint.DrawStringAt(8, 118, "left=cancel", &Font12, kColored);
    } else if (state.fileManager.viewMode == kFileManagerViewItemMenu) {
        const bool isDir = gFileManagerEntryIsDir[state.fileManager.selectedIndex];
        paint.DrawStringAt(8, 48, isDir ? "folder menu" : "file menu", &Font12, kColored);
        paint.DrawStringAtUtf8(8, 66, fileManagerItemDisplayName(state.fileManager.selectedIndex).c_str(), &Font12, kColored);
        if (isDir) {
            paint.DrawStringAt(8, 84, state.fileManager.menuIndex == 0 ? "> 1 open" : "  1 open", &Font12, kColored);
            paint.DrawStringAt(8, 100, state.fileManager.menuIndex == 1 ? "> 2 rename" : "  2 rename", &Font12, kColored);
            paint.DrawStringAt(8, 116, state.fileManager.menuIndex == 2 ? "> 3 delete" : "  3 delete", &Font12, kColored);
            paint.DrawStringAt(140, 84, state.fileManager.menuIndex == 3 ? "> 4 copy" : "  4 copy", &Font12, kColored);
            paint.DrawStringAt(140, 100, state.fileManager.menuIndex == 4 ? "> 5 move" : "  5 move", &Font12, kColored);
        } else {
            paint.DrawStringAt(8, 84, state.fileManager.menuIndex == 0 ? "> 1 rename" : "  1 rename", &Font12, kColored);
            paint.DrawStringAt(8, 100, state.fileManager.menuIndex == 1 ? "> 2 delete" : "  2 delete", &Font12, kColored);
            paint.DrawStringAt(140, 84, state.fileManager.menuIndex == 2 ? "> 3 copy" : "  3 copy", &Font12, kColored);
            paint.DrawStringAt(140, 100, state.fileManager.menuIndex == 3 ? "> 4 move" : "  4 move", &Font12, kColored);
        }
        paint.DrawStringAt(8, 134, "left=cancel", &Font12, kColored);
    } else {
        // --- Browse view ---
        const size_t totalCount = gFileManagerEntryCount;
        const size_t startIndex = min(state.fileManager.scrollOffset, totalCount > kFileManagerPageSize ? totalCount - kFileManagerPageSize : size_t(0));
        const size_t endIndex = min(startIndex + kFileManagerPageSize, totalCount);
        String countLine = String(static_cast<unsigned>(totalCount)) + " items";
        paint.DrawStringAt(200, 26, countLine.c_str(), &Font12, kColored);
        paint.DrawLine(0, 38, kEinkLandscapeWidth - 1, 38, kColored);

        int y = 48;
        for (size_t index = startIndex; index < endIndex; ++index) {
            const bool selected = index == state.fileManager.selectedIndex;
            String line = selected ? String("> ") : String("  ");
            line += gFileManagerEntryIsDir[index] ? "[D] " : "[F] ";
            line += fileManagerItemDisplayName(index);
            // Append file size for files
            if (!gFileManagerEntryIsDir[index] && gFileManagerEntrySizes[index] > 0) {
                line += String("  ") + fileManagerFormatSize(gFileManagerEntrySizes[index]);
            }
            line = notesClipLine(line, 52);
            paint.DrawStringAtUtf8(8, y, line.c_str(), &Font12, kColored);
            y += 16;
        }

        if (totalCount == 0) {
            paint.DrawStringAt(8, 58, "empty directory", &Font12, kColored);
        }

        paint.DrawStringAt(8, 218, "up/dn=nav enter=open s=FS p=preview right=menu", &Font12, kColored);
    }

    gEink.display(paint.GetImage());
    refreshEinkWithCadence(false);
}

void fileManagerEnterDirectory(SystemState &state, const String &path, Stream &out) {
    state.fileManager.currentPath = fileManagerNormalizePath(path);
    state.fileManager.viewMode = kFileManagerViewBrowse;
    fileManagerResetBrowseSelection(state);
    invalidateFileManagerCache();
    fileManagerRefreshListing(state, out);
}

bool fileManagerExecuteSelection(SystemState &state, Stream &out) {
    if (state.fileManager.selectedIndex >= gFileManagerEntryCount) {
        fileManagerSetStatus(state, "selection empty");
        return false;
    }

    const String selectedPath = fileManagerItemPath(state.fileManager.selectedIndex);
    const bool selectedIsDir = gFileManagerEntryIsDir[state.fileManager.selectedIndex];

    if (state.fileManager.menuTargetIsDir) {
        switch (state.fileManager.menuIndex) {
            case 0:
                if (selectedIsDir) {
                    fileManagerEnterDirectory(state, selectedPath, out);
                    fileManagerSetStatus(state, String("open ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
                    return true;
                }
                return false;
            case 1:
                if (fileManagerDeleteRecursive(state, selectedPath, out)) {
                    invalidateFileManagerCache();
                    fileManagerRefreshListing(state, out);
                    fileManagerSetStatus(state, String("deleted ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
                    return true;
                }
                fileManagerSetStatus(state, "delete failed");
                return false;
            case 2:
                state.fileManager.clipboardPath = selectedPath;
                state.fileManager.clipboardMove = false;
                state.fileManager.clipboardActive = true;
                fileManagerSetStatus(state, String("copied ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
                return true;
            case 3:
                state.fileManager.clipboardPath = selectedPath;
                state.fileManager.clipboardMove = true;
                state.fileManager.clipboardActive = true;
                fileManagerSetStatus(state, String("cut ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
                return true;
            default:
                return false;
        }
    }

    switch (state.fileManager.menuIndex) {
        case 0:
            if (fileManagerDeleteRecursive(state, selectedPath, out)) {
                invalidateFileManagerCache();
                fileManagerRefreshListing(state, out);
                fileManagerSetStatus(state, String("deleted ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
                return true;
            }
            fileManagerSetStatus(state, "delete failed");
            return false;
        case 1:
            state.fileManager.clipboardPath = selectedPath;
            state.fileManager.clipboardMove = false;
            state.fileManager.clipboardActive = true;
            fileManagerSetStatus(state, String("copied ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
            return true;
        case 2:
            state.fileManager.clipboardPath = selectedPath;
            state.fileManager.clipboardMove = true;
            state.fileManager.clipboardActive = true;
            fileManagerSetStatus(state, String("cut ") + fileManagerItemDisplayName(state.fileManager.selectedIndex));
            return true;
        default:
            return false;
    }
}

String fileManagerFormatSize(uint32_t bytes) {
    if (bytes < 1024) return String(bytes) + "B";
    if (bytes < 1024 * 1024) return String(bytes / 1024) + "K";
    return String(bytes / (1024 * 1024)) + "M";
}

void fileManagerLoadPreview(const SystemState &state, const String &path) {
    gFileManagerPreviewBuffer = "";
    fs::FS &fs = fileManagerActiveFs(state);
    File f = fs.open(path, FILE_READ);
    if (!f || f.isDirectory()) {
        gFileManagerPreviewBuffer = "(cannot preview)";
        if (f) f.close();
        return;
    }
    while (f.available() && gFileManagerPreviewBuffer.length() < kFileManagerPreviewMaxChars) {
        gFileManagerPreviewBuffer += static_cast<char>(f.read());
    }
    f.close();
    if (gFileManagerPreviewBuffer.length() == 0) {
        gFileManagerPreviewBuffer = "(empty file)";
    }
}

bool fileManagerHandleBrowseInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    const size_t totalCount = gFileManagerEntryCount;

    // Item-by-item navigation
    if (isCardKbUpArrowCode(rawCode)) {
        if (totalCount > 0 && state.fileManager.selectedIndex > 0) {
            --state.fileManager.selectedIndex;
            // Scroll up if needed
            if (state.fileManager.selectedIndex < state.fileManager.scrollOffset) {
                state.fileManager.scrollOffset = state.fileManager.selectedIndex;
            }
        }
        return true;
    }

    if (isCardKbDownArrowCode(rawCode)) {
        if (totalCount > 0 && state.fileManager.selectedIndex < static_cast<uint8_t>(totalCount - 1)) {
            ++state.fileManager.selectedIndex;
            // Scroll down if needed
            if (state.fileManager.selectedIndex >= state.fileManager.scrollOffset + kFileManagerPageSize) {
                state.fileManager.scrollOffset = state.fileManager.selectedIndex - kFileManagerPageSize + 1;
            }
        }
        return true;
    }

    if (isCardKbRightArrowCode(rawCode)) {
        if (state.fileManager.clipboardActive) {
            if (fileManagerPasteClipboard(state, out)) {
                fileManagerRefreshListing(state, out);
                return true;
            }
            return true;
        }

        state.fileManager.viewMode = kFileManagerViewDirectoryMenu;
        state.fileManager.menuIndex = 0;
        fileManagerSetStatus(state, "directory actions");
        return true;
    }

    // Enter: open dir directly, or open item menu for files
    if (normalizedKey == '\r' || normalizedKey == '\n') {
        if (totalCount > 0 && state.fileManager.selectedIndex < totalCount) {
            const size_t idx = state.fileManager.selectedIndex;
            if (gFileManagerEntryIsDir[idx]) {
                fileManagerEnterDirectory(state, fileManagerItemPath(idx), out);
                return true;
            }
            state.fileManager.viewMode = kFileManagerViewItemMenu;
            state.fileManager.menuIndex = 0;
            state.fileManager.menuTargetPath = fileManagerItemPath(idx);
            state.fileManager.menuTargetIsDir = false;
            fileManagerSetStatus(state, fileManagerItemDisplayName(idx));
            return true;
        }
        return true;
    }

    // Number keys: quick-select items on current page
    if (normalizedKey >= '1' && normalizedKey <= '6') {
        const size_t localIndex = static_cast<size_t>(normalizedKey - '1');
        const size_t absoluteIndex = state.fileManager.scrollOffset + localIndex;
        if (absoluteIndex < totalCount) {
            state.fileManager.selectedIndex = static_cast<uint8_t>(absoluteIndex);
            state.fileManager.viewMode = kFileManagerViewItemMenu;
            state.fileManager.menuIndex = 0;
            state.fileManager.menuTargetPath = fileManagerItemPath(absoluteIndex);
            state.fileManager.menuTargetIsDir = gFileManagerEntryIsDir[absoluteIndex];
            fileManagerSetStatus(state, fileManagerItemDisplayName(absoluteIndex));
            return true;
        }
    }

    // s: toggle SD / LittleFS
    if (normalizedKey == 's' || normalizedKey == 'S') {
        state.fileManager.browseLittleFs = !state.fileManager.browseLittleFs;
        state.fileManager.currentPath = "/";
        fileManagerResetBrowseSelection(state);
        invalidateFileManagerCache();
        fileManagerRefreshListing(state, out);
        fileManagerSetStatus(state, state.fileManager.browseLittleFs ? "LittleFS" : "SD card");
        return true;
    }

    // p: preview selected file
    if (normalizedKey == 'p' || normalizedKey == 'P') {
        if (totalCount > 0 && state.fileManager.selectedIndex < totalCount
            && !gFileManagerEntryIsDir[state.fileManager.selectedIndex]) {
            fileManagerLoadPreview(state, fileManagerItemPath(state.fileManager.selectedIndex));
            state.fileManager.viewMode = kFileManagerViewPreview;
            fileManagerSetStatus(state, fileManagerItemDisplayName(state.fileManager.selectedIndex));
            return true;
        }
        fileManagerSetStatus(state, "select a file");
        return true;
    }

    if (normalizedKey >= 32 && normalizedKey <= 126) {
        return true; // consume silently
    }

    return false;
}

// Item menu options:
// Dir:  0=open, 1=rename, 2=delete, 3=copy, 4=move
// File: 0=rename, 1=delete, 2=copy, 3=move
bool fileManagerHandleItemMenuInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    const bool isDir = state.fileManager.menuTargetIsDir;
    const uint8_t optionCount = isDir ? 5 : 4;

    if (isCardKbUpArrowCode(rawCode)) {
        state.fileManager.menuIndex = state.fileManager.menuIndex == 0
            ? optionCount - 1
            : state.fileManager.menuIndex - 1;
        return true;
    }

    if (isCardKbDownArrowCode(rawCode)) {
        state.fileManager.menuIndex = static_cast<uint8_t>((state.fileManager.menuIndex + 1) % optionCount);
        return true;
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        // Dir: open
        if (isDir && state.fileManager.menuIndex == 0) {
            fileManagerEnterDirectory(state, state.fileManager.menuTargetPath, out);
            return true;
        }

        const uint8_t renameIdx = isDir ? 1 : 0;
        const uint8_t deleteIdx = isDir ? 2 : 1;
        const uint8_t copyIdx = isDir ? 3 : 2;
        const uint8_t moveIdx = isDir ? 4 : 3;

        if (state.fileManager.menuIndex == renameIdx) {
            state.fileManager.viewMode = kFileManagerViewRename;
            state.fileManager.pendingRenameName = fileManagerBaseName(state.fileManager.menuTargetPath);
            fileManagerSetStatus(state, "type new name");
            return true;
        }

        if (state.fileManager.menuIndex == deleteIdx) {
            if (fileManagerDeleteRecursive(state, state.fileManager.menuTargetPath, out)) {
                invalidateFileManagerCache();
                fileManagerRefreshListing(state, out);
                fileManagerSetStatus(state, "deleted");
                state.fileManager.viewMode = kFileManagerViewBrowse;
                return true;
            }
            fileManagerSetStatus(state, "delete failed");
            return true;
        }

        if (state.fileManager.menuIndex == copyIdx) {
            state.fileManager.clipboardPath = state.fileManager.menuTargetPath;
            state.fileManager.clipboardMove = false;
            state.fileManager.clipboardActive = true;
            state.fileManager.viewMode = kFileManagerViewBrowse;
            fileManagerSetStatus(state, "copy ready");
            return true;
        }

        if (state.fileManager.menuIndex == moveIdx) {
            state.fileManager.clipboardPath = state.fileManager.menuTargetPath;
            state.fileManager.clipboardMove = true;
            state.fileManager.clipboardActive = true;
            state.fileManager.viewMode = kFileManagerViewBrowse;
            fileManagerSetStatus(state, "move ready");
            return true;
        }

        return false;
    }

    if (isCardKbLeftArrowCode(rawCode)) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.menuIndex = 0;
        fileManagerSetStatus(state, "canceled");
        return true;
    }

    if (normalizedKey >= '1' && normalizedKey <= '5') {
        state.fileManager.menuIndex = static_cast<uint8_t>(normalizedKey - '1');
        return true;
    }

    return false;
}

bool fileManagerHandleDirectoryMenuInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    if (isCardKbUpArrowCode(rawCode) || isCardKbDownArrowCode(rawCode)) {
        state.fileManager.menuIndex = static_cast<uint8_t>((state.fileManager.menuIndex + 1) % 2);
        return true;
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        if (state.fileManager.menuIndex == 0) {
            state.fileManager.viewMode = kFileManagerViewCreateFolder;
            state.fileManager.pendingFolderName = "";
            fileManagerSetStatus(state, "type new folder name");
            return true;
        }

        invalidateFileManagerCache();
        fileManagerRefreshListing(state, out);
        fileManagerSetStatus(state, "refreshed");
        state.fileManager.viewMode = kFileManagerViewBrowse;
        return true;
    }

    if (isCardKbLeftArrowCode(rawCode)) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.menuIndex = 0;
        fileManagerSetStatus(state, "menu canceled");
        return true;
    }

    if (normalizedKey >= '1' && normalizedKey <= '2') {
        state.fileManager.menuIndex = static_cast<uint8_t>(normalizedKey - '1');
        return true;
    }

    return false;
}

bool fileManagerHandleCreateFolderInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    if (isCardKbLeftArrowCode(rawCode)) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.pendingFolderName = "";
        fileManagerSetStatus(state, "create canceled");
        return true;
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        if (fileManagerCreateFolder(state, state.fileManager.pendingFolderName, out)) {
            state.fileManager.viewMode = kFileManagerViewBrowse;
            state.fileManager.pendingFolderName = "";
            fileManagerRefreshListing(state, out);
            return true;
        }
        return true;
    }

    if (normalizedKey == 8 || normalizedKey == 127) {
        if (!state.fileManager.pendingFolderName.isEmpty()) {
            state.fileManager.pendingFolderName.remove(state.fileManager.pendingFolderName.length() - 1);
        }
        fileManagerSetStatus(state, "editing name");
        return true;
    }

    if (normalizedKey >= 32 && normalizedKey <= 126) {
        if (normalizedKey != '/' && normalizedKey != '\\') {
            if (state.fileManager.pendingFolderName.length() < 32) {
                state.fileManager.pendingFolderName += normalizedKey;
            }
            fileManagerSetStatus(state, String("name: ") + state.fileManager.pendingFolderName);
        }
        return true;
    }

    return false;
}

bool fileManagerHandleRenameInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    if (isCardKbLeftArrowCode(rawCode) || static_cast<uint8_t>(normalizedKey) == 0x1B) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.pendingRenameName = "";
        fileManagerSetStatus(state, "rename canceled");
        return true;
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        String newName = state.fileManager.pendingRenameName;
        newName.trim();
        if (newName.isEmpty()) {
            fileManagerSetStatus(state, "name empty");
            return true;
        }
        const String parentDir = fileManagerParentPath(state.fileManager.menuTargetPath);
        const String newPath = fileManagerJoinPath(parentDir, newName);
        if (fileManagerPathExists(state, newPath)) {
            fileManagerSetStatus(state, "name exists");
            return true;
        }
        fs::FS &fs = fileManagerActiveFs(state);
        if (fs.rename(state.fileManager.menuTargetPath, newPath)) {
            invalidateFileManagerCache();
            fileManagerRefreshListing(state, out);
            fileManagerSetStatus(state, String("renamed ") + newName);
            out.print("FM: renamed to ");
            out.println(newPath);
        } else {
            fileManagerSetStatus(state, "rename failed");
        }
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.pendingRenameName = "";
        return true;
    }

    if (normalizedKey == 8 || normalizedKey == 127) {
        if (!state.fileManager.pendingRenameName.isEmpty()) {
            state.fileManager.pendingRenameName.remove(state.fileManager.pendingRenameName.length() - 1);
        }
        return true;
    }

    if (normalizedKey >= 32 && normalizedKey <= 126) {
        if (normalizedKey != '/' && normalizedKey != '\\') {
            if (state.fileManager.pendingRenameName.length() < 32) {
                state.fileManager.pendingRenameName += normalizedKey;
            }
        }
        return true;
    }

    return false;
}

bool fileManagerHandlePreviewInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    (void)out;
    // Any key exits preview
    (void)normalizedKey;
    (void)rawCode;
    state.fileManager.viewMode = kFileManagerViewBrowse;
    gFileManagerPreviewBuffer = "";
    fileManagerSetStatus(state, "browse");
    return true;
}

void fileManagerResetSession(SystemState &state) {
    state.fileManager.currentPath = "/";
    state.fileManager.statusMessage = "ready";
    state.fileManager.clipboardPath = "";
    state.fileManager.menuTargetPath = "";
    state.fileManager.pendingFolderName = "";
    state.fileManager.pendingRenameName = "";
    state.fileManager.clipboardActive = false;
    state.fileManager.clipboardMove = false;
    state.fileManager.menuTargetIsDir = false;
    state.fileManager.browseLittleFs = false;
    state.fileManager.viewMode = kFileManagerViewBrowse;
    state.fileManager.selectedIndex = 0;
    state.fileManager.scrollOffset = 0;
    state.fileManager.menuIndex = 0;
    invalidateFileManagerCache();
}

bool fileManagerHandleBackInput(SystemState &state, uint8_t rawCode, Stream &out) {
    if (!isCardKbLeftArrowCode(rawCode)) {
        return false;
    }

    if (state.fileManager.viewMode == kFileManagerViewCreateFolder) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.pendingFolderName = "";
        fileManagerSetStatus(state, "create canceled");
        return true;
    }

    if (state.fileManager.viewMode == kFileManagerViewRename) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.pendingRenameName = "";
        fileManagerSetStatus(state, "rename canceled");
        return true;
    }

    if (state.fileManager.viewMode == kFileManagerViewPreview) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        gFileManagerPreviewBuffer = "";
        fileManagerSetStatus(state, "browse");
        return true;
    }

    if (state.fileManager.viewMode == kFileManagerViewItemMenu || state.fileManager.viewMode == kFileManagerViewDirectoryMenu) {
        state.fileManager.viewMode = kFileManagerViewBrowse;
        state.fileManager.menuIndex = 0;
        fileManagerSetStatus(state, "menu canceled");
        return true;
    }

    if (state.fileManager.currentPath != "/") {
        fileManagerEnterDirectory(state, fileManagerParentPath(state.fileManager.currentPath), out);
        fileManagerSetStatus(state, "back");
        return true;
    }

    state.launcher.activeAppId = "launcher";
    state.settings.lastMessage = "back to launcher";
    return true;
}

bool handleFileManagerAppInput(SystemState &state, char key, char normalizedKey, uint8_t rawCode, Stream &out) {
    (void)key;

    if (state.fileManager.viewMode == kFileManagerViewRename) {
        return fileManagerHandleRenameInput(state, normalizedKey, rawCode, out);
    }

    if (state.fileManager.viewMode == kFileManagerViewPreview) {
        return fileManagerHandlePreviewInput(state, normalizedKey, rawCode, out);
    }

    if (state.fileManager.viewMode == kFileManagerViewItemMenu) {
        return fileManagerHandleItemMenuInput(state, normalizedKey, rawCode, out);
    }

    if (state.fileManager.viewMode == kFileManagerViewDirectoryMenu) {
        return fileManagerHandleDirectoryMenuInput(state, normalizedKey, rawCode, out);
    }

    if (state.fileManager.viewMode == kFileManagerViewCreateFolder) {
        return fileManagerHandleCreateFolderInput(state, normalizedKey, rawCode, out);
    }

    return fileManagerHandleBrowseInput(state, normalizedKey, rawCode, out);
}

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

size_t launcherTotalItemCount() {
    return kLauncherAppCount + gSdAppCount;
}

bool launcherItemIsBuiltin(size_t absoluteIndex) {
    return absoluteIndex < kLauncherAppCount;
}

String launcherItemTitleAt(size_t absoluteIndex) {
    if (launcherItemIsBuiltin(absoluteIndex)) {
        return String(kLauncherApps[absoluteIndex].title);
    }

    const size_t sdIndex = absoluteIndex - kLauncherAppCount;
    if (sdIndex < gSdAppCount) {
        return gSdApps[sdIndex];
    }
    return String("unknown");
}

String launcherItemIdAt(size_t absoluteIndex) {
    if (launcherItemIsBuiltin(absoluteIndex)) {
        return String(kLauncherApps[absoluteIndex].id);
    }

    const size_t sdIndex = absoluteIndex - kLauncherAppCount;
    if (sdIndex < gSdAppCount) {
        return String("sdapp:") + gSdApps[sdIndex];
    }
    return String("unknown");
}

String truncateWithEllipsis(const String &input, size_t maxChars) {
    if (input.length() <= maxChars) {
        return input;
    }

    if (maxChars <= 3) {
        return String("...");
    }

    return input.substring(0, maxChars - 3) + String("...");
}

String launcherIconLabelAt(size_t absoluteIndex) {
    const String id = launcherItemIdAt(absoluteIndex);
    if (id.equalsIgnoreCase("settings")) {
        return String("ST");
    }
    if (id.equalsIgnoreCase("file-manager")) {
        return String("FM");
    }
    if (id.equalsIgnoreCase("web-upload")) {
        return String("UP");
    }
    if (id.equalsIgnoreCase("music-player") || id.equalsIgnoreCase("sdapp:music-player")) {
        return String("MP");
    }
    if (id.equalsIgnoreCase("notes")) {
        return String("NT");
    }
    if (id.equalsIgnoreCase("apps")) {
        return String("AP");
    }
    if (id.equalsIgnoreCase("serial")) {
        return String("SH");
    }
    if (id.equalsIgnoreCase("about")) {
        return String("AB");
    }
    return String("SD");
}

void refreshLauncherSdApps(const SystemState &state, Stream &out) {
    if (!state.sdReady) {
        gSdAppCount = 0;
        gSdAppCacheDirty = false;
        gSdAppCachePrimed = true;
        return;
    }

    if (gSdAppCachePrimed && !gSdAppCacheDirty) {
        return;
    }

    gSdAppCount = loadSdAppManifest(state, gSdApps, kMaxSdApps, out);
    gSdAppCacheDirty = false;
    gSdAppCachePrimed = true;
}

void drawLauncherOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    const size_t totalCount = launcherTotalItemCount();
    const size_t safeIndex = totalCount == 0 ? 0 : min(static_cast<size_t>(state.launcher.selectedIndex), totalCount - 1);
    const size_t pageCount = totalCount == 0 ? 1 : ((totalCount + kLauncherPageSize - 1) / kLauncherPageSize);
    const size_t pageIndex = totalCount == 0 ? 0 : (safeIndex / kLauncherPageSize);
    const size_t startIndex = pageIndex * kLauncherPageSize;
    const size_t endIndex = min(startIndex + kLauncherPageSize, totalCount);

    gOled.clearBuffer();
    gOled.setFont(u8g2_font_4x6_tf);
    gOled.setCursor(0, 7);
    gOled.print("launcher ");
    gOled.print(pageIndex + 1);
    gOled.print("/");
    gOled.print(pageCount);

    int y = 15;
    for (size_t i = startIndex; i < endIndex; ++i) {
        String line = "[";
        line += launcherIconLabelAt(i);
        line += "] ";
        line += String(static_cast<unsigned>((i - startIndex) + 1));
        line += ".";
        line += truncateWithEllipsis(launcherItemTitleAt(i), 22);
        gOled.setCursor(0, y);
        gOled.print(line);
        y += 8;
    }

    while (y <= 55) {
        gOled.setCursor(0, y);
        gOled.print("-");
        y += 8;
    }

    gOled.setCursor(0, 63);
    gOled.print(gLauncherKeyMessage);
    gOled.sendBuffer();
}

void drawLauncherCard(Paint &paint, int x, int y, int width, int height, const String &numberedTitle, bool selected) {
    paint.DrawRectangle(x, y, x + width, y + height, kColored);
    if (selected) {
        paint.DrawRectangle(x + 1, y + 1, x + width - 1, y + height - 1, kColored);
    }

    paint.DrawStringAtUtf8(x + 8, y + 42, truncateWithEllipsis(numberedTitle, 20).c_str(), &Font12, kColored);
}

void drawLauncherIcon(Paint &paint, int x, int y, int size, const String &itemId) {
    const int x2 = x + size;
    const int y2 = y + size;
    paint.DrawRectangle(x, y, x2, y2, kColored);

    if (itemId.equalsIgnoreCase("settings")) {
        const int cx = x + size / 2;
        const int cy = y + size / 2;
        paint.DrawCircle(cx, cy, 5, kColored);
        paint.DrawLine(cx, y + 1, cx, y + 5, kColored);
        paint.DrawLine(cx, y2 - 1, cx, y2 - 5, kColored);
        paint.DrawLine(x + 1, cy, x + 5, cy, kColored);
        paint.DrawLine(x2 - 1, cy, x2 - 5, cy, kColored);
        return;
    }

    if (itemId.equalsIgnoreCase("web-upload")) {
        const int cx = x + size / 2;
        paint.DrawLine(cx, y + 3, cx, y2 - 7, kColored);
        paint.DrawLine(cx, y + 3, cx - 4, y + 8, kColored);
        paint.DrawLine(cx, y + 3, cx + 4, y + 8, kColored);
        paint.DrawRectangle(x + 3, y2 - 6, x2 - 3, y2 - 3, kColored);
        return;
    }

    if (itemId.equalsIgnoreCase("music-player") || itemId.equalsIgnoreCase("sdapp:music-player")) {
        paint.DrawLine(x + 4, y + 5, x + 4, y2 - 5, kColored);
        paint.DrawLine(x + 4, y + 5, x + 12, y + 9, kColored);
        paint.DrawLine(x + 12, y + 9, x + 8, y + 14, kColored);
        paint.DrawLine(x + 8, y + 14, x + 12, y + 19, kColored);
        paint.DrawLine(x + 12, y + 19, x + 4, y2 - 5, kColored);
        paint.DrawLine(x + 14, y + 6, x + 14, y2 - 6, kColored);
        return;
    }

    if (itemId.equalsIgnoreCase("notes")) {
        paint.DrawRectangle(x + 4, y + 3, x2 - 4, y2 - 3, kColored);
        paint.DrawLine(x + 7, y + 8, x2 - 7, y + 8, kColored);
        paint.DrawLine(x + 7, y + 12, x2 - 7, y + 12, kColored);
        paint.DrawLine(x + 7, y + 16, x2 - 7, y + 16, kColored);
        return;
    }

    if (itemId.equalsIgnoreCase("apps")) {
        paint.DrawRectangle(x + 3, y + 3, x + 8, y + 8, kColored);
        paint.DrawRectangle(x + 10, y + 3, x + 15, y + 8, kColored);
        paint.DrawRectangle(x + 3, y + 10, x + 8, y + 15, kColored);
        paint.DrawRectangle(x + 10, y + 10, x + 15, y + 15, kColored);
        return;
    }

    if (itemId.equalsIgnoreCase("serial")) {
        paint.DrawRectangle(x + 2, y + 3, x2 - 2, y2 - 3, kColored);
        paint.DrawLine(x + 5, y + 8, x + 9, y + 11, kColored);
        paint.DrawLine(x + 5, y + 14, x + 9, y + 11, kColored);
        paint.DrawLine(x + 11, y + 14, x + 16, y + 14, kColored);
        return;
    }

    if (itemId.equalsIgnoreCase("about")) {
        const int cx = x + size / 2;
        const int cy = y + size / 2;
        paint.DrawCircle(cx, cy, 7, kColored);
        paint.DrawLine(cx, cy - 2, cx, cy + 4, kColored);
        paint.DrawCircle(cx, cy - 5, 1, kColored);
        return;
    }

    // Default SD app icon: simple document shape.
    paint.DrawRectangle(x + 4, y + 3, x2 - 4, y2 - 3, kColored);
    paint.DrawLine(x2 - 9, y + 3, x2 - 4, y + 8, kColored);
    paint.DrawLine(x + 7, y + 10, x2 - 7, y + 10, kColored);
    paint.DrawLine(x + 7, y + 14, x2 - 7, y + 14, kColored);
}

void drawLauncherPreview(const SystemState &state, Stream &out) {
    refreshLauncherSdApps(state, out);

    out.println("--- Launcher ---");
    out.print("active app: ");
    out.println(state.launcher.activeAppId);
    const size_t totalCount = launcherTotalItemCount();
    out.print("items: ");
    out.println(totalCount);

    for (size_t i = 0; i < totalCount; ++i) {
        out.print(i == state.launcher.selectedIndex ? "* " : "  ");
        out.print(i + 1);
        out.print(". ");
        out.print(launcherItemTitleAt(i));
        out.print(" [");
        out.print(launcherItemIdAt(i));
        out.println("]");
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

    if (state.launcher.activeAppId.equalsIgnoreCase("music-player") || state.launcher.activeAppId.equalsIgnoreCase("sdapp:music-player")) {
        drawMusicPlayerOled(state);
        return;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("notes")) {
        String charsLine = String("chars: ") + String(static_cast<unsigned>(gNotesBuffer.length()));
        String statusLine = String("status: ") + state.notes.statusMessage;
        String preview = notesTailPreview(18);
        preview.replace("\n", " ");

        gOled.clearBuffer();
        gOled.setFont(u8g2_font_6x10_tf);
        gOled.drawStr(0, 12, "NOTES");
        gOled.drawStr(0, 24, charsLine.c_str());
        gOled.drawStr(0, 36, statusLine.c_str());
        gOled.drawStr(0, 48, preview.c_str());
        gOled.drawStr(0, 62, "type, -> save, <- back");
        gOled.sendBuffer();
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
        gOled.drawStr(0, 55, "3 forget saved");
        gOled.drawStr(64, 55, "<- back");
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
            gOled.drawStr(0, 55, "3 disconnect");
            gOled.drawStr(64, 55, "4 forget");
            gOled.drawStr(64, 64, "<- back");
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
    } else if (state.settings.viewMode == kSettingsViewSdList) {
        String sdLine = String("sd:") + (state.config.sdEnabled ? "on" : "off");
        sdLine += " ";
        sdLine += String(state.config.sdProbeSpeed);
        gOled.drawStr(0, 22, "sd manager");
        gOled.drawStr(0, 33, "1 toggle sd");
        gOled.drawStr(0, 44, "2 cycle speed");
        gOled.drawStr(0, 55, "<- back");
        gOled.setCursor(0, 64);
        gOled.print(sdLine);
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
        gOled.drawStr(0, 64, "enter=connect  <- back");
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
        paint.DrawStringAt(10, 142, "3) Forget saved network", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Left arrow = back", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewWifiSelectList) {
        paint.DrawStringAt(10, 42, "Available WiFi:", &Font12, kColored);
        if (gWifiScanInProgress) {
            paint.DrawStringAt(10, 62, "Scanning networks...", &Font12, kColored);
            paint.DrawStringAt(10, 80, "Please wait", &Font12, kColored);
        } else {
            const size_t maxOffset = gWifiCount > kWifiListVisibleCount ? gWifiCount - kWifiListVisibleCount : 0;
            const size_t startIndex = min(gWifiListScrollOffset, maxOffset);
            const size_t endIndex = min(startIndex + kWifiListVisibleCount, gWifiCount);
            int y = 58;
            for (size_t i = startIndex; i < endIndex; ++i) {
                String line = String(static_cast<unsigned>((i - startIndex) + 1));
                line += ") ";
                line += formatWifiDisplayLabel(i);
                line += " (";
                line += String(static_cast<long>(gWifiRssiList[i]));
                line += ")";
                paint.DrawStringAtUtf8(10, y, line.c_str(), &Font12, kColored);
                y += 16;
            }

            if (gWifiCount == 0) {
                paint.DrawStringAt(10, 76, "No networks found", &Font12, kColored);
            } else if (gWifiCount > kWifiListVisibleCount) {
                String pageLine = String("page ") + String(static_cast<unsigned>(startIndex + 1));
                pageLine += "-";
                pageLine += String(static_cast<unsigned>(endIndex));
                pageLine += "/";
                pageLine += String(static_cast<unsigned>(gWifiCount));
                paint.DrawStringAt(10, 202, pageLine.c_str(), &Font12, kColored);
                paint.DrawStringAt(150, 202, "^ up", &Font12, kColored);
                paint.DrawStringAt(200, 202, "down", &Font12, kColored);
            }
        }

        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "1..9 select, up/down page", &Font12, kColored);
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
        paint.DrawStringAt(10, 142, "3) Disconnect current", &Font12, kColored);
        paint.DrawStringAt(10, 158, "4) Forget saved device", &Font12, kColored);
        if (!state.config.btPreferredDevice.isEmpty()) {
            String preferredLine = String("saved: ") + state.config.btPreferredDevice;
            paint.DrawStringAtUtf8(10, 176, preferredLine.c_str(), &Font12, kColored);
        }
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Left arrow = back", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewBluetoothSelectList) {
        paint.DrawStringAt(10, 42, "Available Bluetooth:", &Font12, kColored);
        const size_t maxOffset = gBluetoothDeviceCount > kBluetoothListVisibleCount ? gBluetoothDeviceCount - kBluetoothListVisibleCount : 0;
        const size_t startIndex = min(gBluetoothListScrollOffset, maxOffset);
        const size_t endIndex = min(startIndex + kBluetoothListVisibleCount, gBluetoothDeviceCount);
        int y = 58;
        for (size_t i = startIndex; i < endIndex; ++i) {
            String line = String(static_cast<unsigned>((i - startIndex) + 1));
            line += ") ";
            line += gBluetoothDeviceList[i];
            paint.DrawStringAtUtf8(10, y, line.c_str(), &Font12, kColored);
            y += 16;
        }
        if (gBluetoothDeviceCount == 0) {
            paint.DrawStringAt(10, 76, "No devices found", &Font12, kColored);
        } else if (gBluetoothDeviceCount > kBluetoothListVisibleCount) {
            String pageLine = String("page ") + String(static_cast<unsigned>(startIndex + 1));
            pageLine += "-";
            pageLine += String(static_cast<unsigned>(endIndex));
            pageLine += "/";
            pageLine += String(static_cast<unsigned>(gBluetoothDeviceCount));
            paint.DrawStringAt(10, 202, pageLine.c_str(), &Font12, kColored);
            paint.DrawStringAt(150, 202, "^ up", &Font12, kColored);
            paint.DrawStringAt(200, 202, "down", &Font12, kColored);
        }
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "1..9 select, up/down page", &Font12, kColored);
    } else if (state.settings.viewMode == kSettingsViewSdList) {
        const String sdState = String("sd: ") + (state.config.sdEnabled ? "on" : "off");
        const String sdSpeed = String("speed: ") + String(state.config.sdProbeSpeed) + " hz";
        paint.DrawStringAt(10, 42, "SD manager", &Font16, kColored);
        paint.DrawStringAt(10, 66, sdState.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 82, sdSpeed.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 110, "1) Toggle SD ON/OFF", &Font12, kColored);
        paint.DrawStringAt(10, 126, "2) Cycle SD speed", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Left arrow = back", &Font12, kColored);
    } else {
        // Password view: e-ink only shows context, typing remains on OLED.
        const String selectedSsidUtf8 = state.settings.selectedSsid;
        paint.DrawStringAt(10, 42, "Connect to:", &Font12, kColored);
        paint.DrawStringAtUtf8(98, 42, selectedSsidUtf8.c_str(), &Font12, kColored);
        paint.DrawStringAt(10, 60, "Password on OLED keyboard", &Font12, kColored);
        paint.DrawStringAt(10, 78, "Press ENTER to connect", &Font12, kColored);
        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        paint.DrawStringAt(10, 218, "Left arrow cancels", &Font12, kColored);
    }

    gEink.display(paint.GetImage());
    refreshEinkWithCadence(false);
}


size_t scanWifiNetworks(Stream &out) {
    gWifiCount = settingsServiceScanWifiNetworks(
        gWifiSsidList,
        gWifiBssidList,
        gWifiRssiList,
        kMaxWifiNetworks,
        gWifiListScrollOffset,
        out);
    return gWifiCount;
}

bool toggleWifiEnabled(SystemState &state, Stream &out) {
    return settingsServiceToggleWifi(state, out);
}

bool connectSelectedWifi(SystemState &state, Stream &out) {
    return settingsServiceConnectSelectedWifi(state, out);
}

bool forgetSavedWifi(SystemState &state, Stream &out) {
    return settingsServiceForgetWifi(state, out);
}

bool toggleBluetoothEnabled(SystemState &state, Stream &out) {
    return settingsServiceToggleBluetooth(state, out);
}

bool selectBluetoothDevice(SystemState &state, const String &deviceName, const String &deviceAddress, int8_t selectedIndex, Stream &out) {
    return settingsServiceSelectBluetoothDevice(state, deviceName, deviceAddress, selectedIndex, out);
}

bool forgetSavedBluetoothDevice(SystemState &state, Stream &out) {
    return settingsServiceForgetBluetoothDevice(state, out);
}

size_t scanBluetoothDevices(Stream &out) {
    gBluetoothDeviceCount = settingsServiceScanBluetoothDevices(
        gBluetoothDeviceList,
        gBluetoothDeviceAddressList,
        kMaxBluetoothDevices,
        gBluetoothBleInitialized,
        out);
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
    // Arrow scan codes can overlap with numeric high-bit ranges.
    if (isCardKbLeftArrowCode(code) || isCardKbUpArrowCode(code) || isCardKbDownArrowCode(code) || isCardKbRightArrowCode(code)) {
        return key;
    }

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
    return code == 0x92 || code == 0x94 || code == 0xB2 || code == 0xB4;
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
        state.settings.viewMode = kSettingsViewSdList;
        state.settings.lastMessage = "sd manager";
        out.println("Settings: sd manager open");
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
        refreshEinkWithCadence(false);
    }

    out.println("App placeholder rendered");
    return true;
}

String musicPlayerBaseNameFromPath(const String &path) {
    String fileName = path;
    int slash = fileName.lastIndexOf('/');
    if (slash >= 0 && slash + 1 < static_cast<int>(fileName.length())) {
        fileName = fileName.substring(slash + 1);
    }

    int dot = fileName.lastIndexOf('.');
    if (dot > 0) {
        fileName = fileName.substring(0, dot);
    }

    fileName.trim();
    return fileName;
}

bool isMusicTrackPath(const String &path) {
    String lowered = path;
    lowered.toLowerCase();
    return lowered.endsWith(".mp3") || lowered.endsWith(".wav") || lowered.endsWith(".aac") || lowered.endsWith(".m4a") || lowered.endsWith(".ogg") || lowered.endsWith(".flac");
}

void invalidateMusicPlayerCache() {
    gMusicCacheDirty = true;
    gMusicCachePrimed = false;
}

void resetMusicPlayerSession(SystemState &state) {
    state.musicPlayer.selectedIndex = 0;
    state.musicPlayer.scrollOffset = 0;
    state.musicPlayer.playing = false;
    state.musicPlayer.nowPlaying = "";
    state.musicPlayer.statusMessage = "ready";
    invalidateMusicPlayerCache();
}

String musicPlayerTrackLabelAt(size_t index) {
    if (index >= gMusicTrackCount) {
        return String();
    }

    return gMusicTrackNames[index];
}

void musicPlayerStoreTrack(const String &path) {
    if (gMusicTrackCount >= kMaxMusicTracks) {
        return;
    }

    gMusicTrackPaths[gMusicTrackCount] = path;
    gMusicTrackNames[gMusicTrackCount] = musicPlayerBaseNameFromPath(path);
    if (gMusicTrackNames[gMusicTrackCount].isEmpty()) {
        gMusicTrackNames[gMusicTrackCount] = path;
    }
    ++gMusicTrackCount;
}

void musicPlayerCollectFromDirectory(File directory, const String &prefix) {
    File entry = directory.openNextFile();
    while (entry && gMusicTrackCount < kMaxMusicTracks) {
        String entryName = entry.name();
        if (entry.isDirectory()) {
            entry.close();
            entry = directory.openNextFile();
            continue;
        }

        String fullPath = prefix;
        if (!fullPath.endsWith("/")) {
            fullPath += "/";
        }
        fullPath += entryName;
        if (isMusicTrackPath(fullPath)) {
            musicPlayerStoreTrack(fullPath);
        }

        entry.close();
        entry = directory.openNextFile();
    }
}

void musicPlayerCollectFromPlaylist(File playlist, const String &basePath) {
    while (playlist.available() && gMusicTrackCount < kMaxMusicTracks) {
        String line = playlist.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) {
            continue;
        }

        String fullPath = line;
        if (!fullPath.startsWith("/")) {
            fullPath = basePath;
            if (!fullPath.endsWith("/")) {
                fullPath += "/";
            }
            fullPath += line;
        }

        if (isMusicTrackPath(fullPath)) {
            musicPlayerStoreTrack(fullPath);
        }
    }
}

void refreshMusicPlayerLibrary(const SystemState &state, Stream &out) {
    if (!state.sdReady) {
        gMusicTrackCount = 0;
        gMusicCacheDirty = false;
        gMusicCachePrimed = true;
        return;
    }

    if (gMusicCachePrimed && !gMusicCacheDirty) {
        return;
    }

    gMusicTrackCount = 0;
    const String libraryPath = state.musicPlayer.libraryPath.length() ? state.musicPlayer.libraryPath : String("/music-player");
    if (!SD.exists(libraryPath)) {
        out.print("Music player: folder not found (");
        out.print(libraryPath);
        out.println(")");
        gMusicCacheDirty = false;
        gMusicCachePrimed = true;
        return;
    }

    File root = SD.open(libraryPath, FILE_READ);
    if (!root) {
        out.print("Music player: cannot open ");
        out.println(libraryPath);
        gMusicCacheDirty = false;
        gMusicCachePrimed = true;
        return;
    }

    if (!root.isDirectory()) {
        out.print("Music player: path is not a folder ");
        out.println(libraryPath);
        root.close();
        gMusicCacheDirty = false;
        gMusicCachePrimed = true;
        return;
    }

    const String playlistPath = libraryPath + "/playlist.txt";
    if (SD.exists(playlistPath)) {
        File playlist = SD.open(playlistPath, FILE_READ);
        if (playlist) {
            musicPlayerCollectFromPlaylist(playlist, libraryPath);
            playlist.close();
        }
    }

    if (gMusicTrackCount == 0) {
        musicPlayerCollectFromDirectory(root, libraryPath);
    }

    root.close();
    gMusicCacheDirty = false;
    gMusicCachePrimed = true;
    out.print("Music player tracks loaded: ");
    out.println(gMusicTrackCount);
}

void drawMusicPlayerOled(const SystemState &state) {
    if (!state.oledReady) {
        return;
    }

    const size_t totalCount = gMusicTrackCount;
    const size_t safeIndex = totalCount == 0 ? 0 : min(static_cast<size_t>(state.musicPlayer.selectedIndex), totalCount - 1);
    const size_t pageCount = totalCount == 0 ? 1 : ((totalCount + 3) / 4);
    const size_t pageIndex = totalCount == 0 ? 0 : (safeIndex / 4);
    const size_t startIndex = pageIndex * 4;
    const size_t endIndex = min(startIndex + 4, totalCount);

    gOled.clearBuffer();
    gOled.setFont(u8g2_font_4x6_tf);
    gOled.setCursor(0, 7);
    gOled.print("music ");
    gOled.print(pageIndex + 1);
    gOled.print("/");
    gOled.print(pageCount);

    gOled.setCursor(0, 15);
    gOled.print(state.musicPlayer.playing ? "playing" : "paused");
    gOled.print(" ");
    gOled.print(state.musicPlayer.statusMessage);

    String currentLabel = state.musicPlayer.nowPlaying;
    if (currentLabel.isEmpty() && totalCount > 0) {
        currentLabel = musicPlayerTrackLabelAt(safeIndex);
    }
    gOled.setCursor(0, 23);
    gOled.print(currentLabel);

    int y = 33;
    for (size_t index = startIndex; index < endIndex && y <= 57; ++index) {
        String line = (index == safeIndex) ? ">" : " ";
        line += String(static_cast<unsigned>((index - startIndex) + 1));
        line += " ";
        line += musicPlayerTrackLabelAt(index);
        gOled.drawStr(0, y, line.c_str());
        y += 8;
    }

    gOled.setCursor(0, 63);
    gOled.print("1 play 2 next 3 prev 4 rescan");
    gOled.sendBuffer();
}

void drawMusicPlayerEink(const SystemState &state) {
    Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
    prepareLandscapePaint(paint);
    paint.Clear(kUncolored);
    paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
    paint.DrawStringAt(8, 10, "MUSIC PLAYER", &Font16, kColored);
    paint.DrawLine(0, 32, kEinkLandscapeWidth - 1, 32, kColored);

    String statusLine = state.musicPlayer.playing ? "playing" : "paused";
    if (!state.musicPlayer.statusMessage.isEmpty()) {
        statusLine += " | ";
        statusLine += state.musicPlayer.statusMessage;
    }
    paint.DrawStringAtUtf8(8, 44, statusLine.c_str(), &Font12, kColored);

    String currentLabel = state.musicPlayer.nowPlaying;
    if (currentLabel.isEmpty() && gMusicTrackCount > 0) {
        currentLabel = musicPlayerTrackLabelAt(min(static_cast<size_t>(state.musicPlayer.selectedIndex), gMusicTrackCount - 1));
    }
    paint.DrawStringAtUtf8(8, 62, currentLabel.isEmpty() ? "no track selected" : currentLabel.c_str(), &Font12, kColored);
    paint.DrawStringAtUtf8(8, 80, state.musicPlayer.libraryPath.c_str(), &Font12, kColored);

    const size_t totalCount = gMusicTrackCount;
    const size_t safeIndex = totalCount == 0 ? 0 : min(static_cast<size_t>(state.musicPlayer.selectedIndex), totalCount - 1);
    const size_t pageStart = (totalCount == 0) ? 0 : (safeIndex / 5) * 5;
    const size_t pageEnd = min(pageStart + 5, totalCount);
    int y = 100;
    for (size_t index = pageStart; index < pageEnd && y < 122; ++index) {
        String line = (index == safeIndex) ? "> " : "  ";
        line += String(static_cast<unsigned>(index + 1));
        line += ". ";
        line += musicPlayerTrackLabelAt(index);
        paint.DrawStringAtUtf8(8, y, line.c_str(), &Font12, kColored);
        y += 16;
    }

    paint.DrawStringAt(168, 104, "1 play/pause", &Font12, kColored);
    paint.DrawStringAt(168, 120, "2 next | 3 prev", &Font12, kColored);
    paint.DrawStringAt(168, 136, "4 rescan | <- back", &Font12, kColored);

    gEink.display(paint.GetImage());
    refreshEinkWithCadence(false);
}

bool handleMusicPlayerAppInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    (void)rawCode;
    (void)out;

    if (normalizedKey == '1' || normalizedKey == '2' || normalizedKey == '3' || normalizedKey == '4' || normalizedKey == 'r' || normalizedKey == 'R' || normalizedKey == ' ' || normalizedKey == '\r') {
        state.musicPlayer.statusMessage = "skeleton only";
        return true;
    }

    return false;
}

void resetNotesSession(SystemState &state) {
    state.notes.loaded = false;
    state.notes.dirty = false;
    state.notes.statusMessage = "select note";
    state.notes.viewMode = kNotesViewPicker;
    state.notes.pickerIndex = 0;
    state.notes.pickerScroll = 0;
    state.notes.pickerSubMode = kNotesPickerNormal;
    state.notes.pickerShowSd = false;
    gNotesScrollLine = 0;
    gNotesScrollBatchSteps = 0;
    gNotesEinkDirty = true;
    gNotesDraftWord = "";
    gNotesUndoAvailable = false;
    gNotesRenameBuffer = "";
    clearNotesTags();
    clearNotesLinks();
    notesRefreshPickerFiles(state);
    notesRefreshPickerSdFiles(state);
}

void notesRefreshPickerFiles(SystemState &state) {
    gNotesPickerFileCount = 0;
    if (!state.littleFsReady) return;

    File dir = LittleFS.open("/notes");
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return;
    }

    File entry;
    while ((entry = dir.openNextFile()) && gNotesPickerFileCount < kNotesPickerMaxFiles) {
        String name = String(entry.name());
        if (name.endsWith(".md") || name.endsWith(".txt")) {
            // entry.name() may return full path; store just the filename
            const int lastSlash = name.lastIndexOf('/');
            if (lastSlash >= 0) {
                name = name.substring(lastSlash + 1);
            }
            gNotesPickerFiles[gNotesPickerFileCount++] = name;
        }
        entry.close();
    }
    dir.close();
}

String notesPickerDisplayName(const String &path) {
    const int lastSlash = path.lastIndexOf('/');
    String name = lastSlash >= 0 ? path.substring(lastSlash + 1) : path;
    // Strip .md extension for display
    if (name.endsWith(".md")) {
        name = name.substring(0, name.length() - 3);
    } else if (name.endsWith(".txt")) {
        name = name.substring(0, name.length() - 4);
    }
    return name;
}

void notesOpenPickerSelection(SystemState &state, Stream &out) {
    const size_t count = notesPickerCurrentFileCount(state);
    if (count == 0) return;
    const size_t idx = state.notes.pickerIndex;
    if (idx >= count) return;

    // Save current note if dirty
    if (state.notes.dirty || !gNotesDraftWord.isEmpty()) {
        commitNotesDraftWord(state, false);
        saveNotes(state, out);
    }

    state.notes.filePath = notesPickerCurrentFilePath(state, idx);
    state.notes.loaded = false;
    state.notes.dirty = false;
    state.notes.viewMode = kNotesViewWrite;
    gNotesScrollLine = 0;
    gNotesScrollBatchSteps = 0;
    gNotesEinkDirty = true;
    gNotesDraftWord = "";
    gNotesCursorPos = 0; // Will be set to end after load
    clearNotesTags();
    clearNotesLinks();
    state.notes.statusMessage = "opened";
}

void notesCreateNewNote(SystemState &state, Stream &out) {
    if (!state.littleFsReady) {
        state.notes.statusMessage = "LittleFS off";
        return;
    }

    // Find next available untitled-N.md
    if (!LittleFS.exists("/notes")) {
        LittleFS.mkdir("/notes");
    }

    for (int n = 1; n <= 99; ++n) {
        String name = String("untitled-") + String(n) + String(".md");
        String path = String("/notes/") + name;
        if (!LittleFS.exists(path)) {
            // Save current note first
            if (state.notes.dirty || !gNotesDraftWord.isEmpty()) {
                commitNotesDraftWord(state, false);
                saveNotes(state, out);
            }

            state.notes.filePath = path;
            state.notes.loaded = true;
            state.notes.dirty = false;
            state.notes.viewMode = kNotesViewWrite;
            gNotesBuffer = "";
            gNotesDraftWord = "";
            gNotesCursorPos = 0;
            gNotesScrollLine = 0;
            gNotesEinkDirty = true;
            clearNotesTags();
            clearNotesLinks();
            state.notes.statusMessage = "new note";
            out.print("Notes: created ");
            out.println(path);
            return;
        }
    }

    state.notes.statusMessage = "too many notes";
}

bool handleNotesAppInput(SystemState &state, char normalizedKey, uint8_t rawCode, bool &refreshEink, Stream &out) {
    refreshEink = false;

    // --- Picker mode ---
    if (state.notes.viewMode == kNotesViewPicker) {
        // --- Rename sub-mode ---
        if (state.notes.pickerSubMode == kNotesPickerRename) {
            if (static_cast<uint8_t>(normalizedKey) == 0x1B) {
                // Esc: cancel rename
                state.notes.pickerSubMode = kNotesPickerNormal;
                gNotesRenameBuffer = "";
                state.notes.statusMessage = "rename cancelled";
                gNotesEinkDirty = true;
                refreshEink = true;
                return true;
            }
            if (normalizedKey == '\r' || normalizedKey == '\n') {
                // Enter: confirm rename
                if (gNotesRenameBuffer.length() > 0) {
                    const size_t count = notesPickerCurrentFileCount(state);
                    if (count > 0 && state.notes.pickerIndex < count) {
                        const String oldPath = notesPickerCurrentFilePath(state, state.notes.pickerIndex);
                        String newName = gNotesRenameBuffer;
                        if (!newName.endsWith(".md") && !newName.endsWith(".txt")) {
                            newName += ".md";
                        }
                        const String newPath = String("/notes/") + newName;
                        fs::FS &fs = state.notes.pickerShowSd
                            ? static_cast<fs::FS &>(SD) : static_cast<fs::FS &>(LittleFS);
                        if (fs.rename(oldPath, newPath)) {
                            state.notes.statusMessage = "renamed";
                            out.print("Notes: renamed to ");
                            out.println(newPath);
                        } else {
                            state.notes.statusMessage = "rename failed";
                        }
                        if (state.notes.pickerShowSd) {
                            notesRefreshPickerSdFiles(state);
                        } else {
                            notesRefreshPickerFiles(state);
                        }
                    }
                }
                state.notes.pickerSubMode = kNotesPickerNormal;
                gNotesRenameBuffer = "";
                gNotesEinkDirty = true;
                refreshEink = true;
                return true;
            }
            if (normalizedKey == 8) {
                // Backspace
                if (gNotesRenameBuffer.length() > 0) {
                    gNotesRenameBuffer = gNotesRenameBuffer.substring(0, gNotesRenameBuffer.length() - 1);
                }
                return true;
            }
            if (normalizedKey >= 32 && normalizedKey <= 126) {
                if (gNotesRenameBuffer.length() < 30) {
                    gNotesRenameBuffer += normalizedKey;
                }
                return true;
            }
            return true;
        }

        // --- Normal picker ---
        if (isCardKbLeftArrowCode(rawCode)) {
            return false; // Let global handler go to launcher
        }

        const size_t fileCount = notesPickerCurrentFileCount(state);

        if (isCardKbUpArrowCode(rawCode)) {
            if (state.notes.pickerIndex > 0) {
                --state.notes.pickerIndex;
                if (state.notes.pickerIndex < state.notes.pickerScroll) {
                    state.notes.pickerScroll = state.notes.pickerIndex;
                }
                gNotesEinkDirty = true;
                refreshEink = true;
            }
            return true;
        }

        if (isCardKbDownArrowCode(rawCode)) {
            if (fileCount > 0 && state.notes.pickerIndex < fileCount - 1) {
                ++state.notes.pickerIndex;
                if (state.notes.pickerIndex >= state.notes.pickerScroll + kNotesPickerVisibleOled) {
                    state.notes.pickerScroll = state.notes.pickerIndex - kNotesPickerVisibleOled + 1;
                }
                gNotesEinkDirty = true;
                refreshEink = true;
            }
            return true;
        }

        if (isCardKbRightArrowCode(rawCode) || normalizedKey == '\r' || normalizedKey == '\n') {
            if (fileCount > 0) {
                notesOpenPickerSelection(state, out);
                gNotesEinkDirty = true;
                refreshEink = true;
            }
            return true;
        }

        if (normalizedKey == 'n' || normalizedKey == 'N') {
            notesCreateNewNote(state, out);
            gNotesEinkDirty = true;
            refreshEink = true;
            return true;
        }

        if (normalizedKey == 'd' || normalizedKey == 'D') {
            if (fileCount > 0 && state.notes.pickerIndex < fileCount) {
                const String path = notesPickerCurrentFilePath(state, state.notes.pickerIndex);
                fs::FS &fs = state.notes.pickerShowSd
                    ? static_cast<fs::FS &>(SD) : static_cast<fs::FS &>(LittleFS);
                if (fs.exists(path)) {
                    fs.remove(path);
                    out.print("Notes: deleted ");
                    out.println(path);
                    state.notes.statusMessage = "deleted";
                }
                if (state.notes.pickerShowSd) {
                    notesRefreshPickerSdFiles(state);
                } else {
                    notesRefreshPickerFiles(state);
                }
                const size_t newCount = notesPickerCurrentFileCount(state);
                if (state.notes.pickerIndex >= newCount && newCount > 0) {
                    state.notes.pickerIndex = newCount - 1;
                }
                gNotesEinkDirty = true;
                refreshEink = true;
            }
            return true;
        }

        if (normalizedKey == 'r' || normalizedKey == 'R') {
            if (fileCount > 0 && state.notes.pickerIndex < fileCount) {
                state.notes.pickerSubMode = kNotesPickerRename;
                const String &currentName = notesPickerCurrentFileName(state, state.notes.pickerIndex);
                // Pre-fill with current name without extension
                gNotesRenameBuffer = notesPickerDisplayName(currentName);
                state.notes.statusMessage = "rename: type name";
                gNotesEinkDirty = true;
                refreshEink = true;
            }
            return true;
        }

        if (normalizedKey == 'c' || normalizedKey == 'C') {
            // Copy selected note to the other storage (FS→SD or SD→FS)
            if (fileCount > 0 && state.notes.pickerIndex < fileCount) {
                const String srcPath = notesPickerCurrentFilePath(state, state.notes.pickerIndex);
                fs::FS &srcFs = state.notes.pickerShowSd
                    ? static_cast<fs::FS &>(SD) : static_cast<fs::FS &>(LittleFS);
                fs::FS &dstFs = state.notes.pickerShowSd
                    ? static_cast<fs::FS &>(LittleFS) : static_cast<fs::FS &>(SD);
                const bool dstReady = state.notes.pickerShowSd ? state.littleFsReady : state.sdReady;

                if (!dstReady) {
                    state.notes.statusMessage = "dest not ready";
                } else {
                    if (!dstFs.exists("/notes")) dstFs.mkdir("/notes");
                    File src = srcFs.open(srcPath, "r");
                    if (!src) {
                        state.notes.statusMessage = "read failed";
                    } else {
                        File dst = dstFs.open(srcPath, "w");
                        if (!dst) {
                            state.notes.statusMessage = "write failed";
                            src.close();
                        } else {
                            while (src.available()) {
                                dst.write(static_cast<uint8_t>(src.read()));
                            }
                            dst.close();
                            src.close();
                            const String destName = state.notes.pickerShowSd ? String("LittleFS") : String("SD");
                            state.notes.statusMessage = String("copied->") + destName;
                            out.print("Notes: copied to ");
                            out.println(destName);
                        }
                    }
                }
                gNotesEinkDirty = true;
                refreshEink = true;
            }
            return true;
        }

        if (normalizedKey == 's' || normalizedKey == 'S') {
            state.notes.pickerShowSd = !state.notes.pickerShowSd;
            state.notes.pickerIndex = 0;
            state.notes.pickerScroll = 0;
            if (state.notes.pickerShowSd) {
                notesRefreshPickerSdFiles(state);
                state.notes.statusMessage = "SD notes";
            } else {
                notesRefreshPickerFiles(state);
                state.notes.statusMessage = "LittleFS notes";
            }
            gNotesEinkDirty = true;
            refreshEink = true;
            return true;
        }

        // Consume other keys in picker
        return true;
    }

    ensureNotesLoaded(state, out);
    const bool writeMode = state.notes.viewMode == kNotesViewWrite;

    // Esc → save + go to picker (from either mode)
    if (static_cast<uint8_t>(normalizedKey) == 0x1B) {
        if (state.notes.dirty) {
            saveNotes(state, out);
        }
        state.notes.viewMode = kNotesViewPicker;
        state.notes.statusMessage = "select note";
        notesRefreshPickerFiles(state);
        gNotesEinkDirty = true;
        refreshEink = true;
        return true;
    }

    // Tab → toggle write/read
    if (normalizedKey == '\t') {
        if (state.notes.viewMode == kNotesViewRead) {
            state.notes.viewMode = kNotesViewWrite;
            state.notes.statusMessage = "mode: write";
            gNotesCursorPos = gNotesBuffer.length();
            notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        } else {
            if (state.notes.dirty) {
                saveNotes(state, out);
            }
            refreshNotesMetadataFromBuffer();
            state.notes.viewMode = kNotesViewRead;
            state.notes.statusMessage = "mode: read";
        }
        gNotesEinkDirty = true;
        gNotesScrollBatchSteps = 0;
        refreshEink = true;
        return true;
    }

    // --- Read mode ---
    if (!writeMode) {
        if (isCardKbLeftArrowCode(rawCode)) {
            if (state.notes.dirty) {
                saveNotes(state, out);
            }
            state.notes.viewMode = kNotesViewPicker;
            state.notes.statusMessage = "select note";
            notesRefreshPickerFiles(state);
            gNotesEinkDirty = true;
            refreshEink = true;
            return true;
        }

        if (isCardKbRightArrowCode(rawCode)) {
            saveNotes(state, out);
            refreshNotesMetadataFromBuffer();
            gNotesEinkDirty = true;
            refreshEink = true;
            return true;
        }

        if (normalizedKey == 't' || normalizedKey == 'T') {
            if (gNotesTagCount == 0) {
                gNotesActiveTagIndex = kNotesTagFilterAll;
                state.notes.statusMessage = "no tags";
            } else if (gNotesActiveTagIndex == kNotesTagFilterAll) {
                gNotesActiveTagIndex = 0;
                state.notes.statusMessage = notesFilterLabel();
            } else {
                ++gNotesActiveTagIndex;
                if (gNotesActiveTagIndex >= static_cast<int>(gNotesTagCount)) {
                    gNotesActiveTagIndex = kNotesTagFilterAll;
                }
                state.notes.statusMessage = notesFilterLabel();
            }
            gNotesScrollLine = 0;
            gNotesEinkDirty = true;
            gNotesScrollBatchSteps = 0;
            refreshEink = true;
            return true;
        }

        if (isCardKbUpArrowCode(rawCode) || isCardKbDownArrowCode(rawCode)) {
            const String displayText = notesComposeFilteredDisplayText(false);
            const size_t maxScroll = notesMaxScrollLine(displayText, kNotesVisibleLinesEink);
            if (isCardKbUpArrowCode(rawCode)) {
                if (gNotesScrollLine > 0) --gNotesScrollLine;
            } else if (gNotesScrollLine < maxScroll) {
                ++gNotesScrollLine;
            }
            state.notes.statusMessage = String("scroll ") + String(static_cast<unsigned>(gNotesScrollLine + 1)) + String("/") + String(static_cast<unsigned>(maxScroll + 1));
            ++gNotesScrollBatchSteps;
            const uint32_t nowMs = millis();
            if (gNotesScrollBatchSteps >= kNotesScrollBatchThreshold || (nowMs - gNotesLastEinkRenderMs) >= kNotesScrollBatchTimeoutMs) {
                refreshEink = true;
                gNotesEinkDirty = true;
                gNotesScrollBatchSteps = 0;
            }
            return true;
        }

        // Enter in read mode: toggle checklist if on a checkbox line
        if (normalizedKey == '\r' || normalizedKey == '\n') {
            notesToggleChecklistAtCursor(state);
            gNotesEinkDirty = true;
            refreshEink = true;
            return true;
        }

        if (normalizedKey == 8 || normalizedKey == ' ' || (normalizedKey >= 32 && normalizedKey <= 126)) {
            state.notes.statusMessage = "read only";
            return true;
        }
        return false;
    }

    // --- Write mode: cursor-based editing ---

    // Search mode active: handle search input
    if (gNotesSearchActive) {
        if (static_cast<uint8_t>(normalizedKey) == 0x1B) {
            // Esc: exit search
            gNotesSearchActive = false;
            gNotesSearchQuery = "";
            gNotesSearchMatchPos = -1;
            state.notes.statusMessage = "search off";
            return true;
        }
        if (normalizedKey == '\r' || normalizedKey == '\n') {
            // Enter: find next
            notesSearchNext();
            if (gNotesSearchMatchPos >= 0) {
                state.notes.statusMessage = String("found @") + String(gNotesSearchMatchPos);
                notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
                gNotesEinkDirty = true;
                refreshEink = true;
            } else {
                state.notes.statusMessage = "not found";
            }
            return true;
        }
        if (normalizedKey == 8) {
            if (gNotesSearchQuery.length() > 0) {
                gNotesSearchQuery = gNotesSearchQuery.substring(0, gNotesSearchQuery.length() - 1);
            }
            state.notes.statusMessage = String("find:") + gNotesSearchQuery;
            return true;
        }
        if (normalizedKey >= 32 && normalizedKey <= 126) {
            if (gNotesSearchQuery.length() < 20) {
                gNotesSearchQuery += normalizedKey;
            }
            state.notes.statusMessage = String("find:") + gNotesSearchQuery;
            return true;
        }
        return true;
    }

    // Ctrl+Z (0x1A) → undo
    if (static_cast<uint8_t>(normalizedKey) == 0x1A) {
        notesRestoreUndo(state);
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        refreshEink = true;
        return true;
    }

    // Ctrl+F (0x06) → search
    if (static_cast<uint8_t>(normalizedKey) == 0x06) {
        gNotesSearchActive = true;
        gNotesSearchQuery = "";
        gNotesSearchMatchPos = -1;
        state.notes.statusMessage = "find:";
        return true;
    }

    // Ctrl+D (0x04) → insert date/time stamp
    if (static_cast<uint8_t>(normalizedKey) == 0x04) {
        String stamp = getRtcTimestamp(state);
        if (stamp.length() == 0) {
            stamp = String("(no RTC)");
        }
        notesInsertStringAtCursor(stamp, state);
        state.notes.statusMessage = "date inserted";
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        refreshEink = true;
        return true;
    }

    // Ctrl+T (0x14) → insert checklist item
    if (static_cast<uint8_t>(normalizedKey) == 0x14) {
        notesInsertStringAtCursor(String("- [ ] "), state);
        state.notes.statusMessage = "checkbox added";
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        refreshEink = true;
        return true;
    }

    // Ctrl+X (0x18) → toggle checklist on current line
    if (static_cast<uint8_t>(normalizedKey) == 0x18) {
        notesToggleChecklistAtCursor(state);
        gNotesEinkDirty = true;
        refreshEink = true;
        return true;
    }

    // Arrow keys move cursor
    if (isCardKbLeftArrowCode(rawCode)) {
        notesMoveCursorLeft();
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        return true;
    }

    if (isCardKbRightArrowCode(rawCode)) {
        notesMoveCursorRight();
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        return true;
    }

    if (isCardKbUpArrowCode(rawCode)) {
        notesMoveCursorUp(kNotesWrapWidthOled);
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        return true;
    }

    if (isCardKbDownArrowCode(rawCode)) {
        notesMoveCursorDown(kNotesWrapWidthOled);
        notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
        gNotesEinkDirty = true;
        return true;
    }

    // Backspace: delete character before cursor
    if (normalizedKey == 8) {
        if (gNotesCursorPos > 0) {
            notesSnapshotUndo();
            notesDeleteAtCursor();
            state.notes.dirty = true;
            state.notes.statusMessage = "editing";
            notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
            gNotesEinkDirty = true;
        }
        return true;
    }

    // Enter: insert newline at cursor
    if (isNotesEnterKey(normalizedKey, rawCode)) {
        if (gNotesBuffer.length() < kNotesMaxChars) {
            notesSnapshotUndo();
            notesInsertAtCursor('\n');
            state.notes.dirty = true;
            state.notes.statusMessage = "new line";
            notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
            gNotesEinkDirty = true;
            refreshEink = true;
        } else {
            state.notes.statusMessage = "note full";
        }
        return true;
    }

    // Printable characters (including space): insert at cursor
    if (normalizedKey >= 32 && normalizedKey <= 126) {
        if (gNotesBuffer.length() < kNotesMaxChars) {
            notesSnapshotUndo();
            notesInsertAtCursor(normalizedKey);
            state.notes.dirty = true;
            state.notes.statusMessage = "typing";
            notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);
            gNotesEinkDirty = true;
        } else {
            state.notes.statusMessage = "note full";
        }
        return true;
    }

    return false;
}

bool renderMusicPlayerScreen(SystemState &state, bool oledOnly, Stream &out) {
    state.musicPlayer.playing = false;
    state.musicPlayer.nowPlaying = "";
    if (state.musicPlayer.libraryPath.isEmpty()) {
        state.musicPlayer.libraryPath = "/music-player";
    }
    if (state.musicPlayer.statusMessage.isEmpty() || state.musicPlayer.statusMessage.equalsIgnoreCase("ready")) {
        state.musicPlayer.statusMessage = "skeleton mode";
    }

    return renderStatusScreen(
        state,
        "MUSIC PLAYER",
        "skeleton pripraven",
        "ceka na druhe ESP",
        "<- zpet | status: " + state.musicPlayer.statusMessage,
        oledOnly,
        out);
}

bool renderNotesScreen(SystemState &state, bool oledOnly, Stream &out) {
    // Auto-save check
    notesAutoSaveCheck(state, out);

    const bool pickerMode = state.notes.viewMode == kNotesViewPicker;
    if (!pickerMode) {
        ensureNotesLoaded(state, out);
    }
    const bool writeMode = state.notes.viewMode == kNotesViewWrite;
    const String modeLine = writeMode ? String("WRITE") : String("READ");
    const String displayText = pickerMode ? String("") : notesComposeFilteredDisplayText(writeMode);
    const size_t totalLines = pickerMode ? 0 : notesCountLines(displayText);
    const size_t maxScroll = pickerMode ? 0 : notesMaxScrollLine(displayText, kNotesVisibleLinesEink);
    if (!pickerMode && gNotesScrollLine > maxScroll) {
        gNotesScrollLine = maxScroll;
    }

    const String noteName = notesPickerDisplayName(state.notes.filePath);
    const String charCount = String(static_cast<unsigned>(gNotesBuffer.length() + gNotesDraftWord.length()))
                           + String("/")
                           + String(static_cast<unsigned>(kNotesMaxChars));

    if (state.oledReady && state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        gOled.clearBuffer();
        gOled.setFont(u8g2_font_6x10_tf);
        gOled.drawFrame(0, 0, 128, 64);
        gOled.drawLine(0, 13, 127, 13);

        if (pickerMode) {
            // --- Note picker ---
            const size_t fileCount = notesPickerCurrentFileCount(state);
            const String storageTag = state.notes.pickerShowSd ? String("[SD]") : String("[FS]");
            String headLine = storageTag + String(" ");
            headLine += String(static_cast<unsigned>(fileCount));
            headLine += " files";
            gOled.drawStr(3, 10, headLine.c_str());

            if (state.notes.pickerSubMode == kNotesPickerRename) {
                String renameLine = String("R:") + gNotesRenameBuffer + String("_");
                renameLine = notesClipLine(renameLine, 20);
                gOled.drawStr(3, 21, renameLine.c_str());
            } else {
                gOled.drawStr(3, 21, "n d r s c ->=open");
            }

            gOled.drawFrame(2, 24, 124, 38);
            if (fileCount == 0) {
                gOled.drawStr(5, 37, "(no notes yet)");
            } else {
                for (size_t i = 0; i < kNotesPickerVisibleOled; ++i) {
                    const size_t fileIdx = state.notes.pickerScroll + i;
                    if (fileIdx >= fileCount) break;
                    String label = notesPickerDisplayName(notesPickerCurrentFileName(state, fileIdx));
                    const bool selected = fileIdx == state.notes.pickerIndex;
                    String displayLine = selected ? String(">") + label : String(" ") + label;
                    displayLine = notesClipLine(displayLine, 19);
                    const int y = 33 + static_cast<int>(i) * 9;
                    gOled.drawStr(5, y, displayLine.c_str());
                }
            }
        } else if (writeMode) {
            // Cursor-based write mode with visual wrapping
            size_t vLine, vCol;
            notesCursorVisualPos(kNotesWrapWidthOled, vLine, vCol);
            notesScrollToCursorVisual(kNotesVisibleLinesOled, kNotesWrapWidthOled);

            String headLine = notesClipLine(noteName, 10);
            headLine += state.notes.dirty ? " W*" : " W";
            gOled.drawStr(3, 10, headLine.c_str());
            gOled.drawStr(80, 10, charCount.c_str());

            String statusShort = notesClipLine(state.notes.statusMessage, 20);
            gOled.drawStr(3, 21, statusShort.c_str());

            gOled.drawFrame(2, 24, 124, 38);
            for (size_t i = 0; i < kNotesVisibleLinesOled; ++i) {
                const size_t lineIndex = gNotesScrollLine + i;
                String line = notesWrappedLineAt(kNotesWrapWidthOled, lineIndex);
                if (lineIndex == vLine) {
                    if (vCol <= line.length()) {
                        line = line.substring(0, vCol) + String("_") + line.substring(vCol);
                    } else {
                        line += "_";
                    }
                }
                line = notesClipLine(line.isEmpty() ? String(" ") : line, 20);

                const int y = 33 + static_cast<int>(i) * 9;
                gOled.drawStr(5, y, line.c_str());
            }
        } else {
            String headLine = notesClipLine(noteName, 10);
            headLine += state.notes.dirty ? " R*" : " R";
            String posLine = String(static_cast<unsigned>(gNotesScrollLine + 1)) + String("/") + String(static_cast<unsigned>(totalLines));
            const size_t oledMaxScroll = notesMaxScrollLine(displayText, kNotesVisibleLinesOled);

            gOled.drawStr(3, 10, headLine.c_str());
            gOled.drawStr(98, 10, posLine.c_str());

            String statusShort = notesClipLine(state.notes.statusMessage, 12);
            gOled.drawStr(3, 21, statusShort.c_str());
            String filterShort = notesClipLine(notesFilterLabel(), 12);
            gOled.drawStr(68, 21, filterShort.c_str());

            gOled.drawFrame(2, 24, 118, 38);
            for (size_t i = 0; i < kNotesVisibleLinesOled; ++i) {
                const size_t lineIndex = gNotesScrollLine + i;
                String line = notesLineAt(displayText, lineIndex);
                line = notesStyleLineForBrowse(line);
                line = notesClipLine(line.isEmpty() ? String(" ") : line, 18);
                const int y = 33 + static_cast<int>(i) * 9;
                gOled.drawStr(5, y, line.c_str());
            }

            // Slim scrollbar for read-mode orientation.
            const int barX = 123;
            const int barY = 24;
            const int barH = 38;
            gOled.drawFrame(barX, barY, 3, barH);
            const int thumbH = 6;
            int thumbY = barY + 1;
            if (oledMaxScroll > 0) {
                thumbY = barY + 1 + static_cast<int>(((barH - 2 - thumbH) * gNotesScrollLine) / oledMaxScroll);
            }
            gOled.drawBox(barX + 1, thumbY, 1, thumbH);
        }

        gOled.sendBuffer();
        xSemaphoreGive(state.spiMutex);
    }

    if (oledOnly) {
        return state.oledReady;
    }

    if (!ensureEinkInitialized(state, out)) {
        return state.oledReady;
    }

    const size_t pickerFileCount = pickerMode ? notesPickerCurrentFileCount(state) : 0;
    const uint32_t viewportSig = pickerMode
        ? static_cast<uint32_t>(pickerFileCount * 100 + state.notes.pickerIndex
            + (state.notes.pickerShowSd ? 50000 : 0)
            + (state.notes.pickerSubMode == kNotesPickerRename ? 25000 : 0))
        : notesViewportSignature(displayText, writeMode, gNotesScrollLine);
    if (!gNotesEinkDirty && viewportSig == gNotesLastViewportSignature) {
        noteDisplayActivity();
        return true;
    }

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
        prepareLandscapePaint(paint);
        paint.Clear(kUncolored);
        paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);

        if (pickerMode) {
            // --- E-ink picker ---
            const size_t fileCount = notesPickerCurrentFileCount(state);
            const String storageTag = state.notes.pickerShowSd ? String("[SD]") : String("[LittleFS]");
            String topLine = storageTag + String("  ") + String(static_cast<unsigned>(fileCount)) + String(" files");
            paint.DrawStringAtUtf8(8, 12, topLine.c_str(), &Font16, kColored);
            paint.DrawLine(0, 20, kEinkLandscapeWidth - 1, 20, kColored);

            if (state.notes.pickerSubMode == kNotesPickerRename) {
                String renameLine = String("Rename: ") + gNotesRenameBuffer + String("_");
                renameLine = notesClipLine(renameLine, 52);
                paint.DrawStringAt(8, 34, renameLine.c_str(), &Font12, kColored);
            } else {
                paint.DrawStringAt(8, 34, "n=new d=del r=rename s=SD/FS c=copy", &Font12, kColored);
            }

            const int textLeft = 8;
            const int textTop = 42;
            const int textRight = kEinkLandscapeWidth - 18;
            const int textBottom = 196;
            paint.DrawRectangle(textLeft - 2, textTop - 4, textRight + 2, textBottom, kColored);

            if (fileCount == 0) {
                paint.DrawStringAt(textLeft, 70, "(no notes yet - press N to create)", &Font12, kColored);
            } else {
                int py = 56;
                for (size_t i = 0; i < kNotesVisibleLinesEink && py <= 186; ++i) {
                    const size_t fileIdx = state.notes.pickerScroll + i;
                    if (fileIdx >= fileCount) break;
                    const bool selected = fileIdx == state.notes.pickerIndex;
                    String label = selected ? String("> ") : String("  ");
                    label += notesPickerCurrentFileName(state, fileIdx);
                    label = notesClipLine(label, 52);
                    paint.DrawStringAt(textLeft, py, label.c_str(), &Font12, kColored);
                    py += 16;
                }
            }
        } else {
            String topLine = noteName + (writeMode
                ? (state.notes.dirty ? String(" WRITE*") : String(" WRITE"))
                : (state.notes.dirty ? String(" READ*") : String(" READ")));
            topLine = notesClipLine(topLine, 28);
            String posLine = String("line ") + String(static_cast<unsigned>(gNotesScrollLine + 1)) + String("/") + String(static_cast<unsigned>(totalLines));
            String statusLine = charCount + String("  ") + notesClipLine(state.notes.statusMessage, 40);
            paint.DrawStringAtUtf8(8, 12, topLine.c_str(), &Font16, kColored);
            paint.DrawStringAtUtf8(188, 12, posLine.c_str(), &Font12, kColored);
            paint.DrawLine(0, 20, kEinkLandscapeWidth - 1, 20, kColored);
            paint.DrawStringAtUtf8(8, 34, statusLine.c_str(), &Font12, kColored);

            const int textLeft = 8;
            const int textTop = 42;
            const int textRight = kEinkLandscapeWidth - 18;
            const int textBottom = 196;
            paint.DrawRectangle(textLeft - 2, textTop - 4, textRight + 2, textBottom, kColored);

            // For e-ink write mode: compute visual scroll independently
            size_t einkScrollLine = gNotesScrollLine;
            size_t eVLine = 0, eVCol = 0;
            if (writeMode) {
                notesCursorVisualPos(kNotesWrapWidthEink, eVLine, eVCol);
                if (eVLine < einkScrollLine) {
                    einkScrollLine = eVLine;
                } else if (eVLine >= einkScrollLine + kNotesVisibleLinesEink) {
                    einkScrollLine = eVLine - kNotesVisibleLinesEink + 1;
                }
            }

            int y = 56;
            const int textInnerBottom = textBottom - 4;
            for (size_t i = 0; i < kNotesVisibleLinesEink && y <= 186; ++i) {
                const size_t lineIndex = (writeMode ? einkScrollLine : gNotesScrollLine) + i;
                String line = writeMode
                    ? notesWrappedLineAt(kNotesWrapWidthEink, lineIndex)
                    : notesLineAt(displayText, lineIndex);

                if (writeMode && lineIndex == eVLine) {
                    if (eVCol <= line.length()) {
                        line = line.substring(0, eVCol) + String("_") + line.substring(eVCol);
                    } else {
                        line += "_";
                    }
                }

                const uint8_t level = !writeMode ? notesHeadingLevel(line) : 0;
                if (!writeMode) {
                    line = notesStyleLineForBrowse(line);
                }
                const bool largeHeading = level > 0 && level <= 2;
                const int lineAdvance = largeHeading ? 20 : 16;
                if (y + lineAdvance > textInnerBottom) {
                    break;
                }

                line = notesClipLine(line.isEmpty() ? String(" ") : line, largeHeading ? 42 : 52);

                if (largeHeading) {
                    paint.DrawStringAtUtf8(textLeft, y, line.c_str(), &Font16, kColored);
                } else {
                    paint.DrawStringAtUtf8(textLeft, y, line.c_str(), &Font12, kColored);
                }

                y += lineAdvance;
            }

            // Right-side scrollbar.
            const int sbX = kEinkLandscapeWidth - 12;
            const int sbY = textTop;
            const int sbH = textBottom - textTop - 4;
            paint.DrawRectangle(sbX, sbY, sbX + 6, sbY + sbH, kColored);
            int sbThumbY = sbY + 2;
            const int sbThumbH = 12;
            if (maxScroll > 0) {
                sbThumbY = sbY + 2 + static_cast<int>(((sbH - 4 - sbThumbH) * gNotesScrollLine) / maxScroll);
            }
            paint.DrawRectangle(sbX + 1, sbThumbY, sbX + 5, sbThumbY + sbThumbH, kColored);
        }

        paint.DrawLine(0, 206, kEinkLandscapeWidth - 1, 206, kColored);
        if (pickerMode) {
            paint.DrawStringAt(8, 220, "n=new d=del r=rename s=SD/FS c=copy", &Font12, kColored);
            paint.DrawStringAt(8, 236, "up/dn=nav  enter=open  left=launcher", &Font12, kColored);
        } else {
            paint.DrawStringAt(8, 220, "^F=find ^D=date ^T=todo ^X=toggle ^Z=undo", &Font12, kColored);
            paint.DrawStringAt(8, 236, "tab=mode esc=list enter=check(read) t=tag", &Font12, kColored);
        }

        gEink.display(paint.GetImage());
        refreshEinkWithCadence(false);
        gNotesLastViewportSignature = viewportSig;
        gNotesLastEinkRenderMs = millis();
        gNotesEinkDirty = false;
        xSemaphoreGive(state.spiMutex);
    }

    noteDisplayActivity();
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
    refreshLauncherSdApps(state, out);

    const size_t totalCount = launcherTotalItemCount();
    if (totalCount > 0 && state.launcher.selectedIndex >= totalCount) {
        state.launcher.selectedIndex = static_cast<uint8_t>(totalCount - 1);
    }

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

        const size_t pageCount = totalCount == 0 ? 1 : ((totalCount + kLauncherPageSize - 1) / kLauncherPageSize);
        const size_t pageIndex = totalCount == 0 ? 0 : (state.launcher.selectedIndex / kLauncherPageSize);
        String pageLabel = String("page ") + String(static_cast<unsigned>(pageIndex + 1)) + "/" + String(static_cast<unsigned>(pageCount));
        paint.DrawStringAt(200, 26, pageLabel.c_str(), &Font12, kColored);
        paint.DrawLine(0, 38, kEinkLandscapeWidth - 1, 38, kColored);

        const int cardWidth = 170;
        const int cardHeight = 50;
        const int startX = 8;
        const int startY = 46;
        const int xGap = 8;
        const int yGap = 8;
        const size_t pageStart = pageIndex * kLauncherPageSize;
        const size_t pageEnd = min(pageStart + kLauncherPageSize, totalCount);

        for (size_t index = pageStart; index < pageEnd; ++index) {
            const size_t localIndex = index - pageStart;
            const int column = static_cast<int>(localIndex % 2);
            const int row = static_cast<int>(localIndex / 2);
            const int x = startX + column * (cardWidth + xGap);
            const int y = startY + row * (cardHeight + yGap);
            const bool selected = index == state.launcher.selectedIndex;
            String numberedTitle = String(static_cast<unsigned>(localIndex + 1));
            numberedTitle += ".";
            numberedTitle += launcherItemTitleAt(index);
            drawLauncherIcon(paint, x + 8, y + 8, 20, launcherItemIdAt(index));
            drawLauncherCard(paint, x, y, cardWidth, cardHeight, numberedTitle, selected);
        }

        paint.DrawStringAt(8, 220, "1..6 open, up/down page", &Font12, kColored);

        gEink.display(paint.GetImage());
        refreshEinkWithCadence(false);
        xSemaphoreGive(state.spiMutex);
    }

    markDisplayActivity();
    out.println("E-ink: launcher rendered");
    return true;
}

bool renderFileManagerScreen(SystemState &state, bool oledOnly, Stream &out) {
    if (state.fileManager.currentPath.isEmpty()) {
        state.fileManager.currentPath = "/";
    }

    if (state.fileManager.viewMode == kFileManagerViewBrowse) {
        fileManagerRefreshListing(state, out);
        const size_t totalCount = gFileManagerEntryCount;
        if (totalCount > 0 && state.fileManager.selectedIndex >= totalCount) {
            state.fileManager.selectedIndex = static_cast<uint8_t>(totalCount - 1);
        }

        if (totalCount > 0) {
            const size_t maxOffset = totalCount > kFileManagerPageSize ? totalCount - kFileManagerPageSize : size_t(0);
            if (state.fileManager.scrollOffset > maxOffset) {
                state.fileManager.scrollOffset = maxOffset;
            }
            if (state.fileManager.selectedIndex < state.fileManager.scrollOffset || state.fileManager.selectedIndex >= state.fileManager.scrollOffset + kFileManagerPageSize) {
                state.fileManager.selectedIndex = static_cast<uint8_t>(state.fileManager.scrollOffset);
            }
        } else {
            state.fileManager.selectedIndex = 0;
            state.fileManager.scrollOffset = 0;
        }
    }

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        drawFileManagerOled(state);
        xSemaphoreGive(state.spiMutex);
    }

    if (oledOnly) {
        return state.oledReady;
    }

    if (!ensureEinkInitialized(state, out)) {
        out.println("OLED: file manager rendered");
        return state.oledReady;
    }

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        drawFileManagerEink(state);
        xSemaphoreGive(state.spiMutex);
    }

    markDisplayActivity();
    out.println("E-ink: file manager rendered");
    return true;
}

void invalidateLauncherSdAppCache() {
    gSdAppCacheDirty = true;
    gSdAppCachePrimed = false;
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

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        drawSettingsEink(state);
        xSemaphoreGive(state.spiMutex);
    }
    markDisplayActivity();
    out.println("Settings app rendered");
    return true;
}

bool renderStatusScreen(SystemState &state, const String &title, const String &line1, const String &line2, const String &line3, bool oledOnly, Stream &out) {
    if (state.oledReady && state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        gOled.clearBuffer();
        gOled.setFont(u8g2_font_6x10_tf);
        gOled.drawStr(0, 12, title.c_str());
        gOled.drawStr(0, 28, line1.c_str());
        gOled.drawStr(0, 42, line2.c_str());
        gOled.drawStr(0, 56, line3.c_str());
        gOled.sendBuffer();
        xSemaphoreGive(state.spiMutex);
    }

    if (oledOnly) {
        return state.oledReady;
    }

    if (!ensureEinkInitialized(state, out)) {
        return state.oledReady;
    }

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
        prepareLandscapePaint(paint);
        paint.Clear(kUncolored);
        paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
        paint.DrawStringAt(8, 10, title.c_str(), &Font16, kColored);
        paint.DrawLine(0, 32, kEinkLandscapeWidth - 1, 32, kColored);
        paint.DrawStringAt(8, 50, line1.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 70, line2.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 90, line3.c_str(), &Font12, kColored);
        gEink.display(paint.GetImage());
        refreshEinkWithCadence(false);
        xSemaphoreGive(state.spiMutex);
    }

    noteDisplayActivity();
    return true;
}

// --- Desktop terminal input handler ---
bool handleDesktopTerminalInput(SystemState &state, char normalizedKey, uint8_t rawCode, bool &refreshEink, Stream &out) {
    refreshEink = false;

    // Esc: clear terminal
    if (static_cast<uint8_t>(normalizedKey) == 0x1B) {
        gTermLineCount = 0;
        gTermScroll = 0;
        gTermInputBuffer = "";
        gTermEinkDirty = true;
        refreshEink = true;
        return true;
    }

    // Up arrow: scroll up
    if (isCardKbUpArrowCode(rawCode)) {
        if (gTermScroll > 0) {
            --gTermScroll;
            gTermEinkDirty = true;
        }
        return true;
    }

    // Down arrow: scroll down
    if (isCardKbDownArrowCode(rawCode)) {
        if (gTermLineCount > kTermVisibleOled && gTermScroll < gTermLineCount - kTermVisibleOled) {
            ++gTermScroll;
            gTermEinkDirty = true;
        }
        return true;
    }

    // Backspace
    if (normalizedKey == 8) {
        if (gTermInputBuffer.length() > 0) {
            gTermInputBuffer = gTermInputBuffer.substring(0, gTermInputBuffer.length() - 1);
        }
        return true;
    }

    // Enter: execute command
    if (normalizedKey == '\r' || normalizedKey == '\n') {
        if (gTermInputBuffer.length() > 0) {
            // Show the command in terminal
            termAddLine(String("$ ") + gTermInputBuffer);

            // Execute via shell
            if (gTermTaskManager) {
                StringStream capture;
                executeShellCommand(state, *gTermTaskManager, gTermInputBuffer, capture);
                if (capture.buffer.length() > 0) {
                    termAddOutput(capture.buffer);
                }
            } else {
                termAddLine("(no task manager)");
            }

            gTermInputBuffer = "";
            gTermEinkDirty = true;
            refreshEink = true;
        }
        return true;
    }

    // Printable characters
    if (normalizedKey >= 32 && normalizedKey <= 126) {
        if (gTermInputBuffer.length() < 60) {
            gTermInputBuffer += normalizedKey;
        }
        return true;
    }

    return false;
}

// --- Fastfetch-style system info ---
String desktopUptimeString() {
    const uint32_t ms = millis();
    const uint32_t secs = ms / 1000;
    const uint32_t mins = secs / 60;
    const uint32_t hrs = mins / 60;
    if (hrs > 0) {
        return String(hrs) + "h " + String(mins % 60) + "m";
    }
    return String(mins) + "m " + String(secs % 60) + "s";
}

String desktopHeapString() {
    const uint32_t freeK = ESP.getFreeHeap() / 1024;
    const uint32_t totalK = ESP.getHeapSize() / 1024;
    return String(freeK) + "K / " + String(totalK) + "K";
}

String desktopFlashString() {
    const uint32_t totalM = ESP.getFlashChipSize() / (1024 * 1024);
    return String(totalM) + "MB " + String(ESP.getFlashChipSpeed() / 1000000) + "MHz";
}

bool renderDesktopScreen(SystemState &state, bool oledOnly, Stream &out) {
    // Show fastfetch on first boot if terminal is empty
    if (gTermLineCount == 0) {
        termAddLine(state.config.deviceName + "@esp32s3");
        termAddLine("----------------");
        termAddLine("OS: NoteWave OS");
        termAddLine("CPU: ESP32-S3 " + String(ESP.getCpuFreqMHz()) + "MHz");
        termAddLine("Heap: " + desktopHeapString());
        termAddLine("Up: " + desktopUptimeString());
        String flags;
        flags += state.sdReady ? "SD " : "";
        flags += state.littleFsReady ? "FS " : "";
        flags += state.rtcReady ? "RTC " : "";
        flags += state.cardKbReady ? "KB " : "";
        flags += state.oledReady ? "OLED " : "";
        flags += state.einkReady ? "EINK" : "";
        termAddLine(flags);
        termAddLine("");
    }

    // --- OLED: terminal view ---
    if (state.oledReady && state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        gOled.clearBuffer();
        gOled.setFont(u8g2_font_4x6_tf);

        // Output lines
        int y = 7;
        for (size_t i = 0; i < kTermVisibleOled; ++i) {
            const size_t lineIdx = gTermScroll + i;
            if (lineIdx < gTermLineCount) {
                String line = gTermLines[lineIdx];
                if (line.length() > 30) line = line.substring(0, 30);
                gOled.drawStr(0, y, line.c_str());
            }
            y += 8;
        }

        // Input prompt at bottom
        gOled.drawLine(0, 55, 127, 55);
        String prompt = String("$ ") + gTermInputBuffer + "_";
        if (prompt.length() > 30) prompt = prompt.substring(prompt.length() - 30);
        gOled.drawStr(0, 63, prompt.c_str());

        gOled.sendBuffer();
        xSemaphoreGive(state.spiMutex);
    }

    if (oledOnly) {
        return state.oledReady;
    }

    if (!ensureEinkInitialized(state, out)) {
        return state.oledReady;
    }

    if (!gTermEinkDirty) {
        noteDisplayActivity();
        return true;
    }

    // --- E-ink: full terminal view ---
    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
        prepareLandscapePaint(paint);
        paint.Clear(kUncolored);
        paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);

        // Title bar
        String titleBar = state.config.deviceName + "@esp32s3 ~ shell";
        paint.DrawStringAt(8, 10, titleBar.c_str(), &Font16, kColored);
        paint.DrawLine(0, 28, kEinkLandscapeWidth - 1, 28, kColored);

        // Terminal output lines
        // Compute e-ink scroll: show latest lines
        size_t einkScroll = 0;
        if (gTermLineCount > kTermVisibleEink) {
            einkScroll = gTermLineCount - kTermVisibleEink;
        }

        int y = 38;
        for (size_t i = 0; i < kTermVisibleEink; ++i) {
            const size_t lineIdx = einkScroll + i;
            if (lineIdx < gTermLineCount) {
                String line = gTermLines[lineIdx];
                if (line.length() > kTermMaxLineLen) line = line.substring(0, kTermMaxLineLen);
                paint.DrawStringAt(8, y, line.c_str(), &Font12, kColored);
            }
            y += 15;
        }

        // Input prompt
        paint.DrawLine(0, 220, kEinkLandscapeWidth - 1, 220, kColored);
        String prompt = String("$ ") + gTermInputBuffer + "_";
        if (prompt.length() > kTermMaxLineLen) prompt = prompt.substring(prompt.length() - kTermMaxLineLen);
        paint.DrawStringAt(8, 232, prompt.c_str(), &Font12, kColored);

        gEink.display(paint.GetImage());
        refreshEinkWithCadence(false);
        gTermEinkDirty = false;
        xSemaphoreGive(state.spiMutex);
    }

    noteDisplayActivity();
    return true;
}

bool renderNotificationsScreen(SystemState &state, bool oledOnly, Stream &out) {
    return renderStatusScreen(
        state,
        "UPOZORNENI",
        "zatim bez notifikaci",
        "full ekran e-ink",
        "sipka doleva = sbalit listu",
        oledOnly,
        out);
}

bool renderActiveApp(SystemState &state, bool oledOnly, Stream &out) {
    AppRouterRenderContext context;
    context.renderDesktopScreen = renderDesktopScreen;
    context.renderNotificationsScreen = renderNotificationsScreen;
    context.renderLauncherScreen = renderLauncherScreen;
    context.renderSettingsScreen = renderSettingsScreen;
    context.renderFileManagerScreen = renderFileManagerScreen;
    context.renderMusicPlayerScreen = renderMusicPlayerScreen;
    context.renderNotesScreen = renderNotesScreen;
    context.renderWebUploadScreen = renderWebUploadScreen;
    context.renderPlaceholderApp = renderPlaceholderApp;
    return appRouterRenderActiveApp(state, oledOnly, out, context);
}

void setDesktopTaskManager(TaskManager *tm) {
    gTermTaskManager = tm;
}

bool handleActiveAppInput(SystemState &state, char key, Stream &out) {
    // Decode CardKB-specific encodings before app-level routing.
    const uint8_t rawCode = static_cast<uint8_t>(key);

    // Desktop terminal gets first crack at input (except left-arrow → launcher).
    if (state.launcher.activeAppId.equalsIgnoreCase("desktop")) {
        // Left arrow exits to launcher
        if (isCardKbLeftArrowCode(rawCode)) {
            state.notifications.viewMode = 0;
            state.launcher.activeAppId = "launcher";
            state.settings.lastMessage = "launcher";
            renderLauncherScreen(state, false, out);
            return true;
        }
        const char desktopNormalizedKey = decodeCardKbKey(key);
        bool refreshEink = false;
        if (handleDesktopTerminalInput(state, desktopNormalizedKey, rawCode, refreshEink, out)) {
            renderDesktopScreen(state, !refreshEink, out);
            return true;
        }
    }

    AppRouterInputContext inputContext;
    inputContext.isLeftArrowCode = isCardKbLeftArrowCode;
    inputContext.isDownArrowCode = isCardKbDownArrowCode;
    inputContext.isRightArrowCode = isCardKbRightArrowCode;
    inputContext.renderDesktopScreen = renderDesktopScreen;
    inputContext.renderLauncherScreen = renderLauncherScreen;
    inputContext.renderSettingsScreen = renderSettingsScreen;
    inputContext.renderNotificationsScreen = renderNotificationsScreen;
    inputContext.stopWebUploadServer = stopWebUploadServer;
    if (appRouterHandleDesktopDirectionalInput(state, rawCode, inputContext, out)) {
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("file-manager") && fileManagerHandleBackInput(state, rawCode, out)) {
        if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
            renderLauncherScreen(state, false, out);
        } else {
            renderFileManagerScreen(state, false, out);
        }
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("file-manager")) {
        const char fileManagerNormalizedKey = decodeCardKbKey(key);
        if (handleFileManagerAppInput(state, key, fileManagerNormalizedKey, rawCode, out)) {
            renderFileManagerScreen(state, false, out);
            return true;
        }
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("music-player") || state.launcher.activeAppId.equalsIgnoreCase("sdapp:music-player")) {
        const char musicNormalizedKey = decodeCardKbKey(key);
        if (handleMusicPlayerAppInput(state, musicNormalizedKey, rawCode, out)) {
            renderMusicPlayerScreen(state, false, out);
            return true;
        }
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("notes")) {
        const char notesNormalizedKey = decodeCardKbKey(key);
        bool refreshNotesEink = false;
        if (handleNotesAppInput(state, notesNormalizedKey, rawCode, refreshNotesEink, out)) {
            renderNotesScreen(state, !refreshNotesEink, out);
            return true;
        }
        // Left arrow in picker mode falls through to global back handler (→ launcher)
    }

    if (appRouterHandleBackInput(
            state,
            rawCode,
            kSettingsViewHome,
            kSettingsViewWifiList,
            kSettingsViewWifiPassword,
            kSettingsViewWifiSelectList,
            kSettingsViewBluetoothList,
            kSettingsViewBluetoothSelectList,
            kSettingsViewSdList,
            inputContext,
            out)) {
        return true;
    }

    const char normalizedKey = decodeCardKbKey(key);

    if (state.launcher.activeAppId.equalsIgnoreCase("launcher")) {
        refreshLauncherSdApps(state, out);
        const size_t totalCount = launcherTotalItemCount();
        if (totalCount > 0) {
            const size_t pageCount = (totalCount + kLauncherPageSize - 1) / kLauncherPageSize;
            const size_t pageIndex = min(static_cast<size_t>(state.launcher.selectedIndex) / kLauncherPageSize, pageCount - 1);
            const size_t pageStart = pageIndex * kLauncherPageSize;

            if (isCardKbLeftArrowCode(rawCode)) {
                state.launcher.activeAppId = "desktop";
                state.settings.lastMessage = "back to desktop";
                renderDesktopScreen(state, false, out);
                return true;
            }

            if (isCardKbRightArrowCode(rawCode) && pageCount > 1) {
                const size_t nextPage = (pageIndex + 1) % pageCount;
                state.launcher.selectedIndex = static_cast<uint8_t>(nextPage * kLauncherPageSize);
                state.settings.lastMessage = String("launcher page ") + String(static_cast<unsigned>(nextPage + 1));
                renderLauncherScreen(state, false, out);
                return true;
            }

            if (isCardKbUpArrowCode(rawCode)) {
                if (pageIndex > 0) {
                    const size_t prevPage = pageIndex - 1;
                    state.launcher.selectedIndex = static_cast<uint8_t>(prevPage * kLauncherPageSize);
                    state.settings.lastMessage = String("launcher page ") + String(static_cast<unsigned>(prevPage + 1));
                    renderLauncherScreen(state, false, out);
                    return true;
                }

                state.launcher.activeAppId = "desktop";
                state.settings.lastMessage = "back to desktop";
                renderDesktopScreen(state, false, out);
                return true;
            }

            if (isCardKbDownArrowCode(rawCode) && pageCount > 1) {
                const size_t nextPage = (pageIndex + 1) % pageCount;
                state.launcher.selectedIndex = static_cast<uint8_t>(nextPage * kLauncherPageSize);
                state.settings.lastMessage = String("launcher page ") + String(static_cast<unsigned>(nextPage + 1));
                renderLauncherScreen(state, false, out);
                return true;
            }

            if (normalizedKey >= '1' && normalizedKey <= '6') {
                const size_t localIndex = static_cast<size_t>(normalizedKey - '1');
                const size_t absoluteIndex = pageStart + localIndex;
                if (absoluteIndex < totalCount) {
                    state.launcher.selectedIndex = static_cast<uint8_t>(absoluteIndex);
                    const String targetId = launcherItemIdAt(absoluteIndex);
                    const String targetTitle = launcherItemTitleAt(absoluteIndex);
                    state.launcher.activeAppId = targetId;
                    state.settings.lastMessage = String("opened ") + targetTitle;
                    if (targetId.equalsIgnoreCase("file-manager")) {
                        fileManagerResetSession(state);
                    } else if (targetId.equalsIgnoreCase("music-player") || targetId.equalsIgnoreCase("sdapp:music-player")) {
                        resetMusicPlayerSession(state);
                    } else if (targetId.equalsIgnoreCase("notes")) {
                        resetNotesSession(state);
                    }
                    out.print("Launcher: opening ");
                    out.println(targetId);
                    renderActiveApp(state, false, out);
                    return true;
                }
            }
        }

        if (normalizedKey >= 32 && normalizedKey <= 126) {
            gLauncherKeyMessage = String("key: ") + normalizedKey + " (1..6, up/down)";
        } else {
            char rawHex[24];
            snprintf(rawHex, sizeof(rawHex), "key:0x%02X (1..6/up/down)", static_cast<uint8_t>(key));
            gLauncherKeyMessage = rawHex;
        }
        // Only update OLED for feedback on unused keys.
        renderLauncherScreen(state, true, out);
        return true;
    }

    if (state.launcher.activeAppId.equalsIgnoreCase("settings")) {
        SettingsInputContext settingsInputContext;
        settingsInputContext.wifiListVisibleCount = kWifiListVisibleCount;
        settingsInputContext.wifiCount = &gWifiCount;
        settingsInputContext.wifiListScrollOffset = &gWifiListScrollOffset;
        settingsInputContext.wifiScanInProgress = &gWifiScanInProgress;
        settingsInputContext.wifiSsidList = gWifiSsidList;
        settingsInputContext.bluetoothDeviceCount = &gBluetoothDeviceCount;
        settingsInputContext.bluetoothListVisibleCount = kBluetoothListVisibleCount;
        settingsInputContext.bluetoothListScrollOffset = &gBluetoothListScrollOffset;
        settingsInputContext.bluetoothScanInProgress = &gBluetoothScanInProgress;
        settingsInputContext.bluetoothDeviceList = gBluetoothDeviceList;
        settingsInputContext.bluetoothDeviceAddressList = gBluetoothDeviceAddressList;
        settingsInputContext.czechComposeDeadKey = &gCzechComposeDeadKey;
        settingsInputContext.isUpArrowCode = isCardKbUpArrowCode;
        settingsInputContext.isDownArrowCode = isCardKbDownArrowCode;
        settingsInputContext.toggleWifiEnabled = toggleWifiEnabled;
        settingsInputContext.scanWifiNetworks = scanWifiNetworks;
        settingsInputContext.connectSelectedWifi = connectSelectedWifi;
        settingsInputContext.forgetSavedWifi = forgetSavedWifi;
        settingsInputContext.toggleBluetoothEnabled = toggleBluetoothEnabled;
        settingsInputContext.disconnectBluetooth = settingsServiceDisconnectBluetooth;
        settingsInputContext.selectBluetoothDevice = selectBluetoothDevice;
        settingsInputContext.forgetSavedBluetoothDevice = forgetSavedBluetoothDevice;
        settingsInputContext.scanBluetoothDevices = scanBluetoothDevices;
        settingsInputContext.tryApplyPostfixCzechCompose = tryApplyPostfixCzechCompose;
        settingsInputContext.decodeCzechComposeKey = decodeCzechComposeKey;
        settingsInputContext.applySettingsSelection = applySettingsSelection;
        settingsInputContext.renderSettingsScreen = renderSettingsScreen;
        settingsInputContext.renderLauncherScreen = renderLauncherScreen;

        return handleSettingsAppInput(
            state,
            key,
            normalizedKey,
            rawCode,
            kSettingsViewWifiList,
            kSettingsViewWifiPassword,
            kSettingsViewWifiSelectList,
            kSettingsViewBluetoothList,
            kSettingsViewBluetoothSelectList,
            kSettingsViewSdList,
            kSettingsViewHome,
            settingsInputContext,
            out);
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

    gEink.display(paint.GetImage());
    const bool partialThresholdReached = gEinkPartialRefreshCounter >= (kEinkFullRefreshInterval - 1);
    const bool doFull = forceFullRefresh || partialThresholdReached;
    refreshEinkWithCadence(forceFullRefresh);

    markDisplayActivity();
    out.println(doFull ? "E-ink: full refresh" : "E-ink: fast refresh");
    return true;
}

namespace {
void refreshEinkWithCadence(bool forceFull) {
    const bool partialThresholdReached = gEinkPartialRefreshCounter >= (kEinkFullRefreshInterval - 1);
    const bool doFull = forceFull || partialThresholdReached;

    vTaskDelay(pdMS_TO_TICKS(1));
    if (doFull) {
        gEink.lut_GC();
        gEinkPartialRefreshCounter = 0;
    } else {
        gEink.lut_DU();
        ++gEinkPartialRefreshCounter;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
    gEink.refresh();
    vTaskDelay(pdMS_TO_TICKS(5));
    ++gEinkUpdateCounter;

    if (kEnableEinkCadenceLogging) {
        Serial.print("E-ink cadence: ");
        Serial.print(doFull ? "FULL" : "DU");
        Serial.print(" update=");
        Serial.print(gEinkUpdateCounter);
        Serial.print(" duCount=");
        Serial.print(gEinkPartialRefreshCounter);
        Serial.print("/");
        Serial.println(kEinkFullRefreshInterval - 1);
    }
}
}  // namespace

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
