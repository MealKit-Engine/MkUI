#include "mkui/Ui.h"

#include "mkui/Atlas.h"
#include "mkui/Document.h"
#include "mkui/Text.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace mkui {
namespace {

constexpr f64 kReloadSeconds = 0.25;
constexpr f32 kScrollStep = 60.0f;   // one wheel notch (logical units)
constexpr f32 kDegToRad = 0.01745329252f;

struct Rect {
    f32 x = 0, y = 0, w = 0, h = 0;
    bool contains(f32 px, f32 py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// ---------------------------------------------------------------- Colors

u32 lerpColor(u32 a, u32 b, f32 t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    u32 out = 0;
    for (int i = 0; i < 4; ++i) {
        const f32 ca = static_cast<f32>((a >> (i * 8)) & 0xFF), cb = static_cast<f32>((b >> (i * 8)) & 0xFF);
        out |= static_cast<u32>(std::lround(ca + (cb - ca) * t)) << (i * 8);
    }
    return out;
}

u32 withAlpha(u32 c, f32 opacity) {
    const f32 a = static_cast<f32>(c >> 24) * std::clamp(opacity, 0.0f, 1.0f);
    return (c & 0x00FFFFFFu) | (static_cast<u32>(std::lround(a)) << 24);
}

// ---------------------------------------------------------------- Screens

struct RNode {
    u32 src = 0;
    i32 parent = -1;
    u32 end = 0;
    i32 listIndex = -1;
    Model item; // list item (bindings look here first)
    Rect rect;  // logical units (relative to the board)
    bool visible = true;
    bool disabled = false, checked = false;
    f32 mix[static_cast<int>(State::Count)] = {};
    std::string text; // text with bindings resolved
    bool placeholder = false; // the text input is empty and shows placeholder text
    Shaped shaped;
    f32 shapedWidth = -2.0f; // lines were broken at this width
    std::string shapedText;
    f32 shapedSize = 0.0f; // font size from fontSizeField when shaped (0 = the node's)
    f32 scroll = 0.0f, contentExtent = 0.0f;
};

struct Screen {
    ScreenId id = kNoScreen;
    i32 layer = 0;
    bool worldTemplate = false;
    bool fresh = true; // hasn't gone through update since opening: ui.* actions are from the opening frame, so they aren't taken (so opening with Esc doesn't close with the same Esc)
    std::string path;
    u64 fileTime = 0;
    bool hasDraft = false; // editor: doc comes from draft, not the file
    i32 previewNode = -1;  // editor: this doc node shows previewState
    State previewState = State::Normal;
    std::string draft;
    Document doc;
    std::vector<std::array<Visual, static_cast<size_t>(State::Count)>> stateVisuals; // nodes with states: drawing values per state
    std::vector<i32> stateIndex; // doc node → stateVisuals index (-1 none)
    Model vm;
    std::vector<RNode> nodes;
    std::vector<u32> listCounts; // re-expanded when list counts change
    bool rebuild = true;
    std::vector<std::string> bindingErrors;
    i32 focus = -1, hover = -1, pressed = -1;
    i32 editing = -1; // text input node being typed into
    f32 rootW = 1920, rootH = 1080, scale = 1, originX = 0, originY = 0;
};

struct WorldDraw {
    ScreenId screen;
    f32 x, y, z;
    Model vm;
    bool clamp;
};

bool truthy(const Value& v) {
    switch (v.kind) {
    case ValueKind::Bool: return *static_cast<const bool*>(v.data);
    case ValueKind::Int:
    case ValueKind::UInt:
    case ValueKind::Ticks:
    case ValueKind::Color: {
        u64 value = 0;
        std::memcpy(&value, v.data, std::min<u32>(v.size, 8));
        return value != 0;
    }
    case ValueKind::Float: return *static_cast<const f32*>(v.data) != 0.0f;
    case ValueKind::String: return !static_cast<const std::string*>(v.data)->empty();
    default: return false;
    }
}

f64 numberOf(const Value& v) {
    switch (v.kind) {
    case ValueKind::Bool: return *static_cast<const bool*>(v.data) ? 1.0 : 0.0;
    case ValueKind::Float: return *static_cast<const f32*>(v.data);
    case ValueKind::Ticks: return static_cast<f64>(*static_cast<const u32*>(v.data)) / v.ticksPerSecond;
    case ValueKind::Int: {
        i64 value = 0;
        if (v.size == 1) value = *static_cast<const i8*>(v.data);
        else if (v.size == 2) value = *static_cast<const i16*>(v.data);
        else if (v.size == 4) value = *static_cast<const i32*>(v.data);
        else value = *static_cast<const i64*>(v.data);
        return static_cast<f64>(value);
    }
    case ValueKind::UInt: {
        u64 value = 0;
        std::memcpy(&value, v.data, std::min<u32>(v.size, 8));
        return static_cast<f64>(value);
    }
    default: return 0.0;
    }
}

void writeNumber(const Value& v, f64 value) {
    switch (v.kind) {
    case ValueKind::Bool: *static_cast<bool*>(v.data) = value != 0.0; break;
    case ValueKind::Float: *static_cast<f32*>(v.data) = static_cast<f32>(value); break;
    case ValueKind::Int: {
        const i64 x = static_cast<i64>(std::llround(value));
        if (v.size == 1) *static_cast<i8*>(v.data) = static_cast<i8>(x);
        else if (v.size == 2) *static_cast<i16*>(v.data) = static_cast<i16>(x);
        else if (v.size == 4) *static_cast<i32*>(v.data) = static_cast<i32>(x);
        else *static_cast<i64*>(v.data) = x;
        break;
    }
    case ValueKind::UInt: {
        const u64 x = static_cast<u64>(std::max<i64>(0, std::llround(value)));
        if (v.size == 1) *static_cast<u8*>(v.data) = static_cast<u8>(x);
        else if (v.size == 2) *static_cast<u16*>(v.data) = static_cast<u16>(x);
        else if (v.size == 4) *static_cast<u32*>(v.data) = static_cast<u32>(x);
        else *static_cast<u64*>(v.data) = x;
        break;
    }
    default: break;
    }
}

// Appends v with thousands separators
void appendThousands(std::string& out, i64 v) {
    char digits[32];
    const int n = std::snprintf(digits, sizeof(digits), "%lld", static_cast<long long>(v < 0 ? -v : v));
    if (v < 0) out += '-';
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += ',';
        out += digits[i];
    }
}

// Formats: none, n (thousands), n1/f1 (decimal places), time (min:sec), pct (0..1 → %). Appends without temporary strings
void appendValue(std::string& out, const Value& v, std::string_view spec, const Binder& binder) {
    if (v.kind == ValueKind::String) {
        out += *static_cast<const std::string*>(v.data);
        return;
    }
    char buf[64];
    auto put = [&](int n) { out.append(buf, static_cast<size_t>(std::clamp(n, 0, static_cast<int>(sizeof(buf)) - 1))); };
    if (v.enumNames && !v.enumNames->empty()) {
        const u64 index = static_cast<u64>(numberOf(v));
        if (index < v.enumNames->size()) out += (*v.enumNames)[index];
        else put(std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(index)));
        return;
    }
    if (v.kind == ValueKind::Bool || v.kind == ValueKind::Int || v.kind == ValueKind::UInt || v.kind == ValueKind::Float ||
        v.kind == ValueKind::Ticks) {
        const f64 x = numberOf(v);
        if (spec == "time") {
            const i64 total = static_cast<i64>(std::floor(std::max(0.0, x)));
            put(std::snprintf(buf, sizeof(buf), "%lld:%02lld", static_cast<long long>(total / 60), static_cast<long long>(total % 60)));
            return;
        }
        if (spec == "pct") {
            put(std::snprintf(buf, sizeof(buf), "%.0f%%", x * 100.0));
            return;
        }
        int decimals = -1;
        bool group = false;
        if (!spec.empty() && (spec[0] == 'n' || spec[0] == 'f')) {
            group = spec[0] == 'n';
            decimals = 0;
            for (size_t i = 1; i < spec.size() && spec[i] >= '0' && spec[i] <= '9'; ++i) decimals = decimals * 10 + (spec[i] - '0');
        }
        if (decimals < 0) {
            if (v.kind == ValueKind::Float || v.kind == ValueKind::Ticks) put(std::snprintf(buf, sizeof(buf), "%g", x));
            else put(std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(x)));
            return;
        }
        if (group && decimals == 0) {
            appendThousands(out, static_cast<i64>(std::llround(x)));
            return;
        }
        const int n = std::snprintf(buf, sizeof(buf), "%.*f", decimals, x);
        if (!group) {
            put(n);
            return;
        }
        appendThousands(out, static_cast<i64>(std::trunc(x)));
        if (const char* dot = std::strchr(buf, '.')) out += dot;
        return;
    }
    binder.format(v, out);
}

// Drawing values of one node this frame: a Visual with theme colors resolved, blended between states.
// Plain values plus a pointer to the shadow list, so drawing copies no strings or vectors
struct Look {
    PaintKind fill = PaintKind::None;
    u32 fill0 = 0, fill1 = 0;
    f32 gradient[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    f32 strokeWidth = 0.0f;
    u32 strokeColor = 0;
    f32 radii[4] = {};
    f32 opacity = 1.0f;
    f32 brightness = 0.0f;
    u32 textColor = 0xFFFFFFFFu, tint = 0xFFFFFFFFu;
    const std::vector<Shadow>* shadows = nullptr;
};

Look lookOf(const Visual& v) {
    Look l;
    l.fill = v.fill.kind;
    l.fill0 = v.fill.c0.rgba;
    l.fill1 = v.fill.c1.rgba;
    std::memcpy(l.gradient, v.fill.p, sizeof(l.gradient));
    l.strokeWidth = v.strokeWidth;
    l.strokeColor = v.strokeColor.rgba;
    std::memcpy(l.radii, v.radii, sizeof(l.radii));
    l.opacity = v.opacity;
    l.brightness = v.brightness;
    l.textColor = v.textColor.rgba;
    l.tint = v.tint.rgba;
    l.shadows = &v.shadows;
    return l;
}

void lerpLook(Look& a, const Visual& b, f32 t) {
    if (t <= 0.0f) return;
    if (b.fill.kind != PaintKind::None || a.fill != PaintKind::None) {
        if (a.fill == b.fill.kind) {
            a.fill0 = lerpColor(a.fill0, b.fill.c0.rgba, t);
            a.fill1 = lerpColor(a.fill1, b.fill.c1.rgba, t);
        } else if (t >= 0.5f) {
            a.fill = b.fill.kind;
            a.fill0 = b.fill.c0.rgba;
            a.fill1 = b.fill.c1.rgba;
            std::memcpy(a.gradient, b.fill.p, sizeof(a.gradient));
        }
    }
    a.strokeWidth += (b.strokeWidth - a.strokeWidth) * t;
    a.strokeColor = lerpColor(a.strokeColor, b.strokeColor.rgba, t);
    for (int i = 0; i < 4; ++i) a.radii[i] += (b.radii[i] - a.radii[i]) * t;
    a.opacity += (b.opacity - a.opacity) * t;
    a.brightness += (b.brightness - a.brightness) * t;
    a.textColor = lerpColor(a.textColor, b.textColor.rgba, t);
    a.tint = lerpColor(a.tint, b.tint.rgba, t);
    if (t >= 0.5f) a.shadows = &b.shadows;
}

// Theme tokens to real colors (before state blending)
void resolveVisual(Visual& v, const Theme& theme) {
    v.fill.c0.rgba = theme.color(v.fill.c0, v.fill.c0.rgba);
    v.fill.c1.rgba = theme.color(v.fill.c1, v.fill.c1.rgba);
    v.strokeColor.rgba = theme.color(v.strokeColor, v.strokeColor.rgba);
    v.textColor.rgba = v.hasTextColor ? theme.color(v.textColor, v.textColor.rgba) : 0xFFFFFFFFu;
    v.tint.rgba = v.hasTint ? theme.color(v.tint, v.tint.rgba) : 0xFFFFFFFFu;
    for (Shadow& s : v.shadows) s.color.rgba = theme.color(s.color, s.color.rgba);
}

// Layout temporaries for one tree depth. Layout handles one depth at a time down the tree, so each depth reuses its own buffers
// (no allocations once warm)
struct FlexItem {
    i32 node;
    f32 main, cross;    // size (excluding margins)
    f32 m0, m1, c0, c1; // margins: start/end (main axis), start/end (cross axis)
    bool grow, stretch;
    f32 minMain, maxMain;
};
struct FlexLine {
    size_t begin, end;
    f32 cross = 0;
};
struct GridItem {
    i32 node;
    u32 row, col, rowSpan, colSpan; // from 0
};
struct Scratch {
    std::vector<i32> flow;
    std::vector<FlexItem> items;
    std::vector<FlexLine> lines;
    std::vector<GridItem> grid;
    std::vector<u8> used, fixed;
    std::vector<f32> cols, rows, colStart, rowStart, colContent, rowContent;
};

} // namespace

struct System::Impl {
    Host& host;
    const Binder& binder;
    Atlas atlas;
    Theme theme;
    u64 themeTime = 0;
    bool themeLoaded = false;
    std::vector<Screen> screens;
    ScreenId nextId = 1;
    f32 uiScale = 1.0f;
    std::vector<Action> actions;
    bool wantsMouse = false, wantsKeyboard = false;
    bool focusVisible = false, focusAlways = false;
    f64 time = 0.0, nextReload = 0.0;
    f32 viewX = 0, viewY = 0, viewW = 1, viewH = 1;
    f32 viewProj[4][4] = {};
    std::vector<WorldDraw> world;
    std::vector<Quad> quads;
    u32 lastQuadCount = 0;
    // Buffers kept between frames
    std::vector<Scratch> scratch; // per tree depth
    std::vector<Screen*> order;
    std::string textBuf, pathBuf;

    Impl(Host& h, const Binder& b) : host(h), binder(b) { atlas.setHost(&host); }

    Screen* find(ScreenId id) {
        for (Screen& s : screens)
            if (s.id == id) return &s;
        return nullptr;
    }

    Scratch& scratchAt(u32 depth) { return scratch[depth]; } // sized in loadScreen (deepest node + 1)

    void warn(const std::string& message) { host.log(LogLevel::Warn, message); }

    void ensureTheme() {
        if (themeLoaded) return;
        themeLoaded = true;
        std::vector<std::string> errors;
        loadTheme(theme, host, &errors);
        for (const std::string& e : errors) warn("UI: " + e);
        themeTime = host.fileTime(kThemeFile);
    }

    void loadScreen(Screen& s) {
        ensureTheme(); // number tokens ($radius) are resolved when reading
        if (s.hasDraft) parseDocument(s.draft, s.path, s.doc, host, &theme);
        else loadDocument(s.path, s.doc, host, &theme);
        s.fileTime = host.fileTime(s.path);
        for (const std::string& e : s.doc.errors) warn("UI: " + e);
        // Build per-state drawing values in advance
        s.stateVisuals.clear();
        s.stateIndex.assign(s.doc.nodes.size(), -1);
        u32 maxDepth = 0;
        for (size_t i = 0; i < s.doc.nodes.size(); ++i) {
            Node& d = s.doc.nodes[i];
            maxDepth = std::max(maxDepth, d.depth);
            resolveVisual(d.visual, theme);
            if (d.states.empty()) continue;
            std::array<Visual, static_cast<size_t>(State::Count)> visuals;
            for (int st = 0; st < static_cast<int>(State::Count); ++st) {
                Node copy = d;
                for (const StateOverride& o : d.states)
                    if (static_cast<int>(o.state) == st)
                        for (const std::string& line : o.lines) applyProperty(copy, line, &theme);
                resolveVisual(copy.visual, theme);
                visuals[static_cast<size_t>(st)] = copy.visual;
            }
            s.stateIndex[i] = static_cast<i32>(s.stateVisuals.size());
            s.stateVisuals.push_back(std::move(visuals));
        }
        if (scratch.size() < maxDepth + 1) scratch.resize(maxDepth + 1);
        s.rebuild = true;
        s.focus = s.hover = s.pressed = -1;
        // The laid-out nodes index the old document (RNode::src): drop them, or queries before the next layout (boxes, info, press)
        // read past the new one. The editor rereads boxes right after an edit sets a draft (a crash on add + delete)
        s.nodes.clear();
    }

    // ---------------------------------------------------------------- Bindings

    bool resolve(Screen& s, const RNode& n, const std::string& path, Value& v) {
        if (n.item && binder.resolve(n.item, path, v)) return true;
        if (s.vm && binder.resolve(s.vm, path, v)) return true;
        return false;
    }
    // Path given as a piece of another string: goes through a kept buffer
    bool resolve(Screen& s, const RNode& n, std::string_view path, Value& v) {
        pathBuf.assign(path);
        return resolve(s, n, pathBuf, v);
    }

    void bindingError(Screen& s, const std::string& message) {
        for (const std::string& e : s.bindingErrors)
            if (e == message) return;
        s.bindingErrors.push_back(message);
        warn("UI " + s.path + ": " + message);
    }
    void missingField(Screen& s, std::string_view path, const char* where = nullptr) {
        if (!s.vm) return;
        std::string message = "no field '" + std::string(path) + "' in " + binder.typeName(s.vm);
        if (where) message += where;
        bindingError(s, message);
    }

    bool flag(Screen& s, const RNode& n, const std::string& field) {
        if (field.empty()) return false;
        const bool negate = field[0] == '!';
        Value v;
        const bool found = negate ? resolve(s, n, std::string_view(field).substr(1), v) : resolve(s, n, field, v);
        if (!found) {
            missingField(s, negate ? std::string_view(field).substr(1) : std::string_view(field));
            return negate;
        }
        return truthy(v) != negate;
    }

    void appendOptionLabel(Screen& s, i32 index, std::string& out) { // current selection of the nearest select box
        for (i32 a = index; a >= 0; a = s.nodes[static_cast<size_t>(a)].parent) {
            const Node& d = s.doc.nodes[s.nodes[static_cast<size_t>(a)].src];
            if (d.widget != WidgetKind::Select || d.options.empty()) continue;
            Value v;
            u32 choice = 0;
            if (resolve(s, s.nodes[static_cast<size_t>(a)], d.field, v))
                choice = static_cast<u32>(std::clamp<f64>(numberOf(v), 0.0, static_cast<f64>(d.options.size() - 1)));
            out += host.text(d.options[choice]);
            return;
        }
    }

    // Text of a text node with bindings resolved, written into out (kept capacity: no allocations once warm)
    void resolveText(Screen& s, i32 index, std::string& out) {
        out.clear();
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        if (d.widget == WidgetKind::TextInput) { // the field value as is (placeholder text if empty)
            n.placeholder = false;
            Value v;
            if (resolve(s, n, d.field, v) && v.kind == ValueKind::String) {
                const std::string& value = *static_cast<const std::string*>(v.data);
                if (!value.empty() || s.editing == index) {
                    out += value;
                    return;
                }
            } else if (s.vm) {
                bindingError(s, "text input '" + d.field + "' is not a string field in " + binder.typeName(s.vm));
            }
            n.placeholder = true;
            if (!d.placeholderKey.empty()) out += host.text(d.placeholderKey);
            return;
        }
        const std::string& source = !d.textKey.empty() ? host.text(d.textKey) : d.textContent;
        const std::string_view src(source);
        for (size_t i = 0; i < src.size(); ++i) {
            if (src[i] == '{') {
                const size_t close = src.find('}', i + 1);
                if (close != std::string_view::npos) {
                    std::string_view name = src.substr(i + 1, close - i - 1), spec;
                    const size_t colon = name.find(':');
                    if (colon != std::string_view::npos) {
                        spec = name.substr(colon + 1);
                        name = name.substr(0, colon);
                    }
                    if (name == "option") {
                        appendOptionLabel(s, index, out);
                        i = close;
                        continue;
                    }
                    Value v;
                    if (resolve(s, n, name, v)) {
                        appendValue(out, v, spec, binder);
                        i = close;
                        continue;
                    }
                    missingField(s, name, (" (text " + d.name + ")").c_str());
                }
            }
            out += src[i];
        }
    }

    // ---------------------------------------------------------------- Expansion (list items)

    u32 listCount(Screen& s, const RNode& n, const Node& d, Value& list) {
        if (!resolve(s, n, d.field, list) || list.kind != ValueKind::Array) {
            if (s.vm) bindingError(s, "list '" + d.field + "' is not an array (MEALKIT_LIST) in " + binder.typeName(s.vm));
            list = {};
            return 0;
        }
        u32 count = list.count;
        pathBuf.assign(d.field);
        pathBuf += "Count";
        Value cv;
        if (resolve(s, n, pathBuf, cv)) count = std::min(count, static_cast<u32>(numberOf(cv)));
        return count;
    }

    void buildSubtree(Screen& s, u32 docIndex, i32 parent, Model item, i32 listIndex) {
        const u32 index = static_cast<u32>(s.nodes.size());
        RNode r;
        r.src = docIndex;
        r.parent = parent;
        r.item = item;
        r.listIndex = listIndex;
        s.nodes.push_back(r);
        const Node& d = s.doc.nodes[docIndex];
        if (d.widget == WidgetKind::List) {
            Value list;
            const u32 count = listCount(s, s.nodes[index], d, list);
            s.listCounts.push_back(count);
            const u32 templ = docIndex + 1;
            if (templ < d.end && list.elementKind == ValueKind::Struct)
                for (u32 k = 0; k < count; ++k)
                    buildSubtree(s, templ, static_cast<i32>(index),
                                 Model{list.elementType, static_cast<u8*>(list.data) + static_cast<size_t>(k) * list.elementSize},
                                 static_cast<i32>(k));
        } else {
            for (u32 c = docIndex + 1; c < d.end; c = s.doc.nodes[c].end) buildSubtree(s, c, static_cast<i32>(index), item, listIndex);
        }
        s.nodes[index].end = static_cast<u32>(s.nodes.size());
    }

    void rebuild(Screen& s) {
        const u32 keepFocus = s.focus >= 0 ? s.nodes[static_cast<size_t>(s.focus)].src : 0xFFFFFFFFu;
        const i32 keepList = s.focus >= 0 ? s.nodes[static_cast<size_t>(s.focus)].listIndex : -1;
        const bool keepEditing = s.editing >= 0 && s.editing == s.focus;
        std::vector<RNode> old = std::move(s.nodes);
        s.nodes.clear();
        s.listCounts.clear();
        if (!s.doc.nodes.empty()) buildSubtree(s, 0, -1, {}, -1);
        s.focus = s.hover = s.pressed = -1;
        for (size_t i = 0; i < s.nodes.size(); ++i) { // focus/scroll carry over to the same node
            if (s.nodes[i].src == keepFocus && s.nodes[i].listIndex == keepList) s.focus = static_cast<i32>(i);
            for (const RNode& o : old)
                if (o.src == s.nodes[i].src && o.listIndex == s.nodes[i].listIndex) {
                    s.nodes[i].scroll = o.scroll;
                    std::memcpy(s.nodes[i].mix, o.mix, sizeof(o.mix));
                    break;
                }
        }
        s.editing = keepEditing ? s.focus : -1;
        s.rebuild = false;
    }

    // Points the expanded nodes at the current view model without rebuilding the node array (world UI draws one template with a
    // different view model per name tag). false if the expansion would differ (list counts changed): rebuild instead
    bool rebind(Screen& s) {
        size_t list = 0;
        return !s.nodes.empty() && rebindSubtree(s, 0, {}, list);
    }

    bool rebindSubtree(Screen& s, u32 index, Model item, size_t& list) {
        s.nodes[index].item = item;
        const Node& d = s.doc.nodes[s.nodes[index].src];
        const u32 end = s.nodes[index].end;
        if (d.widget == WidgetKind::List) {
            Value v;
            const u32 count = listCount(s, s.nodes[index], d, v);
            if (list >= s.listCounts.size() || s.listCounts[list] != count) return false;
            ++list;
            u32 k = 0;
            for (u32 c = index + 1; c < end; c = s.nodes[c].end, ++k)
                if (!rebindSubtree(s, c, Model{v.elementType, static_cast<u8*>(v.data) + static_cast<size_t>(k) * v.elementSize}, list))
                    return false;
            return true;
        }
        for (u32 c = index + 1; c < end; c = s.nodes[c].end)
            if (!rebindSubtree(s, c, item, list)) return false;
        return true;
    }

    bool listsChanged(Screen& s) {
        size_t k = 0;
        for (size_t i = 0; i < s.nodes.size(); ++i) {
            const Node& d = s.doc.nodes[s.nodes[i].src];
            if (d.widget != WidgetKind::List) continue;
            Value list;
            const u32 count = listCount(s, s.nodes[i], d, list);
            if (k >= s.listCounts.size() || s.listCounts[k] != count) return true;
            ++k;
        }
        return false;
    }

    // ---------------------------------------------------------------- Layout

    void shapeText(Screen& s, i32 index, f32 wrapWidth, f32 maxHeight) {
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        f32 size = 0.0f;
        if (!d.fontSizeField.empty()) { // font size binding
            Value v;
            if (resolve(s, n, d.fontSizeField, v)) size = std::max(0.0f, static_cast<f32>(numberOf(v)));
            else missingField(s, d.fontSizeField);
        }
        if (n.shapedWidth == wrapWidth && n.shapedText == n.text && n.shapedSize == size) return;
        shapeGlyphs(d, n.text, atlas, n.shaped, size);
        breakLines(d, n.shaped, wrapWidth, maxHeight, atlas);
        n.shapedWidth = wrapWidth;
        n.shapedText = n.text;
        n.shapedSize = size;
    }

    // Size fitted to content (hug). availW: width used for text wrapping (<0 none)
    // An image node's PNG: its fixed src, or the view model string named by src {field} (null when none or empty)
    const std::string* imagePathOf(Screen& s, const RNode& n, const Node& d) {
        if (d.imageField.empty()) return d.image.empty() ? nullptr : &d.image;
        Value iv;
        if (!resolve(s, n, d.imageField, iv) || iv.kind != ValueKind::String) {
            missingField(s, d.imageField);
            return nullptr;
        }
        const auto* p = static_cast<const std::string*>(iv.data);
        return p->empty() ? nullptr : p;
    }

    void intrinsic(Screen& s, i32 index, f32 availW, f32& outW, f32& outH) {
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        if (d.kind == NodeKind::Image) { // hug = the image's own size (source pixels = logical units)
            const std::string* path = imagePathOf(s, n, d);
            const ImageInfo img = path ? atlas.image(*path) : ImageInfo{};
            outW = d.wMode == SizeMode::Hug ? (img.valid ? static_cast<f32>(img.width) : 0.0f) : d.w;
            outH = d.hMode == SizeMode::Hug ? (img.valid ? static_cast<f32>(img.height) : 0.0f) : d.h;
            return;
        }
        if (d.kind == NodeKind::Text) {
            const f32 wrap = d.wMode == SizeMode::Hug ? -1.0f : (d.wMode == SizeMode::Fixed ? d.w : availW);
            shapeText(s, index, wrap, -1.0f);
            outW = d.wMode == SizeMode::Fixed ? d.w : n.shaped.width;
            outH = d.hMode == SizeMode::Fixed ? d.h : n.shaped.height;
            return;
        }
        if (d.layout == LayoutKind::Grid) {
            Scratch& t = scratchAt(d.depth);
            t.flow.clear();
            for (i32 c = index + 1; c < static_cast<i32>(n.end); c = static_cast<i32>(s.nodes[static_cast<size_t>(c)].end))
                if (s.nodes[static_cast<size_t>(c)].visible && !s.doc.nodes[s.nodes[static_cast<size_t>(c)].src].absolute) t.flow.push_back(c);
            const f32 innerW = d.wMode == SizeMode::Fixed ? d.w - d.padding[1] - d.padding[3] : -1.0f;
            const f32 innerH = d.hMode == SizeMode::Fixed ? d.h - d.padding[0] - d.padding[2] : -1.0f;
            gridItems(s, d, t.flow, t);
            gridSolve(s, d, t, innerW, innerH);
            outW = d.wMode == SizeMode::Fixed ? d.w : trackSum(t.cols, d.columnGap) + d.padding[1] + d.padding[3];
            outH = d.hMode == SizeMode::Fixed ? d.h : trackSum(t.rows, d.rowGap) + d.padding[0] + d.padding[2];
            return;
        }
        if (d.layout != LayoutKind::Flex) {
            outW = d.w;
            outH = d.h;
            return;
        }
        f32 main = 0.0f, cross = 0.0f;
        u32 count = 0;
        const f32 padMain = d.column ? d.padding[0] + d.padding[2] : d.padding[1] + d.padding[3];
        const f32 padCross = d.column ? d.padding[1] + d.padding[3] : d.padding[0] + d.padding[2];
        const f32 innerW = d.wMode == SizeMode::Fixed ? d.w - d.padding[1] - d.padding[3] : (availW >= 0 ? availW - d.padding[1] - d.padding[3] : -1.0f);
        for (i32 c = index + 1; c < static_cast<i32>(n.end); c = static_cast<i32>(s.nodes[static_cast<size_t>(c)].end)) {
            RNode& cn = s.nodes[static_cast<size_t>(c)];
            const Node& cd = s.doc.nodes[cn.src];
            if (!cn.visible || cd.absolute) continue;
            f32 cw = 0, ch = 0;
            childSize(s, c, d.column ? innerW : -1.0f, cw, ch);
            const f32 along = d.column ? ch + cd.margin[0] + cd.margin[2] : cw + cd.margin[1] + cd.margin[3];
            const f32 across = d.column ? cw + cd.margin[1] + cd.margin[3] : ch + cd.margin[0] + cd.margin[2];
            main += along;
            cross = std::max(cross, across);
            ++count;
        }
        if (count > 1) main += (d.column ? d.rowGap : d.columnGap) * static_cast<f32>(count - 1);
        const f32 w = d.column ? cross + padCross : main + padMain;
        const f32 h = d.column ? main + padMain : cross + padCross;
        outW = d.wMode == SizeMode::Fixed ? d.w : w;
        outH = d.hMode == SizeMode::Fixed ? d.h : h;
    }

    // Size a child wants (fill counts as minimum size)
    void childSize(Screen& s, i32 index, f32 availW, f32& w, f32& h) {
        const Node& d = s.doc.nodes[s.nodes[static_cast<size_t>(index)].src];
        f32 iw = d.w, ih = d.h;
        if (d.wMode == SizeMode::Hug || d.hMode == SizeMode::Hug || d.kind == NodeKind::Text) intrinsic(s, index, availW, iw, ih);
        w = d.wMode == SizeMode::Fixed ? d.w : d.wMode == SizeMode::Hug ? iw : d.minW;
        h = d.hMode == SizeMode::Fixed ? d.h : d.hMode == SizeMode::Hug ? ih : d.minH;
        w = std::clamp(w, d.minW, d.maxW);
        h = std::clamp(h, d.minH, d.maxH);
    }

    void place(Screen& s, i32 index, Rect r) {
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        if (!d.widthRatioField.empty()) { // width ratio binding (health bars)
            Value v;
            if (resolve(s, n, d.widthRatioField, v)) r.w *= static_cast<f32>(std::clamp(numberOf(v), 0.0, 1.0));
            else missingField(s, d.widthRatioField);
        }
        if (!d.scaleField.empty()) { // scale binding: about the box centre
            Value v;
            if (resolve(s, n, d.scaleField, v)) {
                const f32 k = static_cast<f32>(std::max(0.0, numberOf(v)));
                r.x += r.w * (1.0f - k) * 0.5f;
                r.y += r.h * (1.0f - k) * 0.5f;
                r.w *= k;
                r.h *= k;
            } else {
                missingField(s, d.scaleField);
            }
        }
        if (!d.offsetXField.empty()) { // offset binding: moves the box (children follow, laid out inside it)
            Value x, y;
            if (resolve(s, n, d.offsetXField, x) && resolve(s, n, d.offsetYField, y)) {
                r.x += static_cast<f32>(numberOf(x));
                r.y += static_cast<f32>(numberOf(y));
            } else {
                missingField(s, d.offsetXField + " / " + d.offsetYField);
            }
        }
        n.rect = r;
        if (d.kind == NodeKind::Text) {
            const f32 wrap = d.wMode == SizeMode::Hug ? -1.0f : r.w;
            shapeText(s, index, wrap, d.hMode == SizeMode::Fixed || d.hMode == SizeMode::Fill ? r.h : -1.0f);
        }
        layoutChildren(s, index);
    }

    void placeConstrained(Screen& s, i32 c, const Rect& parentRect, f32 designW, f32 designH, f32 scrollX, f32 scrollY) {
        const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(c)].src];
        f32 w = 0, h = 0;
        childSize(s, c, cd.wMode == SizeMode::Fill ? parentRect.w - cd.x : -1.0f, w, h);
        if (cd.wMode == SizeMode::Fill) w = std::clamp(parentRect.w - cd.x, cd.minW, cd.maxW);
        if (cd.hMode == SizeMode::Fill) h = std::clamp(parentRect.h - cd.y, cd.minH, cd.maxH);
        auto axis = [](Constraint k, f32 pos, f32 size, f32 design, f32 actual, f32& outPos, f32& outSize) {
            outSize = size;
            switch (k) {
            case Constraint::Start: outPos = pos; break;
            case Constraint::End: outPos = actual - (design - pos - size) - size; break;
            case Constraint::Both:
                outPos = pos;
                outSize = std::max(0.0f, actual - pos - (design - pos - size));
                break;
            case Constraint::Center: outPos = actual * 0.5f + (pos + size * 0.5f - design * 0.5f) - size * 0.5f; break;
            case Constraint::Scale: {
                const f32 k2 = design > 0 ? actual / design : 1.0f;
                outPos = pos * k2;
                outSize = size * k2;
                break;
            }
            }
        };
        f32 x = 0, y = 0, outW = w, outH = h;
        axis(cd.ch, cd.x, w, designW, parentRect.w, x, outW);
        axis(cd.cv, cd.y, h, designH, parentRect.h, y, outH);
        place(s, c, {parentRect.x + x - scrollX, parentRect.y + y - scrollY, outW, outH});
    }

    void layoutChildren(Screen& s, i32 index) {
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        const bool scroll = d.widget == WidgetKind::Scroll;
        const f32 sx = scroll && d.scrollHorizontal ? n.scroll : 0.0f, sy = scroll && !d.scrollHorizontal ? n.scroll : 0.0f;
        const Rect r = n.rect;
        const f32 designW = d.w > 0 ? d.w : r.w, designH = d.h > 0 ? d.h : r.h;
        std::vector<i32>& flow = scratchAt(d.depth).flow;
        flow.clear();
        for (i32 c = index + 1; c < static_cast<i32>(n.end); c = static_cast<i32>(s.nodes[static_cast<size_t>(c)].end)) {
            RNode& cn = s.nodes[static_cast<size_t>(c)];
            const Node& cd = s.doc.nodes[cn.src];
            cn.visible = !cd.hidden && (cd.visibleField.empty() || flag(s, cn, cd.visibleField));
            if (!cn.visible) continue;
            if (d.layout == LayoutKind::None || cd.absolute) placeConstrained(s, c, r, designW, designH, sx, sy);
            else flow.push_back(c);
        }
        if (d.layout == LayoutKind::Flex && !flow.empty()) flex(s, index, flow, sx, sy);
        if (d.layout == LayoutKind::Grid && !flow.empty()) grid(s, index, flow, sx, sy);
        // Scroll area: end of content
        if (scroll) {
            f32 extent = 0.0f;
            for (i32 c = index + 1; c < static_cast<i32>(n.end); c = static_cast<i32>(s.nodes[static_cast<size_t>(c)].end)) {
                const RNode& cn = s.nodes[static_cast<size_t>(c)];
                if (!cn.visible) continue;
                extent = std::max(extent, d.scrollHorizontal ? cn.rect.x + cn.rect.w + sx - r.x : cn.rect.y + cn.rect.h + sy - r.y);
            }
            s.nodes[static_cast<size_t>(index)].contentExtent = extent + (d.scrollHorizontal ? d.padding[1] : d.padding[2]);
        }
    }

    void flex(Screen& s, i32 index, const std::vector<i32>& flow, f32 sx, f32 sy) {
        const RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        const bool col = d.column;
        const Rect r = n.rect;
        const f32 innerX = r.x + d.padding[3] - sx, innerY = r.y + d.padding[0] - sy;
        const f32 innerW = std::max(0.0f, r.w - d.padding[1] - d.padding[3]);
        const f32 innerH = std::max(0.0f, r.h - d.padding[0] - d.padding[2]);
        const f32 mainSize = col ? innerH : innerW, crossSize = col ? innerW : innerH;
        const f32 mainGap = col ? d.rowGap : d.columnGap, crossGap = col ? d.columnGap : d.rowGap;

        Scratch& t = scratchAt(d.depth);
        std::vector<FlexItem>& items = t.items;
        items.clear();
        for (i32 c : flow) {
            const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(c)].src];
            f32 w = 0, h = 0;
            childSize(s, c, col ? innerW - cd.margin[1] - cd.margin[3] : -1.0f, w, h);
            FlexItem it;
            it.node = c;
            it.main = col ? h : w;
            it.cross = col ? w : h;
            it.m0 = col ? cd.margin[0] : cd.margin[3];
            it.m1 = col ? cd.margin[2] : cd.margin[1];
            it.c0 = col ? cd.margin[3] : cd.margin[0];
            it.c1 = col ? cd.margin[1] : cd.margin[2];
            const SizeMode mainMode = col ? cd.hMode : cd.wMode, crossMode = col ? cd.wMode : cd.hMode;
            it.grow = mainMode == SizeMode::Fill;
            const Align self = cd.alignSelf != Align::Auto ? cd.alignSelf : d.alignItems;
            it.stretch = crossMode == SizeMode::Fill || (self == Align::Stretch && crossMode != SizeMode::Fixed);
            it.minMain = col ? cd.minH : cd.minW;
            it.maxMain = col ? cd.maxH : cd.maxW;
            items.push_back(it);
        }
        if (d.reverse) std::reverse(items.begin(), items.end());

        // Line wrapping (wrap)
        std::vector<FlexLine>& lines = t.lines;
        lines.clear();
        {
            size_t begin = 0;
            f32 used = 0.0f;
            for (size_t i = 0; i < items.size(); ++i) {
                const f32 size = items[i].main + items[i].m0 + items[i].m1;
                if (d.wrap && i > begin && used + mainGap + size > mainSize + 0.01f) {
                    lines.push_back({begin, i});
                    begin = i;
                    used = 0.0f;
                }
                used += (i > begin ? mainGap : 0.0f) + size;
            }
            lines.push_back({begin, items.size()});
        }
        // Distribute remaining space to fill children, line heights
        for (FlexLine& line : lines) {
            f32 used = 0.0f;
            u32 grow = 0;
            for (size_t i = line.begin; i < line.end; ++i) {
                used += items[i].main + items[i].m0 + items[i].m1;
                if (items[i].grow) ++grow;
            }
            used += mainGap * static_cast<f32>(line.end - line.begin - 1);
            const f32 free = mainSize - used;
            if (grow > 0 && free > 0.0f)
                for (size_t i = line.begin; i < line.end; ++i)
                    if (items[i].grow)
                        items[i].main = std::clamp(items[i].main + free / static_cast<f32>(grow), items[i].minMain, items[i].maxMain);
            for (size_t i = line.begin; i < line.end; ++i) line.cross = std::max(line.cross, items[i].cross + items[i].c0 + items[i].c1);
        }
        if (lines.size() == 1 && !d.wrap) lines[0].cross = crossSize; // a single line takes the whole cross axis
        // Multi-line distribution
        f32 linesCross = 0.0f;
        for (const FlexLine& l : lines) linesCross += l.cross;
        linesCross += crossGap * static_cast<f32>(lines.size() - 1);
        f32 crossPos = 0.0f, crossBetween = crossGap;
        if (lines.size() > 1) {
            const f32 freeCross = crossSize - linesCross;
            if (d.alignContent == Align::Stretch && freeCross > 0) {
                for (FlexLine& l : lines) l.cross += freeCross / static_cast<f32>(lines.size());
            } else if (d.alignContent == Align::Center) {
                crossPos = freeCross * 0.5f;
            } else if (d.alignContent == Align::End) {
                crossPos = freeCross;
            } else if (d.alignContentJustify == Justify::SpaceBetween && lines.size() > 1) {
                crossBetween += std::max(0.0f, freeCross) / static_cast<f32>(lines.size() - 1);
            } else if (d.alignContentJustify == Justify::SpaceAround) {
                crossBetween += std::max(0.0f, freeCross) / static_cast<f32>(lines.size());
                crossPos = std::max(0.0f, freeCross) / static_cast<f32>(lines.size()) * 0.5f;
            } else if (d.alignContentJustify == Justify::SpaceEvenly) {
                crossBetween += std::max(0.0f, freeCross) / static_cast<f32>(lines.size() + 1);
                crossPos = std::max(0.0f, freeCross) / static_cast<f32>(lines.size() + 1);
            }
        }
        for (const FlexLine& line : lines) {
            f32 used = 0.0f;
            for (size_t i = line.begin; i < line.end; ++i) used += items[i].main + items[i].m0 + items[i].m1;
            const size_t count = line.end - line.begin;
            f32 free = mainSize - used - mainGap * static_cast<f32>(count - 1);
            f32 pos = 0.0f, between = mainGap;
            switch (d.justify) {
            case Justify::Start: break;
            case Justify::Center: pos = free * 0.5f; break;
            case Justify::End: pos = free; break;
            case Justify::SpaceBetween:
                if (count > 1 && free > 0) between += free / static_cast<f32>(count - 1);
                break;
            case Justify::SpaceAround:
                if (free > 0) {
                    between += free / static_cast<f32>(count);
                    pos = free / static_cast<f32>(count) * 0.5f;
                }
                break;
            case Justify::SpaceEvenly:
                if (free > 0) {
                    between += free / static_cast<f32>(count + 1);
                    pos = free / static_cast<f32>(count + 1);
                }
                break;
            }
            for (size_t i = line.begin; i < line.end; ++i) {
                FlexItem& it = items[i];
                const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(it.node)].src];
                const Align self = cd.alignSelf != Align::Auto ? cd.alignSelf : d.alignItems;
                const f32 lineCross = line.cross - it.c0 - it.c1;
                const f32 cross = it.stretch ? lineCross : it.cross;
                f32 offset = 0.0f;
                if (!it.stretch) {
                    if (self == Align::Center) offset = (lineCross - cross) * 0.5f;
                    else if (self == Align::End) offset = lineCross - cross;
                }
                pos += it.m0;
                const f32 mx = pos, cx = crossPos + it.c0 + offset;
                Rect cr = col ? Rect{innerX + cx, innerY + mx, cross, it.main} : Rect{innerX + mx, innerY + cx, it.main, cross};
                place(s, it.node, cr);
                pos += it.main + it.m1 + between;
            }
            crossPos += line.cross + crossBetween;
        }
    }

    // ---------------------------------------------------------------- Grid

    // Decides a cell per child: the given cell if any, otherwise empty cells in row (or column) order. Outside the tracks, clamped to the last track
    void gridItems(Screen& s, const Node& d, const std::vector<i32>& flow, Scratch& t) {
        const u32 rows = static_cast<u32>(std::max<size_t>(1, d.rows.size())), cols = static_cast<u32>(std::max<size_t>(1, d.columns.size()));
        std::vector<u8>& used = t.used;
        used.assign(static_cast<size_t>(rows) * cols, 0);
        auto mark = [&](const GridItem& it) {
            for (u32 r = it.row; r < it.row + it.rowSpan; ++r)
                for (u32 c = it.col; c < it.col + it.colSpan; ++c) used[static_cast<size_t>(r) * cols + c] = 1;
        };
        std::vector<GridItem>& items = t.grid;
        items.clear();
        for (i32 c : flow) { // explicitly placed cells first
            const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(c)].src];
            GridItem it{c, 0, 0, 1, 1};
            if (cd.cellRow > 0 && cd.cellColumn > 0) {
                it.row = std::min<u32>(cd.cellRow - 1u, rows - 1);
                it.col = std::min<u32>(cd.cellColumn - 1u, cols - 1);
                it.rowSpan = std::clamp<u32>(cd.rowSpan, 1, rows - it.row);
                it.colSpan = std::clamp<u32>(cd.columnSpan, 1, cols - it.col);
                mark(it);
            } else {
                it.row = it.col = ~0u;
            }
            items.push_back(it);
        }
        u32 cursor = 0;
        for (GridItem& it : items) {
            if (it.row != ~0u) continue;
            while (cursor < rows * cols) {
                const u32 r = d.gridColumnFlow ? cursor % rows : cursor / cols;
                const u32 c = d.gridColumnFlow ? cursor / rows : cursor % cols;
                ++cursor;
                if (!used[static_cast<size_t>(r) * cols + c]) {
                    it.row = r;
                    it.col = c;
                    break;
                }
            }
            if (it.row == ~0u) it.row = rows - 1, it.col = cols - 1; // if out of cells, overlaps the last cell
            mark(it);
        }
    }

    // Track sizes: fixed/% as is, auto is the largest size of single-cell children, fr shares the remaining space by ratio (minimum is content).
    // If inner < 0, sized to content (% and fr behave like auto). Without fr and with stretch, auto tracks share the remaining space
    static void gridTracks(const std::vector<Track>& tracks, f32 inner, f32 gap, const std::vector<f32>& content, bool stretch,
                           std::vector<f32>& out, std::vector<u8>& fixed) {
        const size_t n = std::max<size_t>(1, tracks.size());
        out.assign(n, 0.0f);
        f32 used = gap * static_cast<f32>(n - 1), fr = 0.0f;
        u32 autos = 0;
        for (size_t i = 0; i < n; ++i) {
            const Track t = i < tracks.size() ? tracks[i] : Track{};
            const f32 c = i < content.size() ? content[i] : 0.0f;
            switch (t.kind) {
            case TrackKind::Fixed: out[i] = t.value; break;
            case TrackKind::Percent: out[i] = inner >= 0.0f ? inner * t.value * 0.01f : c; break;
            case TrackKind::Auto: out[i] = c, ++autos; break;
            case TrackKind::Flex: out[i] = c, fr += t.value; break;
            }
            if (t.kind != TrackKind::Flex) used += out[i];
        }
        if (inner < 0.0f) return;
        if (fr > 0.0f) {
            // fr tracks that would shrink below content are fixed at content size and the rest is redistributed
            fixed.assign(n, 0);
            for (int pass = 0; pass < 4; ++pass) {
                f32 left = inner - used, frLeft = 0.0f;
                for (size_t i = 0; i < n; ++i)
                    if (i < tracks.size() && tracks[i].kind == TrackKind::Flex) {
                        if (fixed[i]) left -= out[i];
                        else frLeft += tracks[i].value;
                    }
                bool again = false;
                for (size_t i = 0; i < n; ++i) {
                    if (i >= tracks.size() || tracks[i].kind != TrackKind::Flex || fixed[i]) continue;
                    const f32 c = i < content.size() ? content[i] : 0.0f;
                    const f32 share = frLeft > 0.0f ? std::max(0.0f, left) * tracks[i].value / frLeft : 0.0f;
                    if (share < c) fixed[i] = 1, out[i] = c, again = true;
                    else out[i] = share;
                }
                if (!again) break;
            }
        } else if (stretch && autos > 0 && inner > used) {
            const f32 extra = (inner - used) / static_cast<f32>(autos);
            for (size_t i = 0; i < n; ++i)
                if (i >= tracks.size() || tracks[i].kind == TrackKind::Auto) out[i] += extra;
        }
    }

    // Columns are decided first (without text wrapping), then row heights with those widths (t.grid → t.cols, t.rows). If innerW/innerH < 0, sized to content
    void gridSolve(Screen& s, const Node& d, Scratch& t, f32 innerW, f32 innerH) {
        t.colContent.assign(std::max<size_t>(1, d.columns.size()), 0.0f);
        t.rowContent.assign(std::max<size_t>(1, d.rows.size()), 0.0f);
        for (const GridItem& it : t.grid) {
            if (it.colSpan != 1) continue;
            const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(it.node)].src];
            f32 w = 0, h = 0;
            childSize(s, it.node, -1.0f, w, h);
            t.colContent[it.col] = std::max(t.colContent[it.col], w + cd.margin[1] + cd.margin[3]);
        }
        gridTracks(d.columns, innerW, d.columnGap, t.colContent, d.justifyStretch, t.cols, t.fixed);
        for (const GridItem& it : t.grid) {
            if (it.rowSpan != 1) continue;
            const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(it.node)].src];
            f32 cellW = d.columnGap * static_cast<f32>(it.colSpan - 1);
            for (u32 c = it.col; c < it.col + it.colSpan; ++c) cellW += t.cols[c];
            f32 w = 0, h = 0;
            childSize(s, it.node, cellW - cd.margin[1] - cd.margin[3], w, h);
            t.rowContent[it.row] = std::max(t.rowContent[it.row], h + cd.margin[0] + cd.margin[2]);
        }
        gridTracks(d.rows, innerH, d.rowGap, t.rowContent, d.alignContent == Align::Stretch, t.rows, t.fixed);
    }

    static f32 trackSum(const std::vector<f32>& sizes, f32 gap) {
        f32 sum = gap * static_cast<f32>(sizes.size() - 1);
        for (f32 v : sizes) sum += v;
        return sum;
    }

    // Distribution of remaining space (justify: columns, alignContent: rows) → track start positions
    static void trackStarts(const std::vector<f32>& sizes, f32 gap, f32 inner, Justify j, Align a, std::vector<f32>& starts) {
        const size_t n = sizes.size();
        const f32 free = std::max(0.0f, inner - trackSum(sizes, gap));
        f32 pos = 0.0f, between = gap;
        if (a == Align::Center) pos = free * 0.5f;
        else if (a == Align::End) pos = free;
        else if (j == Justify::Center) pos = free * 0.5f;
        else if (j == Justify::End) pos = free;
        else if (j == Justify::SpaceBetween && n > 1) between += free / static_cast<f32>(n - 1);
        else if (j == Justify::SpaceAround) between += free / static_cast<f32>(n), pos = free / static_cast<f32>(n) * 0.5f;
        else if (j == Justify::SpaceEvenly) between += free / static_cast<f32>(n + 1), pos = free / static_cast<f32>(n + 1);
        starts.resize(n);
        for (size_t i = 0; i < n; ++i) {
            starts[i] = pos;
            pos += sizes[i] + between;
        }
    }

    void grid(Screen& s, i32 index, const std::vector<i32>& flow, f32 sx, f32 sy) {
        const RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        const Rect r = n.rect;
        const f32 innerX = r.x + d.padding[3] - sx, innerY = r.y + d.padding[0] - sy;
        const f32 innerW = std::max(0.0f, r.w - d.padding[1] - d.padding[3]);
        const f32 innerH = std::max(0.0f, r.h - d.padding[0] - d.padding[2]);
        Scratch& t = scratchAt(d.depth);
        gridItems(s, d, flow, t);
        gridSolve(s, d, t, innerW, innerH);
        const std::vector<f32>& cols = t.cols;
        const std::vector<f32>& rows = t.rows;
        std::vector<f32>& colStart = t.colStart;
        std::vector<f32>& rowStart = t.rowStart;
        // Column distribution uses justify, row distribution alignContent (if Align is start/center/end, that; otherwise space-*)
        trackStarts(cols, d.columnGap, innerW, d.justify, Align::Start, colStart);
        trackStarts(rows, d.rowGap, innerH, d.alignContentJustify, d.alignContent, rowStart);
        for (const GridItem& it : t.grid) {
            const Node& cd = s.doc.nodes[s.nodes[static_cast<size_t>(it.node)].src];
            const u32 c1 = it.col + it.colSpan - 1, r1 = it.row + it.rowSpan - 1;
            const f32 cellX = colStart[it.col] + cd.margin[3], cellY = rowStart[it.row] + cd.margin[0];
            const f32 cellW = std::max(0.0f, colStart[c1] + cols[c1] - colStart[it.col] - cd.margin[1] - cd.margin[3]);
            const f32 cellH = std::max(0.0f, rowStart[r1] + rows[r1] - rowStart[it.row] - cd.margin[0] - cd.margin[2]);
            f32 w = 0, h = 0;
            childSize(s, it.node, cellW, w, h);
            const Align hAlign = cd.justifySelf != Align::Auto ? cd.justifySelf : d.justifyItems;
            const Align vAlign = cd.alignSelf != Align::Auto ? cd.alignSelf : d.alignItems;
            if (cd.wMode == SizeMode::Fill || (hAlign == Align::Stretch && cd.wMode != SizeMode::Fixed)) w = std::clamp(cellW, cd.minW, cd.maxW);
            if (cd.hMode == SizeMode::Fill || (vAlign == Align::Stretch && cd.hMode != SizeMode::Fixed)) h = std::clamp(cellH, cd.minH, cd.maxH);
            const f32 ox = hAlign == Align::Center ? (cellW - w) * 0.5f : hAlign == Align::End ? cellW - w : 0.0f;
            const f32 oy = vAlign == Align::Center ? (cellH - h) * 0.5f : vAlign == Align::End ? cellH - h : 0.0f;
            place(s, it.node, {innerX + cellX + ox, innerY + cellY + oy, w, h});
        }
    }

    void layout(Screen& s, f32 rootW, f32 rootH) {
        if (s.nodes.empty()) return;
        // Resolve text bindings first (only changed text is re-broken)
        for (size_t i = 0; i < s.nodes.size(); ++i) {
            RNode& n = s.nodes[i];
            const Node& d = s.doc.nodes[n.src];
            if (d.kind == NodeKind::Text) {
                resolveText(s, static_cast<i32>(i), textBuf);
                if (textBuf != n.text) n.text.assign(textBuf);
            }
            n.disabled = !d.disabledField.empty() && flag(s, n, d.disabledField);
            n.checked = false;
            if (d.widget == WidgetKind::Toggle) {
                Value v;
                if (resolve(s, n, d.field, v)) n.checked = truthy(v);
                else missingField(s, d.field);
            }
        }
        RNode& root = s.nodes[0];
        root.visible = !s.doc.nodes[0].hidden;
        place(s, 0, {0.0f, 0.0f, rootW, rootH});
        // If the parent is hidden, descendants are hidden too
        for (size_t i = 1; i < s.nodes.size(); ++i)
            if (!s.nodes[static_cast<size_t>(s.nodes[i].parent)].visible) s.nodes[i].visible = false;
    }

    // ---------------------------------------------------------------- Drawing

    void emitText(Screen& s, const RNode& n, const Node& d, const Look& v, f32 opacity, const f32 clip[4], bool caret) {
        const Shaped& sh = n.shaped;
        const f32 k = s.scale;
        if (n.placeholder) opacity *= 0.45f;
        f32 y = n.rect.y;
        f32 caretX = d.textAlign == TextAlign::Center ? n.rect.x + n.rect.w * 0.5f
                     : d.textAlign == TextAlign::Right ? n.rect.x + n.rect.w
                                                        : n.rect.x;
        f32 caretY = y, caretH = d.text.size * d.lineHeight;
        if (d.hMode != SizeMode::Hug) {
            if (d.verticalAlign == VerticalAlign::Center) y += (n.rect.h - sh.height) * 0.5f;
            else if (d.verticalAlign == VerticalAlign::Bottom) y += n.rect.h - sh.height;
        }
        const f32 sdfUnits = atlas.sdfUnitsPerPixel();
        for (const ShapedLine& line : sh.lines) {
            f32 x = n.rect.x;
            if (d.textAlign == TextAlign::Center) x += (n.rect.w - line.width) * 0.5f;
            else if (d.textAlign == TextAlign::Right) x += n.rect.w - line.width;
            const f32 baseline = y + (line.height - (line.ascent + line.descent)) * 0.5f + line.ascent;
            caretX = x + line.width;
            caretY = y;
            caretH = line.height;
            for (u32 i = line.begin; i < line.end; ++i) {
                const ShapedGlyph& g = sh.glyphs[i];
                const TextRun& run = sh.runs[g.run];
                u32 color = v.textColor;
                if (run.span >= 0) color = theme.color(d.spans[static_cast<size_t>(run.span)].color, color);
                color = withAlpha(color, opacity);
                if (g.glyph.u1 > g.glyph.u0) {
                    Quad q;
                    q.rect[0] = s.originX + (x + g.glyph.x0 * run.size) * k;
                    q.rect[1] = s.originY + (baseline + g.glyph.y0 * run.size) * k;
                    q.rect[2] = (g.glyph.x1 - g.glyph.x0) * run.size * k;
                    q.rect[3] = (g.glyph.y1 - g.glyph.y0) * run.size * k;
                    std::memcpy(q.clip, clip, sizeof(q.clip));
                    q.uv[0] = g.glyph.u0;
                    q.uv[1] = g.glyph.v0;
                    q.uv[2] = g.glyph.u1;
                    q.uv[3] = g.glyph.v1;
                    q.color0 = color;
                    q.flags = kQuadGlyph;
                    q.sdfScale = sdfUnits * run.size * k / kBakePixels;
                    quads.push_back(q);
                }
                if ((run.underline || run.strike) && g.cp != '\n') {
                    const FontMetrics& fm = atlas.metrics(g.font);
                    const f32 thick = std::max(1.0f / k, run.size * 0.06f);
                    auto deco = [&](f32 dy) {
                        Quad q;
                        q.rect[0] = s.originX + x * k;
                        q.rect[1] = s.originY + (baseline + dy) * k;
                        q.rect[2] = g.advance * k;
                        q.rect[3] = thick * k;
                        std::memcpy(q.clip, clip, sizeof(q.clip));
                        q.color0 = color;
                        q.flags = kQuadShape;
                        quads.push_back(q);
                    };
                    if (run.underline) deco(run.size * 0.12f);
                    if (run.strike) deco(-fm.ascent * run.size * 0.32f);
                }
                x += g.advance;
            }
            y += line.height;
        }
        if (caret && !n.placeholder) { // typing in a text input: a blinking vertical bar at the end
            Quad q;
            q.rect[0] = s.originX + caretX * k;
            q.rect[1] = s.originY + (caretY + caretH * 0.1f) * k;
            q.rect[2] = std::max(1.0f, 2.0f * k);
            q.rect[3] = caretH * 0.8f * k;
            std::memcpy(q.clip, clip, sizeof(q.clip));
            q.color0 = withAlpha(v.textColor, opacity);
            q.flags = kQuadShape;
            quads.push_back(q);
        }
    }

    void emitNode(Screen& s, i32 index, f32 opacity, const f32 clip[4]) {
        const RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        Look v = lookOf(d.visual);
        const i32 si = s.stateIndex[n.src];
        if (si >= 0)
            for (int st = 1; st < static_cast<int>(State::Count); ++st)
                lerpLook(v, s.stateVisuals[static_cast<size_t>(si)][static_cast<size_t>(st)], n.mix[st]);
        if (!d.colorField.empty()) { // color binding (Color or u32 rgba)
            Value cv;
            if (resolve(s, n, d.colorField, cv) && cv.size == 4) {
                u32 c = 0;
                std::memcpy(&c, cv.data, 4);
                if (d.kind == NodeKind::Text) {
                    v.textColor = c;
                } else if (d.kind == NodeKind::Image) {
                    v.tint = c;
                } else {
                    v.fill = PaintKind::Solid;
                    v.fill0 = c;
                    v.fill1 = 0;
                }
            }
        }
        if (!d.opacityField.empty()) { // opacity binding (fades the subtree)
            Value ov;
            if (resolve(s, n, d.opacityField, ov)) v.opacity *= static_cast<f32>(std::clamp(numberOf(ov), 0.0, 1.0));
        }
        if (!d.brightnessField.empty()) { // brightness binding (images)
            Value bv;
            if (resolve(s, n, d.brightnessField, bv)) v.brightness += static_cast<f32>(numberOf(bv));
        }
        opacity *= v.opacity;
        const f32 k = s.scale;
        const Rect r{s.originX + n.rect.x * k, s.originY + n.rect.y * k, n.rect.w * k, n.rect.h * k};
        const f32 rotation = d.rotation * kDegToRad;
        auto base = [&](Quad& q) {
            std::memcpy(q.clip, clip, sizeof(q.clip));
            q.rotation = rotation;
        };
        if (d.kind != NodeKind::Text) {
            for (const Shadow& sh : *v.shadows) {
                Quad q;
                base(q);
                q.rect[0] = r.x + (sh.dx - sh.spread) * k;
                q.rect[1] = r.y + (sh.dy - sh.spread) * k;
                q.rect[2] = r.w + sh.spread * 2.0f * k;
                q.rect[3] = r.h + sh.spread * 2.0f * k;
                for (int i = 0; i < 4; ++i) q.radii[i] = std::max(0.0f, v.radii[i] + sh.spread) * k;
                q.color0 = withAlpha(sh.color.rgba, opacity);
                q.flags = kQuadShadow;
                q.softness = sh.blur * 0.5f * k;
                quads.push_back(q);
            }
        }
        if (d.kind == NodeKind::Frame && (v.fill != PaintKind::None || v.strokeWidth > 0.0f)) {
            Quad q;
            base(q);
            q.rect[0] = r.x;
            q.rect[1] = r.y;
            q.rect[2] = r.w;
            q.rect[3] = r.h;
            for (int i = 0; i < 4; ++i) q.radii[i] = v.radii[i] * k;
            q.color0 = v.fill == PaintKind::None ? 0u : withAlpha(v.fill0, opacity);
            q.color1 = withAlpha(v.fill1, opacity);
            std::memcpy(q.gradient, v.gradient, sizeof(q.gradient));
            q.flags = kQuadShape | (v.fill == PaintKind::Linear ? kQuadLinear : v.fill == PaintKind::Radial ? kQuadRadial : 0u);
            q.strokeWidth = v.strokeWidth * k;
            q.strokeColor = withAlpha(v.strokeColor, opacity);
            quads.push_back(q);
        }
        const std::string* imagePath = d.kind == NodeKind::Image ? imagePathOf(s, n, d) : nullptr;
        if (imagePath) {
            const ImageInfo img = atlas.image(*imagePath);
            if (!img.valid) bindingError(s, "image assets/" + *imagePath + " not found");
            if (img.valid) {
                const u32 tint = withAlpha(v.tint, opacity);
                // Brightness: the shader mixes the picture toward color1 by its alpha (white for +, black for -)
                const f32 bright = std::clamp(v.brightness, -1.0f, 1.0f);
                const u8 mixAmount = static_cast<u8>(std::lround(std::fabs(bright) * 255.0f));
                const u32 brightness = bright >= 0.0f ? rgba(255, 255, 255, mixAmount) : rgba(0, 0, 0, mixAmount);
                const bool sliced = d.slice[0] > 0 || d.slice[1] > 0 || d.slice[2] > 0 || d.slice[3] > 0;
                auto piece = [&](f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, f32 u1, f32 v1) {
                    if (x1 <= x0 || y1 <= y0) return;
                    Quad q;
                    base(q);
                    q.rect[0] = x0;
                    q.rect[1] = y0;
                    q.rect[2] = x1 - x0;
                    q.rect[3] = y1 - y0;
                    q.uv[0] = u0;
                    q.uv[1] = v0;
                    q.uv[2] = u1;
                    q.uv[3] = v1;
                    q.color0 = tint;
                    q.color1 = brightness;
                    q.flags = kQuadImage;
                    if (!sliced)
                        for (int i = 0; i < 4; ++i) q.radii[i] = v.radii[i] * k;
                    quads.push_back(q);
                };
                if (!sliced) {
                    piece(r.x, r.y, r.x + r.w, r.y + r.h, d.flipX ? img.u1 : img.u0, d.flipY ? img.v1 : img.v0, d.flipX ? img.u0 : img.u1,
                          d.flipY ? img.v0 : img.v1);
                } else { // 9-slice: margins are source pixels = logical units
                    const f32 du = (img.u1 - img.u0) / static_cast<f32>(img.width), dv = (img.v1 - img.v0) / static_cast<f32>(img.height);
                    const f32 xs[4] = {r.x, r.x + d.slice[0] * k, r.x + r.w - d.slice[2] * k, r.x + r.w};
                    const f32 ys[4] = {r.y, r.y + d.slice[1] * k, r.y + r.h - d.slice[3] * k, r.y + r.h};
                    const f32 us[4] = {img.u0, img.u0 + d.slice[0] * du, img.u1 - d.slice[2] * du, img.u1};
                    const f32 vs[4] = {img.v0, img.v0 + d.slice[1] * dv, img.v1 - d.slice[3] * dv, img.v1};
                    for (int yi = 0; yi < 3; ++yi)
                        for (int xi = 0; xi < 3; ++xi) piece(xs[xi], ys[yi], xs[xi + 1], ys[yi + 1], us[xi], vs[yi], us[xi + 1], vs[yi + 1]);
                }
            }
        }
        if (d.kind == NodeKind::Text) emitText(s, n, d, v, opacity, clip, index == s.editing && std::fmod(time, 1.0) < 0.6);
        // Focus indicator (when moving with gamepad/keyboard)
        if (index == s.focus && (focusVisible || focusAlways) && d.widget != WidgetKind::None && d.widget != WidgetKind::List) {
            Quad q;
            base(q);
            const f32 pad = 3.0f * k;
            q.rect[0] = r.x - pad;
            q.rect[1] = r.y - pad;
            q.rect[2] = r.w + pad * 2;
            q.rect[3] = r.h + pad * 2;
            for (int i = 0; i < 4; ++i) q.radii[i] = v.radii[i] * k + pad;
            q.strokeWidth = 2.0f * k;
            q.strokeColor = theme.color({0, "focus"}, rgba(255, 210, 80, 255));
            q.flags = kQuadShape;
            quads.push_back(q);
        }
        // Children
        f32 childClip[4] = {clip[0], clip[1], clip[2], clip[3]};
        if (d.clip) {
            childClip[0] = std::max(clip[0], r.x);
            childClip[1] = std::max(clip[1], r.y);
            childClip[2] = std::min(clip[2], r.x + r.w);
            childClip[3] = std::min(clip[3], r.y + r.h);
        }
        for (i32 c = index + 1; c < static_cast<i32>(n.end); c = static_cast<i32>(s.nodes[static_cast<size_t>(c)].end))
            if (s.nodes[static_cast<size_t>(c)].visible) emitNode(s, c, opacity, childClip);
    }

    void prepare(Screen& s, f32 viewportW, f32 viewportH) {
        if (s.rebuild || listsChanged(s)) rebuild(s);
        const f32 bw = std::max(1.0f, s.doc.boardW), bh = std::max(1.0f, s.doc.boardH);
        s.scale = std::min(viewportW / bw, viewportH / bh) * uiScale;
        if (s.scale <= 0.0f) s.scale = 1.0f;
        s.rootW = viewportW / s.scale;
        s.rootH = viewportH / s.scale;
        layout(s, s.rootW, s.rootH);
    }

    // ---------------------------------------------------------------- Input

    bool focusable(const Screen& s, i32 i) const {
        const RNode& n = s.nodes[static_cast<size_t>(i)];
        const WidgetKind w = s.doc.nodes[n.src].widget;
        return n.visible && !n.disabled && w != WidgetKind::None && w != WidgetKind::List && w != WidgetKind::Scroll;
    }

    i32 hit(Screen& s, f32 x, f32 y, bool& blocks) {
        blocks = false;
        for (i32 i = static_cast<i32>(s.nodes.size()) - 1; i >= 0; --i) {
            const RNode& n = s.nodes[static_cast<size_t>(i)];
            if (!n.visible || !n.rect.contains(x, y)) continue;
            bool clipped = false; // ignored if outside a clipping parent
            for (i32 a = n.parent; a >= 0; a = s.nodes[static_cast<size_t>(a)].parent) {
                const RNode& an = s.nodes[static_cast<size_t>(a)];
                if (s.doc.nodes[an.src].clip && !an.rect.contains(x, y)) clipped = true;
            }
            if (clipped) continue;
            const Node& d = s.doc.nodes[n.src];
            if (d.widget != WidgetKind::None && d.widget != WidgetKind::List && !n.disabled) {
                blocks = true;
                return i;
            }
            if (i > 0 && d.kind == NodeKind::Frame && d.visual.fill.kind != PaintKind::None && (d.visual.fill.c0.rgba >> 24) > 0)
                blocks = true; // over a filled panel (the game behind doesn't receive the click)
        }
        return -1;
    }

    void emit(Screen& s, i32 index, const std::string& name, const std::string& field = {}) {
        const RNode& n = s.nodes[static_cast<size_t>(index)];
        Action a;
        a.name = name;
        a.screen = s.id;
        a.node = s.doc.nodes[n.src].name;
        a.field = field;
        a.listIndex = n.listIndex;
        actions.push_back(std::move(a));
    }

    void activate(Screen& s, i32 index, i32 step = 1) {
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        Value v;
        switch (d.widget) {
        case WidgetKind::Button: emit(s, index, d.action); break;
        case WidgetKind::Toggle:
            if (resolve(s, n, d.field, v)) {
                writeNumber(v, truthy(v) ? 0.0 : 1.0);
                emit(s, index, d.action.empty() ? "changed" : d.action, d.field);
            }
            break;
        case WidgetKind::Select:
            if (!d.options.empty() && resolve(s, n, d.field, v)) {
                const i32 count = static_cast<i32>(d.options.size());
                const i32 cur = static_cast<i32>(numberOf(v));
                writeNumber(v, static_cast<f64>(((cur + step) % count + count) % count));
                emit(s, index, d.action.empty() ? "changed" : d.action, d.field);
            }
            break;
        case WidgetKind::TextInput: s.editing = index; break; // start typing (Enter: action, Esc: stop)
        case WidgetKind::Slider:
            if (resolve(s, n, d.field, v)) {
                const f64 stepSize = d.sliderStep > 0 ? d.sliderStep : (d.sliderMax - d.sliderMin) / 20.0;
                setSlider(s, index, numberOf(v) + stepSize * step);
            }
            break;
        default: break;
        }
    }

    bool setSlider(Screen& s, i32 index, f64 value) { // true if the value changed and an action was fired
        RNode& n = s.nodes[static_cast<size_t>(index)];
        const Node& d = s.doc.nodes[n.src];
        Value v;
        if (!resolve(s, n, d.field, v)) return false;
        value = std::clamp(value, static_cast<f64>(d.sliderMin), static_cast<f64>(d.sliderMax));
        if (d.sliderStep > 0) value = d.sliderMin + std::round((value - d.sliderMin) / d.sliderStep) * d.sliderStep;
        if (std::fabs(numberOf(v) - value) < 1e-9) return false;
        writeNumber(v, value);
        emit(s, index, d.action.empty() ? "changed" : d.action, d.field);
        return true;
    }

    i32 findByName(const Screen& s, const std::string& name) const {
        for (size_t i = 0; i < s.nodes.size(); ++i)
            if (s.doc.nodes[s.nodes[i].src].name == name && focusable(s, static_cast<i32>(i))) return static_cast<i32>(i);
        return -1;
    }

    // Nearest widget in a direction (0 up, 1 down, 2 left, 3 right)
    // Scroll every scroll area holding the node so it's in view (keyboard/gamepad focus; the wheel scrolls directly)
    void scrollIntoView(Screen& s, i32 index) {
        if (index < 0) return;
        const Rect target = s.nodes[static_cast<size_t>(index)].rect;
        for (i32 p = s.nodes[static_cast<size_t>(index)].parent; p >= 0; p = s.nodes[static_cast<size_t>(p)].parent) {
            RNode& a = s.nodes[static_cast<size_t>(p)];
            const Node& d = s.doc.nodes[a.src];
            if (d.widget != WidgetKind::Scroll) continue;
            const f32 start = d.scrollHorizontal ? target.x - a.rect.x : target.y - a.rect.y;
            const f32 size = d.scrollHorizontal ? target.w : target.h, view = d.scrollHorizontal ? a.rect.w : a.rect.h;
            f32 next = a.scroll;
            if (start < 0.0f) next += start;
            else if (start + size > view) next += std::min(start + size - view, start);
            a.scroll = std::clamp(next, 0.0f, std::max(0.0f, a.contentExtent - view));
        }
    }

    i32 neighbor(Screen& s, i32 from, int dir) {
        const Node& d = s.doc.nodes[s.nodes[static_cast<size_t>(from)].src];
        if (!d.focusNext[dir].empty()) {
            const i32 named = findByName(s, d.focusNext[dir]);
            if (named >= 0) return named;
        }
        const Rect a = s.nodes[static_cast<size_t>(from)].rect;
        const f32 ax = a.x + a.w * 0.5f, ay = a.y + a.h * 0.5f;
        i32 best = -1;
        f32 bestScore = 1e30f;
        for (size_t i = 0; i < s.nodes.size(); ++i) {
            if (static_cast<i32>(i) == from || !focusable(s, static_cast<i32>(i))) continue;
            const Rect b = s.nodes[i].rect;
            const f32 bx = b.x + b.w * 0.5f, by = b.y + b.h * 0.5f;
            const f32 dx = bx - ax, dy = by - ay;
            const f32 primary = dir == 0 ? -dy : dir == 1 ? dy : dir == 2 ? -dx : dx;
            const f32 secondary = dir <= 1 ? std::fabs(dx) : std::fabs(dy);
            if (primary <= 1.0f) continue;
            const f32 score = primary + secondary * 2.0f;
            if (score < bestScore) {
                bestScore = score;
                best = static_cast<i32>(i);
            }
        }
        return best;
    }

    void update(const FrameInput& in, f64 dt) {
        actions.clear();
        wantsMouse = wantsKeyboard = false;
        time += dt;
        if (time >= nextReload) {
            nextReload = time + kReloadSeconds;
            reloadChanged();
        }
        order.clear();
        for (Screen& s : screens)
            if (!s.worldTemplate) order.push_back(&s);
        std::stable_sort(order.begin(), order.end(), [](const Screen* a, const Screen* b) { return a->layer > b->layer; });

        const bool mouse = in.mouse;
        const f32 mx = mouse ? in.mouseX : -1e9f, my = mouse ? in.mouseY : -1e9f;
        const bool down = mouse && in.mouseDown;
        const bool clicked = mouse && in.mousePressed;
        if (mouse && in.mouseMoved) focusVisible = false;

        bool mouseTaken = false;
        for (Screen* sp : order) {
            Screen& s = *sp;
            if (s.nodes.empty()) continue;
            const f32 lx = (mx - s.originX) / s.scale, ly = (my - s.originY) / s.scale;
            bool blocks = false;
            const bool mouseHere = !mouseTaken; // no screen above took the mouse (this screen's own panels set mouseTaken below)
            const i32 h = mouseTaken ? -1 : hit(s, lx, ly, blocks);
            if (blocks) mouseTaken = true;
            s.hover = h;
            if (clicked && s.editing >= 0 && h != s.editing) s.editing = -1; // clicking elsewhere stops typing
            if (clicked && h >= 0) {
                s.pressed = h;
                if (focusable(s, h)) s.focus = h;
            }
            if (s.pressed >= 0) {
                const Node& pd = s.doc.nodes[s.nodes[static_cast<size_t>(s.pressed)].src];
                if (pd.widget == WidgetKind::Slider && down) {
                    const Rect r = s.nodes[static_cast<size_t>(s.pressed)].rect;
                    const f64 t = std::clamp((lx - r.x) / std::max(r.w, 1.0f), 0.0f, 1.0f);
                    if (setSlider(s, s.pressed, pd.sliderMin + t * (pd.sliderMax - pd.sliderMin))) actions.back().dragging = true;
                }
                if (!down) {
                    if (pd.widget == WidgetKind::Slider) emit(s, s.pressed, pd.action.empty() ? "changed" : pd.action, pd.field); // released
                    else if (s.pressed == h) activate(s, h);
                    s.pressed = -1;
                }
            }
            if (mouse && in.wheel != 0.0f && mouseHere) {
                // Scrolling: the innermost scroll area under the mouse
                for (i32 i = static_cast<i32>(s.nodes.size()) - 1; i >= 0; --i) {
                    RNode& n = s.nodes[static_cast<size_t>(i)];
                    const Node& d = s.doc.nodes[n.src];
                    if (d.widget != WidgetKind::Scroll || !n.visible || !n.rect.contains(lx, ly)) continue;
                    const f32 view = d.scrollHorizontal ? n.rect.w : n.rect.h;
                    n.scroll = std::clamp(n.scroll - in.wheel * kScrollStep, 0.0f, std::max(0.0f, n.contentExtent - view));
                    mouseTaken = true;
                    break;
                }
            }
            if (s.hover >= 0 || s.pressed >= 0) mouseTaken = true;
        }
        wantsMouse = mouseTaken;

        // While typing in a text input, only that field receives the keyboard (ui.* move/confirm/back aren't used either)
        Screen* typing = nullptr;
        for (Screen* sp : order)
            if (sp->editing >= 0) {
                typing = sp;
                break;
            }
        if (typing) {
            if (in.typing) editText(*typing, in);
            wantsKeyboard = true;
        }
        // Keyboard/gamepad: the topmost screen with a focusable widget
        else if (in.navigation) {
            for (Screen* sp : order) {
                Screen& s = *sp;
                if (s.fresh) break; // if the top is a just-opened screen, don't pass to screens below this time either
                bool any = false;
                for (size_t i = 0; i < s.nodes.size() && !any && s.doc.navigation; ++i) any = focusable(s, static_cast<i32>(i));
                if (in.back && !s.doc.backAction.empty()) {
                    Action back;
                    back.name = s.doc.backAction;
                    back.screen = s.id;
                    actions.push_back(back);
                    break;
                }
                if (!any) continue;
                const bool dirs[4] = {in.up, in.down, in.left, in.right};
                for (int dir = 0; dir < 4; ++dir)
                    if (dirs[dir]) stepFocus(s, dir);
                if (in.confirm && s.focus >= 0 && focusable(s, s.focus)) {
                    focusVisible = true;
                    activate(s, s.focus);
                }
                wantsKeyboard = s.focus >= 0 && focusVisible;
                break;
            }
        }
        for (Screen& s : screens) s.fresh = false;

        // State blending (hover/pressed/focus/disabled/on)
        for (Screen& s : screens)
            for (size_t i = 0; i < s.nodes.size(); ++i) {
                RNode& n = s.nodes[i];
                const Node& d = s.doc.nodes[n.src];
                if (s.stateIndex[n.src] < 0) continue;
                bool target[static_cast<int>(State::Count)] = {
                    true, s.hover == static_cast<i32>(i), s.pressed == static_cast<i32>(i),
                    s.focus == static_cast<i32>(i) && (focusVisible || focusAlways), n.disabled, n.checked};
                if (s.previewNode == static_cast<i32>(n.src)) target[static_cast<int>(s.previewState)] = true;
                const f32 rate = d.transition > 0 ? static_cast<f32>(dt) / d.transition : 1.0f;
                for (int st = 1; st < static_cast<int>(State::Count); ++st)
                    n.mix[st] = std::clamp(n.mix[st] + (target[st] ? rate : -rate), 0.0f, 1.0f);
            }
    }

    void stepFocus(Screen& s, int dir) {
        focusVisible = true;
        if (s.focus < 0 || !focusable(s, s.focus)) {
            s.focus = s.doc.firstFocus.empty() ? -1 : findByName(s, s.doc.firstFocus);
            for (size_t i = 0; i < s.nodes.size() && s.focus < 0; ++i)
                if (focusable(s, static_cast<i32>(i))) s.focus = static_cast<i32>(i);
            return;
        }
        const WidgetKind w = s.doc.nodes[s.nodes[static_cast<size_t>(s.focus)].src].widget;
        if (dir >= 2 && (w == WidgetKind::Slider || w == WidgetKind::Select)) {
            activate(s, s.focus, dir == 2 ? -1 : 1);
            return;
        }
        const i32 next = neighbor(s, s.focus, dir);
        if (next >= 0) {
            s.focus = next;
            scrollIntoView(s, next);
        }
    }

    void editText(Screen& s, const FrameInput& in) {
        RNode& n = s.nodes[static_cast<size_t>(s.editing)];
        const Node& d = s.doc.nodes[n.src];
        Value v;
        if (!n.visible || n.disabled || d.widget != WidgetKind::TextInput || !resolve(s, n, d.field, v) ||
            v.kind != ValueKind::String) {
            s.editing = -1;
            return;
        }
        std::string& value = *static_cast<std::string*>(v.data);
        for (u32 i = 0; i < in.backspaces && !value.empty(); ++i) { // the last character (including UTF-8 continuation bytes)
            size_t end = value.size() - 1;
            while (end > 0 && (static_cast<u8>(value[end]) & 0xC0) == 0x80) --end;
            value.resize(end);
        }
        u32 count = utf8Length(value);
        static const std::string kNoText;
        const std::string& text = in.text ? *in.text : kNoText;
        for (size_t at = 0; at < text.size();) { // appends UTF-8 as is up to the max character count
            size_t len = 1;
            while (at + len < text.size() && (static_cast<u8>(text[at + len]) & 0xC0) == 0x80) ++len;
            if (count < d.maxLength) {
                value.append(text, at, len);
                ++count;
            }
            at += len;
        }
        if (in.enter) {
            emit(s, s.editing, d.action.empty() ? "submit" : d.action, d.field);
            s.editing = -1;
        } else if (in.escape) {
            s.editing = -1;
        }
    }

    void reloadChanged() {
        const u64 tt = host.fileTime(kThemeFile);
        const bool themeChanged = tt != themeTime;
        if (themeChanged) {
            themeTime = tt;
            std::vector<std::string> errors;
            loadTheme(theme, host, &errors);
            for (const std::string& e : errors) warn("UI: " + e);
        }
        for (Screen& s : screens) {
            if (themeChanged || host.fileTime(s.path) != s.fileTime) {
                loadScreen(s);
                host.log(LogLevel::Info, "UI: loaded " + s.path + " (" + std::to_string(s.doc.nodes.size()) + " nodes)");
            }
        }
        atlas.reloadChanged();
    }
};

System::System(Host& host, const Binder& binder) : m(new Impl(host, binder)) {}
System::~System() { delete m; }

void System::reload() {
    m->atlas.setHost(&m->host);
    m->themeLoaded = false;
    m->ensureTheme();
    for (Screen& s : m->screens) m->loadScreen(s); // screens opened before (strings/atlases/theme changed)
}

ScreenId System::openWorld(const std::string& uiPath) {
    const ScreenId id = open(uiPath, 0);
    m->find(id)->worldTemplate = true;
    return id;
}

ScreenId System::open(const std::string& uiPath, i32 layer) {
    Screen s;
    s.id = m->nextId++;
    s.layer = layer;
    s.path = uiPath;
    m->screens.push_back(std::move(s));
    m->loadScreen(m->screens.back());
    return m->screens.back().id;
}

void System::close(ScreenId screen) {
    m->screens.erase(std::remove_if(m->screens.begin(), m->screens.end(), [&](const Screen& s) { return s.id == screen; }),
                     m->screens.end());
}

bool System::isOpen(ScreenId screen) const { return m->find(screen) != nullptr; }
void System::closeAll() { m->screens.clear(); }

void System::bind(ScreenId screen, Model viewModel) {
    if (Screen* s = m->find(screen)) {
        if (s->vm.data != viewModel.data || s->vm.type != viewModel.type) s->rebuild = true;
        s->vm = viewModel;
        s->bindingErrors.clear();
    }
}

void System::setScale(f32 uiScale) { m->uiScale = std::clamp(uiScale, 0.25f, 4.0f); }
f32 System::scale() const { return m->uiScale; }

void System::update(const FrameInput& in, const Viewport& viewport, f64 dt) {
    m->viewX = viewport.x;
    m->viewY = viewport.y;
    m->viewW = std::max(1.0f, viewport.width);
    m->viewH = std::max(1.0f, viewport.height);
    for (Screen& s : m->screens) {
        s.originX = m->viewX;
        s.originY = m->viewY;
    }
    m->update(in, dt);
    m->world.clear();
}

const std::vector<Action>& System::actions() const { return m->actions; }
bool System::wantsMouse() const { return m->wantsMouse; }
bool System::wantsKeyboard() const { return m->wantsKeyboard; }

void System::setCamera(const f32 viewProj[16]) { std::memcpy(m->viewProj, viewProj, sizeof(m->viewProj)); }

void System::drawAt(ScreenId templateScreen, f32 x, f32 y, f32 z, Model viewModel, bool clampToEdges) {
    m->world.push_back({templateScreen, x, y, z, viewModel, clampToEdges});
}

const std::vector<Quad>& System::render() {
    m->quads.clear();
    std::vector<Screen*>& order = m->order;
    order.clear();
    for (Screen& s : m->screens) order.push_back(&s);
    std::stable_sort(order.begin(), order.end(), [](const Screen* a, const Screen* b) { return a->layer < b->layer; });
    const f32 noClip[4] = {-1e9f, -1e9f, 1e9f, 1e9f};
    // World-space UI first (below screen UI)
    for (const WorldDraw& w : m->world) {
        Screen* s = m->find(w.screen);
        if (!s || s->doc.nodes.empty()) continue;
        const f32(&vp)[4][4] = m->viewProj;
        const f32 cx = w.x * vp[0][0] + w.y * vp[1][0] + w.z * vp[2][0] + vp[3][0];
        const f32 cy = w.x * vp[0][1] + w.y * vp[1][1] + w.z * vp[2][1] + vp[3][1];
        const f32 cw = w.x * vp[0][3] + w.y * vp[1][3] + w.z * vp[2][3] + vp[3][3];
        if (cw <= 1e-4f && !w.clamp) continue; // behind the camera
        f32 nx = cx / std::max(cw, 1e-4f), ny = cy / std::max(cw, 1e-4f);
        if (cw <= 1e-4f) {
            nx = -nx;
            ny = -ny;
        }
        if (!w.clamp && (nx < -1.2f || nx > 1.2f || ny < -1.2f || ny > 1.2f)) continue;
        if (w.clamp) {
            nx = std::clamp(nx, -0.95f, 0.95f);
            ny = std::clamp(ny, -0.95f, 0.95f);
        }
        const f32 px = m->viewX + (nx * 0.5f + 0.5f) * m->viewW, py = m->viewY + (0.5f - ny * 0.5f) * m->viewH;
        const Model keep = s->vm;
        s->vm = w.vm;
        const f32 bw = std::max(1.0f, s->doc.boardW), bh = std::max(1.0f, s->doc.boardH);
        const f32 k = std::min(m->viewW / 1920.0f, m->viewH / 1080.0f) * m->uiScale; // scale relative to the reference screen
        if (s->rebuild || !m->rebind(*s)) m->rebuild(*s);
        s->scale = k;
        s->rootW = bw;
        s->rootH = bh;
        m->layout(*s, bw, bh);
        s->originX = px - bw * 0.5f * k;
        s->originY = py - bh * 0.5f * k;
        m->emitNode(*s, 0, 1.0f, noClip);
        s->vm = keep; // nodes keep pointing at this draw's view model until the next draw rebinds them
    }
    for (Screen* s : order) {
        if (s->worldTemplate || s->doc.nodes.empty()) continue;
        s->originX = m->viewX;
        s->originY = m->viewY;
        m->prepare(*s, m->viewW, m->viewH);
        if (s->nodes[0].visible) m->emitNode(*s, 0, 1.0f, noClip);
    }
    m->lastQuadCount = static_cast<u32>(m->quads.size());
    return m->quads;
}

void System::layout(const Viewport& viewport) {
    m->viewX = viewport.x;
    m->viewY = viewport.y;
    m->viewW = std::max(1.0f, viewport.width);
    m->viewH = std::max(1.0f, viewport.height);
    for (Screen& s : m->screens) {
        if (s.worldTemplate || s.doc.nodes.empty()) continue;
        s.originX = m->viewX;
        s.originY = m->viewY;
        m->prepare(s, m->viewW, m->viewH);
    }
}

std::vector<ScreenId> System::screens() const {
    std::vector<ScreenId> out;
    for (const Screen& s : m->screens) out.push_back(s.id);
    return out;
}

Model System::model(ScreenId screen) const {
    const Screen* s = m->find(screen);
    return s ? s->vm : Model{};
}

ScreenInfo System::info(ScreenId screen) const {
    ScreenInfo out;
    if (Screen* s = m->find(screen)) {
        out.path = s->path;
        out.nodes = static_cast<u32>(s->nodes.size());
        for (const RNode& n : s->nodes) out.visibleNodes += n.visible ? 1u : 0u;
        out.errors = s->doc.errors;
        out.errors.insert(out.errors.end(), s->bindingErrors.begin(), s->bindingErrors.end());
        out.exported = s->doc.exported;
        out.originX = s->originX;
        out.originY = s->originY;
        out.scale = s->scale;
        if (s->focus >= 0 && s->focus < static_cast<i32>(s->nodes.size())) out.focus = s->doc.nodes[s->nodes[static_cast<size_t>(s->focus)].src].name;
    }
    return out;
}

std::vector<NodeBox> System::boxes(ScreenId screen) const {
    std::vector<NodeBox> out;
    if (Screen* s = m->find(screen))
        for (const RNode& n : s->nodes) {
            const Node& d = s->doc.nodes[n.src];
            const bool text = d.kind == NodeKind::Text;
            const bool overflow = text && n.visible && (n.shaped.truncated || n.shaped.width > n.rect.w + 0.5f || n.shaped.height > n.rect.h + 0.5f);
            out.push_back({d.name, d.id, n.rect.x, n.rect.y, n.rect.w, n.rect.h, d.depth, n.visible, text, n.src, overflow});
        }
    return out;
}

void System::setDraft(ScreenId screen, const std::string* text) {
    Screen* s = m->find(screen);
    if (!s) return;
    s->hasDraft = text != nullptr;
    s->draft = text ? *text : std::string();
    m->loadScreen(*s);
}

void System::setPreviewState(ScreenId screen, i32 node, State state) {
    if (Screen* s = m->find(screen)) {
        s->previewNode = node;
        s->previewState = state;
    }
}

void System::moveFocus(ScreenId screen, int direction) {
    Screen* s = m->find(screen);
    if (!s) return;
    if (s->rebuild) m->prepare(*s, m->viewW, m->viewH);
    m->stepFocus(*s, direction);
}

void System::setFocusVisible(bool always) { m->focusAlways = always; }
u32 System::quadCount() const { return m->lastQuadCount; }

bool System::press(ScreenId screen, const std::string& nodeName) {
    Screen* s = m->find(screen);
    if (!s) return false;
    if (s->rebuild) m->prepare(*s, m->viewW, m->viewH);
    // "name#k": the k-th (from 0) widget of that name, for list items that share their template's name
    std::string_view name = nodeName;
    size_t skip = 0;
    if (const size_t hash = nodeName.rfind('#'); hash != std::string::npos && hash + 1 < nodeName.size()) {
        name = std::string_view(nodeName).substr(0, hash);
        skip = static_cast<size_t>(std::strtoul(nodeName.c_str() + hash + 1, nullptr, 10));
    }
    for (size_t i = 0; i < s->nodes.size(); ++i)
        if (s->doc.nodes[s->nodes[i].src].name == name && m->focusable(*s, static_cast<i32>(i)) && skip-- == 0) {
            m->activate(*s, static_cast<i32>(i));
            return true;
        }
    return false;
}

bool System::pressAny(const std::string& nodeName) {
    std::vector<Screen*> order;
    for (Screen& s : m->screens)
        if (!s.worldTemplate) order.push_back(&s);
    std::stable_sort(order.begin(), order.end(), [](const Screen* a, const Screen* b) { return a->layer > b->layer; });
    for (Screen* s : order)
        if (press(s->id, nodeName)) return true;
    return false;
}

bool System::focusText(ScreenId screen, const std::string& nodeName) {
    Screen* s = m->find(screen);
    if (!s) return false;
    if (s->rebuild) m->prepare(*s, m->viewW, m->viewH);
    for (size_t i = 0; i < s->nodes.size(); ++i) {
        const Node& d = s->doc.nodes[s->nodes[i].src];
        if (d.name != nodeName || d.widget != WidgetKind::TextInput || !m->focusable(*s, static_cast<i32>(i))) continue;
        s->focus = s->editing = static_cast<i32>(i);
        return true;
    }
    return false;
}

bool System::editingText() const {
    for (const Screen& s : m->screens)
        if (s.editing >= 0) return true;
    return false;
}

} // namespace mkui
