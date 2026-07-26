/*
 * Teste RA8875 (BuyDisplay 5" 800x480) via SPI - so pra validar lib + display.
 * Wiring SPI (JP1): SCLK, MISO, MOSI no SPI de hardware; CS, RST em GPIO; INT (JP2).
 */
#include <SPI.h>
#include "Adafruit_RA8875.h"

#define RA8875_CS    10
#define RA8875_RESET 9
Adafruit_RA8875 tft = Adafruit_RA8875(RA8875_CS, RA8875_RESET);

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 3000) {}
  Serial.println("Iniciando RA8875...");
  if (!tft.begin(RA8875_800x480)) {
    Serial.println("RA8875 NAO encontrado - confere fiacao/SPI/jumpers");
    while (1) {}
  }
  Serial.println("RA8875 OK!");
  tft.displayOn(true);
  tft.GPIOX(true);
  tft.PWM1config(true, RA8875_PWM_CLK_DIV1024);
  tft.PWM1out(255);

  tft.fillScreen(RA8875_BLACK);
  tft.fillRect(40, 40, 200, 120, RA8875_RED);
  tft.fillRect(280, 40, 200, 120, RA8875_GREEN);
  tft.fillRect(520, 40, 200, 120, RA8875_BLUE);
  tft.textMode();
  tft.textColor(RA8875_WHITE, RA8875_BLACK);
  tft.textEnlarge(2);
  tft.textSetCursor(60, 220);
  tft.textWrite("HELLO RA8875");
}

void loop() {
  Serial.println("vivo");
  delay(1000);
}
