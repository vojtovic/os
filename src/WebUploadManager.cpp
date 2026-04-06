#include "WebUploadManager.h"

#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

#include "DisplayManager.h"

namespace {
WebServer gServer(80);
SystemState *gState = nullptr;
bool gHandlersReady = false;
bool gRunning = false;
fs::FS *gActiveFs = nullptr;
File gUploadFile;
String gUploadPath;
String gStatusLine = "idle";
String gLastSavedFile = "";

String sanitizeFilename(String name) {
    name.trim();
    int slash = name.lastIndexOf('/');
    int backslash = name.lastIndexOf('\\');
    if (backslash > slash) {
        slash = backslash;
    }
    if (slash >= 0) {
        name = name.substring(slash + 1);
    }

    if (name.isEmpty()) {
        name = "upload.bin";
    }

    String clean;
    clean.reserve(name.length());
    for (size_t i = 0; i < name.length(); ++i) {
        const char ch = name[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_') {
            clean += ch;
        } else {
            clean += '_';
        }
    }

    if (clean.isEmpty()) {
        clean = "upload.bin";
    }

    return String("/uploads/") + clean;
}

fs::FS *chooseTargetFs(SystemState &state, String &label) {
    if (state.sdReady && state.config.sdEnabled) {
        label = "SD";
        return &SD;
    }

    if (state.littleFsReady) {
        label = "LittleFS";
        return &LittleFS;
    }

    label = "none";
    return nullptr;
}

void ensureUploadRoot(fs::FS &fs) {
    if (!fs.exists("/uploads")) {
        fs.mkdir("/uploads");
    }
}

String buildFileListHtml(fs::FS &fs) {
    String html = "<ul>";
    File dir = fs.open("/uploads", FILE_READ);
    if (!dir || !dir.isDirectory()) {
        html += "<li>no files yet</li></ul>";
        return html;
    }

    File entry = dir.openNextFile();
    uint8_t shown = 0;
    while (entry && shown < 12) {
        html += "<li>";
        html += entry.name();
        html += " (";
        html += String(entry.size());
        html += " B)</li>";
        ++shown;
        entry = dir.openNextFile();
    }

    if (shown == 0) {
        html += "<li>empty</li>";
    }

    html += "</ul>";
    return html;
}

String buildPageHtml() {
    String ip = gState ? gState->settings.wifiIp : String();
    String targetLabel = "offline";
    fs::FS *targetFs = gState ? chooseTargetFs(*gState, targetLabel) : nullptr;

    String html;
    html.reserve(4096);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>mp3-pedia web upload</title>";
    html += "<style>body{font-family:system-ui,sans-serif;margin:24px;max-width:720px} "
            "section{border:1px solid #ddd;border-radius:14px;padding:16px;margin:0 0 16px} "
            "code{background:#f4f4f4;padding:2px 6px;border-radius:6px} "
            "input[type=file]{display:block;margin:12px 0} "
            "button{padding:10px 14px;border:0;border-radius:10px;background:#111;color:#fff}</style></head><body>";
    html += "<h1>Web Upload</h1>";
    html += "<section><div><strong>Status:</strong> ";
    html += gStatusLine;
    html += "</div><div><strong>WiFi:</strong> ";
    html += ip.isEmpty() ? String("not connected") : ip;
    html += "</div><div><strong>Target:</strong> ";
    html += targetLabel;
    html += "</div><div><strong>Last file:</strong> ";
    html += gLastSavedFile.isEmpty() ? String("none") : gLastSavedFile;
    html += "</div></section>";
    html += "<section><form method='post' action='/upload' enctype='multipart/form-data'>";
    html += "<input type='file' name='file'><button type='submit'>Upload</button></form></section>";
    if (targetFs != nullptr) {
        html += "<section><h2>/uploads</h2>";
        html += buildFileListHtml(*targetFs);
        html += "</section>";
    }
    html += "<section><small>Open from the launcher or via <code>launcher open web-upload</code>.</small></section>";
    html += "</body></html>";
    return html;
}

void finishUpload(bool success, const String &message) {
    if (gUploadFile) {
        gUploadFile.close();
    }

    if (!success && gActiveFs != nullptr && gUploadPath.length() > 0) {
        gActiveFs->remove(gUploadPath);
    }

    gActiveFs = nullptr;
    gUploadPath = "";
    gStatusLine = message;
}

void handleUploadChunk() {
    HTTPUpload &upload = gServer.upload();
    if (gState == nullptr) {
        gStatusLine = "no state";
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        gStatusLine = "no storage";
        return;
    }

    switch (upload.status) {
        case UPLOAD_FILE_START: {
            ensureUploadRoot(*targetFs);
            gUploadPath = sanitizeFilename(upload.filename);
            if (targetFs->exists(gUploadPath)) {
                targetFs->remove(gUploadPath);
            }
            gUploadFile = targetFs->open(gUploadPath, FILE_WRITE);
            gActiveFs = targetFs;
            if (!gUploadFile) {
                gStatusLine = "open failed";
                gActiveFs = nullptr;
                gUploadPath = "";
                return;
            }
            gStatusLine = String("uploading ") + gUploadPath;
            break;
        }
        case UPLOAD_FILE_WRITE:
            if (gUploadFile) {
                const size_t written = gUploadFile.write(upload.buf, upload.currentSize);
                if (written != upload.currentSize) {
                    finishUpload(false, "write failed");
                }
            }
            break;
        case UPLOAD_FILE_END:
            if (gUploadFile) {
                gUploadFile.close();
                gLastSavedFile = gUploadPath;
                gStatusLine = String("saved ") + gUploadPath + " (" + String(upload.totalSize) + " B)";
                gActiveFs = nullptr;
                gUploadPath = "";
            }
            break;
        case UPLOAD_FILE_ABORTED:
            finishUpload(false, "aborted");
            break;
        default:
            break;
    }
}

void registerHandlers() {
    if (gHandlersReady) {
        return;
    }

    gServer.on("/", HTTP_GET, []() {
        gServer.send(200, "text/html; charset=utf-8", buildPageHtml());
    });

    gServer.on("/upload", HTTP_POST, []() {
        gServer.send(200, "text/plain", gStatusLine);
    }, handleUploadChunk);

    gServer.onNotFound([]() {
        gServer.send(404, "text/plain", "Not found");
    });

    gHandlersReady = true;
}

bool ensureServerRunning(SystemState &state, Stream *out) {
    if (gRunning) {
        return true;
    }

    if (!state.settings.wifiConnected || state.settings.wifiIp.isEmpty()) {
        gStatusLine = "connect WiFi first";
        if (out != nullptr) {
            out->println("Web upload: WiFi not connected");
        }
        return false;
    }

    registerHandlers();
    gState = &state;
    gServer.begin();
    gRunning = true;
    gStatusLine = String("listening on http://") + state.settings.wifiIp + "/";
    if (out != nullptr) {
        out->print("Web upload: server started at http://");
        out->println(state.settings.wifiIp);
    }
    return true;
}
}  // namespace

bool renderWebUploadScreen(SystemState &state, bool oledOnly, Stream &out) {
    ensureServerRunning(state, &out);
    const String line1 = String("server: ") + (gRunning ? "ready" : "offline");
    const String line2 = String("url: http://") + (state.settings.wifiIp.isEmpty() ? String("offline") : state.settings.wifiIp) + "/";
    String line3 = String("target: ") + (state.sdReady && state.config.sdEnabled ? "SD" : "LittleFS");
    if (!gLastSavedFile.isEmpty()) {
        line3 += String(" last: ") + gLastSavedFile;
    }

    renderStatusScreen(state, "WEB UPLOAD", line1, line2, line3, oledOnly, out);
    out.println("Web upload screen rendered");
    return true;
}

bool startWebUploadServer(SystemState &state, Stream &out) {
    return ensureServerRunning(state, &out);
}

void printWebUploadStatus(const SystemState &state, Stream &out) {
    out.println("--- Web Upload ---");
    out.print("running: ");
    out.println(gRunning ? "yes" : "no");
    out.print("wifi: ");
    out.println(state.settings.wifiConnected ? state.settings.wifiIp : String("not connected"));
    out.print("target: ");
    out.println(state.sdReady && state.config.sdEnabled ? "SD" : "LittleFS");
    out.print("status: ");
    out.println(gStatusLine);
    out.print("last file: ");
    out.println(gLastSavedFile.isEmpty() ? String("none") : gLastSavedFile);
}

bool isWebUploadServerRunning() {
    return gRunning;
}

void serviceWebUploadServer(SystemState &state) {
    if (!gRunning) {
        if (gState != nullptr) {
            ensureServerRunning(state, nullptr);
        }
        return;
    }

    if (!state.settings.wifiConnected) {
        stopWebUploadServer(Serial);
        return;
    }

    gServer.handleClient();
}

void stopWebUploadServer(Stream &out) {
    if (!gRunning) {
        return;
    }

    finishUpload(false, "stopped");
    gServer.stop();
    gState = nullptr;
    gRunning = false;
    gStatusLine = "stopped";
    out.println("Web upload: server stopped");
}