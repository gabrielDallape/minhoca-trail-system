/*
 * TESTE MINIMO DO MKR GPS POR I2C - saida pela Serial (USB)
 * Sem display. Objetivo: descobrir se o modulo u-blox responde em 0x42.
 *
 * Liga (I2C): VCC->3V3, GND->GND, SDA(11)->20, SCL(12)->21 do GIGA.
 */
#include <Wire.h>
#define UBLOX 0x42

unsigned long contador = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }   // espera o monitor abrir (ate 4s)

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" TESTE GPS I2C (sem tela)");
  Serial.println("=========================================");

  Wire.begin();
  Wire.setClock(100000);
}

void loop() {
  contador++;
  Serial.print("\n##### Ciclo ");
  Serial.print(contador);
  Serial.println(" #####");

  // --- niveis das linhas em repouso ---
  pinMode(20, INPUT);  pinMode(21, INPUT);  delay(2);
  Serial.print("SDA(20) solto: "); Serial.println(digitalRead(20) ? "HIGH" : "LOW");
  Serial.print("SCL(21) solto: "); Serial.println(digitalRead(21) ? "HIGH" : "LOW");
  Wire.begin();  // reconfigura como I2C

  // --- varredura ---
  Serial.println("Scan Wire (pinos 20/21):");
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("   achei 0x");
      if (a < 16) Serial.print("0");
      Serial.print(a, HEX);
      if (a == UBLOX) Serial.print("   <<<< GPS u-blox!");
      Serial.println();
      n++;
    }
  }
  if (n == 0) Serial.println("   (nenhum dispositivo respondeu)");

  // --- se o GPS respondeu, le o fluxo NMEA ---
  Wire.beginTransmission(UBLOX);
  if (Wire.endTransmission() == 0) {
    Serial.println("Lendo NMEA do 0x42:");
    int reais = 0;
    for (int blk = 0; blk < 32; blk++) {
      int got = Wire.requestFrom(UBLOX, 32);
      for (int i = 0; i < got && Wire.available(); i++) {
        char c = Wire.read();
        if (c != (char)0xFF) { Serial.write(c); reais++; }
      }
    }
    Serial.print("\n(bytes reais lidos: ");
    Serial.print(reais);
    Serial.println(")");
  }

  delay(2000);
}
