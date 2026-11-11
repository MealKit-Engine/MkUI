#include "mkui/Text.h"

#include "mkui/Document.h"

#include <algorithm>

namespace mkui {
namespace {

constexpr u32 kEllipsis = 0x2026; // …

bool isWide(u32 cp) { // characters broken per syllable (Hangul, Han, Kana, fullwidth symbols)
    return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

} // namespace

void decodeUtf8(const std::string& s, std::vector<u32>& out) {
    out.clear();
    for (size_t i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        u32 cp = 0xFFFD;
        size_t n = 1;
        if (c < 0x80) cp = c;
        else if ((c >> 5) == 0x6 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (static_cast<u8>(s[i + 1]) & 0x3Fu); n = 2; }
        else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<u8>(s[i + 2]) & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<u8>(s[i + 1]) & 0x3Fu) << 12) | ((static_cast<u8>(s[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<u8>(s[i + 3]) & 0x3Fu);
            n = 4;
        }
        out.push_back(cp);
        i += n;
    }
}

u32 utf8Length(const std::string& s) {
    u32 n = 0;
    for (const char c : s) n += (static_cast<u8>(c) & 0xC0) != 0x80 ? 1u : 0u;
    return n;
}

namespace {

u32 transformCp(u32 cp, TextTransform t) {
    if (t == TextTransform::Upper && cp >= 'a' && cp <= 'z') return cp - 32;
    if (t == TextTransform::Lower && cp >= 'A' && cp <= 'Z') return cp + 32;
    return cp;
}

} // namespace

// Resolves tags (<1>…</1>) into styles and decides font/size/width per glyph
void shapeGlyphs(const Node& d, const std::string& text, Atlas& atlas, Shaped& out, f32 size) {
    out.runs.clear(); // clear keeps capacity: reshaping changed text allocates nothing once warm
    out.glyphs.clear();
    out.lines.clear();
    out.width = out.height = 0.0f;
    TextRun base;
    base.font = atlas.font(d.text.font);
    base.size = size > 0.0f ? size : d.text.size;
    base.letterSpacing = d.text.letterSpacing;
    base.underline = d.text.underline;
    base.strike = d.text.strike;
    out.runs.push_back(base);
    constexpr size_t kMaxNesting = 16; // deeper tags keep the outer style
    u16 stack[kMaxNesting] = {0};
    size_t depth = 1;
    decodeUtf8(text, out.codepoints);
    const std::vector<u32>& cps = out.codepoints;
    for (size_t i = 0; i < cps.size(); ++i) {
        const u32 cp = cps[i];
        if (cp == '<') { // <n> or </n>
            size_t j = i + 1;
            const bool closing = j < cps.size() && cps[j] == '/';
            if (closing) ++j;
            u32 tag = 0;
            size_t digits = 0;
            while (j < cps.size() && cps[j] >= '0' && cps[j] <= '9' && digits < 4) tag = tag * 10 + (cps[j++] - '0'), ++digits;
            if (digits > 0 && j < cps.size() && cps[j] == '>') {
                if (closing) {
                    if (depth > 1) --depth;
                } else {
                    TextRun r = out.runs[stack[depth - 1]];
                    for (size_t s = 0; s < d.spans.size(); ++s) {
                        const SpanStyle& sp = d.spans[s];
                        if (sp.tag != tag) continue;
                        if (!sp.font.empty()) r.font = atlas.font(sp.font);
                        if (sp.size > 0.0f) r.size = sp.size;
                        if (sp.letterSpacing > -1e8f) r.letterSpacing = sp.letterSpacing;
                        if (sp.underline >= 0) r.underline = sp.underline != 0;
                        if (sp.strike >= 0) r.strike = sp.strike != 0;
                        if (sp.hasColor) r.span = static_cast<i32>(s);
                    }
                    out.runs.push_back(r);
                    if (depth < kMaxNesting) stack[depth++] = static_cast<u16>(out.runs.size() - 1);
                }
                i = j;
                continue;
            }
        }
        ShapedGlyph g;
        g.cp = transformCp(cp, d.text.transform);
        g.run = stack[depth - 1];
        const TextRun& r = out.runs[g.run];
        g.font = r.font;
        if (g.cp != '\n') {
            g.glyph = atlas.glyph(g.font, g.cp);
            g.advance = g.glyph.advance * r.size + r.letterSpacing;
            if (!out.glyphs.empty()) {
                const ShapedGlyph& prev = out.glyphs.back();
                if (prev.font == g.font && prev.run == g.run && prev.cp != '\n')
                    out.glyphs.back().advance += atlas.kerning(g.font, prev.cp, g.cp) * r.size;
            }
        }
        out.glyphs.push_back(g);
    }
}

namespace {

void measureLine(const Node& d, Shaped& s, ShapedLine& line, Atlas& atlas) {
    u32 visibleEnd = line.end; // excluding trailing spaces
    while (visibleEnd > line.begin && (s.glyphs[visibleEnd - 1].cp == ' ' || s.glyphs[visibleEnd - 1].cp == '\n')) --visibleEnd;
    line.width = 0.0f;
    for (u32 i = line.begin; i < visibleEnd; ++i) line.width += s.glyphs[i].advance;
    if (visibleEnd > line.begin) line.width -= s.runs[s.glyphs[visibleEnd - 1].run].letterSpacing; // the last letter spacing is removed
    f32 asc = 0, desc = 0, height = 0;
    auto add = [&](const TextRun& r, i32 font) {
        const FontMetrics& fm = atlas.metrics(font);
        asc = std::max(asc, fm.ascent * r.size);
        desc = std::max(desc, fm.descent * r.size);
        height = std::max(height, r.size * d.lineHeight);
    };
    if (line.begin == line.end) add(s.runs[0], s.runs[0].font);
    for (u32 i = line.begin; i < line.end; ++i) add(s.runs[s.glyphs[i].run], s.glyphs[i].font);
    line.ascent = asc;
    line.descent = desc;
    line.height = height; // line height as is, like CSS (glyphs may overflow)
}

} // namespace

// Line breaking (across run boundaries in one pass). If wrapWidth < 0, only at newline characters
void breakLines(const Node& d, Shaped& s, f32 wrapWidth, f32 maxHeight, Atlas& atlas) {
    s.lines.clear();
    const u32 count = static_cast<u32>(s.glyphs.size());
    const bool wrap = wrapWidth >= 0.0f && d.wrapMode != WrapMode::None;
    u32 lineStart = 0, lastBreak = 0;
    f32 x = 0.0f;
    auto canBreakAfter = [&](u32 i) {
        if (s.glyphs[i].cp == ' ') return true;
        if (d.wrapMode != WrapMode::Syllable) return false;
        return isWide(s.glyphs[i].cp) || (i + 1 < count && isWide(s.glyphs[i + 1].cp));
    };
    auto pushLine = [&](u32 begin, u32 end) {
        ShapedLine l;
        l.begin = begin;
        l.end = end;
        measureLine(d, s, l, atlas);
        s.lines.push_back(l);
    };
    for (u32 i = 0; i < count; ++i) {
        const ShapedGlyph& g = s.glyphs[i];
        if (g.cp == '\n') {
            pushLine(lineStart, i + 1);
            lineStart = lastBreak = i + 1;
            x = 0.0f;
            continue;
        }
        if (wrap && x + g.advance > wrapWidth + 0.01f && i > lineStart && g.cp != ' ') {
            u32 cut = lastBreak > lineStart ? lastBreak : i;
            pushLine(lineStart, cut);
            while (cut < count && s.glyphs[cut].cp == ' ') ++cut; // leading spaces of the next line are dropped
            lineStart = lastBreak = cut;
            x = 0.0f;
            for (u32 j = cut; j < i; ++j) x += s.glyphs[j].advance;
        }
        x += g.advance;
        if (canBreakAfter(i)) lastBreak = i + 1;
    }
    if (lineStart < count || s.lines.empty() || (count > 0 && s.glyphs[count - 1].cp == '\n')) pushLine(lineStart, count);

    // Line count limit (maxLines, fixed height) → ellipsis on the last line
    u32 keep = static_cast<u32>(s.lines.size());
    if (d.maxLines > 0) keep = std::min(keep, d.maxLines);
    if (maxHeight >= 0.0f) {
        f32 h = 0.0f;
        u32 fit = 0;
        for (const ShapedLine& l : s.lines) {
            if (fit > 0 && h + l.height > maxHeight + 0.01f) break;
            h += l.height;
            ++fit;
        }
        if (d.ellipsis) keep = std::min(keep, std::max(fit, 1u));
    }
    const bool truncated = keep < s.lines.size();
    s.truncated = truncated;
    s.lines.resize(keep);
    if (d.ellipsis && !s.lines.empty() && (truncated || (wrapWidth >= 0.0f && s.lines.back().width > wrapWidth + 0.01f))) {
        ShapedLine& last = s.lines.back();
        const u16 run = last.end > last.begin ? s.glyphs[last.end - 1].run : 0;
        ShapedGlyph dots;
        dots.cp = kEllipsis;
        dots.run = run;
        dots.font = s.runs[run].font;
        dots.glyph = atlas.glyph(dots.font, kEllipsis);
        dots.advance = dots.glyph.advance * s.runs[run].size;
        const f32 limit = wrapWidth >= 0.0f ? wrapWidth : 1e9f;
        u32 end = last.end;
        while (end > last.begin && (s.glyphs[end - 1].cp == '\n' || s.glyphs[end - 1].cp == ' ')) --end;
        f32 w = 0.0f;
        for (u32 i = last.begin; i < end; ++i) w += s.glyphs[i].advance;
        while (end > last.begin && w + dots.advance > limit) w -= s.glyphs[--end].advance;
        s.glyphs.resize(end); // later glyphs are dropped (the following lines were already cut)
        s.glyphs.push_back(dots);
        last.end = static_cast<u32>(s.glyphs.size());
        measureLine(d, s, last, atlas);
        s.truncated = true;
    }
    s.width = 0.0f;
    s.height = 0.0f;
    for (const ShapedLine& l : s.lines) {
        s.width = std::max(s.width, l.width);
        s.height += l.height;
    }
}

} // namespace mkui
