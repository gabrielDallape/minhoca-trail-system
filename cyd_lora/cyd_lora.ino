/*
 * CYD (ESP32) - ETAPA 2: recebe os dados do LIDER por LoRa e mostra na tela.
 * -------------------------------------------------------------------------
 * LoRa master (ID 0) na Serial1 do ESP32:  RX=IO35, TX=IO22.
 *   Modulo LoRa: VCC->3.3V(CN1), GND->P3, RX_1(2)->IO22, TX_1(3)->IO35.
 * Recebe pacote de aplicacao cmd 0x11 (fix, sats, lat, lon) do lider (MKR).
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "LoRaMESH.h"

static LGFX tft;

LoRaMESH lora(&Serial1);
const uint8_t APP_CMD_GPS = 0x11;

bool     linkOk = false, ldrFix = false;
int      ldrSats = 0;
double   ldrLat = 0, ldrLon = 0;
uint32_t pkt = 0;
unsigned long lastRx = 0, lastDraw = 0;

void row(int y, const char* label, const char* value, uint16_t col) {
  tft.setTextSize(2);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(8, y);  tft.printf("%-10s", label);
  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(140, y); tft.printf("%-14s", value);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  tft.init();
  tft.setRotation(1);                 // 320x240 paisagem
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(8, 8); tft.print("SEGUIDOR - LoRa");

  Serial1.begin(9600, SERIAL_8N1, 35, 22);   // RX=IO35, TX=IO22
  delay(200);
  lora.begin(true);
  Serial.print("Meu LocalID = "); Serial.println(lora.localId);
}

void loop() {
  if (Serial1.available()) {
    uint16_t id = 0xFFFF; uint8_t cmd = 0, p[240], plen = 0;
    if (lora.ReceivePacketCommand(&id, &cmd, p, &plen, 300)) {
      if (cmd == APP_CMD_GPS && plen >= 10) {
        lastRx = millis(); pkt++;
        ldrFix  = p[0] & 0x01;
        ldrSats = p[1];
        int32_t lat = (int32_t)((uint32_t)p[2] | ((uint32_t)p[3]<<8) | ((uint32_t)p[4]<<16) | ((uint32_t)p[5]<<24));
        int32_t lon = (int32_t)((uint32_t)p[6] | ((uint32_t)p[7]<<8) | ((uint32_t)p[8]<<16) | ((uint32_t)p[9]<<24));
        ldrLat = lat / 1e7; ldrLon = lon / 1e7;
      }
    }
  }
  linkOk = (lastRx != 0) && (millis() - lastRx < 3000);

  if (millis() - lastDraw > 300) {
    lastDraw = millis();
    char b[24];
    row(50,  "LINK",   linkOk ? "OK" : "SEM", linkOk ? TFT_GREEN : TFT_RED);
    row(80,  "LIDER FIX", (linkOk && ldrFix) ? "SIM" : "NAO", (linkOk && ldrFix) ? TFT_GREEN : TFT_ORANGE);
    snprintf(b, sizeof(b), "%d", ldrSats);          row(110, "SATELITES", b, TFT_WHITE);
    snprintf(b, sizeof(b), "%.5f", ldrLat);         row(140, "LAT", b, TFT_YELLOW);
    snprintf(b, sizeof(b), "%.5f", ldrLon);         row(170, "LON", b, TFT_YELLOW);
    snprintf(b, sizeof(b), "%lu", (unsigned long)pkt); row(200, "PACOTES", b, TFT_CYAN);
  }
}
