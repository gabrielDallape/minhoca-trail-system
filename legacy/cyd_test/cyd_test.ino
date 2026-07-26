/*
 * CYD - TESTE combinado: valida fiacao do LoRa (le ID do modulo) + do GPS,
 * e mostra dados do lider se ele estiver transmitindo.
 *   LoRa: Serial1 RX=IO35, TX=IO22   |   GPS: Serial2 RX=IO27
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>

static LGFX tft;
LoRaMESH   lora(&Serial1);
TinyGPSPlus gps;

const uint8_t APP_CMD_GPS = 0x11;

bool loraOk = false; int loraId = -1;
bool linkOk = false, ldrFix = false; int ldrSats = 0; double ldrLat=0, ldrLon=0; uint32_t pkt=0;
unsigned long lastRx=0, lastDraw=0;

void line(int y, const char* s, uint16_t col) {
  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(6, y); tft.printf("%-40s", s);
}

void setup() {
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  Serial1.begin(9600, SERIAL_8N1, 35, 22);   // LoRa
  Serial2.begin(9600, SERIAL_8N1, 27, -1);   // GPS (so leitura)
  delay(200);

  // valida fiacao do LoRa: le o ID local do modulo (deve ser 0 = master)
  loraOk = lora.localread();
  loraId = lora.localId;
}

void loop() {
  // GPS
  while (Serial2.available()) gps.encode(Serial2.read());

  // LoRa (dados do lider)
  if (Serial1.available()) {
    uint16_t id=0xFFFF; uint8_t cmd=0, p[240], plen=0;
    if (lora.ReceivePacketCommand(&id,&cmd,p,&plen,150)) {
      if (cmd==APP_CMD_GPS && plen>=10) {
        lastRx=millis(); pkt++;
        ldrFix=p[0]&0x01; ldrSats=p[1];
        int32_t la=(int32_t)((uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)p[4]<<16)|((uint32_t)p[5]<<24));
        int32_t lo=(int32_t)((uint32_t)p[6]|((uint32_t)p[7]<<8)|((uint32_t)p[8]<<16)|((uint32_t)p[9]<<24));
        ldrLat=la/1e7; ldrLon=lo/1e7;
      }
    }
  }
  linkOk = (lastRx!=0) && (millis()-lastRx<3000);

  if (millis()-lastDraw > 300) {
    lastDraw = millis();
    char b[48];

    tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.setCursor(6,6); tft.print("TESTE CYD");

    snprintf(b,sizeof(b),"MODULO LoRa: %s id=%d", loraOk?"OK":"FALHA", loraId);
    line(34, b, loraOk?TFT_GREEN:TFT_RED);

    // EU (meu GPS)
    bool myFix = gps.location.isValid();
    int mySats = gps.satellites.isValid()? gps.satellites.value():0;
    snprintf(b,sizeof(b),"EU GPS: fix=%s sat=%d", myFix?"SIM":"NAO", mySats);
    line(66, b, myFix?TFT_GREEN:TFT_ORANGE);
    if (myFix) snprintf(b,sizeof(b),"  %.5f, %.5f", gps.location.lat(), gps.location.lng());
    else       snprintf(b,sizeof(b),"  NMEA rx=%lu", (unsigned long)gps.charsProcessed());
    line(90, b, TFT_WHITE);

    // LIDER (via LoRa)
    snprintf(b,sizeof(b),"LINK: %s  pkt=%lu", linkOk?"OK":"SEM", (unsigned long)pkt);
    line(130, b, linkOk?TFT_GREEN:TFT_RED);
    snprintf(b,sizeof(b),"LIDER: fix=%s sat=%d", (linkOk&&ldrFix)?"SIM":"NAO", ldrSats);
    line(154, b, (linkOk&&ldrFix)?TFT_GREEN:TFT_ORANGE);
    if (linkOk&&ldrFix) snprintf(b,sizeof(b),"  %.5f, %.5f", ldrLat, ldrLon);
    else                snprintf(b,sizeof(b),"  (lider sem fix/desligado)");
    line(178, b, TFT_WHITE);
  }
}
