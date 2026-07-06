#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "board.h"

// Panel config matches Elecrow 1.28" wiki (GC9A01, 240x240).
class PanelDisplay : public lgfx::LGFX_Device {
 public:
  PanelDisplay() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = board::kDisplaySclk;
      cfg.pin_mosi = board::kDisplayMosi;
      cfg.pin_miso = -1;
      cfg.pin_dc = board::kDisplayDc;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = board::kDisplayCs;
      cfg.pin_rst = board::kDisplayRst;
      cfg.pin_busy = -1;
      cfg.memory_width = board::kWidth;
      cfg.memory_height = board::kHeight;
      cfg.panel_width = board::kWidth;
      cfg.panel_height = board::kHeight;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    setPanel(&_panel);
  }

 private:
  lgfx::Bus_SPI _bus;
  lgfx::Panel_GC9A01 _panel;
};
