# MkUI

Game screen UI runtime, part of the [MealKit](https://github.com/MealKit-Engine/MealKit) tool family. Free, MIT.

> **Beta (v0.x).** APIs and the `.ui` format may change between versions until v1.0. Feedback and bug reports are welcome.

Screens are text files (`.ui`, `@format ui 1`) that merge in git: a board with frames, text and images, Flex/Grid layout,
multi-style text with SDF glyphs, SDF shapes (radii, strokes, gradients, shadows), widgets (buttons, toggles, sliders,
selects, lists, scroll, text inputs), keyboard/gamepad focus, themes, components and world-space UI. Values are bound one
way to view models through a `Binder` the host implements; input widgets write back and fire actions.

The library includes no engine header. The host implements `mkui::Host` (files, log, string table, image decoding,
atlas upload) and `mkui::Binder`, and draws the `mkui::Quad` list `System::render()` returns (112 bytes each, one
instanced draw with the two atlases):

```cpp
mkui::System ui(host, binder);
mkui::ScreenId menu = ui.open("ui/menu.ui");
ui.bind(menu, {&menuVmType, &menuVm});
// every frame
ui.update(input, viewport, dt);
for (const mkui::Action& a : ui.actions()) { /* "menu.start" ... */ }
draw(ui.render());
```

## Drawing

Two ways (`mkui/Draw.h`):

- **SDF quads** (what MealKit does): one instanced draw of 6 vertices per `mkui::Quad` with the reference shader
  `shaders/mkui_quad.hlsl` (HLSL; DXC also compiles it to SPIR-V). Per-pixel rounded corners, borders, gradients, blurred
  shadows and SDF text.
- **Triangles** for any 2D renderer: `mkui::tessellate(ui.render(), list)` gives vertices (position, atlas uv, straight-alpha
  RGBA8), u32 indices and commands per texture (none, glyph SDF atlas, image atlas) and clip rect. Blend SrcAlpha /
  OneMinusSrcAlpha; for glyph commands take alpha = smoothstep around 0.5 of the atlas value (or an alpha test at 0.5).
  Edges are feathered over one pixel; shadows fall off linearly and image brightness isn't applied.

Or derive from `mkui::DrawBackend` and override `drawQuads` (SDF) or only `drawList` (triangles), then `mkui::draw(ui, backend)`.

## View models without reflection

`mkui::ValueModel` holds named values, nested models and lists; make the `System` with `mkui::valueBinder()`:

```cpp
mkui::ValueModel hud;
hud.set("score", 120);
hud.child("stats").set("hp", 0.8f);   // "stats.hp"
hud.list("players").resize(3);        // "list players", each row a ValueModel
mkui::System ui(host, mkui::valueBinder());
ui.bind(ui.open("ui/hud.ui"), hud.model());
```

Engines with their own reflection implement `mkui::Binder` instead (MealKit binds `MEALKIT_VIEWMODEL` structs that way).

`mkui/Source.h` edits `.ui` text while keeping comments and order (what an editor uses).

## MkUI Designer

`tool/` builds the **MkUI** program (SDL3 + Dear ImGui), which edits a project's `assets/ui/*.ui` without an engine: the
files the game reads as they are.

- **Files** lists `assets/ui` (and its subfolders); **Source** is the text with comments, **Tree** and **Properties** edit
  nodes and property lines through `mkui/Source.h`, so comments and order stay. Unsaved edits show in the preview at once.
- **Preview** draws through `mkui::tessellate` on SDL_Renderer at a chosen resolution, with the project's fonts, images,
  theme (`ui/theme.ui`) and strings (`assets/strings/<language>.txt`). Wheel zooms at the mouse, the middle button (or Space + drag) pans, F fits. A click selects the node under it; dragging moves a node that has a number `pos` (layout children follow their parent), the eight handles resize it (only the dragged axes become numbers), Shift snaps to 10 units, one drag is one undo step. With
  **Interact** on, buttons, sliders and inputs work and their actions are listed in **Problems**. Orange boxes are cut text.
- **Library** adds ready-made widgets (panel, text, image, button, toggle, slider, progress bar, text input, list, scroll
  area: the same set as the MealKit editor) and the project's components (`use` nodes) into the selected container or
  after the selection. **Make component** moves a selected frame into `ui/components/<name>.ui` and leaves a `use`;
  **Edit component** opens the file behind a `use`. **New screen** writes `ui/<name>.ui` with an empty 1920×1080 board.
- **Values** fills the screen with fake view-model values (a `mkui::ValueModel` bound to it): the fields come from the
  screen itself (bindings, `{field}` in texts, widget fields) with a guessed kind, fields under a list belong to its rows,
  and the row count is a slider. Defaults: bools on, numbers 0.6, texts the field's name, lists 3 rows. Kept in
  `<project>/art/ui/samples/<screen>.values` (not shipped); "Use these values" off shows the screen unbound.
- **From a running game** (bottom of Values): a game or editor of the same project started with the live connection
  (MealKit dev builds, engine `ui/` service) is listed with its open screens; **Take values** copies the view model it has
  bound to this screen, **Follow** keeps copying twice a second. Saved `.ui` files reload in the running game by themselves.
- A file changed on disk reloads by itself when there are no edits. Ctrl+S saves, Ctrl+Z / Ctrl+Y undo and redo.
- Next (taking over the MealKit editor's UI workspace): the editor's inspector rows and canvas drop, then the editor's UI workspace goes; the Penpot plugin.

```
MkUI [--project <project folder>] [--file ui/menu.ui] [--select <node>] [--zoom 1.5] [--show Values] [--follow] [--resolution 0-4] [--frames N --capture shot.png]
```

The preview is close to the engine's SDF look but not the same (linear shadow falloff, image brightness not applied, a
fixed glyph edge). The MealKit editor lists the program in its **Tools** menu when `MkUI` and `mealkit-tool.txt` (both in
`build/bin`) are in `%LOCALAPPDATA%\MealKit\tools\ui\`, or with `--tool-path build/bin`. `-DMKUI_BUILD_TOOL=OFF` skips it.

## Build

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Added to another project with `add_subdirectory`, only the library is built (no tests, no Designer). `MKUI_STB_DIR` and `MKUI_MKCONTRACT_DIR`
point to the including side's copies (defaults: `third_party/`). If an `mkcontract` target already exists it is used.

## Planned (MealKit ROADMAP "UI 2")

A XAML-like layer: data binding (one-way/two-way, converters), styles and control templates, triggers and state
animations, resource dictionaries. Own format; no `.xaml` compatibility is promised. The Penpot plugin moves here too, next to the Designer.

Third-party code in `third_party/`: stb_truetype (public domain / MIT), mkcontract (MIT); for the Designer only, SDL 3.4.18 (Zlib) and Dear ImGui v1.92.9b-docking (MIT).
