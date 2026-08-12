/*
 * MTS - Minhoca Trail System | tela ESP32-P4
 *
 * ESTAGIO 1: provar o painel. Nada de interface ainda - so acender, pintar e
 * escrever, para separar "o painel esta configurado certo" de "a UI tem bug".
 * Se o ILI9881C nao casar com os timings do LGFX_P4_LCD5.h, e aqui que aparece.
 *
 * O log sai pela CH343 (COM), entao da para diagnosticar mesmo com a tela preta.
 *
 *   .\tools\build.ps1 firmware\mts_p4 -Board p4 -Upload -Port COM8
 */
// O painel da Waveshare NAO responde a leitura por DBI: o readParams(0xF4) da
// deteccao de ID trava para sempre (medido - o rastro do init para exatamente
// nele). Entao pulamos a deteccao e assumimos ILI9881C, que e o controlador
// classico de 720x1280 e o mesmo que o M5Tab5 usa com este SoC.
#define MTS_SKIP_PANEL_ID 1
// Sobe o DPI com um Panel_DSI cru (que nao trava) e so DEPOIS manda a sequencia
// do HX8394. E a ordem do driver oficial: quando panel_hx8394_init roda, o painel
// DPI ja existe. Ver o cabecalho do Panel_HX8394.h.
#define MTS_PANEL_BARE 1
#include "LGFX_P4_LCD5.h"

LGFX_P4 tft;

static void barra(int y, int h, uint16_t cor, const char* nome)
{
  tft.fillRect(0, y, tft.width(), h, cor);
}

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) delay(10);
  delay(300);

  Serial.println("\n=== MTS | teste de painel P4 ===");

  // A PSRAM e pre-requisito: o framebuffer de 1,8 MB nao cabe na RAM interna.
  size_t ps = ESP.getPsramSize();
  Serial.printf("PSRAM: %u bytes (%.1f MB)\n", (unsigned)ps, ps / 1048576.0);
  if (ps == 0) {
    Serial.println("ERRO: PSRAM em 0. Falta PSRAM=enabled no FQBN - a tela NAO vai subir.");
  }

  Serial.println("chamando tft.init()...");
  Serial.flush();
  uint32_t ti = millis();
  bool ok = tft.init();
  Serial.printf("tft.init(): %s (%lu ms)\n", ok ? "OK" : "FALHOU", (unsigned long)(millis() - ti));
  if (!ok) {
    Serial.println("Painel nao inicializou. Suspeitos, em ordem:");
    Serial.println("  1) timings do DPI no LGFX_P4_LCD5.h (porches/dpi_freq_mhz)");
    Serial.println("  2) lane_mbps ou lane_num do Bus_DSI");
    Serial.println("  3) controlador do painel nao e ILI9881C");
    while (true) delay(1000);
  }

  Serial.printf("painel detectado: %s (%d x %d)\n", tft.panelName, tft.panelW, tft.panelH);
  Serial.printf("resolucao util : %d x %d\n", tft.width(), tft.height());
  Serial.printf("heap livre: %u | psram livre: %u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());

  Serial.println("enviando sequencia do HX8394 com o DPI ja rodando...");
  Serial.flush();
  bool seq = tft.enviarInitHX8394();
  Serial.printf("sequencia do painel: %s\n", seq ? "COMPLETA" : "FALHOU");

  tft.setBrightness(255);
  Serial.println("backlight ligado");

  // ---- 1. barras de cor: prova a ordem dos canais e a ausencia de tearing
  int h = tft.height() / 6;
  barra(0 * h, h, TFT_RED,     "vermelho");
  barra(1 * h, h, TFT_GREEN,   "verde");
  barra(2 * h, h, TFT_BLUE,    "azul");
  barra(3 * h, h, TFT_WHITE,   "branco");
  barra(4 * h, h, TFT_BLACK,   "preto");
  barra(5 * h, tft.height() - 5 * h, TFT_ORANGE, "laranja");
  Serial.println("barras desenhadas - confira se as cores batem com os nomes");
  delay(2500);

  // ---- 2. moldura + cantos: prova que a area toda e enderecavel
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_ORANGE);
  tft.drawRect(4, 4, tft.width() - 8, tft.height() - 8, 0x8410);
  const int m = 40;
  tft.fillCircle(m, m, 12, TFT_RED);
  tft.fillCircle(tft.width() - m, m, 12, TFT_GREEN);
  tft.fillCircle(m, tft.height() - m, 12, TFT_BLUE);
  tft.fillCircle(tft.width() - m, tft.height() - m, 12, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(middle_center);
  tft.setTextSize(3);
  tft.drawString("MTS", tft.width() / 2, tft.height() / 2 - 60);
  tft.setTextSize(2);
  tft.drawString("Minhoca Trail System", tft.width() / 2, tft.height() / 2);
  tft.setTextSize(1);
  char buf[64];
  snprintf(buf, sizeof(buf), "%d x %d  |  ESP32-P4", tft.width(), tft.height());
  tft.drawString(buf, tft.width() / 2, tft.height() / 2 + 40);

  Serial.println("texto desenhado. Se voce esta lendo MTS na tela, o painel esta OK.");
  Serial.println("Confira: os 4 circulos dos cantos aparecem inteiros?");
}

void loop()
{
  // Pisca um ponto para provar que o refresh continua vivo (e que nao travou
  // depois do primeiro quadro, sintoma tipico de framebuffer mal alocado).
  static bool on = false;
  static uint32_t t = 0;
  if (millis() - t > 500) {
    t = millis();
    on = !on;
    tft.fillCircle(tft.width() / 2, tft.height() - 80, 8, on ? TFT_ORANGE : TFT_BLACK);
  }
  delay(20);
}
