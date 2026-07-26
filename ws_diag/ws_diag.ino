/*
 * ws_diag - DIAGNOSTICO da tela Waveshare ESP32-S3-Touch-LCD-7B
 * ---------------------------------------------------------------------------
 * Objetivo: validar a fiacao do LoRa e do GPS na tela nova SEM depender da
 * tela (so log pela USB). Mesmo padrao do giga_diag/cyd_gps_ser que ja
 * funcionaram. Assim que os dois aparecerem vivos aqui, a fiacao esta certa
 * e partimos pro port da UI (LovyanGFX / painel RGB).
 *
 * Placa: Waveshare ESP32-S3-Touch-LCD-7B (ESP32-S3, painel RGB 1024x600).
 *
 * ---- LIGACAO (ver waveshare-7b-pinout na memoria) ----
 * LoRa Radioenge (UART de comando)  -> header UART-TTL (switch de cima na
 *   posicao UART, NAO RS485):
 *     modulo VCC        -> placa 3V3
 *     modulo GND        -> placa GND
 *     modulo TX_1 (p3)  -> GPIO15   (modulo transmite -> ESP recebe / RX)
 *     modulo RX_1 (p2)  -> GPIO16   (ESP transmite -> modulo recebe / TX)
 *   Se der [LoRa] FALHA, inverta os 2 fios de dados (15<->16); e' 3,3V, nao queima.
 *
 * GPS NEO-7M (so o dado, TXD) -> header "GP6":
 *     GPS VCC  -> placa 3V3
 *     GPS GND  -> placa GND
 *     GPS TXD  -> GPIO6           (RXD/PPS nao liga)
 *
 * Energia: alimentar a PLACA com fonte 5V/2A (display puxa corrente).
 * Antena no LoRa ANTES de energizar. Afastar antena do LoRa da do GPS.
 *
 * ---- GRAVACAO ----
 * Grave pela porta USB-C rotulada "UART" (chip USB-serial). No Arduino IDE:
 *   Placa: "ESP32S3 Dev Module"
 *   USB CDC On Boot: DISABLED   (senao o log sai pelo USB nativo 19/20, nao pela porta UART)
 *   PSRAM: OPI PSRAM ; Flash: 16MB
 * Abra o Serial Monitor a 115200.
 */
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>

// --- Pinos (Waveshare 7B) ---
static const int LORA_RX = 15;   // <- modulo TX_1 (p3)
static const int LORA_TX = 16;   // -> modulo RX_1 (p2)
static const int GPS_RX  = 6;    // <- GPS TXD  (header GP6)

LoRaMESH lora(&Serial1);         // LoRa na UART de comando (Serial1)
TinyGPSPlus gps;                 // GPS na Serial2
unsigned long lastTry = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 3000)) { }

  Serial.println();
  Serial.println("========================================================");
  Serial.println(" ws_diag - Waveshare ESP32-S3-Touch-LCD-7B (LoRa + GPS)");
  Serial.println("========================================================");

  // LoRa: Serial1 nos pinos 15/16, 9600 8N1 (abrir ANTES do lora.begin)
  Serial1.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);
  // GPS: Serial2, so RX no GPIO6
  Serial2.begin(9600, SERIAL_8N1, GPS_RX, -1);
  delay(200);

  lora.begin(true);              // debug=true -> imprime TX/RX cru do LoRa
  Serial.print("LoRa LocalID  : "); Serial.println(lora.localId);
  Serial.print("LoRa UniqueID : "); Serial.println(lora.localUniqueId);
  Serial.println("--------------------------------------------------------");
  Serial.println("(esperado: um UniqueID != 0 = LoRa vivo; NMEA subindo = GPS vivo)");
}

void loop() {
  // Alimenta o parser do GPS continuamente e ecoa o NMEA cru.
  while (Serial2.available()) {
    char c = Serial2.read();
    gps.encode(c);
    Serial.write(c);
  }

  // A cada 3s: re-le o LoRa e imprime status do GPS.
  if (millis() - lastTry > 3000) {
    lastTry = millis();

    if (lora.localread()) {
      Serial.print("\n[LoRa OK]  LocalID="); Serial.print(lora.localId);
      Serial.print("  UniqueID=");           Serial.print(lora.localUniqueId);
    } else {
      Serial.print("\n[LoRa FALHA] sem resposta -> confira 15<->16 cruzados, VCC(3V3)/GND, switch em UART, baud 9600");
    }

    Serial.printf("  |  [GPS] NMEA_chars=%lu sats=%d fix=%d",
      gps.charsProcessed(),
      gps.satellites.isValid() ? (int)gps.satellites.value() : -1,
      gps.location.isValid() ? 1 : 0);
    if (gps.location.isValid())
      Serial.printf(" %.6f,%.6f", gps.location.lat(), gps.location.lng());
    Serial.println();
  }
}
