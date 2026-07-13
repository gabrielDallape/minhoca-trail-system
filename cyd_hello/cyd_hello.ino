/*
 * CYD (ESP32-2432S028R) - TESTE HELLO com LovyanGFX (AUTODETECT)
 * A LovyanGFX detecta sozinha o painel do CYD (ILI9341 ou ST7789) e ja
 * ajusta inversao de cor e ordem RGB. Sem config manual = menos chance de bug.
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

static LGFX tft;

void setup() {
  Serial.begin(115200);
  delay(300);
  bool ok = tft.init();
  Serial.print("init="); Serial.println(ok);
  Serial.print("board detectado (num)="); Serial.println((int)tft.getBoard());

  tft.setRotation(1);              // paisagem 320x240
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(10, 10, 90, 60, TFT_RED);
  tft.fillRect(115, 10, 90, 60, TFT_GREEN);
  tft.fillRect(220, 10, 90, 60, TFT_BLUE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(4);
  tft.setCursor(40, 110);
  tft.print("HELLO CYD");

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(40, 170);
  tft.print("cores certas?");
}

void loop() {
  Serial.println("CYD vivo");
  delay(1000);
}
