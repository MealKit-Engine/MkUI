#pragma once

#include "mkui/Types.h"

#include <string>
#include <vector>

// What the runtime asks of the program that uses it (the engine's mealkit/ui/ glue). Files are asset paths ("ui/hud.ui", "fonts/a.ttf").
namespace mkui {

enum class LogLevel : u8 { Info, Warn };
enum class AtlasKind : u8 { Glyphs, Images }; // glyph SDF (1 byte per pixel), UI images (RGBA 4 bytes)
inline constexpr u32 kAtlasSize = 2048;       // both atlases are kAtlasSize × kAtlasSize

class Host {
public:
    virtual ~Host() = default;
    virtual bool readFile(const std::string& assetPath, std::string& out) = 0;
    virtual u64 fileTime(const std::string& assetPath) = 0; // change stamp, 0 if missing
    virtual std::vector<std::string> listFiles(const std::string& assetFolder, const char* extension) = 0; // file names (no folder)
    virtual void log(LogLevel level, const std::string& message) = 0;
    // String table text for a key (the key itself if there's no table). The reference stays valid until the next call
    virtual const std::string& text(const std::string& key) = 0;
    // PNG/JPEG → RGBA8. false if it can't be decoded
    virtual bool decodeImage(const std::string& bytes, u32& width, u32& height, std::vector<u8>& rgba) = 0;
    // Copies pixels into an atlas region (no-op when there's nothing to draw with: layout-only use)
    virtual void uploadAtlas(AtlasKind atlas, u32 x, u32 y, u32 w, u32 h, const u8* pixels) = 0;
};

// ---------------------------------------------------------------- View model binding

// A view model value. type is opaque to the runtime (the host's type description), data points at the value
struct Model {
    const void* type = nullptr;
    void* data = nullptr;
    explicit operator bool() const { return type != nullptr; }
};

enum class ValueKind : u8 { Other, Bool, Int, UInt, Float, Ticks, Color, String, Array, Struct };

struct Value {
    ValueKind kind = ValueKind::Other;
    u32 size = 0;  // bytes (Int/UInt: 1, 2, 4, 8)
    void* data = nullptr;
    const void* info = nullptr; // the host's own description of the value (for Binder::format)
    const std::vector<std::string>* enumNames = nullptr; // integer enums: names shown for 0, 1, 2 ...
    f64 ticksPerSecond = 60.0;                           // Ticks: shown as seconds
    // Array: fixed slots; the list shows the first min(count, <field>Count) of them
    u32 count = 0;
    u32 elementSize = 0;
    ValueKind elementKind = ValueKind::Other;
    const void* elementType = nullptr; // Model::type of a Struct element
};

class Binder {
public:
    virtual ~Binder() = default;
    // Field path ("score", "players[2].name", "stats.hp") inside a model
    virtual bool resolve(const Model& model, const std::string& path, Value& out) const = 0;
    virtual const char* typeName(const Model& model) const = 0;
    // Text for kinds the runtime doesn't format itself (ValueKind::Other)
    virtual void format(const Value& value, std::string& out) const = 0;
};

} // namespace mkui
