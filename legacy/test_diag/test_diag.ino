/*
 * TESTE DIAGNOSTICO - texto em VARIAS posicoes + formas, sobre cor solida.
 * Descobre se o glitch:
 *   (a) fica so na FAIXA DE CIMA  -> regiao de hardware
 *   (b) acompanha o TEXTO onde estiver -> renderizacao (software)
 *   (c) aparece tambem nas FORMAS -> desenho geral
 */
#include "Arduino_GigaDisplay_GFX.h"

GigaDisplay_GFX gfx;
int SCR_W, SCR_H;
unsigned long last = 0;
int frame = 0;

void setup() {
  Serial.begin(115200);
  gfx.begin();
  gfx.setRotation(0);
  SCR_W = gfx.width();
  SCR_H = gfx.height();
}

void draw() {
  gfx.fillScreen(0x001F);                       // fundo AZUL solido

  gfx.setTextColor(0xFFFF, 0x001F);

  gfx.setTextSize(4);
  gfx.setCursor(20,  30); gfx.print("TOPO 12345");
  gfx.setCursor(20, 380); gfx.print("MEIO 12345");
  gfx.setCursor(20, 740); gfx.print("BASE 12345");

  // formas no meio (sem texto)
  gfx.drawRect(20, 450, 300, 120, 0xFFFF);      // retangulo branco
  gfx.fillCircle(170, 260, 40, 0xFFE0);         // circulo amarelo
  gfx.drawLine(20, 200, 320, 230, 0xF800);      // linha vermelha
  gfx.fillRect(20, 600, 120, 60, 0x07E0);       // bloco verde
}

void loop() {
  draw();                 // redesenha sempre (mantem os 2 framebuffers iguais)
  frame++;
  if (millis() - last > 1000) { Serial.print("frame="); Serial.println(frame); last = millis(); }
  delay(100);
}
