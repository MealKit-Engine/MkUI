#include "mkui/Draw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mkui {
namespace {

struct Color {
    f32 r, g, b, a;
};

Color unpack(u32 c) {
    return {static_cast<f32>(c & 0xFF) / 255.0f, static_cast<f32>((c >> 8) & 0xFF) / 255.0f, static_cast<f32>((c >> 16) & 0xFF) / 255.0f,
            static_cast<f32>(c >> 24) / 255.0f};
}

u32 pack(const Color& c, f32 alphaScale) {
    auto byte = [](f32 v) { return static_cast<u32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a * alphaScale) << 24);
}

Color lerp(const Color& a, const Color& b, f32 t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

constexpr f32 kPi = 3.14159265358979f;

// Builds one quad's triangles into a DrawList (local coordinates = pixels from the box center, before rotation)
struct Builder {
    const Quad& q;
    DrawList& out;
    f32 halfW, halfH, sinR, cosR;
    u32 segments; // points per corner arc minus one
    bool textured;
    f32 ringX[4 * 13], ringY[4 * 13];
    u32 ringCount = 0;

    Builder(const Quad& quad, DrawList& list, bool tex) : q(quad), out(list), textured(tex) {
        halfW = q.rect[2] * 0.5f;
        halfH = q.rect[3] * 0.5f;
        sinR = std::sin(q.rotation);
        cosR = std::cos(q.rotation);
        f32 maxRadius = 0.0f;
        for (f32 r : q.radii) maxRadius = std::max(maxRadius, r);
        maxRadius = std::min(maxRadius, std::min(halfW, halfH));
        segments = maxRadius <= 0.0f ? 0u : std::clamp(static_cast<u32>(std::ceil(std::sqrt(maxRadius) * 1.5f)), 1u, 12u);
    }

    // Points of the rounded box grown by delta pixels (negative = inset), clockwise on screen from the top left corner
    void ring(f32 delta) {
        const f32 hx = std::max(halfW + delta, 0.0f), hy = std::max(halfH + delta, 0.0f);
        const f32 baseLimit = std::min(halfW, halfH);
        static const f32 cx[4] = {-1, 1, 1, -1}, cy[4] = {-1, -1, 1, 1};
        static const f32 start[4] = {kPi, kPi * 1.5f, 0.0f, kPi * 0.5f};
        ringCount = 0;
        for (int c = 0; c < 4; ++c) {
            f32 r = std::max(std::min(q.radii[c], baseLimit) + delta, 0.0f);
            r = std::min(r, std::min(hx, hy));
            const f32 ox = cx[c] * (hx - r), oy = cy[c] * (hy - r);
            for (u32 i = 0; i <= segments; ++i) {
                const f32 a = start[c] + (segments ? kPi * 0.5f * static_cast<f32>(i) / static_cast<f32>(segments) : 0.0f);
                ringX[ringCount] = ox + std::cos(a) * r;
                ringY[ringCount] = oy + std::sin(a) * r;
                ++ringCount;
            }
        }
    }

    Color fillAt(f32 lx, f32 ly) const {
        const Color c0 = unpack(q.color0);
        const u32 kind = q.flags & 0xF;
        if (kind != kQuadShape) return c0;
        const u32 paint = (q.flags >> 4) & 0x3;
        if (paint == 0) return c0;
        const f32 bx = (lx + halfW) / std::max(q.rect[2], 1e-3f), by = (ly + halfH) / std::max(q.rect[3], 1e-3f);
        f32 t;
        if (paint == 1) {
            const f32 ax = q.gradient[0], ay = q.gradient[1], dx = q.gradient[2] - ax, dy = q.gradient[3] - ay;
            t = ((bx - ax) * dx + (by - ay) * dy) / std::max(dx * dx + dy * dy, 1e-6f);
        } else {
            const f32 dx = (bx - q.gradient[0]) / std::max(q.gradient[2], 1e-4f), dy = (by - q.gradient[1]) / std::max(q.gradient[3], 1e-4f);
            t = std::sqrt(dx * dx + dy * dy);
        }
        return lerp(c0, unpack(q.color1), std::clamp(t, 0.0f, 1.0f));
    }

    u32 vertex(f32 lx, f32 ly, u32 color) {
        DrawVertex v;
        v.x = q.rect[0] + halfW + lx * cosR - ly * sinR;
        v.y = q.rect[1] + halfH + lx * sinR + ly * cosR;
        if (textured) {
            const f32 bx = (lx + halfW) / std::max(q.rect[2], 1e-3f), by = (ly + halfH) / std::max(q.rect[3], 1e-3f);
            v.u = q.uv[0] + (q.uv[2] - q.uv[0]) * bx;
            v.v = q.uv[1] + (q.uv[3] - q.uv[1]) * by;
        } else {
            v.u = v.v = 0.0f;
        }
        v.color = color;
        out.vertices.push_back(v);
        return static_cast<u32>(out.vertices.size() - 1);
    }

    void tri(u32 a, u32 b, u32 c) {
        out.indices.push_back(a);
        out.indices.push_back(b);
        out.indices.push_back(c);
    }

    // Fan from the center to the ring at delta
    void fan(f32 delta, bool stroke, f32 alpha) {
        ring(delta);
        const Color s = unpack(q.strokeColor);
        const u32 center = vertex(0.0f, 0.0f, pack(stroke ? s : fillAt(0.0f, 0.0f), alpha));
        const u32 first = static_cast<u32>(out.vertices.size());
        for (u32 i = 0; i < ringCount; ++i) vertex(ringX[i], ringY[i], pack(stroke ? s : fillAt(ringX[i], ringY[i]), alpha));
        for (u32 i = 0; i < ringCount; ++i) tri(center, first + i, first + (i + 1) % ringCount);
    }

    // Band between the rings at deltaA and deltaB (same point count, so point i matches point i)
    void band(f32 deltaA, f32 alphaA, f32 deltaB, f32 alphaB, bool stroke) {
        const Color s = unpack(q.strokeColor);
        ring(deltaA);
        const u32 a = static_cast<u32>(out.vertices.size());
        for (u32 i = 0; i < ringCount; ++i) vertex(ringX[i], ringY[i], pack(stroke ? s : fillAt(ringX[i], ringY[i]), alphaA));
        ring(deltaB);
        const u32 b = static_cast<u32>(out.vertices.size());
        for (u32 i = 0; i < ringCount; ++i) vertex(ringX[i], ringY[i], pack(stroke ? s : fillAt(ringX[i], ringY[i]), alphaB));
        for (u32 i = 0; i < ringCount; ++i) {
            const u32 j = (i + 1) % ringCount;
            tri(a + i, b + i, b + j);
            tri(a + i, b + j, a + j);
        }
    }

    void build() {
        const u32 kind = q.flags & 0xF;
        if (kind == kQuadGlyph) {
            const u32 c = q.color0;
            const u32 v0 = vertex(-halfW, -halfH, c), v1 = vertex(halfW, -halfH, c), v2 = vertex(halfW, halfH, c), v3 = vertex(-halfW, halfH, c);
            tri(v0, v1, v2);
            tri(v0, v2, v3);
        } else if (kind == kQuadShadow) {
            const f32 blur = std::max(q.softness, 0.5f);
            fan(-blur, false, 1.0f);
            band(-blur, 1.0f, blur, 0.0f, false);
        } else if (q.strokeWidth > 0.0f && kind == kQuadShape) {
            const f32 sw = std::max(q.strokeWidth, 0.5f);
            fan(-sw, false, 1.0f);
            if (sw > 0.5f) band(-sw, 1.0f, -0.5f, 1.0f, true);
            band(-0.5f, 1.0f, 0.5f, 0.0f, true);
        } else {
            fan(-0.5f, false, 1.0f);
            band(-0.5f, 1.0f, 0.5f, 0.0f, false);
        }
    }
};

} // namespace

void tessellate(const Quad* quads, size_t count, DrawList& out) {
    out.clear();
    for (size_t i = 0; i < count; ++i) {
        const Quad& q = quads[i];
        if (q.rect[2] <= 0.0f || q.rect[3] <= 0.0f) continue;
        const u32 kind = q.flags & 0xF;
        const DrawTexture texture = kind == kQuadGlyph ? DrawTexture::Glyphs : kind == kQuadImage ? DrawTexture::Images : DrawTexture::None;
        const u32 firstIndex = static_cast<u32>(out.indices.size());
        Builder(q, out, texture != DrawTexture::None).build();
        const u32 added = static_cast<u32>(out.indices.size()) - firstIndex;
        if (added == 0) continue;
        if (!out.commands.empty()) {
            DrawCommand& last = out.commands.back();
            if (last.texture == texture && std::memcmp(last.clip, q.clip, sizeof(last.clip)) == 0) {
                last.indexCount += added;
                continue;
            }
        }
        DrawCommand cmd;
        cmd.texture = texture;
        cmd.firstIndex = firstIndex;
        cmd.indexCount = added;
        std::memcpy(cmd.clip, q.clip, sizeof(cmd.clip));
        out.commands.push_back(cmd);
    }
}

} // namespace mkui
