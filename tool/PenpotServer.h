#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Penpot plugin import server of the MkUI Designer (DECISIONS 2026-11-12 "The MkUI Designer serves the Penpot plugin"), on 127.0.0.1:
//   GET  /<token>/penpot/manifest.json, plugin.js, ui.html, icon.png   plugin files (penpot-plugin/dist, copied next to the exe as penpot/)
//   GET  /<token>/penpot/api/project                                   project name, language, fonts, actions used in .ui, screens
//   POST /<token>/penpot/api/export                                    writes the exported .ui/PNG/strings/theme/Penpot source/boxes
// Security as before in the editor: local address only, a random token in every path (kept per project in the user's MkUI folder, so
// the plugin's install address stays the same), browser Origins only from Penpot (https://*.penpot.app, localhost) or this server, and
// exports only from this server's own panel. Written paths: .ui/.png under assets/ui/, art/ui/ sources, boxes and previews, and the
// project language's string table. Non-blocking: poll() every frame on the main thread.
namespace mkui::tool {

struct PenpotProject {
    std::filesystem::path dir;       // project folder
    std::string name;                // shown in the plugin panel
    std::string language;            // string table that gets new keys (assets/strings/<language>.txt)
    std::filesystem::path pluginDir; // plugin files (manifest.json ...)
    std::string token;               // empty: random
};

class PenpotServer {
public:
    PenpotServer();
    ~PenpotServer();
    PenpotServer(const PenpotServer&) = delete;
    PenpotServer& operator=(const PenpotServer&) = delete;

    bool start(const PenpotProject& project); // false if no local port could be opened
    void stop();
    bool running() const;
    std::string pluginUrl() const; // http://127.0.0.1:<port>/<token>/penpot/manifest.json
    void poll();                   // accept, read, answer
    std::vector<std::string> takeEvents(); // one line per finished export ("Penpot: wrote 5 file(s) ...")

    struct Response {
        int status = 200;
        std::string contentType = "application/json";
        std::string body;
    };
    // One request (public so it can be tested without sockets). path is the whole /<token>/...; allow gets the Origin to echo
    Response handle(const std::string& method, const std::string& path, const std::string& origin, const std::string& body, std::string* allow);

private:
    Response serveFile(const std::string& name) const;
    Response project() const;
    Response importFiles(const std::string& body);
    std::string origin() const;

    struct Connection;
    PenpotProject m_project;
    std::vector<Connection*> m_connections;
    std::vector<std::string> m_events;
    unsigned long long m_listener;
    unsigned short m_port = 0;
};

} // namespace mkui::tool
