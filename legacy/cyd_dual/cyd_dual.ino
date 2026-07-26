/*
 * CYD - DUAL + alerta longe + BO (icone) + caminho de volta (mini-mapa c/ rastro).
 * Tela inteira desenhada num SPRITE unico (sem flicker). Papel derivado do pacote
 * do GIGA (cmd 0x11, bit2). BO por serial 'b' (touch resistivo ainda nao calibrado).
 *   GPS -> Serial2 RX=IO27 ; LoRa -> Serial1 RX=IO35 TX=IO22. GIGA=slave ID1 -> DEST=1.
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

// ---- papel / protocolo ----
bool cydIsLeader = false, myBO = false;
const uint16_t DEST_GIGA   = 1;
const uint8_t  APP_CMD_GPS = 0x11;
const float    FAR_THRESH_M= 500.0f;
unsigned long lastPosTx = 0, lastDbg = 0, lastTapMs = 0, rxAtMs = 0;
bool pendingTx = false;
uint32_t txCount = 0;
bool prevLeader = false, firstLoop = true;

// ---- layout ----
const int SCR_W=320, SCR_H=240, PANEL_W=72;
const int MW=SCR_W-PANEL_W, MH=SCR_H, CX=MW/2, CY=MH/2, PX=SCR_W-PANEL_W;
const float MPP=2.0f, MINI_MPP=4.0f;
const float MIN_SPD_COURSE=1.5f;
const double R=6371000.0;
const int mX=118, mY=34, mW=126, mH=176, mCX=mX+mW/2, mCY=mY+mH/2;   // mini-mapa (lider)
const int boX=PX+4, boY=SCR_H-58, boW=PANEL_W-8, boH=52;              // botao BO (grande)

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
#define C_DRED 0x6000

double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0;
unsigned long myLastFix=0;
double othLat=0,othLon=0; bool othFix=false,othBO=false; int othSats=0; uint32_t pkt=0;
unsigned long lastRx=0; bool linkOk=false;
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
void worldToScreenC(double lat,double lon,int cx,int cy,float mpp,int&sx,int&sy){
  double east=R*cos(radians(myLat))*radians(lon-myLon);
  double north=R*radians(lat-myLat);
  double h=radians(myHeading);
  double up=north*cos(h)+east*sin(h);
  double right=east*cos(h)-north*sin(h);
  sx=cx+(int)(right/mpp); sy=cy-(int)(up/mpp);
}
void addTrail(double lat,double lon){
  if(haveT && haversine(lastTLat,lastTLon,lat,lon)<3.0) return;
  trail[trailHead]={lat,lon}; trailHead=(trailHead+1)%TRAIL_MAX;
  if(trailN<TRAIL_MAX) trailN++; lastTLat=lat; lastTLon=lon; haveT=true;
}
void trailReset(){ trailN=0; trailHead=0; haveT=false; }

// ---- icones ----
void drawSatG(lgfx::LGFXBase &g,int cx,int cy,uint16_t col){
  g.fillRect(cx-4,cy-6,8,12,col); g.fillRect(cx-12,cy-4,6,8,col); g.fillRect(cx+6,cy-4,6,8,col);
}
void drawWarnTriG(lgfx::LGFXBase &g,int cx,int cy,int s,bool filled,uint16_t col){
  if(filled) g.fillTriangle(cx,cy-s,cx-s,cy+s,cx+s,cy+s,col);
  g.drawTriangle(cx,cy-s,cx-s,cy+s,cx+s,cy+s,filled?C_WHITE:col);
  uint16_t ic=filled?C_WHITE:col;
  g.fillRect(cx-1,cy-(int)(s*0.15),2,(int)(s*0.55),ic);
  g.fillCircle(cx,cy+(int)(s*0.6),1,ic);
}

void setup(){
  Serial.begin(115200);
  tft.init(); tft.setRotation(1); tft.fillScreen(C_BLACK);
  uint16_t calData[8]={549,3553,619,389,3613,3475,3618,378};   // calibracao do toque (medida)
  tft.setTouchCalibrate(calData);
  canvas.setColorDepth(8);
  canvas.createSprite(SCR_W,SCR_H);           // tela inteira (sem flicker)
  Serial1.begin(9600,SERIAL_8N1,35,22);
  Serial2.begin(9600,SERIAL_8N1,27,-1);
  delay(150);
  lora.localread();
}

void sendMyPos(){
  int32_t la=(int32_t)(myLat*1e7), lo=(int32_t)(myLon*1e7);
  uint8_t p[10];
  p[0]=(myFix?0x01:0x00)|(myBO?0x02:0x00); p[1]=(uint8_t)mySats;
  p[2]=la&0xFF; p[3]=(la>>8)&0xFF; p[4]=(la>>16)&0xFF; p[5]=(la>>24)&0xFF;
  p[6]=lo&0xFF; p[7]=(lo>>8)&0xFF; p[8]=(lo>>16)&0xFF; p[9]=(lo>>24)&0xFF;
  lora.PrepareFrameCommand(DEST_GIGA, APP_CMD_GPS, p, 10); lora.SendPacket(); txCount++;
}

void loop(){
  while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=MIN_SPD_COURSE) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000);

  int guard=0;
  while(Serial1.available() && guard++<8){
    uint16_t id=0xFFFF; uint8_t cmd=0,p[240],plen=0;
    if(!lora.ReceivePacketCommand(&id,&cmd,p,&plen,60)) break;
    if(cmd==APP_CMD_GPS && plen>=10){
      cydIsLeader = !(p[0]&0x04);
      lastRx=millis(); pkt++;
      othFix=p[0]&0x01; othBO=p[0]&0x02; othSats=p[1];
      int32_t la=(int32_t)((uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)p[4]<<16)|((uint32_t)p[5]<<24));
      int32_t lo=(int32_t)((uint32_t)p[6]|((uint32_t)p[7]<<8)|((uint32_t)p[8]<<16)|((uint32_t)p[9]<<24));
      if(othFix){ othLat=la/1e7; othLon=lo/1e7; addTrail(othLat,othLon); }
      rxAtMs=millis(); pendingTx=true;    // ouvi o GIGA -> vou responder no intervalo (sem colidir)
    }
  }
  linkOk=(lastRx!=0)&&(millis()-lastRx<4000);

  handleSerial();
  handleTouch();

  if(cydIsLeader!=prevLeader || firstLoop){ if(!cydIsLeader) trailReset(); prevLeader=cydIsLeader; firstLoop=false; }

  unsigned long now=millis();
  if(!myFix){
    // SEM fix: fico bem quieto (6s) pra o GPS travar — o TX do LoRa atrapalha a busca.
    if(now-lastPosTx>6000){ sendMyPos(); lastPosTx=now; pendingTx=false; }
  } else if(cydIsLeader){
    // LIDER com fix: preciso alimentar o mapa do seguidor (mais frequente)
    if(pendingTx && now-rxAtMs>=450){ sendMyPos(); lastPosTx=now; pendingTx=false; }
    else if(now-lastPosTx>1500){ sendMyPos(); lastPosTx=now; }
  } else {
    // SEGUIDOR com fix: transmito raro (so p/ o lider ter distancia) e protejo o meu proprio fix
    if(now-lastPosTx>3000){ sendMyPos(); lastPosTx=now; pendingTx=false; }
  }

  if(now-lastDraw>250){
    lastDraw=now;
    canvas.fillScreen(C_BLACK);
    if(cydIsLeader) drawLeader(); else { drawMap(); drawPanel(); }
    drawBObtn();
    canvas.pushSprite(0,0);
  }
  if(now-lastDbg>1000){ debugSerial(); lastDbg=now; }
}

// ---- painel comum (satelite, vel, dist) desenhado no canvas, area direita ----
void drawSidePanel(const char* title, bool distToLeaderLabel){
  canvas.drawFastVLine(PX,0,SCR_H,C_DGREY);
  canvas.setTextSize(1); canvas.setTextColor(C_CYAN,C_BLACK); canvas.setCursor(PX+4,6); canvas.print(title);
  canvas.fillCircle(PX+PANEL_W-9,9,5, linkOk?C_GREEN:C_RED);           // link dot
  drawSatG(canvas, PX+12, 34, myFix?C_GREEN:C_RED);                    // satelite
  canvas.setTextSize(2); canvas.setTextColor(myFix?C_GREEN:C_RED,C_BLACK); canvas.setCursor(PX+30,26); canvas.printf("%-2d",mySats);
  canvas.setTextSize(1); canvas.setTextColor(C_GREY,C_BLACK); canvas.setCursor(PX+4,58); canvas.print("km/h");
  canvas.setTextSize(3); canvas.setTextColor(C_WHITE,C_BLACK); canvas.setCursor(PX+4,68); canvas.printf("%-3d",(int)(mySpeed+0.5));
  canvas.setTextSize(1); canvas.setTextColor(C_GREY,C_BLACK); canvas.setCursor(PX+4,108); canvas.print("DIST");
  if(myFix&&linkOk&&othFix){ double d=haversine(myLat,myLon,othLat,othLon); bool far=d>FAR_THRESH_M;
    canvas.setTextColor(far?C_RED:C_YEL,C_BLACK); canvas.setTextSize(3); canvas.setCursor(PX+4,120);
    if(d>=1000) canvas.printf("%.1f",d/1000.0); else canvas.printf("%-3d",(int)d); }
  else { canvas.setTextSize(3); canvas.setTextColor(C_GREY,C_BLACK); canvas.setCursor(PX+4,120); canvas.print("--"); }
  if(othBO && ((millis()/300)%2)){ canvas.fillRect(PX+2,140,PANEL_W-4,30,C_RED); drawWarnTriG(canvas,PX+13,155,9,true,C_RED);
    canvas.setTextColor(C_WHITE,C_RED); canvas.setTextSize(1); canvas.setCursor(PX+26,149); canvas.print("OUT"); canvas.setCursor(PX+26,160); canvas.print("BO!"); }
}
void drawPanel(){ drawSidePanel("SEGUIDOR", true); }

void drawBObtn(){
  canvas.fillRect(boX,boY,boW,boH,myBO?C_DRED:C_BLACK);
  canvas.drawRect(boX,boY,boW,boH,myBO?C_RED:C_YEL);
  canvas.drawRect(boX+1,boY+1,boW-2,boH-2,myBO?C_RED:C_YEL);
  drawWarnTriG(canvas, boX+boW/2, boY+boH/2, 20, myBO, myBO?C_RED:C_YEL);   // icone grande
}

// ---- modo seguidor: mapa (area esquerda) ----
void drawMap(){
  canvas.fillRect(0,0,MW,MH,C_BG);
  if(!myFix){
    canvas.setTextColor(C_ORANG,C_BG); canvas.setTextSize(2); canvas.setCursor(14,CY-20); canvas.print("SEM FIX (EU)");
    canvas.setTextColor(C_GREY,C_BG); canvas.setTextSize(1); canvas.setCursor(14,CY+4); canvas.print("aguardando o ceu...");
    return;
  }
  int psx=-9999,psy=-9999;
  for(int k=0;k<trailN;k++){
    int idx=(trailHead-trailN+k+TRAIL_MAX)%TRAIL_MAX; int sx,sy;
    worldToScreenC(trail[idx].lat,trail[idx].lon,CX,CY,MPP,sx,sy);
    if(sx>=0&&sx<MW&&sy>=0&&sy<MH){ if(psx>-9999) canvas.drawLine(psx,psy,sx,sy,C_CYAN); canvas.fillCircle(sx,sy,1,C_CYAN); psx=sx;psy=sy; } else psx=-9999;
  }
  if(linkOk&&othFix){
    int lx,ly; worldToScreenC(othLat,othLon,CX,CY,MPP,lx,ly);
    uint16_t oc = othBO ? C_RED : C_ORANG;
    if(lx>=0&&lx<MW&&ly>=0&&ly<MH){
      canvas.drawLine(CX,CY,lx,ly, othBO?C_RED:C_DGREY);
      canvas.fillCircle(lx,ly,6,oc); canvas.drawCircle(lx,ly,9,C_WHITE);
      if(othBO){ canvas.drawCircle(lx,ly,12,C_RED); canvas.fillRect(lx-1,ly-4,2,6,C_WHITE); canvas.fillCircle(lx,ly+5,1,C_WHITE); }
    } else { float ax=lx-CX,ay=ly-CY,L=sqrtf(ax*ax+ay*ay); if(L<1)L=1; ax/=L;ay/=L;
      int ex=constrain(CX+(int)(ax*(CX-14)),12,MW-12); int ey=constrain(CY+(int)(ay*(CY-14)),12,MH-12);
      canvas.fillCircle(ex,ey,6,oc); }
  }
  canvas.fillTriangle(CX,CY-10,CX-7,CY+8,CX+7,CY+8,C_GREEN); canvas.drawTriangle(CX,CY-10,CX-7,CY+8,CX+7,CY+8,C_WHITE);
  float h=radians(myHeading),ux=-sin(h),uy=cos(h);
  canvas.drawLine(18,20,18+(int)(ux*12),20-(int)(uy*12),C_RED);
  canvas.setTextColor(C_RED); canvas.setTextSize(1); canvas.setCursor(18+(int)(ux*15)-2,20-(int)(uy*15)-3); canvas.print("N");
}

// ---- modo lider: info (esquerda) + mini-mapa do seguidor ----
void drawLeader(){
  canvas.setTextSize(2); canvas.setTextColor(C_GREEN,C_BLACK); canvas.setCursor(6,6); canvas.print("LIDER");
  canvas.fillCircle(96,14,5, linkOk?C_GREEN:C_RED);
  drawSatG(canvas, 18, 46, myFix?C_GREEN:C_RED);
  canvas.setTextSize(3); canvas.setTextColor(myFix?C_GREEN:C_RED,C_BLACK); canvas.setCursor(38,34); canvas.printf("%-2d",mySats);
  canvas.setTextSize(1); canvas.setTextColor(C_GREY,C_BLACK); canvas.setCursor(6,74); canvas.print("km/h");
  canvas.setTextSize(4); canvas.setTextColor(C_WHITE,C_BLACK); canvas.setCursor(6,86); canvas.printf("%-3d",(int)(mySpeed+0.5));
  canvas.setTextSize(1); canvas.setTextColor(C_GREY,C_BLACK); canvas.setCursor(6,132); canvas.print("DIST SEG");
  if(myFix&&linkOk&&othFix){ double d=haversine(myLat,myLon,othLat,othLon); bool far=d>FAR_THRESH_M;
    canvas.setTextColor(far?C_RED:C_YEL,C_BLACK); canvas.setTextSize(4); canvas.setCursor(6,144);
    if(d>=1000) canvas.printf("%.1fk",d/1000.0); else canvas.printf("%dm",(int)d); }
  else { canvas.setTextSize(4); canvas.setTextColor(C_GREY,C_BLACK); canvas.setCursor(6,144); canvas.print("--"); }
  drawMiniMap();
  if(othBO && ((millis()/300)%2)){ canvas.fillRect(2,196,150,40,C_RED); drawWarnTriG(canvas,18,216,10,true,C_RED);
    canvas.setTextColor(C_WHITE,C_RED); canvas.setTextSize(2); canvas.setCursor(34,206); canvas.print("SEG BO"); }
}
void drawMiniMap(){
  canvas.setTextSize(1); canvas.setTextColor(C_CYAN,C_BLACK); canvas.setCursor(mX,mY-10); canvas.print("SEG rastro");
  canvas.drawRect(mX,mY,mW,mH,C_DGREY);
  int psx=-9999,psy=-9999;
  for(int k=0;k<trailN;k++){
    int idx=(trailHead-trailN+k+TRAIL_MAX)%TRAIL_MAX; int sx,sy;
    worldToScreenC(trail[idx].lat,trail[idx].lon,mCX,mCY,MINI_MPP,sx,sy);
    if(sx>=mX&&sx<mX+mW&&sy>=mY&&sy<mY+mH){ if(psx>-9999) canvas.drawLine(psx,psy,sx,sy,C_CYAN); canvas.fillCircle(sx,sy,1,C_CYAN); psx=sx;psy=sy; } else psx=-9999;
  }
  if(linkOk&&othFix&&myFix){
    int lx,ly; worldToScreenC(othLat,othLon,mCX,mCY,MINI_MPP,lx,ly);
    uint16_t oc = othBO ? C_RED : C_ORANG;
    if(lx>=mX&&lx<mX+mW&&ly>=mY&&ly<mY+mH){
      canvas.drawLine(mCX,mCY,lx,ly, othBO?C_RED:C_DGREY);   // caminho ate o seguidor (vermelho no BO)
      canvas.fillCircle(lx,ly,5,oc); canvas.drawCircle(lx,ly,8,C_WHITE);
      if(othBO){ canvas.drawCircle(lx,ly,11,C_RED); canvas.fillRect(lx-1,ly-4,2,6,C_WHITE); canvas.fillCircle(lx,ly+5,1,C_WHITE); }
    } else { float dx=lx-mCX,dy=ly-mCY,L=sqrtf(dx*dx+dy*dy); if(L<1)L=1; dx/=L;dy/=L;
      int ex=constrain(mCX+(int)(dx*(mW/2-10)),mX+8,mX+mW-8); int ey=constrain(mCY+(int)(dy*(mH/2-10)),mY+8,mY+mH-8);
      canvas.fillCircle(ex,ey,5,oc); }
  }
  canvas.fillTriangle(mCX,mCY-8,mCX-5,mCY+6,mCX+5,mCY+6,C_GREEN);
}

void handleTouch(){
  int32_t x,y;
  if(tft.getTouch(&x,&y)){
    if(millis()-lastTapMs>500){
      if(x>=boX && x<=boX+boW && y>=boY && y<=boY+boH){ myBO=!myBO; lastTapMs=millis(); }
    }
  }
}
void handleSerial(){
  while(Serial.available()){ char c=Serial.read(); if(c=='b'||c=='B'){ myBO=!myBO; Serial.print(">>> meu BO -> "); Serial.println(myBO?"LIGADO":"desligado"); } }
}
void debugSerial(){
  double dist=(myFix&&linkOk&&othFix)?haversine(myLat,myLon,othLat,othLon):-1;
  Serial.print("PAPEL="); Serial.print(cydIsLeader?"LIDER":"SEGUIDOR");
  Serial.print(" myBO="); Serial.print(myBO?"1":"0");
  Serial.print(" | EUfix="); Serial.print(myFix?"S":"N"); Serial.print(" sat="); Serial.print(mySats);
  Serial.print(" | LINK="); Serial.print(linkOk?"OK":"--");
  Serial.print(" rx="); Serial.print(pkt); Serial.print(" tx="); Serial.print(txCount);
  Serial.print(" | OUTfix="); Serial.print(othFix?"S":"N"); Serial.print(" BO="); Serial.print(othBO?"1":"0");
  Serial.print(" dist="); if(dist>=0) Serial.print(dist,0); else Serial.print("--");
  Serial.println();
}
