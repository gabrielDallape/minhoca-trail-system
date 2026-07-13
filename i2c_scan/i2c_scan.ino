/*
 * DIAGNOSTICO I2C - Arduino GIGA + GIGA Display Shield
 * Mostra na tela:
 *   - Estado das linhas SDA(20)/SCL(21) em repouso (devem estar em HIGH se
 *     houver pull-up e nada segurando a linha).
 *   - Varredura de enderecos em Wire (20/21), Wire1 e Wire2 (8/9).
 *   - Destaca 0x42 (endereco do u-blox SAM-M8Q do MKR GPS).
 *
 * Use so para diagnostico; depois voltamos ao sketch do odometro.
 */
#include <Wire.h>
#include "Arduino_GigaDisplay_GFX.h"

GigaDisplay_GFX gfx;

#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_GREEN  0x07E0
#define C_RED    0xF800
#define C_YELLOW 0xFFE0
#define C_CYAN   0x07FF
#define C_GREY   0x8410

int Y;

void line(const char* s, uint16_t color, int size = 1) {
  gfx.setTextSize(size);
  gfx.setTextColor(color);
  gfx.setCursor(10, Y);
  gfx.print(s);
  Y += (size == 1) ? 16 : 22;
}

int scanBus(TwoWire &bus, const char* name) {
  char buf[40];
  gfx.setTextSize(2);
  gfx.setTextColor(C_YELLOW);
  gfx.setCursor(10, Y);
  gfx.print(name);
  Y += 24;

  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) {
      snprintf(buf, sizeof(buf), "  achei 0x%02X%s", a, (a == 0x42) ? "  <-- GPS!" : "");
      line(buf, (a == 0x42) ? C_GREEN : C_WHITE, 1);
      found++;
    }
  }
  if (found == 0) line("  (nenhum dispositivo)", C_RED, 1);
  Y += 8;
  return found;
}

void setup() {
  Serial.begin(115200);
  gfx.begin();
  gfx.setRotation(0);
}

void loop() {
  char buf[48];
  gfx.fillScreen(C_BLACK);
  Y = 10;
  line("DIAGNOSTICO I2C", C_CYAN, 2);
  Y += 6;

  // 1) Nivel das linhas em repouso, SEM pull-up interno (so leitura)
  pinMode(20, INPUT);
  pinMode(21, INPUT);
  delay(2);
  int sdaF = digitalRead(20);
  int sclF = digitalRead(21);
  snprintf(buf, sizeof(buf), "SDA(20) solto: %s", sdaF ? "HIGH" : "LOW");
  line(buf, sdaF ? C_GREEN : C_RED, 1);
  snprintf(buf, sizeof(buf), "SCL(21) solto: %s", sclF ? "HIGH" : "LOW");
  line(buf, sclF ? C_GREEN : C_RED, 1);

  // 2) Com pull-up interno do GIGA
  pinMode(20, INPUT_PULLUP);
  pinMode(21, INPUT_PULLUP);
  delay(2);
  int sdaP = digitalRead(20);
  int sclP = digitalRead(21);
  snprintf(buf, sizeof(buf), "SDA(20) c/pullup: %s", sdaP ? "HIGH" : "LOW");
  line(buf, sdaP ? C_GREEN : C_RED, 1);
  snprintf(buf, sizeof(buf), "SCL(21) c/pullup: %s", sclP ? "HIGH" : "LOW");
  line(buf, sclP ? C_GREEN : C_RED, 1);
  Y += 8;

  // 3) Varredura nos barramentos
  Wire.begin();
  scanBus(Wire, "Wire (pinos 20/21)");

  Wire1.begin();
  scanBus(Wire1, "Wire1");

  Wire2.begin();
  scanBus(Wire2, "Wire2 (pinos 8/9)");

  line("atualiza a cada 2s...", C_GREY, 1);
  delay(2000);
}
