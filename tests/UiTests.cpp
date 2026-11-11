// mkui tests without an engine: an in-memory host, no GPU (atlas uploads are dropped).
#include "mkui/Document.h"
#include "mkui/Host.h"
#include "mkui/Source.h"
#include "mkui/Ui.h"

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

} // namespace

int main() {
    testDocument();
    testSourceRoundTrip();
    testSystem();
    std::printf("mkui %s: %d checks, %d failed\n", MKUI_VERSION, checks, failures);
    return failures == 0 ? 0 : 1;
}
