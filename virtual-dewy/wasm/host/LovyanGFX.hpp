// LovyanGFX stand-in for the browser build. Based on firmware/test/mock/LovyanGFX.hpp, extended so that:
//  - pushImage() keeps the frame the firmware sends to the screen (the browser draws it),
//  - setBrightness() and getTouch() talk to the browser (backlight level, taps),
//  - every pin number that display.h hands to the driver is written down (so tests can compare it with the
//    wiring panel: this is what the REAL display.h assigns, after config.h's macros are expanded),
//  - the little text sprite used for the clock's date line draws with the 18 pt font scaled down (APPROXIMATE:
//    the real board uses FreeSansBold12pt7b, which is not in the repo's test fonts).
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

enum spi_host_device_t { SPI2_HOST = 1, SPI3_HOST = 2 };

// ---- state shared with the browser
inline uint16_t g_fb565[320 * 240];      // the last frame pushed to the screen (byte-swapped RGB565, as the firmware builds it)
inline uint32_t g_fbSerial = 0;          // counts frames
inline int g_brightness = 0;
inline int g_touchDown = 0, g_touchX = 0, g_touchY = 0, g_touchPresent = 1;
struct HostPinReport {
  int sclk = -1, mosi = -1, miso = -1, dc = -1, cs = -1, rst = -1, bl = -1;
  int tcs = -1, tirq = -1, tsclk = -1, tmosi = -1, tmiso = -1;
  int lightSet = 0, busSet = 0, panelSet = 0, touchSet = 0;
};
inline HostPinReport g_pins;

namespace lgfx { inline namespace v1 {
struct swap565_t { uint16_t raw; };
struct GFXglyph { uint32_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };
struct GFXfont { uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; float scale = 1.f; };
namespace fonts { extern const GFXfont FreeSansBold12pt7b, FreeSansBold18pt7b, FreeSansBold24pt7b; }
enum class textdatum_t { top_center = 1 };

struct Bus_SPI {
  struct config_t { uint32_t freq_write, freq_read; int16_t pin_sclk, pin_miso, pin_mosi, pin_dc; uint8_t spi_mode; bool spi_3wire, use_lock; uint8_t dma_channel; spi_host_device_t spi_host; };
  config_t c;
  config_t config() const { return c; }
  void config(const config_t& x) { c = x; g_pins.sclk = x.pin_sclk; g_pins.mosi = x.pin_mosi; g_pins.miso = x.pin_miso; g_pins.dc = x.pin_dc; g_pins.busSet = 1; }
};
struct ITouch {
  struct config_t { uint32_t freq; uint16_t x_min, x_max, y_min, y_max; bool bus_shared; int16_t pin_int, pin_rst; uint8_t offset_rotation;
    union { struct { int8_t spi_host; int16_t pin_sclk, pin_mosi, pin_miso; }; struct { int8_t i2c_port; int16_t pin_scl, pin_sda, i2c_addr; }; }; int16_t pin_cs; };
  config_t c;
  config_t config() const { return c; }
  void config(const config_t& x) { c = x; g_pins.tcs = x.pin_cs; g_pins.tirq = x.pin_int; g_pins.tsclk = x.pin_sclk; g_pins.tmosi = x.pin_mosi; g_pins.tmiso = x.pin_miso; g_pins.touchSet = 1; }
};
struct Touch_XPT2046 : ITouch {};
struct Light_PWM {
  struct config_t { uint32_t freq; int16_t pin_bl; uint8_t pwm_channel; bool invert; };
  config_t c;
  config_t config() const { return c; }
  void config(const config_t& x) { c = x; g_pins.bl = x.pin_bl; g_pins.lightSet = 1; }
};
struct Panel_Device {
  struct config_t { int16_t pin_cs, pin_rst, pin_busy; uint16_t memory_width, memory_height, panel_width, panel_height; int16_t offset_x, offset_y; uint8_t offset_rotation, dummy_read_pixel, dummy_read_bits; bool readable, invert, rgb_order, dlen_16bit, bus_shared; };
  config_t c;
  config_t config() const { return c; }
  void config(const config_t& x) { c = x; g_pins.cs = x.pin_cs; g_pins.rst = x.pin_rst; g_pins.panelSet = 1; }
  void setBus(Bus_SPI*) {} void setLight(Light_PWM*) {} void setTouch(ITouch*) {}
};
struct Panel_ILI9341 : Panel_Device {};

struct LGFXBase {
  void init() {}
  void setRotation(int) {}
  void setBrightness(uint8_t b) { g_brightness = b; }
  void fillScreen(int) {}
  template <typename T> void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const T* data) {
    static_assert(sizeof(T) == 2, "pushImage expects 16-bit pixels");
    const uint16_t* src = reinterpret_cast<const uint16_t*>(data);
    for (int32_t row = 0; row < h; row++) {
      int32_t yy = y + row; if (yy < 0 || yy >= 240) continue;
      for (int32_t col = 0; col < w; col++) { int32_t xx = x + col; if (xx >= 0 && xx < 320) g_fb565[yy * 320 + xx] = src[row * w + col]; }
    }
    g_fbSerial++;
  }
  template <typename T> uint8_t getTouch(T* x, T* y, uint8_t = 0) {
    if (!g_touchPresent || !g_touchDown) return 0;
    *x = T(g_touchX); *y = T(g_touchY); return 1;
  }
};
struct LGFX_Device : LGFXBase { void setPanel(Panel_Device*) {} };

// A small off-screen drawing surface, just enough for the date line: text in white on black, read back as brightness.
struct LGFX_Sprite : LGFXBase {
  int w = 0, h = 0, datum = 0; std::vector<uint16_t> px; const GFXfont* font = nullptr;
  LGFX_Sprite(LGFX_Device*) {}
  void setColorDepth(int) {}
  void* createSprite(int W, int H) { w = W; h = H; px.assign(size_t(W) * H, 0); return this; }
  void deleteSprite() { px.clear(); w = h = 0; }
  void fillScreen(int c) { std::fill(px.begin(), px.end(), uint16_t(c)); }
  void setFont(const GFXfont* f) { font = f; }
  void setTextDatum(textdatum_t d) { datum = int(d); }
  void setTextColor(int) {}
  uint16_t readPixel(int x, int y) { return (x >= 0 && y >= 0 && x < w && y < h) ? px[size_t(y) * w + x] : 0; }
  void drawString(const char* s, int x, int y) {
    if (!font || !s) return;
    const float sc = font->scale;
    auto glyphFor = [&](unsigned char c) -> const GFXglyph* { return (c >= font->first && c <= font->last) ? &font->glyph[c - font->first] : nullptr; };
    float width = 0;
    for (const char* p = s; *p; p++) if (auto g = glyphFor((unsigned char)*p)) width += g->xAdvance * sc;
    const GFXglyph* l = glyphFor('l');
    float baseline = y + (l ? -l->yOffset * sc : font->yAdvance * sc * 0.7f);
    float pen = x - width / 2;                        // top_center: x is the middle of the text, y is the top
    for (const char* p = s; *p; p++) {
      const GFXglyph* g = glyphFor((unsigned char)*p);
      if (!g) continue;
      int dw = int(std::ceil(g->width * sc)), dh = int(std::ceil(g->height * sc));
      int dx0 = int(std::floor(pen + g->xOffset * sc)), dy0 = int(std::floor(baseline + g->yOffset * sc));
      for (int j = 0; j < dh; j++) for (int i = 0; i < dw; i++) {
        // average the font's 1-bit pixels that fall under this destination pixel (box filter)
        int sx0 = int(i / sc), sx1 = std::max(sx0 + 1, int(std::ceil((i + 1) / sc))), sy0 = int(j / sc), sy1 = std::max(sy0 + 1, int(std::ceil((j + 1) / sc)));
        int on = 0, all = 0;
        for (int sy = sy0; sy < sy1 && sy < g->height; sy++) for (int sx = sx0; sx < sx1 && sx < g->width; sx++) {
          uint32_t bit = uint32_t(sy) * g->width + sx;
          on += (font->bitmap[g->bitmapOffset + (bit >> 3)] >> (7 - (bit & 7))) & 1; all++;
        }
        if (!all || !on) continue;
        int v = on * 255 / all, X = dx0 + i, Y = dy0 + j;
        if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
        uint16_t c = uint16_t(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
        uint16_t& d = px[size_t(Y) * w + X];
        if (((c >> 5) & 63) > ((d >> 5) & 63)) d = c;
      }
      pen += g->xAdvance * sc;
    }
  }
};
}}
using LGFX_Sprite = lgfx::LGFX_Sprite;
using lgfx::textdatum_t;
namespace fonts { using namespace lgfx::v1::fonts; }
static constexpr int TFT_BLACK = 0, TFT_WHITE = 0xFFFF;
