#pragma once

#include "mkui/Host.h"
#include "mkui/Types.h"

#include <string>
#include <vector>

// Game screen UI runtime: screens (.ui, mkui/Document.h) laid out in retained mode, bound one way to view models, output as a list of
// 2D quads per frame. The host gives files/strings/images (Host), reads view model fields (Binder) and draws the quads.
//   Coordinates are logical units of the board (reference 1920×1080), scale = min(width/board width, height/board height) × UI scale.
//   Rereads when files (.ui, theme, fonts, images) change. Controls are the mouse plus ui.up/down/left/right/confirm/back (keyboard/gamepad).
//   Input widgets (toggles/sliders/selects/text inputs) write values to their own view model fields and fire actions.
// A steady frame allocates nothing (bound text is built into buffers that keep their capacity).
namespace mkui {

using ScreenId = u32;
inline constexpr ScreenId kNoScreen = 0;

struct Action {
    std::string name;   // button action in the .ui (input widgets: the given action or "changed")
    ScreenId screen = kNoScreen;
    std::string node;   // name of the node that fired it
    std::string field;  // input widgets: the changed view model field
    i32 listIndex = -1; // inside a list, which item it is
    bool dragging = false; // dragging a slider with the mouse (fires once more with dragging=false on release)
};

enum class State : u8; // mkui/Document.h

struct FrameInput {
    bool mouse = false; // mouse values below are valid (the game may have taken the mouse)
    f32 mouseX = 0, mouseY = 0; // pixels
    bool mouseDown = false, mousePressed = false, mouseMoved = false;
    f32 wheel = 0.0f; // notches (+ = up)
    bool typing = false; // keyboard text below is valid (text inputs)
    const std::string* text = nullptr; // UTF-8 typed this frame
    u32 backspaces = 0;
    bool enter = false, escape = false;
    bool navigation = false; // ui.* actions below are valid (keyboard/gamepad)
    bool up = false, down = false, left = false, right = false, confirm = false, back = false;
};

struct Viewport {
    f32 x = 0, y = 0, width = 1, height = 1; // pixels
};

// One 2D UI rectangle in back buffer pixels (112 bytes, the engine's render::UiQuad and HLSL UiQuad layout)
struct Quad {
    f32 rect[4] = {};                         // x, y, w, h (box before rotation)
    f32 clip[4] = {-1e9f, -1e9f, 1e9f, 1e9f}; // x0, y0, x1, y1 (nothing drawn outside)
    f32 radii[4] = {};                        // top left, top right, bottom right, bottom left
    f32 uv[4] = {};                           // atlas u0, v0, u1, v1 (images/text)
    f32 gradient[4] = {};                     // linear: start xy, end xy / radial: center xy, radius xy (box 0..1 coordinates)
    u32 color0 = 0;                           // sRGB RGBA8 (rgba()), straight alpha
    u32 color1 = 0;
    u32 strokeColor = 0;
    u32 flags = 0; // kQuadShape/Image/Glyph/Shadow | kQuadLinear/kQuadRadial
    f32 strokeWidth = 0.0f;
    f32 rotation = 0.0f; // radians, around the box center
    f32 softness = 0.0f; // shadow blur (pixels)
    f32 sdfScale = 0.0f; // text: screen pixels per SDF value unit
};
static_assert(sizeof(Quad) == 112, "Quad must match the HLSL layout");
inline constexpr u32 kQuadShape = 0, kQuadImage = 1, kQuadGlyph = 2, kQuadShadow = 3;
inline constexpr u32 kQuadLinear = 1u << 4, kQuadRadial = 2u << 4;

struct ScreenInfo {
    std::string path;
    u32 nodes = 0;         // expanded node count (including list items, budget 500)
    u32 visibleNodes = 0;
    std::vector<std::string> errors; // file errors, binding errors (missing fields), missing fonts/images
    bool exported = false;
    f32 originX = 0, originY = 0, scale = 1; // screen pixel = origin + logical × scale (last layout)
    std::string focus; // name of the focused node (empty if none)
};

struct NodeBox {
    std::string name;
    std::string id; // Penpot shape id (exported files only)
    f32 x, y, w, h; // logical units (relative to the board's top left)
    u32 depth;
    bool visible;
    bool text; // text node (layout comparison is looser by font differences)
    u32 node;  // index in the screen's Document::nodes (list rows repeat their template's index)
    bool overflow = false; // text node: the text is cut (maxLines, ellipsis) or larger than its box (editor warnings)
};

class System {
public:
    // host and binder must outlive the system
    System(Host& host, const Binder& binder);
    ~System();
    System(const System&) = delete;
    System& operator=(const System&) = delete;

    // Rereads the theme, fonts and every open screen and clears the atlases (the host's strings/atlas target changed)
    void reload();

    ScreenId open(const std::string& uiPath, i32 layer = 0);
    ScreenId openWorld(const std::string& uiPath);
    void close(ScreenId screen);
    bool isOpen(ScreenId screen) const;
    void closeAll();
    void bind(ScreenId screen, Model viewModel); // must live while the screen is open
    void setScale(f32 uiScale);
    f32 scale() const;

    void update(const FrameInput& in, const Viewport& viewport, f64 dt);
    const std::vector<Action>& actions() const;
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // viewProj: row-major m[row][column], points are row vectors (p × M)
    void setCamera(const f32 viewProj[16]);
    void drawAt(ScreenId templateScreen, f32 x, f32 y, f32 z, Model viewModel, bool clampToEdges = false);

    // Lays out and returns this frame's quads (valid until the next call)
    const std::vector<Quad>& render();
    void layout(const Viewport& viewport);

    ScreenInfo info(ScreenId screen) const;
    std::vector<ScreenId> screens() const; // open screens (editor: the game's screens while playing), world templates included
    Model model(ScreenId screen) const;    // the bound view model (empty if none)
    std::vector<NodeBox> boxes(ScreenId screen) const;
    // Editor: show this .ui text instead of the screen's file (unsaved edits); nullptr goes back to the file
    void setDraft(ScreenId screen, const std::string* text);
    // Editor preview: this Document node shows this state as if it were real (node < 0: none)
    void setPreviewState(ScreenId screen, i32 node, State state);
    // Editor preview: one keyboard/gamepad move (0 up, 1 down, 2 left, 3 right), same as the ui.* actions
    void moveFocus(ScreenId screen, int direction);
    void setFocusVisible(bool always);
    u32 quadCount() const;
    bool press(ScreenId screen, const std::string& nodeName);
    bool pressAny(const std::string& nodeName);
    bool focusText(ScreenId screen, const std::string& nodeName);
    bool editingText() const;

private:
    struct Impl;
    Impl* m;
};

} // namespace mkui
