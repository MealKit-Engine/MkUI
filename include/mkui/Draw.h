#pragma once

#include "mkui/Types.h"
#include "mkui/Ui.h"

#include <cstddef>
#include <vector>

// Drawing what System::render() returns. Two ways (docs: README "Drawing"):
//   1. The quads as they are: one instanced draw of 6 vertices per Quad with shaders/mkui_quad.hlsl (per-pixel rounded corners,
//      borders, gradients, blurred shadows, SDF text). What MealKit does.
//   2. tessellate() into a plain triangle list for any 2D renderer: draw each DrawCommand with its texture and scissor rect,
//      blend SrcAlpha / OneMinusSrcAlpha (straight alpha). Edges are feathered over one pixel.
//      DrawTexture::Glyphs is the SDF glyph atlas (R8): alpha = smoothstep around 0.5, or an alpha test at 0.5, times the vertex color.
//      Approximations: shadows fall off linearly, radial gradients are interpolated across a fan, image brightness isn't applied.
namespace mkui {

enum class DrawTexture : u8 {
    None,   // vertex color only
    Glyphs, // glyph SDF atlas (AtlasKind::Glyphs, 1 byte per pixel, 0.5 = edge)
    Images, // image atlas (AtlasKind::Images, RGBA8), multiplied by the vertex color
};

struct DrawVertex {
    f32 x, y;  // back buffer pixels
    f32 u, v;  // atlas coordinates (0 when the command has no texture)
    u32 color; // RGBA8 like rgba(), straight alpha
};
static_assert(sizeof(DrawVertex) == 20, "DrawVertex is 20 bytes");

struct DrawCommand {
    DrawTexture texture = DrawTexture::None;
    u32 firstIndex = 0, indexCount = 0;
    f32 clip[4] = {}; // scissor x0, y0, x1, y1 in pixels (may be far outside the screen: clamp it)
};

struct DrawList {
    std::vector<DrawVertex> vertices;
    std::vector<u32> indices; // triangles, counter-clockwise or clockwise (draw without culling)
    std::vector<DrawCommand> commands;
    void clear() { vertices.clear(), indices.clear(), commands.clear(); } // keeps capacity: a steady frame allocates nothing
};

// Replaces out with the triangles of the quads (in order; consecutive quads with the same texture and clip share a command)
void tessellate(const Quad* quads, size_t count, DrawList& out);
inline void tessellate(const std::vector<Quad>& quads, DrawList& out) { tessellate(quads.data(), quads.size(), out); }

// What a host implements to draw. Override drawQuads to use the SDF shader, or only drawList to get triangles
class DrawBackend {
public:
    virtual ~DrawBackend() = default;
    virtual void drawQuads(const std::vector<Quad>& quads) {
        tessellate(quads, list_);
        drawList(list_);
    }
    virtual void drawList(const DrawList&) {}

private:
    DrawList list_;
};

// Lays out the frame and hands it to the backend (same as backend.drawQuads(ui.render()))
inline void draw(System& ui, DrawBackend& backend) { backend.drawQuads(ui.render()); }

} // namespace mkui
