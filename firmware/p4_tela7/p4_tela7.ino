/*
 * Primeira luz da ESP32-P4 Function EV Board de 7" (EK79007, 1024x600).
 *
 * POR QUE UM SKETCH SO PARA ISTO, em vez de ja gravar o MTS: o MTS tem 1280x720
 * cravado em 28 lugares e um botao que cairia fora de uma tela de 600 px de
 * altura. Se eu gravasse tudo de uma vez e a tela ficasse preta, haveria duas
 * hipoteses misturadas - painel errado ou layout errado - e nenhuma forma rapida
 * de separar. Este sketch responde SO a primeira.
 *
 * Foi assim que a tela de 5" subiu (p4_hello), e foi o que pegou o PSRAM=0 e a
 * polaridade invertida do reset.
 *
 *   .\tools\build.ps1 firmware\p4_tela7 -Board p4 -Upload -Port COM9
 */
#include "../mts_p4/LGFX_P4_LCD7.h"
#include "SD_MMC.h"

LGFX_P4 tft;

// Pinos do cartao nesta placa - DIFERENTES da de 5", conferidos no BSP oficial.
// E a alimentacao do slot nao e um GPIO: e um LDO interno, canal 4.
#define SD_CLK 43
#define SD_CMD 44
#define SD_D0  39
#define SD_D1  40
#define SD_D2  41
#define SD_D3  42
#define SD_LDO_CANAL 4
// reset do toque nesta placa (RESET_TP no esquematico)
#define TP_RST 23

// Recebe onde desenhar (tela ou sprite) em vez de escrever direto na tela.
//
// A primeira versao desenhava sempre na tela E DEPOIS empurrava um sprite azul-
// marinho por cima para medir o pushSprite. Resultado: tela quase preta, e eu
// passei a caçar bug de painel num painel que estava certo. A medicao e a imagem
// final tem de ser A MESMA COISA - senao o teste mente.
// LovyanGFX& e nao template: o pre-processador do Arduino gera prototipos
// automaticos e os enfia ENTRE o 'template<...>' e a funcao, quebrando a
// compilacao com "'G' was not declared". Como a tela e o sprite herdam os dois
// de LovyanGFX, a referencia a base resolve sem template nenhum.
static void barra(LovyanGFX& g, int i, const char* nome, uint16_t cor, int w, int h)
{
  int bh = h / 6;
  g.fillRect(0, i * bh, w, bh, cor);
  g.setTextDatum(middle_left);
  g.setFont(&fonts::FreeSansBold18pt7b);
  g.setTextColor(TFT_BLACK);
  g.drawString(nome, 30, i * bh + bh / 2);
}

static void faixas(LovyanGFX& g, int w, int h)
{
  // Cores nomeadas, para a conferencia ser objetiva em vez de "parece certo".
  // Se alguma faixa sair na cor errada, e formato de pixel (RGB565 x BGR565 ou
  // byte trocado) - o mesmo erro que deixou o logo rosa na tela de 5".
  barra(g, 0, "VERMELHO", TFT_RED,    w, h);
  barra(g, 1, "VERDE",    TFT_GREEN,  w, h);
  barra(g, 2, "AZUL",     TFT_BLUE,   w, h);
  barra(g, 3, "AMARELO",  TFT_YELLOW, w, h);
  barra(g, 4, "BRANCO",   TFT_WHITE,  w, h);
  barra(g, 5, "CINZA",    (uint16_t)0x8410, w, h);
}

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== MTS | primeira luz da tela de 7 polegadas ===");
  Serial.printf("PSRAM: %u bytes\n", (unsigned)ESP.getPsramSize());
  if (ESP.getPsramSize() == 0) {
    Serial.println("PSRAM ZERO - falta PSRAM=enabled no FQBN. A tela nao vai subir.");
  }

  if (!tft.init()) { Serial.println("ERRO: painel nao inicializou."); while (true) delay(1000); }
  tft.setBrightness(255);

  Serial.printf("painel %s | %dx%d\n", tft.panelName, tft.width(), tft.height());
  if (tft.width() != 1024 || tft.height() != 600)
    Serial.println("ATENCAO: nao deu 1024x600 - config do painel errada.");

  // ------------------------------------------------------------- DIAGNOSTICO
  // A tela acende mas fica preta, e TODAS as etapas reportam sucesso. Isso deixa
  // duas hipoteses que precisam ser separadas antes de mexer em qualquer coisa:
  //
  //   A) o desenho NAO chega no framebuffer  -> problema no caminho de desenho
  //   B) o desenho chega e nao aparece       -> problema na SAIDA DE VIDEO
  //      (temporizacao do DPI, lanes, DISPON, painel em modo errado)
  //
  // readPixel le de volta do proprio framebuffer. Um numero resolve a duvida, e
  // sem ele eu ficaria trocando parametro no escuro - que foi o que eu estava
  // fazendo.
  tft.fillRect(0, 0, 200, 200, TFT_RED);
  uint32_t leu = tft.readPixel(10, 10);
  Serial.printf("\n[diag] pintei VERMELHO e li de volta: %06X\n", (unsigned)leu);
  Serial.println(leu ? "[diag] o desenho CHEGA no framebuffer -> o problema e a SAIDA DE VIDEO"
                     : "[diag] o desenho NAO chega -> o problema e o caminho de desenho");
  tft.fillRect(0, 0, 200, 200, TFT_GREEN);
  Serial.printf("[diag] pintei VERDE e li de volta:    %06X\n",
                (unsigned)tft.readPixel(10, 10));

  // NAO LEIA O PAINEL POR DBI NESTA PLACA. Tentei ler 0x04 (RDDID) aqui e a placa
  // TRAVOU - a saida serial morre e nada mais roda. E o mesmo comportamento do
  // painel da tela de 5" (por isso existe o MTS_SKIP_PANEL_ID la): o painel nao
  // responde leitura, o esp_lcd_new_panel_io_dbi espera o bus turn-around, a FIFO
  // enche e o HAL gira num while() sem timeout. Nao ha erro, nao ha panic - a
  // placa simplesmente emudece.

  // ---- o backlight e mesmo o GPIO 26?
  // Tres piscadas LENTAS. Se a tela piscar, o pino esta certo e ela esta acesa.
  // Se nao piscar, ou o pino e outro ou o backlight nunca ligou - e nesse caso o
  // video pode estar funcionando o tempo todo, so invisivel.
  Serial.println("[diag] o backlight vai PISCAR 3 vezes agora (1 s aceso, 1 s apagado)");
  for (int i = 0; i < 3; i++) {
    tft.setBrightness(0);   delay(1000);
    tft.setBrightness(255); delay(1000);
  }
  Serial.println("[diag] fim das piscadas");

  // Desenha direto na tela primeiro: se o sprite falhar, ainda ha imagem.
  faixas(tft, tft.width(), tft.height());

  // Medida do quadro, para comparar com a de 5" (73 ms de push, 921.600 px).
  // Aqui sao 614.400 px e nao ha rotacao: espero algo perto de 49 ms.
  // O sprite desenha AS MESMAS faixas, entao empurrar por cima nao muda o que
  // esta na tela - so prova que o caminho do sprite tambem funciona.
  LGFX_Sprite cv(&tft);
  cv.setPsram(true);
  cv.setColorDepth(16);
  if (cv.createSprite(tft.width(), tft.height())) {
    faixas(cv, cv.width(), cv.height());
    cv.drawString("via sprite", 30, cv.height() - 40);
    uint32_t a = micros();
    cv.pushSprite(0, 0);
    uint32_t b = micros();
    Serial.printf("pushSprite %dx%d: %lu ms  (na de 5 polegadas: 73 ms)\n",
                  tft.width(), tft.height(), (unsigned long)((b - a) / 1000));
    cv.deleteSprite();
  } else {
    Serial.println("nao coube o sprite na PSRAM");
  }

  // ---- cartao SD. Nesta placa a alimentacao vem de LDO interno, nao de GPIO.
  Serial.println("\n-- cartao SD --");
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
#ifdef SOC_SDMMC_IO_POWER_EXTERNAL
  SD_MMC.setPowerChannel(SD_LDO_CANAL);
  Serial.printf("alimentacao do slot: LDO interno canal %d\n", SD_LDO_CANAL);
#else
  Serial.println("ATENCAO: este core nao tem setPowerChannel - o cartao pode nao subir");
#endif
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("nao montou (cartao esta na outra tela? erro 0x107 = sem cartao)");
  } else {
    Serial.printf("montou: %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
  }

  // Termina em BRANCO PURO no brilho maximo. E o estado mais visivel possivel:
  // se o video e o backlight estiverem os dois funcionando, e impossivel nao ver.
  delay(2000);
  tft.fillScreen(TFT_WHITE);
  tft.setBrightness(255);
  Serial.println("[diag] tela agora em BRANCO PURO, brilho maximo");

  Serial.println("\ntoque na tela para ver as coordenadas.");
}

void loop()
{
  int16_t x, y;
  if (tft.getTouch(&x, &y)) {
    Serial.printf("toque: %d,%d\n", x, y);
    tft.fillCircle(x, y, 12, TFT_MAGENTA);
    delay(80);
  }
  delay(10);
}
