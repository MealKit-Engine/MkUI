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

## Build

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Added to another project with `add_subdirectory`, only the library is built. `MKUI_STB_DIR` and `MKUI_MKCONTRACT_DIR`
point to the including side's copies (defaults: `third_party/`). If an `mkcontract` target already exists it is used.

## Planned (MealKit ROADMAP "UI 2")

A XAML-like layer: data binding (one-way/two-way, converters), styles and control templates, triggers and state
animations, resource dictionaries. Own format; no `.xaml` compatibility is promised. The Penpot plugin moves here too.

Third-party code in `third_party/`: stb_truetype (public domain / MIT), mkcontract (MIT).
