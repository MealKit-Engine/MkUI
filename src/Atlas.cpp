#include "mkui/Atlas.h"

#include "mkui/Host.h"

#include "stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace mkui {
namespace {

constexpr u32 kSize = kAtlasSize;
constexpr f32 kOnEdge = 128.0f;
constexpr f32 kDistScale = kOnEdge / static_cast<f32>(kSdfPadding); // SDF value per baked pixel

// Simple row-based rectangle packing (UI glyphs/images are similar in size)
struct Shelves {
    struct Shelf {
        u32 y, height, x;
    };
    std::vector<Shelf> shelves;
    u32 top = 0;

    void clear() {
        shelves.clear();
        top = 0;
    }
    bool place(u32 w, u32 h, u32& outX, u32& outY) {
        w += 1; // 1 pixel gap from neighbors (linear filtering)
        h += 1;
        for (Shelf& s : shelves)
            if (h <= s.height && h * 4 >= s.height * 3 && s.x + w <= kSize) {
                outX = s.x;
                outY = s.y;
                s.x += w;
                return true;
            }
        if (top + h > kSize || w > kSize) {
            // No room for a new shelf: the lowest taller shelf with room left (images of many heights, such as sprites)
            Shelf* best = nullptr;
            for (Shelf& s : shelves)
                if (h <= s.height && s.x + w <= kSize && (!best || s.height < best->height)) best = &s;
            if (!best) return false;
            outX = best->x;
            outY = best->y;
            best->x += w;
            return true;
        }
        shelves.push_back({top, h, w});
        outX = 0;
        outY = top;
        top += h;
        return true;
    }
};

struct FontFile {
    std::string name, path; // path: asset path
    u64 time = 0;
    std::vector<u8> data;
    stbtt_fontinfo info{};
    f32 scale = 0.0f; // font units → baked pixels
    FontMetrics metrics;
};

struct Image {
    std::string path;
    u64 time = 0;
    ImageInfo info;
};

} // namespace

struct Atlas::Impl {
    Host* host = nullptr;
    bool fontsScanned = false;
    std::vector<FontFile> fonts;
    std::unordered_map<u64, Glyph> glyphs;
    Shelves glyphShelves, imageShelves;
    std::vector<Image> images;
    Glyph none;
    ImageInfo noImage;
    FontMetrics defaultMetrics;

    void scanFonts() {
        fontsScanned = true;
        fonts.clear();
        if (!host) return;
        for (const char* ext : {".ttf", ".otf", ".ttc"})
            for (const std::string& file : host->listFiles("fonts", ext)) {
                FontFile f;
                f.name = file.substr(0, file.size() - 4);
                f.path = "fonts/" + file;
                if (!loadFont(f)) continue;
                fonts.push_back(std::move(f));
            }
        if (fonts.empty()) host->log(LogLevel::Warn, "UI: no fonts in assets/fonts - text is not drawn");
    }

    bool loadFont(FontFile& f) {
        std::string bytes;
        if (!host->readFile(f.path, bytes)) return false;
        f.data.assign(bytes.begin(), bytes.end());
        f.time = host->fileTime(f.path);
        const int offset = stbtt_GetFontOffsetForIndex(f.data.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&f.info, f.data.data(), offset)) {
            host->log(LogLevel::Warn, "UI: cannot read font assets/" + f.path);
            return false;
        }
        f.scale = stbtt_ScaleForMappingEmToPixels(&f.info, kBakePixels);
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &lineGap);
        const f32 em = f.scale / kBakePixels;
        f.metrics = {static_cast<f32>(ascent) * em, static_cast<f32>(-descent) * em, static_cast<f32>(lineGap) * em};
        return true;
    }

    void resetGlyphs() {
        glyphs.clear();
        glyphShelves.clear();
    }

    // Flattens outlines into line segments and computes the SDF directly. stbtt_GetGlyphSDF misses cubic curves (CFF/.otf fonts), so round strokes vanish.
    static void flatten(const stbtt_vertex* v, int count, f32 scale, std::vector<f32>& seg) {
        seg.clear();
        f32 px = 0, py = 0;
        auto line = [&](f32 x, f32 y) {
            seg.insert(seg.end(), {px, py, x, y});
            px = x;
            py = y;
        };
        for (int i = 0; i < count; ++i) {
            const f32 x = static_cast<f32>(v[i].x) * scale, y = -static_cast<f32>(v[i].y) * scale; // y is + downward
            if (v[i].type == STBTT_vmove) {
                px = x;
                py = y;
            } else if (v[i].type == STBTT_vline) {
                line(x, y);
            } else if (v[i].type == STBTT_vcurve) {
                const f32 cx = static_cast<f32>(v[i].cx) * scale, cy = -static_cast<f32>(v[i].cy) * scale;
                const f32 sx = px, sy = py;
                constexpr int kSteps = 8;
                for (int k = 1; k <= kSteps; ++k) {
                    const f32 t = static_cast<f32>(k) / kSteps, u = 1.0f - t;
                    line(u * u * sx + 2 * u * t * cx + t * t * x, u * u * sy + 2 * u * t * cy + t * t * y);
                }
            } else if (v[i].type == STBTT_vcubic) {
                const f32 c0x = static_cast<f32>(v[i].cx) * scale, c0y = -static_cast<f32>(v[i].cy) * scale;
                const f32 c1x = static_cast<f32>(v[i].cx1) * scale, c1y = -static_cast<f32>(v[i].cy1) * scale;
                const f32 sx = px, sy = py;
                constexpr int kSteps = 10;
                for (int k = 1; k <= kSteps; ++k) {
                    const f32 t = static_cast<f32>(k) / kSteps, u = 1.0f - t;
                    line(u * u * u * sx + 3 * u * u * t * c0x + 3 * u * t * t * c1x + t * t * t * x,
                         u * u * u * sy + 3 * u * u * t * c0y + 3 * u * t * t * c1y + t * t * t * y);
                }
            }
        }
    }

    Glyph bake(FontFile& f, int glyphIndex) {
        Glyph g;
        g.valid = true;
        const f32 em = 1.0f / kBakePixels;
        int advance = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&f.info, glyphIndex, &advance, &lsb);
        g.advance = static_cast<f32>(advance) * f.scale * em;
        stbtt_vertex* verts = nullptr;
        const int count = stbtt_GetGlyphShape(&f.info, glyphIndex, &verts);
        if (count <= 0) return g; // empty glyph (space)
        std::vector<f32> seg;
        flatten(verts, count, f.scale, seg);
        stbtt_FreeShape(&f.info, verts);
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetGlyphBitmapBox(&f.info, glyphIndex, f.scale, f.scale, &ix0, &iy0, &ix1, &iy1);
        if (ix1 <= ix0 || iy1 <= iy0) return g;
        const int xoff = ix0 - kSdfPadding, yoff = iy0 - kSdfPadding;
        const int w = ix1 - ix0 + kSdfPadding * 2, h = iy1 - iy0 + kSdfPadding * 2;
        std::vector<u8> sdf(static_cast<size_t>(w) * h);
        const f32 maxDist = static_cast<f32>(kSdfPadding) + 1.0f;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const f32 px = static_cast<f32>(x + xoff) + 0.5f, py = static_cast<f32>(y + yoff) + 0.5f;
                f32 best = maxDist * maxDist;
                int winding = 0;
                for (size_t i = 0; i + 3 < seg.size(); i += 4) {
                    const f32 ax = seg[i], ay = seg[i + 1], bx = seg[i + 2], by = seg[i + 3];
                    // Nearest distance
                    const f32 dx = bx - ax, dy = by - ay;
                    const f32 len2 = dx * dx + dy * dy;
                    f32 t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
                    t = std::clamp(t, 0.0f, 1.0f);
                    const f32 qx = ax + dx * t - px, qy = ay + dy * t - py;
                    best = std::min(best, qx * qx + qy * qy);
                    // Winding number (nonzero inside)
                    if (ay <= py) {
                        if (by > py && (dx * (py - ay) - (px - ax) * dy) > 0) ++winding;
                    } else if (by <= py && (dx * (py - ay) - (px - ax) * dy) < 0) {
                        --winding;
                    }
                }
                const f32 dist = std::sqrt(best) * (winding != 0 ? 1.0f : -1.0f);
                sdf[static_cast<size_t>(y) * w + x] = static_cast<u8>(std::clamp(kOnEdge + dist * kDistScale, 0.0f, 255.0f));
            }
        u32 ax = 0, ay = 0;
        if (!glyphShelves.place(static_cast<u32>(w), static_cast<u32>(h), ax, ay)) {
            if (host) host->log(LogLevel::Warn, "UI: glyph atlas full (" + std::to_string(glyphs.size()) + " glyphs) - rebaking");
            resetGlyphs();
            glyphShelves.place(static_cast<u32>(w), static_cast<u32>(h), ax, ay);
        }
        if (host) host->uploadAtlas(AtlasKind::Glyphs, ax, ay, static_cast<u32>(w), static_cast<u32>(h), sdf.data());
        g.x0 = static_cast<f32>(xoff) * em;
        g.y0 = static_cast<f32>(yoff) * em;
        g.x1 = static_cast<f32>(xoff + w) * em;
        g.y1 = static_cast<f32>(yoff + h) * em;
        g.u0 = static_cast<f32>(ax) / kSize;
        g.v0 = static_cast<f32>(ay) / kSize;
        g.u1 = static_cast<f32>(ax + static_cast<u32>(w)) / kSize;
        g.v1 = static_cast<f32>(ay + static_cast<u32>(h)) / kSize;
        return g;
    }
};

Atlas::Atlas() : m(new Impl()) {}
Atlas::~Atlas() { delete m; }

void Atlas::setHost(Host* host) {
    m->host = host;
    m->fontsScanned = false;
    m->resetGlyphs(); // a new host's atlas is empty
    m->imageShelves.clear();
    m->images.clear();
}

i32 Atlas::font(const std::string& name) {
    if (!m->fontsScanned) m->scanFonts();
    if (m->fonts.empty()) return -1;
    for (size_t i = 0; i < m->fonts.size(); ++i)
        if (m->fonts[i].name == name) return static_cast<i32>(i);
    return 0;
}

const FontMetrics& Atlas::metrics(i32 font) {
    if (font < 0 || font >= static_cast<i32>(m->fonts.size())) return m->defaultMetrics;
    return m->fonts[static_cast<size_t>(font)].metrics;
}

Glyph Atlas::glyph(i32& font, u32 codepoint) {
    if (font < 0 || font >= static_cast<i32>(m->fonts.size())) return m->none;
    i32 use = font;
    int index = stbtt_FindGlyphIndex(&m->fonts[static_cast<size_t>(use)].info, static_cast<int>(codepoint));
    for (size_t i = 0; index == 0 && i < m->fonts.size(); ++i) { // in other fonts
        const int other = stbtt_FindGlyphIndex(&m->fonts[i].info, static_cast<int>(codepoint));
        if (other) {
            use = static_cast<i32>(i);
            index = other;
        }
    }
    font = use;
    const u64 key = (static_cast<u64>(use) << 32) | codepoint;
    auto it = m->glyphs.find(key);
    if (it != m->glyphs.end()) return it->second;
    return m->glyphs.emplace(key, m->bake(m->fonts[static_cast<size_t>(use)], index)).first->second; // .notdef if missing
}

f32 Atlas::kerning(i32 font, u32 a, u32 b) {
    if (font < 0 || font >= static_cast<i32>(m->fonts.size())) return 0.0f;
    FontFile& f = m->fonts[static_cast<size_t>(font)];
    return static_cast<f32>(stbtt_GetCodepointKernAdvance(&f.info, static_cast<int>(a), static_cast<int>(b))) * f.scale /
           kBakePixels;
}

f32 Atlas::sdfUnitsPerPixel() const { return 255.0f / kDistScale; }

ImageInfo Atlas::image(const std::string& assetPath) {
    for (const Image& i : m->images)
        if (i.path == assetPath) return i.info;
    Image img;
    img.path = assetPath;
    if (!m->host) return m->noImage;
    Host& host = *m->host;
    img.time = host.fileTime(assetPath);
    std::string bytes;
    u32 w = 0, h = 0;
    std::vector<u8> pixels;
    if (!host.readFile(assetPath, bytes) || !host.decodeImage(bytes, w, h, pixels)) {
        host.log(LogLevel::Warn, "UI: cannot load image assets/" + assetPath);
    } else {
        u32 x = 0, y = 0;
        bool placed = m->imageShelves.place(w, h, x, y);
        if (!placed && !m->images.empty()) {
            // Full: start the image atlas over (images bound by path, "src {field}", come and go). Images still in use are
            // read again on their next draw; quads already emitted this frame may show the wrong pixels for one frame.
            host.log(LogLevel::Warn, "UI: image atlas full (" + std::to_string(m->images.size()) + " images) - rebuilding");
            m->imageShelves.clear();
            m->images.clear();
            placed = m->imageShelves.place(w, h, x, y);
        }
        if (placed) {
            host.uploadAtlas(AtlasKind::Images, x, y, w, h, pixels.data());
            img.info = {true, w, h, static_cast<f32>(x) / kSize, static_cast<f32>(y) / kSize, static_cast<f32>(x + w) / kSize,
                        static_cast<f32>(y + h) / kSize};
        } else {
            host.log(LogLevel::Warn, "UI: image larger than the atlas - assets/" + assetPath + " is not shown");
        }
    }
    m->images.push_back(std::move(img));
    return m->images.back().info;
}

void Atlas::reloadChanged() {
    if (!m->host) return;
    Host& host = *m->host;
    bool fontsChanged = false;
    for (FontFile& f : m->fonts)
        if (host.fileTime(f.path) != f.time) fontsChanged = true;
    if (fontsChanged) {
        host.log(LogLevel::Info, "UI: fonts changed - reloading");
        m->scanFonts();
        m->resetGlyphs();
    }
    for (size_t i = 0; i < m->images.size(); ++i) {
        if (host.fileTime(m->images[i].path) != m->images[i].time) {
            m->images.erase(m->images.begin() + static_cast<std::ptrdiff_t>(i)); // reread on next use (the atlas space is abandoned)
            --i;
        }
    }
}

u32 Atlas::glyphCount() const { return static_cast<u32>(m->glyphs.size()); }

} // namespace mkui
