#include "WebUploadManager.h"

#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>

#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <HTTPSServer.hpp>
#include <ResourceNode.hpp>
#include <SSLCert.hpp>

#include "DisplayManager.h"

using namespace httpsserver;

namespace {
SystemState *gState = nullptr;
bool gHandlersReady = false;
bool gRunning = false;
bool gHttpsReady = false;

SSLCert *gCert = nullptr;
HTTPSServer *gHttpsServer = nullptr;
HTTPServer *gHttpServer = nullptr;

String gStatusLine = "idle";
String gLastSavedFile = "";

constexpr uint16_t kHttpsPort = 443;
constexpr uint16_t kHttpPort = 80;
constexpr uint8_t kHttpsMaxConnections = 2;
constexpr uint8_t kHttpMaxConnections = 4;

String normalizePath(String path) {
    path.trim();
    path.replace('\\', '/');
    if (path.isEmpty()) {
        return String("/");
    }

    if (!path.startsWith("/")) {
        path = String("/") + path;
    }

    String out = "/";
    int start = 1;
    while (start <= static_cast<int>(path.length())) {
        int slash = path.indexOf('/', start);
        if (slash < 0) {
            slash = path.length();
        }

        String token = path.substring(start, slash);
        token.trim();
        if (!token.isEmpty() && token != ".") {
            if (token == "..") {
                if (out.length() > 1) {
                    const int prev = out.lastIndexOf('/', out.length() - 2);
                    if (prev >= 0) {
                        out = out.substring(0, prev + 1);
                    } else {
                        out = "/";
                    }
                }
            } else {
                if (!out.endsWith("/")) {
                    out += "/";
                }
                out += token;
            }
        }

        start = slash + 1;
    }

    if (out.length() > 1 && out.endsWith("/")) {
        out.remove(out.length() - 1);
    }

    return out;
}

String sanitizeName(String name) {
    name.trim();

    String clean;
    clean.reserve(name.length());
    for (size_t i = 0; i < name.length(); ++i) {
        const char ch = name[i];
        if (ch == '/' || ch == '\\' || static_cast<uint8_t>(ch) < 32) {
            continue;
        }

        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_' || ch == ' ') {
            clean += ch;
        } else {
            clean += '_';
        }
    }

    if (clean.isEmpty()) {
        clean = "item";
    }

    return clean;
}

String joinPath(const String &parentRaw, const String &nameRaw) {
    const String parent = normalizePath(parentRaw);
    const String name = sanitizeName(nameRaw);
    if (parent == "/") {
        return normalizePath(String("/") + name);
    }

    return normalizePath(parent + "/" + name);
}

String parentPathOf(const String &pathRaw) {
    const String path = normalizePath(pathRaw);
    if (path == "/") {
        return String("/");
    }

    const int slash = path.lastIndexOf('/');
    if (slash <= 0) {
        return String("/");
    }

    return path.substring(0, slash);
}

bool ensureDirectoryPath(fs::FS &fs, const String &pathRaw) {
    const String path = normalizePath(pathRaw);
    if (path == "/") {
        return true;
    }

    String current = "/";
    int start = 1;
    while (start <= static_cast<int>(path.length())) {
        int slash = path.indexOf('/', start);
        if (slash < 0) {
            slash = path.length();
        }

        String token = path.substring(start, slash);
        if (!token.isEmpty()) {
            current = joinPath(current, token);
            if (!fs.exists(current) && !fs.mkdir(current)) {
                return false;
            }
        }

        start = slash + 1;
    }

    return true;
}

bool removePathRecursive(fs::FS &fs, const String &pathRaw) {
    const String path = normalizePath(pathRaw);
    if (path == "/") {
        return false;
    }

    File node = fs.open(path, FILE_READ);
    if (!node) {
        return false;
    }

    if (node.isDirectory()) {
        File child = node.openNextFile();
        while (child) {
            String childName = child.name();
            if (childName.startsWith("/")) {
                childName = childName.substring(1);
            }
            const String childPath = joinPath(path, childName);
            child.close();
            if (!removePathRecursive(fs, childPath)) {
                node.close();
                return false;
            }
            child = node.openNextFile();
        }
        node.close();
        return fs.rmdir(path);
    }

    node.close();
    return fs.remove(path);
}

String jsonEscape(const String &value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch == '"' || ch == '\\') {
            escaped += '\\';
            escaped += ch;
        } else if (ch == '\n') {
            escaped += "\\n";
        } else if (ch == '\r') {
            escaped += "\\r";
        } else {
            escaped += ch;
        }
    }
    return escaped;
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

String currentTargetLabel() {
    if (gState == nullptr) {
        return String("offline");
    }

    String label;
    if (chooseTargetFs(*gState, label) == nullptr) {
        return String("none");
    }

    return label;
}

bool getQueryParam(HTTPRequest *req, const char *name, String &out) {
    if (!req || !name) {
        return false;
    }

    ResourceParameters *params = req->getParams();
    if (!params) {
        return false;
    }

    std::string value;
    if (!params->getQueryParameter(name, value)) {
        return false;
    }

    out = String(value.c_str());
    return true;
}

void sendJson(HTTPResponse *res, int code, const String &json) {
    res->setStatusCode(code);
    res->setHeader("Content-Type", "application/json; charset=utf-8");
    res->println(json.c_str());
}

void sendApiError(HTTPResponse *res, int code, const String &message) {
    String json = "{\"ok\":false,\"error\":\"";
    json += jsonEscape(message);
    json += "\"}";
    sendJson(res, code, json);
}

String buildListJson(fs::FS &fs, const String &pathRaw) {
    const String path = normalizePath(pathRaw);
    File dir = fs.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) {
        return String("{\"ok\":false,\"error\":\"folder not found\"}");
    }

    String json = "{\"ok\":true,\"path\":\"";
    json += jsonEscape(path);
    json += "\",\"entries\":[";

    bool first = true;
    File entry = dir.openNextFile();
    while (entry) {
        String name = entry.name();
        if (name.startsWith("/")) {
            name = name.substring(1);
        }

        const bool isDir = entry.isDirectory();
        const uint32_t size = isDir ? 0 : static_cast<uint32_t>(entry.size());
        const String fullPath = joinPath(path, name);

        if (!first) {
            json += ",";
        }
        first = false;

        json += "{\"name\":\"";
        json += jsonEscape(name);
        json += "\",\"path\":\"";
        json += jsonEscape(fullPath);
        json += "\",\"isDir\":";
        json += isDir ? "true" : "false";
        json += ",\"size\":";
        json += String(size);
        json += "}";

        entry.close();
        entry = dir.openNextFile();
    }

    dir.close();
    json += "],\"target\":\"";
    json += currentTargetLabel();
    json += "\"}";
    return json;
}

String buildPageHtml() {
    String ip = gState ? gState->settings.wifiIp : String();
    String targetLabel = currentTargetLabel();

    String html;
    html.reserve(12000);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>NoteWave Explorer</title>";
    html += "<style>"
            ":root{--bg:#e9eef5;--panel:#ffffff;--line:#d7dee8;--text:#1d2a3a;--muted:#5f6e82;--accent:#0b57d0;--accent2:#0f69ff;}"
            "*{box-sizing:border-box}"
            "body{margin:0;background:linear-gradient(180deg,#eef3fb,#e6edf9);font-family:'Segoe UI',Tahoma,sans-serif;color:var(--text)}"
            ".app{max-width:1100px;margin:18px auto;padding:0 14px}"
            ".title{font-size:20px;font-weight:700;margin:0 0 10px 2px}"
            ".shell{background:var(--panel);border:1px solid var(--line);border-radius:14px;overflow:hidden;box-shadow:0 8px 26px rgba(16,39,80,.08)}"
            ".toolbar{display:flex;gap:8px;align-items:center;padding:10px;border-bottom:1px solid var(--line);background:#f8fbff;flex-wrap:wrap}"
            "button,.btn{border:1px solid #c9d4e3;background:#fff;color:#1b2d45;border-radius:9px;padding:7px 11px;font-weight:600;cursor:pointer}"
            "button.primary{background:var(--accent);color:#fff;border-color:var(--accent2)}"
            "button:disabled{opacity:.5;cursor:not-allowed}"
            ".path{flex:1;min-width:260px;border:1px solid #cfd9e8;border-radius:9px;padding:7px 10px;background:#fff;font-family:ui-monospace,Consolas,monospace}"
            ".grid{display:grid;grid-template-columns:220px 1fr;min-height:540px}"
            ".left{border-right:1px solid var(--line);padding:10px;background:#f9fbff}"
            ".left h3{margin:4px 0 10px;font-size:13px;color:var(--muted);font-weight:700;letter-spacing:.04em;text-transform:uppercase}"
            ".chip{display:block;margin-bottom:8px;padding:8px 10px;border:1px solid #d3deee;border-radius:10px;background:#fff;font-size:13px}"
            ".chip b{display:block;color:#1e3047;margin-bottom:2px}"
            ".right{display:flex;flex-direction:column}"
            ".tablewrap{overflow:auto;flex:1}"
            "table{width:100%;border-collapse:collapse;font-size:14px}"
            "thead th{position:sticky;top:0;background:#f4f8ff;border-bottom:1px solid var(--line);text-align:left;padding:10px}"
            "tbody td{padding:9px 10px;border-bottom:1px solid #edf2f8}"
            "tbody tr{cursor:pointer}"
            "tbody tr:hover{background:#f4f8ff}"
            "tbody tr.sel{background:#dfeeff}"
            ".name{display:flex;align-items:center;gap:8px}"
            ".ico{font-size:16px;width:24px;text-align:center;font-family:ui-monospace,Consolas,monospace}"
            ".status{padding:8px 10px;border-top:1px solid var(--line);font-size:12px;color:#4f6075;background:#f8fbff}"
            "@media (max-width:900px){.grid{grid-template-columns:1fr}.left{border-right:0;border-bottom:1px solid var(--line)}}"
            "</style></head><body>";
    html += "<div class='app'><div class='title'>NoteWave File Explorer (HTTPS)</div><div class='shell'>";
    html += "<div class='toolbar'>";
    html += "<button id='upBtn'>Up</button><button id='refreshBtn'>Refresh</button>";
    html += "<button id='newFolderBtn'>New Folder</button><button id='renameBtn'>Rename</button><button id='deleteBtn'>Delete</button>";
    html += "<label class='btn primary'>Upload<input id='uploadInput' type='file' multiple style='display:none'></label>";
    html += "<button id='downloadBtn'>Download</button>";
    html += "<input id='pathInput' class='path' value='/'><button id='goBtn'>Go</button>";
    html += "</div>";
    html += "<div class='grid'><div class='left'>";
    html += "<h3>Connection</h3>";
    html += "<div class='chip'><b>Target</b><span id='targetLabel'>" + targetLabel + "</span></div>";
    html += "<div class='chip'><b>WiFi IP</b><span>" + (ip.isEmpty() ? String("offline") : ip) + "</span></div>";
    html += "<div class='chip'><b>Last Saved</b><span id='lastSaved'>" + (gLastSavedFile.isEmpty() ? String("none") : gLastSavedFile) + "</span></div>";
    html += "<div class='chip'><b>Status</b><span id='serverStatus'>" + gStatusLine + "</span></div>";
    html += "</div><div class='right'><div class='tablewrap'>";
    html += "<table><thead><tr><th style='width:55%'>Name</th><th style='width:15%'>Type</th><th style='width:20%'>Size</th><th style='width:10%'>Open</th></tr></thead><tbody id='fileRows'></tbody></table>";
    html += "</div><div class='status' id='statusLine'>Ready.</div></div></div></div></div>";
    html += "<script>"
            "const pathInput=document.getElementById('pathInput');"
            "const rows=document.getElementById('fileRows');"
            "const statusLine=document.getElementById('statusLine');"
            "const targetLabel=document.getElementById('targetLabel');"
            "const serverStatus=document.getElementById('serverStatus');"
            "const lastSaved=document.getElementById('lastSaved');"
            "let currentPath='/';let selected=null;let selectedIsDir=false;"
            "function setStatus(t){statusLine.textContent=t;}"
            "function esc(s){return String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');}"
            "function fmtSize(v){if(!v)return '-';if(v<1024)return v+' B';if(v<1024*1024)return (v/1024).toFixed(1)+' KB';return (v/(1024*1024)).toFixed(1)+' MB';}"
            "async function api(url,opts){const r=await fetch(url,opts);const t=await r.text();let j={};try{j=JSON.parse(t);}catch(_){j={ok:false,error:t||'invalid server response'};}if(!r.ok||!j.ok)throw new Error(j.error||('HTTP '+r.status));return j;}"
            "function parentPath(p){if(p==='/'||!p)return '/';const i=p.lastIndexOf('/');if(i<=0)return '/';return p.slice(0,i);}"
            "function renderEntries(list){rows.innerHTML='';if(!list.length){rows.innerHTML='<tr><td colspan=4><em>Folder is empty</em></td></tr>';return;}list.forEach(e=>{const tr=document.createElement('tr');tr.dataset.path=e.path;tr.dataset.dir=e.isDir?'1':'0';tr.innerHTML=`<td><div class='name'><span class='ico'>${e.isDir?'[D]':'[F]'}</span><span>${esc(e.name)}</span></div></td><td>${e.isDir?'Folder':'File'}</td><td>${fmtSize(e.size)}</td><td>${e.isDir?'Open':'-'}</td>`;tr.onclick=()=>{document.querySelectorAll('tbody tr').forEach(x=>x.classList.remove('sel'));tr.classList.add('sel');selected=e.path;selectedIsDir=!!e.isDir;setStatus('Selected: '+e.path);};tr.ondblclick=()=>{if(e.isDir){loadDir(e.path);}else{window.location='/api/download?path='+encodeURIComponent(e.path);}};rows.appendChild(tr);});}"
            "async function loadDir(path){try{setStatus('Loading '+path+' ...');const j=await api('/api/list?path='+encodeURIComponent(path));currentPath=j.path||path;pathInput.value=currentPath;selected=null;selectedIsDir=false;renderEntries(j.entries||[]);if(j.target){targetLabel.textContent=j.target;}serverStatus.textContent='ready';setStatus('Loaded '+currentPath);}catch(e){setStatus('Error: '+e.message);}}"
            "document.getElementById('goBtn').onclick=()=>loadDir(pathInput.value||'/');"
            "document.getElementById('refreshBtn').onclick=()=>loadDir(currentPath);"
            "document.getElementById('upBtn').onclick=()=>loadDir(parentPath(currentPath));"
            "document.getElementById('newFolderBtn').onclick=async()=>{const name=prompt('Folder name:');if(!name)return;try{await api('/api/mkdir?path='+encodeURIComponent(currentPath)+'&name='+encodeURIComponent(name),{method:'POST'});setStatus('Folder created');await loadDir(currentPath);}catch(e){setStatus('Error: '+e.message);}};"
            "document.getElementById('renameBtn').onclick=async()=>{if(!selected){setStatus('Select an item first');return;}const base=selected.split('/').filter(Boolean).pop()||'';const name=prompt('New name:',base);if(!name)return;try{await api('/api/rename?from='+encodeURIComponent(selected)+'&name='+encodeURIComponent(name),{method:'POST'});setStatus('Renamed');await loadDir(currentPath);}catch(e){setStatus('Error: '+e.message);}};"
            "document.getElementById('deleteBtn').onclick=async()=>{if(!selected){setStatus('Select an item first');return;}if(!confirm('Delete '+selected+' ?'))return;try{await api('/api/delete?path='+encodeURIComponent(selected),{method:'POST'});setStatus('Deleted');await loadDir(currentPath);}catch(e){setStatus('Error: '+e.message);}};"
            "document.getElementById('downloadBtn').onclick=()=>{if(!selected||selectedIsDir){setStatus('Select a file to download');return;}window.location='/api/download?path='+encodeURIComponent(selected);};"
            "document.getElementById('uploadInput').onchange=async(ev)=>{const files=[...(ev.target.files||[])];if(!files.length)return;for(const f of files){try{const fd=new FormData();fd.append('file',f,f.name);setStatus('Uploading '+f.name+' ...');const r=await fetch('/api/upload?path='+encodeURIComponent(currentPath),{method:'POST',body:fd});if(!r.ok){throw new Error('upload failed');}const msg=await r.text();let j={};try{j=JSON.parse(msg);}catch(_){j={ok:false,error:msg};}if(!j.ok){throw new Error(j.error||'upload failed');}serverStatus.textContent='saved '+(j.path||f.name);lastSaved.textContent=(j.path||f.name);}catch(e){setStatus('Upload error: '+e.message);break;}}ev.target.value='';await loadDir(currentPath);};"
            "loadDir('/');"
            "</script></body></html>";
    return html;
}

bool ensureCertificate() {
    if (gCert != nullptr) {
        return true;
    }

    gCert = new SSLCert();
    const int certResult = createSelfSignedCert(
        *gCert,
        KEYSIZE_1024,
        "CN=notewave.local,O=NoteWave,C=CZ",
        "20260101000000",
        "20360101000000");

    if (certResult != 0) {
        Serial.print("Web upload HTTPS: certificate create failed: ");
        Serial.println(certResult);
        delete gCert;
        gCert = nullptr;
        return false;
    }

    return true;
}

void handleRoot(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    res->setHeader("Content-Type", "text/html; charset=utf-8");
    res->println(buildPageHtml().c_str());
}

void handleApiList(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    if (gState == nullptr) {
        sendApiError(res, 503, "state unavailable");
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        sendApiError(res, 503, "no storage available");
        return;
    }

    String path;
    if (!getQueryParam(req, "path", path)) {
        path = "/";
    }

    sendJson(res, 200, buildListJson(*targetFs, path));
}

void handleApiMkdir(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    if (gState == nullptr) {
        sendApiError(res, 503, "state unavailable");
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        sendApiError(res, 503, "no storage available");
        return;
    }

    String base;
    String name;
    if (!getQueryParam(req, "path", base)) {
        base = "/";
    }
    if (!getQueryParam(req, "name", name) || name.isEmpty()) {
        sendApiError(res, 400, "missing folder name");
        return;
    }

    const String full = joinPath(base, name);
    if (!ensureDirectoryPath(*targetFs, parentPathOf(full))) {
        sendApiError(res, 500, "cannot prepare parent folder");
        return;
    }
    if (targetFs->exists(full)) {
        sendApiError(res, 409, "already exists");
        return;
    }
    if (!targetFs->mkdir(full)) {
        sendApiError(res, 500, "mkdir failed");
        return;
    }

    gStatusLine = String("folder created ") + full;
    sendJson(res, 200, "{\"ok\":true}");
}

void handleApiDelete(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    if (gState == nullptr) {
        sendApiError(res, 503, "state unavailable");
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        sendApiError(res, 503, "no storage available");
        return;
    }

    String path;
    if (!getQueryParam(req, "path", path)) {
        sendApiError(res, 400, "invalid path");
        return;
    }

    path = normalizePath(path);
    if (path.isEmpty() || path == "/") {
        sendApiError(res, 400, "invalid path");
        return;
    }

    if (!targetFs->exists(path)) {
        sendApiError(res, 404, "path not found");
        return;
    }

    if (!removePathRecursive(*targetFs, path)) {
        sendApiError(res, 500, "delete failed");
        return;
    }

    gStatusLine = String("deleted ") + path;
    sendJson(res, 200, "{\"ok\":true}");
}

void handleApiRename(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    if (gState == nullptr) {
        sendApiError(res, 503, "state unavailable");
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        sendApiError(res, 503, "no storage available");
        return;
    }

    String from;
    String name;
    if (!getQueryParam(req, "from", from) || !getQueryParam(req, "name", name) || name.isEmpty()) {
        sendApiError(res, 400, "invalid rename params");
        return;
    }

    from = normalizePath(from);
    if (from.isEmpty() || from == "/") {
        sendApiError(res, 400, "invalid source");
        return;
    }
    if (!targetFs->exists(from)) {
        sendApiError(res, 404, "source not found");
        return;
    }

    const String to = joinPath(parentPathOf(from), name);
    if (targetFs->exists(to)) {
        sendApiError(res, 409, "target exists");
        return;
    }

    if (!targetFs->rename(from, to)) {
        sendApiError(res, 500, "rename failed");
        return;
    }

    gStatusLine = String("renamed ") + from + " -> " + to;
    sendJson(res, 200, "{\"ok\":true}");
}

void handleApiDownload(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    if (gState == nullptr) {
        res->setStatusCode(503);
        res->setHeader("Content-Type", "text/plain");
        res->println("state unavailable");
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        res->setStatusCode(503);
        res->setHeader("Content-Type", "text/plain");
        res->println("no storage available");
        return;
    }

    String path;
    if (!getQueryParam(req, "path", path)) {
        res->setStatusCode(400);
        res->setHeader("Content-Type", "text/plain");
        res->println("invalid path");
        return;
    }

    path = normalizePath(path);
    if (path.isEmpty() || path == "/") {
        res->setStatusCode(400);
        res->setHeader("Content-Type", "text/plain");
        res->println("invalid path");
        return;
    }

    File file = targetFs->open(path, FILE_READ);
    if (!file || file.isDirectory()) {
        res->setStatusCode(404);
        res->setHeader("Content-Type", "text/plain");
        res->println("file not found");
        return;
    }

    String fileName = path;
    const int slash = fileName.lastIndexOf('/');
    if (slash >= 0 && slash + 1 < static_cast<int>(fileName.length())) {
        fileName = fileName.substring(slash + 1);
    }

    String contentDisposition = String("attachment; filename=\"") + fileName + "\"";
    res->setHeader("Content-Type", "application/octet-stream");
    res->setHeader("Content-Disposition", contentDisposition.c_str());

    uint8_t buffer[1024];
    while (file.available()) {
        const size_t readLength = file.read(buffer, sizeof(buffer));
        if (readLength == 0) {
            break;
        }
        res->write(buffer, readLength);
    }

    file.close();
}

void handleApiUpload(HTTPRequest *req, HTTPResponse *res) {
    if (gState == nullptr) {
        sendApiError(res, 503, "state unavailable");
        req->discardRequestBody();
        return;
    }

    String targetLabel;
    fs::FS *targetFs = chooseTargetFs(*gState, targetLabel);
    if (targetFs == nullptr) {
        sendApiError(res, 503, "no storage available");
        req->discardRequestBody();
        return;
    }

    String basePath;
    if (!getQueryParam(req, "path", basePath)) {
        basePath = "/";
    }
    basePath = normalizePath(basePath);
    if (!ensureDirectoryPath(*targetFs, basePath)) {
        sendApiError(res, 500, "cannot prepare target folder");
        req->discardRequestBody();
        return;
    }

    std::string contentType = req->getHeader("Content-Type");
    const size_t semicolonPos = contentType.find(';');
    if (semicolonPos != std::string::npos) {
        contentType = contentType.substr(0, semicolonPos);
    }
    if (contentType != "multipart/form-data") {
        sendApiError(res, 415, "content type must be multipart/form-data");
        req->discardRequestBody();
        return;
    }

    HTTPMultipartBodyParser parser(req);
    bool saved = false;
    String savedPath;

    while (parser.nextField()) {
        const std::string fieldName = parser.getFieldName();
        const std::string fileNameRaw = parser.getFieldFilename();

        if (fieldName != "file" || fileNameRaw.empty()) {
            while (!parser.endOfField()) {
                uint8_t drain[256];
                const size_t readLength = parser.read(drain, sizeof(drain));
                if (readLength == 0) {
                    break;
                }
            }
            continue;
        }

        String cleanName = sanitizeName(String(fileNameRaw.c_str()));
        if (cleanName.isEmpty()) {
            cleanName = "upload.bin";
        }

        savedPath = joinPath(basePath, cleanName);
        if (targetFs->exists(savedPath)) {
            targetFs->remove(savedPath);
        }

        File out = targetFs->open(savedPath, FILE_WRITE);
        if (!out) {
            sendApiError(res, 500, "cannot open target file");
            req->discardRequestBody();
            return;
        }

        while (!parser.endOfField()) {
            uint8_t chunk[1024];
            const size_t readLength = parser.read(chunk, sizeof(chunk));
            if (readLength == 0) {
                break;
            }
            const size_t written = out.write(chunk, readLength);
            if (written != readLength) {
                out.close();
                targetFs->remove(savedPath);
                sendApiError(res, 500, "write failed");
                req->discardRequestBody();
                return;
            }
        }

        out.close();
        saved = true;
        gLastSavedFile = savedPath;
    }

    if (!saved) {
        sendApiError(res, 400, "no uploaded file field");
        return;
    }

    gStatusLine = String("saved ") + savedPath;
    String json = "{\"ok\":true,\"path\":\"";
    json += jsonEscape(savedPath);
    json += "\"}";
    sendJson(res, 200, json);
}

void handle404(HTTPRequest *req, HTTPResponse *res) {
    req->discardRequestBody();
    res->setStatusCode(404);
    res->setHeader("Content-Type", "text/plain; charset=utf-8");
    res->println("Not found");
}

void registerHandlersOnServer(HTTPServer &server) {
    server.registerNode(new ResourceNode("/", "GET", &handleRoot));
    server.registerNode(new ResourceNode("/api/list", "GET", &handleApiList));
    server.registerNode(new ResourceNode("/api/mkdir", "POST", &handleApiMkdir));
    server.registerNode(new ResourceNode("/api/delete", "POST", &handleApiDelete));
    server.registerNode(new ResourceNode("/api/rename", "POST", &handleApiRename));
    server.registerNode(new ResourceNode("/api/download", "GET", &handleApiDownload));
    server.registerNode(new ResourceNode("/api/upload", "POST", &handleApiUpload));
    server.setDefaultNode(new ResourceNode("", "GET", &handle404));
}

void registerHandlers() {
    if (gHandlersReady) {
        return;
    }

    if (gHttpsServer) {
        registerHandlersOnServer(*gHttpsServer);
    }
    if (gHttpServer) {
        registerHandlersOnServer(*gHttpServer);
    }

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

    gState = &state;

    if (gHttpServer == nullptr) {
        gHttpServer = new HTTPServer(kHttpPort, kHttpMaxConnections);
    }

    if (ensureCertificate() && gHttpsServer == nullptr) {
        gHttpsServer = new HTTPSServer(gCert, kHttpsPort, kHttpsMaxConnections);
    }

    registerHandlers();

    bool anyRunning = false;
    if (gHttpsServer != nullptr) {
        gHttpsServer->start();
        gHttpsReady = gHttpsServer->isRunning();
        anyRunning = anyRunning || gHttpsReady;
    } else {
        gHttpsReady = false;
    }

    if (gHttpServer != nullptr) {
        gHttpServer->start();
        anyRunning = anyRunning || gHttpServer->isRunning();
    }

    gRunning = anyRunning;
    if (!gRunning) {
        gStatusLine = "server start failed";
        if (out != nullptr) {
            out->println("Web upload: server start failed");
        }
        return false;
    }

    const String proto = gHttpsReady ? "https" : "http";
    gStatusLine = String("listening on ") + proto + "://" + state.settings.wifiIp + "/";
    if (out != nullptr) {
        out->print("Web upload: server started at ");
        out->println(gStatusLine);
    }
    return true;
}
}  // namespace

bool renderWebUploadScreen(SystemState &state, bool oledOnly, Stream &out) {
    ensureServerRunning(state, &out);
    const String line1 = String("server: ") + (gRunning ? "ready" : "offline");
    const String urlPrefix = gHttpsReady ? "https://" : "http://";
    const String line2 = String("url: ") + urlPrefix + (state.settings.wifiIp.isEmpty() ? String("offline") : state.settings.wifiIp) + "/";
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
    out.print("https: ");
    out.println(gHttpsReady ? "yes" : "no");
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

    if (gHttpsServer != nullptr && gHttpsServer->isRunning()) {
        gHttpsServer->loop();
    }
    if (gHttpServer != nullptr && gHttpServer->isRunning()) {
        gHttpServer->loop();
    }
}

void stopWebUploadServer(Stream &out) {
    if (!gRunning) {
        return;
    }

    if (gHttpsServer != nullptr && gHttpsServer->isRunning()) {
        gHttpsServer->stop();
    }
    if (gHttpServer != nullptr && gHttpServer->isRunning()) {
        gHttpServer->stop();
    }

    gRunning = false;
    gHttpsReady = false;
    gStatusLine = "stopped";
    out.println("Web upload: server stopped");
}
