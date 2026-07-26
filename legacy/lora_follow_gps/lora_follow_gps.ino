/*
 * LoRaMESH - SEGUIDOR (GIGA): recebe lat/lon do lider por RF e imprime.
 * ---------------------------------------------------------------------------
 * LoRa master na Serial2 (D18/D19). Le pacote de aplicacao cmd 0x11 (GPS).
 * (Por enquanto so imprime no USB; o mapa na tela vem depois.)
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial2);
const uint8_t APP_CMD_GPS = 0x11;

uint32_t rx_ok = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" SEGUIDOR (GIGA) - recebendo GPS do lider");
  Serial.println("=========================================");

  Serial2.begin(9600);
  delay(200);
  lora.begin(false);
  Serial.print("Meu LocalID = "); Serial.println(lora.localId);
  Serial.println("Aguardando GPS do lider...");
  Serial.println("-----------------------------------------");
}

void loop() {
  uint16_t id = 0xFFFF;
  uint8_t  cmd = 0, p[240], plen = 0;

  if (lora.ReceivePacketCommand(&id, &cmd, p, &plen, 3000)) {
    if (cmd == APP_CMD_GPS && plen >= 10) {
      bool fix = p[0] & 0x01;
      uint8_t sats = p[1];
      int32_t lat = (int32_t)((uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24));
      int32_t lon = (int32_t)((uint32_t)p[6] | ((uint32_t)p[7] << 8) | ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 24));
      rx_ok++;
      Serial.print("[RX de ID="); Serial.print(id); Serial.print("] fix=");
      Serial.print(fix ? "SIM" : "NAO");
      Serial.print(" sats="); Serial.print(sats);
      Serial.print(" lat="); Serial.print(lat / 1e7, 6);
      Serial.print(" lon="); Serial.print(lon / 1e7, 6);
      Serial.print("  (pacotes="); Serial.print(rx_ok); Serial.println(")");
    } else {
      Serial.print("[outro] id="); Serial.print(id);
      Serial.print(" cmd=0x"); Serial.print(cmd, HEX);
      Serial.print(" plen="); Serial.println(plen);
    }
  }
}
