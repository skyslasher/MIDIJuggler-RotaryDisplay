#pragma once

// LGFXScreenBuilder export for MIDIJuggler-RotaryDisplay boot splash.
// Profile index 2 = 240x240 (Elecrow CrowPanel). Re-export from
// https://tanakamasayuki.github.io/LGFXScreenBuilder/ when layouts change.
// Include <LovyanGFX.hpp> and <LGFXScreenBuilder.h> before this header.
//
// Note: LGFXScreenBuilder 0.2.x descriptor structs use default member
// initializers (non-aggregate). Factory helpers below work around that on
// the ESP32 GCC toolchain.

namespace RotaryUi {

enum class Profile : uint8_t { Auto = 0, Core, Stick, Rotary };

namespace Scene {
struct Boot {
  static constexpr lgfxsb::SceneId id = 0;
  const char* title = nullptr;
  const char* subtitle = nullptr;
};
}  // namespace Scene

namespace detail {

inline lgfxsb::PartDesc part(const char* id, lgfxsb::PartType type, int16_t assetIndex = -1) {
  lgfxsb::PartDesc p;
  p.id = id;
  p.type = type;
  p.assetIndex = assetIndex;
  return p;
}

inline lgfxsb::SceneDesc scene(lgfxsb::SceneId id, const char* name, uint16_t partStart, uint16_t partCount) {
  lgfxsb::SceneDesc s;
  s.id = id;
  s.name = name;
  s.partStart = partStart;
  s.partCount = partCount;
  return s;
}

inline lgfxsb::ProfileDesc profile(int16_t w, int16_t h, uint8_t rotation = 0) {
  lgfxsb::ProfileDesc p;
  p.w = w;
  p.h = h;
  p.rotation = rotation;
  return p;
}

inline lgfxsb::PartLayout layout(int16_t x, int16_t y, int16_t w = 0, int16_t h = 0, int16_t x2 = 0, int16_t y2 = 0,
                                 int16_t r = 0, uint8_t datum = 0, float size = 1.0f, uint32_t color = 0,
                                 bool fill = true, bool visible = true, const void* font = nullptr,
                                 const char* text = nullptr) {
  lgfxsb::PartLayout l;
  l.x = x;
  l.y = y;
  l.w = w;
  l.h = h;
  l.x2 = x2;
  l.y2 = y2;
  l.r = r;
  l.datum = datum;
  l.size = size;
  l.color = color;
  l.fill = fill;
  l.visible = visible;
  l.font = font;
  l.text = text;
  return l;
}

inline const lgfxsb::PartDesc* parts() {
  static const lgfxsb::PartDesc data[] = {
      part("logo", lgfxsb::PartType::Rect),
      part("title", lgfxsb::PartType::Text),
      part("subtitle", lgfxsb::PartType::Text),
  };
  return data;
}

inline const lgfxsb::SceneDesc* scenes() {
  static const lgfxsb::SceneDesc data[] = {
      scene(0, "Boot", 0, 3),
  };
  return data;
}

inline const lgfxsb::PartLayout* layouts() {
  static const lgfxsb::PartLayout data[] = {
      // ---- Profile: Core 320x240 rot0 ----
      layout(110, 80, 100, 60, 0, 0, 8, 0, 0.0f, 0x202028),
      layout(160, 118, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 2.0f, 0xffffff, true, true, nullptr,
             "MIDIJuggler"),
      layout(160, 152, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 1.0f, 0x40c0f0, true, true, nullptr,
             "Rotary Display"),
      // ---- Profile: Stick 135x240 rot0 ----
      layout(30, 80, 75, 50, 0, 0, 8, 0, 0.0f, 0x202028),
      layout(68, 112, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 1.5f, 0xffffff, true, true, nullptr,
             "MIDIJuggler"),
      layout(68, 140, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 1.0f, 0x40c0f0, true, true, nullptr,
             "Rotary Display"),
      // ---- Profile: Rotary 240x240 rot0 (enum Rotary -> index 2) ----
      layout(24, 56, 192, 128, 0, 0, 16, 0, 0.0f, 0x202028),
      layout(120, 108, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 2.5f, 0xffffff, true, true, nullptr,
             "MIDIJuggler"),
      layout(120, 142, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 1.0f, 0x40c0f0, true, true, nullptr,
             "Rotary Display"),
  };
  return data;
}

inline const lgfxsb::ProfileDesc* profiles() {
  static const lgfxsb::ProfileDesc data[] = {
      profile(320, 240),
      profile(135, 240),
      profile(240, 240),
  };
  return data;
}

inline const lgfxsb::Project& project() {
  static const lgfxsb::Project data = []() {
    lgfxsb::Project p;
    p.profiles = profiles();
    p.profileCount = 3;
    p.scenes = scenes();
    p.sceneCount = 1;
    p.parts = parts();
    p.partCount = 3;
    p.layouts = layouts();
    p.background = 0x101018;
    return p;
  }();
  return data;
}

}  // namespace detail

#if defined(LGFXVIRTUALCANVAS_H)
using Canvas = LGFXVirtualCanvas;
#else
using Canvas = lgfx::LGFXBase;
#endif

class Screen : public lgfxsb::RendererT<Canvas> {
  using Base = lgfxsb::RendererT<Canvas>;

 public:
  explicit Screen(lgfx::LGFX_Device& gfx) : Base(gfx, detail::project()) {}
  void setProfile(Profile p) { _profile = static_cast<uint8_t>(p); }
  void show(const Scene::Boot& s) {
    lgfxsb::Value v[3];
    if (s.title) v[1] = lgfxsb::Value::text(s.title);
    if (s.subtitle) v[2] = lgfxsb::Value::text(s.subtitle);
    renderScene(Scene::Boot::id, v, 3, nullptr, nullptr, nullptr);
  }
};

}  // namespace RotaryUi
