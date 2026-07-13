/*
 * Teste RA8875 (BuyDisplay 5") no ESP32 via SPI (VSPI).
 * SPI do ESP32: SCLK=18, MISO=19, MOSI=23. CS=5, RST=4, INT=25.
 * Placa em 3,3V (J8). Conector JP1 (SPI) + JP2 (RESET/INT).
 */
#include <SPI.h>
#include "Adafruit_RA8875.h"

#define RA8875_CS    5
#define RA8875_RESET 4
#define RA8875_INT   25
Adafruit_RA8875 tft = Adafruit_RA8875(RA8875_CS, RA8875_RESET);

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("Iniciando RA8875 no ESP32...");
  if (!tft.begin(RA8875_480x272)) {         // painel desta placa e 480x272
    Serial.println("RA8875 NAO encontrado - placa em paralelo? confere jumpers/SPI");
    while (1) { delay(500); }
  }
  Serial.println("RA8875 OK!");
  tft.displayOn(true);
  tft.GPIOX(true);
  tft.PWM1config(true, RA8875_PWM_CLK_DIV1024);
  tft.PWM1out(255);

  tft.fillScreen(RA8875_BLACK);
  tft.fillRect(20, 20, 130, 90, RA8875_RED);
  tft.fillRect(175, 20, 130, 90, RA8875_GREEN);
  tft.fillRect(330, 20, 130, 90, RA8875_BLUE);
  tft.textMode();
  tft.textColor(RA8875_WHITE, RA8875_BLACK);
  tft.textEnlarge(1);
  tft.textSetCursor(30, 150);
  tft.textWrite("HELLO RA8875");
}

void loop() {
  // prova de SPI: escreve 0x2A num registrador e le de volta
  tft.writeReg(0x63, 0x2A);
  uint8_t v = tft.readReg(0x63);
  Serial.print("SPI readback reg0x63 (esperado 2A) = 0x");
  Serial.println(v, HEX);
  delay(1000);
}
