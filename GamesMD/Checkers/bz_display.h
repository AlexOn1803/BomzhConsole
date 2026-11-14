#pragma once
// LovyanGFX setup for the 2.0" ST7789V 240x320 SPI module, sharing the bus with the SD card.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "bz_config.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel;
  lgfx::Bus_SPI bus;

 public:
  LGFX() {
    {
      auto c = bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = TFT_SPI_FREQ;
      c.freq_read = 16000000;
      c.pin_sclk = PIN_SPI_SCK;
      c.pin_mosi = PIN_SPI_MOSI;
      c.pin_miso = PIN_SPI_MISO;   // the SD card needs it; the display ignores it
      c.pin_dc = PIN_TFT_DC;
      c.spi_3wire = false;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      bus.config(c);
      panel.setBus(&bus);
    }
    {
      auto c = panel.config();
      c.pin_cs = PIN_TFT_CS;
      c.pin_rst = PIN_TFT_RST;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 320;
      c.offset_x = 0;
      c.offset_y = 0;
      c.readable = false;
      c.invert = TFT_INVERT;
      c.rgb_order = false;
      c.bus_shared = true;         // release the bus after every frame so SD can use it
      panel.config(c);
    }
    setPanel(&panel);
  }
};
