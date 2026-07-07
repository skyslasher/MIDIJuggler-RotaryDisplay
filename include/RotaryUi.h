#pragma once

// LGFXScreenBuilder export for MIDIJuggler-RotaryDisplay boot screen.
// Profile index 2 = 240x240 (Elecrow CrowPanel). Re-export from
// https://tanakamasayuki.github.io/LGFXScreenBuilder/ when layouts change.
// Include <LovyanGFX.hpp> and <LGFXScreenBuilder.h> before this header.
//
// Note: LGFXScreenBuilder 0.2.x descriptor structs use default member
// initializers (non-aggregate). Factory helpers below work around that on
// the ESP32 GCC toolchain.

namespace RotaryUi {

enum class Profile : uint8_t { Auto = 0, Core, Stick, Cardputer };

namespace Scene {
struct Boot {
  static constexpr lgfxsb::SceneId id = 0;
  const char* boot = nullptr;
};
struct Main {
  static constexpr lgfxsb::SceneId id = 1;
  const char* title = nullptr;
  const char* battery = nullptr;
  const char* temp = nullptr;
};
struct Settings {
  static constexpr lgfxsb::SceneId id = 2;
  const char* ttl = nullptr;
  const char* row1 = nullptr;
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
      part("boot", lgfxsb::PartType::Text),
      part("headerBand", lgfxsb::PartType::Rect),
      part("title", lgfxsb::PartType::Text),
      part("battery", lgfxsb::PartType::Text),
      part("temp", lgfxsb::PartType::Text),
      part("panel", lgfxsb::PartType::Rect),
      part("header", lgfxsb::PartType::Rect),
      part("ttl", lgfxsb::PartType::Text),
      part("row1", lgfxsb::PartType::Text),
  };
  return data;
}

inline const lgfxsb::SceneDesc* scenes() {
  static const lgfxsb::SceneDesc data[] = {
      scene(0, "Boot", 0, 2),
      scene(1, "Main", 2, 5),
      scene(2, "Settings", 7, 3),
  };
  return data;
}

inline const lgfxsb::PartLayout* layouts() {
  static const lgfxsb::PartLayout data[] = {
      // ---- Profile: Core 320x240 rot0 ----
      layout(110, 80, 100, 60, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(160, 160, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 2.0f, 0x9ce5ac, true, true, nullptr, "Booting..."),
      layout(0, 0, 320, 40, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(12, 10, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 2.0f, 0xffffff, true, true, nullptr, "Main"),
      layout(310, 12, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopRight, 1.5f, 0x9ce5ac, true, true, nullptr, "82%"),
      layout(18, 70, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 4.0f, 0xffffff, true, true, nullptr, "24.5C"),
      layout(18, 150, 284, 54, 0, 0, 0, 0, 0.0f, 0x172126),
      layout(0, 0, 320, 40, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(12, 10, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 2.0f, 0xffffff, true, true, nullptr, "Settings"),
      layout(18, 60, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 2.0f, 0xffffff, true, true, nullptr, "Wi-Fi"),
      // ---- Profile: Stick 135x240 rot0 ----
      layout(30, 80, 75, 50, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(69, 148, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 1.5f, 0x9ce5ac, true, true, nullptr, "Boot..."),
      layout(0, 0, 135, 30, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(8, 7, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0xffffff, true, true, nullptr, "Main"),
      layout(8, 180, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0x9ce5ac, true, true, nullptr, "82%"),
      layout(10, 60, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 3.5f, 0xffffff, true, true, nullptr, "24.5"),
      layout(10, 110, 115, 60, 0, 0, 0, 0, 0.0f, 0x172126),
      layout(0, 0, 135, 30, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(8, 7, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0xffffff, true, true, nullptr, "Settings"),
      layout(10, 40, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0xffffff, true, true, nullptr, "Wi-Fi"),
      // ---- Profile: Elecrow 240x240 rot0 (enum Cardputer -> index 2) ----
      layout(24, 56, 192, 128, 0, 0, 16, 0, 0.0f, 0x202028),
      layout(120, 108, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::MidCenter, 2.5f, 0xffffff, true, true, nullptr, "MIDIJuggler"),
      layout(0, 0, 240, 26, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(8, 5, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0xffffff, true, true, nullptr, "Main"),
      layout(232, 6, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopRight, 1.25f, 0x9ce5ac, true, true, nullptr, "82%"),
      layout(12, 40, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 3.0f, 0xffffff, true, true, nullptr, "24.5C"),
      layout(12, 86, 216, 40, 0, 0, 0, 0, 0.0f, 0x172126, true, false),
      layout(0, 0, 240, 26, 0, 0, 0, 0, 0.0f, 0x1e2a30),
      layout(8, 5, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0xffffff, true, true, nullptr, "Settings"),
      layout(12, 36, 0, 0, 0, 0, 0, (uint8_t)lgfxsb::Datum::TopLeft, 1.5f, 0xffffff, true, true, nullptr, "Wi-Fi"),
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
    p.sceneCount = 3;
    p.parts = parts();
    p.partCount = 10;
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
  void (*_ov_Boot)(Canvas&, const Scene::Boot&) = nullptr;
  static void _ovt_Boot(Canvas& g, const void* s, const void* fnp) {
    (*static_cast<void (*const*)(Canvas&, const Scene::Boot&)>(fnp))(g, *static_cast<const Scene::Boot*>(s));
  }

 public:
  explicit Screen(lgfx::LGFX_Device& gfx) : Base(gfx, detail::project()) {}
  void setProfile(Profile p) { _profile = static_cast<uint8_t>(p); }
  void show(const Scene::Boot& s) {
    lgfxsb::Value v[2];
    if (s.boot) v[1] = lgfxsb::Value::text(s.boot);
    renderScene(Scene::Boot::id, v, 2, _ov_Boot ? &_ovt_Boot : nullptr, &s, &_ov_Boot);
  }
  void setOverlay(void (*fn)(Canvas&, const Scene::Boot&)) { _ov_Boot = fn; }
};

}  // namespace RotaryUi
