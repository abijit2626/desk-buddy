
#pragma once
#include <cstdint>
enum spi_host_device_t { SPI2_HOST = 1, SPI3_HOST = 2 };
namespace lgfx { inline namespace v1 {
struct swap565_t { uint16_t raw; };
struct GFXglyph { uint32_t bitmapOffset; uint8_t width, height, xAdvance; int8_t xOffset, yOffset; };
struct GFXfont { uint8_t* bitmap; GFXglyph* glyph; uint16_t first, last; uint8_t yAdvance; };
namespace fonts { extern const GFXfont FreeSansBold12pt7b, FreeSansBold18pt7b, FreeSansBold24pt7b; }
enum class textdatum_t { top_center = 1 };
struct Bus_SPI { struct config_t { uint32_t freq_write, freq_read; int16_t pin_sclk, pin_miso, pin_mosi, pin_dc; uint8_t spi_mode; bool spi_3wire, use_lock; uint8_t dma_channel; spi_host_device_t spi_host; }; config_t c; config_t config() const { return c; } void config(const config_t& x) { c = x; } };
struct ITouch { struct config_t { uint32_t freq; uint16_t x_min, x_max, y_min, y_max; bool bus_shared; int16_t pin_int, pin_rst; uint8_t offset_rotation; union { struct { int8_t spi_host; int16_t pin_sclk, pin_mosi, pin_miso; }; struct { int8_t i2c_port; int16_t pin_scl, pin_sda, i2c_addr; }; }; int16_t pin_cs; }; config_t c; config_t config() const { return c; } void config(const config_t& x) { c = x; } };
struct Touch_XPT2046 : ITouch {};
struct Light_PWM { struct config_t { uint32_t freq; int16_t pin_bl; uint8_t pwm_channel; bool invert; }; config_t c; config_t config() const { return c; } void config(const config_t& x) { c = x; } };
struct Panel_Device { struct config_t { int16_t pin_cs, pin_rst, pin_busy; uint16_t memory_width, memory_height, panel_width, panel_height; int16_t offset_x, offset_y; uint8_t offset_rotation, dummy_read_pixel, dummy_read_bits; bool readable, invert, rgb_order, dlen_16bit, bus_shared; }; config_t c; config_t config() const { return c; } void config(const config_t& x) { c = x; } void setBus(Bus_SPI*) {} void setLight(Light_PWM*) {} void setTouch(ITouch*) {} };
struct Panel_ILI9341 : Panel_Device {};
struct LGFXBase {
  void init() {} void setRotation(int) {} void setBrightness(uint8_t) {} void fillScreen(int) {}
  template <typename T> void pushImage(int32_t, int32_t, int32_t, int32_t, const T*) {}
  template <typename T> uint8_t getTouch(T*, T*, uint8_t = 0) { return 0; }
  void setFont(const GFXfont*) {} void setTextDatum(textdatum_t) {} void setTextColor(int) {}
  void drawString(const char*, int, int) {} uint16_t readPixel(int, int) { return 0; }
};
struct LGFX_Device : LGFXBase { void setPanel(Panel_Device*) {} };
struct LGFX_Sprite : LGFXBase { LGFX_Sprite(LGFX_Device*) {} void setColorDepth(int) {} void* createSprite(int, int) { return this; } void deleteSprite() {} };
}}
using LGFX_Sprite = lgfx::LGFX_Sprite;
using lgfx::textdatum_t;
namespace fonts { using namespace lgfx::v1::fonts; }
static constexpr int TFT_BLACK = 0, TFT_WHITE = 0xFFFF;
