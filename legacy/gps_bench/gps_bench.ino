/*
 * GPS BENCH - mede objetivamente cada modulo/antena (roda no CYD/ESP32).
 * Liga o GPS em Serial2 (TX do GPS -> IO27, 9600). Cronometra o 1o FIX (TTFF)
 * e mostra satelites usados + satelites a vista + qualidade (SNR max).
 * Testa cada config (SAM-M8Q / NEO-7M interna / NEO-7M externa) e compara os numeros.
 * REGRA JUSTA: mesmo lugar, mesma janela de ceu; anota TTFF e sats de cada um.
 */
#include <TinyGPSPlus.h>
TinyGPSPlus gps;
TinyGPSCustom gpInView(gps, "GPGSV", 3);   // GPS satelites a vista
TinyGPSCustom glInView(gps, "GLGSV", 3);   // GLONASS satelites a vista

unsigned long t0, lastP=0; bool got=false; unsigned long ttffMs=0;

void setup(){
  Serial.begin(115200); delay(400);
  Serial2.begin(9600, SERIAL_8N1, 27, -1);
  t0 = millis();
  Serial.println("\n================================");
  Serial.println(" GPS BENCH - cronometro do 1o FIX");
  Serial.println(" (TX do GPS -> IO27, 9600)");
  Serial.println("================================");
}

void loop(){
  while(Serial2.available()) gps.encode(Serial2.read());

  if(!got && gps.location.isValid() && gps.location.isUpdated()){
    got = true; ttffMs = millis()-t0;
    Serial.println();
    Serial.printf(">>> FIX! TTFF = %.1f s  |  sats usados = %d\n", ttffMs/1000.0, gps.satellites.value());
    Serial.println();
  }

  if(millis()-lastP > 2000){
    lastP = millis();
    int inview = atoi(gpInView.value()) + atoi(glInView.value());
    Serial.printf("t=%3.0fs | fix=%s | sats_usados=%d | sats_a_vista=%d | NMEA=%lu",
      (millis()-t0)/1000.0,
      got?"SIM":"---",
      gps.satellites.isValid()?gps.satellites.value():0,
      inview,
      gps.charsProcessed());
    if(got) Serial.printf("  [TTFF foi %.1fs]", ttffMs/1000.0);
    Serial.println();
  }
}
