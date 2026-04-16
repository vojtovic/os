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
constexpr size_t kNotesMaxChars = 1200;
String gNotesBuffer;
String gNotesDraftWord;
constexpr uint8_t kNotesViewRead = 0;
constexpr uint8_t kNotesViewWrite = 1;
constexpr size_t kNotesMaxTags = 16;
String gNotesTags[kNotesMaxTags];
size_t gNotesTagCount = 0;
constexpr size_t kNotesMaxLinks = 16;
String gNotesLinks[kNotesMaxLinks];
size_t gNotesLinkCount = 0;
constexpr int kNotesTagFilterAll = -1;
int gNotesActiveTagIndex = kNotesTagFilterAll;
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
bool handleNotesAppInput(SystemState &state, char normalizedKey, uint8_t rawCode, bool &refreshEink, Stream &out);
bool renderNotesScreen(SystemState &state, bool oledOnly, Stream &out);
String notesTailPreview(size_t maxChars);
String notesStyledPreviewForActiveFilter(size_t maxChars);
String notesLinksSummary(size_t maxChars);
String normalizedNotesPath(SystemState &state);
String normalizeMarkdownText(const String &raw);
bool isOrderedListPrefix(const String &line);

bool isNotesTagChar(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-';
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
    clearNotesTags();
    clearNotesLinks();

    if (!state.littleFsReady) {
        state.notes.statusMessage = "LittleFS off";
        return false;
    }

    String path = normalizedNotesPath(state);
    const String legacyPath = "/notes/quicknote.txt";

    if (!LittleFS.exists(path)) {
        if (path == "/notes/quicknote.md" && LittleFS.exists(legacyPath)) {
            path = legacyPath;
            state.notes.statusMessage = "legacy txt loaded";
        } else {
            state.notes.statusMessage = "new note";
            state.notes.dirty = false;
            return true;
        }
    }

    File file = LittleFS.open(path, "r");
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
    refreshNotesMetadataFromBuffer();
    return true;
}

bool saveNotes(SystemState &state, Stream &out) {
    if (!state.littleFsReady) {
        state.notes.statusMessage = "LittleFS off";
        return false;
    }

    String path = normalizedNotesPath(state);

    const int slash = path.lastIndexOf('/');
    if (slash > 0) {
        const String dir = path.substring(0, slash);
        if (!LittleFS.exists(dir)) {
            LittleFS.mkdir(dir);
        }
    }

    File file = LittleFS.open(path, "w");
    if (!file) {
        state.notes.statusMessage = "save failed";
        return false;
    }

    file.print(normalizeMarkdownText(gNotesBuffer));
    file.close();
    state.notes.dirty = false;
    state.notes.statusMessage = "saved";
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
constexpr size_t kFileManagerMaxEntries = 24;
constexpr size_t kFileManagerPageSize = 6;

String gFileManagerCachedPath;
bool gFileManagerCacheDirty = true;
String gFileManagerEntryNames[kFileManagerMaxEntries];
String gFileManagerEntryPaths[kFileManagerMaxEntries];
bool gFileManagerEntryIsDir[kFileManagerMaxEntries];
uint32_t gFileManagerEntrySizes[kFileManagerMaxEntries];
size_t gFileManagerEntryCount = 0;

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

bool fileManagerPathExists(const String &path) {
    return SD.exists(fileManagerNormalizePath(path));
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

String fileManagerUniqueChildPath(const String &directoryPath, const String &baseName) {
    String candidate = fileManagerJoinPath(directoryPath, baseName);
    if (!fileManagerPathExists(candidate)) {
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
        if (!fileManagerPathExists(candidate)) {
            return candidate;
        }
    }

    return String();
}

bool fileManagerDeleteRecursive(const String &path, Stream &out);
bool fileManagerCopyRecursive(const String &sourcePath, const String &destinationPath, Stream &out);

size_t fileManagerRefreshListing(SystemState &state, Stream &out) {
    if (!state.sdReady) {
        gFileManagerEntryCount = 0;
        fileManagerSetStatus(state, "SD not ready");
        return 0;
    }

    const String currentPath = fileManagerNormalizePath(state.fileManager.currentPath);
    if (!gFileManagerCacheDirty && gFileManagerCachedPath == currentPath) {
        return gFileManagerEntryCount;
    }

    gFileManagerCachedPath = currentPath;
    gFileManagerEntryCount = 0;

    File dir = SD.open(currentPath, FILE_READ);
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
    if (fileManagerPathExists(targetPath)) {
        fileManagerSetStatus(state, "folder exists");
        return false;
    }

    if (!SD.mkdir(targetPath)) {
        fileManagerSetStatus(state, "mkdir failed");
        out.print("File manager: mkdir failed ");
        out.println(targetPath);
        return false;
    }

    invalidateFileManagerCache();
    fileManagerSetStatus(state, String("created ") + cleanName);
    return true;
}

bool fileManagerDeleteRecursive(const String &path, Stream &out) {
    const String normalized = fileManagerNormalizePath(path);
    if (normalized == "/") {
        out.println("File manager: refusing to delete root");
        return false;
    }

    File entry = SD.open(normalized, FILE_READ);
    if (!entry) {
        out.print("File manager: delete open failed ");
        out.println(normalized);
        return false;
    }

    if (!entry.isDirectory()) {
        entry.close();
        if (!SD.remove(normalized)) {
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
        if (!fileManagerDeleteRecursive(childPath, out)) {
            entry.close();
            return false;
        }
        child = entry.openNextFile();
    }

    entry.close();
    if (!SD.rmdir(normalized)) {
        out.print("File manager: rmdir failed ");
        out.println(normalized);
        return false;
    }

    return true;
}

bool fileManagerCopyRecursive(const String &sourcePath, const String &destinationPath, Stream &out) {
    const String normalizedSource = fileManagerNormalizePath(sourcePath);
    const String normalizedDestination = fileManagerNormalizePath(destinationPath);

    if (fileManagerIsInsideSourcePath(normalizedSource, normalizedDestination)) {
        out.println("File manager: invalid copy target");
        return false;
    }

    File source = SD.open(normalizedSource, FILE_READ);
    if (!source) {
        out.print("File manager: copy open failed ");
        out.println(normalizedSource);
        return false;
    }

    if (!source.isDirectory()) {
        File destination = SD.open(normalizedDestination, FILE_WRITE);
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

    if (!SD.mkdir(normalizedDestination)) {
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
        if (!fileManagerCopyRecursive(childSource, childDestination, out)) {
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
        : fileManagerUniqueChildPath(targetDir, sourceName);

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

        if (fileManagerPathExists(targetPath)) {
            fileManagerSetStatus(state, "target exists");
            return false;
        }
    }

    if (!fileManagerCopyRecursive(sourcePath, targetPath, out)) {
        fileManagerSetStatus(state, "paste failed");
        return false;
    }

    if (state.fileManager.clipboardMove) {
        if (!fileManagerDeleteRecursive(sourcePath, out)) {
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
    gOled.drawStr(0, 7, "FILE MANAGER");
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

    if (state.fileManager.viewMode == kFileManagerViewDirectoryMenu) {
        gOled.drawStr(0, 24, "directory actions");
        gOled.drawStr(0, 35, state.fileManager.currentPath.c_str());
        gOled.drawStr(0, 46, state.fileManager.menuIndex == 0 ? "> 1 new folder" : "  1 new folder");
        gOled.drawStr(0, 56, state.fileManager.menuIndex == 1 ? "> 2 refresh" : "  2 refresh");
        gOled.drawStr(80, 56, "<- cancel");
        gOled.sendBuffer();
        return;
    }

    if (state.fileManager.viewMode == kFileManagerViewItemMenu) {
        const String targetLabel = fileManagerItemDisplayName(state.fileManager.selectedIndex);
        gOled.drawStr(0, 24, gFileManagerEntryIsDir[state.fileManager.selectedIndex] ? "folder menu" : "file menu");
        gOled.setCursor(0, 35);
        gOled.print(targetLabel);
        const bool isDir = gFileManagerEntryIsDir[state.fileManager.selectedIndex];
        size_t optionRow = 0;
        if (isDir) {
            gOled.drawStr(0, 46, state.fileManager.menuIndex == 0 ? "> 1 open" : "  1 open");
            gOled.drawStr(0, 56, state.fileManager.menuIndex == 1 ? "> 2 delete" : "  2 delete");
            optionRow = 2;
            gOled.drawStr(80, 46, state.fileManager.menuIndex == 2 ? "> 3 copy" : "  3 copy");
            gOled.drawStr(80, 56, state.fileManager.menuIndex == 3 ? "> 4 move" : "  4 move");
        } else {
            gOled.drawStr(0, 46, state.fileManager.menuIndex == 0 ? "> 1 delete" : "  1 delete");
            gOled.drawStr(0, 56, state.fileManager.menuIndex == 1 ? "> 2 copy" : "  2 copy");
            optionRow = 2;
            gOled.drawStr(80, 46, state.fileManager.menuIndex == 2 ? "> 3 move" : "  3 move");
        }
        (void)optionRow;
        gOled.drawStr(0, 64, "<- cancel");
        gOled.sendBuffer();
        return;
    }

    const size_t totalCount = gFileManagerEntryCount;
    const size_t startIndex = min(state.fileManager.scrollOffset, totalCount > kFileManagerPageSize ? totalCount - kFileManagerPageSize : size_t(0));
    const size_t endIndex = min(startIndex + kFileManagerPageSize, totalCount);
    const size_t pageCount = totalCount == 0 ? 1 : ((totalCount + kFileManagerPageSize - 1) / kFileManagerPageSize);
    const size_t pageIndex = totalCount == 0 ? 0 : (startIndex / kFileManagerPageSize);

    gOled.drawStr(0, 22, "browse");
    gOled.setCursor(48, 22);
    gOled.print(pageIndex + 1);
    gOled.print("/");
    gOled.print(pageCount);

    int y = 32;
    for (size_t index = startIndex; index < endIndex; ++index) {
        String line = (index == state.fileManager.selectedIndex) ? ">" : " ";
        line += String(static_cast<unsigned>((index - startIndex) + 1));
        line += " ";
        line += gFileManagerEntryIsDir[index] ? "[D] " : "[F] ";
        line += fileManagerItemDisplayName(index);
        gOled.drawStr(0, y, line.c_str());
        y += 8;
    }

    while (y <= 58) {
        gOled.drawStr(0, y, "");
        y += 8;
    }

    gOled.setCursor(0, 64);
    gOled.print(state.fileManager.clipboardActive ? "right=paste " : "right=new ");
    gOled.print(state.fileManager.statusMessage);
    gOled.sendBuffer();
}

void drawFileManagerEink(const SystemState &state) {
    Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
    prepareLandscapePaint(paint);
    paint.Clear(kUncolored);
    paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
    paint.DrawStringAt(8, 10, "FILE MANAGER", &Font16, kColored);
    paint.DrawStringAtUtf8(8, 26, state.fileManager.currentPath.c_str(), &Font12, kColored);

    if (state.fileManager.viewMode == kFileManagerViewCreateFolder) {
        paint.DrawStringAt(8, 48, "new folder", &Font12, kColored);
        paint.DrawStringAtUtf8(8, 66, state.fileManager.pendingFolderName.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 84, "enter=save", &Font12, kColored);
        paint.DrawStringAt(8, 100, "left=cancel", &Font12, kColored);
    } else if (state.fileManager.viewMode == kFileManagerViewDirectoryMenu) {
        paint.DrawStringAt(8, 48, "directory actions", &Font12, kColored);
        paint.DrawStringAtUtf8(8, 66, state.fileManager.currentPath.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 84, state.fileManager.menuIndex == 0 ? "> 1 new folder" : "  1 new folder", &Font12, kColored);
        paint.DrawStringAt(8, 100, state.fileManager.menuIndex == 1 ? "> 2 refresh" : "  2 refresh", &Font12, kColored);
        paint.DrawStringAt(8, 118, "left=cancel", &Font12, kColored);
    } else if (state.fileManager.viewMode == kFileManagerViewItemMenu) {
        const bool isDir = gFileManagerEntryIsDir[state.fileManager.selectedIndex];
        paint.DrawStringAt(8, 48, isDir ? "folder menu" : "file menu", &Font12, kColored);
        paint.DrawStringAtUtf8(8, 66, fileManagerItemDisplayName(state.fileManager.selectedIndex).c_str(), &Font12, kColored);
        if (isDir) {
            paint.DrawStringAt(8, 84, state.fileManager.menuIndex == 0 ? "> 1 open" : "  1 open", &Font12, kColored);
            paint.DrawStringAt(8, 100, state.fileManager.menuIndex == 1 ? "> 2 delete" : "  2 delete", &Font12, kColored);
            paint.DrawStringAt(140, 84, state.fileManager.menuIndex == 2 ? "> 3 copy" : "  3 copy", &Font12, kColored);
            paint.DrawStringAt(140, 100, state.fileManager.menuIndex == 3 ? "> 4 move" : "  4 move", &Font12, kColored);
        } else {
            paint.DrawStringAt(8, 84, state.fileManager.menuIndex == 0 ? "> 1 delete" : "  1 delete", &Font12, kColored);
            paint.DrawStringAt(8, 100, state.fileManager.menuIndex == 1 ? "> 2 copy" : "  2 copy", &Font12, kColored);
            paint.DrawStringAt(140, 84, state.fileManager.menuIndex == 2 ? "> 3 move" : "  3 move", &Font12, kColored);
        }
        paint.DrawStringAt(8, 118, "left=cancel", &Font12, kColored);
    } else {
        const size_t totalCount = gFileManagerEntryCount;
        const size_t startIndex = min(state.fileManager.scrollOffset, totalCount > kFileManagerPageSize ? totalCount - kFileManagerPageSize : size_t(0));
        const size_t endIndex = min(startIndex + kFileManagerPageSize, totalCount);
        const size_t pageCount = totalCount == 0 ? 1 : ((totalCount + kFileManagerPageSize - 1) / kFileManagerPageSize);
        const size_t pageIndex = totalCount == 0 ? 0 : (startIndex / kFileManagerPageSize);
        String pageLine = String("page ") + String(static_cast<unsigned>(pageIndex + 1)) + "/" + String(static_cast<unsigned>(pageCount));
        paint.DrawStringAt(200, 26, pageLine.c_str(), &Font12, kColored);
        paint.DrawLine(0, 38, kEinkLandscapeWidth - 1, 38, kColored);

        int y = 48;
        for (size_t index = startIndex; index < endIndex; ++index) {
            String line = String(static_cast<unsigned>((index - startIndex) + 1));
            line += ".";
            line += gFileManagerEntryIsDir[index] ? "[D] " : "[F] ";
            line += fileManagerItemDisplayName(index);
            if (index == state.fileManager.selectedIndex) {
                line = String("> ") + line;
            } else {
                line = String("  ") + line;
            }
            paint.DrawStringAtUtf8(8, y, line.c_str(), &Font12, kColored);
            y += 16;
        }

        if (totalCount == 0) {
            paint.DrawStringAt(8, 58, "empty directory", &Font12, kColored);
        }

        paint.DrawStringAt(8, 218, state.fileManager.clipboardActive ? "1..6 open/menu, up/down page, right=paste" : "1..6 open/menu, up/down page, right=new", &Font12, kColored);
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
                if (fileManagerDeleteRecursive(selectedPath, out)) {
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
            if (fileManagerDeleteRecursive(selectedPath, out)) {
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

bool fileManagerHandleBrowseInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    const size_t totalCount = gFileManagerEntryCount;
    const size_t pageCount = totalCount == 0 ? 1 : ((totalCount + kFileManagerPageSize - 1) / kFileManagerPageSize);

    if (isCardKbUpArrowCode(rawCode)) {
        if (state.fileManager.scrollOffset >= kFileManagerPageSize) {
            state.fileManager.scrollOffset -= kFileManagerPageSize;
        } else {
            state.fileManager.scrollOffset = 0;
        }
        state.fileManager.selectedIndex = static_cast<uint8_t>(state.fileManager.scrollOffset);
        fileManagerSetStatus(state, "page up");
        return true;
    }

    if (isCardKbDownArrowCode(rawCode) && pageCount > 1) {
        const size_t maxOffset = totalCount > kFileManagerPageSize ? totalCount - kFileManagerPageSize : 0;
        state.fileManager.scrollOffset = min(state.fileManager.scrollOffset + kFileManagerPageSize, maxOffset);
        state.fileManager.selectedIndex = static_cast<uint8_t>(state.fileManager.scrollOffset);
        fileManagerSetStatus(state, "page down");
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

    if (normalizedKey >= '1' && normalizedKey <= '6') {
        const size_t localIndex = static_cast<size_t>(normalizedKey - '1');
        const size_t absoluteIndex = state.fileManager.scrollOffset + localIndex;
        if (absoluteIndex < totalCount) {
            state.fileManager.selectedIndex = static_cast<uint8_t>(absoluteIndex);
            state.fileManager.viewMode = kFileManagerViewItemMenu;
            state.fileManager.menuIndex = 0;
            state.fileManager.menuTargetPath = fileManagerItemPath(absoluteIndex);
            state.fileManager.menuTargetIsDir = gFileManagerEntryIsDir[absoluteIndex];
            fileManagerSetStatus(state, String("selected ") + fileManagerItemDisplayName(absoluteIndex));
            return true;
        }
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        fileManagerSetStatus(state, "pick 1..6 or arrow keys");
        return true;
    }

    if (normalizedKey >= 32 && normalizedKey <= 126) {
        fileManagerSetStatus(state, String("key: ") + normalizedKey);
        return true;
    }

    return false;
}

bool fileManagerHandleItemMenuInput(SystemState &state, char normalizedKey, uint8_t rawCode, Stream &out) {
    const bool isDir = state.fileManager.menuTargetIsDir;
    const uint8_t optionCount = isDir ? 4 : 3;

    if (isCardKbUpArrowCode(rawCode)) {
        if (state.fileManager.menuIndex == 0) {
            state.fileManager.menuIndex = optionCount - 1;
        } else {
            --state.fileManager.menuIndex;
        }
        return true;
    }

    if (isCardKbDownArrowCode(rawCode)) {
        state.fileManager.menuIndex = static_cast<uint8_t>((state.fileManager.menuIndex + 1) % optionCount);
        return true;
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        if (state.fileManager.menuIndex == 0 && isDir) {
            fileManagerEnterDirectory(state, state.fileManager.menuTargetPath, out);
            return true;
        }

        if (state.fileManager.menuIndex == (isDir ? 1 : 0)) {
            if (fileManagerDeleteRecursive(state.fileManager.menuTargetPath, out)) {
                invalidateFileManagerCache();
                fileManagerRefreshListing(state, out);
                fileManagerSetStatus(state, "deleted item");
                state.fileManager.viewMode = kFileManagerViewBrowse;
                return true;
            }
            fileManagerSetStatus(state, "delete failed");
            return true;
        }

        if (state.fileManager.menuIndex == (isDir ? 2 : 1)) {
            state.fileManager.clipboardPath = state.fileManager.menuTargetPath;
            state.fileManager.clipboardMove = false;
            state.fileManager.clipboardActive = true;
            state.fileManager.viewMode = kFileManagerViewBrowse;
            fileManagerSetStatus(state, "copy ready");
            return true;
        }

        if (state.fileManager.menuIndex == (isDir ? 3 : 2)) {
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
        fileManagerSetStatus(state, "menu canceled");
        return true;
    }

    if (normalizedKey >= '1' && normalizedKey <= '4') {
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

void fileManagerResetSession(SystemState &state) {
    state.fileManager.currentPath = "/";
    state.fileManager.statusMessage = "ready";
    state.fileManager.clipboardPath = "";
    state.fileManager.menuTargetPath = "";
    state.fileManager.pendingFolderName = "";
    state.fileManager.clipboardActive = false;
    state.fileManager.clipboardMove = false;
    state.fileManager.menuTargetIsDir = false;
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
    state.notes.statusMessage = "ready";
    state.notes.viewMode = kNotesViewWrite;
    gNotesDraftWord = "";
    clearNotesTags();
    clearNotesLinks();
}

bool handleNotesAppInput(SystemState &state, char normalizedKey, uint8_t rawCode, bool &refreshEink, Stream &out) {
    refreshEink = false;
    ensureNotesLoaded(state, out);
    const bool writeMode = state.notes.viewMode == kNotesViewWrite;

    if (normalizedKey == '\t' || static_cast<uint8_t>(normalizedKey) == 0x1B) {
        if (state.notes.viewMode == kNotesViewRead) {
            state.notes.viewMode = kNotesViewWrite;
            state.notes.statusMessage = "mode: write";
        } else {
            state.notes.viewMode = kNotesViewRead;
            state.notes.statusMessage = "mode: read";
        }
        refreshEink = true;
        return true;
    }

    // Keep arrow keys out of text input path.
    if (isCardKbLeftArrowCode(rawCode)) {
        // Let global back handler process this key.
        return false;
    }

    if (isCardKbRightArrowCode(rawCode)) {
        commitNotesDraftWord(state, false);
        saveNotes(state, out);
        refreshNotesMetadataFromBuffer();
        refreshEink = true;
        return true;
    }

    if (isCardKbUpArrowCode(rawCode) || isCardKbDownArrowCode(rawCode)) {
        if (gNotesTagCount == 0) {
            gNotesActiveTagIndex = kNotesTagFilterAll;
            state.notes.statusMessage = "no tags";
            return true;
        }

        const bool isUp = isCardKbUpArrowCode(rawCode);
        if (isUp) {
            if (gNotesActiveTagIndex == kNotesTagFilterAll) {
                gNotesActiveTagIndex = static_cast<int>(gNotesTagCount) - 1;
            } else {
                --gNotesActiveTagIndex;
                if (gNotesActiveTagIndex < 0) {
                    gNotesActiveTagIndex = kNotesTagFilterAll;
                }
            }
        } else {
            if (gNotesActiveTagIndex == kNotesTagFilterAll) {
                gNotesActiveTagIndex = 0;
            } else {
                ++gNotesActiveTagIndex;
                if (gNotesActiveTagIndex >= static_cast<int>(gNotesTagCount)) {
                    gNotesActiveTagIndex = kNotesTagFilterAll;
                }
            }
        }

        state.notes.statusMessage = notesFilterLabel();
        refreshEink = true;
        return true;
    }

    if (!writeMode) {
        if (normalizedKey == 8 || normalizedKey == ' ' || normalizedKey == '\r' || normalizedKey == '\n' || (normalizedKey >= 32 && normalizedKey <= 126)) {
            state.notes.statusMessage = "read only";
            return true;
        }
    }

    if (normalizedKey == 8) {
        if (!gNotesDraftWord.isEmpty()) {
            gNotesDraftWord.remove(gNotesDraftWord.length() - 1);
            state.notes.statusMessage = gNotesDraftWord.startsWith("#") ? "typing tag" : "typing";
        } else if (!gNotesBuffer.isEmpty()) {
            gNotesBuffer.remove(gNotesBuffer.length() - 1);
            state.notes.dirty = true;
            state.notes.statusMessage = "edited";
            refreshNotesMetadataFromBuffer();
            refreshEink = true;
        }
        return true;
    }

    if (normalizedKey == ' ') {
        if (commitNotesDraftWord(state, true)) {
            state.notes.statusMessage = "word committed";
            refreshEink = true;
            return true;
        }

        state.notes.statusMessage = "typing";
        return true;
    }

    if (normalizedKey == '\r' || normalizedKey == '\n') {
        bool committed = commitNotesDraftWord(state, false);
        if (gNotesBuffer.length() < kNotesMaxChars) {
            gNotesBuffer += '\n';
            state.notes.dirty = true;
            state.notes.statusMessage = committed ? "line committed" : "new line";
            refreshEink = true;
        } else {
            state.notes.statusMessage = "note full";
        }
        return true;
    }

    if (normalizedKey >= 32 && normalizedKey <= 126 && normalizedKey != ' ') {
        if (notesTotalChars() < kNotesMaxChars) {
            gNotesDraftWord += normalizedKey;
            state.notes.statusMessage = gNotesDraftWord.startsWith("#") ? "typing tag" : "typing";
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
    ensureNotesLoaded(state, out);
    const bool writeMode = state.notes.viewMode == kNotesViewWrite;
    const String modeLine = writeMode ? String("mode: write") : String("mode: read");

    if (state.oledReady && state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        String charsLine = String("chars: ") + String(static_cast<unsigned>(notesTotalChars())) + (state.notes.dirty ? " *" : "");
        String tagsLine = notesTagsSummary(24);
        String linksLine = notesLinksSummary(24);
        String filterLine = notesFilterLabel();

        gOled.clearBuffer();
        gOled.setFont(u8g2_font_6x10_tf);
        gOled.drawStr(0, 10, "NOTES");
        gOled.drawStr(0, 20, modeLine.c_str());
        gOled.drawStr(0, 30, charsLine.c_str());
        gOled.drawStr(0, 40, state.notes.statusMessage.c_str());
        gOled.drawStr(0, 50, filterLine.c_str());
        gOled.drawStr(0, 60, writeMode ? tagsLine.c_str() : linksLine.c_str());
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
        paint.DrawStringAt(8, 10, "NOTES", &Font16, kColored);

        String metaLine = modeLine + String(" chars:") + String(static_cast<unsigned>(gNotesBuffer.length())) +
                          (state.notes.dirty ? " *" : "") +
                          String(" status:") + state.notes.statusMessage;
        paint.DrawStringAt(8, 28, metaLine.c_str(), &Font12, kColored);
        paint.DrawLine(0, 40, kEinkLandscapeWidth - 1, 40, kColored);

        // Tags are rendered in their own boxed area so they stand out from note text.
        paint.DrawRectangle(8, 48, kEinkLandscapeWidth - 8, 78, kColored);
        paint.DrawStringAt(12, 62, "TAGS", &Font12, kColored);
        String tagsLine = notesTagsSummary(40);
        paint.DrawStringAtUtf8(56, 62, tagsLine.c_str(), &Font12, kColored);
        paint.DrawStringAtUtf8(8, 82, notesFilterLabel().c_str(), &Font12, kColored);

        String linksLine = notesLinksSummary(46);
        String previewLine = writeMode ? notesPreviewForActiveFilter(46) : notesStyledPreviewForActiveFilter(46);
        previewLine.replace("\n", " ");
        paint.DrawStringAtUtf8(8, 98, linksLine.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 112, writeMode ? "WRITE" : "READ", &Font12, kColored);
        paint.DrawStringAtUtf8(66, 112, previewLine.c_str(), &Font12, kColored);
        paint.DrawStringAt(8, 126, "up/down=tag  tab/esc=read/write", &Font12, kColored);

        gEink.display(paint.GetImage());
        refreshEinkWithCadence(false);
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

bool renderDesktopScreen(SystemState &state, bool oledOnly, Stream &out) {
    if (state.oledReady && state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        const bool notifStripVisible = state.notifications.viewMode == 1;
        const int panelWidth = 42;  // ~1/3 of 128px OLED width.
        const int contentWidth = 128;
        const int contentStartX = 0;
        const char *artLines[] = {
R"( ▄▀▀▄ ▀▄  ▄▀▀▀▀▄   ▄▀▀▀█▀▀▄  ▄▀▀█▄▄▄▄  ▄▀▀▄    ▄▀▀▄  ▄▀▀█▄   ▄▀▀▄    ▄▀▀▄  ▄▀▀█▄▄▄▄ )",
R"(█  █ █ █ █      █ █    █  ▐ ▐  ▄▀   ▐ █   █    ▐  █ ▐ ▄▀ ▀▄ █   █    ▐  █ ▐  ▄▀   ▐ )",
R"(▐  █  ▀█ █      █ ▐   █       █▄▄▄▄▄  ▐  █        █   █▄▄▄█ ▐  █        █   █▄▄▄▄▄  )",
R"(  █   █  ▀▄    ▄▀    █        █    ▌    █   ▄    █   ▄▀   █   █   ▄    █    █    ▌  )",
R"(▄▀   █     ▀▀▀▀    ▄▀        ▄▀▄▄▄▄      ▀▄▀ ▀▄ ▄▀  █   ▄▀     ▀▄▀ ▀▄ ▄▀   ▄▀▄▄▄▄   )",
R"(█    ▐            █          █    ▐            ▀    ▐   ▐            ▀     █    ▐   )",
R"(▐                 ▐          ▐                                             ▐        )"
        };
        const size_t artLineCount = sizeof(artLines) / sizeof(artLines[0]);
        const int lineHeight = 8;
        const int artHeight = static_cast<int>(artLineCount) * lineHeight;
        const int artStartY = max(10, (64 - artHeight) / 2 + 6);

        gOled.clearBuffer();
        gOled.setFont(u8g2_font_4x6_tf);
        for (size_t i = 0; i < artLineCount; ++i) {
            const int lineWidth = static_cast<int>(String(artLines[i]).length()) * 4;
            const int x = contentStartX + max(0, (contentWidth - lineWidth) / 2);
            const int y = artStartY + static_cast<int>(i) * lineHeight;
            gOled.drawStr(x, y, artLines[i]);
        }

        if (notifStripVisible) {
            const int panelX = 128 - panelWidth;
            gOled.setDrawColor(0);
            gOled.drawBox(panelX, 0, panelWidth, 64);
            gOled.setDrawColor(1);
            gOled.drawFrame(panelX, 0, panelWidth, 64);
            gOled.drawLine(panelX, 0, panelX, 63);
            gOled.setFont(u8g2_font_4x6_tf);
            gOled.drawStr(panelX + 2, 8, "NOTIF");
            gOled.drawStr(panelX + 2, 20, "doprava");
            gOled.drawStr(panelX + 2, 28, "= full");
            gOled.drawStr(panelX + 2, 40, "doleva");
            gOled.drawStr(panelX + 2, 48, "= zavrit");
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

    if (state.spiMutex && xSemaphoreTake(state.spiMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        const bool notifStripVisible = state.notifications.viewMode == 1;
        const int panelWidth = kEinkLandscapeWidth / 3;
        const int panelX = kEinkLandscapeWidth - panelWidth;
        const int contentStartX = 0;
        const int contentWidth = kEinkLandscapeWidth;
        constexpr int kArtGlyphAdvance = 4;
        const char *artLines[] = {
R"( ▄▀▀▄ ▀▄  ▄▀▀▀▀▄   ▄▀▀▀█▀▀▄  ▄▀▀█▄▄▄▄  ▄▀▀▄    ▄▀▀▄  ▄▀▀█▄   ▄▀▀▄    ▄▀▀▄  ▄▀▀█▄▄▄▄ )",
R"(█  █ █ █ █      █ █    █  ▐ ▐  ▄▀   ▐ █   █    ▐  █ ▐ ▄▀ ▀▄ █   █    ▐  █ ▐  ▄▀   ▐ )",
R"(▐  █  ▀█ █      █ ▐   █       █▄▄▄▄▄  ▐  █        █   █▄▄▄█ ▐  █        █   █▄▄▄▄▄  )",
R"(  █   █  ▀▄    ▄▀    █        █    ▌    █   ▄    █   ▄▀   █   █   ▄    █    █    ▌  )",
R"(▄▀   █     ▀▀▀▀    ▄▀        ▄▀▄▄▄▄      ▀▄▀ ▀▄ ▄▀  █   ▄▀     ▀▄▀ ▀▄ ▄▀   ▄▀▄▄▄▄   )",
R"(█    ▐            █          █    ▐            ▀    ▐   ▐            ▀     █    ▐   )",
R"(▐                 ▐          ▐                                             ▐        )"
        };
        const size_t artLineCount = sizeof(artLines) / sizeof(artLines[0]);
        const int lineHeight = 10;
        const int artHeight = static_cast<int>(artLineCount) * lineHeight;
        const int artStartY = max(56, (kEinkLandscapeHeight - artHeight) / 2);

        Paint paint(gEinkBuffer, kEinkNativeWidth, kEinkNativeHeight);
        prepareLandscapePaint(paint);
        paint.Clear(kUncolored);
        paint.DrawRectangle(0, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
        paint.DrawStringAt(8, 10, "NOTEWAVE", &Font16, kColored);
        paint.DrawLine(0, 32, kEinkLandscapeWidth - 1, 32, kColored);

        const size_t maxGlyphsPerLine = max<size_t>(1, static_cast<size_t>(contentWidth / kArtGlyphAdvance));
        for (size_t i = 0; i < artLineCount; ++i) {
            String line = utf8ClipToGlyphs(artLines[i], maxGlyphsPerLine);
            const int lineWidth = static_cast<int>(utf8GlyphCount(line.c_str()) * kArtGlyphAdvance);
            const int x = contentStartX + max(0, (contentWidth - lineWidth) / 2);
            const int y = artStartY + static_cast<int>(i) * lineHeight;
            paint.DrawStringAtUtf8Compact(x, y, line.c_str(), &Font8, kColored, kArtGlyphAdvance);
        }

        if (notifStripVisible) {
            paint.DrawFilledRectangle(panelX, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kUncolored);
            paint.DrawRectangle(panelX, 0, kEinkLandscapeWidth - 1, kEinkLandscapeHeight - 1, kColored);
            paint.DrawLine(panelX, 0, panelX, kEinkLandscapeHeight - 1, kColored);
            paint.DrawStringAt(panelX + 8, 18, "UPOZORNENI", &Font12, kColored);
            paint.DrawStringAt(panelX + 8, 40, "doprava=full", &Font12, kColored);
            paint.DrawStringAt(panelX + 8, 58, "doleva=zavrit", &Font12, kColored);
        }

        gEink.display(paint.GetImage());
        refreshEinkWithCadence(false);
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

bool handleActiveAppInput(SystemState &state, char key, Stream &out) {
    // Decode CardKB-specific encodings before app-level routing.
    const uint8_t rawCode = static_cast<uint8_t>(key);
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
