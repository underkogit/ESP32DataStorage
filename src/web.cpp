#include "web.h"
#include "storage.h"
#include "ui.h"
#include "logging.h"
#include "config.h"

#include <WebServer.h>
#include <WiFi.h>

#include <dirent.h>
#include <sys/stat.h>
#include <string.h>

namespace {

WebServer* server = nullptr;
File uploadFile;
int uploadCard = -1;

String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

bool validPath(const String& path) {
    return path.startsWith("/") && path.indexOf("..") < 0;
}

int parseCard() {
    String v = server ? server->arg("card") : String("");
    if (v.length() == 0) return 0;
    return v.toInt();
}

void sendError(int code, const char* msg) {
    if (server) server->send(code, "application/json", String("{\"error\":\"") + msg + "\"}");
}

String urlDecode(const String& input) {
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '%' && i + 2 < input.length()) {
            char h = input[++i];
            char l = input[++i];
            auto hex = [](char x) -> int {
                if (x >= '0' && x <= '9') return x - '0';
                if (x >= 'a' && x <= 'f') return x - 'a' + 10;
                if (x >= 'A' && x <= 'F') return x - 'A' + 10;
                return -1;
            };
            int hi = hex(h);
            int lo = hex(l);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
            } else {
                out += '%';
                out += h;
                out += l;
            }
        } else if (c == '+') {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

String baseNameOf(const String& p) {
    int slash = p.lastIndexOf('/');
    return (slash >= 0) ? p.substring(slash + 1) : p;
}

// ---------- Low-level fast helpers against the storage's own POSIX mount ----------
//
// Why: the Arduino File API is expensive here. dir.openNextFile() does
// readdir + stat() + fopen()/opendir() for EVERY entry (vfs_api.cpp), i.e. the
// FAT directory chain gets re-read from the card 3x per item:
// (see VFSFileImpl::openNextFile constructing a full VFSFileImpl).
// Plain POSIX opendir()/readdir()/stat() avoids the per-entry open entirely.

String fsPath(Storage* s, const String& path) {
    return String(s->mount()) + path;   // path always starts with '/'
}

bool isDotName(const char* n) {
    return n[0] == '\0' || !strcmp(n, ".") || !strcmp(n, "..");
}

bool statIsDir(const String& full) {
    struct stat st;
    if (stat(full.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return false;
}

// Children count of a directory: one readdir pass, no per-entry open().
long countChildren(const String& full) {
    DIR* d = opendir(full.c_str());
    if (!d) return -1;
    long n = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (!isDotName(e->d_name)) n++;
    }
    closedir(d);
    return n;
}

String childJoin(const String& dirPath, const String& name) {
    return dirPath.endsWith("/") ? dirPath + name : dirPath + '/' + name;
}

// Create every missing parent directory of '/a/b/c.log' (i.e. '/a', '/a/b').
// VFSImpl::mkdir returns true when the dir already exists, so errors on the
// final segment (a plain file) are harmless.
void ensureParentDirs(Storage* s, const String& path) {
    int p = 1;  // skip the leading '/'
    while (true) {
        int slash = path.indexOf('/', p);
        if (slash < 0) break;
        String dir = path.substring(0, slash);
        if (dir.length() > 1) s->fs().mkdir(dir);
        p = slash + 1;
    }
}

void beginChunked(const char* mime, const String& prelude) {
    server->sendHeader("Cache-Control", "no-store");
    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, mime, "");
    server->sendContent(prelude);
}

// ---- /api/files: instant single-level listing (names + type only) ----
void handleApiFiles() {
    Storage* s = nullptr;
    int card = parseCard();
    if (card >= 0 && card < storage::count()) s = storage::get(card);
    if (!s) { sendError(404, "card not found"); return; }
    if (!s->ready()) { sendError(503, "storage not ready"); return; }

    String dirPath = urlDecode(server->arg("dir"));
    if (dirPath.length() == 0) dirPath = urlDecode(server->arg("path"));

    if (dirPath.length() == 0) dirPath = "/";
    if (!validPath(dirPath)) { sendError(400, "invalid dir"); return; }
    while (dirPath.length() > 1 && dirPath.endsWith("/")) dirPath = dirPath.substring(0, dirPath.length() - 1);
    if (dirPath.length() == 0) dirPath = "/";

    uint32_t t0 = millis();
    DIR* d = opendir(fsPath(s, dirPath).c_str());
    if (!d) { sendError(404, "dir not found"); return; }

    beginChunked("application/json",
        "{\"card\":" + String(card) + ",\"dir\":\"" + jsonEscape(dirPath) + "\",\"children\":[");

    // NOTE: sizes are NOT fetched here — the client pulls them progressively
    // from /api/sizes, so the list itself renders almost instantly.
    String chunk;
    chunk.reserve(900);
    bool firstItem = true;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (isDotName(e->d_name)) continue;

        bool isDir;
        if (e->d_type == DT_DIR) isDir = true;
        else if (e->d_type == DT_REG) isDir = false;
        else isDir = statIsDir(fsPath(s, childJoin(dirPath, String(e->d_name))));  // rare fallback

        String name = String(e->d_name);
        String childPath = childJoin(dirPath, name);

        if (!firstItem) chunk += ',';
        firstItem = false;
        chunk += "{\"name\":\"" + jsonEscape(name) + "\",\"path\":\"" + jsonEscape(childPath) +
                 "\",\"dir\":" + (isDir ? "true" : "false") + "}";
        if (chunk.length() >= 768) { server->sendContent(chunk); chunk = ""; }
    }
    closedir(d);

    chunk += "]}";
    server->sendContent(chunk);
    Serial.printf("[API] /api/files dir=%s listed in %u ms\n", dirPath.c_str(), (unsigned)(millis() - t0));
}

// ---- /api/sizes: progressive size / children-count fill-up ----
// GET /api/sizes?card=X&dir=/&start=0&count=24
// Walks the SAME entry sequence as /api/files (filter non-functions (. and ..),
// same names — stat from index `start`, at most `count` entries per request.
// Response: {"items":[{"name":"a.txt","dir":false,"size":123},
//                     {"name":"Sub","dir":true,"children":7}...],
//             "more": <next start index> | -1}
void handleApiSizes() {
    Storage* s = nullptr;
    int card = parseCard();
    if (card >= 0 && card < storage::count()) s = storage::get(card);
    if (!s) { sendError(404, "card not found"); return; }
    if (!s->ready()) { sendError(503, "storage not ready"); return; }

    String dirPath = urlDecode(server->arg("dir"));
    if (dirPath.length() == 0) dirPath = urlDecode(server->arg("path"));
    if (dirPath.length() == 0) dirPath = "/";
    if (!validPath(dirPath)) { sendError(400, "invalid dir"); return; }
    while (dirPath.length() > 1 && dirPath.endsWith("/")) dirPath = dirPath.substring(0, dirPath.length() - 1);
    if (dirPath.length() == 0) dirPath = "/";

    long start = server->arg("start").toInt();
    long count = server->arg("count").toInt();
    if (start < 0) start = 0;
    if (count < 1 || count > 64) count = 24;

    DIR* d = opendir(fsPath(s, dirPath).c_str());
    if (!d) { sendError(404, "dir not found"); return; }

    beginChunked("application/json", "{\"items\":[");

    String chunk;
    chunk.reserve(900);
    bool firstItem = true;
    long idx = 0;      // index of the just-read (filtered) entry
    long sent = 0;     // how many entries emitted in this batch
    long more = -1;    // index of the next entry to resume from, or -1
    struct dirent* e;

    while (true) {
        if (sent >= count) { more = idx; break; }   // next unread index = idx (nothing wasted)
        e = readdir(d);
        if (!e) { more = -1; break; }
        if (isDotName(e->d_name)) continue;         // same filter as /api/files

        if (idx < start) { idx++; continue; }       // fast-forward (readdir only, no stat)

        String name = String(e->d_name);
        String childPath = childJoin(dirPath, name);
        struct stat st;
        if (stat(fsPath(s, childPath).c_str(), &st) == 0) {
            if (!firstItem) chunk += ',';
            firstItem = false;
            chunk += "{\"name\":\"" + jsonEscape(name) + "\",\"dir\":";
            if (S_ISDIR(st.st_mode)) {
                long kids = countChildren(fsPath(s, childPath));
                chunk += "true,\"children\":" + String(kids < 0 ? 0 : kids);
            } else {
                chunk += "false,\"size\":" + String((uint32_t)st.st_size);
            }
            chunk += "}";
        } else {
            // vanished between listing and sizing — tell the client to clear its placeholder
            if (!firstItem) chunk += ',';
            firstItem = false;
            chunk += "{\"name\":\"" + jsonEscape(name) + "\",\"err\":true}";
        }

        idx++;
        sent++;
        if (chunk.length() >= 768) { server->sendContent(chunk); chunk = ""; }
    }
    closedir(d);

    chunk += "],\"more\":" + String(more) + "}";
    server->sendContent(chunk);
}

void handleApiCardsList() {
    String json = "{\"cards\":[";
    for (int i = 0; i < storage::count(); i++) {
        Storage* s = storage::get(i);
        if (i) json += ',';
        json += "{\"index\":" + String(i) + ",\"name\":\"" + jsonEscape(s->def().name) + "\",\"ready\":";
        json += s->ready() ? "true" : "false";
        if (s->ready()) {
            uint64_t tot = s->fs().totalBytes();
            uint64_t used = s->fs().usedBytes();
            json += ",\"totalBytes\":" + String((uint32_t)tot);
            json += ",\"usedBytes\":" + String((uint32_t)used);
            json += ",\"freeBytes\":" + String((uint32_t)(tot - used));
            json += ",\"type\":" + String((int)s->fs().cardType());
        }
        json += "}";
    }
    json += "]}";
    Serial.println("[API] /api/cards");
    Serial.flush();
    server->send(200, "application/json", json);
}

void handleRoot() {
    server->send_P(200, "text/html", INDEX_HTML);
}

void handleApiFileInfo() {
    int card = parseCard();
    if (card < 0 || card >= storage::count()) { sendError(404, "card not found"); return; }
    Storage* s = storage::get(card);
    if (!s->ready()) { sendError(503, "storage not ready"); return; }

    String path = urlDecode(server->arg("path"));
    if (path.length() == 0 || !validPath(path)) { sendError(400, "invalid path"); return; }

    fs::File f = s->fs().open(path);
    if (!f) { sendError(404, "not found"); return; }

    bool isDir = f.isDirectory();
    uint32_t st_size = isDir ? 0 : f.size();
    uint32_t n_children = 0;
    if (isDir) n_children = (uint32_t)countChildren(fsPath(s, path));  // plain readdir pass
    f.close();

    uint64_t total = s->fs().totalBytes();
    uint64_t used = s->fs().usedBytes();
    uint64_t freeb = total - used;

    String json = String("{\"card\":") + String(card) +
        ",\"cardName\":\"" + jsonEscape(s->def().name) + "\"" +
        ",\"path\":\"" + jsonEscape(path) + "\"" +
        ",\"name\":\"" + jsonEscape(baseNameOf(path)) + "\"" +
        ",\"dir\":" + (isDir ? "true" : "false") +
        ",\"size\":" + String(st_size) +
        ",\"children\":" + String(n_children) +
        ",\"totalBytes\":" + String((uint32_t)total) +
        ",\"usedBytes\":" + String((uint32_t)used) +
        ",\"freeBytes\":" + String((uint32_t)freeb) +
        "}";
    Serial.println("[API] /api/file/info");
    Serial.flush();
    server->send(200, "application/json", json);
}

// ---- /api/append: append line(s) to a log file, API-only ----
// POST /api/append?card=N&path=/dir/file.log
// Body (text/plain — WebServer stores it in arg "plain"): the line(s) as sent
// by the client ESP, appended verbatim. A newline is ensured at the end, so
// each request reads back as complete line(s); multi-line bodies work too.
// Missing parent directories are created automatically.
void handleApiAppendLine() {
    int card = parseCard();
    if (card < 0 || card >= storage::count()) { sendError(404, "card not found"); return; }
    Storage* s = storage::get(card);
    if (!s->ready()) { sendError(503, "storage not ready"); return; }

    String path = urlDecode(server->arg("path"));
    if (path.length() == 0 || !validPath(path)) { sendError(400, "invalid path"); return; }

    String body = server->arg("plain");  // raw POST body (uint8 as-is; NULL bytes not supported)
    if (body.length() == 0) { sendError(400, "empty body (send text/plain)"); return; }

    ensureParentDirs(s, path);

    fs::File f = s->fs().open(path, FILE_APPEND);
    if (!f) { sendError(500, "open failed"); return; }

    size_t w = f.write((const uint8_t*)body.c_str(), body.length());
    if (w == body.length() && !body.endsWith("\n")) {
        w += f.write((const uint8_t*)"\n", 1);
    }
    uint32_t sizeAfter = (uint32_t)f.size();
    f.close();

    if (w == 0 || w < body.length()) { sendError(500, "short write"); return; }

    Serial.printf("[APPEND] %s: +%u bytes (file size %u)\n", path.c_str(), (unsigned)body.length(), (unsigned)sizeAfter);
    Serial.flush();
    server->send(200, "application/json",
        "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\",\"bytes\":" + String((uint32_t)w) +
        ",\"size\":" + String(sizeAfter) + "}");
}

// ---- /api/delete: remove a file or an EMPTY folder (API + web UI) ----
// GET /api/delete?card=N&path=/file.txt
void handleApiDelete() {
    int card = parseCard();
    if (card < 0 || card >= storage::count()) { sendError(404, "card not found"); return; }
    Storage* s = storage::get(card);
    if (!s->ready()) { sendError(503, "storage not ready"); return; }

    String path = urlDecode(server->arg("path"));
    if (path.length() == 0 || !validPath(path)) { sendError(400, "invalid path"); return; }
    if (path.length() <= 1) { sendError(400, "cannot delete root"); return; }

    fs::File f = s->fs().open(path);
    if (!f) { sendError(404, "not found"); return; }
    bool isDir = f.isDirectory();
    f.close();

    bool ok = isDir ? s->fs().rmdir(path) : s->fs().remove(path);  // rmdir fails on non-empty dir
    if (!ok) {
        sendError(500, isDir ? "folder not empty (or delete failed)" : "delete failed");
        return;
    }
    Serial.printf("[DELETE] %s%s\n", path.c_str(), isDir ? "/ (dir)" : "");
    Serial.flush();
    server->send(200, "application/json",
        "{\"ok\":true,\"path\":\"" + jsonEscape(path) + "\",\"" + (isDir ? "type\":\"dir\"" : "type\":\"file\"") + "}");
}

void handleDownload() {
    int card = parseCard();
    if (card < 0 || card >= storage::count()) { sendError(404, "card not found"); return; }
    Storage* s = storage::get(card);
    if (!s->ready()) { sendError(503, "storage not ready"); return; }

    String path = urlDecode(server->arg("path"));   // browser already encoded once — decode once
    if (path.length() == 0 || !validPath(path)) { sendError(400, "invalid path"); return; }

    fs::File f = s->fs().open(path);
    if (!f || f.isDirectory()) { sendError(404, "file not found"); return; }

    String name = baseNameOf(f.name());
    name.replace("\"", "'");  // keep the header well-formed

    // Do NOT call server->send() and do NOT set Content-Length manually here:
    // streamFile() emits a single header block (Content-Type + Content-Length=
    // file size) and streams the body in 1360-byte chunks.
    // The old code sent one complete header block with Content-Length: 0 and
    // then streamFile() emitted a SECOND block mid-stream -> browsers refused
    // the download / saved empty files.
    server->sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");

    uint32_t t0 = millis();
    size_t total = (size_t)f.size();
    size_t sent = server->streamFile(f, "application/octet-stream");
    f.close();
    Serial.printf("[DL] %s: sent %u/%u bytes in %u ms\n", name.c_str(),
                  (unsigned)sent, (unsigned)total, (unsigned)(millis() - t0));
    Serial.flush();
}

void handleUploadData() {
    HTTPUpload& u = server->upload();

    if (u.status == UPLOAD_FILE_START) {
        int card = parseCard();
        if (card < 0 || card >= storage::count()) { uploadCard = -2; return; }
        Storage* s = storage::get(card);
        if (!s || !s->ready()) { uploadCard = -2; return; }

        String name = "/" + u.filename;
        Serial.printf("[UPLOAD] %s: start %s\n", s->def().name, name.c_str());
        Serial.flush();
        logging::put(String("[UPLOAD] ") + s->def().name + ": start " + name);

        uploadCard = card;
        uploadFile = s->fs().open(name, FILE_WRITE);
        if (!uploadFile) uploadCard = -3;
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (uploadFile && uploadCard >= 0) {
            size_t w = uploadFile.write(u.buf, u.currentSize);
            if (w != (size_t)u.currentSize) {
                logging::put(String("[UPLOAD] short write ") + String((unsigned)w) + "/" + String((unsigned)u.currentSize));
            }
        }
    } else if (u.status == UPLOAD_FILE_END) {
        uint32_t sz = 0;
        if (uploadFile) { sz = uploadFile.size(); uploadFile.close(); }
        Serial.printf("[UPLOAD] done, %u bytes\n", (unsigned)sz);
        Serial.flush();
        uploadCard = -1;
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        Serial.println("[UPLOAD] aborted");
        Serial.flush();
        if (uploadFile) { uploadFile.close(); }
        uploadCard = -1;
    }
}

void handleUploadEnd() {
    if (uploadFile) uploadFile.close();
    if (uploadCard == -2) { sendError(503, "storage not ready"); uploadCard = -1; return; }
    if (uploadCard == -3) { sendError(500, "file open failed"); uploadCard = -1; return; }
    uploadCard = -1;
    server->send(200, "application/json", "{\"ok\":true,\"message\":\"upload complete\"}");
}

} // namespace

namespace web {

void begin() {
    static WebServer s(80);
    server = &s;
    s.on("/", HTTP_GET, handleRoot);
    s.on("/api/files", HTTP_GET, handleApiFiles);
    s.on("/api/sizes", HTTP_GET, handleApiSizes);
    s.on("/api/file/info", HTTP_GET, handleApiFileInfo);
    s.on("/api/cards", HTTP_GET, handleApiCardsList);
    s.on("/api/append", HTTP_POST, handleApiAppendLine);
    s.on("/api/delete", HTTP_GET, handleApiDelete);
    s.on("/download", HTTP_GET, handleDownload);
    s.on("/upload", HTTP_POST, handleUploadEnd, handleUploadData);
    s.onNotFound(handleRoot);
    s.begin();
    Serial.println("[WEB] HTTP server started on port 80");
    Serial.flush();
}

void handle() {
    if (server) server->handleClient();
}

} // namespace web
