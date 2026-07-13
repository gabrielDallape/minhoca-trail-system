/*
 * TESTE DE CORES SOLIDAS - preenche a tela inteira com uma cor e vai trocando.
 * Redesenha continuamente (mantem os dois framebuffers preenchidos).
 *
 * DIAGNOSTICO:
 *  - Cores solidas PURAS e uniformes  -> painel/driver OK; bug era so desenho (software).
 *  - Listras / faixas / ruido / cor errada numa cor solida -> HARDWARE (conector/painel).
 */
#include "Arduino_GigaDisplay_GFX.h"

GigaDisplay_GFX gfx;

uint16_t cores[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xFFE0, 0x07FF};
const char* nomes[] = {"VERMELHO", "VERDE", "AZUL", "BRANCO", "PRETO", "AMARELO", "CIANO"};
const int N = 7;
int i = 0;
unsigned long lastSwitch = 0;

void setup() {
  Serial.begin(115200);
  gfx.begin();
  gfx.setRotation(0);
  lastSwitch = millis();
}

void loop() {
  gfx.fillScreen(cores[i]);                 // tela toda numa cor

  gfx.setTextSize(4);
  gfx.setTextColor(i == 4 ? 0xFFFF : 0x0000, cores[i]);   // texto contrastante
  gfx.setCursor(30, 40);
  gfx.print(nomes[i]);

  if (millis() - lastSwitch > 1500) {       // troca de cor a cada 1,5 s
    i = (i + 1) % N;
    lastSwitch = millis();
  }
  delay(80);
}
