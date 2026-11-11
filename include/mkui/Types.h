#pragma once

#include <cstdint>

// mkui: the game UI runtime (.ui reading, Penpot-style layout, text layout, view model binding). No engine headers:
// the host (mealkit/ui/ in the engine) fills in mkui::Host and mkui::Binder (mkui/Host.h) and draws the output quads.
namespace mkui {

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f32 = float;
using f64 = double;

// sRGB RGBA8 packed as R in the low byte (same as the engine's render::rgba)
constexpr u32 rgba(u8 r, u8 g, u8 b, u8 a = 255) {
    return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
}

} // namespace mkui
