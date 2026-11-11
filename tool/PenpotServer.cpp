#include "PenpotServer.h"

#include <mkcontract/Json.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kBad = INVALID_SOCKET;
static void closeSocket(socket_t s) { closesocket(s); }
static bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void nonBlocking(socket_t s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kBad = -1;
static void closeSocket(socket_t s) { close(s); }
static bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void nonBlocking(socket_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace mkui::tool {
namespace {

constexpr unsigned short kPort = 27240;         // 10 ports from here (the engine editor's live server starts at 27220)
constexpr size_t kMaxRequest = 64u << 20;       // exports carry baked PNGs and the Penpot source
constexpr const char* kImportMagic = "MEALKIT-IMPORT 1";
constexpr const char* kExportedHeader = "# MealKit UI exported from Penpot";

using mkcontract::jsonList;
using mkcontract::jsonString;
namespace fs = std::filesystem;

fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string utf8Of(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

bool readFile(const fs::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool writeFile(const fs::path& p, const std::string& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    out << data;
    return static_cast<bool>(out);
}

std::vector<std::string> listFiles(const fs::path& dir, const char* ext) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ext) out.push_back(utf8Of(e.path().filename()));
    std::sort(out.begin(), out.end());
    return out;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Received file paths: relative, letters/digits/_-./, no ".."
bool safePath(const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find("..") != std::string::npos || p.find("//") != std::string::npos) return false;
    for (const char c : p)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/'))
            return false;
    return true;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        pos = end + 1;
    }
    return lines;
}

// Replaces or adds "color name value" / "number name value" lines by name (other hand-written tokens are left)
std::string mergeTheme(const std::string& existing, const std::string& incoming) {
    std::vector<std::string> lines = splitLines(existing);
    auto keyOf = [](const std::string& line) {
        const size_t a = line.find(' ');
        if (a == std::string::npos) return std::string();
        const size_t b = line.find(' ', a + 1);
        const std::string kind = line.substr(0, a);
        if (kind != "color" && kind != "number") return std::string();
        return line.substr(0, b == std::string::npos ? line.size() : b);
    };
    bool header = lines.empty(); // new file: the header/format lines before the first token too
    for (const std::string& line : splitLines(incoming)) {
        const std::string key = keyOf(line);
        if (key.empty()) {
            if (header && !line.empty()) lines.push_back(line);
            continue;
        }
        header = false;
        bool replaced = false;
        for (std::string& l : lines)
            if (keyOf(l) == key) l = line, replaced = true;
        if (!replaced) lines.push_back(line);
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

// Origin from which Penpot calls the plugin (https://*.penpot.app, localhost for self-hosted Penpot)
bool penpotOrigin(const std::string& origin) {
    const std::string o = lower(origin);
    if (startsWith(o, "https://")) {
        const std::string host = o.substr(8);
        if (host == "penpot.app" || endsWith(host, ".penpot.app")) return true;
    }
    for (const char* local : {"http://localhost", "http://127.0.0.1", "https://localhost"})
        if (o == local || startsWith(o, std::string(local) + ":")) return true;
    return false;
}

const char* reasonOf(int status) {
    switch (status) {
    case 200: return "OK";
    case 204: return "No Content";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    default: return "Bad Request";
    }
}

PenpotServer::Response error(int status, const std::string& message) {
    PenpotServer::Response r;
    r.status = status;
    r.body = "{\"error\":" + jsonString(message) + "}";
    return r;
}

} // namespace

struct PenpotServer::Connection {
    socket_t socket = kBad;
    std::string in, out;
    size_t sent = 0;
    bool responded = false;
    int idle = 0;
};

PenpotServer::PenpotServer() : m_listener(static_cast<unsigned long long>(kBad)) {}
PenpotServer::~PenpotServer() { stop(); }

bool PenpotServer::running() const { return static_cast<socket_t>(m_listener) != kBad; }

std::string PenpotServer::origin() const { return running() ? "http://127.0.0.1:" + std::to_string(m_port) : std::string(); }

std::string PenpotServer::pluginUrl() const { return running() ? origin() + "/" + m_project.token + "/penpot/manifest.json" : std::string(); }

bool PenpotServer::start(const PenpotProject& project) {
    stop();
    m_project = project;
    if (m_project.token.empty()) {
        char buf[33];
        std::random_device rd;
        std::snprintf(buf, sizeof buf, "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
        m_project.token = buf;
    }
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    for (unsigned short port = kPort; port < kPort + 10; ++port) {
        const socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == kBad) break;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if defined(_WIN32)
        BOOL exclusive = TRUE; // another process can't take the port from under us
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof exclusive);
#endif
        if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 && listen(s, 16) == 0) {
            nonBlocking(s);
            m_listener = static_cast<unsigned long long>(s);
            m_port = port;
            return true;
        }
        closeSocket(s);
    }
#if defined(_WIN32)
    WSACleanup();
#endif
    return false;
}

void PenpotServer::stop() {
    for (Connection* c : m_connections) {
        closeSocket(c->socket);
        delete c;
    }
    m_connections.clear();
    if (!running()) return;
    closeSocket(static_cast<socket_t>(m_listener));
    m_listener = static_cast<unsigned long long>(kBad);
#if defined(_WIN32)
    WSACleanup();
#endif
}

std::vector<std::string> PenpotServer::takeEvents() {
    std::vector<std::string> out;
    out.swap(m_events);
    return out;
}

void PenpotServer::poll() {
    if (!running()) return;
    for (int i = 0; i < 8; ++i) {
        const socket_t s = accept(static_cast<socket_t>(m_listener), nullptr, nullptr);
        if (s == kBad) break;
        nonBlocking(s);
        Connection* c = new Connection;
        c->socket = s;
        m_connections.push_back(c);
    }
    char buffer[65536];
    for (Connection* c : m_connections) {
        ++c->idle;
        bool closed = false;
        if (!c->responded) {
            for (;;) {
                const int got = static_cast<int>(recv(c->socket, buffer, sizeof buffer, 0));
                if (got > 0) {
                    c->in.append(buffer, static_cast<size_t>(got));
                    c->idle = 0;
                    if (c->in.size() > kMaxRequest) closed = true;
                    if (closed) break;
                    continue;
                }
                if (got == 0 || !wouldBlock()) closed = true;
                break;
            }
            const size_t headerEnd = c->in.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                std::string method, path, origin;
                bool privateNetwork = false;
                size_t length = 0;
                const std::vector<std::string> lines = splitLines(c->in.substr(0, headerEnd));
                for (size_t i = 0; i < lines.size(); ++i) {
                    const std::string& line = lines[i];
                    if (i == 0) {
                        const size_t a = line.find(' '), b = line.find(' ', a + 1);
                        if (a != std::string::npos && b != std::string::npos) method = line.substr(0, a), path = line.substr(a + 1, b - a - 1);
                        continue;
                    }
                    const size_t colon = line.find(':');
                    if (colon == std::string::npos) continue;
                    const std::string name = lower(line.substr(0, colon));
                    std::string value = line.substr(colon + 1);
                    while (!value.empty() && value.front() == ' ') value.erase(0, 1);
                    if (name == "content-length") length = static_cast<size_t>(std::strtoull(value.c_str(), nullptr, 10));
                    else if (name == "origin") origin = value;
                    else if (name == "access-control-request-private-network") privateNetwork = lower(value) == "true";
                }
                if (length > kMaxRequest) closed = true;
                else if (c->in.size() >= headerEnd + 4 + length) {
                    const std::string body = c->in.substr(headerEnd + 4, length);
                    c->in.clear();
                    c->responded = true;
                    closed = false; // the client may half-close after sending
                    std::string allow;
                    const Response r = handle(method, path, origin, body, &allow);
                    std::string head = "HTTP/1.1 " + std::to_string(r.status) + " " + reasonOf(r.status) + "\r\n";
                    head += "Content-Type: " + r.contentType + "\r\nContent-Length: " + std::to_string(r.body.size()) + "\r\n";
                    head += "Cache-Control: no-store\r\nConnection: close\r\n";
                    if (!allow.empty()) {
                        head += "Access-Control-Allow-Origin: " + allow + "\r\nVary: Origin\r\n";
                        head += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\n";
                        if (privateNetwork) head += "Access-Control-Allow-Private-Network: true\r\n"; // Chrome private network access
                    }
                    c->out = head + "\r\n" + r.body;
                }
            }
        }
        if (c->responded && !closed) {
            while (c->sent < c->out.size()) {
                const int chunk = static_cast<int>(std::min<size_t>(c->out.size() - c->sent, 1u << 20));
                const int n = static_cast<int>(send(c->socket, c->out.data() + c->sent, chunk, 0));
                if (n > 0) {
                    c->sent += static_cast<size_t>(n);
                    c->idle = 0;
                    continue;
                }
                if (!wouldBlock()) closed = true;
                break;
            }
            if (c->sent >= c->out.size()) closed = true;
        }
        if (closed || c->idle > 1800) {
            closeSocket(c->socket);
            c->socket = kBad;
        }
    }
    m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
                                       [](Connection* c) {
                                           if (c->socket != kBad) return false;
                                           delete c;
                                           return true;
                                       }),
                        m_connections.end());
}

PenpotServer::Response PenpotServer::handle(const std::string& method, const std::string& fullPath, const std::string& from, const std::string& body,
                                            std::string* allow) {
    const size_t q = fullPath.find('?');
    std::string path = fullPath.substr(0, q);
    const std::string prefix = "/" + m_project.token + "/penpot";
    if (m_project.token.empty() || !(path == prefix || startsWith(path, prefix + "/"))) return error(404, "not found");
    path = path.size() > prefix.size() ? path.substr(prefix.size() + 1) : std::string();
    if (!from.empty() && lower(from) != origin() && !penpotOrigin(from)) return error(403, "origin not allowed");
    if (allow && !from.empty()) *allow = from;
    if (method == "OPTIONS") { // CORS preflight
        Response r;
        r.status = 204;
        return r;
    }
    if (method == "GET" && path == "api/project") return project();
    if (method == "POST" && path == "api/export") {
        if (lower(from) != origin()) return error(403, "export must come from the plugin panel"); // only this server's panel writes
        return importFiles(body);
    }
    if (method == "GET") return serveFile(path.empty() ? "ui.html" : path);
    return error(404, "not found");
}

PenpotServer::Response PenpotServer::serveFile(const std::string& name) const {
    static const std::pair<const char*, const char*> files[] = {
        {"manifest.json", "application/json"},
        {"plugin.js", "text/javascript"},
        {"ui.html", "text/html; charset=utf-8"},
        {"icon.png", "image/png"},
    };
    for (const auto& [file, type] : files) {
        if (name != file) continue;
        Response r;
        if (!readFile(m_project.pluginDir / file, r.body)) break;
        r.contentType = type;
        // Plugin code/icon/panel addresses are this server's token paths (doesn't rely on where Penpot resolves relative addresses)
        const std::string base = origin() + "/" + m_project.token + "/penpot/";
        for (size_t at = r.body.find("__MEALKIT_BASE__"); at != std::string::npos; at = r.body.find("__MEALKIT_BASE__", at + base.size()))
            r.body.replace(at, 16, base);
        // Penpot reads the icon as "plugin origin + / + value": a path relative to the origin
        const std::string path = m_project.token + "/penpot/";
        for (size_t at = r.body.find("__MEALKIT_PATH__"); at != std::string::npos; at = r.body.find("__MEALKIT_PATH__", at + path.size()))
            r.body.replace(at, 16, path);
        return r;
    }
    return error(404, "not found");
}

PenpotServer::Response PenpotServer::project() const {
    std::vector<std::string> fonts;
    for (const char* ext : {".ttf", ".otf"})
        for (const std::string& f : listFiles(m_project.dir / "assets" / "fonts", ext)) fonts.push_back(f.substr(0, f.size() - 4));
    // Actions: those used in .ui files now (names the game code receives)
    std::vector<std::string> actions, uiFiles;
    const fs::path uiDir = m_project.dir / "assets" / "ui";
    for (const std::string& f : listFiles(uiDir, ".ui")) {
        uiFiles.push_back(f);
        std::string text;
        if (!readFile(uiDir / pathOf(f), text)) continue;
        for (std::string line : splitLines(text)) {
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            for (const std::string k : {"button ", "back "})
                if (startsWith(line, k)) {
                    const std::string a = line.substr(k.size());
                    if (!a.empty() && a.find(' ') == std::string::npos && std::find(actions.begin(), actions.end(), a) == actions.end()) actions.push_back(a);
                }
        }
    }
    std::sort(actions.begin(), actions.end());
    Response r; // no view models: the Designer has no game code (DECISIONS 2026-11-12)
    r.body = "{\"name\":" + jsonString(m_project.name) + ",\"language\":" + jsonString(m_project.language) + ",\"fonts\":" + jsonList(fonts) +
             ",\"actions\":" + jsonList(actions) + ",\"uiFiles\":" + jsonList(uiFiles) + ",\"viewModels\":[]}";
    return r;
}

PenpotServer::Response PenpotServer::importFiles(const std::string& body) {
    std::vector<std::string> written, errors, warnings, cleanDirs;
    size_t pos = body.find('\n');
    if (pos == std::string::npos || body.compare(0, pos, kImportMagic) != 0) {
        Response r;
        r.status = 400;
        r.body = "{\"ok\":false,\"errors\":[\"bad request (expected " + std::string(kImportMagic) + ")\"]}";
        return r;
    }
    ++pos;
    const fs::path& root = m_project.dir;
    auto rel = [&](const fs::path& p) { return utf8Of(fs::relative(p, root)); };
    while (pos < body.size()) {
        const size_t lineEnd = body.find('\n', pos);
        if (lineEnd == std::string::npos) break;
        const std::string header = body.substr(pos, lineEnd - pos);
        pos = lineEnd + 1;
        // "<kind> <path> <size> [force]"
        std::vector<std::string> words;
        std::istringstream hs(header);
        for (std::string w; hs >> w;) words.push_back(w);
        const unsigned long long size = words.size() >= 3 ? std::strtoull(words[2].c_str(), nullptr, 10) : 0;
        if (words.size() < 3 || pos + size > body.size()) {
            errors.push_back("bad entry: " + header);
            break;
        }
        const std::string data = body.substr(pos, static_cast<size_t>(size));
        pos += static_cast<size_t>(size);
        if (pos < body.size() && body[pos] == '\n') ++pos;
        const std::string& k = words[0];
        const std::string& p = words[1];
        const bool force = words.size() >= 4 && words[3] == "force";
        if (k != "strings" && !safePath(p)) {
            errors.push_back("unsafe path: " + p);
            continue;
        }
        fs::path target;
        if (k == "ui" && startsWith(p, "ui/") && endsWith(p, ".ui") && p != "ui/theme.ui") {
            target = root / "assets" / pathOf(p);
            std::string old;
            if (!force && readFile(target, old) && !startsWith(old, kExportedHeader)) {
                errors.push_back("assets/" + p + " is hand-written; export again with \"Replace hand-written files\" to overwrite it");
                continue;
            }
        } else if (k == "png" && startsWith(p, "ui/") && endsWith(p, ".png")) {
            target = root / "assets" / pathOf(p);
        } else if (k == "clean" && startsWith(p, "ui/") && endsWith(p, "/")) {
            cleanDirs.push_back(p);
            continue;
        } else if (k == "theme" && p == "ui/theme.ui") {
            target = root / "assets" / pathOf(p);
            std::string old;
            readFile(target, old);
            const std::string merged = mergeTheme(old, data);
            if (merged == old) continue;
            if (writeFile(target, merged)) written.push_back("assets/" + p);
            else errors.push_back("cannot write assets/" + p);
            continue;
        } else if (k == "strings") {
            // Adds only keys missing from the project language's table (existing keys win; warns if different)
            target = root / "assets" / "strings" / pathOf(m_project.language + ".txt");
            std::string text;
            readFile(target, text);
            std::vector<std::pair<std::string, std::string>> existing;
            for (const std::string& line : splitLines(text)) {
                const size_t eq = line.find(" = ");
                if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
                existing.emplace_back(line.substr(0, eq), line.substr(eq + 3));
            }
            std::string added;
            for (const std::string& line : splitLines(data)) {
                const size_t eq = line.find(" = ");
                if (eq == std::string::npos) continue;
                const std::string key = line.substr(0, eq), value = line.substr(eq + 3);
                const auto it = std::find_if(existing.begin(), existing.end(), [&](const auto& e) { return e.first == key; });
                if (it == existing.end()) added += line + "\n";
                else if (it->second != value) warnings.push_back("string " + key + ": Penpot has \"" + value + "\", the table has \"" + it->second + "\" (kept the table)");
            }
            if (added.empty()) continue;
            if (!text.empty() && text.back() != '\n') text += "\n";
            text += "\n# From Penpot\n" + added;
            if (writeFile(target, text)) written.push_back("assets/strings/" + m_project.language + ".txt");
            else errors.push_back("cannot write the string table");
            continue;
        } else if (k == "penpot" && endsWith(p, ".penpot") && p.find('/') == std::string::npos) {
            target = root / "art" / "ui" / pathOf(p);
        } else if (k == "boxes" && endsWith(p, ".boxes") && p.find('/') == std::string::npos) {
            target = root / "art" / "ui" / "boxes" / pathOf(p);
        } else if (k == "preview" && endsWith(p, ".png") && p.find('/') == std::string::npos) {
            target = root / "art" / "ui" / "preview" / pathOf(p); // pixel comparison (secondary check)
        } else {
            errors.push_back("not allowed: " + k + " " + p);
            continue;
        }
        if (writeFile(target, data)) written.push_back(rel(target));
        else errors.push_back("cannot write " + rel(target));
    }
    // Old baked PNGs not written this time (screen folder assets/ui/<screen>/) go
    for (const std::string& dir : cleanDirs)
        for (const std::string& f : listFiles(root / "assets" / pathOf(dir), ".png")) {
            const std::string r = "assets/" + dir + f;
            std::error_code ec;
            if (std::find(written.begin(), written.end(), r) == written.end() && fs::remove(root / pathOf(r), ec)) written.push_back(r + " (removed)");
        }
    char line[160];
    std::snprintf(line, sizeof line, "Penpot: wrote %u file(s), %u error(s), %u warning(s)", static_cast<unsigned>(written.size()),
                  static_cast<unsigned>(errors.size()), static_cast<unsigned>(warnings.size()));
    m_events.push_back(line);
    for (const std::string& e : errors) m_events.push_back("Penpot import: " + e);
    Response r;
    r.body = "{\"ok\":" + std::string(errors.empty() ? "true" : "false") + ",\"written\":" + jsonList(written) + ",\"errors\":" + jsonList(errors) +
             ",\"warnings\":" + jsonList(warnings) + "}";
    return r;
}

} // namespace mkui::tool
