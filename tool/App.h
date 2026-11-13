#pragma once

#include "PenpotServer.h"
#include "mkui/Document.h"
#include "mkui/Draw.h"
#include "mkui/Host.h"
#include "mkui/Source.h"
#include "mkui/Ui.h"
#include "mkui/Values.h"

#include <mkcontract/Client.h>
#include <mkcontract/Json.h>

#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

// MkUI Designer: edits a project's assets/ui/*.ui (the files the game reads as they are) without the engine. Preview through
// mkui::System + mkui::tessellate on SDL_Renderer, edits through mkui/Source.h (comments and order stay).
namespace mkui::tool {

// Files from <project>/assets, strings from assets/strings/<language>.txt, atlases kept on the CPU and uploaded to textures
class DesignerHost : public Host {
public:
    std::filesystem::path assets;
    std::map<std::string, std::string> strings; // key → text of the chosen language
    std::vector<std::string> warnings;          // log lines since the screen was opened (shown in Problems)
    bool quiet = false;                         // drops log lines (the editor's own parse of the text on every edit)
    std::vector<u8> glyphs, images;             // kAtlasSize² (R8) and kAtlasSize² × 4 (RGBA8)
    int dirty[2][4] = {{1 << 30, 1 << 30, -1, -1}, {1 << 30, 1 << 30, -1, -1}}; // changed region per atlas: x0 y0 x1 y1

    DesignerHost();
    bool readFile(const std::string& path, std::string& out) override;
    u64 fileTime(const std::string& path) override;
    std::vector<std::string> listFiles(const std::string& folder, const char* extension) override;
    void log(LogLevel level, const std::string& message) override;
    const std::string& text(const std::string& key) override;
    bool decodeImage(const std::string& bytes, u32& width, u32& height, std::vector<u8>& rgba) override;
    void uploadAtlas(AtlasKind atlas, u32 x, u32 y, u32 w, u32 h, const u8* pixels) override;
    bool loadStrings(const std::string& language); // false if the file is missing (keys are shown)
};

class App {
public:
    struct FakeField {   // a view-model field the open screen uses
        std::string path; // "score", "stats.hp"; relative to the row for fields under a list
        std::string list; // the list field whose rows hold it (empty: the screen's model)
        ValueKind kind;
    };
    bool init(SDL_Renderer* renderer, const std::string& projectDir, const std::string& file, int resolution);
    void shutdown();
    void frame(double dt); // between ImGui NewFrame and Render
    void requestQuit() { m_quitAsked = true; }
    bool canQuit() const { return m_quitConfirmed; }
    std::string title() const;
    void setPendingProject(const std::string& dir); // from the folder dialog thread
    bool selectByName(const std::string& name);     // first node with this name (command line --select)
    void setZoom(float zoom) { m_zoom = zoom; }
    void showWindow(const std::string& name) { m_showWindow = name; } // brings a docked tab forward on the next frame
    void followFirstGame() { m_followFirst = true; }                 // follow the first running game found (--follow)
    void* window = nullptr;                          // SDL_Window* (dialog parent)
    bool iconFont = false; // the Segoe icon font merged into the UI font (main.cpp loadFont)
    std::string languageOverride; // --lang: used instead of the suite language, which isn't followed then

private:
    // Flat square icon button with its name in the tooltip (engine docs/TOOL_DESIGN.md section 1); the name as the label without the icon font
    bool iconButton(const char* glyph, const char* name, bool small = false);
    // Language icon at the right end of the menu bar: the languages by their own names (engine docs/TOOL_DESIGN.md section 4 "Language")
    void drawLanguageButton();
    // Panel tab title "glyph  Name###Name" (engine docs/TOOL_DESIGN.md section 1 "Panel tabs"): the id after ### stays the same in every
    // language and without the icon font, so the saved dock layout keeps working
    std::string panelTitle(const char* glyph, const char* name) const;
    using Path = SourcePath;

    bool openProject(const std::string& dir);
    void startPenpot(); // Penpot plugin import server for the open project
    void scanFiles();
    bool openFile(const std::string& uiPath); // "ui/menu.ui"
    bool save();
    void reloadFromDisk();
    void checkExternalChange();
    void textChanged();  // m_text was edited: parse it again, give it to the preview
    void sourceEdited(); // m_source was edited: write it into m_text
    void snapshot();
    void undo(bool redo);
    bool guardUnsaved(int next); // true if there's nothing to lose; otherwise asks and remembers what to do next

    void drawMenu();
    void drawStart();
    void drawFiles();
    void drawPreview(double dt);
    void drawTree();
    void drawLibrary();
    void drawNameDialog();
    // parent null: into the selected container, else after the selection, else at the end of the board.
    // parent given: last child of it, at pos (logical, inside parent) when its children are free
    void insertNode(SourceNode node, const Path* parent = nullptr, const float* pos = nullptr);
    void dropNode(SourceNode node, float lx, float ly); // library item dropped on the preview at board point lx, ly
    bool drawInspector(SourceNode& node);               // true if it changed node
    bool makeComponent(const std::string& name);
    bool newScreen(const std::string& name);
    void drawTreeNode(const SourceNode& node, Path& path);
    void drawProperties();
    void drawText();
    void drawProblems();
    void drawValues();
    void drawLive();
    bool takeLiveValues(const mkcontract::live::Instance& from, bool quiet); // the game's view model of this screen → m_values
    void collectFields(); // after each parse: the screen's bound fields; rebuilds the model when they change
    void buildModel();
    void loadValues();
    void saveValues();
    std::filesystem::path valuesPath() const;
    std::string valueKey(const FakeField& f, int row) const;
    const std::string& value(const std::string& key, const FakeField& f); // stored or default
    void drawUnsavedDialog();
    void layoutDocks(unsigned dockspace);

    void renderPreview(int width, int height);
    void uploadAtlases();
    int pickNode(float x, float y) const; // Document node under a logical point, -1 if none
    bool selectedBox(NodeBox& out) const;  // drawn box of the selected source node (the first one: list rows repeat)
    void startDrag(int kind, int handle, float mx, float my);
    void applyDrag(float dx, float dy, bool fine); // logical units since the drag started

    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_target = nullptr;
    SDL_Texture* m_glyphTexture = nullptr;
    SDL_Texture* m_imageTexture = nullptr;
    int m_targetW = 0, m_targetH = 0;
    std::vector<u8> m_glyphRgba; // glyph SDF turned into white with alpha (SDL_Renderer has no SDF shader)

    DesignerHost m_host;
    std::unique_ptr<System> m_ui;
    ScreenId m_screen{};
    DrawList m_list;
    ValueModel m_model; // fake view model built from m_fields and m_values
    std::vector<FakeField> m_fields;
    std::map<std::string, std::string> m_values; // "score" = "12", "items" = "3", "items[0].name" = "Ann"
    bool m_valuesOn = true;
    std::vector<mkcontract::live::Instance> m_live; // running games/editors of this project
    std::vector<std::string> m_liveScreens;         // per instance: its open screens ("ui/hud.ui (HudVm)")
    double m_liveCheck = -10.0, m_followTime = 0.0;
    int m_follow = -1; // instance whose values are taken every half second
    std::string m_liveStatus;
    PenpotServer m_penpot;
    std::string m_showWindow;
    bool m_followFirst = false;

    std::string m_project; // UTF-8
    std::vector<std::string> m_files; // "ui/menu.ui", "ui/components/x.ui"
    std::vector<std::string> m_languages;
    std::string m_language;
    std::string m_file;       // open file (asset path), empty if none
    std::string m_text;       // what the editor holds
    std::string m_savedText;  // contents last read or written
    std::filesystem::file_time_type m_fileTime{};
    double m_fileCheck = 0.0;
    bool m_externalChange = false;

    Source m_source;          // last text that parsed
    std::string m_sourceError;
    Document m_doc;           // m_text as the runtime reads it (for node boxes ↔ source paths)
    std::vector<Path> m_docPaths;
    Path m_selected;
    bool m_hasSelection = false;
    char m_newProperty[128] = {};
    bool m_inspectorActive = false;                      // an inspector edit is under way (its undo step is pushed)
    std::vector<std::string> m_imageFiles, m_fontFiles; // pickers: assets/ui + assets/textures pictures, assets/fonts
    double m_assetListTime = -10.0;
    int m_nameDialog = 0; // 1 new screen, 2 make component (popup asking for a name)
    char m_nameInput[64] = {};

    std::deque<std::string> m_undo, m_redo;
    std::string m_lastSnapshot;
    bool m_textActive = false;

    int m_resolution = 0;     // index into the presets in App.cpp
    bool m_interact = false;
    float m_zoom = 1.0f, m_panX = 0.0f, m_panY = 0.0f; // 1 = the resolution fits the panel; pan in panel pixels
    int m_drag = 0;          // 0 none, 1 move, 2 resize, 3 pan
    int m_handle = 0;        // resize: 1 left 2 right 4 top 8 bottom
    bool m_dragArmed = false; // a move starts only after the mouse went a few pixels
    float m_dragStart[2] = {}, m_startPos[2] = {}, m_startSize[2] = {};
    std::string m_dragBefore; // text before the drag (one undo step)  // preview gets the mouse (buttons, sliders) instead of selecting nodes
    std::vector<std::string> m_actions; // actions fired in the preview (newest last)
    std::string m_status;
    double m_statusTime = 0.0, m_time = 0.0;

    bool m_layoutDone = false;
    bool m_quitAsked = false, m_quitConfirmed = false;
    int m_pendingAction = 0; // after the unsaved dialog: 1 quit, 2 open m_pendingFile, 3 open m_pendingProject
    std::string m_pendingFile;
    std::string m_pendingProject;
    std::string m_dialogProject;
    std::mutex m_pendingMutex;
};

} // namespace mkui::tool
