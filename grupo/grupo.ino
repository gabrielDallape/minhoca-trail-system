/*
 * ============================================================================
 *  MODO GRUPO - firmware unificado ESP32/LovyanGFX (CYD agora, Waveshare depois).
 *  Um binario roda como LIDER ou SEGUIDOR (definido na config, salva em NVS).
 *
 *  TOPOLOGIA HUB (LoRaMESH): o LIDER (master, ID0/slot0) faz BROADCAST (2047) do
 *  "estado do mundo" (posicao de todos + rota + alertas + cores). Os SEGUIDORES
 *  (slaves) mandam UPLINK pro lider na SUA VEZ (slot). Beacon = o proprio WORLD,
 *  marca o tempo zero. Seguidores nao se escutam direto -> o lider retransmite.
 *
 *  MARCADORES: voce = TRIANGULO AZUL (centro) ; lider = TRIANGULO AMARELO ;
 *  outros carros = BOLINHAS na cor de cada um. Estrada com contorno, paleta sobria.
 *
 *  Config de bancada por SERIAL (enquanto a pagina WiFi nao entra):
 *    'L'      -> virar LIDER (slot 0)
 *    'F' 'n'  -> virar SEGUIDOR no slot n (ex.: manda "F1")
 *    'R' 5dig -> definir sala (ex.: "R48291")
 *    'C' 'n'  -> minha cor (0..7)
 *    'P'      -> imprime a config atual
 *  Config salva em NVS (sobrevive a desligar).
 *
 *  Pinos (CYD): LoRa Serial1 RX=IO35 TX=IO22 ; GPS Serial2 RX=IO27.
 * ============================================================================
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>
#include <Preferences.h>

static LGFX  tft;
LoRaMESH     lora(&Serial1);
TinyGPSPlus  gps;
Preferences  prefs;

// ---------------- protocolo ----------------
const uint16_t BCAST      = 2047;
const uint16_t LEADER_ID  = 0;
const uint8_t  CMD_WORLD  = 0x30;   // lider -> broadcast (estado do mundo)
const uint8_t  CMD_UPLINK = 0x32;   // seguidor -> lider (minha posicao)
const int      MAXN       = 8;      // ate 8 carrinhos (slot 0 = lider)
const int      ROUTE_MAX  = 140;    // pontos da rota do lider (desenho)
const int      HIST_N     = 12;     // pontos de rota enviados por WORLD (curvas)
const float    STEP_M     = 5.0f;
const double   R_EARTH    = 6371000.0;
const unsigned long SLOT_MS  = 450;
const unsigned long CYCLE_MS = (unsigned long)MAXN * SLOT_MS;   // ~3.6s
const unsigned long NODE_TTL = 12000;   // no some da tela se ficar mudo tanto tempo
const unsigned long LEAD_TTL = 8000;    // lider offline

// ---------------- config ----------------
struct Cfg { uint32_t room; uint8_t role; uint8_t slot; uint8_t color; };
Cfg cfg = { 48291, 1, 0, 0 };            // default: LIDER, slot 0, sala 48291
bool isLeader(){ return cfg.role==1; }

// ---------------- estado do mundo ----------------
struct Node { bool active; double lat,lon; bool fix; bool alert; unsigned long lastMs; uint8_t color; };
Node world[MAXN];
struct Geo { double lat,lon; };
Geo route[ROUTE_MAX]; int routeN=0, routeHead=0; uint16_t routeSeq=0; uint16_t lastRouteSeq=0; bool haveRouteSeq=false;
double lastAddLat=0,lastAddLon=0; bool haveAdd=false;

// meu GPS
double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0;
unsigned long myLastFix=0;
bool myAlert=false; unsigned long myAlertUntil=0;

unsigned long lastCycle=0, worldRxMs=0, lastDraw=0, lastDbg=0;
bool pendingUplink=false; unsigned long slotDue=0;
float mapMPP=2.0f;
bool touchWasDown=false;

// ---------------- cores ----------------
#define C_BG    0x1925
#define C_CARD  0x10E4
#define C_LINE  0x2945
#define C_BLUE  0x3C7F   // voce
#define C_AMBER 0xE548   // lider
#define C_ROUTE 0x2CF1   // rota a seguir (teal)
#define C_ROUTEC 0x11A6
#define C_TRAV  0x6B8F   // ja percorrido (cinza)
#define C_TRAVC 0x31A6
#define C_RED   0xE207
#define C_WHITE 0xFFFF
#define C_MUT   0x8CB5
#define C_GREEN 0x2648
static const uint16_t PALETTE[8] = {0x3C7F,0x2CF1,0x9694,0xEC88,0xE36E,0x2648,0xFD20,0x07FF};
uint16_t colorOf(uint8_t i){ return PALETTE[i&7]; }

int SCR_W, SCR_H, CXp, CYp;

double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH*2*atan2(sqrt(a),sqrt(1-a));
}
void worldToScreen(double lat,double lon,double clat,double clon,double head,int&sx,int&sy){
  double east=R_EARTH*cos(radians(clat))*radians(lon-clon);
  double north=R_EARTH*radians(lat-clat);
  double h=radians(head);
  double up=north*cos(h)+east*sin(h), ri=east*cos(h)-north*sin(h);
  sx=CXp+(int)(ri/mapMPP); sy=CYp-(int)(up/mapMPP);
}

// ---------------- config / NVS ----------------
void saveCfg(){ prefs.begin("grupo",false); prefs.putBytes("cfg",&cfg,sizeof(cfg)); prefs.end(); }
void loadCfg(){ prefs.begin("grupo",true); prefs.getBytes("cfg",&cfg,sizeof(cfg)); prefs.end();
  if(cfg.room==0||cfg.room>99999){ cfg.room=48291; cfg.role=1; cfg.slot=0; cfg.color=0; } }
void printCfg(){ Serial.print("[cfg] sala="); Serial.print(cfg.room);
  Serial.print(" papel="); Serial.print(isLeader()?"LIDER":"SEGUIDOR");
  Serial.print(" slot="); Serial.print(cfg.slot); Serial.print(" cor="); Serial.println(cfg.color); }
void handleSerialCfg(){
  if(!Serial.available()) return;
  char c=Serial.read();
  if(c=='L'){ cfg.role=1; cfg.slot=0; saveCfg(); printCfg(); }
  else if(c=='F'){ while(!Serial.available()){} int n=Serial.read()-'0'; if(n>=1&&n<MAXN){ cfg.role=0; cfg.slot=n; saveCfg(); printCfg(); } }
  else if(c=='C'){ while(!Serial.available()){} int n=Serial.read()-'0'; if(n>=0&&n<8){ cfg.color=n; saveCfg(); printCfg(); } }
  else if(c=='R'){ uint32_t v=0; for(int i=0;i<5;i++){ while(!Serial.available()){} char d=Serial.read(); if(d>='0'&&d<='9') v=v*10+(d-'0'); } cfg.room=v; saveCfg(); printCfg(); }
  else if(c=='P'){ printCfg(); }
}

// ---------------- rota (buffer) ----------------
void routeAdd(double la,double lo){
  route[routeHead].lat=la; route[routeHead].lon=lo;
  routeHead=(routeHead+1)%ROUTE_MAX; if(routeN<ROUTE_MAX) routeN++;
}
void leaderRecordOwnPath(){
  if(myFix && (!haveAdd || haversine(lastAddLat,lastAddLon,myLat,myLon)>=STEP_M)){
    routeAdd(myLat,myLon); routeSeq++; lastAddLat=myLat; lastAddLon=myLon; haveAdd=true;
  }
}

// ---------------- LoRa ----------------
void putLE32(uint8_t*p,int32_t v){ p[0]=v&0xFF;p[1]=(v>>8)&0xFF;p[2]=(v>>16)&0xFF;p[3]=(v>>24)&0xFF; }
int32_t getLE32(const uint8_t*p){ return (int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24)); }

void sendWorld(){
  // [room(3)][round(1)][8*color(1)][8*(flags,lat,lon)=9][Nh(1)][seq(2)][Nh*(lat,lon)]
  uint8_t p[4+8+72+3+HIST_N*8]; int o=0;
  p[o++]=cfg.room&0xFF; p[o++]=(cfg.room>>8)&0xFF; p[o++]=(cfg.room>>16)&0xFF;
  p[o++]=(uint8_t)(millis()/CYCLE_MS);
  for(int k=0;k<MAXN;k++) p[o++]=world[k].color;
  for(int k=0;k<MAXN;k++){
    uint8_t fl=0; if(world[k].active)fl|=1; if(world[k].fix)fl|=2; if(world[k].alert)fl|=4;
    p[o++]=fl; putLE32(p+o,(int32_t)(world[k].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(world[k].lon*1e7)); o+=4;
  }
  int nh=routeN<HIST_N?routeN:HIST_N;
  p[o++]=(uint8_t)nh; p[o++]=routeSeq&0xFF; p[o++]=(routeSeq>>8)&0xFF;
  int idx=(routeHead-nh+ROUTE_MAX)%ROUTE_MAX;
  for(int i=0;i<nh;i++){ int j=(idx+i)%ROUTE_MAX; putLE32(p+o,(int32_t)(route[j].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(route[j].lon*1e7)); o+=4; }
  lora.PrepareFrameCommand(BCAST,CMD_WORLD,p,o); lora.SendPacket();
}
void sendUplink(){
  uint8_t p[13]; int o=0;
  p[o++]=cfg.room&0xFF; p[o++]=(cfg.room>>8)&0xFF; p[o++]=(cfg.room>>16)&0xFF;
  p[o++]=cfg.slot;
  uint8_t fl=0; if(myFix)fl|=2; if(millis()<myAlertUntil)fl|=4; p[o++]=fl;
  putLE32(p+o,(int32_t)(myLat*1e7)); o+=4; putLE32(p+o,(int32_t)(myLon*1e7)); o+=4;
  lora.PrepareFrameCommand(LEADER_ID,CMD_UPLINK,p,o); lora.SendPacket();
}
uint32_t roomOf(const uint8_t*p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16); }

void handleRx(uint8_t cmd,uint8_t*p,uint8_t plen){
  if(isLeader()){
    if(cmd==CMD_UPLINK && plen>=13 && roomOf(p)==cfg.room){
      uint8_t sl=p[3]; if(sl>=1&&sl<MAXN){
        uint8_t fl=p[4]; world[sl].active=true; world[sl].fix=fl&2; world[sl].alert=fl&4;
        world[sl].lat=getLE32(p+5)/1e7; world[sl].lon=getLE32(p+9)/1e7; world[sl].lastMs=millis();
      }
    }
  } else {
    if(cmd==CMD_WORLD && plen>=4+8+72+3 && roomOf(p)==cfg.room){
      worldRxMs=millis();
      int o=4;
      for(int k=0;k<MAXN;k++) world[k].color=p[o++];
      for(int k=0;k<MAXN;k++){
        uint8_t fl=p[o]; world[k].active=fl&1; world[k].fix=fl&2; world[k].alert=fl&4;
        world[k].lat=getLE32(p+o+1)/1e7; world[k].lon=getLE32(p+o+5)/1e7;
        if(world[k].active) world[k].lastMs=millis();
        o+=9;
      }
      int nh=p[o++]; uint16_t seqN=(uint16_t)p[o]|((uint16_t)p[o+1]<<8); o+=2;
      for(int i=0;i<nh;i++){
        double la=getLE32(p+o)/1e7, lo=getLE32(p+o+4)/1e7; o+=8;
        uint16_t seq=seqN-(nh-1)+i;
        if(!haveRouteSeq || (int16_t)(seq-lastRouteSeq)>0){ routeAdd(la,lo); lastRouteSeq=seq; haveRouteSeq=true; }
      }
      // agenda meu uplink na minha vez
      slotDue=worldRxMs+(unsigned long)cfg.slot*SLOT_MS; pendingUplink=true;
    }
  }
}
void readLoRa(){
  int guard=0; uint16_t id; uint8_t cmd=0,p[240],plen=0;
  while(guard++<8 && lora.ReceivePacketCommand(&id,&cmd,p,&plen,15)) handleRx(cmd,p,plen);
}

// ---------------- GPS ----------------
void readGPS(){
  while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=1.5f) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000);
}

// ---------------- desenho ----------------
void triMarker(int cx,int cy,float ang,uint16_t col,int s){
  float pts[4][2]={{0,-(float)s},{0.62f*s,0.72f*s},{0,0.32f*s},{-0.62f*s,0.72f*s}};
  int X[4],Y[4]; float ca=cos(ang),sa=sin(ang);
  for(int i=0;i<4;i++){ X[i]=cx+(int)(pts[i][0]*ca-pts[i][1]*sa); Y[i]=cy+(int)(pts[i][0]*sa+pts[i][1]*ca); }
  tft.fillTriangle(X[0],Y[0],X[1],Y[1],X[2],Y[2],col);
  tft.fillTriangle(X[0],Y[0],X[2],Y[2],X[3],Y[3],col);
}
void dotMarker(int cx,int cy,uint16_t col,int r){ tft.fillCircle(cx,cy,r,col); tft.drawCircle(cx,cy,r,C_WHITE); }
void card(int x,int y,int w,int h){ tft.fillRoundRect(x,y,w,h,7,C_CARD); tft.drawRoundRect(x,y,w,h,7,C_LINE); }
void roadSeg(int x0,int y0,int x1,int y1,uint16_t cas,uint16_t fil,int wc,int wf){
  for(int d=-(wc/2);d<=wc/2;d++){ tft.drawLine(x0+d,y0,x1+d,y1,cas); tft.drawLine(x0,y0+d,x1,y1+d,cas); }
  for(int d=-(wf/2);d<=wf/2;d++){ tft.drawLine(x0+d,y0,x1+d,y1,fil); tft.drawLine(x0,y0+d,x1,y1+d,fil); }
}

int nearestRouteIdx(double la,double lo){
  int best=-1; double bd=1e18;
  for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX;
    double d=haversine(la,lo,route[idx].lat,route[idx].lon); if(d<bd){bd=d;best=k;} }
  return best;
}

void drawMap(){
  tft.fillScreen(C_BG);
  // centro = eu (meu slot). heading = meu curso.
  double clat=myLat, clon=myLon, chead=myHeading;
  bool leadOffline = !isLeader() && (worldRxMs==0 || millis()-worldRxMs>LEAD_TTL);

  if(!myFix){
    tft.setTextColor(C_AMBER); tft.setTextSize(2); tft.setCursor(20,CYp-10); tft.print("PROCURANDO GPS...");
    triMarker(CXp,CYp,0,C_BLUE,14); return;
  }
  int myNear = nearestRouteIdx(clat,clon);
  // rota do lider (estrada). depois do meu ponto = a seguir (teal) ; antes = cinza
  int psx=-1,psy=-1; double plat=0,plon=0; bool havePrev=false;
  for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX;
    int sx,sy; worldToScreen(route[idx].lat,route[idx].lon,clat,clon,chead,sx,sy);
    bool vis=(sx>=-20&&sx<SCR_W+20&&sy>=-20&&sy<SCR_H+20);
    if(vis && psx>=0){
      bool gap = havePrev && haversine(plat,plon,route[idx].lat,route[idx].lon)>40.0;
      if(!gap){ if(k>myNear) roadSeg(psx,psy,sx,sy,C_ROUTEC,C_ROUTE,9,4);
                else roadSeg(psx,psy,sx,sy,C_TRAVC,C_TRAV,7,3); }
    }
    psx=vis?sx:-1; psy=sy; plat=route[idx].lat; plon=route[idx].lon; havePrev=true;
  }
  // alerta: estrada vermelha seguindo o caminho ate quem apertou
  int alertSlot=-1; for(int k=0;k<MAXN;k++) if(world[k].active&&world[k].alert){ alertSlot=k; break; }
  if(alertSlot>=0){
    int aNear=nearestRouteIdx(world[alertSlot].lat,world[alertSlot].lon);
    if(aNear>=0&&myNear>=0){ int a=min(aNear,myNear),b=max(aNear,myNear); int qx=-1,qy=-1;
      for(int k=a;k<=b;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; int sx,sy;
        worldToScreen(route[idx].lat,route[idx].lon,clat,clon,chead,sx,sy);
        if(qx>=0) roadSeg(qx,qy,sx,sy,0x6800,C_RED,11,5); qx=sx; qy=sy; } }
  }
  // nos: lider = triangulo amarelo ; outros = bolinhas ; eu = triangulo azul (centro)
  for(int k=0;k<MAXN;k++){
    if(!world[k].active) continue;
    if((int)cfg.slot==k) continue;                 // eu desenho por ultimo no centro
    if(millis()-world[k].lastMs>NODE_TTL) continue;
    int sx,sy; worldToScreen(world[k].lat,world[k].lon,clat,clon,chead,sx,sy);
    if(sx<-30||sx>SCR_W+30||sy<-30||sy>SCR_H+30) continue;
    bool al=world[k].alert;
    if(k==0) triMarker(sx,sy,0,al?C_RED:C_AMBER,13);        // lider
    else dotMarker(sx,sy,al?C_RED:colorOf(world[k].color),8);
  }
  triMarker(CXp,CYp,0,C_BLUE,14);                            // eu

  if(leadOffline){ card(SCR_W/2-90,8,180,26); tft.setTextColor(C_RED); tft.setTextSize(1); tft.setCursor(SCR_W/2-70,16); tft.print("LIDER OFFLINE"); }
}

void drawUI(){
  char b[32];
  // status (topo esq)
  card(8,8,150,40);
  tft.fillCircle(24,28,5, isLeader()?C_AMBER:C_BLUE);
  tft.setTextColor(C_WHITE); tft.setTextSize(1); tft.setCursor(36,16); snprintf(b,sizeof(b),"Sala %lu",(unsigned long)cfg.room); tft.print(b);
  tft.setTextColor(C_MUT); tft.setCursor(36,30); tft.print(isLeader()?"voce e o LIDER":"seguidor");
  // distancia ao lider (baixo esq) - seguidor
  if(!isLeader() && world[0].active && myFix){
    double d=haversine(myLat,myLon,world[0].lat,world[0].lon);
    card(8,SCR_H-52,150,44);
    tft.setTextColor(C_MUT); tft.setTextSize(1); tft.setCursor(16,SCR_H-46); tft.print("LIDER");
    tft.setTextColor(C_WHITE); tft.setTextSize(3); tft.setCursor(16,SCR_H-34);
    if(d>=1000) snprintf(b,sizeof(b),"%.1fkm",d/1000.0); else snprintf(b,sizeof(b),"%dm",(int)d);
    tft.print(b);
  }
  // botao de alerta (FAB) canto inf direito - so o simbolo
  bool aOn = (millis()<myAlertUntil) && ((millis()/300)%2==0);
  int fx=SCR_W-30, fy=SCR_H-30;
  tft.fillCircle(fx,fy,24, aOn?C_RED:C_CARD); tft.drawCircle(fx,fy,24,C_RED);
  uint16_t tc=aOn?C_WHITE:C_RED; tft.fillTriangle(fx,fy-12,fx-12,fy+10,fx+12,fy+10,tc);
  tft.fillRect(fx-2,fy-4,4,8, aOn?C_RED:C_CARD); tft.fillRect(fx-2,fy+7,4,3, aOn?C_RED:C_CARD);
}

void handleTouch(){
  int32_t tx,ty;
  if(tft.getTouch(&tx,&ty)){
    if(!touchWasDown){
      int fx=SCR_W-30, fy=SCR_H-30;
      if((tx-fx)*(tx-fx)+(ty-fy)*(ty-fy) <= 30*30){ myAlertUntil=millis()+4000; }
    }
    touchWasDown=true;
  } else touchWasDown=false;
}

void setup(){
  Serial.begin(115200);
  loadCfg();
  tft.init(); tft.setRotation(1);
  SCR_W=tft.width(); SCR_H=tft.height(); CXp=SCR_W/2; CYp=SCR_H/2;
  tft.fillScreen(C_BG);
  Serial1.begin(9600,SERIAL_8N1,35,22);   // LoRa
  Serial2.begin(9600,SERIAL_8N1,27,-1);   // GPS
  delay(150);
  lora.localread();
  for(int k=0;k<MAXN;k++){ world[k]=Node(); world[k].color=k; }
  Serial.print("== GRUPO == "); printCfg();
  Serial.print("LoRa localId="); Serial.print(lora.localId); Serial.print(" uid="); Serial.println(lora.localUniqueId);
}

void loop(){
  handleSerialCfg();
  readGPS(); readLoRa(); handleTouch();
  unsigned long now=millis();

  // meu no no mundo
  world[cfg.slot].active=true; world[cfg.slot].fix=myFix; world[cfg.slot].alert=(now<myAlertUntil);
  world[cfg.slot].lat=myLat; world[cfg.slot].lon=myLon; world[cfg.slot].lastMs=now; world[cfg.slot].color=cfg.color;

  if(isLeader()){
    leaderRecordOwnPath();
    if(now-lastCycle>=CYCLE_MS){ if(myFix) sendWorld(); lastCycle=now; }   // quieto ate ter fix
  } else {
    if(pendingUplink && now>=slotDue){ sendUplink(); pendingUplink=false; }
  }

  if(now-lastDraw>250){ drawMap(); drawUI(); lastDraw=now; }
  if(now-lastDbg>2000){
    Serial.print(isLeader()?"LIDER":"SEG"); Serial.print(" fix="); Serial.print(myFix?"S":"N");
    Serial.print(" sat="); Serial.print(mySats); int n=0; for(int k=0;k<MAXN;k++) if(world[k].active&&now-world[k].lastMs<NODE_TTL)n++;
    Serial.print(" nos="); Serial.println(n); lastDbg=now;
  }
}
