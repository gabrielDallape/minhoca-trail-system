/*
 * CYD - LIDER (simples). Le o proprio GPS e SO ENVIA a posicao pro seguidor (GIGA).
 * Sem troca de papel, sem BO, sem receber. Fica QUIETO no radio ate travar o GPS
 * (o TX do LoRa atrapalha a busca); depois envia a cada 2s (protege o fix).
 *   GPS -> Serial2 RX=IO27 ; LoRa -> Serial1 RX=IO35 TX=IO22. Envia p/ DEST=1 (GIGA).
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

const uint16_t DEST_GIGA   = 1;
const uint8_t  APP_CMD_GPSH= 0x12;          // GPS + historico rolante
const int      BUFN        = 24;            // pontos por pacote (~120m de buraco coberto)
const float    STEP_M      = 5.0f;          // grava ponto do caminho a cada 5 m
const double   R_EARTH     = 6371000.0;
uint32_t txCount=0;
unsigned long lastTx=0, lastDraw=0, lastDbg=0;
double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0;
unsigned long myLastFix=0;
// buffer do proprio caminho (ring), com sequencia
struct Pt{int32_t la,lo;};
Pt buf[BUFN]; int bufCount=0, bufHead=0; uint16_t seqNewest=0;
double lastAddLat=0,lastAddLon=0; bool haveAdd=false;

double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH*2*atan2(sqrt(a),sqrt(1-a));
}
void addPt(){
  buf[bufHead].la=(int32_t)(myLat*1e7); buf[bufHead].lo=(int32_t)(myLon*1e7);
  bufHead=(bufHead+1)%BUFN; if(bufCount<BUFN) bufCount++;
  seqNewest++;                              // sequencia deste ponto (o mais novo)
  lastAddLat=myLat; lastAddLon=myLon; haveAdd=true;
}

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GREEN 0x07E0
#define C_RED   0xF800
#define C_YEL   0xFFE0
#define C_CYAN  0x07FF
#define C_GREY  0x8410
#define C_DGREY 0x39E7
#define C_ORANG 0xFD20

void drawSat(int cx,int cy,uint16_t col){
  tft.fillRect(cx-6,cy-9,12,18,col); tft.fillRect(cx-18,cy-6,9,12,col); tft.fillRect(cx+9,cy-6,9,12,col);
}

void setup(){
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(C_BLACK);
  Serial1.begin(9600,SERIAL_8N1,35,22);   // LoRa
  Serial2.begin(9600,SERIAL_8N1,27,-1);   // GPS
  delay(150);
  lora.localread();
  Serial.print("LoRa LocalID="); Serial.print(lora.localId);
  Serial.print("  UniqueID="); Serial.print(lora.localUniqueId);
  Serial.println(lora.localUniqueId>0 ? "  (LoRa OK)" : "  (LoRa SEM RESPOSTA - confere fiacao)");
  tft.setTextColor(C_CYAN,C_BLACK); tft.setTextSize(4); tft.setCursor(10,8); tft.print("LIDER");
}

void sendHist(){
  int n=bufCount;
  if(n<1){   // ainda sem ponto: manda so status (mantem link/flags)
    uint8_t p[5]={ (uint8_t)(myFix?1:0),(uint8_t)mySats,0,(uint8_t)(seqNewest&0xFF),(uint8_t)((seqNewest>>8)&0xFF) };
    lora.PrepareFrameCommand(DEST_GIGA,APP_CMD_GPSH,p,5); lora.SendPacket(); txCount++; return;
  }
  uint8_t p[5+BUFN*8];
  p[0]=myFix?0x01:0x00; p[1]=(uint8_t)mySats; p[2]=(uint8_t)n;
  p[3]=seqNewest&0xFF; p[4]=(seqNewest>>8)&0xFF;
  int idx=(bufHead-n+BUFN)%BUFN;                     // mais antigo dos n pontos
  for(int i=0;i<n;i++){
    int j=(idx+i)%BUFN; int off=5+i*8;
    int32_t la=buf[j].la, lo=buf[j].lo;
    p[off]=la&0xFF; p[off+1]=(la>>8)&0xFF; p[off+2]=(la>>16)&0xFF; p[off+3]=(la>>24)&0xFF;
    p[off+4]=lo&0xFF; p[off+5]=(lo>>8)&0xFF; p[off+6]=(lo>>16)&0xFF; p[off+7]=(lo>>24)&0xFF;
  }
  lora.PrepareFrameCommand(DEST_GIGA,APP_CMD_GPSH,p,5+n*8); lora.SendPacket(); txCount++;
}

void loop(){
  while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000);

  // grava ponto do proprio caminho quando andou >= STEP (pra remontar as curvas)
  if(myFix && (!haveAdd || haversine(lastAddLat,lastAddLon,myLat,myLon)>=STEP_M)) addPt();

  unsigned long now=millis();
  // Quieto ate travar; com fix, envia o historico a cada 2s
  if(myFix && now-lastTx>2000){ sendHist(); lastTx=now; }

  if(now-lastDraw>400){
    lastDraw=now; char b[48];
    drawSat(32,74, myFix?C_GREEN:C_RED);
    tft.setTextSize(3); tft.setTextColor(myFix?C_GREEN:C_RED,C_BLACK); tft.setCursor(58,58); snprintf(b,sizeof(b),"%-2d ",mySats); tft.print(b);
    tft.setTextSize(3);
    if(myFix){ tft.setTextColor(C_GREEN,C_BLACK); tft.setCursor(10,110); tft.print("ENVIANDO   "); }
    else     { tft.setTextColor(C_ORANG,C_BLACK); tft.setCursor(10,110); tft.print("PROCURANDO "); }
    tft.setTextSize(1); tft.setTextColor(C_GREY,C_BLACK); tft.setCursor(10,150); tft.print("VEL km/h");
    tft.setTextSize(3); tft.setTextColor(C_WHITE,C_BLACK); tft.setCursor(10,162); snprintf(b,sizeof(b),"%-3d ",(int)(mySpeed+0.5)); tft.print(b);
    tft.setTextSize(1); tft.setTextColor(myFix?C_WHITE:C_GREY,C_BLACK); tft.setCursor(10,205);
    if(myFix){ snprintf(b,sizeof(b),"%.5f, %.5f      ",myLat,myLon); tft.print(b); } else tft.print("aguardando o ceu...     ");
    tft.setTextColor(C_DGREY,C_BLACK); tft.setCursor(10,222); snprintf(b,sizeof(b),"enviados: %lu   ",(unsigned long)txCount); tft.print(b);
  }
  if(now-lastDbg>1000){
    Serial.print("LIDER | fix="); Serial.print(myFix?"S":"N"); Serial.print(" sat="); Serial.print(mySats);
    Serial.print(" tx="); Serial.println(txCount); lastDbg=now;
  }
}
