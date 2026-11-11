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

`mkui/Source.h` edits `.ui` text while keeping comments and order (what an editor uses).

## Build

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Added to another project with `add_subdirectory`, only the library is built. `MKUI_STB_DIR` and `MKUI_MKCONTRACT_DIR`
point to the including side's copies (defaults: `third_party/`). If an `mkcontract` target already exists it is used.

## Planned (MealKit ROADMAP "UI 2")

A draw backend interface, font/image loader hooks, a public API for view model values without engine reflection, then a
XAML-like layer: data binding (one-way/two-way, converters), styles and control templates, triggers and state
animations, resource dictionaries. Own format; no `.xaml` compatibility is promised. The Penpot plugin moves here too.

Third-party code in `third_party/`: stb_truetype (public domain / MIT), mkcontract (MIT).
