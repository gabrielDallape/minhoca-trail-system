/*
 * TESTE DE DISPLAY (puro) - sem GPS, sem LoRa.
 * Objetivo: descobrir QUAL modo de desenho fica limpo neste GIGA Display.
 *
 * Alterna a cada 5 s entre dois modos (escrito no topo da tela):
 *   MODO A = buffering ON  + limpa a tela toda e redesenha (o que usei no mapa)
 *   MODO B = buffering OFF + desenho incremental (so apaga/redesenha o que mudou)
 *
 * A cena tem: um contador (testa apagar texto), um quadrado que anda (testa
 * apagar area) e um texto fixo. Me diga qual modo (A ou B) fica sem "bug".
 */
#include "Arduino_GigaDisplay_GFX.h"

GigaDisplay_GFX gfx;

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_YEL   0xFFE0
#define C_CYAN  0x07FF

int SCR_W, SCR_H;
uint32_t counter = 0;
int boxX = 20, boxDir = 6;
int prevBoxX = 20;
const int boxY = 300, boxW = 60, boxH = 60;
int mode = 0;                 // 0 = A, 1 = B
unsigned long lastSwitch = 0, lastFrame = 0;
bool modeInit = false;

void setup() {
  Serial.begin(115200);
  gfx.begin();
  gfx.setRotation(0);
  SCR_W = gfx.width();
  SCR_H = gfx.height();
  gfx.fillScreen(C_BLACK);
  lastSwitch = millis();
}

void drawScene() {
  char b[16];
  // titulo do modo
  gfx.setTextSize(3); gfx.setTextColor(mode == 0 ? C_GREEN : C_CYAN, C_BLACK);
  gfx.setCursor(20, 30); gfx.print(mode == 0 ? "MODO A" : "MODO B");

  // contador
  gfx.setTextSize(4); gfx.setTextColor(C_YEL, C_BLACK);
  snprintf(b, sizeof(b), "%lu   ", (unsigned long)counter);
  gfx.setCursor(20, 120); gfx.print(b);

  // texto fixo
  gfx.setTextSize(2); gfx.setTextColor(C_WHITE, C_BLACK);
  gfx.setCursor(20, 500); gfx.print("TESTE DISPLAY - A ou B?");

  // quadrado que anda
  gfx.fillRect(boxX, boxY, boxW, boxH, C_RED);
  gfx.drawRect(boxX, boxY, boxW, boxH, C_WHITE);
}

void loop() {
  // troca de modo a cada 5 s
  if (millis() - lastSwitch > 5000) {
    lastSwitch = millis();
    mode = 1 - mode;
    modeInit = false;
  }

  if (millis() - lastFrame < 60) return;   // ~16 fps
  lastFrame = millis();

  counter++;
  prevBoxX = boxX;
  boxX += boxDir;
  if (boxX < 10 || boxX > SCR_W - boxW - 10) boxDir = -boxDir;

  if (mode == 0) {
    // ---- MODO A: buffering + limpa tudo e redesenha ----
    if (!modeInit) { modeInit = true; }
    gfx.startBuffering();
    gfx.fillScreen(C_BLACK);
    drawScene();
    gfx.endBuffering();
  } else {
    // ---- MODO B: sem buffering, incremental ----
    if (!modeInit) { gfx.fillScreen(C_BLACK); modeInit = true; }
    // apaga so o quadrado antigo
    gfx.fillRect(prevBoxX, boxY, boxW, boxH, C_BLACK);
    drawScene();   // textos com fundo opaco se sobrescrevem; quadrado novo desenhado
  }
}
