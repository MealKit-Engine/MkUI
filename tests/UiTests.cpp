// mkui tests without an engine: an in-memory host, no GPU (atlas uploads are dropped).
#include "mkui/Document.h"
#include "mkui/Draw.h"
#include "mkui/Host.h"
#include "mkui/Source.h"
#include "mkui/Ui.h"
#include "mkui/Values.h"

#include <algorithm>
#include <cmath>

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace {

int failures = 0, checks = 0;

void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}

class MemoryHost : public mkui::Host {
public:
    std::map<std::string, std::string> files;
    int warnings = 0;

    bool readFile(const std::string& path, std::string& out) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
    mkui::u64 fileTime(const std::string& path) override { return files.count(path) ? 1 : 0; }
    std::vector<std::string> listFiles(const std::string&, const char*) override { return {}; }
    void log(mkui::LogLevel level, const std::string& message) override {
        if (level == mkui::LogLevel::Warn) ++warnings;
        std::printf("  [%s] %s\n", level == mkui::LogLevel::Warn ? "warn" : "info", message.c_str());
    }
    const std::string& text(const std::string& key) override { return key; }
    bool decodeImage(const std::string&, mkui::u32&, mkui::u32&, std::vector<mkui::u8>&) override { return false; }
    void uploadAtlas(mkui::AtlasKind, mkui::u32, mkui::u32, mkui::u32, mkui::u32, const mkui::u8*) override {}
};

class NoBinder : public mkui::Binder {
public:
    bool resolve(const mkui::Model&, const std::string&, mkui::Value&) const override { return false; }
    const char* typeName(const mkui::Model&) const override { return "none"; }
    void format(const mkui::Value&, std::string& out) const override { out.clear(); }
};

const char* kScreen = "# test screen\n"
                      "@format ui 1\n"
                      "board test 1920 1080\n"
                      "  frame panel\n"
                      "    pos 100 100\n"
                      "    size 400 200\n"
                      "    fill #10141CCC\n"
                      "  end\n"
                      "  frame go\n"
                      "    pos 100 400\n"
                      "    size 300 80\n"
                      "    button test.go\n"
                      "    fill #2A3242E6\n"
                      "  end\n"
                      "end\n";

void testDocument() {
    MemoryHost host;
    mkui::Document doc;
    check(mkui::parseDocument(kScreen, "ui/test.ui", doc, host), "parseDocument reads the screen");
    check(doc.nodes.size() == 3, "board + two frames");
    mkui::Document outlined;
    const char* text = "@format ui 1\nboard t 100 100\n  text a\n    content hi\n    outline 2 #272727\n  end\n"
                       "  text b\n    content hi\n    outline 1.5\n    outlineField od\n    outlineColorField oc\n  end\nend\n";
    check(mkui::parseDocument(text, "ui/outline.ui", outlined, host) && outlined.nodes.size() == 3, "outline parses");
    if (outlined.nodes.size() == 3) {
        check(outlined.nodes[1].outline == 2.0f && outlined.nodes[1].hasOutlineColor, "outline width and colour");
        check(outlined.nodes[2].outline == 1.5f && !outlined.nodes[2].hasOutlineColor && outlined.nodes[2].outlineField == "od" &&
                  outlined.nodes[2].outlineColorField == "oc", "outline without colour, and its fields");
    }
}

void testSourceRoundTrip() {
    mkui::Source source;
    std::string error;
    check(mkui::parseSource(kScreen, source, &error), "parseSource reads the screen");
    check(mkui::writeSource(source) == kScreen, "writeSource gives the same text back");
    mkui::SourceNode* panel = mkui::findNode(source, {0});
    check(panel != nullptr, "first child found");
    if (panel) {
        mkui::setProperty(*panel, "size", "500 200");
        std::string value;
        check(mkui::getProperty(*panel, "size", value) && value == "500 200", "setProperty replaces the line");
    }
}

void testSystem() {
    MemoryHost host;
    host.files["ui/test.ui"] = kScreen;
    NoBinder binder;
    mkui::System ui(host, binder);
    mkui::ScreenId screen = ui.open("ui/test.ui");
    check(ui.isOpen(screen), "screen opens");
    mkui::Viewport viewport{0, 0, 960, 540};
    mkui::FrameInput input;
    ui.update(input, viewport, 1.0 / 60.0);
    const std::vector<mkui::Quad>& quads = ui.render();
    check(quads.size() >= 2, "two frames draw quads");
    bool scaled = false;
    for (const mkui::Quad& q : quads)
        if (q.rect[0] == 50.0f && q.rect[2] == 200.0f) scaled = true;
    check(scaled, "board scales to the viewport (half size)");
    check(ui.info(screen).errors.empty(), "no screen errors");

    check(ui.press(screen, "go"), "press finds the button"); // the action is there until the next update
    bool fired = false;
    for (const mkui::Action& a : ui.actions())
        if (a.name == "test.go") fired = true;
    check(fired, "pressed button fires its action");

    // A steady frame allocates nothing; here only check that it keeps the same output
    size_t count = ui.render().size();
    ui.update(input, viewport, 1.0 / 60.0);
    check(ui.render().size() == count, "steady frame draws the same quads");
}

mkui::Quad shapeQuad(float x, float y, float w, float h, float radius) {
    mkui::Quad q;
    q.rect[0] = x, q.rect[1] = y, q.rect[2] = w, q.rect[3] = h;
    for (float& r : q.radii) r = radius;
    q.color0 = 0xFF0000FFu;
    q.flags = mkui::kQuadShape;
    return q;
}

void bounds(const mkui::DrawList& list, float b[4]) {
    b[0] = b[1] = 1e9f, b[2] = b[3] = -1e9f;
    for (const mkui::DrawVertex& v : list.vertices) {
        b[0] = std::min(b[0], v.x), b[1] = std::min(b[1], v.y);
        b[2] = std::max(b[2], v.x), b[3] = std::max(b[3], v.y);
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }

void testTessellate() {
    mkui::DrawList list;
    std::vector<mkui::Quad> quads{shapeQuad(10, 20, 100, 50, 8)};
    mkui::tessellate(quads, list);
    check(list.commands.size() == 1 && list.commands[0].texture == mkui::DrawTexture::None, "one untextured command");
    bool inRange = !list.indices.empty() && list.indices.size() % 3 == 0;
    for (mkui::u32 i : list.indices) inRange = inRange && i < list.vertices.size();
    check(inRange, "indices are triangles inside the vertex list");
    float b[4];
    bounds(list, b);
    check(near(b[0], 9.5f) && near(b[1], 19.5f) && near(b[2], 110.5f) && near(b[3], 70.5f), "shape covers its box plus half a pixel");

    quads[0].rotation = 3.14159265f * 0.5f; // a wide box turned a quarter is tall
    mkui::tessellate(quads, list);
    bounds(list, b);
    check(b[3] - b[1] > b[2] - b[0], "rotation turns the box about its center");

    // same clip and texture share one command; a glyph between them breaks it
    quads = {shapeQuad(0, 0, 10, 10, 0), shapeQuad(20, 0, 10, 10, 2)};
    mkui::tessellate(quads, list);
    check(list.commands.size() == 1, "consecutive shapes share a command");
    mkui::Quad glyph = shapeQuad(40, 0, 8, 12, 0);
    glyph.flags = mkui::kQuadGlyph;
    glyph.uv[2] = glyph.uv[3] = 0.5f;
    quads.insert(quads.begin() + 1, glyph);
    mkui::tessellate(quads, list);
    check(list.commands.size() == 3 && list.commands[1].texture == mkui::DrawTexture::Glyphs && list.commands[1].indexCount == 6,
          "a glyph is its own textured command (two triangles)");
    bool uvSet = false;
    for (const mkui::DrawVertex& v : list.vertices)
        if (v.u == 0.5f && v.v == 0.5f) uvSet = true;
    check(uvSet, "glyph corners carry its atlas rect");

    quads = {shapeQuad(0, 0, 10, 10, 0)};
    quads[0].strokeWidth = 2;
    quads[0].strokeColor = 0xFFFFFFFFu;
    mkui::tessellate(quads, list);
    bool white = false;
    for (const mkui::DrawVertex& v : list.vertices)
        if (v.color == 0xFFFFFFFFu) white = true;
    check(white, "border vertices take the stroke color");

    struct Counting : mkui::DrawBackend {
        size_t indices = 0;
        void drawList(const mkui::DrawList& l) override { indices = l.indices.size(); }
    } backend;
    backend.drawQuads(quads);
    check(backend.indices == list.indices.size(), "DrawBackend tessellates by default");
}

const char* kBoundScreen = "@format ui 1\n"
                           "board hud 1920 1080\n"
                           "  frame bar\n"
                           "    pos 0 0\n"
                           "    size 400 40\n"
                           "    fill #FFFFFFFF\n"
                           "    visible alive\n"
                           "  end\n"
                           "end\n";

void testValueModel() {
    mkui::ValueModel vm;
    vm.set("alive", true);
    vm.set("score", 12);
    vm.child("stats").set("hp", 0.5f);
    auto& rows = vm.list("players");
    rows.resize(2);
    rows[1].set("name", "B");
    const mkui::ValueBinder& binder = mkui::valueBinder();
    mkui::Value v;
    check(binder.resolve(vm.model(), "score", v) && v.kind == mkui::ValueKind::Int && *static_cast<int*>(v.data) == 12, "int value");
    check(binder.resolve(vm.model(), "stats.hp", v) && v.kind == mkui::ValueKind::Float, "nested value");
    check(binder.resolve(vm.model(), "players[1].name", v) && *static_cast<std::string*>(v.data) == "B", "list row value");
    check(binder.resolve(vm.model(), "players", v) && v.kind == mkui::ValueKind::Array && v.count == 2, "list");
    check(!binder.resolve(vm.model(), "missing", v) && !binder.resolve(vm.model(), "players[5].name", v), "missing paths fail");

    MemoryHost host;
    host.files["ui/hud.ui"] = kBoundScreen;
    mkui::System ui(host, binder);
    mkui::ScreenId screen = ui.open("ui/hud.ui");
    ui.bind(screen, vm.model());
    mkui::Viewport viewport{0, 0, 1920, 1080};
    mkui::FrameInput input;
    ui.update(input, viewport, 1.0 / 60.0);
    const size_t shown = ui.render().size();
    vm.set("alive", false);
    ui.update(input, viewport, 1.0 / 60.0);
    const size_t hidden = ui.render().size();
    check(shown > hidden, "a bound value hides a node");
    check(ui.info(screen).errors.empty(), "no binding errors");
}

// Images are "W H" text files decoded to blank pixels, so atlas packing can be driven without PNGs
class ImageHost : public MemoryHost {
public:
    int atlasFull = 0;
    bool decodeImage(const std::string& bytes, mkui::u32& w, mkui::u32& h, std::vector<mkui::u8>& pixels) override {
        if (std::sscanf(bytes.c_str(), "%u %u", &w, &h) != 2) return false;
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);
        return true;
    }
    void log(mkui::LogLevel level, const std::string& message) override {
        if (message.find("image atlas full") != std::string::npos) ++atlasFull;
        MemoryHost::log(level, message);
    }
};

std::string imageScreen(const char* name, std::initializer_list<const char*> images) {
    std::string ui = std::string("@format ui 1\nboard ") + name + " 1920 1080\n";
    int i = 0;
    for (const char* path : images) {
        ui += "  image pic" + std::to_string(i) + "\n    pos " + std::to_string(i * 600) + " 0\n    size 500 500\n    src " + path + "\n  end\n";
        ++i;
    }
    return ui + "end\n";
}

std::vector<mkui::Quad> imageQuads(const std::vector<mkui::Quad>& quads) {
    std::vector<mkui::Quad> out;
    for (const mkui::Quad& q : quads)
        if ((q.flags & 0xF) == mkui::kQuadImage) out.push_back(q);
    return out;
}

// The image atlas fills up in the middle of a frame: quads drawn before the restart must not keep the old atlas regions
void testAtlasRebuild() {
    ImageHost host;
    host.files["ui/big.ui"] = imageScreen("big", {"ui/b.png"});
    host.files["ui/pair.ui"] = imageScreen("pair", {"ui/a.png", "ui/c.png"});
    host.files["ui/b.png"] = "2000 1500";
    host.files["ui/a.png"] = "500 500";
    host.files["ui/c.png"] = "2000 1000";
    host.files["ui/x.png"] = "2000 1500";
    host.files["ui/y.png"] = "2000 1500";
    host.files["ui/huge.ui"] = imageScreen("huge", {"ui/x.png", "ui/y.png"});
    NoBinder binder;
    mkui::System ui(host, binder);
    mkui::Viewport viewport{0, 0, 1920, 1080};
    mkui::FrameInput input;

    mkui::ScreenId big = ui.open("ui/big.ui");
    ui.update(input, viewport, 1.0 / 60.0);
    check(imageQuads(ui.render()).size() == 1, "big picture drawn");
    ui.close(big);
    // a.png still fits below the stale b.png (row 1501), c.png doesn't: the atlas restarts after a.png's quad was emitted and
    // c.png goes to row 0. The redraw reads a.png again, now below c.png (row 1001); without it a.png would point at row 1501.
    mkui::ScreenId pair = ui.open("ui/pair.ui");
    ui.update(input, viewport, 1.0 / 60.0);
    std::vector<mkui::Quad> quads = imageQuads(ui.render());
    check(host.atlasFull == 1, "image atlas restarted once");
    check(quads.size() == 2, "the redrawn frame has each picture once");
    check(quads.size() == 2 && quads[0].uv[1] == 1001.0f / mkui::kAtlasSize && quads[1].uv[1] == 0.0f,
          "pictures drawn before the restart use the fresh atlas");
    ui.update(input, viewport, 1.0 / 60.0);
    check(imageQuads(ui.render()).size() == 2 && host.atlasFull == 1, "next frame is steady");
    ui.close(pair);

    // Two pictures that can never share the atlas: restart, redraw once, and stop (no endless loop)
    host.atlasFull = 0;
    ui.open("ui/huge.ui");
    ui.update(input, viewport, 1.0 / 60.0);
    check(imageQuads(ui.render()).size() == 2 && host.atlasFull == 4, "a working set that can't fit is redrawn only once (two restarts a pass)");
}

} // namespace

// The Penpot plugin converter's expected files (penpot-plugin/test/expected, made by node --test) parse without errors
void testPenpotExpected() {
    auto read = [](const char* name, std::string& out) {
        std::ifstream in(std::string(MKUI_SOURCE_DIR) + "/penpot-plugin/test/expected/" + name, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        out = ss.str();
        return static_cast<bool>(in);
    };
    std::string theme, pause;
    check(read("theme.ui", theme) && theme.rfind("# MealKit UI exported from Penpot", 0) == 0 && theme.find("color accent #FFC83D") != std::string::npos,
          "expected theme.ui is in theme format");
    check(read("pause.ui", pause), "expected pause.ui read");
    mkui::Theme t;
    t.colors.emplace_back("accent", 0xFF3DC8FFu);
    MemoryHost host;
    mkui::Document doc;
    const bool ok = mkui::parseDocument(pause, "ui/pause.ui", doc, host, &t);
    for (const std::string& e : doc.errors) std::printf("  %s\n", e.c_str());
    check(ok && doc.exported && doc.navigation && doc.firstFocus == "resume" && doc.nodes.size() == 12 && doc.nodes[0].id == "id-pause",
          "expected pause.ui parses as an exported screen");
}

// <img:path> inside text: one picture quad per tag, one em tall and as wide as its aspect ratio, wrapping with the words
void testInlineImage() {
    ImageHost host;
    host.files["ui/burn.png"] = "60 30"; // 2:1
    host.files["ui/inline.ui"] = "@format ui 1\nboard inline 1920 1080\n  text line\n    pos 0 0\n    size 1000 100\n"
                                 "    fontSize 20\n    content a<img:ui/burn.png>b <img:ui/burn.png>\n  end\n"
                                 "  text missing\n    pos 0 200\n    size 1000 100\n    content <img:ui/none.png>\n  end\nend\n";
    NoBinder binder;
    mkui::System ui(host, binder);
    mkui::ScreenId screen = ui.open("ui/inline.ui");
    mkui::Viewport viewport{0, 0, 1920, 1080};
    mkui::FrameInput input;
    ui.update(input, viewport, 1.0 / 60.0);
    const std::vector<mkui::Quad> quads = imageQuads(ui.render());
    check(quads.size() == 2, "two inline pictures drawn (the missing one draws nothing)");
    check(quads.size() == 2 && near(quads[0].rect[3], 20.0f) && near(quads[0].rect[2], 40.0f), "inline picture is one em tall, 2:1 wide");
    // No fonts in these tests (letters take no room): the second picture starts where the first ends
    check(quads.size() == 2 && near(quads[1].rect[0], quads[0].rect[0] + 40.0f), "the second picture follows the first");
    bool reported = false;
    for (const std::string& e : ui.info(screen).errors) reported |= e.find("ui/none.png") != std::string::npos;
    check(reported, "a missing inline picture is reported");
}

void testSqueeze() {
    ImageHost host;
    host.files["ui/burn.png"] = "60 30"; // 2:1, one em tall = 40 wide at fontSize 20
    // Five pictures (200 wide) in a 100 wide box: squeezed to half; the same line unsqueezed overflows; a short line stays
    host.files["ui/squeeze.ui"] = "@format ui 1\nboard squeeze 1920 1080\n"
                                  "  text wide\n    pos 10 0\n    size 100 40\n    fontSize 20\n    wrap none\n    squeeze\n    textAlign center\n"
                                  "    content <img:ui/burn.png><img:ui/burn.png><img:ui/burn.png><img:ui/burn.png><img:ui/burn.png>\n  end\n"
                                  "  text plain\n    pos 10 100\n    size 100 40\n    fontSize 20\n    wrap none\n"
                                  "    content <img:ui/burn.png><img:ui/burn.png><img:ui/burn.png><img:ui/burn.png><img:ui/burn.png>\n  end\n"
                                  "  text short\n    pos 10 200\n    size 100 40\n    fontSize 20\n    squeeze\n    textAlign center\n"
                                  "    content <img:ui/burn.png>\n  end\nend\n";
    NoBinder binder;
    mkui::System ui(host, binder);
    ui.open("ui/squeeze.ui");
    mkui::Viewport viewport{0, 0, 1920, 1080};
    mkui::FrameInput input;
    ui.update(input, viewport, 1.0 / 60.0);
    const std::vector<mkui::Quad> quads = imageQuads(ui.render());
    check(quads.size() == 11, "squeeze: all pictures drawn");
    if (quads.size() != 11) return;
    check(near(quads[0].rect[0], 10.0f) && near(quads[0].rect[2], 20.0f), "squeeze: a wide line starts at the box and is half as wide");
    check(near(quads[4].rect[0] + quads[4].rect[2], 110.0f), "squeeze: the squeezed line ends at the box's right edge");
    check(near(quads[5].rect[2], 40.0f) && near(quads[9].rect[0], 170.0f), "no squeeze: the same line runs past the box");
    check(near(quads[10].rect[2], 40.0f) && near(quads[10].rect[0], 40.0f), "squeeze: a line that fits keeps its size and alignment");
}

void testSqueezeField() {
    ImageHost host;
    host.files["ui/burn.png"] = "60 30"; // one em tall = 40 wide at fontSize 20
    // Two pictures (80 wide) at half width, centred in a 100 wide box; four (160) at half still overflow 60 wide and squeeze fits them
    host.files["ui/narrow.ui"] = "@format ui 1\nboard narrow 1920 1080\n"
                                 "  text centred\n    pos 10 0\n    size 100 40\n    fontSize 20\n    wrap none\n    squeezeField n\n    textAlign center\n"
                                 "    content <img:ui/burn.png><img:ui/burn.png>\n  end\n"
                                 "  text fitted\n    pos 10 100\n    size 60 40\n    fontSize 20\n    wrap none\n    squeeze\n    squeezeField n\n"
                                 "    content <img:ui/burn.png><img:ui/burn.png><img:ui/burn.png><img:ui/burn.png>\n  end\nend\n";
    mkui::ValueModel vm;
    vm.set("n", 0.5f);
    mkui::System ui(host, mkui::valueBinder());
    ui.bind(ui.open("ui/narrow.ui"), vm.model());
    mkui::Viewport viewport{0, 0, 1920, 1080};
    mkui::FrameInput input;
    ui.update(input, viewport, 1.0 / 60.0);
    const std::vector<mkui::Quad> quads = imageQuads(ui.render());
    check(quads.size() == 6, "squeezeField: all pictures drawn");
    if (quads.size() != 6) return;
    check(near(quads[0].rect[2], 20.0f) && near(quads[0].rect[0], 40.0f), "squeezeField: half as wide, the narrowed line centred");
    check(near(quads[2].rect[0], 10.0f) && near(quads[5].rect[0] + quads[5].rect[2], 70.0f), "squeezeField + squeeze: still too wide, fills the box");
}

void testSkew() {
    ImageHost host;
    host.files["ui/burn.png"] = "60 30";
    // The same picture with and without skew: slanted, it carries the shear and moves right (its centre is above the baseline)
    host.files["ui/skew.ui"] = "@format ui 1\nboard skew 1920 1080\n"
                               "  text plain\n    pos 10 0\n    size 100 40\n    fontSize 20\n    wrap none\n    content <img:ui/burn.png>\n  end\n"
                               "  text slanted\n    pos 10 100\n    size 100 40\n    fontSize 20\n    wrap none\n    skew 10\n    content <img:ui/burn.png>\n  end\n"
                               "  text field\n    pos 10 200\n    size 100 40\n    fontSize 20\n    wrap none\n    skewField k\n    content <img:ui/burn.png>\n  end\nend\n";
    mkui::ValueModel vm;
    vm.set("k", -10.0f);
    mkui::System ui(host, mkui::valueBinder());
    ui.bind(ui.open("ui/skew.ui"), vm.model());
    mkui::Viewport viewport{0, 0, 1920, 1080};
    mkui::FrameInput input;
    ui.update(input, viewport, 1.0 / 60.0);
    const std::vector<mkui::Quad> quads = imageQuads(ui.render());
    check(quads.size() == 3, "skew: all pictures drawn");
    if (quads.size() != 3) return;
    check(mkui::quadShear(quads[0].flags) == 0.0f, "skew: none by default");
    check(near(mkui::quadShear(quads[1].flags), 0.1763f) && quads[1].rect[0] > quads[0].rect[0], "skew 10: sheared, leaning right about the baseline");
    check(near(mkui::quadShear(quads[2].flags), -0.1763f) && quads[2].rect[0] < quads[0].rect[0], "skewField -10: leaning left");
    check((quads[1].flags & 0xF) == mkui::kQuadImage, "skew: the kind bits are kept");
    mkui::DrawList list;
    mkui::tessellate(&quads[1], 1, list);
    float topX = 1e9f, bottomX = 1e9f, topY = 1e9f, bottomY = -1e9f;
    for (const mkui::DrawVertex& v : list.vertices) topY = std::min(topY, v.y), bottomY = std::max(bottomY, v.y);
    for (const mkui::DrawVertex& v : list.vertices) {
        if (near(v.y, topY)) topX = std::min(topX, v.x);
        if (near(v.y, bottomY)) bottomX = std::min(bottomX, v.x);
    }
    check(topX > bottomX, "skew: tessellated tops lean right");
}

int main() {
    testPenpotExpected();
    testDocument();
    testSourceRoundTrip();
    testSystem();
    testTessellate();
    testValueModel();
    testAtlasRebuild();
    testInlineImage();
    testSqueeze();
    testSqueezeField();
    testSkew();
    std::printf("mkui %s: %d checks, %d failed\n", MKUI_VERSION, checks, failures);
    return failures == 0 ? 0 : 1;
}
