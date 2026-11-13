#include "App.h"
#include "Strings.h"

#include <mkcontract/User.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mkui::tool {

namespace {

// Panel tabs (engine docs/TOOL_DESIGN.md section 2 "Panels")
constexpr const char* kIconFolder = "\xEE\xA2\xB7";    // U+E8B7 Files
constexpr const char* kIconLibrary = "\xEE\xA3\xB1";   // U+E8F1
constexpr const char* kIconTree = "\xEE\xA3\xBD";      // U+E8FD
constexpr const char* kIconInspector = "\xEE\xA7\xA9"; // U+E9E9 Properties
constexpr const char* kIconData = "\xEE\xA3\xAF";      // U+E8EF Values
constexpr const char* kIconCode = "\xEE\xA5\x83";      // U+E943 Source
constexpr const char* kIconWarning = "\xEE\x9E\xBA";   // U+E7BA Problems
constexpr const char* kIconPreview = "\xEE\x9F\xB4";   // U+E7F4
constexpr const char* kIconLanguage = "\xEF\x8A\xB7";  // U+F2B7 LocaleLanguage

// Checkbox and radio colors (engine docs/TOOL_DESIGN.md 3.1, 3.2): a gray 4 box on a gray 2 panel is about 1.25:1, so an unchecked
// box gets a gray 9 outline (gray 11 hovered) and a checked one the accent fill with a gray 12 mark. Same as MkRender tool/App.cpp
const ImVec4 kControlOutline(0x8A / 255.0f, 0x8A / 255.0f, 0x8A / 255.0f, 1.0f);      // gray 9
const ImVec4 kControlOutlineHover(0xAF / 255.0f, 0xAF / 255.0f, 0xAF / 255.0f, 1.0f); // gray 11
const ImVec4 kControlMark(0xEB / 255.0f, 0xEB / 255.0f, 0xEB / 255.0f, 1.0f);         // gray 12
const ImVec4 kAccent(0x3B / 255.0f, 0x63 / 255.0f, 0xFB / 255.0f, 1.0f);
const ImVec4 kAccentHover(0x4B / 255.0f, 0x75 / 255.0f, 0xFF / 255.0f, 1.0f);
const ImVec4 kAccentDown(0x27 / 255.0f, 0x4D / 255.0f, 0xEA / 255.0f, 1.0f);

// Pushes the fill and mark of a checkbox or radio button in the state it is drawn in; popControlColors after
void pushControlColors(bool on) {
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    const ImGuiStyle& s = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, on ? kAccent : clear);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, on ? kAccentHover : s.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, on ? kAccentDown : s.Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kControlMark);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.FrameRounding * 0.5f); // a 4 px box, not the round 8 px of fields
}

void popControlColors() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

// Every checkbox goes through here, never ImGui::Checkbox (engine docs/TOOL_DESIGN.md 3.2)
bool checkbox(const char* label, bool* v) {
    const bool on = *v;
    pushControlColors(on);
    const bool changed = ImGui::Checkbox(label, v);
    const float rounding = ImGui::GetStyle().FrameRounding;
    popControlColors();
    if (!on) {
        // The box is the square at the start of the item (ImGui::Checkbox check_bb)
        const ImVec2 p = ImGui::GetItemRectMin();
        const float size = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
        const float inset = width * 0.5f;
        ImGui::GetWindowDrawList()->AddRect({p.x + inset, p.y + inset}, {p.x + size - inset, p.y + size - inset},
                                            ImGui::GetColorU32(ImGui::IsItemHovered() ? kControlOutlineHover : kControlOutline), rounding, 0, width);
    }
    return changed;
}

bool radioButton(const char* label, bool active) {
    pushControlColors(active);
    const bool pressed = ImGui::RadioButton(label, active);
    popControlColors();
    if (!active) {
        // Same circle as ImGui::RadioButton: radius (square - 1) / 2 around the square's center
        const ImVec2 p = ImGui::GetItemRectMin();
        const float size = ImGui::GetFrameHeight();
        const float width = std::max(1.0f, ImGui::GetStyle().FontScaleDpi);
        const ImVec2 center(std::floor(p.x + size * 0.5f + 0.5f), std::floor(p.y + size * 0.5f + 0.5f));
        ImGui::GetWindowDrawList()->AddCircle(center, (size - 1.0f) * 0.5f - width * 0.5f,
                                              ImGui::GetColorU32(ImGui::IsItemHovered() ? kControlOutlineHover : kControlOutline), 0, width);
    }
    return pressed;
}

std::filesystem::path pathOf(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string utf8Of(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool readText(const std::filesystem::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::stringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

struct Resolution {
    const char* name;
    int w, h;
};
constexpr Resolution kResolutions[] = {
    {"1920 x 1080", 1920, 1080}, {"1280 x 720", 1280, 720}, {"2560 x 1440", 2560, 1440},
    {"1280 x 800 (16:10)", 1280, 800}, {"1080 x 1920 (portrait)", 1080, 1920},
};

// Glyph SDF value → alpha. The engine's shader sharpens by the drawn size (sdfScale); a fixed ramp around 0.5 is enough here
constexpr float kGlyphRamp = 5.0f;
// Glyphs from engine docs/TOOL_DESIGN.md section 2 (Segoe Fluent Icons / MDL2 Assets)
constexpr const char* kIconAdd = "\xEE\x9C\x90";     // U+E710
constexpr const char* kIconRemove = "\xEE\x9C\xB8";  // U+E738 remove from a list
constexpr const char* kIconRefresh = "\xEE\x9C\xAC"; // U+E72C
constexpr const char* kIconCopy = "\xEE\xA3\x88";    // U+E8C8
constexpr const char* kIconEdit = "\xEE\x9C\x8F";    // U+E70F

int stringCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

bool inputMultiline(const char* label, std::string& s, ImVec2 size, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextMultiline(label, s.data(), s.capacity() + 1, size, flags | ImGuiInputTextFlags_CallbackResize,
                                     stringCallback, &s);
}

bool inputLine(const char* label, std::string& s, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, s.data(), s.capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize, stringCallback, &s);
}

void folderPicked(void* user, const char* const* list, int) {
    if (list && list[0]) static_cast<App*>(user)->setPendingProject(list[0]);
}

bool isPropertyItem(const SourceItem& item) { return item.child < 0 && !item.line.empty() && item.line[0] != '#'; }

} // namespace

// ---------------------------------------------------------------- Host

DesignerHost::DesignerHost() : glyphs(static_cast<size_t>(kAtlasSize) * kAtlasSize), images(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4) {}

bool DesignerHost::readFile(const std::string& path, std::string& out) { return readText(assets / pathOf(path), out); }

u64 DesignerHost::fileTime(const std::string& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(assets / pathOf(path), ec);
    if (ec) return 0;
    return static_cast<u64>(t.time_since_epoch().count()) | 1u;
}

std::vector<std::string> DesignerHost::listFiles(const std::string& folder, const char* extension) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(assets / pathOf(folder), ec))
        if (e.is_regular_file() && utf8Of(e.path().extension()) == extension) out.push_back(utf8Of(e.path().filename()));
    std::sort(out.begin(), out.end());
    return out;
}

void DesignerHost::log(LogLevel level, const std::string& message) {
    if (quiet) return;
    SDL_Log("%s%s", level == LogLevel::Warn ? "warn: " : "", message.c_str());
    if (level == LogLevel::Warn && std::find(warnings.begin(), warnings.end(), message) == warnings.end()) {
        if (warnings.size() >= 100) warnings.erase(warnings.begin());
        warnings.push_back(message);
    }
}

const std::string& DesignerHost::text(const std::string& key) {
    auto it = strings.find(key);
    return it == strings.end() ? key : it->second;
}

bool DesignerHost::decodeImage(const std::string& bytes, u32& width, u32& height, std::vector<u8>& rgba) {
    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!io) return false;
    SDL_Surface* loaded = SDL_LoadPNG_IO(io, true); // PNG only (the engine also reads JPEG)
    if (!loaded) return false;
    SDL_Surface* s = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!s) return false;
    width = static_cast<u32>(s->w), height = static_cast<u32>(s->h);
    rgba.resize(static_cast<size_t>(width) * height * 4);
    for (u32 y = 0; y < height; ++y)
        std::memcpy(&rgba[static_cast<size_t>(y) * width * 4], static_cast<const u8*>(s->pixels) + static_cast<size_t>(y) * s->pitch, width * 4);
    SDL_DestroySurface(s);
    return true;
}

void DesignerHost::uploadAtlas(AtlasKind atlas, u32 x, u32 y, u32 w, u32 h, const u8* pixels) {
    const int k = atlas == AtlasKind::Glyphs ? 0 : 1;
    const u32 bpp = k == 0 ? 1 : 4;
    std::vector<u8>& dst = k == 0 ? glyphs : images;
    for (u32 row = 0; row < h && y + row < kAtlasSize; ++row)
        std::memcpy(&dst[(static_cast<size_t>(y + row) * kAtlasSize + x) * bpp], pixels + static_cast<size_t>(row) * w * bpp,
                    std::min(w, kAtlasSize - x) * bpp);
    int* d = dirty[k];
    d[0] = std::min(d[0], static_cast<int>(x)), d[1] = std::min(d[1], static_cast<int>(y));
    d[2] = std::max(d[2], static_cast<int>(std::min(x + w, kAtlasSize))), d[3] = std::max(d[3], static_cast<int>(std::min(y + h, kAtlasSize)));
}

bool DesignerHost::loadStrings(const std::string& language) {
    strings.clear();
    std::string text;
    if (language.empty() || !readText(assets / "strings" / pathOf(language + ".txt"), text)) return false;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == '@') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string value = trim(line.substr(eq + 1)), unescaped;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == 'n') unescaped += '\n', ++i;
            else unescaped += value[i];
        }
        strings[trim(line.substr(0, eq))] = unescaped;
    }
    return true;
}

// ---------------------------------------------------------------- App

bool App::init(SDL_Renderer* renderer, const std::string& projectDir, const std::string& file, int resolution) {
    loadLanguage();
    if (!languageOverride.empty()) setLanguage(languageOverride);
    m_renderer = renderer;
    m_resolution = std::clamp(resolution, 0, static_cast<int>(std::size(kResolutions)) - 1);
    m_glyphTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlasSize, kAtlasSize);
    m_imageTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kAtlasSize, kAtlasSize);
    for (SDL_Texture* t : {m_glyphTexture, m_imageTexture}) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    }
    m_glyphRgba.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize * 4, 255);
    for (size_t i = 3; i < m_glyphRgba.size(); i += 4) m_glyphRgba[i] = 0;
    if (!projectDir.empty() && openProject(projectDir) && !file.empty()) openFile(file);
    return true;
}

void App::shutdown() {
    m_penpot.stop();
    if (m_ui) m_ui->closeAll();
    m_ui.reset();
    for (SDL_Texture* t : {m_target, m_glyphTexture, m_imageTexture})
        if (t) SDL_DestroyTexture(t);
}

std::string App::title() const {
    std::string t = "MkUI";
    if (!m_file.empty()) t += " - " + m_file + (m_text != m_savedText ? " *" : "");
    if (!m_project.empty()) t += " (" + utf8Of(pathOf(m_project).filename()) + ")";
    return t;
}

void App::setPendingProject(const std::string& dir) {
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_dialogProject = dir;
}

bool App::selectByName(const std::string& name) {
    if (m_source.root.children.empty()) return false;
    Path path;
    // depth first through the board
    struct Walk {
        const std::string& name;
        Path& path;
        bool operator()(const SourceNode& n) {
            if (n.name == name) return true;
            for (u32 i = 0; i < n.children.size(); ++i) {
                path.push_back(i);
                if ((*this)(n.children[i])) return true;
                path.pop_back();
            }
            return false;
        }
    } walk{name, path};
    if (!walk(m_source.root.children[0])) return false;
    m_selected = path, m_hasSelection = true;
    return true;
}

bool App::openProject(const std::string& dir) {
    std::error_code ec;
    const std::filesystem::path assets = pathOf(dir) / "assets";
    if (!std::filesystem::is_directory(assets / "ui", ec)) {
        m_status = trf("No assets/ui folder in %s", dir.c_str());
        m_statusTime = m_time;
        return false;
    }
    if (m_ui) m_ui->closeAll();
    m_ui.reset();
    m_project = dir;
    m_host.assets = assets;
    m_host.warnings.clear();
    m_file.clear(), m_text.clear(), m_savedText.clear();
    m_screen = kNoScreen;
    m_hasSelection = false;
    m_undo.clear(), m_redo.clear();
    m_languages.clear();
    for (const std::string& f : m_host.listFiles("strings", ".txt")) m_languages.push_back(f.substr(0, f.size() - 4));
    m_language = m_languages.empty() ? std::string() : m_languages.front();
    m_host.loadStrings(m_language);
    m_ui = std::make_unique<System>(m_host, valueBinder());
    scanFiles();
    startPenpot();
    return true;
}

// Penpot plugin import server for this project. The token is kept per project in the user's MkUI folder so the plugin's install
// address stays the same between runs (DECISIONS 2026-11-12 "The MkUI Designer serves the Penpot plugin")
void App::startPenpot() {
    m_penpot.stop();
    PenpotProject p;
    p.dir = pathOf(m_project);
    p.name = utf8Of(p.dir.filename());
    p.language = m_language;
    std::string settings;
    if (readText(p.dir / "mealkit.project", settings)) { // "language ko": the table new string keys go to
        std::istringstream in(settings);
        for (std::string line; std::getline(in, line);) {
            line = trim(line);
            if (line.rfind("language ", 0) == 0) p.language = trim(line.substr(9));
            if (line.rfind("name ", 0) == 0) p.name = trim(line.substr(5));
        }
    }
    const char* base = SDL_GetBasePath();
    p.pluginDir = pathOf(base ? base : "") / "penpot";
    std::filesystem::path tokens;
    if (char* pref = SDL_GetPrefPath("MealKit", "MkUI")) {
        tokens = pathOf(pref) / "penpot-tokens.txt";
        SDL_free(pref);
    }
    std::string text;
    std::vector<std::string> lines;
    if (!tokens.empty() && readText(tokens, text)) { // "<token> <project folder>"
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            line = trim(line);
            if (line.empty()) continue;
            lines.push_back(line);
            const size_t sp = line.find(' ');
            if (sp != std::string::npos && line.substr(sp + 1) == m_project) p.token = line.substr(0, sp);
        }
    }
    if (!m_penpot.start(p)) {
        m_host.warnings.push_back("Penpot plugin: no free local port (27240-27249)");
        return;
    }
    if (p.token.empty() && !tokens.empty()) { // remember the new one
        const std::string url = m_penpot.pluginUrl();
        const size_t a = url.find('/', 8) + 1;
        lines.push_back(url.substr(a, url.find('/', a) - a) + " " + m_project);
        std::ofstream out(tokens, std::ios::binary);
        for (const std::string& l : lines) out << l << '\n';
    }
}

void App::scanFiles() {
    m_files.clear();
    std::error_code ec;
    const std::filesystem::path ui = m_host.assets / "ui";
    for (auto it = std::filesystem::recursive_directory_iterator(ui, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".ui") continue;
        std::string rel = utf8Of(std::filesystem::relative(it->path(), m_host.assets, ec));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (rel != kThemeFile) m_files.push_back(rel);
    }
    std::sort(m_files.begin(), m_files.end());
}

bool App::openFile(const std::string& uiPath) {
    std::string text;
    if (!m_host.readFile(uiPath, text)) {
        m_status = trf("Cannot read %s", uiPath.c_str());
        m_statusTime = m_time;
        return false;
    }
    if (m_screen != kNoScreen) m_ui->close(m_screen);
    m_host.warnings.clear();
    m_file = uiPath;
    m_text = m_savedText = m_lastSnapshot = text;
    std::error_code ec;
    m_fileTime = std::filesystem::last_write_time(m_host.assets / pathOf(uiPath), ec);
    m_externalChange = false;
    m_undo.clear(), m_redo.clear();
    m_hasSelection = false;
    m_actions.clear();
    m_screen = m_ui->open(uiPath);
    loadValues();
    textChanged();
    return true;
}

bool App::save() {
    if (m_file.empty()) return false;
    {
        std::ofstream out(m_host.assets / pathOf(m_file), std::ios::binary);
        out << m_text;
        if (!out) {
            m_status = trf("Cannot write %s", m_file.c_str());
            m_statusTime = m_time;
            return false;
        }
    }
    m_savedText = m_text;
    std::error_code ec;
    m_fileTime = std::filesystem::last_write_time(m_host.assets / pathOf(m_file), ec);
    m_externalChange = false;
    m_ui->setDraft(m_screen, nullptr); // the file is the text again
    m_status = trf("Saved %s", m_file.c_str());
    m_statusTime = m_time;
    return true;
}

void App::reloadFromDisk() {
    const std::string file = m_file;
    openFile(file);
    m_status = trf("Reloaded %s (changed on disk)", file.c_str());
    m_statusTime = m_time;
}

void App::checkExternalChange() {
    if (m_file.empty() || m_time - m_fileCheck < 0.5) return;
    m_fileCheck = m_time;
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(m_host.assets / pathOf(m_file), ec);
    if (ec || t == m_fileTime) return;
    if (m_text == m_savedText) reloadFromDisk();
    else m_externalChange = true;
}

void App::textChanged() {
    Source parsed;
    std::string error;
    if (parseSource(m_text, parsed, &error)) {
        m_source = std::move(parsed);
        m_sourceError.clear();
    } else m_sourceError = error;
    m_host.quiet = true;
    m_doc = Document{};
    Theme theme; // number tokens ($gap) need it, as in the runtime
    loadTheme(theme, m_host);
    parseDocument(m_text, m_file, m_doc, m_host, &theme); // keeps what it read even with errors
    m_host.quiet = false;
    m_docPaths = m_sourceError.empty() ? mapDocumentNodes(m_source, m_doc) : std::vector<Path>{};
    if (m_hasSelection && !findNode(m_source, m_selected)) m_hasSelection = false;
    if (m_screen != kNoScreen) m_ui->setDraft(m_screen, m_text == m_savedText ? nullptr : &m_text);
    collectFields();
}

void App::snapshot() {
    if (!m_undo.empty() && m_undo.back() == m_text) return;
    m_undo.push_back(m_text);
    if (m_undo.size() > 200) m_undo.pop_front();
    m_redo.clear();
}

void App::sourceEdited() {
    m_text = writeSource(m_source);
    textChanged();
}

void App::undo(bool redo) {
    std::deque<std::string>& from = redo ? m_redo : m_undo;
    std::deque<std::string>& to = redo ? m_undo : m_redo;
    if (from.empty()) return;
    to.push_back(m_text);
    m_text = from.back();
    from.pop_back();
    textChanged();
}

bool App::guardUnsaved(int next) {
    if (m_text == m_savedText) return true;
    m_pendingAction = next;
    ImGui::OpenPopup("###unsaved");
    return false;
}

// ---------------------------------------------------------------- Frame

void App::frame(double dt) {
    m_time += dt;
    m_penpot.poll();
    for (const std::string& e : m_penpot.takeEvents()) { // an export finished: new screens show, the open one reloads if it changed
        m_status = e;
        m_statusTime = m_time;
        if (e.rfind("Penpot import: ", 0) == 0) m_host.warnings.push_back(e);
        else scanFiles();
    }
    {
        std::string picked;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            picked.swap(m_dialogProject);
        }
        if (!picked.empty()) {
            m_pendingProject = picked;
            if (guardUnsaved(3)) openProject(picked);
        }
    }
    if (m_quitAsked) {
        m_quitAsked = false;
        if (guardUnsaved(1)) m_quitConfirmed = true;
    }
    checkExternalChange();
    if (languageOverride.empty()) followLanguage(); // MealKit Manager or another Mk tool changed the suite language

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) undo(io.KeyShift);
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) undo(true);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) save();

    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    if (!m_layoutDone) {
        layoutDocks(dockspace);
        m_layoutDone = true;
    }
    drawMenu();
    if (m_project.empty()) drawStart();
    else {
        drawFiles();
        drawLibrary();
        drawTree();
        drawProperties();
        drawText();
        drawProblems();
        drawValues();
        drawPreview(dt);
    }
    drawUnsavedDialog();
    drawNameDialog();
    if (!m_showWindow.empty()) {
        ImGui::SetWindowFocus(("###" + m_showWindow).c_str());
        m_showWindow.clear();
    }
}

void App::layoutDocks(unsigned dockspace) {
    if (ImGui::DockBuilderGetNode(dockspace) && ImGui::DockBuilderGetNode(dockspace)->IsSplitNode() && ImGui::FindWindowSettingsByID(ImHashStr("###Files")))
        return; // saved layout (one saved before the ### panel ids is laid out again)
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);
    ImGuiID center = dockspace;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.32f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.2f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.6f, nullptr, &left);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);
    ImGui::DockBuilderDockWindow("###Files", left);
    ImGui::DockBuilderDockWindow("###Library", left);
    ImGui::DockBuilderDockWindow("###Tree", leftBottom);
    ImGui::DockBuilderDockWindow("###Properties", right);
    ImGui::DockBuilderDockWindow("###Values", right);
    ImGui::DockBuilderDockWindow("###Source", rightBottom);
    ImGui::DockBuilderDockWindow("###Problems", bottom);
    ImGui::DockBuilderDockWindow("###Preview", center);
    ImGui::DockBuilderFinish(dockspace);
}

void App::drawMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu(tr("File"))) {
        if (ImGui::MenuItem(tr("Open project folder..."))) SDL_ShowOpenFolderDialog(folderPicked, this, static_cast<SDL_Window*>(window), nullptr, false);
        if (ImGui::MenuItem(tr("New screen..."), nullptr, false, !m_project.empty())) m_nameDialog = 1;
        if (ImGui::MenuItem(tr("Save"), "Ctrl+S", false, !m_file.empty())) save();
        if (ImGui::MenuItem(tr("Reload from disk"), nullptr, false, !m_file.empty())) reloadFromDisk();
        ImGui::Separator();
        if (ImGui::MenuItem(tr("Quit"))) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(tr("Edit"))) {
        if (ImGui::MenuItem(tr("Undo"), "Ctrl+Z", false, !m_undo.empty())) undo(false);
        if (ImGui::MenuItem(tr("Redo"), "Ctrl+Y", false, !m_redo.empty())) undo(true);
        ImGui::EndMenu();
    }
    if (!m_project.empty()) {
        ImGui::SetNextItemWidth(170.0f * ImGui::GetStyle().FontScaleDpi);
        ImGui::Combo("##res", &m_resolution, [](void*, int i) { return kResolutions[i].name; }, nullptr,
                     static_cast<int>(std::size(kResolutions)));
        if (!m_languages.empty()) {
            ImGui::SetNextItemWidth(90.0f * ImGui::GetStyle().FontScaleDpi);
            if (ImGui::BeginCombo("##lang", m_language.c_str())) {
                for (const std::string& l : m_languages)
                    if (ImGui::Selectable(l.c_str(), l == m_language)) {
                        m_language = l;
                        m_host.loadStrings(l);
                        m_ui->reload();
                        textChanged(); // {field} in the new language's texts
                    }
                ImGui::EndCombo();
            }
        }
        checkbox(tr("Interact"), &m_interact);
        ImGui::SetItemTooltip(tr("On: the preview gets the mouse (buttons, sliders, inputs). Off: clicks select nodes"));
    }
    if (!m_status.empty() && m_time - m_statusTime < 6.0) ImGui::TextDisabled("  %s", m_status.c_str());
    drawLanguageButton();
    ImGui::EndMainMenuBar();
}

void App::drawLanguageButton() {
    // The name in English too while another language is on, so someone who can't read it still knows the button
    const std::string name = std::strcmp(language(), "en") == 0 ? std::string("Language") : std::string(tr("Language")) + " (Language)";
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight()); // the right end, inside the padding
    if (iconButton(kIconLanguage, name.c_str())) ImGui::OpenPopup("##language");
    if (ImGui::BeginPopup("##language")) {
        for (const Language& l : kLanguages) {
            if (ImGui::MenuItem(l.name, nullptr, std::strcmp(language(), l.code) == 0)) {
                languageOverride.clear();
                if (!saveLanguage(l.code)) {
                    setLanguage(l.code);
                    m_status = trf("Cannot save the language to %s", mkcontract::userPath(mkcontract::kEditorSettings).c_str());
                    m_statusTime = m_time;
                }
            }
        }
        ImGui::EndPopup();
    }
}

std::string App::panelTitle(const char* glyph, const char* name) const {
    return (iconFont ? std::string(glyph) + "  " : std::string()) + tr(name) + "###" + name;
}

void App::drawStart() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin((std::string(tr("Start")) + "###Start").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted((std::string(tr("MkUI Designer")) + " " MKUI_VERSION).c_str());
    ImGui::TextDisabled(tr("Edits a project's assets/ui/*.ui. The game reads the same files."));
    ImGui::Spacing();
    if (ImGui::Button(tr("Open project folder..."))) SDL_ShowOpenFolderDialog(folderPicked, this, static_cast<SDL_Window*>(window), nullptr, false);
    if (!m_status.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", m_status.c_str());
    ImGui::End();
}

bool App::iconButton(const char* glyph, const char* name, bool small) {
    const std::string label = std::string(iconFont ? glyph : name) + "##" + name;
    // Flat like the editor's icon buttons: clear until hovered
    if (iconFont) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    }
    const bool pressed = small ? ImGui::SmallButton(label.c_str()) : iconFont ? ImGui::Button(label.c_str(), {ImGui::GetFrameHeight(), 0}) : ImGui::Button(label.c_str());
    if (iconFont) ImGui::PopStyleColor(2);
    if (iconFont && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", name);
    return pressed;
}

void App::drawFiles() {
    ImGui::Begin(panelTitle(kIconFolder, "Files").c_str());
    if (iconButton(kIconAdd, tr("New screen"), true)) m_nameDialog = 1;
    ImGui::SameLine();
    if (iconButton(kIconRefresh, tr("Refresh"), true)) scanFiles();
    if (m_penpot.running()) {
        ImGui::SameLine();
        if (iconButton(kIconCopy, tr("Copy the Penpot plugin address"), true)) {
            ImGui::SetClipboardText(m_penpot.pluginUrl().c_str());
            m_status = tr("Copied the Penpot plugin address: in Penpot, Plugins > Install, paste it");
            m_statusTime = m_time;
        }
        ImGui::SetItemTooltip(tr("Copies %s\nIn Penpot: Plugins > Install. Exports from the MealKit plugin are written into this project"), m_penpot.pluginUrl().c_str());
    }
    for (const std::string& f : m_files) {
        const std::string label = f.substr(3); // without "ui/"
        if (ImGui::Selectable(label.c_str(), f == m_file) && f != m_file) {
            m_pendingFile = f;
            if (guardUnsaved(2)) openFile(f);
        }
    }
    ImGui::End();
}

void App::drawTreeNode(const SourceNode& node, Path& path) {
    const bool selected = m_hasSelection && m_selected == path;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (selected) flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(static_cast<int>(path.empty() ? -1 : static_cast<int>(path.back())));
    const bool open = ImGui::TreeNodeEx("##n", flags, "%s %s", node.kind.c_str(), node.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_selected = path, m_hasSelection = true;
    if (open) {
        for (u32 i = 0; i < node.children.size(); ++i) {
            path.push_back(i);
            drawTreeNode(node.children[i], path);
            path.pop_back();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void App::drawTree() {
    ImGui::Begin(panelTitle(kIconTree, "Tree").c_str());
    if (m_file.empty()) ImGui::TextDisabled(tr("Open a file"));
    else if (m_source.root.children.empty()) ImGui::TextDisabled(tr("No board"));
    else {
        const bool editable = m_sourceError.empty() && !m_source.exported;
        ImGui::BeginDisabled(!editable);
        const SourceNode* sel = m_hasSelection ? findNode(m_source, m_selected) : nullptr;
        ImGui::BeginDisabled(!sel || m_selected.empty());
        if (iconButton(kIconRemove, tr("Delete node"), true)) {
            snapshot();
            removeNode(m_source, m_selected);
            m_hasSelection = false;
            sourceEdited();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!sel || sel->kind != "frame" || m_selected.empty());
        if (ImGui::SmallButton(tr("Make component"))) m_nameDialog = 2;
        ImGui::SetItemTooltip(tr("Moves this frame into ui/components/<name>.ui and leaves a \"use\" here"));
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (sel && sel->kind == "use" && sel->args.rfind("ui/", 0) == 0) {
            ImGui::SameLine();
            if (iconButton(kIconEdit, tr("Edit component"), true)) {
                m_pendingFile = sel->args;
                if (guardUnsaved(2)) openFile(m_pendingFile);
            }
        }
        if (m_source.exported) ImGui::TextDisabled(tr("Penpot export: read-only"));
        ImGui::Separator();
        ImGui::BeginChild("tree");
        Path path;
        drawTreeNode(m_source.root.children[0], path);
        ImGui::EndChild();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- Inspector rows (common properties with their own widgets)

namespace {

constexpr const char* kJustify[] = {"start", "center", "end", "space-between", "space-around", "space-evenly"};
constexpr const char* kAlign[] = {"start", "center", "end", "stretch"};
constexpr const char* kTextAlign[] = {"left", "center", "right"};

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> w;
    for (size_t a = 0; (a = s.find_first_not_of(' ', a)) != std::string::npos;) {
        const size_t b = s.find(' ', a);
        w.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        a = b;
    }
    return w;
}

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", std::fabs(v) < 1e-4f ? 0.0f : v);
    return buf;
}

bool isNumber(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == 0;
}

std::vector<float> numbers(const std::string& s) {
    std::vector<float> out;
    for (const std::string& w : words(s)) {
        float v = 0;
        if (!isNumber(w, v)) return {};
        out.push_back(v);
    }
    return out;
}

// "#RRGGBB" / "#RRGGBBAA" ↔ 0..1 floats
bool parseHex(const std::string& s, float rgba[4]) {
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    for (size_t i = 1; i < s.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
    for (int i = 0; i < 4; ++i) rgba[i] = i < 3 || s.size() == 9 ? std::strtoul(s.substr(1 + i * 2, 2).c_str(), nullptr, 16) / 255.0f : 1.0f;
    return true;
}

std::string hex(const float rgba[4], bool alpha) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    if (alpha) std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", b(rgba[0]), b(rgba[1]), b(rgba[2]), b(rgba[3]));
    else std::snprintf(buf, sizeof buf, "#%02X%02X%02X", b(rgba[0]), b(rgba[1]), b(rgba[2]));
    return buf;
}

void listAssets(const std::filesystem::path& dir, const std::string& prefix, const std::vector<std::string>& exts, bool keepExt,
                std::vector<std::string>& out, int depth = 0) {
    std::error_code ec;
    if (depth > 6 || !std::filesystem::is_directory(dir, ec)) return;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = utf8Of(e.path().filename());
        if (e.is_directory(ec)) {
            listAssets(e.path(), prefix + name + "/", exts, keepExt, out, depth + 1);
            continue;
        }
        std::string ext = utf8Of(e.path().extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;
        out.push_back(prefix + (keepExt ? name : utf8Of(e.path().stem())));
    }
    std::sort(out.begin(), out.end());
}

} // namespace

// Returns true if node changed (the caller writes it back into the text once)
bool App::drawInspector(SourceNode& node) {
    bool changed = false;
    std::string value;
    auto set = [&](const char* key, const std::string& v) { setProperty(node, key, v), changed = true; };
    auto unset = [&](const char* key) { changed |= removeProperty(node, key); };
    auto combo = [&](const char* label, const char* key, const char* const* options, size_t count) {
        std::string v;
        const bool has = getProperty(node, key, v);
        if (ImGui::BeginCombo(label, has ? v.c_str() : tr("(default)"))) {
            if (ImGui::Selectable(tr("(default)"), !has) && has) unset(key);
            for (size_t i = 0; i < count; ++i)
                if (ImGui::Selectable(options[i], has && v == options[i])) set(key, options[i]);
            ImGui::EndCombo();
        }
    };
    auto number = [&](const char* label, const char* key, float fallback, float speed, float lo, float hi, const char* format) {
        std::string v;
        const bool has = getProperty(node, key, v);
        std::vector<float> n = has ? numbers(v) : std::vector<float>{fallback};
        if (n.size() != 1) {
            ImGui::TextDisabled("%s  %s %s", label, key, v.c_str()); // a $token or a binding word
            return;
        }
        if (ImGui::DragFloat(label, &n[0], speed, lo, hi, format, ImGuiSliderFlags_AlwaysClamp)) set(key, num(n[0]));
    };
    NodeBox drawn{};
    const bool hasBox = selectedBox(drawn);
    std::string parentLayout;
    bool placedByLayout = false;
    if (!m_selected.empty()) {
        const Path up(m_selected.begin(), m_selected.end() - 1);
        if (const SourceNode* p = findNode(m_source, up)) placedByLayout = getProperty(*p, "layout", parentLayout);
    }

    // ---- Layout: position, size modes, anchors
    ImGui::SeparatorText(tr("Layout"));
    if (placedByLayout) ImGui::TextDisabled(tr("Placed by the parent's layout (%s)"), words(parentLayout).empty() ? "" : words(parentLayout)[0].c_str());
    else if (node.kind != "board") {
        std::vector<float> pos = getProperty(node, "pos", value) ? numbers(value) : std::vector<float>{0.0f, 0.0f};
        if (pos.size() == 2 && ImGui::DragFloat2(tr("Position"), pos.data(), 1.0f, 0.0f, 0.0f, "%.0f")) set("pos", num(std::round(pos[0])) + " " + num(std::round(pos[1])));
    }
    if (node.kind != "board" && (node.kind != "use" || getProperty(node, "size", value))) {
        std::vector<std::string> parts = getProperty(node, "size", value) ? words(value) : std::vector<std::string>{};
        const bool text = node.kind == "text";
        if (parts.size() != 2)
            parts = {text ? "hug" : num(hasBox ? std::round(drawn.w) : 100.0f), text ? "hug" : num(hasBox ? std::round(drawn.h) : 100.0f)};
        const char* modes[] = {tr("Fixed"), tr("Fit content"), tr("Fill")};
        const char* tips[] = {tr("A set number of units"), tr("As large as its children or text (hug)"), tr("Takes the room left in the parent's layout (fill)")};
        bool sizeChanged = false;
        for (int axis = 0; axis < 2; ++axis) {
            ImGui::PushID(axis);
            std::string& p = parts[static_cast<size_t>(axis)];
            const int mode = p == "hug" ? 1 : p == "fill" ? 2 : 0;
            const float full = ImGui::CalcItemWidth();
            ImGui::SetNextItemWidth(mode == 0 ? full * 0.5f : full);
            if (ImGui::BeginCombo(mode == 0 ? "##mode" : axis == 0 ? tr("Width") : tr("Height"), modes[mode])) {
                for (int m = 0; m < 3; ++m) {
                    if (ImGui::Selectable(modes[m], m == mode) && m != mode) {
                        p = m == 1 ? "hug" : m == 2 ? "fill" : num(std::round(hasBox ? (axis == 0 ? drawn.w : drawn.h) : 100.0f));
                        sizeChanged = true;
                    }
                    ImGui::SetItemTooltip("%s", tips[m]);
                }
                ImGui::EndCombo();
            }
            if (mode == 0) {
                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                ImGui::SetNextItemWidth(full * 0.5f - ImGui::GetStyle().ItemInnerSpacing.x);
                float v = 0;
                if (!isNumber(p, v)) {
                    ImGui::TextDisabled("%s", p.c_str());
                } else if (ImGui::DragFloat(axis == 0 ? tr("Width") : tr("Height"), &v, 1.0f, 0.0f, 100000.0f, "%.0f")) {
                    p = num(std::round(v));
                    sizeChanged = true;
                }
            }
            ImGui::PopID();
        }
        if (sizeChanged) set("size", parts[0] + " " + parts[1]);
    }
    if (!placedByLayout && node.kind != "board") {
        // Anchor: 3x3 grid (which edge or center it stays attached to), with stretch/scale per axis beside it
        std::vector<std::string> c = getProperty(node, "constraints", value) ? words(value) : std::vector<std::string>{};
        if (c.size() != 2) c = {"left", "top"};
        auto write = [&] {
            if (c[0] == "left" && c[1] == "top") unset("constraints");
            else set("constraints", c[0] + " " + c[1]);
        };
        const char* hs[] = {"left", "center", "right"};
        const char* vs[] = {"top", "center", "bottom"};
        const float s = ImGui::GetFrameHeight() * 0.8f;
        const ImVec2 start = ImGui::GetCursorScreenPos();
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) {
                ImGui::SetCursorScreenPos(ImVec2(start.x + col * (s + 2.0f), start.y + row * (s + 2.0f)));
                ImGui::PushID(row * 3 + col);
                const bool on = c[0] == hs[col] && c[1] == vs[row];
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
                if (ImGui::Button("##a", ImVec2(s, s))) c = {hs[col], vs[row]}, write();
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("%s %s", hs[col], vs[row]);
                ImGui::PopID();
            }
        const char* hAll[] = {"left", "center", "right", "leftright", "scale"};
        const char* vAll[] = {"top", "center", "bottom", "topbottom", "scale"};
        const float x = start.x + 3.0f * (s + 2.0f) + 8.0f;
        for (int axis = 0; axis < 2; ++axis) {
            ImGui::SetCursorScreenPos(ImVec2(x, start.y + axis * (ImGui::GetFrameHeight() + 4.0f)));
            ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetFontSize() * 7.0f));
            ImGui::PushID(10 + axis);
            if (ImGui::BeginCombo(axis == 0 ? tr("Anchor") : "##v", c[static_cast<size_t>(axis)].c_str())) {
                for (const char* o : axis == 0 ? hAll : vAll)
                    if (ImGui::Selectable(o, c[static_cast<size_t>(axis)] == o)) c[static_cast<size_t>(axis)] = o, write();
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("%s", axis == 0 ? tr("Which edge it keeps its distance to when the screen is wider. leftright stretches, scale grows with the screen")
                                                  : tr("Which edge it keeps its distance to when the screen is taller. topbottom stretches, scale grows with the screen"));
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + std::max(3.0f * (s + 2.0f), 2.0f * (ImGui::GetFrameHeight() + 4.0f)) + 2.0f));
        ImGui::Dummy(ImVec2(1, 1));
    }
    if (hasBox) ImGui::TextDisabled(tr("Drawn at %.0f, %.0f  size %.0f x %.0f"), drawn.x, drawn.y, drawn.w, drawn.h);

    // ---- Children's layout (frames and the board)
    if (node.kind == "frame" || node.kind == "board") {
        std::string layout;
        const bool hasLayout = getProperty(node, "layout", layout);
        const std::vector<std::string> parts = words(layout);
        const std::string kind = parts.empty() ? std::string() : parts[0];
        if (ImGui::BeginCombo(tr("Children"), hasLayout ? kind.c_str() : tr("free (pos)"))) {
            if (ImGui::Selectable(tr("free (pos)"), !hasLayout) && hasLayout) unset("layout");
            for (const char* k : {"row", "column", "grid"})
                if (ImGui::Selectable(k, kind == k) && kind != k) {
                    std::string line = k; // row <-> column keeps reverse/wrap; grid starts plain
                    if (std::string(k) != "grid" && kind != "grid")
                        for (size_t i = 1; i < parts.size(); ++i) line += " " + parts[i];
                    set("layout", line);
                }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(tr("How the children are placed. Free: each by its own pos"));
        if (hasLayout && kind != "grid") {
            bool wrap = std::find(parts.begin(), parts.end(), "wrap") != parts.end();
            if (checkbox(tr("Wrap"), &wrap)) {
                std::string line = kind;
                for (size_t i = 1; i < parts.size(); ++i)
                    if (parts[i] != "wrap") line += " " + parts[i];
                set("layout", wrap ? line + " wrap" : line);
            }
        }
        if (hasLayout) {
            combo(tr("Justify"), "justify", kJustify, std::size(kJustify));
            combo(tr("Align"), "align", kAlign, std::size(kAlign));
            number(tr("Gap"), "gap", 0.0f, 0.5f, 0.0f, 10000.0f, "%.0f");
        }
        std::string pad;
        std::vector<float> p = getProperty(node, "padding", pad) ? numbers(pad) : std::vector<float>{0.0f};
        if (p.size() == 2) p = {p[0], p[1], p[0], p[1]};
        if (p.size() == 1) p = {p[0], p[0], p[0], p[0]};
        if (p.size() == 4) {
            if (ImGui::DragFloat4(tr("Padding"), p.data(), 0.5f, 0.0f, 10000.0f, "%.0f")) {
                const bool same = p[0] == p[1] && p[1] == p[2] && p[2] == p[3];
                set("padding", same ? num(p[0]) : num(p[0]) + " " + num(p[1]) + " " + num(p[2]) + " " + num(p[3]));
            }
            ImGui::SetItemTooltip(tr("top, right, bottom, left"));
        } else ImGui::TextDisabled(tr("Padding  %s"), pad.c_str());
    }

    // ---- Style
    if (node.kind != "board" || getProperty(node, "fill", value)) ImGui::SeparatorText(tr("Style"));
    for (const char* key : {"fill", "color"}) {
        if (!getProperty(node, key, value)) continue;
        const char* label = key == std::string("fill") ? tr("Fill") : tr("Text color");
        float rgba[4];
        if (parseHex(value, rgba)) {
            if (ImGui::ColorEdit4(label, rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf))
                set(key, hex(rgba, value.size() == 9 || rgba[3] < 1.0f));
        } else ImGui::TextDisabled("%s  %s", label, value.c_str()); // $token
    }
    if (node.kind != "board" && node.kind != "use") {
        if (!getProperty(node, "fill", value) && node.kind == "frame" && ImGui::SmallButton(tr("Add fill"))) set("fill", "#FFFFFF33");
        if (node.kind == "frame" || node.kind == "image") number(tr("Corner radius"), "radius", 0.0f, 0.5f, 0.0f, 10000.0f, "%.0f");
        number(tr("Opacity"), "opacity", 1.0f, 0.01f, 0.0f, 1.0f, "%.2f");
    }

    // ---- Pickers: the project's fonts and pictures, read again every few seconds
    if ((node.kind == "text" || node.kind == "image") && m_time - m_assetListTime > 5.0) {
        m_assetListTime = m_time;
        m_imageFiles.clear();
        m_fontFiles.clear();
        for (const char* folder : {"ui", "textures"}) listAssets(m_host.assets / folder, std::string(folder) + "/", {".png", ".jpg", ".jpeg"}, true, m_imageFiles);
        listAssets(m_host.assets / "fonts", "", {".ttf", ".otf"}, false, m_fontFiles);
    }
    if (node.kind == "text") {
        ImGui::SeparatorText(tr("Text"));
        std::string content;
        if (getProperty(node, "content", content)) {
            ImGui::SetNextItemWidth(-1);
            if (inputLine("##content", content)) set("content", content);
            ImGui::SetItemTooltip(tr("{field} inserts a view model value"));
        } else if (getProperty(node, "key", content)) ImGui::TextDisabled(tr("Translated text: key %s"), content.c_str());
        number(tr("Font size"), "fontSize", 24.0f, 0.25f, 1.0f, 1000.0f, "%.0f");
        std::string font;
        const bool hasFont = getProperty(node, "font", font);
        if (ImGui::BeginCombo(tr("Font"), hasFont ? font.c_str() : tr("(default)"))) {
            if (ImGui::Selectable(tr("(default)"), !hasFont) && hasFont) unset("font");
            if (m_fontFiles.empty()) ImGui::TextDisabled(tr("No .ttf or .otf in assets/fonts"));
            for (const std::string& f : m_fontFiles)
                if (ImGui::Selectable(f.c_str(), hasFont && f == font)) set("font", f);
            ImGui::EndCombo();
        }
        combo(tr("Text align"), "textAlign", kTextAlign, std::size(kTextAlign));
        if (!getProperty(node, "color", value) && ImGui::SmallButton(tr("Set text color"))) set("color", "#FFFFFF");
    }
    if (node.kind == "image") {
        ImGui::SeparatorText(tr("Image"));
        std::string src;
        const bool has = getProperty(node, "src", src);
        if (ImGui::BeginCombo(tr("Image file"), has ? src.c_str() : tr("(none)"), ImGuiComboFlags_HeightLarge)) {
            if (m_imageFiles.empty()) ImGui::TextDisabled(tr("No pictures in assets/ui or assets/textures yet"));
            for (const std::string& f : m_imageFiles)
                if (ImGui::Selectable(f.c_str(), has && f == src)) set("src", f);
            ImGui::EndCombo();
        }
        if (!has) ImGui::TextDisabled(tr("Copy a PNG into assets/ui and pick it here"));
    }

    // ---- Behaviour: what the node does when the player uses it
    if (node.kind != "board" && node.kind != "use") {
        struct Kind {
            const char* key;
            const char* label;
            const char* fresh; // the words a new choice writes
            bool text;         // text nodes only
        };
        const Kind kinds[] = {{"button", tr("Button (sends an action)"), "confirm", false},
                              {"toggle", tr("Toggle (bool field)"), "enabled", false},
                              {"slider", tr("Slider (float field)"), "value01 0 1 0.01", false},
                              {"list", tr("List (repeats the first child)"), "items", false},
                              {"scroll", tr("Scroll area"), "", false},
                              {"textInput", tr("Text input (string field)"), "text", true}};
        int current = -1;
        std::string line;
        for (int i = 0; i < static_cast<int>(std::size(kinds)); ++i)
            if (getProperty(node, kinds[i].key, line)) {
                current = i;
                break;
            }
        ImGui::SeparatorText(tr("Behaviour"));
        if (ImGui::BeginCombo(tr("Kind"), current < 0 ? tr("None (just drawn)") : kinds[current].label)) {
            if (ImGui::Selectable(tr("None (just drawn)"), current < 0) && current >= 0) unset(kinds[current].key);
            for (int i = 0; i < static_cast<int>(std::size(kinds)); ++i) {
                if (kinds[i].text != (node.kind == "text")) continue;
                if (ImGui::Selectable(kinds[i].label, i == current) && i != current) {
                    if (current >= 0) unset(kinds[current].key);
                    set(kinds[i].key, kinds[i].fresh);
                }
            }
            ImGui::EndCombo();
        }
        if (current >= 0) {
            const std::string key = kinds[current].key;
            std::vector<std::string> w = words(line);
            std::vector<const char*> names;
            if (key == "button") names = {"Action"};
            else if (key == "toggle" || key == "textInput") names = {"Field", "Action (optional)"};
            else if (key == "slider") names = {"Field", "Minimum", "Maximum", "Step", "Action (optional)"};
            else if (key == "list") names = {"Field"};
            else if (key == "scroll") names = {"Direction (horizontal or empty)"};
            if (w.size() < names.size()) w.resize(names.size());
            bool edited = false;
            for (size_t i = 0; i < names.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (inputLine(names[i], w[i])) edited = true;
                ImGui::PopID();
            }
            if (key == "button") ImGui::TextDisabled(tr("Game code reads the action from ctx.ui.actions()"));
            else if (key != "scroll") ImGui::TextDisabled(tr("The field is a member of the screen's view model"));
            if (edited) {
                while (!w.empty() && w.back().empty()) w.pop_back();
                std::string out;
                for (const std::string& s : w) out += (out.empty() ? "" : " ") + (s.empty() ? std::string("-") : s);
                set(kinds[current].key, out);
            }
        }
    }
    return changed;
}

void App::drawProperties() {
    ImGui::Begin(panelTitle(kIconInspector, "Properties").c_str());
    SourceNode* n = m_hasSelection && m_sourceError.empty() ? findNode(m_source, m_selected) : nullptr;
    if (!n) {
        ImGui::TextDisabled(m_sourceError.empty() ? tr("Select a node in the tree or the preview") : tr("Fix the source first"));
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(m_source.exported);
    // Edits apply on every key (the preview follows); one undo step per field edit
    auto field = [&](const char* label, std::string& value) {
        const std::string before = m_text;
        const bool changed = inputLine(label, value);
        if (ImGui::IsItemActivated()) m_lastSnapshot = before;
        if (changed) sourceEdited();
        if (ImGui::IsItemDeactivatedAfterEdit() && m_lastSnapshot != m_text) {
            m_undo.push_back(m_lastSnapshot);
            m_redo.clear();
        }
        return changed;
    };
    ImGui::Text("%s", n->kind.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    field("##name", n->name);
    n = findNode(m_source, m_selected);
    if (n) {
        const std::string before = m_text;
        if (drawInspector(*n)) {
            if (!m_inspectorActive) { // one undo step per drag or pick
                m_undo.push_back(before);
                m_redo.clear();
                m_inspectorActive = true;
            }
            sourceEdited();
            n = findNode(m_source, m_selected);
        }
        if (!ImGui::IsAnyItemActive()) m_inspectorActive = false;
    }
    ImGui::SeparatorText(tr("All lines"));
    if (!n) {
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }
    int remove = -1;
    for (size_t i = 0; i < n->items.size(); ++i) {
        SourceItem& item = n->items[i];
        if (!isPropertyItem(item)) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
        field("##line", item.line);
        // n may move when sourceEdited re-reads the text: the next lines use the fresh node
        n = findNode(m_source, m_selected);
        ImGui::SameLine();
        if (iconButton(kIconRemove, tr("Remove"))) remove = static_cast<int>(i);
        ImGui::PopID();
        if (!n || i >= n->items.size()) break;
    }
    if (n && remove >= 0 && remove < static_cast<int>(n->items.size())) {
        snapshot();
        n->items.erase(n->items.begin() + remove);
        sourceEdited();
        n = findNode(m_source, m_selected);
    }
    ImGui::SetNextItemWidth(-1);
    if (n && ImGui::InputTextWithHint("##add", tr("add a line: fill #FF8800FF, on hover fill ..."), m_newProperty, sizeof m_newProperty,
                                      ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string line = trim(m_newProperty);
        if (!line.empty()) {
            // after the last property line (before the children, like setProperty)
            size_t at = 0;
            for (size_t i = 0; i < n->items.size(); ++i)
                if (isPropertyItem(n->items[i])) at = i + 1;
            snapshot();
            n->items.insert(n->items.begin() + static_cast<std::ptrdiff_t>(at), SourceItem{line, -1});
            sourceEdited();
        }
        m_newProperty[0] = 0;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void App::drawText() {
    ImGui::Begin(panelTitle(kIconCode, "Source").c_str());
    if (m_file.empty()) {
        ImGui::TextDisabled(tr("Open a file"));
        ImGui::End();
        return;
    }
    if (m_externalChange) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), tr("Changed on disk."));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Reload (drop my edits)"))) reloadFromDisk();
    }
    const std::string before = m_text;
    if (inputMultiline("##text", m_text, ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput)) textChanged();
    if (ImGui::IsItemActivated()) m_lastSnapshot = before;
    if (ImGui::IsItemDeactivatedAfterEdit() && m_lastSnapshot != m_text) {
        m_undo.push_back(m_lastSnapshot);
        m_redo.clear();
    }
    ImGui::End();
}

void App::drawProblems() {
    ImGui::Begin(panelTitle(kIconWarning, "Problems").c_str());
    if (!m_sourceError.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", m_sourceError.c_str());
    if (m_screen != kNoScreen)
        for (const std::string& e : m_ui->info(m_screen).errors) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", e.c_str());
    for (const std::string& w : m_host.warnings) ImGui::TextColored(ImVec4(0.949f, 0.580f, 0.137f, 1.0f), "%s", w.c_str());
    if (!m_actions.empty()) {
        ImGui::Separator();
        for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) ImGui::TextDisabled(tr("action %s"), it->c_str());
    }
    ImGui::End();
}

void App::drawUnsavedDialog() {
    if (!ImGui::BeginPopupModal((std::string(tr("Unsaved changes")) + "###unsaved").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text(tr("%s has unsaved changes."), m_file.c_str());
    auto proceed = [&] {
        const int action = m_pendingAction;
        m_pendingAction = 0;
        ImGui::CloseCurrentPopup();
        if (action == 1) m_quitConfirmed = true;
        else if (action == 2) openFile(m_pendingFile);
        else if (action == 3) openProject(m_pendingProject);
    };
    if (ImGui::Button(tr("Save"))) {
        if (save()) proceed();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Don't save"))) {
        m_savedText = m_text; // drop the edits
        proceed();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel"))) {
        m_pendingAction = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Library, components, new screens

namespace {

struct Widget {
    const char* label;
    const char* tooltip;
    const char* text; // the node as .ui lines
};

// Same widgets as the MealKit editor's library. Plain colors (not $tokens) so any theme takes them; field and action names are
// placeholders (a missing field shows under Problems once a view model is bound)
const Widget kWidgets[] = {
    {trn("Panel"), trn("A box that lines its children up in a column (padding, gap, rounded corners)"),
     "frame panel\n  size 420 hug\n  layout column\n  gap 12\n  padding 24\n  fill #1B2130E6\n  radius 14\nend\n"},
    {trn("Text"), trn("A line of text. {field} in it shows a view model value"), "text label\n  content Text\n  fontSize 28\n  color #FFFFFF\nend\n"},
    {trn("Image"), trn("A picture from assets (set src to a PNG path)"), "image image\n  size 96 96\nend\n"},
    {trn("Button"), trn("A button that sends an action when pressed"),
     "frame button\n  size 280 60\n  layout row\n  justify center\n  align center\n  fill #3B6FE0\n  radius 10\n  button confirm\n"
     "  on hover fill #5585F0\n  on pressed fill #2C58B8\n  on focus stroke 3 #FFFFFF\n  text buttonLabel\n    content Button\n    fontSize 26\n"
     "    color #FFFFFF\n  end\nend\n"},
    {trn("Toggle"), trn("An on/off switch for a bool field"),
     "frame toggleRow\n  size hug hug\n  layout row\n  align center\n  gap 14\n  frame toggle\n    size 64 32\n    radius 16\n    fill #FFFFFF26\n"
     "    toggle enabled\n    on checked fill #3B6FE0\n    frame knobOff\n      pos 4 4\n      size 24 24\n      radius 12\n      fill #FFFFFF\n"
     "      visible !enabled\n    end\n    frame knobOn\n      pos 36 4\n      size 24 24\n      radius 12\n      fill #FFFFFF\n      visible enabled\n"
     "    end\n  end\n  text toggleLabel\n    content Option\n    fontSize 24\n    color #FFFFFF\n  end\nend\n"},
    {trn("Slider"), trn("Drag to set a float field between a minimum and a maximum"),
     "frame slider\n  size 320 28\n  radius 14\n  fill #FFFFFF26\n  clip\n  slider value01 0 1 0.01\n  on hover stroke 2 #FFFFFF40\n"
     "  frame sliderFill\n    size fill fill\n    widthRatio value01\n    radius 14\n    fill #3B6FE0\n  end\nend\n"},
    {trn("Progress bar"), trn("A bar filled by a float field from 0 to 1 (health, loading)"),
     "frame progress\n  size 320 20\n  radius 10\n  fill #FFFFFF1A\n  clip\n  frame progressFill\n    size fill fill\n    widthRatio progress\n"
     "    radius 10\n    fill #4CC38A\n  end\nend\n"},
    {trn("Text input"), trn("A box the player types into (a string field)"),
     "frame inputBox\n  size 360 52\n  layout row\n  align center\n  padding 0 16\n  fill #00000055\n  stroke 1 #FFFFFF33\n  radius 10\n"
     "  text input\n    size fill hug\n    textInput text\n    maxLength 32\n    fontSize 24\n    color #FFFFFF\n    maxLines 1\n  end\nend\n"},
    {trn("List"), trn("Repeats its first child for every element of a list field"),
     "frame list\n  size 420 hug\n  layout column\n  gap 6\n  list items\n  frame row\n    size fill 48\n    layout row\n    align center\n"
     "    padding 0 14\n    radius 8\n    fill #FFFFFF0A\n    text rowName\n      content {name}\n      fontSize 22\n      color #FFFFFF\n    end\n  end\nend\n"},
    {trn("Scroll area"), trn("Clips its content and scrolls it with the wheel or the gamepad"),
     "frame scrollArea\n  size 420 300\n  scroll\n  frame content\n    size fill hug\n    layout column\n    gap 8\n  end\nend\n"},
};

// Where a new node may go inside: the board, or a frame with a layout that isn't itself a widget (a button, a toggle row, a text
// box). Anything else gets the node beside it, so Button twice gives two buttons, not a button in a button
bool isContainer(const SourceNode& n) {
    if (n.kind == "board") return true;
    std::string v;
    if (n.kind != "frame" || !getProperty(n, "layout", v)) return false;
    for (const char* w : {"button", "toggle", "slider", "select", "list"})
        if (getProperty(n, w, v)) return false;
    for (const SourceNode& c : n.children)
        for (const char* w : {"toggle", "textInput", "widthRatio", "select"})
            if (getProperty(c, w, v)) return false;
    return true;
}

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 48) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
    return true;
}

} // namespace

// A library item dropped on the preview: into the deepest container under the mouse (or the board), at the mouse in free layouts
void App::dropNode(SourceNode node, float lx, float ly) {
    if (m_screen == kNoScreen || m_docPaths.empty()) return;
    const std::vector<NodeBox> boxes = m_ui->boxes(m_screen);
    const NodeBox* best = nullptr;
    for (const NodeBox& b : boxes) {
        if (!b.visible || b.node >= m_docPaths.size() || lx < b.x || ly < b.y || lx >= b.x + b.w || ly >= b.y + b.h) continue;
        const SourceNode* n = findNode(m_source, m_docPaths[b.node]);
        if (!n || !isContainer(*n)) continue;
        if (!best || b.depth >= best->depth) best = &b;
    }
    const Path parent = best ? m_docPaths[best->node] : Path{};
    const float at[2] = {lx - (best ? best->x : 0.0f), ly - (best ? best->y : 0.0f)};
    insertNode(std::move(node), &parent, at);
}

void App::insertNode(SourceNode node, const Path* parent, const float* pos) {
    if (m_source.root.children.empty() || !m_sourceError.empty() || m_source.exported) return;
    // Names unique in the file, the subtree's own names included
    struct Rename {
        const Source& source;
        std::vector<std::string> taken;
        void run(SourceNode& n) {
            std::string name = uniqueName(source, n.name);
            for (int k = 2; std::find(taken.begin(), taken.end(), name) != taken.end(); ++k) name = uniqueName(source, n.name + std::to_string(k));
            taken.push_back(name);
            n.name = name;
            for (SourceNode& c : n.children) run(c);
        }
    } rename{m_source, {}};
    rename.run(node);
    Path into;
    u32 at = 0xFFFFFFFFu;
    if (parent) {
        const SourceNode* p = findNode(m_source, *parent);
        if (!p) return;
        std::string layout;
        if (pos && !getProperty(*p, "layout", layout)) // free layout: where it was dropped
            setProperty(node, "pos", std::to_string(static_cast<int>(std::lround(pos[0]))) + " " + std::to_string(static_cast<int>(std::lround(pos[1]))));
        into = *parent;
    } else if (m_hasSelection) {
        const SourceNode* sel = findNode(m_source, m_selected);
        if (sel && isContainer(*sel)) into = m_selected;
        else if (!m_selected.empty()) {
            into.assign(m_selected.begin(), m_selected.end() - 1);
            at = m_selected.back() + 1;
        }
    }
    snapshot();
    m_selected = addNode(m_source, into, at, std::move(node));
    m_hasSelection = true;
    sourceEdited();
}

bool App::makeComponent(const std::string& name) {
    const SourceNode* n = m_hasSelection ? findNode(m_source, m_selected) : nullptr;
    NodeBox b{};
    if (!n || n->kind != "frame" || m_selected.empty() || !selectedBox(b)) return false;
    const std::string asset = "ui/components/" + name + ".ui";
    const std::filesystem::path file = m_host.assets / pathOf(asset);
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        m_status = trf("%s exists already", asset.c_str());
        m_statusTime = m_time;
        return false;
    }
    const std::string before = m_text;
    Source component;
    if (!extractComponent(m_source, m_selected, name, asset, static_cast<i32>(b.w + 0.5f), static_cast<i32>(b.h + 0.5f), component)) {
        m_status = tr("Only a frame can become a component");
        m_statusTime = m_time;
        textChanged(); // m_source back from the text
        return false;
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << writeSource(component);
    if (!out) {
        m_status = trf("Cannot write %s", asset.c_str());
        m_statusTime = m_time;
        m_text = before;
        textChanged();
        return false;
    }
    out.close();
    m_undo.push_back(before);
    m_redo.clear();
    sourceEdited();
    scanFiles();
    m_status = trf("Component %s (save the screen to keep the use)", asset.c_str());
    m_statusTime = m_time;
    return true;
}

bool App::newScreen(const std::string& name) {
    if (m_text != m_savedText) {
        m_status = tr("Save the open screen first");
        m_statusTime = m_time;
        return false;
    }
    const std::string asset = "ui/" + name + ".ui";
    const std::filesystem::path file = m_host.assets / pathOf(asset);
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        m_status = trf("%s exists already", asset.c_str());
        m_statusTime = m_time;
        return false;
    }
    std::ofstream out(file, std::ios::binary);
    out << "# MkUI screen: " << name << "\n@format ui 1\nboard " << name << " 1920 1080\n";
    if (!out) {
        m_status = trf("Cannot write %s", asset.c_str());
        m_statusTime = m_time;
        return false;
    }
    out.close();
    scanFiles();
    return openFile(asset);
}

void App::drawLibrary() {
    ImGui::Begin(panelTitle(kIconLibrary, "Library").c_str());
    const bool editable = !m_file.empty() && m_sourceError.empty() && !m_source.exported;
    ImGui::BeginDisabled(!editable);
    ImGui::TextDisabled(tr("Widgets"));
    const float width = ImGui::GetContentRegionAvail().x;
    const float button = std::max(90.0f, (width - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    int column = 0;
    for (const Widget& w : kWidgets) {
        if (column++ % 2 && width >= button * 2) ImGui::SameLine();
        if (ImGui::Button(tr(w.label), ImVec2(button, 0))) {
            SourceNode n;
            if (parseNodeText(w.text, n)) insertNode(std::move(n));
        }
        if (ImGui::BeginDragDropSource()) { // or drag it onto the preview
            const int index = static_cast<int>(&w - kWidgets);
            ImGui::SetDragDropPayload("MKUI_WIDGET", &index, sizeof index);
            ImGui::Text("%s", tr(w.label));
            ImGui::EndDragDropSource();
        }
        ImGui::SetItemTooltip(tr("%s (click: add to the selection, drag: drop on the preview)"), tr(w.tooltip));
    }
    ImGui::Spacing();
    ImGui::TextDisabled(tr("Components (ui/components)"));
    bool any = false;
    for (const std::string& f : m_files) {
        if (f.rfind("ui/components/", 0) != 0 || f == m_file) continue;
        any = true;
        const std::string base = utf8Of(pathOf(f).stem());
        if (ImGui::Selectable(base.c_str())) {
            SourceNode n;
            n.kind = "use";
            n.name = base;
            n.args = f;
            insertNode(std::move(n));
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("MKUI_COMPONENT", f.data(), f.size());
            ImGui::Text("%s", base.c_str());
            ImGui::EndDragDropSource();
        }
        ImGui::SetItemTooltip(tr("Adds \"use %s %s\": the component's look, placed here"), base.c_str(), f.c_str());
    }
    if (!any) ImGui::TextDisabled(tr("  none yet: select a frame, then Make component"));
    ImGui::EndDisabled();
    ImGui::End();
}

void App::drawNameDialog() {
    if (m_nameDialog) {
        ImGui::OpenPopup("###name");
        if (m_nameDialog == 2 && m_hasSelection)
            if (const SourceNode* n = findNode(m_source, m_selected)) std::snprintf(m_nameInput, sizeof m_nameInput, "%s", n->name.c_str());
        if (m_nameDialog == 1) m_nameInput[0] = 0;
    }
    static int kind = 0;
    if (m_nameDialog) kind = m_nameDialog, m_nameDialog = 0;
    if (!ImGui::BeginPopupModal((std::string(tr("Name")) + "###name").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(kind == 1 ? tr("New screen in assets/ui/ (letters, digits, _ and -)") : tr("Component file in assets/ui/components/"));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", m_nameInput, sizeof m_nameInput, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool ok = validName(m_nameInput);
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button(tr("Create")) || (enter && ok)) {
        if (kind == 1) newScreen(m_nameInput);
        else makeComponent(m_nameInput);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- Fake view-model values

// The screen has no view model type here, so the fields come from the screen itself: every binding, {field} in texts and
// widget fields, with a kind guessed from where it's used. Fields under a list node belong to its rows (the runtime gives each
// row its own model). Values live in <project>/art/ui/samples/<screen>.values ("key = value" lines, not shipped)

namespace {

void addField(std::vector<App::FakeField>& out, std::string path, const std::string& list, ValueKind kind) {
    if (!path.empty() && path[0] == '!') path.erase(0, 1);
    if (path.empty() || path == "option") return;
    for (App::FakeField& f : out)
        if (f.path == path && f.list == list) {
            if (f.kind == ValueKind::String && kind != ValueKind::String) f.kind = kind; // a widget or binding says more than a text
            return;
        }
    out.push_back({path, list, kind});
}

void textFields(std::vector<App::FakeField>& out, const std::string& text, const std::string& list) {
    for (size_t i = 0; (i = text.find('{', i)) != std::string::npos; ++i) {
        const size_t close = text.find('}', i + 1);
        if (close == std::string::npos) break;
        std::string name = text.substr(i + 1, close - i - 1);
        name = name.substr(0, name.find(':'));
        if (!name.empty() && name.find(' ') == std::string::npos) addField(out, name, list, ValueKind::String);
        i = close;
    }
}

std::string defaultValue(const App::FakeField& f) {
    switch (f.kind) {
    case ValueKind::Bool: return "true";
    case ValueKind::Float: return "0.6";
    case ValueKind::Int: return "0";
    case ValueKind::Color: return "#3B6FE0FF";
    case ValueKind::Array: return "3";
    default: {
        const size_t dot = f.path.rfind('.');
        return f.path.substr(dot == std::string::npos ? 0 : dot + 1); // the field's own name reads well in a layout
    }
    }
}

ValueModel& childAt(ValueModel& m, const std::string& path, std::string& leaf) {
    ValueModel* cur = &m;
    size_t a = 0;
    for (size_t dot; (dot = path.find('.', a)) != std::string::npos; a = dot + 1) cur = &cur->child(path.substr(a, dot - a));
    leaf = path.substr(a);
    return *cur;
}

u32 parseColor(const std::string& s) {
    std::string hex = s[0] == '#' ? s.substr(1) : s;
    if (hex.size() == 6) hex += "FF";
    const u32 v = static_cast<u32>(std::strtoul(hex.c_str(), nullptr, 16)); // RRGGBBAA → rgba() order
    return rgba(static_cast<u8>(v >> 24), static_cast<u8>(v >> 16), static_cast<u8>(v >> 8), static_cast<u8>(v));
}

void setValue(ValueModel& m, const std::string& path, ValueKind kind, const std::string& value) {
    std::string leaf;
    ValueModel& at = childAt(m, path, leaf);
    switch (kind) {
    case ValueKind::Bool: at.set(leaf, value == "true" || value == "1"); break;
    case ValueKind::Float: at.set(leaf, static_cast<f32>(std::atof(value.c_str()))); break;
    case ValueKind::Int: at.set(leaf, static_cast<i32>(std::atoi(value.c_str()))); break;
    case ValueKind::Color: at.setColor(leaf, parseColor(value.empty() ? "#FFFFFFFF" : value)); break;
    default: at.set(leaf, value); break;
    }
}

} // namespace

std::string App::valueKey(const FakeField& f, int row) const {
    return f.list.empty() ? f.path : f.list + "[" + std::to_string(row) + "]." + f.path;
}

const std::string& App::value(const std::string& key, const FakeField& f) {
    auto it = m_values.find(key);
    if (it == m_values.end()) it = m_values.emplace(key, defaultValue(f)).first;
    return it->second;
}

void App::collectFields() {
    std::vector<FakeField> fields;
    for (size_t i = 0; i < m_doc.nodes.size(); ++i) {
        const Node& n = m_doc.nodes[i];
        std::string list; // the nearest list above (its rows hold this node's fields)
        for (i32 p = n.parent; p >= 0; p = m_doc.nodes[static_cast<size_t>(p)].parent)
            if (m_doc.nodes[static_cast<size_t>(p)].widget == WidgetKind::List) {
                list = m_doc.nodes[static_cast<size_t>(p)].field;
                break;
            }
        for (const std::string* f : {&n.visibleField, &n.disabledField}) addField(fields, *f, list, ValueKind::Bool);
        for (const std::string* f : {&n.widthRatioField, &n.scaleField, &n.offsetXField, &n.offsetYField, &n.opacityField, &n.brightnessField,
                                     &n.fontSizeField})
            addField(fields, *f, list, ValueKind::Float);
        addField(fields, n.colorField, list, ValueKind::Color);
        addField(fields, n.imageField, list, ValueKind::String);
        switch (n.widget) {
        case WidgetKind::Toggle: addField(fields, n.field, list, ValueKind::Bool); break;
        case WidgetKind::Slider: addField(fields, n.field, list, ValueKind::Float); break;
        case WidgetKind::Select: addField(fields, n.field, list, ValueKind::Int); break;
        case WidgetKind::TextInput: addField(fields, n.field, list, ValueKind::String); break;
        case WidgetKind::List: addField(fields, n.field, list, ValueKind::Array); break;
        default: break;
        }
        textFields(fields, !n.textKey.empty() ? m_host.text(n.textKey) : n.textContent, list);
    }
    const bool changed = fields.size() != m_fields.size() ||
                         !std::equal(fields.begin(), fields.end(), m_fields.begin(), [](const FakeField& a, const FakeField& b) {
                             return a.path == b.path && a.list == b.list && a.kind == b.kind;
                         });
    if (!changed) return;
    m_fields = std::move(fields);
    buildModel();
}

void App::buildModel() {
    m_model = ValueModel{};
    for (const FakeField& f : m_fields) {
        if (!f.list.empty()) continue;
        if (f.kind == ValueKind::Array) {
            const int rows = std::clamp(std::atoi(value(f.path, f).c_str()), 0, 50);
            std::string leaf;
            std::vector<ValueModel>& items = childAt(m_model, f.path, leaf).list(leaf);
            items.resize(static_cast<size_t>(rows));
            for (int r = 0; r < rows; ++r)
                for (const FakeField& c : m_fields)
                    if (c.list == f.path && c.kind != ValueKind::Array) setValue(items[static_cast<size_t>(r)], c.path, c.kind, value(valueKey(c, r), c));
            continue;
        }
        setValue(m_model, f.path, f.kind, value(f.path, f));
    }
    if (m_screen != kNoScreen) m_ui->bind(m_screen, m_valuesOn ? m_model.model() : Model{});
}

std::filesystem::path App::valuesPath() const {
    if (m_file.empty()) return {};
    std::string stem = m_file.substr(3); // without "ui/"
    stem = stem.substr(0, stem.size() - 3);
    std::replace(stem.begin(), stem.end(), '/', '_');
    return pathOf(m_project) / "art" / "ui" / "samples" / pathOf(stem + ".values");
}

void App::loadValues() {
    m_values.clear();
    m_fields.clear();
    std::string text;
    if (readText(valuesPath(), text)) {
        std::istringstream in(text);
        for (std::string line; std::getline(in, line);) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == '@') continue;
            const size_t eq = line.find(" = ");
            if (eq != std::string::npos) m_values[line.substr(0, eq)] = line.substr(eq + 3);
        }
    }
}

void App::saveValues() {
    const std::filesystem::path p = valuesPath();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    out << "# Preview values for MkUI Designer (" << m_file << "), not shipped\n";
    for (const auto& [key, v] : m_values) out << key << " = " << v << "\n";
}

void App::drawValues() {
    ImGui::Begin(panelTitle(kIconData, "Values").c_str());
    if (m_file.empty()) {
        ImGui::TextDisabled(tr("Open a file"));
        ImGui::End();
        return;
    }
    if (checkbox(tr("Use these values"), &m_valuesOn)) buildModel();
    ImGui::SetItemTooltip(tr("Off: the screen as the game shows it with no view model bound"));
    if (m_fields.empty()) ImGui::TextDisabled(tr("This screen binds no fields"));
    ImGui::TextDisabled(tr("Saved in art/ui/samples/ (not shipped)"));
    ImGui::Separator();
    bool changed = false, commit = false;
    auto edit = [&](const FakeField& f, const std::string& key, const char* label) {
        std::string& v = m_values[key];
        if (v.empty() && f.kind != ValueKind::String) v = defaultValue(f);
        ImGui::PushID(key.c_str());
        ImGui::SetNextItemWidth(-ImGui::GetContentRegionAvail().x * 0.45f);
        switch (f.kind) {
        case ValueKind::Bool: {
            bool b = v == "true";
            if (checkbox(label, &b)) v = b ? "true" : "false", changed = commit = true;
            break;
        }
        case ValueKind::Float: {
            float x = static_cast<float>(std::atof(v.c_str()));
            if (ImGui::DragFloat(label, &x, 0.01f)) {
                char buf[32];
                std::snprintf(buf, sizeof buf, "%g", x);
                v = buf, changed = true;
            }
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case ValueKind::Int:
        case ValueKind::Array: {
            int x = std::atoi(v.c_str());
            if (f.kind == ValueKind::Array ? ImGui::SliderInt(label, &x, 0, 20, tr("%d rows")) : ImGui::InputInt(label, &x)) v = std::to_string(x), changed = true;
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case ValueKind::Color: {
            const u32 c = parseColor(v.empty() ? "#FFFFFFFF" : v);
            float rgba4[4] = {(c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, (c >> 24) / 255.0f};
            if (ImGui::ColorEdit4(label, rgba4, ImGuiColorEditFlags_AlphaBar)) {
                char buf[16];
                std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", static_cast<int>(rgba4[0] * 255 + 0.5f), static_cast<int>(rgba4[1] * 255 + 0.5f),
                              static_cast<int>(rgba4[2] * 255 + 0.5f), static_cast<int>(rgba4[3] * 255 + 0.5f));
                v = buf, changed = true;
            }
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        default:
            if (inputLine(label, v)) changed = true;
            commit |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        ImGui::PopID();
    };
    for (const FakeField& f : m_fields) {
        if (!f.list.empty()) continue;
        edit(f, f.path, f.path.c_str());
        if (f.kind != ValueKind::Array) continue;
        const int rows = std::clamp(std::atoi(m_values[f.path].c_str()), 0, 50);
        ImGui::Indent();
        for (int r = 0; r < rows; ++r) {
            const std::string row = f.path + "[" + std::to_string(r) + "]";
            if (!ImGui::TreeNodeEx(row.c_str(), r == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
            for (const FakeField& c : m_fields)
                if (c.list == f.path && c.kind != ValueKind::Array) edit(c, valueKey(c, r), c.path.c_str());
            ImGui::TreePop();
        }
        ImGui::Unindent();
    }
    if (changed) buildModel();
    if (commit) saveValues();
    drawLive();
    ImGui::End();
}

// ---------------------------------------------------------------- Live connection (real view-model values)

// A running game or editor of this project (MealKit tool contract) answers ui/screens and ui/values?screen=<path> with the
// view model bound to that screen as "path value" pairs. Its arrays are fixed slots; a "<list>Count" field says how many are used
bool App::takeLiveValues(const mkcontract::live::Instance& from, bool quiet) {
    const mkcontract::live::HttpResult r = mkcontract::live::request(from, "GET", "ui/values?screen=" + mkcontract::live::urlEncode(m_file), {}, 250);
    mkcontract::Json json;
    if (r.status != 200 || !mkcontract::Json::parse(r.body, json) || !json["ok"].flag()) {
        if (!quiet) {
            mkcontract::Json err;
            m_liveStatus = r.status == 0 ? std::string(tr("No answer")) : mkcontract::Json::parse(r.body, err) ? err["error"].str("error") : std::string(tr("Bad answer"));
        }
        return false;
    }
    std::map<std::string, std::string> got;
    for (const mkcontract::Json& pair : json["values"].array)
        if (pair.array.size() == 2) got[pair.array[0].str()] = pair.array[1].str();
    for (const FakeField& f : m_fields) {
        if (f.kind != ValueKind::Array) continue;
        if (!f.list.empty()) continue; // lists inside rows: not yet
        const std::string& full = f.path;
        auto count = got.find(full + "Count");
        int rows = 0;
        if (count != got.end()) rows = std::atoi(count->second.c_str());
        else
            for (const auto& [key, v] : got)
                if (key.rfind(full + "[", 0) == 0) rows = std::max(rows, std::atoi(key.c_str() + full.size() + 1) + 1);
        got[full] = std::to_string(rows);
    }
    for (const auto& [key, v] : got) m_values[key] = v;
    buildModel();
    if (!quiet) {
        saveValues();
        m_liveStatus = trf("Took %d values of %s", static_cast<int>(got.size()), json["model"].str("?").c_str());
    }
    return true;
}

void App::drawLive() {
    if (m_project.empty()) return;
    // The list of running games/editors, and what each has open, every two seconds
    if (m_time - m_liveCheck > 2.0) {
        m_liveCheck = m_time;
        std::vector<mkcontract::live::Instance> found = mkcontract::live::findInstances(m_project);
        const int followPid = m_follow >= 0 && m_follow < static_cast<int>(m_live.size()) ? static_cast<int>(m_live[m_follow].pid) : -1;
        m_live = std::move(found);
        m_follow = -1;
        m_liveScreens.assign(m_live.size(), std::string());
        for (size_t i = 0; i < m_live.size(); ++i) {
            if (static_cast<int>(m_live[i].pid) == followPid) m_follow = static_cast<int>(i);
            const mkcontract::live::HttpResult r = mkcontract::live::request(m_live[i], "GET", "ui/screens", {}, 150);
            mkcontract::Json json;
            if (r.status == 503) m_liveScreens[i] = "not playing";
            else if (r.status != 200 || !mkcontract::Json::parse(r.body, json)) m_liveScreens[i] = "no ui/ service (older engine?)";
            else
                for (const mkcontract::Json& sc : json["screens"].array) {
                    const std::string model = sc["model"].str();
                    m_liveScreens[i] += (m_liveScreens[i].empty() ? "" : ", ") + sc["path"].str() + (model.empty() ? "" : " (" + model + ")");
                }
        }
    }
    if (m_followFirst && !m_live.empty() && !m_file.empty()) {
        m_followFirst = false;
        m_follow = 0;
        m_liveStatus = tr("Following (values update twice a second)");
    }
    if (m_follow >= 0 && m_follow < static_cast<int>(m_live.size()) && m_time - m_followTime > 0.5) {
        m_followTime = m_time;
        if (!takeLiveValues(m_live[m_follow], true)) m_follow = -1, m_liveStatus = tr("Follow stopped: the screen isn't open there");
    }

    ImGui::SeparatorText(tr("From a running game"));
    if (m_live.empty()) {
        ImGui::TextDisabled(tr("None running for this project. Start the game or press Play in the editor;"));
        ImGui::TextDisabled(tr("saved .ui files reload there by themselves."));
        return;
    }
    for (size_t i = 0; i < m_live.size(); ++i) {
        const mkcontract::live::Instance& in = m_live[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%s %u", in.kind.c_str(), in.pid);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Take values"))) takeLiveValues(in, false);
        ImGui::SetItemTooltip(tr("Copies the view model this game has bound to %s into these values"), m_file.c_str());
        ImGui::SameLine();
        bool follow = m_follow == static_cast<int>(i);
        if (checkbox(tr("Follow"), &follow)) {
            m_follow = follow ? static_cast<int>(i) : -1;
            m_liveStatus = follow ? tr("Following (values update twice a second)") : "";
        }
        ImGui::TextDisabled(tr("  open: %s"), m_liveScreens[i].empty() ? tr("no screens") : m_liveScreens[i].c_str());
        ImGui::PopID();
    }
    if (!m_liveStatus.empty()) ImGui::TextWrapped("%s", m_liveStatus.c_str());
}

// ---------------------------------------------------------------- Preview

void App::uploadAtlases() {
    int* g = m_host.dirty[0];
    if (g[2] > g[0] && g[3] > g[1]) {
        for (int y = g[1]; y < g[3]; ++y)
            for (int x = g[0]; x < g[2]; ++x) {
                const size_t i = static_cast<size_t>(y) * kAtlasSize + static_cast<size_t>(x);
                const float s = m_host.glyphs[i] / 255.0f;
                const float a = std::clamp((s - 0.5f) * kGlyphRamp + 0.5f, 0.0f, 1.0f);
                m_glyphRgba[i * 4 + 3] = static_cast<u8>(a * 255.0f + 0.5f);
            }
        const SDL_Rect r{g[0], g[1], g[2] - g[0], g[3] - g[1]};
        SDL_UpdateTexture(m_glyphTexture, &r, &m_glyphRgba[(static_cast<size_t>(r.y) * kAtlasSize + r.x) * 4], kAtlasSize * 4);
    }
    int* m = m_host.dirty[1];
    if (m[2] > m[0] && m[3] > m[1]) {
        const SDL_Rect r{m[0], m[1], m[2] - m[0], m[3] - m[1]};
        SDL_UpdateTexture(m_imageTexture, &r, &m_host.images[(static_cast<size_t>(r.y) * kAtlasSize + r.x) * 4], kAtlasSize * 4);
    }
    for (int* d : {g, m}) d[0] = d[1] = 1 << 30, d[2] = d[3] = -1;
}

void App::renderPreview(int width, int height) {
    if (!m_target || m_targetW != width || m_targetH != height) {
        if (m_target) SDL_DestroyTexture(m_target);
        m_target = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
        SDL_SetTextureScaleMode(m_target, SDL_SCALEMODE_LINEAR);
        m_targetW = width, m_targetH = height;
    }
    tessellate(m_ui->render(), m_list);
    uploadAtlases();

    static std::vector<SDL_Vertex> verts;
    verts.resize(m_list.vertices.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        const DrawVertex& v = m_list.vertices[i];
        verts[i].position = SDL_FPoint{v.x, v.y};
        verts[i].tex_coord = SDL_FPoint{v.u, v.v};
        verts[i].color = SDL_FColor{(v.color & 0xFF) / 255.0f, ((v.color >> 8) & 0xFF) / 255.0f, ((v.color >> 16) & 0xFF) / 255.0f,
                                    (v.color >> 24) / 255.0f};
    }
    SDL_SetRenderTarget(m_renderer, m_target);
    SDL_SetRenderDrawColor(m_renderer, 38, 40, 46, 255); // stands in for the game behind the UI
    SDL_RenderClear(m_renderer);
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    for (const DrawCommand& c : m_list.commands) {
        const float x0 = std::max(c.clip[0], 0.0f), y0 = std::max(c.clip[1], 0.0f);
        const float x1 = std::min(c.clip[2], static_cast<float>(width)), y1 = std::min(c.clip[3], static_cast<float>(height));
        if (x1 <= x0 || y1 <= y0) continue;
        const SDL_Rect clip{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(std::ceil(x1 - x0)), static_cast<int>(std::ceil(y1 - y0))};
        SDL_SetRenderClipRect(m_renderer, &clip);
        SDL_Texture* tex = c.texture == DrawTexture::Glyphs ? m_glyphTexture : c.texture == DrawTexture::Images ? m_imageTexture : nullptr;
        SDL_RenderGeometry(m_renderer, tex, verts.data(), static_cast<int>(verts.size()), reinterpret_cast<const int*>(&m_list.indices[c.firstIndex]),
                           static_cast<int>(c.indexCount));
    }
    SDL_SetRenderClipRect(m_renderer, nullptr);
    SDL_SetRenderTarget(m_renderer, nullptr);
}

int App::pickNode(float x, float y) const {
    if (m_screen == kNoScreen) return -1;
    int best = -1;
    u32 bestDepth = 0;
    for (const NodeBox& b : m_ui->boxes(m_screen))
        if (b.visible && x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h && (best < 0 || b.depth >= bestDepth))
            best = static_cast<int>(b.node), bestDepth = b.depth;
    return best;
}

namespace {

bool toNumber(const std::string& s, float& out) {
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && end != s.c_str() && *end == 0;
}

std::string whole(float v) { return std::to_string(static_cast<int>(std::lround(v))); }

constexpr int kHandles[] = {1 | 4, 2 | 4, 1 | 8, 2 | 8, 1, 2, 4, 8}; // 1 left 2 right 4 top 8 bottom

ImVec2 handlePoint(int h, ImVec2 a, ImVec2 b) {
    return ImVec2((h & 1) ? a.x : (h & 2) ? b.x : (a.x + b.x) * 0.5f, (h & 4) ? a.y : (h & 8) ? b.y : (a.y + b.y) * 0.5f);
}

} // namespace

bool App::selectedBox(NodeBox& out) const {
    if (!m_hasSelection || m_screen == kNoScreen || m_docPaths.empty()) return false;
    for (const NodeBox& b : m_ui->boxes(m_screen))
        if (b.visible && b.node < m_docPaths.size() && m_docPaths[b.node] == m_selected) {
            out = b;
            return true;
        }
    return false;
}

// Move needs a number "pos" (layout children and $tokens aren't dragged); resize changes only the dragged axes
void App::startDrag(int kind, int handle, float mx, float my) {
    m_drag = kind, m_handle = handle;
    m_dragArmed = kind != 1;
    m_dragStart[0] = mx, m_dragStart[1] = my;
    m_dragBefore = m_text;
    if (kind == 3) return;
    const SourceNode* n = findNode(m_source, m_selected);
    NodeBox b{};
    if (!n || m_selected.empty() || m_source.exported || !m_sourceError.empty() || !selectedBox(b)) {
        m_drag = 0;
        return;
    }
    std::string value;
    std::vector<std::string> w;
    m_startPos[0] = m_startPos[1] = 0.0f;
    const bool hasPos = getProperty(*n, "pos", value) && (w = words(value)).size() == 2 && toNumber(w[0], m_startPos[0]) &&
                        toNumber(w[1], m_startPos[1]);
    if (kind == 1 && !hasPos) {
        m_drag = 0;
        return; // selected only; layout children move with their parent
    }
    if (kind == 2 && !hasPos) m_handle &= ~(1 | 4); // the left/top edges need a pos
    if (kind == 2 && m_handle == 0) {
        m_drag = 0;
        return;
    }
    m_startSize[0] = b.w, m_startSize[1] = b.h; // hug/fill/$token start from the drawn size
    if (getProperty(*n, "size", value)) {
        w = words(value);
        for (size_t i = 0; i < 2 && i < w.size(); ++i) toNumber(w[i], m_startSize[i]);
    }
}

void App::applyDrag(float dx, float dy, bool snap) {
    SourceNode* n = findNode(m_source, m_selected);
    if (!n) return;
    auto grid = [&](float v) { return snap ? std::round(v / 10.0f) * 10.0f : v; }; // Shift: 10-unit grid
    if (m_drag == 1) setProperty(*n, "pos", whole(grid(m_startPos[0] + dx)) + " " + whole(grid(m_startPos[1] + dy)));
    else if (m_drag == 2) {
        float x = m_startPos[0], y = m_startPos[1], w = m_startSize[0], h = m_startSize[1];
        if (m_handle & 2) w = grid(x + w + dx) - x;
        if (m_handle & 8) h = grid(y + h + dy) - y;
        if (m_handle & 1) {
            const float right = x + w;
            x = grid(x + dx);
            w = right - x;
        }
        if (m_handle & 4) {
            const float bottom = y + h;
            y = grid(y + dy);
            h = bottom - y;
        }
        std::string value;
        std::vector<std::string> size = getProperty(*n, "size", value) ? words(value) : std::vector<std::string>{};
        size.resize(2, "hug");
        if (m_handle & 3) size[0] = whole(std::max(w, 1.0f));
        if (m_handle & 12) size[1] = whole(std::max(h, 1.0f));
        setProperty(*n, "size", size[0] + " " + size[1]);
        if (m_handle & 5) setProperty(*n, "pos", whole(x) + " " + whole(y));
    }
    sourceEdited();
}

void App::drawPreview(double dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(panelTitle(kIconPreview, "Preview").c_str());
    ImGui::PopStyleVar();
    if (m_screen == kNoScreen) {
        ImGui::TextDisabled(tr("  Pick a .ui file on the left"));
        ImGui::End();
        return;
    }
    const Resolution& res = kResolutions[m_resolution];
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float fit = std::max(0.05f, std::min(avail.x / res.w, avail.y / res.h)) * m_zoom;
    const ImVec2 size(res.w * fit, res.h * fit);
    const ImVec2 pos(std::floor(cursor.x + (avail.x - size.x) * 0.5f + m_panX), std::floor(cursor.y + (avail.y - size.y) * 0.5f + m_panY));

    // The whole panel takes the mouse (zoom and pan work outside the picture too)
    ImGui::SetCursorScreenPos(cursor);
    ImGui::InvisibleButton("##preview", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    SourceNode dropped; // a library item let go over the preview
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MKUI_WIDGET")) {
            const int index = *static_cast<const int*>(p->Data);
            if (index >= 0 && index < static_cast<int>(std::size(kWidgets))) parseNodeText(kWidgets[index].text, dropped);
        } else if (const ImGuiPayload* c = ImGui::AcceptDragDropPayload("MKUI_COMPONENT")) {
            dropped.kind = "use";
            dropped.args.assign(static_cast<const char*>(c->Data), static_cast<size_t>(c->DataSize));
            dropped.name = utf8Of(pathOf(dropped.args).stem());
        }
        ImGui::EndDragDropTarget();
    }
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const float mx = (mouse.x - pos.x) / fit, my = (mouse.y - pos.y) / fit; // preview pixels
    const bool overPicture = hovered && mx >= 0 && my >= 0 && mx < res.w && my < res.h;

    FrameInput input;
    static float lastX = 0, lastY = 0;
    if (m_interact && overPicture) {
        input.mouse = true;
        input.mouseX = mx, input.mouseY = my;
        input.mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        input.mousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        input.mouseMoved = mx != lastX || my != lastY;
        input.wheel = io.MouseWheel;
    }
    lastX = mx, lastY = my;
    m_ui->update(input, Viewport{0, 0, static_cast<f32>(res.w), static_cast<f32>(res.h)}, dt);
    for (const Action& a : m_ui->actions()) {
        m_actions.push_back(a.name + (a.node.empty() ? "" : "  (" + a.node + ")"));
        if (m_actions.size() > 50) m_actions.erase(m_actions.begin());
    }
    renderPreview(res.w, res.h);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddImage(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_target)), pos, ImVec2(pos.x + size.x, pos.y + size.y));

    // Logical (board) units ↔ panel: screen pixel = origin + logical × scale
    const ScreenInfo info = m_ui->info(m_screen);
    const float unit = std::max(info.scale, 1e-4f);
    auto toPanel = [&](float x, float y) { return ImVec2(pos.x + (info.originX + x * unit) * fit, pos.y + (info.originY + y * unit) * fit); };
    const float lx = (mx - info.originX) / unit, ly = (my - info.originY) / unit;
    if (!dropped.kind.empty() && !m_source.exported && m_sourceError.empty()) dropNode(std::move(dropped), lx, ly);

    // Zoom at the mouse (wheel), pan with the middle button or Space + left, F fits again
    if (hovered && !m_interact && io.MouseWheel != 0.0f) {
        const float old = m_zoom;
        m_zoom = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), 0.1f, 16.0f);
        const float k = m_zoom / old;
        const ImVec2 c(cursor.x + avail.x * 0.5f + m_panX, cursor.y + avail.y * 0.5f + m_panY); // picture center
        m_panX += (mouse.x - c.x) * (1.0f - k);
        m_panY += (mouse.y - c.y) * (1.0f - k);
    }
    if (hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) m_zoom = 1.0f, m_panX = m_panY = 0.0f;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) startDrag(3, 0, mouse.x, mouse.y);

    // Selected box and its handles
    NodeBox sel{};
    const bool hasBox = selectedBox(sel);
    const bool handles = hasBox && !m_interact && !m_selected.empty() && !m_source.exported;
    ImVec2 a, b;
    int hoverHandle = 0;
    if (hasBox) {
        a = toPanel(sel.x, sel.y), b = toPanel(sel.x + sel.w, sel.y + sel.h);
        if (handles && hovered)
            for (int h : kHandles) {
                const ImVec2 p = handlePoint(h, a, b);
                if (std::fabs(mouse.x - p.x) <= 6 && std::fabs(mouse.y - p.y) <= 6) {
                    hoverHandle = h;
                    break;
                }
            }
    }
    const int hoverNode = !m_interact && overPicture && !hoverHandle && m_drag == 0 ? pickNode(lx, ly) : -1;
    const bool mapped = !m_docPaths.empty();

    if (!m_interact && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (ImGui::IsKeyDown(ImGuiKey_Space)) startDrag(3, 0, mouse.x, mouse.y);
        else if (hoverHandle) startDrag(2, hoverHandle, mouse.x, mouse.y);
        else if (hoverNode >= 0 && mapped && static_cast<size_t>(hoverNode) < m_docPaths.size()) {
            m_selected = m_docPaths[hoverNode], m_hasSelection = true;
            startDrag(1, 0, mouse.x, mouse.y);
        } else m_hasSelection = false;
    }
    if (m_drag) {
        const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const float dx = mouse.x - m_dragStart[0], dy = mouse.y - m_dragStart[1];
        if (m_drag == 3) m_panX += io.MouseDelta.x, m_panY += io.MouseDelta.y;
        else {
            if (!m_dragArmed && (std::fabs(dx) > 3 || std::fabs(dy) > 3)) m_dragArmed = true;
            if (m_dragArmed && down) applyDrag(dx / (fit * unit), dy / (fit * unit), io.KeyShift);
        }
        if (!down) {
            if (m_drag != 3 && m_dragBefore != m_text) { // the whole drag is one undo step
                m_undo.push_back(m_dragBefore);
                m_redo.clear();
            }
            m_drag = 0;
        }
    }

    for (const NodeBox& nb : m_ui->boxes(m_screen)) {
        if (!nb.visible) continue;
        if (static_cast<int>(nb.node) == hoverNode && !(hasBox && nb.node == sel.node))
            draw->AddRect(toPanel(nb.x, nb.y), toPanel(nb.x + nb.w, nb.y + nb.h), IM_COL32(255, 255, 255, 110));
        if (nb.overflow) draw->AddRect(toPanel(nb.x, nb.y), toPanel(nb.x + nb.w, nb.y + nb.h), IM_COL32(255, 120, 60, 200));
    }
    if (hasBox) {
        draw->AddRect(a, b, IM_COL32(80, 170, 255, 255), 0, 0, 2.0f);
        if (handles)
            for (int h : kHandles) {
                const ImVec2 p = handlePoint(h, a, b);
                const bool hot = h == hoverHandle || (m_drag == 2 && h == m_handle);
                draw->AddRectFilled(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), hot ? IM_COL32(80, 170, 255, 255) : IM_COL32(255, 255, 255, 255));
                draw->AddRect(ImVec2(p.x - 4, p.y - 4), ImVec2(p.x + 4, p.y + 4), IM_COL32(30, 90, 170, 255));
            }
        if (const SourceNode* n = findNode(m_source, m_selected)) {
            const std::string label = n->name + "  " + whole(sel.w) + " x " + whole(sel.h);
            draw->AddText(ImVec2(a.x, b.y + 4), IM_COL32(80, 170, 255, 255), label.c_str());
        }
    }
    if (hoverHandle)
        ImGui::SetMouseCursor(hoverHandle == 1 || hoverHandle == 2   ? ImGuiMouseCursor_ResizeEW
                              : hoverHandle == 4 || hoverHandle == 8 ? ImGuiMouseCursor_ResizeNS
                              : hoverHandle == (1 | 4) || hoverHandle == (2 | 8) ? ImGuiMouseCursor_ResizeNWSE
                                                                                 : ImGuiMouseCursor_ResizeNESW);
    const std::string zoom = trf("%d%%   wheel zoom, middle drag pan, F fit, Shift snaps to 10", static_cast<int>(std::lround(fit * 100.0f)));
    draw->AddText(ImVec2(cursor.x + 8, cursor.y + avail.y - ImGui::GetTextLineHeight() - 6), IM_COL32(200, 200, 200, 150), zoom.c_str());
    ImGui::End();
}

} // namespace mkui::tool
