/*
 * CYD - calibracao do toque resistivo (XPT2046) via LovyanGFX.
 * Mostra alvos nos cantos; toque em cada um. Ao fim imprime calData[8] na serial.
 * Depois: colar esse calData no cyd_dual (tft.setTouchCalibration).
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

static LGFX tft;

void setup(){
  Serial.begin(115200);
  delay(400);
  tft.init();
  tft.setRotation(1);            // mesma orientacao do cyd_dual
  tft.fillScreen(0x0000);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(2);
  tft.setCursor(20, 90);  tft.print("CALIBRACAO DO TOQUE");
  tft.setTextSize(1);
  tft.setCursor(20, 120); tft.print("Toque no centro de cada alvo");
  tft.setCursor(20, 135); tft.print("que aparecer nos cantos.");
  delay(2500);

  uint16_t cal[8];
  tft.calibrateTouch(cal, 0xFFFF, 0x0000, 20);   // alvos nos cantos, aguarda 4 toques

  Serial.println();
  Serial.print("CALDATA: ");
  for (int i=0;i<8;i++){ Serial.print(cal[i]); if(i<7) Serial.print(","); }
  Serial.println();

  tft.fillScreen(0x0000);
  tft.setTextSize(2); tft.setTextColor(0x07E0);
  tft.setCursor(20, 90); tft.print("OK! Toque p/ testar");
  tft.setTextColor(0xFFFF);
  tft.setCursor(20, 120); tft.setTextSize(1); tft.print("os pontos verdes devem cair onde voce toca");
}

void loop(){
  int32_t x,y;
  if (tft.getTouch(&x,&y)){
    tft.fillCircle(x,y,4,0x07E0);
    Serial.print("touch "); Serial.print(x); Serial.print(","); Serial.println(y);
  }
  delay(15);
}
