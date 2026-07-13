/*
 * CYD - SEGUIDOR (mapa Waze REAL): meu GPS no centro + lider pelo LoRa.
 * -------------------------------------------------------------------------
 *   GPS  -> Serial2 RX=IO27 (MKR GPS Shield: dado sai no pino 13 -> IO27)
 *   LoRa -> Serial1 RX=IO35 TX=IO22 (master ID 0). Recebe cmd 0x11 do lider.
 *   EU no centro, mapa HEADING-UP (meu course), rastro do lider, seta+dist.
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>
#include <math.h>

static LGFX tft;
LGFX_Sprite canvas(&tft);
LoRaMESH   lora(&Serial1);
TinyGPSPlus gps;

// ---- layout ----
const int SCR_W=320, SCR_H=240, PANEL_W=72;
const int MW=SCR_W-PANEL_W, MH=SCR_H, CX=MW/2, CY=MH/2, PX=SCR_W-PANEL_W;
const float MPP=2.0f;                 // metros por pixel
const uint8_t APP_CMD_GPS=0x11;
const float MIN_SPD_COURSE=1.5f;      // km/h min p/ confiar no rumo
const double R=6371000.0;

// ---- cores ----
#define C_BG 0x0841
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GREEN 0x07E0
#define C_CYAN 0x07FF
#define C_ORANG 0xFD20
#define C_YEL 0xFFE0
#define C_RED 0xF800
#define C_GREY 0x8410
#define C_DGREY 0x39E7

// ---- meu estado ----
double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0;
unsigned long myLastFix=0;
// ---- lider ----
double ldrLat=0,ldrLon=0; bool ldrFix=false; int ldrSats=0; uint32_t pkt=0;
unsigned long lastRx=0; bool linkOk=false;
// ---- rastro do lider ----
struct Geo{double lat,lon;}; const int TRAIL_MAX=400;
Geo trail[TRAIL_MAX]; int trailN=0,trailHead=0; double lastTLat=0,lastTLon=0; bool haveT=false;
unsigned long lastDraw=0;

double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R*2*atan2(sqrt(a),sqrt(1-a));
}
double bearingTo(double la1,double lo1,double la2,double lo2){
  double y=sin(radians(lo2-lo1))*cos(radians(la2));
  double x=cos(radians(la1))*sin(radians(la2))-sin(radians(la1))*cos(radians(la2))*cos(radians(lo2-lo1));
  double b=degrees(atan2(y,x)); return b<0?b+360:b;
}
void worldToScreen(double lat,double lon,int&sx,int&sy){
  double east=R*cos(radians(myLat))*radians(lon-myLon);
  double north=R*radians(lat-myLat);
  double h=radians(myHeading);
  double up=north*cos(h)+east*sin(h);
  double right=east*cos(h)-north*sin(h);
  sx=CX+(int)(right/MPP); sy=CY-(int)(up/MPP);
}
void addTrail(double lat,double lon){
  if(haveT && haversine(lastTLat,lastTLon,lat,lon)<3.0) return;
  trail[trailHead]={lat,lon}; trailHead=(trailHead+1)%TRAIL_MAX;
  if(trailN<TRAIL_MAX) trailN++; lastTLat=lat; lastTLon=lon; haveT=true;
}

void setup(){
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(C_BLACK);
  canvas.setColorDepth(8);
  canvas.createSprite(MW,MH);
  Serial1.begin(9600,SERIAL_8N1,35,22);   // LoRa
  Serial2.begin(9600,SERIAL_8N1,27,-1);   // GPS
  delay(150);
  lora.localread();                        // confirma master (id 0)
  drawPanelStatic();
}

void drawPanelStatic(){
  tft.fillRect(PX,0,PANEL_W,SCR_H,C_BLACK);
  tft.drawFastVLine(PX,0,SCR_H,C_DGREY);
  tft.setTextColor(C_CYAN,C_BLACK); tft.setTextSize(1);
  tft.setCursor(PX+4,6); tft.print("SEGUIDOR");
}
void pv(int y,const char*label,const char*val,uint16_t col){
  tft.setTextSize(1); tft.setTextColor(C_GREY,C_BLACK); tft.setCursor(PX+4,y); tft.print(label);
  tft.setTextSize(2); tft.setTextColor(col,C_BLACK); tft.setCursor(PX+4,y+10); tft.printf("%-5s",val);
}

void loop(){
  // GPS proprio
  while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=MIN_SPD_COURSE) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000);

  // LoRa (lider)
  if(Serial1.available()){
    uint16_t id=0xFFFF; uint8_t cmd=0,p[240],plen=0;
    if(lora.ReceivePacketCommand(&id,&cmd,p,&plen,120)){
      if(cmd==APP_CMD_GPS && plen>=10){
        lastRx=millis(); pkt++;
        ldrFix=p[0]&0x01; ldrSats=p[1];
        int32_t la=(int32_t)((uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)p[4]<<16)|((uint32_t)p[5]<<24));
        int32_t lo=(int32_t)((uint32_t)p[6]|((uint32_t)p[7]<<8)|((uint32_t)p[8]<<16)|((uint32_t)p[9]<<24));
        if(ldrFix){ ldrLat=la/1e7; ldrLon=lo/1e7; addTrail(ldrLat,ldrLon); }
      }
    }
  }
  linkOk=(lastRx!=0)&&(millis()-lastRx<3000);

  if(millis()-lastDraw>250){ lastDraw=millis(); drawMap(); drawPanel(); }
}

void drawMap(){
  canvas.fillScreen(C_BG);
  if(!myFix){
    canvas.setTextColor(C_ORANG,C_BG); canvas.setTextSize(2);
    canvas.setCursor(14,CY-20); canvas.print("SEM FIX (EU)");
    canvas.setTextColor(C_GREY,C_BG); canvas.setTextSize(1);
    canvas.setCursor(14,CY+4); canvas.print("aguardando o ceu...");
    canvas.pushSprite(0,0); return;
  }
  // rastro
  int psx=-9999,psy=-9999;
  for(int k=0;k<trailN;k++){
    int idx=(trailHead-trailN+k+TRAIL_MAX)%TRAIL_MAX; int sx,sy;
    worldToScreen(trail[idx].lat,trail[idx].lon,sx,sy);
    if(sx>=0&&sx<MW&&sy>=0&&sy<MH){ if(psx>-9999) canvas.drawLine(psx,psy,sx,sy,C_CYAN); canvas.fillCircle(sx,sy,1,C_CYAN); }
    psx=sx; psy=sy;
  }
  // lider
  if(linkOk&&ldrFix){
    int lx,ly; worldToScreen(ldrLat,ldrLon,lx,ly);
    if(lx>=0&&lx<MW&&ly>=0&&ly<MH){
      canvas.drawLine(CX,CY,lx,ly,C_DGREY); canvas.fillCircle(lx,ly,5,C_ORANG); canvas.drawCircle(lx,ly,8,C_WHITE);
    }else{
      float ax=lx-CX,ay=ly-CY,L=sqrtf(ax*ax+ay*ay); if(L<1)L=1; ax/=L; ay/=L;
      int ex=CX+(int)(ax*(CX-14)); ex=constrain(ex,12,MW-12);
      int ey=CY+(int)(ay*(CY-14)); ey=constrain(ey,12,MH-12);
      canvas.fillCircle(ex,ey,6,C_ORANG);
    }
  }
  // eu (centro)
  canvas.fillTriangle(CX,CY-10,CX-7,CY+8,CX+7,CY+8,C_GREEN);
  canvas.drawTriangle(CX,CY-10,CX-7,CY+8,CX+7,CY+8,C_WHITE);
  // norte
  float h=radians(myHeading),ux=-sin(h),uy=cos(h);
  canvas.drawLine(18,20,18+(int)(ux*12),20-(int)(uy*12),C_RED);
  canvas.setTextColor(C_RED); canvas.setTextSize(1);
  canvas.setCursor(18+(int)(ux*15)-2,20-(int)(uy*15)-3); canvas.print("N");

  canvas.pushSprite(0,0);
}

void drawPanel(){
  char b[16];
  pv(28,"LINK", linkOk?"OK":"SEM", linkOk?C_GREEN:C_RED);
  pv(60,"EU fix", myFix?"SIM":"NAO", myFix?C_GREEN:C_ORANG);
  pv(92,"LID fix",(linkOk&&ldrFix)?"SIM":"NAO",(linkOk&&ldrFix)?C_GREEN:C_ORANG);
  if(myFix&&linkOk&&ldrFix){
    double d=haversine(myLat,myLon,ldrLat,ldrLon);
    if(d>=1000) snprintf(b,sizeof(b),"%.1fk",d/1000.0); else snprintf(b,sizeof(b),"%dm",(int)d);
    pv(124,"DIST",b,C_YEL);
    snprintf(b,sizeof(b),"%d",(int)bearingTo(myLat,myLon,ldrLat,ldrLon)); pv(156,"RUMO",b,C_CYAN);
  }else{ pv(124,"DIST","--",C_GREY); pv(156,"RUMO","--",C_GREY); }
  snprintf(b,sizeof(b),"%d/%d",mySats,ldrSats); pv(188,"SAT",b,C_WHITE);
  snprintf(b,sizeof(b),"%lu",(unsigned long)pkt); pv(212,"PKT",b,C_DGREY);
}
