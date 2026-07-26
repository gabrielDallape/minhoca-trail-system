/*
 * DIAGNOSTICO do SEGUIDOR (GIGA) - checa LoRa + GPS pela serial USB.
 * Roda em loop: tenta ler o ID do LoRa (Serial2) e conta bytes NMEA do GPS (Serial1).
 * Serve pra validar fiacao/energia SEM depender da tela.
 *
 * LoRa (Serial2): VCC->3V3(step-down)  GND->GND  TX_1(3)->pino18  RX_1(2)->pino19
 * GPS  (Serial1): VCC->3V3(step-down)  GND->GND  pino13(dado)->pino0
 * Abrir monitor a 115200.
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial2);

unsigned long gpsBytes = 0;
char lastLine[120];
int  lp = 0;
bool gotLine = false;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) {}
  Serial.println();
  Serial.println("=====================================");
  Serial.println(" DIAG SEGUIDOR (GIGA)  -  LoRa + GPS");
  Serial.println("=====================================");

  Serial2.begin(9600);   // LoRa (comando)
  Serial1.begin(9600);   // GPS
  delay(200);
  lora.begin(true);      // tenta ja no boot (pode falhar se a fonte ainda nao ligou)
  Serial.println("Ligue a fonte (3,3V medido!) e observe os status abaixo:");
}

void loop() {
  // --- coleta GPS por ~1s ---
  unsigned long t0 = millis();
  while (millis() - t0 < 1000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      gpsBytes++;
      if (c == '\n') { lastLine[lp] = 0; gotLine = (lp > 0); lp = 0; }
      else if (lp < (int)sizeof(lastLine) - 1 && c != '\r') lastLine[lp++] = c;
    }
  }

  // --- testa LoRa ---
  bool loraOk = lora.localread();

  Serial.println("-------------------------------------");
  if (loraOk) {
    Serial.print("[LoRa] OK   LocalID="); Serial.print(lora.localId);
    Serial.print("  UniqueID=");          Serial.println(lora.localUniqueId);
  } else {
    Serial.println("[LoRa] FALHA -> confira VCC(3,3V)/GND, TX(3)->18, RX(2)->19, antena, baud 9600");
  }

  Serial.print("[GPS]  bytes recebidos (total)="); Serial.print(gpsBytes);
  if (gotLine) { Serial.print("  ultima linha: "); Serial.println(lastLine); }
  else         { Serial.println("  (nenhuma linha NMEA ainda -> confira pino13->0, VCC/GND)"); }
}
