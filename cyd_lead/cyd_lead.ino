/*
 * CYD - LIDER. Le o proprio GPS, ENVIA a posicao pro seguidor (GIGA) por LoRa E
 * plota o proprio mapa (voce no centro + o caminho que vai tracando).
 * Sem troca de papel, sem receber. Fica QUIETO no radio ate travar o GPS
 * (o TX do LoRa atrapalha a busca); depois envia a cada 2s (protege o fix).
 *   GPS -> Serial2 RX=IO27 ; LoRa -> Serial1 RX=IO35 TX=IO22. Envia p/ DEST=1 (GIGA).
 *   Tela paisagem 320x240: barra de status fina no topo + mapa (heading-up).
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
const uint8_t  APP_CMD_GPSH= 0x12;          // GPS + historico rolante (flags bit0=fix, bit1=alerta)
const uint8_t  APP_CMD_ALERT= 0x13;         // alerta recebido do seguidor
const int      BUFN        = 26;            // pontos por pacote (~130m; payload 213B < 232 max)
const float    STEP_M      = 5.0f;          // grava ponto do caminho a cada 5 m
const double   R_EARTH     = 6371000.0;
const float    MIN_SPD_COURSE = 1.5f;
const double   GAP_M       = 40.0;
const bool     HEADING_UP  = true;
uint32_t txCount=0;
unsigned long lastTx=0, lastDraw=0, lastDbg=0;
double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0;
unsigned long myLastFix=0;
// buffer do proprio caminho (ring) p/ TX, com sequencia
struct Pt{int32_t la,lo;};
Pt buf[BUFN]; int bufCount=0, bufHead=0; uint16_t seqNewest=0;
double lastAddLat=0,lastAddLon=0; bool haveAdd=false;
// trilha (maior) so pra desenhar o mapa
const int TRAIL_L=300;
struct Geo{double lat,lon;}; Geo trail[TRAIL_L]; int trailN=0,trailHead=0;
float mapMPP=2.0f;
// alerta: myAlert = eu (lider) apertei ; rxAlert = o seguidor me alertou
unsigned long myAlertUntil=0, rxAlertUntil=0; bool touchWasDown=false;
uint16_t touchCal[8]={549,3553,619,389,3613,3475,3618,378};   // calibracao ja feita (NAO mexer)

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
  // grava tambem na trilha do mapa
  trail[trailHead].lat=myLat; trail[trailHead].lon=myLon;
  trailHead=(trailHead+1)%TRAIL_L; if(trailN<TRAIL_L) trailN++;
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
#define C_BG    0x08C5   // navy (cara de mapa)
#define C_RING  0x29C9   // aneis fracos
#define C_PANEL 0x10A2

int SCR_W, SCR_H, STATUS_H=30, PLOT_CX, PLOT_CY;

uint16_t scaleColor(uint16_t c, float f){
  if(f<0)f=0; if(f>1)f=1;
  int r=(c>>11)&0x1F, g=(c>>5)&0x3F, b=c&0x1F;
  r=(int)(r*f+0.5f); g=(int)(g*f+0.5f); b=(int)(b*f+0.5f);
  return (uint16_t)((r<<11)|(g<<5)|b);
}
void worldToScreen(double lat,double lon,int&sx,int&sy){
  double east=R_EARTH*cos(radians(myLat))*radians(lon-myLon);
  double north=R_EARTH*radians(lat-myLat);
  double up,right;
  if(HEADING_UP){ double h=radians(myHeading); up=north*cos(h)+east*sin(h); right=east*cos(h)-north*sin(h); }
  else { up=north; right=east; }
  sx=PLOT_CX+(int)(right/mapMPP); sy=PLOT_CY-(int)(up/mapMPP);
}

void drawSat(int cx,int cy,uint16_t col){
  tft.fillRect(cx-4,cy-6,8,12,col); tft.fillRect(cx-12,cy-4,6,8,col); tft.fillRect(cx+6,cy-4,6,8,col);
}
void drawMe(){
  int cx=PLOT_CX,cy=PLOT_CY;
  tft.fillCircle(cx,cy,16,scaleColor(C_GREEN,0.16f));
  tft.fillCircle(cx,cy,10,scaleColor(C_GREEN,0.30f));
  tft.fillTriangle(cx,cy-13,cx-10,cy+11,cx+10,cy+11,C_GREEN);
  tft.fillTriangle(cx,cy-3,cx-5,cy+9,cx+5,cy+9,C_BG);
  tft.drawTriangle(cx,cy-13,cx-10,cy+11,cx+10,cy+11,C_WHITE);
}
// simbolo de alerta (triangulo com "!") - sem texto
void drawWarnTri(int cx,int cy,int r,uint16_t tri,uint16_t ex){
  tft.fillTriangle(cx,cy-r, cx-r,cy+r*4/5, cx+r,cy+r*4/5, tri);
  tft.fillRect(cx-2,cy-r/4,4,r*55/100,ex);
  tft.fillRect(cx-2,cy+r*70/100-2,4,4,ex);
}
// botao de alerta redondo (FAB) no canto inferior direito
void drawFab(){
  bool active=millis()<myAlertUntil; bool blink=(millis()/300)%2==0;
  int fx=SCR_W-34, fy=SCR_H-34, fr=27;
  tft.fillCircle(fx,fy,fr,(active&&blink)?C_RED:C_BG);
  tft.drawCircle(fx,fy,fr,C_RED); tft.drawCircle(fx,fy,fr-1,C_RED);
  drawWarnTri(fx,fy,15,(active&&blink)?C_WHITE:C_RED,(active&&blink)?C_RED:C_BG);
}
// overlay quando o SEGUIDOR me alertou: borda vermelha piscando + simbolo grande
void drawRxAlert(){
  if(millis()>=rxAlertUntil) return;
  if((millis()/300)%2) return;
  for(int t=0;t<6;t++) tft.drawRect(t,STATUS_H+t,SCR_W-1-2*t,SCR_H-STATUS_H-1-2*t,C_RED);
  drawWarnTri(PLOT_CX,STATUS_H+42,26,C_RED,C_BG);
}
void drawNorth(){ int nx=24,ny=STATUS_H+18; double h=HEADING_UP?radians(myHeading):0; float ux=-sin(h),uy=cos(h);
  tft.drawLine(nx,ny,nx+(int)(ux*14),ny-(int)(uy*14),C_RED);
  tft.setTextSize(1); tft.setTextColor(C_RED); tft.setCursor(nx+(int)(ux*17)-3,ny-(int)(uy*17)-4); tft.print("N"); }
void drawRings(){
  static const int rm[]={25,50,100,200,400,800};
  for(unsigned i=0;i<sizeof(rm)/sizeof(rm[0]);i++){
    int rp=(int)(rm[i]/mapMPP);
    if(rp>=22 && rp<=SCR_H) tft.drawCircle(PLOT_CX,PLOT_CY,rp,C_RING);
  }
}
void drawMap(){
  tft.fillRect(0,STATUS_H,SCR_W,SCR_H-STATUS_H,C_BG);
  if(!myFix){
    tft.setTextSize(2); tft.setTextColor(C_ORANG); tft.setCursor(30,PLOT_CY-16); tft.print("PROCURANDO CEU...");
    drawMe(); drawNorth(); return;
  }
  // auto-fit: enquadra toda a trilha do proprio caminho
  double maxd=0;
  for(int k=0;k<trailN;k++){ int idx=(trailHead-trailN+k+TRAIL_L)%TRAIL_L; double d=haversine(myLat,myLon,trail[idx].lat,trail[idx].lon); if(d>maxd)maxd=d; }
  double halfExt=0.42*min(SCR_W,SCR_H-STATUS_H);
  double want=(maxd>5 && halfExt>1)? maxd/halfExt : 1.0;
  if(want<0.5)want=0.5; if(want>30)want=30;
  mapMPP=mapMPP*0.8f+(float)want*0.2f; if(mapMPP<0.5f)mapMPP=0.5f; if(mapMPP>30.0f)mapMPP=30.0f;
  drawRings();
  // caminho: tudo ciano (ja percorrido), recente vivo / antigo desbota. Salto>GAP nao liga a reta.
  int psx=-1,psy=-1; double plat=0,plon=0; bool havePrev=false;
  for(int k=0;k<trailN;k++){
    int idx=(trailHead-trailN+k+TRAIL_L)%TRAIL_L;
    double la=trail[idx].lat, lo=trail[idx].lon; int sx,sy; worldToScreen(la,lo,sx,sy);
    bool vis=(sx>=0&&sx<SCR_W&&sy>=STATUS_H&&sy<SCR_H);
    if(vis){
      float f=(trailN>1)?(0.30f+0.60f*k/(trailN-1)):1.0f;
      uint16_t col=scaleColor(C_CYAN,f);
      bool gap=havePrev && haversine(plat,plon,la,lo)>GAP_M;
      if(psx>=0 && !gap){ tft.drawLine(psx,psy,sx,sy,col); tft.drawLine(psx,psy+1,sx,sy+1,col); tft.drawLine(psx+1,psy,sx+1,sy,col); }
      tft.fillCircle(sx,sy,2,col);
      psx=sx; psy=sy;
    } else psx=-1;
    plat=la; plon=lo; havePrev=true;
  }
  drawMe(); drawNorth();
}
void drawStatus(){
  char b[40];
  tft.fillRect(0,0,SCR_W,STATUS_H,C_PANEL); tft.drawFastHLine(0,STATUS_H,SCR_W,C_DGREY);
  tft.setTextSize(2); tft.setTextColor(C_CYAN,C_PANEL); tft.setCursor(6,7); tft.print("LIDER");
  // satelite
  drawSat(78,15, myFix?C_GREEN:C_RED);
  tft.setTextSize(2); tft.setTextColor(myFix?C_GREEN:C_RED,C_PANEL); tft.setCursor(92,7); snprintf(b,sizeof(b),"%d ",mySats); tft.print(b);
  // velocidade
  tft.setTextSize(2); tft.setTextColor(C_WHITE,C_PANEL); tft.setCursor(126,7); snprintf(b,sizeof(b),"%d",(int)(mySpeed+0.5)); tft.print(b);
  tft.setTextSize(1); tft.setTextColor(C_GREY,C_PANEL); tft.setCursor(126+((int)(mySpeed+0.5)>=100?36:(int)(mySpeed+0.5)>=10?24:12),15); tft.print("km/h");
  // enviando / procurando
  tft.setTextSize(1);
  if(myFix){ tft.setTextColor(C_GREEN,C_PANEL); tft.setCursor(210,4); tft.print("* ENVIANDO"); }
  else     { tft.setTextColor(C_ORANG,C_PANEL); tft.setCursor(210,4); tft.print("  PROCURANDO"); }
  tft.setTextColor(C_DGREY,C_PANEL); tft.setCursor(210,16); snprintf(b,sizeof(b),"tx:%lu   ",(unsigned long)txCount); tft.print(b);
  // ponto de status a direita
  tft.fillCircle(SCR_W-12,15,5, myFix?C_GREEN:C_ORANG);
}

void setup(){
  Serial.begin(115200);
  tft.init(); tft.setRotation(1);
  tft.setTouchCalibrate(touchCal);          // usa a calibracao ja feita
  SCR_W=tft.width(); SCR_H=tft.height();
  PLOT_CX=SCR_W/2; PLOT_CY=STATUS_H+(SCR_H-STATUS_H)/2;
  tft.fillScreen(C_BG);
  Serial1.begin(9600,SERIAL_8N1,35,22);   // LoRa
  Serial2.begin(9600,SERIAL_8N1,27,-1);   // GPS
  delay(150);
  lora.localread();
  Serial.print("LoRa LocalID="); Serial.print(lora.localId);
  Serial.print("  UniqueID="); Serial.print(lora.localUniqueId);
  Serial.println(lora.localUniqueId>0 ? "  (LoRa OK)" : "  (LoRa SEM RESPOSTA - confere fiacao)");
}

// le o LoRa so pra pegar o alerta do seguidor (cmd 0x13)
void readLoRa(){
  while(Serial1.available()){
    uint16_t id=0xFFFF; uint8_t cmd=0,p[240],plen=0;
    if(!lora.ReceivePacketCommand(&id,&cmd,p,&plen,20)) break;
    if(cmd==APP_CMD_ALERT) rxAlertUntil=millis()+5000;
  }
}

void sendHist(){
  uint8_t fl=(myFix?0x01:0x00)|(millis()<myAlertUntil?0x02:0x00);   // bit1 = alerta do lider
  int n=bufCount;
  if(n<1){   // ainda sem ponto: manda so status (mantem link/flags)
    uint8_t p[5]={ fl,(uint8_t)mySats,0,(uint8_t)(seqNewest&0xFF),(uint8_t)((seqNewest>>8)&0xFF) };
    lora.PrepareFrameCommand(DEST_GIGA,APP_CMD_GPSH,p,5); lora.SendPacket(); txCount++; return;
  }
  uint8_t p[5+BUFN*8];
  p[0]=fl; p[1]=(uint8_t)mySats; p[2]=(uint8_t)n;
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
  readLoRa();                               // pega alerta do seguidor
  // toque no botao de alerta (FAB canto inferior direito)
  int32_t tx,ty;
  if(tft.getTouch(&tx,&ty)){
    if(!touchWasDown){ int fx=SCR_W-34, fy=SCR_H-34;
      if((tx-fx)*(tx-fx)+(ty-fy)*(ty-fy) <= 34*34) myAlertUntil=millis()+4000; }
    touchWasDown=true;
  } else touchWasDown=false;

  while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=MIN_SPD_COURSE) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000);

  // grava ponto do proprio caminho quando andou >= STEP (pra remontar as curvas + mapa)
  if(myFix && (!haveAdd || haversine(lastAddLat,lastAddLon,myLat,myLon)>=STEP_M)) addPt();

  unsigned long now=millis();
  // Quieto ate travar; com fix, envia o historico a cada 2s
  if(myFix && now-lastTx>2000){ sendHist(); lastTx=now; }

  if(now-lastDraw>300){ lastDraw=now; drawMap(); drawStatus(); drawFab(); drawRxAlert(); }
  if(now-lastDbg>1000){
    Serial.print("LIDER | fix="); Serial.print(myFix?"S":"N"); Serial.print(" sat="); Serial.print(mySats);
    Serial.print(" tx="); Serial.println(txCount); lastDbg=now;
  }
}
