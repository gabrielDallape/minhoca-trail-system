// LGFX para a ESP32-P4 Function EV Board com painel de 7" (EK79007, 1024x600).
//
// E UMA PLACA DIFERENTE da Waveshare de 5" (ver LGFX_P4_LCD5.h). O que muda, e
// tudo foi conferido no BSP oficial da Espressif (esp-bsp,
// bsp/esp32_p4_function_ev_board) e no componente esp_lcd_ek79007 - nao chutado:
//
//   painel ........ EK79007 1024x600 PAISAGEM, contra HX8394 720x1280 retrato
//   reset ......... ATIVO-BAIXO (o BSP nao seta reset_active_high). Na Waveshare
//                   de 5" e ativo-ALTO, e essa inversao custou uma tarde inteira.
//   energia ....... NAO tem o chip I2C 0x45 da Waveshare. O painel ja sobe.
//   backlight ..... um pino so (GPIO 26). A de 5" tem enable + PWM.
//   toque ......... GT911 sem pino de reset nem de interrupcao fiados
//   cartao SD ..... alimentado por LDO INTERNO canal 4, nao por GPIO, e em pinos
//                   proprios (43/44 e 39-42). Ver mts_p4.ino.
//
// POR QUE PAISAGEM IMPORTA. MEDIDO na tela de 5": pushSprite com o painel girado
// custa 719 ms; sem girar, 73 ms. Aqui o painel JA NASCE em paisagem, entao nao ha
// rotacao nenhuma no caminho - some a parte mais cara do quadro. E sao 614.400
// pixels contra 921.600, 33% a menos de tudo.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
  #error "Este header e so para ESP32-P4. Use -Board p4."
#endif

#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include "lgfx/v1/platforms/esp32p4/Panel_EK79007.hpp"

// --------------------------------------------------------------- pinos
// WAVESHARE ESP32-P4-WIFI6-Touch-LCD-7B. Confirmados no esquematico oficial da
// placa (redes BL_CTRL via R42/R48) e no BSP da Waveshare
// (Waveshare-ESP32-components/bsp/esp32_p4_wifi6_touch_lcd_7b).
//
// A ARMADILHA QUE ME CUSTOU A TARDE: o log de boot desta placa diz "ESP32_P4_EV",
// porque a Waveshare COPIOU o BSP da Espressif e manteve a etiqueta de log,
// trocando so os pinos. Eu li "EV board", peguei a pinagem da Espressif (26/27) e
// fiquei depurando video. Nesta placa o 26 e o 27 sao TXD e RXD do RS485 - nao
// tem relacao nenhuma com a tela.
#define P4_LCD_BL       32   // BL_CTRL. ATENCAO: INVERTIDO, ver abaixo
#define P4_LCD_RST      33   // RESET_LCD, ativo-baixo
#define P4_TP_SDA        7   // ESP_I2C_SDA
#define P4_TP_SCL        8   // ESP_I2C_SCL
#define P4_TP_RST       23   // RESET_TP (o BSP do 7B declara NC, mas o pino existe)

// O BACKLIGHT E INVERTIDO. O BSP da Waveshare configura o LEDC com
// .flags = { .output_invert = 1 }; o da Espressif nao tem esse flag. Portanto:
//        nivel BAIXO = ACESO        nivel ALTO = apagado
// Eu escrevi HIGH achando que estava acendendo, apaguei a luz, e passei horas
// depurando o video atras dela. Isto nao esta em documentacao da Espressif
// nenhuma - so no BSP da Waveshare.
#define P4_LCD_BL_INVERTIDO true

// --------------------------------------------------- painel, do esp_lcd_ek79007
// EK79007_1024_600_PANEL_60HZ_CONFIG. Nao arredonde: o refresh sai de
//   (dpi * 1e6) / (h + hpw + hbp + hfp) / (v + vpw + vbp + vfp)
//   = 52e6 / 1354 / 636 = 60,4 Hz
#define EK7_W            1024
#define EK7_H             600
#define EK7_DPI_MHZ        52
#define EK7_HSYNC_PW       10
#define EK7_HSYNC_BP      160
#define EK7_HSYNC_FP      160
#define EK7_VSYNC_PW        1
#define EK7_VSYNC_BP       23
#define EK7_VSYNC_FP       12
#define EK7_LANE_NUM        2      // BSP_LCD_MIPI_DSI_LANE_NUM
// 900 e o valor do macro do FABRICANTE DO PAINEL
// (EK79007_PANEL_BUS_DSI_2CH_CONFIG). O BSP da placa usa 1000; os dois aparecem
// em codigo validado, mas o perfil do painel e o que manda.
#define EK7_LANE_MBPS     900

#ifndef MTS_LOG
  #define MTS_LOG(fmt, ...) do { Serial.printf("[lgfx] " fmt "\n", ##__VA_ARGS__); Serial.flush(); } while(0)
#endif

class LGFX_P4 : public lgfx::LGFX_Device
{
  lgfx::Bus_DSI        _dsi;
  lgfx::Light_PWM      _luz;
  lgfx::Panel_EK79007* _pnl = nullptr;
  lgfx::Touch_GT911*   _tp  = nullptr;

public:
  const char* panelName = "EK79007";
  int panelW = EK7_W, panelH = EK7_H;

  LGFX_P4() {}

  bool init_impl(bool use_reset, bool use_clear) override
  {
    // Deixa o pino de reset em repouso (alto) antes de qualquer coisa, mas NAO
    // pulsa ainda - ver o passo 2.
    lgfx::pinMode(P4_LCD_RST, lgfx::pin_mode_t::output);
    lgfx::gpio_hi(P4_LCD_RST);

    {
      auto cfg = _dsi.config();
      cfg.bus_id    = 0;
      cfg.lane_num  = EK7_LANE_NUM;
      cfg.lane_mbps = EK7_LANE_MBPS;
      // O PHY do DSI tem alimentacao propria (VDD_MIPI_DPHY), servida por um LDO
      // interno - canal 3 a 2500 mV, igual na de 5". BSP_MIPI_DSI_PHY_PWR_LDO_*.
      cfg.ldo_chan_id    = 3;
      cfg.ldo_voltage_mv = 2500;
      _dsi.config(cfg);
    }
    MTS_LOG("2. Bus_DSI.init() (%d lanes @ %d Mbps, LDO ch3 2500 mV)",
            EK7_LANE_NUM, EK7_LANE_MBPS);
    if (!_dsi.init()) { MTS_LOG("   FALHOU"); return false; }
    MTS_LOG("   ok");
    lgfx::delay(80);

    // RESET SO AGORA, com o PHY do DSI ja energizado e o barramento de pe.
    //
    // A ORDEM IMPORTA e foi conferida no BSP oficial: ele adquire o LDO do PHY,
    // cria o barramento e o painel, e SO ENTAO chama esp_lcd_panel_reset. O
    // EK79007 amostra o estado das lanes ao sair do reset; se isso acontecer com
    // o PHY desenergizado, ele acorda configurado errado - e o sintoma e
    // exatamente o que se via aqui: tudo reporta sucesso, o framebuffer recebe os
    // pixels (conferido com readPixel: vermelho volta 0xF800), e a tela fica
    // preta com o backlight aceso, porque o painel nao esta escutando o video.
    //
    // Na tela de 5" (HX8394) resetar antes funcionava, e foi de la que eu copiei
    // a ordem errada.
    MTS_LOG("2b. reset do painel no GPIO %d (ativo-baixo), com o DSI ja de pe",
            P4_LCD_RST);
    lgfx::gpio_lo(P4_LCD_RST); lgfx::delay(20);    // assert
    lgfx::gpio_hi(P4_LCD_RST); lgfx::delay(120);   // release

    // Sem deteccao de ID: a placa e conhecida e o painel e soldado nela. Na de 5"
    // a deteccao existe porque o modelo nao estava em esquematico nenhum.
    _pnl = new lgfx::Panel_EK79007();
    _tp  = new lgfx::Touch_GT911();
    {
      auto d = _pnl->config_detail();
      d.dpi_freq_mhz      = EK7_DPI_MHZ;
      d.hsync_pulse_width = EK7_HSYNC_PW;
      d.hsync_back_porch  = EK7_HSYNC_BP;
      d.hsync_front_porch = EK7_HSYNC_FP;
      d.vsync_pulse_width = EK7_VSYNC_PW;
      d.vsync_back_porch  = EK7_VSYNC_BP;
      d.vsync_front_porch = EK7_VSYNC_FP;
      _pnl->config_detail(d);
    }
    MTS_LOG("3. painel: %s (%dx%d, paisagem nativa)", panelName, panelW, panelH);

    setPanel(_pnl);
    {
      auto cfg = _pnl->config();
      cfg.memory_width  = panelW; cfg.memory_height = panelH;
      cfg.panel_width   = panelW; cfg.panel_height  = panelH;
      // GIRO DE 180 DA CAIXINHA (MTS_TELA_180 em hardware.h). Este e o UNICO
      // lugar que gira este painel de verdade: setRotation() em tempo de
      // execucao e ignorado em silencio pelo caminho DSI (testado em
      // 2026-08-25 - compilou, gravou, nada virou). Ja o offset daqui vira a
      // imagem comprovadamente: uma versao antiga pos 2 aqui POR ENGANO
      // tentando consertar o toque, e a imagem ficou de cabeca para baixo.
      // O engano de la e o recurso de ca.
      //
      // O toque gira JUNTO com este offset (a LovyanGFX soma os dois em
      // convertRawXY), entao o offset_rotation=2 DO TOQUE logo abaixo continua
      // certo: ele corrige os 180 de fabrica entre o sensor e o vidro, que
      // nao mudam por a placa estar montada de ponta-cabeca.
      cfg.offset_x = 0; cfg.offset_y = 0;
#if defined(MTS_TELA_180) && MTS_TELA_180
      cfg.offset_rotation = 2;
#else
      cfg.offset_rotation = 0;
#endif
      cfg.readable = true; cfg.rgb_order = true; cfg.bus_shared = false;
      cfg.pin_cs  = GPIO_NUM_NC;
      cfg.pin_rst = GPIO_NUM_NC;    // ja resetamos acima, com a polaridade certa
      _pnl->config(cfg);
      _pnl->setBus(&_dsi);
    }
    {
      auto cfg = _tp->config();
      cfg.pin_sda = P4_TP_SDA; cfg.pin_scl = P4_TP_SCL;
      cfg.pin_rst = -1; cfg.pin_int = -1;   // nao fiados nesta placa
      cfg.freq = 400000; cfg.i2c_port = 1; cfg.bus_shared = false;
      cfg.x_min = 0; cfg.x_max = panelW - 1;
      cfg.y_min = 0; cfg.y_max = panelH - 1;
      // O TOUCH ESTA MONTADO 180 GRAUS FORA DO DISPLAY nesta placa. Sintoma:
      // tocar no canto inferior direito registra no superior esquerdo.
      //
      // O BSP da Waveshare diz a mesma coisa por outro caminho - ele usa o painel
      // em ROTATE_180 e AINDA POR CIMA espelha o toque nos dois eixos
      // (mirror_x=1, mirror_y=1). Espelhar em x e y e girar 180: ou seja, o toque
      // leva 180 a MAIS que a imagem.
      //
      // Este offset e do TOQUE, e a LovyanGFX o SOMA a rotacao do painel
      // (Panel_Device::convertRawXY: tr = _internal_rotation + offset do toque).
      // Por isso ele corrige o dedo sem virar o desenho.
      cfg.offset_rotation = 2;
      _tp->config(cfg);
      _pnl->setTouch(_tp);
    }
    {
      auto cfg = _luz.config();
      cfg.pin_bl = P4_LCD_BL;
      cfg.freq = 5000;
      cfg.pwm_channel = 7;
      cfg.offset = 0;
      cfg.invert = P4_LCD_BL_INVERTIDO;   // nesta placa, BAIXO = aceso
      _luz.config(cfg);
      _pnl->setLight(&_luz);
    }

    MTS_LOG("4. init_impl() - framebuffer de %d KB na PSRAM",
            (panelW * panelH * 2) / 1024);
    bool r = lgfx::LGFX_Device::init_impl(use_reset, use_clear);
    MTS_LOG("   %s", r ? "ok" : "FALHOU");
    if (!r) return false;

    // NAO MANDE 0x29 (DISPON) AQUI.
    //
    // Eu tinha posto um DISPON neste ponto, com um comentario afirmando que o
    // driver oficial da Espressif mandava e a LovyanGFX nao. ISSO ERA FALSO: o
    // esp_lcd_ek79007.c nao manda 0x29 em lugar nenhum - a sequencia dele termina
    // em 0x11 (SLEEP OUT) e o video comeca no init do DPI. Eu afirmei com
    // confianca a partir de uma comparacao incompleta, e aquilo nao consertou
    // nada. A tela estava preta por outro motivo: o backlight, que e invertido
    // nesta placa e que eu tinha apagado escrevendo HIGH.
    //
    // Fica registrado para ninguem "reintroduzir a correcao".

    // NUNCA leia o painel por DBI nesta pilha. Alem de o painel nao responder
    // (a FIFO enche e o HAL gira num while() sem timeout), o
    // mipi_dsi_hal_host_gen_read_short_packet DESLIGA o modo de video e NUNCA
    // religa. Uma unica leitura mata a imagem de vez, com tudo devolvendo ESP_OK.
    return true;
  }
};
