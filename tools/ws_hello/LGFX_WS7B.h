#pragma once
// LGFX para Waveshare ESP32-S3-Touch-LCD-7B (1024x600 RGB).
// IMPORTANTE: NAO usa Light_CH422G do LovyanGFX (protocolo incompativel com o
// expansor do 7B). O backlight/reset/power sao ligados a mao via I2C no .ino
// (ver ioExt()). Aqui fica so o barramento RGB + painel. Touch tratado a parte.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

class LGFX : public lgfx::LGFX_Device
{
public:
  lgfx::Bus_RGB   _bus_instance;
  lgfx::Panel_RGB _panel_instance;

  LGFX(void)
  {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width  = 1024;
      cfg.memory_height = 600;
      cfg.panel_width   = 1024;
      cfg.panel_height  = 600;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;
      _panel_instance.config_detail(cfg);
    }
    {
      auto cfg = _bus_instance.config();
      cfg.panel   = &_panel_instance;
      cfg.pin_d0  = GPIO_NUM_14;  // B0
      cfg.pin_d1  = GPIO_NUM_38;  // B1
      cfg.pin_d2  = GPIO_NUM_18;  // B2
      cfg.pin_d3  = GPIO_NUM_17;  // B3
      cfg.pin_d4  = GPIO_NUM_10;  // B4
      cfg.pin_d5  = GPIO_NUM_39;  // G0
      cfg.pin_d6  = GPIO_NUM_0;   // G1
      cfg.pin_d7  = GPIO_NUM_45;  // G2
      cfg.pin_d8  = GPIO_NUM_48;  // G3
      cfg.pin_d9  = GPIO_NUM_47;  // G4
      cfg.pin_d10 = GPIO_NUM_21;  // G5
      cfg.pin_d11 = GPIO_NUM_1;   // R0
      cfg.pin_d12 = GPIO_NUM_2;   // R1
      cfg.pin_d13 = GPIO_NUM_42;  // R2
      cfg.pin_d14 = GPIO_NUM_41;  // R3
      cfg.pin_d15 = GPIO_NUM_40;  // R4

      cfg.pin_henable = GPIO_NUM_5;
      cfg.pin_vsync   = GPIO_NUM_3;
      cfg.pin_hsync   = GPIO_NUM_46;
      cfg.pin_pclk    = GPIO_NUM_7;
      cfg.freq_write  = 16000000;   // 16 MHz: Bus_RGB nao tem bounce buffer; 30MHz falta banda PSRAM

      // ATENCAO: no LovyanGFX 1.2.24 estes campos sao int8_t (max 127).
      // Os timings oficiais do 7B (hpw=162, hbp=152) ESTOURAM int8_t -> tela branca.
      // Painel usa DE (henable) -> porches de sync sao tolerantes -> valores <=127.
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 48;
      cfg.hsync_pulse_width = 20;    // era 162 (nao cabe em int8_t)
      cfg.hsync_back_porch  = 120;   // era 152 (nao cabe em int8_t)
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 3;
      cfg.vsync_pulse_width = 45;
      cfg.vsync_back_porch  = 13;
      cfg.pclk_idle_high    = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);
    setPanel(&_panel_instance);
  }
};
