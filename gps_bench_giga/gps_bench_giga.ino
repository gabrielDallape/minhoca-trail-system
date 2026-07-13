/*
 * GPS BENCH (GIGA) - mede o modulo/antena pela Serial1 (pino 0 = RX0, 9600).
 * Cronometra o 1o FIX (TTFF) + satelites usados/a vista. Sem tela (so USB serial).
 * Fiacao: NEO-7M VCC->3V3, GND->GND, TXD->pino 0. Antena externa no conector.
 * Testa cada config no mesmo lugar/janela e compara os numeros.
 */
#include <TinyGPSPlus.h>
TinyGPSPlus gps;
TinyGPSCustom gpInView(gps, "GPGSV", 3);
TinyGPSCustom glInView(gps, "GLGSV", 3);

unsigned long t0, lastP=0; bool got=false; unsigned long ttffMs=0;

void setup(){
  Serial.begin(115200);
  unsigned long s=millis(); while(!Serial && millis()-s<3000){}
  Serial1.begin(9600);          // GPS na Serial1 (pinos 0/1)
  t0 = millis();
  Serial.println("\n================================");
  Serial.println(" GPS BENCH GIGA - cronometro 1o FIX");
  Serial.println(" (TXD do GPS -> pino 0, 9600)");
  Serial.println("================================");
}

void loop(){
  while(Serial1.available()) gps.encode(Serial1.read());

  if(!got && gps.location.isValid() && gps.location.isUpdated()){
    got = true; ttffMs = millis()-t0;
    Serial.println();
    Serial.print(">>> FIX! TTFF = "); Serial.print(ttffMs/1000.0,1);
    Serial.print(" s  |  sats usados = "); Serial.println(gps.satellites.value());
    Serial.println();
  }

  if(millis()-lastP > 2000){
    lastP = millis();
    int inview = atoi(gpInView.value()) + atoi(glInView.value());
    Serial.print("t="); Serial.print((millis()-t0)/1000.0,0);
    Serial.print("s | fix="); Serial.print(got?"SIM":"---");
    Serial.print(" | sats_usados="); Serial.print(gps.satellites.isValid()?gps.satellites.value():0);
    Serial.print(" | sats_a_vista="); Serial.print(inview);
    Serial.print(" | NMEA="); Serial.print(gps.charsProcessed());
    if(got){ Serial.print("  [TTFF="); Serial.print(ttffMs/1000.0,1); Serial.print("s]"); }
    Serial.println();
  }
}
