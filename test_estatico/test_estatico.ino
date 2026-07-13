/*
 * TESTE ESTATICO do GIGA Display - desenha UMA vez e para.
 * Se isto aparecer limpo, o display esta 100% (o bug era o redesenho).
 */
#include "Arduino_GigaDisplay_GFX.h"

GigaDisplay_GFX gfx;

void setup() {
  gfx.begin();
  gfx.setRotation(0);

  gfx.fillScreen(0x0000);                       // fundo preto

  gfx.fillRect(40,  60, 200, 160, 0xF800);      // vermelho
  gfx.fillRect(40, 260, 200, 160, 0x07E0);      // verde
  gfx.fillRect(40, 460, 200, 160, 0x001F);      // azul

  gfx.setTextColor(0xFFFF);
  gfx.setTextSize(5);
  gfx.setCursor(30, 700);
  gfx.print("GIGA OK");

  gfx.setTextSize(2);
  gfx.setTextColor(0xFFE0);
  gfx.setCursor(30, 20);
  gfx.print("TESTE ESTATICO");
}

void loop() {
  // nada: imagem fixa
}
