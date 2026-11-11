#pragma once

#include "mkui/Types.h"
#include "mkui/Atlas.h"

#include <string>
#include <vector>

// UI text shaping (used only inside ui::System): UTF-8 → glyphs, style span tags (<1>…</1>), kerning, line breaking, ellipsis.
// Results are in logical units. Drawing is done by Ui.cpp.
namespace mkui {

struct Node;

struct TextRun { // style attached to a glyph
    i32 font = -1;
    f32 size = 24.0f;
    f32 letterSpacing = 0.0f;
    bool underline = false, strike = false;
    i32 span = -1; // Node::spans index (color), node color if none
};

struct ShapedGlyph {
    u32 cp = 0;
    u16 run = 0;
    i32 font = -1;
    f32 advance = 0.0f; // logical units (including letter spacing)
    Glyph glyph;
};

struct ShapedLine {
    u32 begin = 0, end = 0; // glyph range (spaces before end aren't counted in the width)
    f32 width = 0.0f;
    f32 ascent = 0.0f, descent = 0.0f, height = 0.0f;
};

struct Shaped {
    std::vector<TextRun> runs;
    std::vector<ShapedGlyph> glyphs;
    std::vector<ShapedLine> lines;
    f32 width = 0.0f, height = 0.0f;
    bool truncated = false;      // lines were dropped or an ellipsis cut the text (editor overflow warnings)
    std::vector<u32> codepoints; // decoded text (kept so reshaping allocates nothing once warm)
};

void decodeUtf8(const std::string& s, std::vector<u32>& out); // replaces out
u32 utf8Length(const std::string& s);                          // code point count
// Resolves tags (<1>…</1>) into styles and decides font/size/width per glyph
// size: the base font size (0 = the node's fontSize; fontSizeField passes its value)
void shapeGlyphs(const Node& d, const std::string& text, Atlas& atlas, Shaped& out, f32 size = 0.0f);
// Line breaking (across run boundaries in one pass). If wrapWidth < 0, only at newline characters. If maxHeight < 0, no height limit
void breakLines(const Node& d, Shaped& s, f32 wrapWidth, f32 maxHeight, Atlas& atlas);

} // namespace mkui
