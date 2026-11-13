// LovyanGFX setup for the 3.2" ILI9341 + XPT2046 touch: either one shared SPI bus, or (SCREEN_PARALLEL) the screen on
// the ESP32-S3's 8-bit parallel LCD controller with touch on its own small SPI bus.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
#if SCREEN_PARALLEL
  lgfx::Bus_Parallel8 _bus;
#else
  lgfx::Bus_SPI _bus;
#endif
  lgfx::Light_PWM _light;
#if TOUCH_ENABLED
  lgfx::Touch_XPT2046 _touch;
#endif
public:
  LGFX() {
#if SCREEN_PARALLEL
    {
      auto cfg = _bus.config();
      cfg.freq_write = LCD_FREQ;
      cfg.pin_wr = PIN_LCD_WR;
      cfg.pin_rd = -1;               // LCD_RD tied to 3V3: Dewy never reads the screen back
      cfg.pin_rs = PIN_LCD_RS;
      cfg.pin_d0 = PIN_LCD_D0; cfg.pin_d1 = PIN_LCD_D1; cfg.pin_d2 = PIN_LCD_D2; cfg.pin_d3 = PIN_LCD_D3;
      cfg.pin_d4 = PIN_LCD_D4; cfg.pin_d5 = PIN_LCD_D5; cfg.pin_d6 = PIN_LCD_D6; cfg.pin_d7 = PIN_LCD_D7;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
#else
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = SPI_FREQ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SCREEN_DMA ? SPI_DMA_CH_AUTO : 0;   // strips are sent by DMA while the next one is drawn
      cfg.pin_sclk = PIN_TFT_SCLK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_dc = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
#endif
    {
      auto cfg = _panel.config();
#if SCREEN_PARALLEL
      cfg.pin_cs = -1;                // LCD_CS tied to GND: the screen is alone on its bus
#else
      cfg.pin_cs = PIN_TFT_CS;
#endif
      cfg.pin_rst = PIN_TFT_RST;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = !SCREEN_PARALLEL;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = !SCREEN_PARALLEL;   // on SPI the touch chip shares the bus
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_TFT_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#if TOUCH_ENABLED
    {
      auto cfg = _touch.config();
      cfg.x_min = 300; cfg.x_max = 3900;   // rough calibration; only "touched or not" matters for now
      cfg.y_min = 200; cfg.y_max = 3800;
      cfg.pin_int = PIN_TOUCH_IRQ;
      cfg.bus_shared = !SCREEN_PARALLEL;
      cfg.offset_rotation = 0;
      cfg.spi_host = SPI2_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = PIN_TOUCH_SCLK;
      cfg.pin_mosi = PIN_TOUCH_MOSI;
      cfg.pin_miso = PIN_TOUCH_MISO;
      cfg.pin_cs = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
#endif
    setPanel(&_panel);
  }
};
