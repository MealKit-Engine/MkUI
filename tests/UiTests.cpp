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
#include <map>
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

} // namespace

int main() {
    testDocument();
    testSourceRoundTrip();
    testSystem();
    testTessellate();
    testValueModel();
    std::printf("mkui %s: %d checks, %d failed\n", MKUI_VERSION, checks, failures);
    return failures == 0 ? 0 : 1;
}
