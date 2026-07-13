/*
 * ============================================================================
 *  GIGA - SEGUIDOR (simples) - PAISAGEM 800x480
 *  SO RECEBE a posicao do LIDER (CYD) por LoRa e plota o mapa estilo Waze.
 *  Sem troca de papel, sem BO. Nao transmite nada (unidirecional = link solido).
 *  GPS proprio -> Serial1 (0/1) ; LoRa -> Serial2 (18/19).
 *  Pacote cmd 0x12: flags[0](bit0=fix), sats[1], N[2], seqNewest[3..4],
 *                   + N x (lat int32 LE x1e7, lon int32 LE x1e7).
 *  UI: fundo azul-mapa, seta EU com brilho, rota roxa (falta ir) grossa +
 *      rastro ciano (ja passei) fino que desbota, aneis de distancia,
 *      auto-zoom (enquadra voce e o lider), etiqueta do lider, ↑/↓ aprox/afasta.
 * ============================================================================
 */
#include <TinyGPSPlus.h>
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include "LoRaMESH.h"

GigaDisplay_GFX          gfx;
Arduino_GigaDisplayTouch touch;
TinyGPSPlus     gps;
LoRaMESH        lora(&Serial2);

const uint8_t APP_CMD_GPSH = 0x12;     // GPS + historico rolante (flags bit0=fix, bit1=alerta)
const uint8_t APP_CMD_ALERT= 0x13;     // alerta do seguidor -> lider
const uint16_t DEST_LEADER = 0;        // ID do lider (CYD) p/ mandar o alerta
uint16_t lastSeq=0; bool haveSeq=false;
const float   FAR_THRESH_M= 500.0f;
const bool    HEADING_UP  = true;
float         mapMPP      = 2.0f;   // metros/pixel (zoom manual pelos botoes +/-)
int           PANEL_W     = 200;
const float   TRAIL_STEP_M= 3.0f;
const int     TRAIL_MAX   = 700;
const float   MIN_SPD_COURSE = 1.5f;
const double  R_EARTH     = 6371000.0;
const double  GAP_M       = 40.0;   // salto maior que isso = perdeu sinal (nao liga a reta)
const double  OFFROUTE_M  = 30.0;   // mais longe que isso do caminho do lider = fora de rota

#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_YEL   0xFFE0
#define C_CYAN  0x07FF
#define C_GREY  0x8410
#define C_DGREY 0x39E7
#define C_ORANG 0xFD20
#define C_PURPLE 0xC81F
#define C_BG    0x08C5   // navy (cara de mapa)
#define C_RING  0x29C9   // aneis fracos (azul-aco)
#define C_PANEL 0x10A2   // painel um tom acima do fundo
#define C_GHOST 0x7C53   // ponte tracejada (sinal perdido alem do buffer)

int SCR_W, SCR_H, PANEL_X, PLOT_W, PLOT_CX, PLOT_CY;
int zOutX, zInX, zY, zW=95, zH=82;              // botoes de zoom (canto inf esquerdo)
int abX, abY, abW, abH;                          // botao de alerta (rodape do painel)
bool touchWasDown=false; unsigned long lastTapMs=0;
// alerta: myAlert = eu apertei ; rxAlert = o lider me alertou
unsigned long myAlertUntil=0, rxAlertUntil=0, lastAlertTx=0; int alertTxLeft=0;

double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0;
unsigned long myLastFixMs=0;
double ldrLat=0,ldrLon=0; bool ldrFix=false; int ldrSats=0; uint32_t pkt=0;
unsigned long lastRxMs=0; bool linkOk=false;
struct Geo{double lat,lon;}; Geo trail[TRAIL_MAX]; int trailN=0,trailHead=0;
double lastTLat=0,lastTLon=0; bool haveT=false;
unsigned long lastDraw=0,lastDbg=0;

// escurece uma cor RGB565 por um fator 0..1 (pro rastro desbotar)
uint16_t scaleColor(uint16_t c, float f){
  if(f<0)f=0; if(f>1)f=1;
  int r=(c>>11)&0x1F, g=(c>>5)&0x3F, b=c&0x1F;
  r=(int)(r*f+0.5f); g=(int)(g*f+0.5f); b=(int)(b*f+0.5f);
  return (uint16_t)((r<<11)|(g<<5)|b);
}

void setup(){
  Serial.begin(115200);
  Serial1.begin(9600);          // GPS
  Serial2.begin(9600);          // LoRa
  delay(150);
  lora.begin(false);
  gfx.begin(); gfx.setRotation(1);
  SCR_W=gfx.width(); SCR_H=gfx.height();
  PANEL_X=SCR_W-PANEL_W; PLOT_W=SCR_W-PANEL_W; PLOT_CX=PLOT_W/2; PLOT_CY=SCR_H/2;
  zY=SCR_H-zH-10; zOutX=10; zInX=zOutX+zW+8;   // "-" e "+" no canto inferior esquerdo
  abW=PANEL_W-28; abX=PANEL_X+14; abH=78; abY=SCR_H-abH-14;   // botao de alerta no rodape do painel
  touch.begin();
  gfx.startBuffering(); gfx.fillScreen(C_BG); gfx.endBuffering();
}

double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH*2*atan2(sqrt(a),sqrt(1-a));
}
double bearingTo(double la1,double lo1,double la2,double lo2){
  double y=sin(radians(lo2-lo1))*cos(radians(la2));
  double x=cos(radians(la1))*sin(radians(la2))-sin(radians(la1))*cos(radians(la2))*cos(radians(lo2-lo1));
  double b=degrees(atan2(y,x)); return b<0?b+360:b;
}
void worldToScreen(double lat,double lon,int&sx,int&sy){
  double east=R_EARTH*cos(radians(myLat))*radians(lon-myLon);
  double north=R_EARTH*radians(lat-myLat);
  double up,right;
  if(HEADING_UP){ double h=radians(myHeading); up=north*cos(h)+east*sin(h); right=east*cos(h)-north*sin(h); }
  else { up=north; right=east; }
  sx=PLOT_CX+(int)(right/mapMPP); sy=PLOT_CY-(int)(up/mapMPP);
}
void addTrail(double lat,double lon){
  if(haveT && haversine(lastTLat,lastTLon,lat,lon)<TRAIL_STEP_M) return;
  trail[trailHead].lat=lat; trail[trailHead].lon=lon;
  trailHead=(trailHead+1)%TRAIL_MAX; if(trailN<TRAIL_MAX) trailN++;
  lastTLat=lat; lastTLon=lon; haveT=true;
}

void readGPS(){
  while(Serial1.available()) gps.encode(Serial1.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=MIN_SPD_COURSE) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFixMs=millis(); }
}
void readLoRa(){
  int guard=0;
  while(Serial2.available() && guard++<8){
    uint16_t id=0xFFFF; uint8_t cmd=0,p[240],plen=0;
    if(!lora.ReceivePacketCommand(&id,&cmd,p,&plen,60)) break;
    if(cmd==APP_CMD_GPSH && plen>=5){
      lastRxMs=millis(); pkt++;
      ldrFix=p[0]&0x01; ldrSats=p[1];
      if(p[0]&0x02) rxAlertUntil=millis()+5000;      // lider apertou o alerta
      int n=p[2]; uint16_t seqN=(uint16_t)p[3]|((uint16_t)p[4]<<8);
      for(int i=0;i<n;i++){
        int off=5+i*8; if(off+8>plen) break;
        int32_t la=(int32_t)((uint32_t)p[off]|((uint32_t)p[off+1]<<8)|((uint32_t)p[off+2]<<16)|((uint32_t)p[off+3]<<24));
        int32_t lo=(int32_t)((uint32_t)p[off+4]|((uint32_t)p[off+5]<<8)|((uint32_t)p[off+6]<<16)|((uint32_t)p[off+7]<<24));
        uint16_t seq = seqN - (n-1) + i;
        double laD=la/1e7, loD=lo/1e7;
        if(!haveSeq || (int16_t)(seq-lastSeq) > 0){    // ponto NOVO (nao repetido) -> remonta a curva
          addTrail(laD, loD);
          lastSeq=seq; haveSeq=true;
        }
        if(i==n-1){ ldrLat=laD; ldrLon=loD; }          // mais novo = posicao atual do lider
      }
    }
  }
}

void drawSat(int cx,int cy,uint16_t col){
  gfx.fillRect(cx-5,cy-8,10,16,col); gfx.fillRect(cx-18,cy-6,9,12,col); gfx.fillRect(cx+9,cy-6,9,12,col);
}
// linha ~4px (varias linhas deslocadas, cobre diagonal tambem)
void drawThick(int x0,int y0,int x1,int y1,uint16_t c){
  for(int d=-1; d<=2; d++){
    gfx.drawLine(x0+d,y0,x1+d,y1,c);
    gfx.drawLine(x0,y0+d,x1,y1+d,c);
  }
}
// ponte tracejada: sinal perdido alem do que o buffer recupera (nem reta cheia, nem vao vazio)
void drawDashed(int x0,int y0,int x1,int y1,uint16_t c){
  float dx=x1-x0,dy=y1-y0,len=sqrt(dx*dx+dy*dy); if(len<1)return; dx/=len;dy/=len;
  for(float s=0;s<len;s+=14){ float e=min(s+7.0f,len); gfx.drawLine((int)(x0+dx*s),(int)(y0+dy*s),(int)(x0+dx*e),(int)(y0+dy*e),c); }
}
// simbolo de alerta (triangulo com "!") - sem texto
void drawWarnTri(int cx,int cy,int r,uint16_t tri,uint16_t ex){
  gfx.fillTriangle(cx,cy-r, cx-r,cy+r*4/5, cx+r,cy+r*4/5, tri);
  gfx.fillRect(cx-2,cy-r/4,4,r*55/100,ex);           // haste do "!"
  gfx.fillRect(cx-2,cy+r*70/100-2,4,4,ex);           // ponto do "!"
}
// botao de alerta no rodape do painel (so o simbolo)
void drawAlertBtn(){
  bool active=millis()<myAlertUntil; bool blink=(millis()/300)%2==0;
  uint16_t fill=(active&&blink)?C_RED:C_PANEL;
  gfx.fillRect(abX,abY,abW,abH,fill);
  gfx.drawRect(abX,abY,abW,abH,C_RED); gfx.drawRect(abX+1,abY+1,abW-2,abH-2,C_RED);
  drawWarnTri(abX+abW/2, abY+abH/2, 24, (active&&blink)?C_WHITE:C_RED, fill);
}
// overlay quando o LIDER me alertou: borda vermelha piscando + simbolo grande
void drawRxAlert(){
  if(millis()>=rxAlertUntil) return;
  if((millis()/300)%2) return;
  for(int t=0;t<8;t++) gfx.drawRect(t,t,PLOT_W-1-2*t,SCR_H-1-2*t,C_RED);
  drawWarnTri(PLOT_CX,62,36,C_RED,C_BG);
}
// EU estilo Waze: chevron verde com halo (V1)
void drawMe(){
  int cx=PLOT_CX,cy=PLOT_CY;
  gfx.fillCircle(cx,cy,18,scaleColor(C_GREEN,0.16f));   // halo
  gfx.fillCircle(cx,cy,11,scaleColor(C_GREEN,0.30f));
  gfx.fillTriangle(cx,cy-15,cx-11,cy+12,cx+11,cy+12,C_GREEN);     // corpo
  gfx.fillTriangle(cx,cy-3,cx-6,cy+10,cx+6,cy+10,C_BG);          // entalhe (vira "V")
  gfx.drawTriangle(cx,cy-15,cx-11,cy+12,cx+11,cy+12,C_WHITE);
}
void drawNorth(){ int nx=28,ny=34; double h=HEADING_UP?radians(myHeading):0; float ux=-sin(h),uy=cos(h);
  gfx.drawLine(nx,ny,nx+(int)(ux*16),ny-(int)(uy*16),C_RED); gfx.setTextSize(2); gfx.setTextColor(C_RED);
  gfx.setCursor(nx+(int)(ux*18)-4,ny-(int)(uy*18)-6); gfx.print("N"); }
void drawRings(){
  static const int rm[]={25,50,100,200,400,800,1600};
  for(unsigned i=0;i<sizeof(rm)/sizeof(rm[0]);i++){
    int rp=(int)(rm[i]/mapMPP);
    if(rp>=30 && rp<=SCR_H) gfx.drawCircle(PLOT_CX,PLOT_CY,rp,C_RING);
  }
}
void drawEdge(int lx,int ly){
  float dx=lx-PLOT_CX,dy=ly-PLOT_CY,len=sqrt(dx*dx+dy*dy); if(len<1)return; dx/=len;dy/=len; int m=22;
  int ex=constrain(PLOT_CX+(int)(dx*(PLOT_CX-m)),m,PLOT_W-m); int ey=constrain(PLOT_CY+(int)(dy*(PLOT_CY-m)),m,SCR_H-m);
  gfx.fillCircle(ex,ey,8,C_ORANG);
  gfx.drawLine(ex,ey,ex-(int)(dx*18)+(int)(dy*9),ey-(int)(dy*18)-(int)(dx*9),C_ORANG);
  gfx.drawLine(ex,ey,ex-(int)(dx*18)-(int)(dy*9),ey-(int)(dy*18)+(int)(dx*9),C_ORANG);
  return;
}
// etiqueta "LIDER 281m" perto de um ponto (V5)
void drawLabel(int x,int y,double dist){
  char b[20]; if(dist>=1000) snprintf(b,sizeof(b),"LIDER %.1fkm",dist/1000.0); else snprintf(b,sizeof(b),"LIDER %dm",(int)dist);
  int w=strlen(b)*6+8; int lx=x+12, ly=y-8; if(lx+w>PLOT_W) lx=x-12-w; if(ly<2) ly=2;
  gfx.fillRect(lx,ly,w,16,C_BLACK); gfx.drawRect(lx,ly,w,16,C_ORANG);
  gfx.setTextSize(1); gfx.setTextColor(C_ORANG); gfx.setCursor(lx+4,ly+4); gfx.print(b);
}

void drawMap(){
  gfx.fillRect(0,0,PLOT_W,SCR_H,C_BG);
  if(!myFix){
    gfx.setTextSize(3); gfx.setTextColor(C_ORANG); gfx.setCursor(30,PLOT_CY-40); gfx.print("SEM FIX (EU)");
    gfx.setTextSize(2); gfx.setTextColor(C_GREY); gfx.setCursor(30,PLOT_CY); gfx.print("procurando satelite...");
    drawMe(); drawNorth(); return;
  }
  drawRings();
  // acha o ponto do caminho do lider mais PERTO de mim: divide "ja passei" de "falta ir"
  int kNear=0; double best=1e18;
  for(int k=0;k<trailN;k++){
    int idx=(trailHead-trailN+k+TRAIL_MAX)%TRAIL_MAX;
    double d=haversine(myLat,myLon,trail[idx].lat,trail[idx].lon);
    if(d<best){ best=d; kNear=k; }
  }
  // desenha o caminho. Depois do kNear = ROXO grosso/brilhante (falta ir, estilo Waze).
  // Antes = CIANO fino que desbota (ja passei). Salto >GAP_M (perda de sinal) nao liga a reta.
  int psx=-1,psy=-1; double plat=0,plon=0; bool havePrev=false;
  for(int k=0;k<trailN;k++){
    int idx=(trailHead-trailN+k+TRAIL_MAX)%TRAIL_MAX;
    double la=trail[idx].lat, lo=trail[idx].lon; int sx,sy; worldToScreen(la,lo,sx,sy);
    bool vis=(sx>=0&&sx<PLOT_W&&sy>=0&&sy<SCR_H);
    if(vis){
      bool gap = havePrev && haversine(plat,plon,la,lo) > GAP_M;
      if(psx>=0 && !gap){
        if(k>kNear){ drawThick(psx,psy,sx,sy,C_PURPLE); }   // falta ir: grosso + brilhante
        else { float f=(trailN>1)?(0.30f+0.55f*k/(trailN-1)):1.0f;   // ja passei: fino + desbota
               uint16_t col=scaleColor(C_CYAN,f); gfx.drawLine(psx,psy,sx,sy,col); gfx.drawLine(psx,psy+1,sx,sy+1,col); }
      } else if(psx>=0 && gap){                              // perda alem do buffer: ponte tracejada
        drawDashed(psx,psy,sx,sy,C_GHOST);
      }
      gfx.fillCircle(sx,sy,2,(k>kNear)?C_PURPLE:scaleColor(C_CYAN,0.6f));
      psx=sx; psy=sy;
    } else psx=-1;
    plat=la; plon=lo; havePrev=true;
  }
  // lider = bolinha laranja na posicao mais nova + etiqueta (sem reta ate mim)
  if(linkOk && ldrFix){
    int lx,ly; worldToScreen(ldrLat,ldrLon,lx,ly);
    double d=haversine(myLat,myLon,ldrLat,ldrLon);
    if(lx>=0&&lx<PLOT_W&&ly>=0&&ly<SCR_H){
      gfx.fillCircle(lx,ly,7,C_ORANG); gfx.drawCircle(lx,ly,10,C_WHITE);
      drawLabel(lx,ly,d);
    } else drawEdge(lx,ly);
  }
  drawMe(); drawNorth();
  // FORA DE ROTA: longe do caminho do lider -> pisca amarelo (best = dist ao ponto mais perto)
  if(trailN>3 && best>OFFROUTE_M && ((millis()/350)%2==0)){
    for(int t=0;t<6;t++) gfx.drawRect(t,t,PLOT_W-1-2*t,SCR_H-1-2*t,C_YEL);
    gfx.setTextSize(3); gfx.setTextColor(C_YEL,C_BG); gfx.setCursor(PLOT_CX-108,18); gfx.print("FORA DE ROTA");
  }
}
void drawPanel(){
  char b[24]; int x=PANEL_X+10;
  gfx.fillRect(PANEL_X+1,0,PANEL_W-1,SCR_H,C_PANEL); gfx.drawFastVLine(PANEL_X,0,SCR_H,C_DGREY);
  gfx.setTextSize(2); gfx.setTextColor(C_CYAN,C_PANEL); gfx.setCursor(x,8); gfx.print("SEGUIDOR");
  gfx.fillCircle(PANEL_X+PANEL_W-16,16,7, linkOk?C_GREEN:C_RED);
  // meu satelite
  drawSat(x+12,60, myFix?C_GREEN:C_RED);
  gfx.setTextSize(3); gfx.setTextColor(myFix?C_GREEN:C_RED,C_PANEL);
  snprintf(b,sizeof(b),"%d ",mySats); gfx.setCursor(x+40,46); gfx.print(b);
  // velocidade
  gfx.setTextSize(1); gfx.setTextColor(C_GREY,C_PANEL); gfx.setCursor(x,104); gfx.print("VEL km/h");
  gfx.setTextSize(6); gfx.setTextColor(C_WHITE,C_PANEL); gfx.setCursor(x,120); snprintf(b,sizeof(b),"%d ",(int)(mySpeed+0.5)); gfx.print(b);
  // distancia ao lider (numero grande + cor por faixa)
  gfx.setTextSize(1); gfx.setTextColor(C_GREY,C_PANEL); gfx.setCursor(x,196); gfx.print("DIST LIDER");
  if(myFix && linkOk && ldrFix){ double d=haversine(myLat,myLon,ldrLat,ldrLon); bool far=d>FAR_THRESH_M;
    uint16_t dc=far?C_RED:(d>150?C_YEL:C_GREEN);
    gfx.setTextColor(dc,C_PANEL);
    if(d>=1000){ gfx.setTextSize(5); snprintf(b,sizeof(b),"%.1fkm ",d/1000.0);} else { gfx.setTextSize(6); snprintf(b,sizeof(b),"%dm ",(int)d);}
    gfx.setCursor(x,214); gfx.print(b);
  } else { gfx.setTextSize(6); gfx.setTextColor(C_GREY,C_PANEL); gfx.setCursor(x,214); gfx.print("--   "); }
  // status lider
  gfx.setTextSize(1); gfx.setTextColor(C_GREY,C_PANEL); gfx.setCursor(x,320); gfx.print("LIDER");
  gfx.setTextSize(2); gfx.setTextColor((linkOk&&ldrFix)?C_GREEN:C_ORANG,C_PANEL); gfx.setCursor(x,334);
  gfx.print(!linkOk?"SEM LINK":(ldrFix?"OK      ":"SEM FIX "));
}

void drawZoomBtns(){
  gfx.fillRect(zOutX,zY,zW,zH,C_PANEL); gfx.drawRect(zOutX,zY,zW,zH,C_WHITE);
  gfx.fillRect(zInX,zY,zW,zH,C_PANEL); gfx.drawRect(zInX,zY,zW,zH,C_WHITE);
  gfx.setTextSize(5); gfx.setTextColor(C_WHITE,C_PANEL);
  gfx.setCursor(zOutX+22,zY+14); gfx.print("-");
  gfx.setCursor(zInX+16,zY+14); gfx.print("+");
}
void handleZoom(){
  GDTpoint_t p[5]; uint8_t n=touch.getTouchPoints(p);
  if(n>0){
    int tx = p[0].y;                 // mapeamento ORIGINAL que funcionava no giga_dual
    int ty = (SCR_H - 1) - p[0].x;
    if(!touchWasDown){
      Serial.print(">>> TOUCH raw="); Serial.print(p[0].x); Serial.print(","); Serial.print(p[0].y);
      Serial.print(" -> map="); Serial.print(tx); Serial.print(","); Serial.println(ty);
      if(millis()-lastTapMs>300){
        if(tx>=zInX&&tx<=zInX+zW&&ty>=zY&&ty<=zY+zH){ mapMPP/=1.4f; if(mapMPP<0.5f)mapMPP=0.5f; lastTapMs=millis(); }        // + = aproxima
        else if(tx>=zOutX&&tx<=zOutX+zW&&ty>=zY&&ty<=zY+zH){ mapMPP*=1.4f; if(mapMPP>30.0f)mapMPP=30.0f; lastTapMs=millis(); } // - = afasta
        else if(tx>=abX&&tx<=abX+abW&&ty>=abY&&ty<=abY+abH){ myAlertUntil=millis()+4000; alertTxLeft=5; lastAlertTx=0; lastTapMs=millis(); } // ALERTA
      }
    }
    touchWasDown=true;
  } else touchWasDown=false;
}

void loop(){
  readGPS(); readLoRa(); handleZoom();
  linkOk=(lastRxMs!=0)&&(millis()-lastRxMs<4000);
  myFix=(myLastFixMs!=0)&&(millis()-myLastFixMs<3000);
  unsigned long now=millis();
  // manda o alerta pro lider (rajada curta de repeticoes so no toque -> sem colisao continua)
  if(alertTxLeft>0 && now-lastAlertTx>250){
    uint8_t ap[1]={0x01};
    lora.PrepareFrameCommand(DEST_LEADER,APP_CMD_ALERT,ap,1); lora.SendPacket();
    alertTxLeft--; lastAlertTx=now;
  }
  if(now-lastDraw>250){ gfx.startBuffering(); drawMap(); drawPanel(); drawZoomBtns(); drawAlertBtn(); drawRxAlert(); gfx.endBuffering(); lastDraw=now; }
  if(now-lastDbg>1000){
    Serial.print("SEGUIDOR | EUfix="); Serial.print(myFix?"S":"N"); Serial.print(" sat="); Serial.print(mySats);
    Serial.print(" | LINK="); Serial.print(linkOk?"OK":"--"); Serial.print(" pkt="); Serial.print(pkt);
    Serial.print(" | LIDERfix="); Serial.println(ldrFix?"S":"N"); lastDbg=now;
  }
}
