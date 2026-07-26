/*
 * CYD - diagnostico do GPS pela serial USB.
 * Ecoa o NMEA cru do GPS (Serial2, RX=IO27) e um status a cada 2s.
 * Se aparecerem frases $GNGGA/$GPGSV etc subindo, o GPS esta vivo (so falta fix).
 */
#include <TinyGPSPlus.h>
TinyGPSPlus gps;
unsigned long lastStatus = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== GPS diag (Serial2 RX=IO27, 9600) ===");
  Serial2.begin(9600, SERIAL_8N1, 27, -1);
}

void loop() {
  while (Serial2.available()) {
    char c = Serial2.read();
    gps.encode(c);
    Serial.write(c);              // ecoa o NMEA cru
  }
  if (millis() - lastStatus > 2000) {
    lastStatus = millis();
    Serial.printf("\n[STATUS] NMEA_chars=%lu  sats=%d  fix=%d",
      gps.charsProcessed(),
      gps.satellites.isValid()?gps.satellites.value():-1,
      gps.location.isValid()?1:0);
    if (gps.location.isValid())
      Serial.printf("  %.6f,%.6f", gps.location.lat(), gps.location.lng());
    Serial.println();
  }
}
