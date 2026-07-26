/*
 * ws_hello v4 - texto + toque com LOG continuo (Waveshare 7B).
 * A tela mostra o "hello" (nao fica preta) E o serial loga o status do GT911
 * o tempo todo, pra diagnosticar o toque sem tela preta.
 * Display via LovyanGFX; expansor CH32V003 (0x24) e touch GT911 (0x5D) via Wire.
 * Grave pela porta UART/CH343 (switch UART1), CDCOnBoot=default.
 */
#include <Wire.h>
#include "LGFX_WS7B.h"

LGFX tft;

bool ioExt(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x24);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

// liga o painel ANTES do tft.init(), com o touch mantido em reset
void powerUpPanel() {
  Wire.begin(8, 9);
  Wire.setClock(400000);
  ioExt(0x02, 0xFF);                          // todos os IO como saida
  ioExt(0x03, 0x5C); delay(50);               // painel on (IO2/3/6), USB (IO5=0), touch em reset (IO1=0)
}
// reset do GT911 DEPOIS do painel ja estar varrendo (mesma ordem do grupo_ws que funcionou)
void resetTouch() {
  pinMode(4, OUTPUT);
  ioExt(0x03, 0x5C); delay(100);              // IO1=0: touch em reset
  digitalWrite(4, LOW); delay(100);           // INT baixo -> GT911 assume o endereco 0x5D
  ioExt(0x03, 0x5E); delay(200);              // IO1=1: solta o reset
  pinMode(4, INPUT);                          // INT vira entrada
}

// le status + ponto do GT911 (0x5D). retorna status; x/y=-1 se sem ponto.
// MAPA CORRETO (do gt911.cpp oficial): 0x814E=status; ponto 1: X em 0x8150(L)/0x8151(H),
// Y em 0x8152(L)/0x8153(H). (eu lia deslocado a partir de 0x8150 descartando 1 byte -> errado)
uint8_t gt911(int &x, int &y) {
  x = -1; y = -1;
  uint8_t st = 0;
  Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x4E);
  if (Wire.endTransmission(false) == 0) {
    Wire.requestFrom(0x5D, 1);
    if (Wire.available()) st = Wire.read();
  }
  uint8_t nt = st & 0x0F;
  if ((st & 0x80) && nt > 0 && nt <= 5) {
    Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x50);   // X_low do ponto 1
    Wire.endTransmission(false);
    Wire.requestFrom(0x5D, 4);
    if (Wire.available() >= 4) {
      uint8_t xl = Wire.read(), xh = Wire.read(), yl = Wire.read(), yh = Wire.read();
      x = xl | (xh << 8); y = yl | (yh << 8);
    }
  }
  if (st & 0x80) {                            // limpa o flag (obrigatorio senao trava)
    Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x4E); Wire.write((uint8_t)0);
    Wire.endTransmission();
  }
  return st;
}

void drawHello() {
  int w = tft.width(), h = tft.height();
  tft.fillScreen(tft.color888(11, 20, 27));
  tft.fillRect(0, 0, w, 8, tft.color888(52, 211, 153));
  tft.setTextDatum(middle_center);
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.setTextSize(2);
  tft.drawString("TRILHA", w/2, h/2 - 90);
  tft.setTextSize(1);
  tft.setFont(&fonts::FreeSans18pt7b);
  tft.setTextColor(tft.color888(52, 211, 153));
  tft.drawString("Waveshare 7B - toque para testar", w/2, h/2 - 10);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(tft.color888(148, 169, 157));
  tft.drawString("a bolinha laranja segue o dedo", w/2, h/2 + 50);
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 3000) { }
  Serial.println("\n=== ws_hello v4: texto + toque (log continuo) ===");
  powerUpPanel();                             // liga painel (touch em reset)
  bool ok = tft.init();
  Serial.printf("tft.init()=%s %dx%d\n", ok ? "OK" : "FALHOU", tft.width(), tft.height());
  resetTouch();                               // AGORA reseta o touch (painel ja varrendo)
  drawHello();
  Serial.println("pronto. TOQUE na tela.");
}

void loop() {
  int x, y;
  uint8_t st = gt911(x, y);
  if (x >= 0 && x < tft.width() && y >= 0 && y < tft.height())
    tft.fillCircle(x, y, 14, TFT_ORANGE);

  static unsigned long lastLog = 0;
  if (millis() - lastLog > 500) {
    lastLog = millis();
    Serial.printf("status=0x%02X dedos=%d", st, st & 0x0F);
    if (x >= 0) Serial.printf("  ponto=(%d,%d)", x, y);
    Serial.println();
  }
  delay(15);
}
