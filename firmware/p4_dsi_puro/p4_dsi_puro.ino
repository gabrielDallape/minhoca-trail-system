/*
 * ESP32-P4 de 7" (EK79007) usando SO a API da Espressif - sem LovyanGFX.
 *
 * POR QUE ESTE SKETCH EXISTE
 * A tela de 7" fica preta com a LovyanGFX, e TODAS as etapas reportam sucesso.
 * Ja foi medido que o desenho chega no framebuffer (readPixel devolve 0xF800 para
 * vermelho), entao o problema e a saida de video. Mas o demo de fabrica, que usa
 * a API da Espressif, funcionava 100% na mesma placa.
 *
 * Enquanto os dois caminhos diferirem, eu fico trocando parametro no escuro. Este
 * sketch reproduz EXATAMENTE o caminho do demo - mesmas chamadas, mesma ordem,
 * mesmos valores do BSP oficial - e escreve direto no framebuffer. O resultado
 * separa duas coisas que eu nao consigo separar de outro jeito:
 *
 *   aparece imagem  -> o hardware esta bom e o erro esta no meu uso da LovyanGFX
 *   continua preta  -> o problema e pino, energia ou a placa nao e o que penso
 *
 * Valores conferidos em esp-bsp (bsp/esp32_p4_function_ev_board) e no componente
 * esp_lcd_ek79007 do esp-iot-solution.
 *
 *   .\tools\build.ps1 firmware\p4_dsi_puro -Board p4 -Upload -Port COM9
 */
#include <Arduino.h>
#include <Wire.h>
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

// PINOS DA WAVESHARE ESP32-P4-WIFI6-Touch-LCD-7B.
//
// NAO sao os do BSP da Espressif. Eu usei 26/27 (EV board da Espressif) e passei
// horas com a tela preta. A pista estava no log do demo de fabrica desde o
// comeco: "ledc: GPIO 32 is not usable" - o demo tentava o backlight no 32, que
// nao existe em nenhum BSP da Espressif. Fonte:
// Waveshare-ESP32-components/bsp/esp32_p4_wifi6_touch_lcd_7b/include/bsp/...h
//
// Nenhuma placa Waveshare com EK79007 usa 26/27; as que usam 26/27 tem painel
// JD9365. Sem ninguem dirigir o RST, o painel pode ficar preso em reset - e todas
// as chamadas continuam devolvendo ESP_OK.
#define LCD_W      1024
#define LCD_H       600
#define LCD_RST      33      // Waveshare 7B (era 27 = EV board da Espressif)
#define LCD_BL       32      // Waveshare 7B (era 26)
#define DSI_LANES     2      // BSP_LCD_MIPI_DSI_LANE_NUM
// 900 e o valor do macro OFICIAL do painel (EK79007_PANEL_BUS_DSI_2CH_CONFIG).
// O BSP da placa usa 1000; os dois existem em codigo validado, mas quando se esta
// perdido o certo e copiar o perfil do fabricante do painel, nao o da placa.
#define DSI_MBPS    900
#define PHY_LDO_CH    3      // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN
#define PHY_LDO_MV 2500      // BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV
#define DPI_MHZ      52      // EK79007_1024_600_PANEL_60HZ_CONFIG

// Agora COM reset, mas no pino certo (33).
#define MTS_USAR_RESET 1

static esp_lcd_dsi_bus_handle_t  bus  = nullptr;
static esp_lcd_panel_io_handle_t io   = nullptr;
static esp_lcd_panel_handle_t    disp = nullptr;

#define CHECA(x) do { esp_err_t _e = (x); \
  Serial.printf("  %-46s %s\n", #x, _e == ESP_OK ? "OK" : esp_err_to_name(_e)); \
  if (_e != ESP_OK) { Serial.println("  -> parou aqui"); return; } } while (0)

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== MTS | DSI puro (sem LovyanGFX) na tela de 7 polegadas ===");

  // 0. BACKLIGHT. A versao anterior deste sketch nao acendia NENHUM backlight -
  // ou seja, estava garantidamente preta independentemente do DSI, e eu estava
  // depurando video no escuro. Literalmente.
  // O BACKLIGHT DESTA PLACA E INVERTIDO. O BSP da Waveshare configura o LEDC com
  // .flags = { .output_invert = 1 } - a Espressif nao tem esse flag. Entao:
  //        nivel BAIXO  = ACESO        nivel ALTO = apagado
  // Eu escrevi HIGH na versao anterior deste sketch, ou seja, APAGUEI a luz e
  // fiquei depurando o video atras dela.
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);
  Serial.printf("  backlight ACESO no GPIO %d (nivel BAIXO - e invertido)\n", LCD_BL);

  // 0b. Varredura do I2C do toque. Se 0x45 responder, esta placa e a de modulo
  // de 7" DESTACAVEL, que exige uma habilitacao por I2C antes do painel (o mesmo
  // chip da tela de 5"). Se nao responder, o painel e integrado e sobe sozinho.
  Wire.begin(7, 8, 100000);
  Serial.print("  I2C em 7/8:");
  int achou = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); achou++; }
  }
  Serial.println(achou ? "" : " (nada)");

  // 1. energia do PHY do DSI. Sem isto nada no barramento funciona.
  esp_ldo_channel_handle_t phy = nullptr;
  esp_ldo_channel_config_t ldo = {};
  ldo.chan_id    = PHY_LDO_CH;
  ldo.voltage_mv = PHY_LDO_MV;
  CHECA(esp_ldo_acquire_channel(&ldo, &phy));

  // 2. barramento DSI
  esp_lcd_dsi_bus_config_t bcfg = {};
  bcfg.bus_id              = 0;
  bcfg.num_data_lanes      = DSI_LANES;
  // phy_clk_src FICA ZERO, de proposito.
  //
  // O macro MIPI_DSI_PHY_CLK_SRC_DEFAULT esta HARD-WIRED na fonte de clock
  // anterior a revisao 3.0 do silicio (PLL_F20M) - ver clk_tree_defs.h. O driver
  // so escolhe a fonte certa PARA A REVISAO DO CHIP quando este campo e zero
  // (esp_lcd_mipi_dsi_bus.c). O macro oficial do EK79007 deixa zero, e um
  // mantenedor do esp_lcd recomenda exatamente isso na issue esp-idf#18521.
  //
  // Com a fonte errada, a PLL do PHY e programada para uma referencia que nao e a
  // real: ela trava, os comandos em modo LP passam, tudo devolve ESP_OK - e o
  // video em alta velocidade nunca sai.
  bcfg.phy_clk_src         = (mipi_dsi_phy_clock_source_t)0;
  bcfg.lane_bit_rate_mbps  = DSI_MBPS;
  CHECA(esp_lcd_new_dsi_bus(&bcfg, &bus));

  // 3. canal de comando (DBI)
  esp_lcd_dbi_io_config_t dbi = {};
  dbi.virtual_channel  = 0;
  dbi.lcd_cmd_bits     = 8;
  dbi.lcd_param_bits   = 8;
  CHECA(esp_lcd_new_panel_io_dbi(bus, &dbi, &io));

  // 4. painel DPI (o video). Valores do EK79007_1024_600_PANEL_60HZ_CONFIG.
  esp_lcd_dpi_panel_config_t dpi = {};
  dpi.virtual_channel      = 0;
  dpi.dpi_clk_src          = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi.dpi_clock_freq_mhz   = DPI_MHZ;
  dpi.pixel_format         = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi.num_fbs              = 1;
  dpi.video_timing.h_size            = LCD_W;
  dpi.video_timing.v_size            = LCD_H;
  dpi.video_timing.hsync_pulse_width = 10;
  dpi.video_timing.hsync_back_porch  = 160;
  dpi.video_timing.hsync_front_porch = 160;
  dpi.video_timing.vsync_pulse_width = 1;
  dpi.video_timing.vsync_back_porch  = 23;
  dpi.video_timing.vsync_front_porch = 12;
  dpi.flags.use_dma2d = true;
  CHECA(esp_lcd_new_panel_dpi(bus, &dpi, &disp));

  // 5. RESET - ou a AUSENCIA dele.
  //
  // POR QUE ESTA OPCAO EXISTE: o demo de fabrica desta placa tentou o backlight
  // no GPIO 32, e nenhum dos dois BSP oficiais usa 32 (usam 26 e 23). Ou seja,
  // esta placa NAO e a EV board da Espressif - e uma variante com pinagem
  // propria, e eu venho aplicando a pinagem de outra. Se o reset dela nao for o
  // GPIO 27, eu nao estou resetando o painel: estou mexendo num pino qualquer,
  // que pode inclusive estar ligado a outra coisa.
  //
  // Muitas dessas placas nem tem reset fiado (BSP_LCD_RST = NC) e dependem do
  // reset de energizacao. Nao tocar no pino e o teste mais barato e mais seguro:
  // se a imagem aparecer, o meu "reset" era o problema.
  // RESET nos DOIS candidatos, porque as fontes divergem: o header do BSP da
  // Waveshare diz GPIO 33, o esquematico da mesma placa diz 27. Pulsar os dois
  // custa 60 ms e resolve a divergencia sem mais uma rodada de gravacao. Se um
  // deles estiver ligado a outra coisa, um pulso curto em nivel baixo nao faz
  // estrago - sao os dois pinos de reset de painel em placas irmas.
  // RESET no GPIO 33, confirmado NO ESQUEMATICO da placa (rede RESET_LCD, via R42
  // de 0 ohm). NAO no 27: naquela placa o 26 e o 27 sao TXD e RXD do RS485.
  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);  delay(10);
  digitalWrite(LCD_RST, HIGH); delay(20);
  Serial.printf("  reset pulsado no GPIO %d (ativo-baixo)\n", LCD_RST);

  // 6. sequencia de inicializacao do EK79007, do driver oficial.
  //    0xB2 = PAD_CONTROL; 0x10 = 2 lanes.
  struct Cmd { uint8_t cmd; uint8_t dado; int nd; int esperaMs; };
  static const Cmd SEQ[] = {
    { 0xB2, 0x10, 1,   0 },
    { 0x80, 0x8B, 1,   0 },
    { 0x81, 0x78, 1,   0 },
    { 0x82, 0x84, 1,   0 },
    { 0x83, 0x88, 1,   0 },
    { 0x84, 0xA8, 1,   0 },
    { 0x85, 0xE3, 1,   0 },
    { 0x86, 0x88, 1,   0 },
    { 0x11, 0x00, 0, 120 },     // SLEEP OUT
  };
  for (auto& c : SEQ) {
    esp_err_t e = esp_lcd_panel_io_tx_param(io, c.cmd, c.nd ? &c.dado : nullptr, c.nd);
    Serial.printf("  cmd %02X -> %s\n", c.cmd, e == ESP_OK ? "OK" : esp_err_to_name(e));
    if (c.esperaMs) delay(c.esperaMs);
  }

  // 7. liga o video
  CHECA(esp_lcd_panel_init(disp));

  // 7b. PADRAO DE BARRAS GERADO PELO HARDWARE.
  //
  // E o melhor teste que existe para este problema, e eu nao o conhecia: o host
  // DSI gera as barras sozinho, sem ler framebuffer, PSRAM, cache ou DMA. Ele
  // separa de uma vez o que eu vinha separando por eliminacao:
  //
  //   barras aparecem  -> link DSI, temporizacao, energia e painel estao BONS,
  //                       e o problema esta no caminho do framebuffer
  //   continua preta   -> e backlight, reset ou energia do painel
  Serial.println("\n  >>> BARRAS VERTICAIS geradas pelo HARDWARE por 6 s <<<");
  CHECA(esp_lcd_dpi_panel_set_pattern(disp, MIPI_DSI_PATTERN_BAR_VERTICAL));
  delay(6000);
  Serial.println("  >>> agora BARRAS HORIZONTAIS por 6 s <<<");
  CHECA(esp_lcd_dpi_panel_set_pattern(disp, MIPI_DSI_PATTERN_BAR_HORIZONTAL));
  delay(6000);
  Serial.println("  >>> desligando o padrao e voltando ao framebuffer <<<");
  CHECA(esp_lcd_dpi_panel_set_pattern(disp, MIPI_DSI_PATTERN_NONE));

  // 8. escreve direto no framebuffer que o DSI varre
  void* fb = nullptr;
  CHECA(esp_lcd_dpi_panel_get_frame_buffer(disp, 1, &fb));
  Serial.printf("  framebuffer em %p\n", fb);

  uint16_t* p = (uint16_t*)fb;
  const uint16_t CORES[] = { 0xF800, 0x07E0, 0x001F, 0xFFE0, 0xFFFF, 0x8410 };
  for (int y = 0; y < LCD_H; y++) {
    uint16_t c = CORES[(y * 6) / LCD_H];
    for (int x = 0; x < LCD_W; x++) p[y * LCD_W + x] = c;
  }
  Serial.println("  framebuffer pintado com 6 faixas");

  // O draw_bitmap faz o cache da PSRAM ser escrito de volta. Sem isso o DMA do
  // DSI pode ler memoria velha - e a tela fica preta com o framebuffer "certo"
  // do ponto de vista da CPU. E uma das hipoteses que este teste separa.
  CHECA(esp_lcd_panel_draw_bitmap(disp, 0, 0, LCD_W, LCD_H, fb));
  Serial.println("\nSe apareceram 6 faixas coloridas, o hardware esta bom.");
}

void loop() { delay(1000); }
