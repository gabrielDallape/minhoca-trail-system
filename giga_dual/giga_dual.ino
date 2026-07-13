/*
 * ============================================================================
 *  GIGA - DUAL (LIDER <-> SEGUIDOR) - PAISAGEM 800x480
 *  Troca de papel por toque em "VIRAR"; botao BO = ICONE de alerta (triangulo).
 *  Bidirecional (cada um sabe onde o outro esta): alerta "SEGUIDOR LONGE" e
 *  aviso de BO + mini-mapa com rastro (caminho de volta).
 *  Comandos serial (testar sem tocar): 't' = troca papel ; 'b' = liga/desliga BO.
 *  Pacote cmd 0x11 (10B): flags[0](bit0=fix,bit1=BO), sats[1], lat[2..5], lon[6..9].
 *  GPS -> Serial1 (0/1) ; LoRa -> Serial2 (18/19). GIGA=slave(ID1) -> DEST=0 (CYD).
 * ============================================================================
 */
#include <TinyGPSPlus.h>
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include "LoRaMESH.h"

GigaDisplay_GFX          gfx;
Arduino_GigaDisplayTouch touch;
TinyGPSPlus              gps;
LoRaMESH                 lora(&Serial2);

// ----- papel / protocolo -----
bool iAmLeader = true, myBO = false;
const uint16_t DEST_CYD    = 0;
const uint8_t  APP_CMD_GPS = 0x11, APP_CMD_ROLE = 0x20;
const float    FAR_THRESH_M= 500.0f;
unsigned long lastPosTx = 0, lastRoleTx = 0;
uint32_t txCount = 0;

// ----- mapa -----
const bool  HEADING_UP    = true;
const float MAP_M_PER_PX  = 3.81f;
const float MINI_MPP      = 3.81f;
int         PANEL_W       = 200;
const float TRAIL_STEP_M  = 3.0f;
const int   TRAIL_MAX     = 700;
const float MIN_SPD_COURSE= 1.5f;

// ----- cores -----
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_YEL   0xFFE0
#define C_CYAN  0x07FF
#define C_GREY  0x8410
#define C_DGREY 0x39E7
#define C_ORANG 0xFD20
#define C_DRED  0x6000

int SCR_W, SCR_H, PANEL_X, PLOT_W, PLOT_H, PLOT_CX, PLOT_CY;
int btnRoleX, btnRoleY, btnBOX, btnBOY, btnW, btnH;
int miniX, miniY, miniW, miniH, miniCX, miniCY;

// ----- meu estado -----
double myLat=0, myLon=0; bool myFix=false; int mySats=0; float mySpeed=0, myHeading=0;
unsigned long myLastByteMs=0, myLastFixMs=0; bool everMyComm=false;
// ----- o OUTRO -----
double othLat=0, othLon=0; bool othFix=false, othBO=false; int othSats=0;
unsigned long lastRxMs=0; uint32_t pktCount=0; bool linkOk=false;
// ----- rastro do OUTRO -----
struct Geo { double lat, lon; };
Geo trail[TRAIL_MAX]; int trailCount=0, trailHead=0; double lastTrailLat=0, lastTrailLon=0; bool haveTrail=false;

unsigned long lastDraw=0, lastDbg=0; bool touchWasDown=false; unsigned long lastTapMs=0;
const double R_EARTH=6371000.0;

// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  Serial2.begin(9600);
  delay(150);
  lora.begin(false);
  gfx.begin();
  gfx.setRotation(1);            // PAISAGEM
  SCR_W = gfx.width();           // 800
  SCR_H = gfx.height();          // 480
  computeLayout();
  touch.begin();
  gfx.startBuffering(); gfx.fillScreen(C_BLACK); gfx.endBuffering();
}

// ============================================================================
void loop() {
  readMyGPS();
  readLoRa();
  linkOk = (lastRxMs != 0) && (millis() - lastRxMs < 4000);
  myFix  = (myLastFixMs != 0) && (millis() - myLastFixMs < 3000);
  handleTouch();
  handleSerialCmd();

  unsigned long now = millis();
  if (now - lastPosTx  > 1000) { sendMyPos(); lastPosTx = now; }   // 1Hz; o papel vai DENTRO do pacote

  if (now - lastDraw > 200) {
    gfx.startBuffering();
    if (iAmLeader) drawLeaderScreen(); else { drawMap(); drawPanel(); }
    gfx.endBuffering();
    lastDraw = now;
  }
  if (now - lastDbg > 1000) { debugSerial(); lastDbg = now; }
}

// ============================================================================
//  LORA
// ============================================================================
void sendMyPos() {
  int32_t lat = (int32_t)(myLat*1e7), lon = (int32_t)(myLon*1e7);
  uint8_t p[10];
  p[0] = (myFix?0x01:0x00) | (myBO?0x02:0x00) | (iAmLeader?0x04:0x00); p[1] = (uint8_t)mySats;
  p[2]=lat&0xFF; p[3]=(lat>>8)&0xFF; p[4]=(lat>>16)&0xFF; p[5]=(lat>>24)&0xFF;
  p[6]=lon&0xFF; p[7]=(lon>>8)&0xFF; p[8]=(lon>>16)&0xFF; p[9]=(lon>>24)&0xFF;
  lora.PrepareFrameCommand(DEST_CYD, APP_CMD_GPS, p, 10); lora.SendPacket(); txCount++;
}
void sendRole() { uint8_t r = iAmLeader?0:1; lora.PrepareFrameCommand(DEST_CYD, APP_CMD_ROLE, &r, 1); lora.SendPacket(); }

void readMyGPS() {
  while (Serial1.available()) { gps.encode(Serial1.read()); myLastByteMs=millis(); everMyComm=true; }
  if (gps.satellites.isValid()) mySats  = gps.satellites.value();
  if (gps.speed.isValid())      mySpeed = gps.speed.kmph();
  if (gps.course.isValid() && mySpeed >= MIN_SPD_COURSE) myHeading = gps.course.deg();
  if (gps.location.isValid() && gps.location.isUpdated()) { myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFixMs=millis(); }
}
void readLoRa() {
  int guard = 0;
  while (Serial2.available() && guard++ < 8) {          // esvazia a fila, sem travar
    uint16_t id=0xFFFF; uint8_t cmd=0, p[240], plen=0;
    if (!lora.ReceivePacketCommand(&id, &cmd, p, &plen, 60)) break;
    if (cmd == APP_CMD_GPS && plen >= 10) {
      lastRxMs=millis(); pktCount++;
      othFix = p[0]&0x01; othBO = p[0]&0x02; othSats = p[1];
      int32_t lat=(int32_t)((uint32_t)p[2]|((uint32_t)p[3]<<8)|((uint32_t)p[4]<<16)|((uint32_t)p[5]<<24));
      int32_t lon=(int32_t)((uint32_t)p[6]|((uint32_t)p[7]<<8)|((uint32_t)p[8]<<16)|((uint32_t)p[9]<<24));
      if (othFix) { othLat=lat/1e7; othLon=lon/1e7; addTrail(othLat, othLon); }
    }
  }
}
void addTrail(double lat, double lon) {
  if (haveTrail && haversine(lastTrailLat, lastTrailLon, lat, lon) < TRAIL_STEP_M) return;
  trail[trailHead].lat=lat; trail[trailHead].lon=lon;
  trailHead=(trailHead+1)%TRAIL_MAX; if (trailCount<TRAIL_MAX) trailCount++;
  lastTrailLat=lat; lastTrailLon=lon; haveTrail=true;
}

// ============================================================================
//  GEO
// ============================================================================
double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH*2*atan2(sqrt(a),sqrt(1-a));
}
double bearingTo(double la1,double lo1,double la2,double lo2){
  double y=sin(radians(lo2-lo1))*cos(radians(la2));
  double x=cos(radians(la1))*sin(radians(la2))-sin(radians(la1))*cos(radians(la2))*cos(radians(lo2-lo1));
  double b=degrees(atan2(y,x)); return (b<0)?b+360:b;
}
void worldToScreenC(double lat,double lon,int cx,int cy,float mpp,int&sx,int&sy){
  double east=R_EARTH*cos(radians(myLat))*radians(lon-myLon);
  double north=R_EARTH*radians(lat-myLat);
  double up,right;
  if(HEADING_UP){ double h=radians(myHeading); up=north*cos(h)+east*sin(h); right=east*cos(h)-north*sin(h); }
  else { up=north; right=east; }
  sx=cx+(int)(right/mpp); sy=cy-(int)(up/mpp);
}

// ============================================================================
//  LAYOUT (paisagem)
// ============================================================================
void computeLayout() {
  PANEL_X = SCR_W - PANEL_W;              // 600
  PLOT_W  = SCR_W - PANEL_W;              // 600
  PLOT_H  = SCR_H;                        // 480
  PLOT_CX = PLOT_W/2; PLOT_CY = PLOT_H/2; // 300,240
  btnW = PANEL_W - 16; btnH = 58;
  btnRoleX = PANEL_X+8; btnRoleY = 10;                   // VIRAR no TOPO direito
  btnBOX   = PANEL_X+8; btnBOY   = SCR_H - btnH - 10;    // ALERTA embaixo direito
  // mini-mapa (modo lider) GRANDE: preenche a area preta a esquerda
  miniX = 6; miniW = PANEL_X - miniX - 6; miniY = 6; miniH = SCR_H - 12;
  miniCX = miniX + miniW/2; miniCY = miniY + miniH/2;
}

// icone de satelite (verde=fix, vermelho=sem fix)
void drawSat(int cx, int cy, uint16_t col) {
  gfx.fillRect(cx-5, cy-8, 10, 16, col);      // corpo
  gfx.fillRect(cx-18, cy-6, 9, 12, col);      // painel esq
  gfx.fillRect(cx+9,  cy-6, 9, 12, col);      // painel dir
  gfx.drawLine(cx-9, cy, cx-5, cy, col);
  gfx.drawLine(cx+5, cy, cx+9, cy, col);
  gfx.drawCircle(cx, cy, 2, C_BLACK);
}

// icone triangulo de alerta (apex pra cima) com exclamacao
void drawWarnTri(int cx, int cy, int s, bool filled, uint16_t col) {
  if (filled) gfx.fillTriangle(cx, cy-s, cx-s, cy+s, cx+s, cy+s, col);
  gfx.drawTriangle(cx, cy-s, cx-s, cy+s, cx+s, cy+s, filled?C_WHITE:col);
  gfx.drawTriangle(cx, cy-s+1, cx-s+1, cy+s-1, cx+s-1, cy+s-1, filled?C_WHITE:col);
  uint16_t ic = filled?C_WHITE:col;
  gfx.fillRect(cx-1, cy-(int)(s*0.15), 3, (int)(s*0.55), ic);
  gfx.fillCircle(cx, cy+(int)(s*0.62), 2, ic);
}

// ============================================================================
//  TELA - MODO LIDER
// ============================================================================
// info da coluna direita (entre VIRAR e ALERTA): titulo, satelite, VEL, DIST
void drawRightInfo(const char* title) {
  char b[24];
  int x = PANEL_X + 10;
  int y = btnRoleY + btnH + 12;      // abaixo do VIRAR
  gfx.setTextSize(2); gfx.setTextColor(C_CYAN, C_BLACK); gfx.setCursor(x, y); gfx.print(title);
  gfx.fillCircle(PANEL_X+PANEL_W-16, y+7, 7, linkOk?C_GREEN:C_RED);   // dot de link
  y += 28;
  drawSat(x+12, y+14, myFix?C_GREEN:C_RED);
  gfx.setTextSize(3); gfx.setTextColor(myFix?C_GREEN:C_RED, C_BLACK);
  snprintf(b,sizeof(b),"%d ", mySats); gfx.setCursor(x+40, y); gfx.print(b);
  y += 44;
  gfx.setTextSize(1); gfx.setTextColor(C_GREY,C_BLACK); gfx.setCursor(x, y); gfx.print("VEL km/h");
  gfx.setTextSize(6); gfx.setTextColor(C_WHITE,C_BLACK); gfx.setCursor(x, y+12);
  snprintf(b,sizeof(b),"%d ",(int)(mySpeed+0.5)); gfx.print(b);
  y += 84;
  gfx.setTextSize(1); gfx.setTextColor(C_GREY,C_BLACK); gfx.setCursor(x, y); gfx.print("DIST");
  if(myFix && linkOk && othFix){ double d=haversine(myLat,myLon,othLat,othLon); bool far=d>FAR_THRESH_M;
    gfx.setTextColor(far?C_RED:C_YEL,C_BLACK);
    if(d>=1000){ gfx.setTextSize(5); snprintf(b,sizeof(b),"%.1fk",d/1000.0);} else { gfx.setTextSize(6); snprintf(b,sizeof(b),"%dm",(int)d);}
    gfx.setCursor(x, y+12); gfx.print(b);
  } else { gfx.setTextSize(6); gfx.setTextColor(C_GREY,C_BLACK); gfx.setCursor(x, y+12); gfx.print("--"); }
}

// alerta de BO PISCANDO (aparece/some), sobre a area do mapa (nao a tela toda)
void drawBOAlertBlink(const char* who) {
  if (((millis()/300)%2)==0) return;
  int w=360, h=54, x=(PLOT_W-w)/2, y=12; if(x<4)x=4;
  gfx.fillRect(x,y,w,h,C_RED); gfx.drawRect(x,y,w,h,C_WHITE);
  int tcx=x+34, tcy=y+h/2, s=18;
  gfx.fillTriangle(tcx,tcy-s,tcx-s,tcy+s,tcx+s,tcy+s,C_WHITE);
  gfx.fillRect(tcx-2,tcy-6,4,13,C_RED); gfx.fillCircle(tcx,tcy+11,2,C_RED);
  char b[32]; snprintf(b,sizeof(b),"%s BO!", who);
  gfx.setTextSize(3); gfx.setTextColor(C_WHITE,C_RED); gfx.setCursor(x+68,y+16); gfx.print(b);
}

void drawLeaderScreen() {
  gfx.fillScreen(C_BLACK);
  drawMiniMap();                 // mapa GRANDE do seguidor (esquerda)
  drawRightInfo("LIDER");
  drawButtons();
  if (othBO) drawBOAlertBlink("SEGUIDOR");
}

void drawMiniMap() {
  gfx.drawRect(miniX, miniY, miniW, miniH, C_DGREY);
  int psx=-1, psy=-1;
  for (int k=0;k<trailCount;k++){
    int idx=(trailHead-trailCount+k+TRAIL_MAX)%TRAIL_MAX; int sx,sy;
    worldToScreenC(trail[idx].lat, trail[idx].lon, miniCX, miniCY, MINI_MPP, sx, sy);
    if (sx>=miniX&&sx<miniX+miniW&&sy>=miniY&&sy<miniY+miniH){ if(psx>=0) gfx.drawLine(psx,psy,sx,sy,C_CYAN); gfx.fillCircle(sx,sy,1,C_CYAN); psx=sx;psy=sy; }
    else psx=-1;
  }
  if (linkOk && othFix && myFix) {
    int lx,ly; worldToScreenC(othLat, othLon, miniCX, miniCY, MINI_MPP, lx, ly);
    uint16_t oc = othBO ? C_RED : C_ORANG;
    if (lx>=miniX&&lx<miniX+miniW&&ly>=miniY&&ly<miniY+miniH){
      gfx.drawLine(miniCX,miniCY,lx,ly, othBO?C_RED:C_DGREY);   // caminho ate o seguidor (vermelho no BO)
      gfx.fillCircle(lx,ly,6,oc); gfx.drawCircle(lx,ly,9,C_WHITE);
      if(othBO){ gfx.drawCircle(lx,ly,12,C_RED); gfx.fillRect(lx-1,ly-5,3,7,C_WHITE); gfx.fillCircle(lx,ly+5,1,C_WHITE); }
    } else {
      float dx=lx-miniCX,dy=ly-miniCY,L=sqrt(dx*dx+dy*dy); if(L<1)L=1; dx/=L;dy/=L;
      int ex=constrain(miniCX+(int)(dx*(miniW/2-14)),miniX+10,miniX+miniW-10);
      int ey=constrain(miniCY+(int)(dy*(miniH/2-14)),miniY+10,miniY+miniH-10);
      gfx.fillCircle(ex,ey,6,oc);
    }
  }
  gfx.fillTriangle(miniCX,miniCY-9,miniCX-6,miniCY+7,miniCX+6,miniCY+7,C_GREEN);
}

// ============================================================================
//  TELA - MODO SEGUIDOR (mapa)
// ============================================================================
void drawMap() {
  gfx.fillRect(0, 0, PLOT_W, SCR_H, C_BLACK);
  if (!myFix) {
    gfx.setTextSize(3); gfx.setTextColor(C_ORANG);
    gfx.setCursor(30, PLOT_CY-24); gfx.print("SEM FIX (EU)");
    gfx.setTextSize(2); gfx.setTextColor(C_GREY);
    gfx.setCursor(30, PLOT_CY+12); gfx.print("aguardando o ceu...");
    drawMeMarker(); drawNorthTag(); return;
  }
  int psx=-1, psy=-1;
  for (int k=0;k<trailCount;k++){
    int idx=(trailHead-trailCount+k+TRAIL_MAX)%TRAIL_MAX; int sx,sy;
    worldToScreenC(trail[idx].lat, trail[idx].lon, PLOT_CX, PLOT_CY, MAP_M_PER_PX, sx, sy);
    bool vis=(sx>=0&&sx<PLOT_W&&sy>=0&&sy<SCR_H);
    if (vis){ if(psx>=0) gfx.drawLine(psx,psy,sx,sy,C_CYAN); gfx.fillCircle(sx,sy,1,C_CYAN); psx=sx;psy=sy; } else psx=-1;
  }
  if (linkOk && othFix) {
    int lx,ly; worldToScreenC(othLat, othLon, PLOT_CX, PLOT_CY, MAP_M_PER_PX, lx, ly);
    uint16_t oc = othBO ? C_RED : C_ORANG;
    bool vis=(lx>=0&&lx<PLOT_W&&ly>=0&&ly<SCR_H);
    if (vis){
      gfx.drawLine(PLOT_CX,PLOT_CY,lx,ly, othBO?C_RED:C_DGREY);   // caminho ate o outro (vermelho no BO)
      gfx.fillCircle(lx,ly,7,oc); gfx.drawCircle(lx,ly,10,C_WHITE);
      if(othBO){ gfx.drawCircle(lx,ly,13,C_RED); gfx.fillRect(lx-1,ly-5,3,7,C_WHITE); gfx.fillCircle(lx,ly+5,1,C_WHITE); } // icone de alerta
    } else drawEdgeArrow(lx,ly, oc);
  }
  drawMeMarker(); drawNorthTag();
}
void drawMeMarker(){ int cx=PLOT_CX,cy=PLOT_CY; gfx.fillTriangle(cx,cy-11,cx-8,cy+9,cx+8,cy+9,C_GREEN); gfx.drawTriangle(cx,cy-11,cx-8,cy+9,cx+8,cy+9,C_WHITE); }
void drawEdgeArrow(int lx,int ly,uint16_t col){
  float dx=lx-PLOT_CX,dy=ly-PLOT_CY,len=sqrt(dx*dx+dy*dy); if(len<1)return; dx/=len;dy/=len; int m=22;
  int ex=constrain(PLOT_CX+(int)(dx*(PLOT_CX-m)),m,PLOT_W-m);
  int ey=constrain(PLOT_CY+(int)(dy*(PLOT_CY-m)),m,SCR_H-m);
  gfx.fillCircle(ex,ey,7,col);
  gfx.drawLine(ex,ey,ex-(int)(dx*16)+(int)(dy*8),ey-(int)(dy*16)-(int)(dx*8),col);
  gfx.drawLine(ex,ey,ex-(int)(dx*16)-(int)(dy*8),ey-(int)(dy*16)+(int)(dx*8),col);
}
void drawNorthTag(){
  int nx=28,ny=34; double h=HEADING_UP?radians(myHeading):0; float ux=-sin(h),uy=cos(h);
  gfx.drawLine(nx,ny,nx+(int)(ux*16),ny-(int)(uy*16),C_RED);
  gfx.setTextSize(2); gfx.setTextColor(C_RED);
  gfx.setCursor(nx+(int)(ux*18)-4,ny-(int)(uy*18)-6); gfx.print("N");
}

// ============================================================================
//  PAINEL (modo seguidor)
// ============================================================================
int panelBlock(int y,const char*label,const char*value,uint16_t col){
  int x=PANEL_X+8;
  gfx.fillRect(PANEL_X+1,y,PANEL_W-1,32,C_BLACK);
  gfx.setTextSize(1); gfx.setTextColor(C_GREY,C_BLACK); gfx.setCursor(x,y); gfx.print(label);
  gfx.setTextSize(2); gfx.setTextColor(col,C_BLACK); gfx.setCursor(x,y+11); gfx.print(value);
  return y+33;
}
void drawPanel() {
  gfx.fillRect(PANEL_X+1, 0, PANEL_W-1, SCR_H, C_BLACK);
  gfx.drawFastVLine(PANEL_X, 0, SCR_H, C_DGREY);
  drawRightInfo("SEGUIDOR");
  drawButtons();
  if (othBO) drawBOAlertBlink("LIDER");
}

// ------- botoes: VIRAR (texto) + BO (icone triangulo) -------
void drawButtons() {
  uint16_t rc = iAmLeader?C_ORANG:C_GREEN;
  gfx.fillRect(btnRoleX,btnRoleY,btnW,btnH,C_BLACK);
  gfx.drawRect(btnRoleX,btnRoleY,btnW,btnH,rc);
  gfx.setTextSize(2); gfx.setTextColor(rc,C_BLACK);
  gfx.setCursor(btnRoleX+10, btnRoleY+10); gfx.print("VIRAR");
  gfx.setCursor(btnRoleX+10, btnRoleY+34); gfx.print(iAmLeader?"SEGUIDOR":"LIDER");

  uint16_t bc = myBO?C_RED:C_YEL;
  gfx.fillRect(btnBOX,btnBOY,btnW,btnH, myBO?C_DRED:C_BLACK);
  gfx.drawRect(btnBOX,btnBOY,btnW,btnH,bc);
  drawWarnTri(btnBOX+btnW/2, btnBOY+btnH/2, 22, myBO, bc);   // so o icone de alerta
}

// ============================================================================
//  TOQUE / SERIAL / DEBUG
// ============================================================================
void toggleRole() {
  iAmLeader = !iAmLeader;
  if (!iAmLeader) { trailCount=0; trailHead=0; haveTrail=false; }
  lastRoleTx=0; lastPosTx=0;
}
void handleTouch() {
  GDTpoint_t p[5];
  uint8_t n = touch.getTouchPoints(p);
  if (n > 0) {
    int rx = p[0].x, ry = p[0].y;
    int tx = ry;
    int ty = (SCR_H - 1) - rx;
    if (!touchWasDown && millis() - lastTapMs > 700) {    // anti-repique
      if (tx>=btnRoleX && tx<=btnRoleX+btnW && ty>=btnRoleY && ty<=btnRoleY+btnH) { toggleRole(); lastTapMs=millis(); }
      else if (tx>=btnBOX && tx<=btnBOX+btnW && ty>=btnBOY && ty<=btnBOY+btnH) { myBO=!myBO; lastTapMs=millis(); }
    }
    touchWasDown = true;
  } else touchWasDown = false;
}
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c=='t'||c=='T'){ toggleRole(); Serial.print(">>> PAPEL -> "); Serial.println(iAmLeader?"LIDER":"SEGUIDOR"); }
    else if (c=='b'||c=='B'){ myBO=!myBO; Serial.print(">>> meu BO -> "); Serial.println(myBO?"LIGADO":"desligado"); }
  }
}
void debugSerial() {
  double dist=(myFix&&linkOk&&othFix)?haversine(myLat,myLon,othLat,othLon):-1;
  Serial.print("PAPEL="); Serial.print(iAmLeader?"LIDER":"SEGUIDOR");
  Serial.print(" myBO="); Serial.print(myBO?"1":"0");
  Serial.print(" | EU fix="); Serial.print(myFix?"S":"N"); Serial.print(" sat="); Serial.print(mySats);
  Serial.print(" | LINK="); Serial.print(linkOk?"OK":"--");
  Serial.print(" rx="); Serial.print(pktCount); Serial.print(" tx="); Serial.print(txCount);
  Serial.print(" | OUT fix="); Serial.print(othFix?"S":"N"); Serial.print(" BO="); Serial.print(othBO?"1":"0");
  Serial.print(" dist="); if(dist>=0) Serial.print(dist,0); else Serial.print("--");
  Serial.println();
}
