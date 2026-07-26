/*
 * LoRaMESH - LIDER (MKR Zero): le o GPS e envia lat/lon por RF ao seguidor.
 * ---------------------------------------------------------------------------
 * GPS  -> Serial1 (pinos 13/14, MKR GPS Shield), NMEA 9600, TinyGPSPlus.
 * LoRa -> Serial2 via SERCOM3 (D6=TX / D7=RX), UART de comando do modulo.
 *
 * Pacote de aplicacao, comando 0x11 (GPS), 10 bytes:
 *   [0] flags (bit0 = fix valido)
 *   [1] satelites
 *   [2..5]  latitude  (int32 LE = graus * 1e7)
 *   [6..9]  longitude (int32 LE = graus * 1e7)
 *
 * ANTENA nos dois modulos! Sem fix (dentro de casa) manda fix=0 e lat/lon=0,
 * mas o PIPELINE ja e validado; ao ar livre chega coordenada real.
 */
#include "LoRaMESH.h"
#include <wiring_private.h>
#include <TinyGPSPlus.h>

Uart Serial2(&sercom3, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2);
void SERCOM3_Handler() { Serial2.IrqHandler(); }

LoRaMESH   lora(&Serial2);
TinyGPSPlus gps;

const uint16_t DEST_MASTER = 0;
const uint8_t  APP_CMD_GPS  = 0x11;

unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" LIDER (MKR) - GPS -> LoRa");
  Serial.println("=========================================");

  Serial1.begin(9600);            // GPS
  Serial2.begin(9600);            // LoRa (comando)
  pinPeripheral(6, PIO_SERCOM_ALT);
  pinPeripheral(7, PIO_SERCOM_ALT);
  delay(200);

  lora.begin(false);              // debug off (o GPS ja gera muita saida)
  Serial.print("Meu LocalID = "); Serial.println(lora.localId);
  Serial.println("-----------------------------------------");
}

void loop() {
  // Alimenta o parser NMEA continuamente
  while (Serial1.available()) gps.encode(Serial1.read());

  // Envia 1x por segundo
  if (millis() - lastSend >= 1000) {
    lastSend = millis();

    bool fix = gps.location.isValid();
    int32_t lat = fix ? (int32_t)(gps.location.lat() * 1e7) : 0;
    int32_t lon = fix ? (int32_t)(gps.location.lng() * 1e7) : 0;
    uint8_t sats = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;

    uint8_t p[10];
    p[0] = fix ? 0x01 : 0x00;
    p[1] = sats;
    p[2] = lat & 0xFF; p[3] = (lat >> 8) & 0xFF; p[4] = (lat >> 16) & 0xFF; p[5] = (lat >> 24) & 0xFF;
    p[6] = lon & 0xFF; p[7] = (lon >> 8) & 0xFF; p[8] = (lon >> 16) & 0xFF; p[9] = (lon >> 24) & 0xFF;

    lora.PrepareFrameCommand(DEST_MASTER, APP_CMD_GPS, p, 10);
    lora.SendPacket();

    Serial.print(">> TX  fix="); Serial.print(fix ? "SIM" : "NAO");
    Serial.print(" sats="); Serial.print(sats);
    Serial.print(" lat="); Serial.print(lat / 1e7, 6);
    Serial.print(" lon="); Serial.print(lon / 1e7, 6);
    Serial.print("  (NMEA chars="); Serial.print(gps.charsProcessed()); Serial.println(")");
  }
}
