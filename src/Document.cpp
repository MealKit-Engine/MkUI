#include "mkui/Document.h"

#include "mkui/Host.h"

#include "mkcontract/Text.h"

#include <cstdlib>
#include <cstring>

namespace mkui {
namespace {

constexpr u32 kFormatVersion = 1; // "@format ui 1" (only version 1 for now, so no migrate)

// Format line: true if the line is one (warns about other kinds/newer versions/wrong shapes through the host)
bool formatLine(const std::string& line, const std::string& path, Host& host) {
    mkcontract::FormatLine f;
    switch (mkcontract::parseFormatLine(line, f)) {
    case mkcontract::FormatParse::NotFormat: return false;
    case mkcontract::FormatParse::Malformed:
        host.log(LogLevel::Warn, path + ": expected \"@format <kind> <version>\", got \"" + line + "\" (ignored)");
        return true;
    case mkcontract::FormatParse::Ok: break;
    }
    if (f.kind != "ui")
        host.log(LogLevel::Warn, path + ": this is a '" + f.kind + "' file, expected 'ui'");
    else if (f.version > kFormatVersion)
        host.log(LogLevel::Warn, path + ": written in ui format " + std::to_string(f.version) + ", this engine reads up to " +
                                     std::to_string(kFormatVersion) + " - unknown keys are ignored (update the engine)");
    return true;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// The rest after the first word (text as is)
std::string rest(const std::string& line, size_t words) {
    size_t i = 0;
    for (size_t w = 0; w < words; ++w) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    }
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return line.substr(i);
}

bool parseNumber(const std::string& s, const Theme* theme, f32& out) {
    if (!s.empty() && s[0] == '$') return theme && theme->number(s.substr(1), out);
    char* end = nullptr;
    const f32 v = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end) return false;
    out = v;
    return true;
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseColor(const std::string& s, ColorValue& out) {
    out = {};
    if (s == "none") return true;
    if (!s.empty() && s[0] == '$') {
        out.token = s.substr(1);
        return !out.token.empty();
    }
    if (s.size() != 7 && s.size() != 9) return false;
    if (s[0] != '#') return false;
    u8 c[4] = {0, 0, 0, 255};
    for (size_t i = 0; i < (s.size() - 1) / 2; ++i) {
        const int hi = hex(s[1 + i * 2]), lo = hex(s[2 + i * 2]);
        if (hi < 0 || lo < 0) return false;
        c[i] = static_cast<u8>(hi * 16 + lo);
    }
    out.rgba = rgba(c[0], c[1], c[2], c[3]);
    return true;
}

// 1, 2, or 4 values (CSS order: top right bottom left)
bool parseBox(const std::vector<std::string>& t, const Theme* theme, f32 out[4]) {
    f32 v[4] = {};
    const size_t n = t.size() - 1;
    if (n != 1 && n != 2 && n != 4) return false;
    for (size_t i = 0; i < n; ++i)
        if (!parseNumber(t[i + 1], theme, v[i])) return false;
    if (n == 1) v[1] = v[2] = v[3] = v[0];
    if (n == 2) {
        v[2] = v[0];
        v[3] = v[1];
    }
    for (int i = 0; i < 4; ++i) out[i] = v[i];
    return true;
}

bool parseSize(const std::string& s, const Theme* theme, SizeMode& mode, f32& value) {
    if (s == "hug") mode = SizeMode::Hug;
    else if (s == "fill") mode = SizeMode::Fill;
    else {
        mode = SizeMode::Fixed;
        return parseNumber(s, theme, value);
    }
    return true;
}

bool parseJustify(const std::string& s, Justify& out) {
    static const char* names[] = {"start", "center", "end", "space-between", "space-around", "space-evenly"};
    for (int i = 0; i < 6; ++i)
        if (s == names[i]) {
            out = static_cast<Justify>(i);
            return true;
        }
    return false;
}

bool parseAlign(const std::string& s, Align& out) {
    static const char* names[] = {"auto", "start", "center", "end", "stretch"};
    for (int i = 0; i < 5; ++i)
        if (s == names[i]) {
            out = static_cast<Align>(i);
            return true;
        }
    return false;
}

bool parseConstraint(const std::string& s, Constraint& out) {
    if (s == "left" || s == "top") out = Constraint::Start;
    else if (s == "right" || s == "bottom") out = Constraint::End;
    else if (s == "leftright" || s == "topbottom") out = Constraint::Both;
    else if (s == "center") out = Constraint::Center;
    else if (s == "scale") out = Constraint::Scale;
    else return false;
    return true;
}

bool parsePaint(const std::vector<std::string>& t, const Theme* theme, Paint& out) {
    out = {};
    if (t.size() == 2) {
        if (!parseColor(t[1], out.c0)) return false;
        out.kind = t[1] == "none" ? PaintKind::None : PaintKind::Solid;
        return true;
    }
    if (t.size() == 8 && (t[1] == "linear" || t[1] == "radial")) {
        out.kind = t[1] == "linear" ? PaintKind::Linear : PaintKind::Radial;
        for (int i = 0; i < 4; ++i)
            if (!parseNumber(t[2 + static_cast<size_t>(i)], theme, out.p[i])) return false;
        return parseColor(t[6], out.c0) && parseColor(t[7], out.c1);
    }
    return false;
}

bool parseState(const std::string& s, State& out) {
    static const char* names[] = {"normal", "hover", "pressed", "focus", "disabled", "checked"};
    for (int i = 0; i < 6; ++i)
        if (s == names[i]) {
            out = static_cast<State>(i);
            return true;
        }
    return false;
}

} // namespace

u32 Theme::color(const ColorValue& c, u32 fallback) const {
    if (c.token.empty()) return c.rgba;
    for (const auto& [name, value] : colors)
        if (name == c.token) return value;
    return fallback;
}

bool Theme::number(const std::string& name, f32& out) const {
    for (const auto& [n, v] : numbers)
        if (n == name) {
            out = v;
            return true;
        }
    return false;
}

bool loadTheme(Theme& out, Host& host, std::vector<std::string>* errors) {
    out = {};
    const std::string path = std::string("assets/") + kThemeFile;
    std::string text;
    if (!host.readFile(kThemeFile, text)) return false;
    u32 lineNo = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        if (formatLine(line, path, host)) continue;
        const std::vector<std::string> t = split(line);
        bool ok = t.size() == 3;
        if (ok && t[0] == "color") {
            ColorValue c;
            ok = parseColor(t[2], c) && c.token.empty();
            if (ok) out.colors.emplace_back(t[1], c.rgba);
        } else if (ok && t[0] == "number") {
            f32 v = 0;
            ok = parseNumber(t[2], nullptr, v);
            if (ok) out.numbers.emplace_back(t[1], v);
        } else {
            ok = false;
        }
        if (!ok && errors) errors->push_back(std::string(kThemeFile) + "(" + std::to_string(lineNo) + "): expected \"color <name> #RRGGBB\" or \"number <name> <value>\"");
    }
    return true;
}

bool applyProperty(Node& n, const std::string& line, const Theme* theme, std::string* error) {
    const std::vector<std::string> t = split(line);
    if (t.empty()) return true;
    const std::string& k = t[0];
    const size_t args = t.size() - 1;
    auto num = [&](size_t i, f32& out) { return i < t.size() && parseNumber(t[i], theme, out); };
    auto fail = [&](const char* what) {
        if (error) *error = std::string("bad '") + k + "': " + what;
        return false;
    };
    Visual& v = n.visual;

    if (k == "pos") return (num(1, n.x) && num(2, n.y)) || fail("pos <x> <y>");
    if (k == "size") {
        if (args != 2) return fail("size <w|hug|fill> <h|hug|fill>");
        return (parseSize(t[1], theme, n.wMode, n.w) && parseSize(t[2], theme, n.hMode, n.h)) || fail("number, hug or fill");
    }
    if (k == "min" || k == "max") {
        if (args != 2) return fail("min|max <w> <h> (- for none)");
        f32* w = k == "min" ? &n.minW : &n.maxW;
        f32* h = k == "min" ? &n.minH : &n.maxH;
        if (t[1] != "-" && !num(1, *w)) return fail("number");
        if (t[2] != "-" && !num(2, *h)) return fail("number");
        return true;
    }
    if (k == "margin") return parseBox(t, theme, n.margin) || fail("1, 2 or 4 numbers");
    if (k == "padding") return parseBox(t, theme, n.padding) || fail("1, 2 or 4 numbers");
    if (k == "absolute") return n.absolute = true;
    if (k == "constraints")
        return (args == 2 && parseConstraint(t[1], n.ch) && parseConstraint(t[2], n.cv)) ||
               fail("constraints <left|right|leftright|center|scale> <top|bottom|topbottom|center|scale>");
    if (k == "rotation") return num(1, n.rotation) || fail("degrees");
    if (k == "opacity") return num(1, v.opacity) || fail("0 to 1");
    if (k == "brightness") return num(1, v.brightness) || fail("-1 to 1");
    if (k == "hidden") return n.hidden = true;
    if (k == "clip") return n.clip = true;
    if (k == "layout" && args >= 1 && t[1] == "grid") {
        n.layout = LayoutKind::Grid;
        if (args == 2 && (t[2] == "row" || t[2] == "column")) n.gridColumnFlow = t[2] == "column";
        else if (args != 1) return fail("layout grid [row|column]");
        return true;
    }
    if (k == "rows" || k == "columns") {
        std::vector<Track>& out = k == "rows" ? n.rows : n.columns;
        out.clear();
        for (size_t i = 1; i < t.size(); ++i) {
            Track tr;
            std::string word = t[i];
            if (word == "auto") tr.kind = TrackKind::Auto;
            else {
                if (word.size() > 2 && word.compare(word.size() - 2, 2, "fr") == 0) tr.kind = TrackKind::Flex, word.resize(word.size() - 2);
                else if (word.size() > 1 && word.back() == '%') tr.kind = TrackKind::Percent, word.pop_back();
                else tr.kind = TrackKind::Fixed;
                if (!parseNumber(word, theme, tr.value) || tr.value < 0.0f) return fail("tracks: <number> | <n>fr | <n>% | auto");
            }
            out.push_back(tr);
        }
        return !out.empty() || fail("at least one track");
    }
    if (k == "cell") {
        f32 cell[4] = {0, 0, 1, 1};
        if (args != 2 && args != 4) return fail("cell <row> <column> [<rowSpan> <columnSpan>]");
        for (size_t i = 0; i < args; ++i)
            if (!num(i + 1, cell[i]) || cell[i] < 1.0f) return fail("numbers from 1");
        n.cellRow = static_cast<u16>(cell[0]);
        n.cellColumn = static_cast<u16>(cell[1]);
        n.rowSpan = static_cast<u16>(cell[2]);
        n.columnSpan = static_cast<u16>(cell[3]);
        return true;
    }
    if (k == "justifyItems") return (args == 1 && parseAlign(t[1], n.justifyItems)) || fail("start|center|end|stretch");
    if (k == "justifySelf") return (args == 1 && parseAlign(t[1], n.justifySelf)) || fail("auto|start|center|end|stretch");
    if (k == "id") return args == 1 ? (n.id = t[1], true) : fail("id <penpot shape id>");
    if (k == "layout") {
        if (args < 1 || (t[1] != "row" && t[1] != "column")) return fail("layout row|column [reverse] [wrap] or layout grid [row|column]");
        n.layout = LayoutKind::Flex;
        n.column = t[1] == "column";
        for (size_t i = 2; i < t.size(); ++i) {
            if (t[i] == "reverse") n.reverse = true;
            else if (t[i] == "wrap") n.wrap = true;
            else return fail("reverse or wrap");
        }
        return true;
    }
    if (k == "gap") {
        if (!num(1, n.rowGap)) return fail("gap <row> [column]");
        n.columnGap = n.rowGap;
        return args < 2 || num(2, n.columnGap) || fail("number");
    }
    if (k == "justify") {
        n.justifyStretch = args == 1 && t[1] == "stretch"; // Grid: auto tracks share the remaining space
        return n.justifyStretch || (args == 1 && parseJustify(t[1], n.justify)) ||
               fail("start|center|end|space-between|space-around|space-evenly (grid: also stretch)");
    }
    if (k == "align") return (args == 1 && parseAlign(t[1], n.alignItems)) || fail("start|center|end|stretch");
    if (k == "alignSelf") return (args == 1 && parseAlign(t[1], n.alignSelf)) || fail("auto|start|center|end|stretch");
    if (k == "alignContent") {
        if (args == 1 && parseJustify(t[1], n.alignContentJustify)) return true;
        return (args == 1 && parseAlign(t[1], n.alignContent)) || fail("start|center|end|stretch|space-*");
    }
    if (k == "fill") return parsePaint(t, theme, v.fill) || fail("fill <#color|$token|none> or fill linear|radial a b c d <color> <color>");
    if (k == "stroke") return (args == 2 && num(1, v.strokeWidth) && parseColor(t[2], v.strokeColor)) || fail("stroke <width> <color>");
    if (k == "radius") {
        if (args == 1 && num(1, v.radii[0])) {
            v.radii[1] = v.radii[2] = v.radii[3] = v.radii[0];
            return true;
        }
        return (args == 4 && num(1, v.radii[0]) && num(2, v.radii[1]) && num(3, v.radii[2]) && num(4, v.radii[3])) ||
               fail("radius <r> or radius <tl> <tr> <br> <bl>");
    }
    if (k == "shadow") {
        Shadow s;
        if (args == 1 && t[1] == "none") {
            v.shadows.clear();
            return true;
        }
        if (args != 5 || !num(1, s.dx) || !num(2, s.dy) || !num(3, s.blur) || !num(4, s.spread) || !parseColor(t[5], s.color))
            return fail("shadow <dx> <dy> <blur> <spread> <color>");
        v.shadows.push_back(s);
        return true;
    }
    if (k == "src") {
        if (args != 1) return fail("src <assets path> | src {field}");
        const std::string& a = t[1];
        if (a.size() > 2 && a.front() == '{' && a.back() == '}') {
            n.imageField = a.substr(1, a.size() - 2);
            n.image.clear();
        } else {
            n.image = a;
            n.imageField.clear();
        }
        return true;
    }
    if (k == "slice") return (args == 4 && num(1, n.slice[0]) && num(2, n.slice[1]) && num(3, n.slice[2]) && num(4, n.slice[3])) || fail("slice <left> <top> <right> <bottom>");
    if (k == "tint") return (args == 1 && parseColor(t[1], v.tint) && (v.hasTint = true)) || fail("color");
    // Text
    if (k == "key") return args == 1 ? (n.textKey = t[1], true) : fail("key <strings key>");
    if (k == "content") return n.textContent = rest(line, 1), true;
    if (k == "font") return args == 1 ? (n.text.font = t[1], true) : fail("font <file name in assets/fonts>");
    if (k == "fontSize") return num(1, n.text.size) || fail("number");
    if (k == "color") return (args == 1 && parseColor(t[1], v.textColor) && (v.hasTextColor = true)) || fail("color");
    if (k == "letterSpacing") return num(1, n.text.letterSpacing) || fail("number");
    if (k == "lineHeight") return num(1, n.lineHeight) || fail("multiple of the font size");
    if (k == "underline") return n.text.underline = true;
    if (k == "strike") return n.text.strike = true;
    if (k == "transform") {
        if (args == 1 && t[1] == "upper") n.text.transform = TextTransform::Upper;
        else if (args == 1 && t[1] == "lower") n.text.transform = TextTransform::Lower;
        else if (args == 1 && t[1] == "none") n.text.transform = TextTransform::None;
        else return fail("upper|lower|none");
        return true;
    }
    if (k == "textAlign") {
        if (args == 1 && t[1] == "left") n.textAlign = TextAlign::Left;
        else if (args == 1 && t[1] == "center") n.textAlign = TextAlign::Center;
        else if (args == 1 && t[1] == "right") n.textAlign = TextAlign::Right;
        else return fail("left|center|right");
        return true;
    }
    if (k == "verticalAlign") {
        if (args == 1 && t[1] == "top") n.verticalAlign = VerticalAlign::Top;
        else if (args == 1 && t[1] == "center") n.verticalAlign = VerticalAlign::Center;
        else if (args == 1 && t[1] == "bottom") n.verticalAlign = VerticalAlign::Bottom;
        else return fail("top|center|bottom");
        return true;
    }
    if (k == "wrap") {
        if (args == 1 && t[1] == "word") n.wrapMode = WrapMode::Word;
        else if (args == 1 && t[1] == "syllable") n.wrapMode = WrapMode::Syllable;
        else if (args == 1 && t[1] == "none") n.wrapMode = WrapMode::None;
        else return fail("word|syllable|none");
        return true;
    }
    if (k == "overflow") {
        if (args == 1 && (t[1] == "ellipsis" || t[1] == "clip")) return n.ellipsis = t[1] == "ellipsis", true;
        return fail("ellipsis|clip");
    }
    if (k == "maxLines") {
        f32 lines = 0;
        if (!num(1, lines) || lines < 0) return fail("number");
        n.maxLines = static_cast<u32>(lines);
        return true;
    }
    if (k == "span") {
        f32 tag = 0;
        if (!num(1, tag) || tag < 1) return fail("span <tag number> <property> <value> ...");
        SpanStyle s;
        s.tag = static_cast<u32>(tag);
        for (size_t i = 2; i < t.size(); ++i) {
            const std::string& p = t[i];
            if (p == "underline") s.underline = 1;
            else if (p == "strike") s.strike = 1;
            else if (p == "font" && i + 1 < t.size()) s.font = t[++i];
            else if ((p == "size" || p == "fontSize") && num(i + 1, s.size)) ++i;
            else if (p == "color" && i + 1 < t.size() && parseColor(t[i + 1], s.color)) s.hasColor = true, ++i;
            else if (p == "letterSpacing" && num(i + 1, s.letterSpacing)) ++i;
            else return fail("span properties: font, size, color, letterSpacing, underline, strike");
        }
        n.spans.push_back(s);
        return true;
    }
    // Bindings
    if (k == "visible") return args == 1 ? (n.visibleField = t[1], true) : fail("visible <field>");
    if (k == "disabled") return args == 1 ? (n.disabledField = t[1], true) : fail("disabled <field>");
    if (k == "widthRatio") return args == 1 ? (n.widthRatioField = t[1], true) : fail("widthRatio <field>");
    if (k == "colorField") return args == 1 ? (n.colorField = t[1], true) : fail("colorField <field>");
    if (k == "scaleField") return args == 1 ? (n.scaleField = t[1], true) : fail("scaleField <field>");
    if (k == "offsetField") return args == 2 ? (n.offsetXField = t[1], n.offsetYField = t[2], true) : fail("offsetField <x field> <y field>");
    if (k == "opacityField") return args == 1 ? (n.opacityField = t[1], true) : fail("opacityField <field>");
    if (k == "fontSizeField") return args == 1 ? (n.fontSizeField = t[1], true) : fail("fontSizeField <field>");
    if (k == "brightnessField") return args == 1 ? (n.brightnessField = t[1], true) : fail("brightnessField <field>");
    if (k == "flip") {
        bool ok = args >= 1;
        for (size_t i = 1; i < t.size(); ++i) {
            if (t[i] == "horizontal") n.flipX = true;
            else if (t[i] == "vertical") n.flipY = true;
            else ok = false;
        }
        return ok || fail("flip horizontal|vertical");
    }
    // Widgets
    if (k == "button") {
        if (args != 1) return fail("button <action>");
        n.widget = WidgetKind::Button;
        n.action = t[1];
        return true;
    }
    if (k == "toggle") {
        if (args < 1 || args > 2) return fail("toggle <field> [action]");
        n.widget = WidgetKind::Toggle;
        n.field = t[1];
        if (args == 2) n.action = t[2];
        return true;
    }
    if (k == "slider") {
        if ((args != 4 && args != 5) || !num(2, n.sliderMin) || !num(3, n.sliderMax) || !num(4, n.sliderStep))
            return fail("slider <field> <min> <max> <step> [action]");
        n.widget = WidgetKind::Slider;
        n.field = t[1];
        if (args == 5) n.action = t[5];
        return true;
    }
    if (k == "select") {
        if (args < 2 || args > 3) return fail("select <field> <key1,key2,...> [action]");
        n.widget = WidgetKind::Select;
        n.field = t[1];
        n.options.clear();
        size_t a = 0;
        while (a <= t[2].size()) {
            size_t b = t[2].find(',', a);
            if (b == std::string::npos) b = t[2].size();
            if (b > a) n.options.push_back(t[2].substr(a, b - a));
            a = b + 1;
        }
        if (args == 3) n.action = t[3];
        return true;
    }
    if (k == "textInput") { // text node: edits a view model string field. Action on Enter ("submit" if empty)
        if (args < 1 || args > 2) return fail("textInput <field> [action]");
        if (n.kind != NodeKind::Text) return fail("textInput is for text nodes");
        n.widget = WidgetKind::TextInput;
        n.field = t[1];
        if (args == 2) n.action = t[2];
        return true;
    }
    if (k == "placeholder") return args == 1 ? (n.placeholderKey = t[1], true) : fail("placeholder <string key>");
    if (k == "maxLength") {
        f32 length = 0;
        if (args != 1 || !num(1, length) || length < 1) return fail("maxLength <characters>");
        n.maxLength = static_cast<u32>(length);
        return true;
    }
    if (k == "scroll") {
        if (n.widget == WidgetKind::List) return fail("scroll and list on one frame: put the list in a child frame of the scroll area");
        n.widget = WidgetKind::Scroll;
        n.clip = true;
        n.scrollHorizontal = args == 1 && t[1] == "horizontal";
        return true;
    }
    if (k == "list") {
        if (args != 1) return fail("list <field>");
        if (n.widget == WidgetKind::Scroll) return fail("scroll and list on one frame: put the list in a child frame of the scroll area");
        n.widget = WidgetKind::List;
        n.field = t[1];
        return true;
    }
    if (k == "focusUp" || k == "focusDown" || k == "focusLeft" || k == "focusRight") {
        if (args != 1) return fail("<node name>");
        const int dir = k == "focusUp" ? 0 : k == "focusDown" ? 1 : k == "focusLeft" ? 2 : 3;
        n.focusNext[dir] = t[1];
        return true;
    }
    if (k == "transition") return num(1, n.transition) || fail("seconds");
    if (k == "on") {
        StateOverride o;
        if (args < 2 || !parseState(t[1], o.state)) return fail("on <hover|pressed|focus|disabled|checked> <property> ...");
        const std::string inner = rest(line, 2);
        Node probe; // checks in advance whether this line is valid
        std::string innerError;
        if (!applyProperty(probe, inner, theme, &innerError)) return fail(innerError.c_str());
        for (StateOverride& existing : n.states)
            if (existing.state == o.state) {
                existing.lines.push_back(inner);
                return true;
            }
        o.lines.push_back(inner);
        n.states.push_back(std::move(o));
        return true;
    }
    if (error) *error = "unknown property '" + k + "'";
    return false;
}

namespace {

bool loadNested(const std::string& assetPath, Document& out, Host& host, const Theme* theme, u32 depth);

struct Parser {
    Document& doc;
    Host& host;
    const Theme* theme;
    std::string file;
    std::vector<i32> stack;
    u32 includeDepth = 0;

    void error(u32 line, const std::string& message) {
        doc.errors.push_back(file + "(" + std::to_string(line) + "): " + message);
    }

    i32 open(NodeKind kind, const std::string& name, u32 line) {
        Node n;
        n.kind = kind;
        n.name = name;
        n.parent = stack.empty() ? -1 : stack.back();
        n.depth = static_cast<u32>(stack.size());
        if (kind == NodeKind::Text) {
            n.wMode = n.hMode = SizeMode::Hug;
        }
        if (stack.empty() && !doc.nodes.empty()) error(line, "only one board per file");
        doc.nodes.push_back(n);
        return static_cast<i32>(doc.nodes.size() - 1);
    }

    bool parse(const std::string& text) {
        u32 lineNo = 0;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            const std::string line = trim(text.substr(pos, end - pos));
            pos = end + 1;
            ++lineNo;
            if (lineNo == 1 && line.rfind("# MealKit UI exported from Penpot", 0) == 0) doc.exported = true;
            if (line.empty() || line[0] == '#') continue;
            if (formatLine(line, "assets/" + doc.path, host)) continue;
            const std::vector<std::string> t = split(line);
            const std::string& k = t[0];
            if (k == "board") {
                f32 w = 0, h = 0;
                if (!stack.empty() || t.size() != 4 || !parseNumber(t[2], theme, w) || !parseNumber(t[3], theme, h)) {
                    error(lineNo, "expected \"board <name> <width> <height>\" at the top");
                    continue;
                }
                const i32 i = open(NodeKind::Frame, t[1], lineNo);
                doc.nodes[static_cast<size_t>(i)].w = w;
                doc.nodes[static_cast<size_t>(i)].h = h;
                doc.boardW = w;
                doc.boardH = h;
                stack.push_back(i);
            } else if (k == "frame" || k == "text" || k == "image") {
                if (stack.empty() || t.size() != 2) {
                    error(lineNo, "expected \"" + k + " <name>\" inside the board");
                    continue;
                }
                stack.push_back(open(k == "frame" ? NodeKind::Frame : k == "text" ? NodeKind::Text : NodeKind::Image, t[1], lineNo));
            } else if (k == "use") {
                if (stack.empty() || t.size() != 3) {
                    error(lineNo, "expected \"use <name> <component.ui>\" inside the board");
                    continue;
                }
                stack.push_back(include(t[1], t[2], lineNo));
            } else if (k == "end") {
                if (stack.empty()) error(lineNo, "end without a node");
                else stack.pop_back();
            } else if (k == "back" && stack.size() == 1) {
                doc.backAction = t.size() > 1 ? t[1] : "";
            } else if (k == "navigation" && stack.size() == 1) {
                doc.navigation = true;
            } else if (k == "firstFocus" && stack.size() == 1) {
                doc.firstFocus = t.size() > 1 ? t[1] : "";
            } else if (k == "set") {
                // Overriding descendant properties inside use: set <descendant name> <property> <value...>
                if (stack.empty() || t.size() < 3) {
                    error(lineNo, "expected \"set <child name> <property> <value>\"");
                    continue;
                }
                const size_t from = static_cast<size_t>(stack.back());
                bool found = false;
                for (size_t i = from + 1; i < doc.nodes.size() && doc.nodes[i].depth > doc.nodes[from].depth; ++i)
                    if (doc.nodes[i].name == t[1]) {
                        std::string message;
                        if (!applyProperty(doc.nodes[i], rest(line, 2), theme, &message)) error(lineNo, message);
                        found = true;
                        break;
                    }
                if (!found) error(lineNo, "no child named '" + t[1] + "'");
            } else {
                if (stack.empty()) {
                    error(lineNo, "property outside a node");
                    continue;
                }
                std::string message;
                if (!applyProperty(doc.nodes[static_cast<size_t>(stack.back())], line, theme, &message)) error(lineNo, message);
            }
        }
        if (!stack.empty()) error(lineNo, "missing end (" + std::to_string(stack.size()) + " node(s) open)");
        return doc.errors.empty();
    }

    // Component: puts that file's board here as a frame
    i32 include(const std::string& name, const std::string& path, u32 line) {
        Document sub;
        if (includeDepth >= 8 || !loadNested(path, sub, host, theme, includeDepth + 1)) { // stops at mutually referencing components
            error(line, "cannot use component " + path);
            for (const std::string& e : sub.errors) doc.errors.push_back(e); // why it couldn't be used (not found, syntax error)
            return open(NodeKind::Frame, name, line);
        }
        for (const std::string& e : sub.errors) doc.errors.push_back(e);
        const i32 base = static_cast<i32>(doc.nodes.size());
        const u32 depth = static_cast<u32>(stack.size());
        const i32 parent = stack.back();
        for (size_t i = 0; i < sub.nodes.size(); ++i) {
            Node n = sub.nodes[i];
            n.parent = i == 0 ? parent : n.parent + base;
            n.depth += depth;
            if (i == 0) n.name = name;
            doc.nodes.push_back(std::move(n));
        }
        return base;
    }
};

void computeEnds(Document& doc) {
    std::vector<size_t> open;
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
        while (!open.empty() && doc.nodes[open.back()].depth >= doc.nodes[i].depth) {
            doc.nodes[open.back()].end = static_cast<u32>(i);
            open.pop_back();
        }
        open.push_back(i);
    }
    for (size_t i : open) doc.nodes[i].end = static_cast<u32>(doc.nodes.size());
}

bool parseNested(const std::string& text, const std::string& assetPath, Document& out, Host& host, const Theme* theme, u32 depth) {
    out = {};
    out.path = assetPath;
    Parser p{out, host, theme, assetPath, {}, depth};
    const bool ok = p.parse(text);
    if (out.nodes.empty()) out.errors.push_back(assetPath + ": no board");
    computeEnds(out);
    return ok && !out.nodes.empty();
}

bool loadNested(const std::string& assetPath, Document& out, Host& host, const Theme* theme, u32 depth) {
    std::string text;
    if (!host.readFile(assetPath, text)) {
        out = {};
        out.path = assetPath;
        out.errors.push_back("assets/" + assetPath + " not found");
        return false;
    }
    return parseNested(text, assetPath, out, host, theme, depth);
}

} // namespace

bool parseDocument(const std::string& text, const std::string& assetPath, Document& out, Host& host, const Theme* theme) {
    return parseNested(text, assetPath, out, host, theme, 0);
}

bool loadDocument(const std::string& assetPath, Document& out, Host& host, const Theme* theme) {
    return loadNested(assetPath, out, host, theme, 0);
}

} // namespace mkui
