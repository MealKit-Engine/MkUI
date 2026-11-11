#include "App.h"

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
        m_status = "No assets/ui folder in " + dir;
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
    return true;
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
        m_status = "Cannot read " + uiPath;
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
    textChanged();
    return true;
}

bool App::save() {
    if (m_file.empty()) return false;
    {
        std::ofstream out(m_host.assets / pathOf(m_file), std::ios::binary);
        out << m_text;
        if (!out) {
            m_status = "Cannot write " + m_file;
            m_statusTime = m_time;
            return false;
        }
    }
    m_savedText = m_text;
    std::error_code ec;
    m_fileTime = std::filesystem::last_write_time(m_host.assets / pathOf(m_file), ec);
    m_externalChange = false;
    m_ui->setDraft(m_screen, nullptr); // the file is the text again
    m_status = "Saved " + m_file;
    m_statusTime = m_time;
    return true;
}

void App::reloadFromDisk() {
    const std::string file = m_file;
    openFile(file);
    m_status = "Reloaded " + file + " (changed on disk)";
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
    ImGui::OpenPopup("Unsaved changes");
    return false;
}

// ---------------------------------------------------------------- Frame

void App::frame(double dt) {
    m_time += dt;
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
        drawPreview(dt);
    }
    drawUnsavedDialog();
    drawNameDialog();
}

void App::layoutDocks(unsigned dockspace) {
    if (ImGui::DockBuilderGetNode(dockspace) && ImGui::DockBuilderGetNode(dockspace)->IsSplitNode()) return; // saved layout
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);
    ImGuiID center = dockspace;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.32f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.2f, nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.6f, nullptr, &left);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);
    ImGui::DockBuilderDockWindow("Files", left);
    ImGui::DockBuilderDockWindow("Library", left);
    ImGui::DockBuilderDockWindow("Tree", leftBottom);
    ImGui::DockBuilderDockWindow("Properties", right);
    ImGui::DockBuilderDockWindow("Source", rightBottom);
    ImGui::DockBuilderDockWindow("Problems", bottom);
    ImGui::DockBuilderDockWindow("Preview", center);
    ImGui::DockBuilderFinish(dockspace);
}

void App::drawMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open project folder...")) SDL_ShowOpenFolderDialog(folderPicked, this, static_cast<SDL_Window*>(window), nullptr, false);
        if (ImGui::MenuItem("New screen...", nullptr, false, !m_project.empty())) m_nameDialog = 1;
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !m_file.empty())) save();
        if (ImGui::MenuItem("Reload from disk", nullptr, false, !m_file.empty())) reloadFromDisk();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty())) undo(false);
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty())) undo(true);
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
                    }
                ImGui::EndCombo();
            }
        }
        ImGui::Checkbox("Interact", &m_interact);
        ImGui::SetItemTooltip("On: the preview gets the mouse (buttons, sliders, inputs). Off: clicks select nodes");
    }
    if (!m_status.empty() && m_time - m_statusTime < 6.0) ImGui::TextDisabled("  %s", m_status.c_str());
    ImGui::EndMainMenuBar();
}

void App::drawStart() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::Begin("Start", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("MkUI Designer " MKUI_VERSION);
    ImGui::TextDisabled("Edits a project's assets/ui/*.ui. The game reads the same files.");
    ImGui::Spacing();
    if (ImGui::Button("Open project folder...")) SDL_ShowOpenFolderDialog(folderPicked, this, static_cast<SDL_Window*>(window), nullptr, false);
    if (!m_status.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", m_status.c_str());
    ImGui::End();
}

void App::drawFiles() {
    ImGui::Begin("Files");
    if (ImGui::SmallButton("New screen")) m_nameDialog = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) scanFiles();
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
    ImGui::Begin("Tree");
    if (m_file.empty()) ImGui::TextDisabled("Open a file");
    else if (m_source.root.children.empty()) ImGui::TextDisabled("No board");
    else {
        const bool editable = m_sourceError.empty() && !m_source.exported;
        ImGui::BeginDisabled(!editable);
        const SourceNode* sel = m_hasSelection ? findNode(m_source, m_selected) : nullptr;
        ImGui::BeginDisabled(!sel || m_selected.empty());
        if (ImGui::SmallButton("Delete")) {
            snapshot();
            removeNode(m_source, m_selected);
            m_hasSelection = false;
            sourceEdited();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!sel || sel->kind != "frame" || m_selected.empty());
        if (ImGui::SmallButton("Make component")) m_nameDialog = 2;
        ImGui::SetItemTooltip("Moves this frame into ui/components/<name>.ui and leaves a \"use\" here");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (sel && sel->kind == "use" && sel->args.rfind("ui/", 0) == 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit component")) {
                m_pendingFile = sel->args;
                if (guardUnsaved(2)) openFile(m_pendingFile);
            }
        }
        if (m_source.exported) ImGui::TextDisabled("Penpot export: read-only");
        ImGui::Separator();
        ImGui::BeginChild("tree");
        Path path;
        drawTreeNode(m_source.root.children[0], path);
        ImGui::EndChild();
    }
    ImGui::End();
}

void App::drawProperties() {
    ImGui::Begin("Properties");
    SourceNode* n = m_hasSelection && m_sourceError.empty() ? findNode(m_source, m_selected) : nullptr;
    if (!n) {
        ImGui::TextDisabled(m_sourceError.empty() ? "Select a node in the tree or the preview" : "Fix the source first");
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
    ImGui::Separator();
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
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) remove = static_cast<int>(i);
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
    if (n && ImGui::InputTextWithHint("##add", "add a line: fill #FF8800FF, on hover fill ...", m_newProperty, sizeof m_newProperty,
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
    ImGui::Begin("Source");
    if (m_file.empty()) {
        ImGui::TextDisabled("Open a file");
        ImGui::End();
        return;
    }
    if (m_externalChange) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Changed on disk.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload (drop my edits)")) reloadFromDisk();
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
    ImGui::Begin("Problems");
    if (!m_sourceError.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", m_sourceError.c_str());
    if (m_screen != kNoScreen)
        for (const std::string& e : m_ui->info(m_screen).errors) ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", e.c_str());
    for (const std::string& w : m_host.warnings) ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1), "%s", w.c_str());
    if (!m_actions.empty()) {
        ImGui::Separator();
        for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) ImGui::TextDisabled("action %s", it->c_str());
    }
    ImGui::End();
}

void App::drawUnsavedDialog() {
    if (!ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::Text("%s has unsaved changes.", m_file.c_str());
    auto proceed = [&] {
        const int action = m_pendingAction;
        m_pendingAction = 0;
        ImGui::CloseCurrentPopup();
        if (action == 1) m_quitConfirmed = true;
        else if (action == 2) openFile(m_pendingFile);
        else if (action == 3) openProject(m_pendingProject);
    };
    if (ImGui::Button("Save")) {
        if (save()) proceed();
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't save")) {
        m_savedText = m_text; // drop the edits
        proceed();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
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
    {"Panel", "A box that lines its children up in a column (padding, gap, rounded corners)",
     "frame panel\n  size 420 hug\n  layout column\n  gap 12\n  padding 24\n  fill #1B2130E6\n  radius 14\nend\n"},
    {"Text", "A line of text. {field} in it shows a view model value", "text label\n  content Text\n  fontSize 28\n  color #FFFFFF\nend\n"},
    {"Image", "A picture from assets (set src to a PNG path)", "image image\n  size 96 96\nend\n"},
    {"Button", "A button that sends an action when pressed",
     "frame button\n  size 280 60\n  layout row\n  justify center\n  align center\n  fill #3B6FE0\n  radius 10\n  button confirm\n"
     "  on hover fill #5585F0\n  on pressed fill #2C58B8\n  on focus stroke 3 #FFFFFF\n  text buttonLabel\n    content Button\n    fontSize 26\n"
     "    color #FFFFFF\n  end\nend\n"},
    {"Toggle", "An on/off switch for a bool field",
     "frame toggleRow\n  size hug hug\n  layout row\n  align center\n  gap 14\n  frame toggle\n    size 64 32\n    radius 16\n    fill #FFFFFF26\n"
     "    toggle enabled\n    on checked fill #3B6FE0\n    frame knobOff\n      pos 4 4\n      size 24 24\n      radius 12\n      fill #FFFFFF\n"
     "      visible !enabled\n    end\n    frame knobOn\n      pos 36 4\n      size 24 24\n      radius 12\n      fill #FFFFFF\n      visible enabled\n"
     "    end\n  end\n  text toggleLabel\n    content Option\n    fontSize 24\n    color #FFFFFF\n  end\nend\n"},
    {"Slider", "Drag to set a float field between a minimum and a maximum",
     "frame slider\n  size 320 28\n  radius 14\n  fill #FFFFFF26\n  clip\n  slider value01 0 1 0.01\n  on hover stroke 2 #FFFFFF40\n"
     "  frame sliderFill\n    size fill fill\n    widthRatio value01\n    radius 14\n    fill #3B6FE0\n  end\nend\n"},
    {"Progress bar", "A bar filled by a float field from 0 to 1 (health, loading)",
     "frame progress\n  size 320 20\n  radius 10\n  fill #FFFFFF1A\n  clip\n  frame progressFill\n    size fill fill\n    widthRatio progress\n"
     "    radius 10\n    fill #4CC38A\n  end\nend\n"},
    {"Text input", "A box the player types into (a string field)",
     "frame inputBox\n  size 360 52\n  layout row\n  align center\n  padding 0 16\n  fill #00000055\n  stroke 1 #FFFFFF33\n  radius 10\n"
     "  text input\n    size fill hug\n    textInput text\n    maxLength 32\n    fontSize 24\n    color #FFFFFF\n    maxLines 1\n  end\nend\n"},
    {"List", "Repeats its first child for every element of a list field",
     "frame list\n  size 420 hug\n  layout column\n  gap 6\n  list items\n  frame row\n    size fill 48\n    layout row\n    align center\n"
     "    padding 0 14\n    radius 8\n    fill #FFFFFF0A\n    text rowName\n      content {name}\n      fontSize 22\n      color #FFFFFF\n    end\n  end\nend\n"},
    {"Scroll area", "Clips its content and scrolls it with the wheel or the gamepad",
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

void App::insertNode(SourceNode node) {
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
    if (m_hasSelection) {
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
        m_status = asset + " exists already";
        m_statusTime = m_time;
        return false;
    }
    const std::string before = m_text;
    Source component;
    if (!extractComponent(m_source, m_selected, name, asset, static_cast<i32>(b.w + 0.5f), static_cast<i32>(b.h + 0.5f), component)) {
        m_status = "Only a frame can become a component";
        m_statusTime = m_time;
        textChanged(); // m_source back from the text
        return false;
    }
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << writeSource(component);
    if (!out) {
        m_status = "Cannot write " + asset;
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
    m_status = "Component " + asset + " (save the screen to keep the use)";
    m_statusTime = m_time;
    return true;
}

bool App::newScreen(const std::string& name) {
    if (m_text != m_savedText) {
        m_status = "Save the open screen first";
        m_statusTime = m_time;
        return false;
    }
    const std::string asset = "ui/" + name + ".ui";
    const std::filesystem::path file = m_host.assets / pathOf(asset);
    std::error_code ec;
    if (std::filesystem::exists(file, ec)) {
        m_status = asset + " exists already";
        m_statusTime = m_time;
        return false;
    }
    std::ofstream out(file, std::ios::binary);
    out << "# MkUI screen: " << name << "\n@format ui 1\nboard " << name << " 1920 1080\n";
    if (!out) {
        m_status = "Cannot write " + asset;
        m_statusTime = m_time;
        return false;
    }
    out.close();
    scanFiles();
    return openFile(asset);
}

void App::drawLibrary() {
    ImGui::Begin("Library");
    const bool editable = !m_file.empty() && m_sourceError.empty() && !m_source.exported;
    ImGui::BeginDisabled(!editable);
    ImGui::TextDisabled("Widgets");
    const float width = ImGui::GetContentRegionAvail().x;
    const float button = std::max(90.0f, (width - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    int column = 0;
    for (const Widget& w : kWidgets) {
        if (column++ % 2 && width >= button * 2) ImGui::SameLine();
        if (ImGui::Button(w.label, ImVec2(button, 0))) {
            SourceNode n;
            if (parseNodeText(w.text, n)) insertNode(std::move(n));
        }
        ImGui::SetItemTooltip("%s", w.tooltip);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Components (ui/components)");
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
        ImGui::SetItemTooltip("Adds \"use %s %s\": the component's look, placed here", base.c_str(), f.c_str());
    }
    if (!any) ImGui::TextDisabled("  none yet: select a frame, then Make component");
    ImGui::EndDisabled();
    ImGui::End();
}

void App::drawNameDialog() {
    if (m_nameDialog) {
        ImGui::OpenPopup("Name");
        if (m_nameDialog == 2 && m_hasSelection)
            if (const SourceNode* n = findNode(m_source, m_selected)) std::snprintf(m_nameInput, sizeof m_nameInput, "%s", n->name.c_str());
        if (m_nameDialog == 1) m_nameInput[0] = 0;
    }
    static int kind = 0;
    if (m_nameDialog) kind = m_nameDialog, m_nameDialog = 0;
    if (!ImGui::BeginPopupModal("Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(kind == 1 ? "New screen in assets/ui/ (letters, digits, _ and -)" : "Component file in assets/ui/components/");
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", m_nameInput, sizeof m_nameInput, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool ok = validName(m_nameInput);
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Create") || (enter && ok)) {
        if (kind == 1) newScreen(m_nameInput);
        else makeComponent(m_nameInput);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
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

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

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
    ImGui::Begin("Preview");
    ImGui::PopStyleVar();
    if (m_screen == kNoScreen) {
        ImGui::TextDisabled("  Pick a .ui file on the left");
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
    char zoom[80];
    std::snprintf(zoom, sizeof zoom, "%d%%   wheel zoom, middle drag pan, F fit, Shift snaps to 10", static_cast<int>(std::lround(fit * 100.0f)));
    draw->AddText(ImVec2(cursor.x + 8, cursor.y + avail.y - ImGui::GetTextLineHeight() - 6), IM_COL32(200, 200, 200, 150), zoom);
    ImGui::End();
}

} // namespace mkui::tool
