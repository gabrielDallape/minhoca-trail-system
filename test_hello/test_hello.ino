#include "Arduino_GigaDisplay_GFX.h"

GigaDisplay_GFX display;

void setup() {
  display.begin();
}

void loop() {
  display.fillScreen(0x0000);
  display.setTextColor(0xFFFF);
  display.setTextSize(6);
  display.setCursor(60, 300);
  display.print("HELLO");
  display.drawRect(50, 200, 380, 250, 0xF800);
  delay(200);
}
