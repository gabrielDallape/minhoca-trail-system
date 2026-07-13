/*
 * ============================================================================
 *  MODO GRUPO - SEGUIDOR no GIGA R1 (reserva / p/ bench-testar com o CYD).
 *  Mesmo protocolo do grupo/grupo.ino, mas render em GigaDisplay_GFX (paisagem).
 *  So SEGUIDOR (o lider e sempre o CYD/Waveshare). Config por serial:
 *    'R' 5dig -> sala (ex.: "R48291")   'F' n -> meu slot (1..7, ex.: "F1")
 *  LoRa -> Serial2 (18/19) ; GPS -> Serial1 (0/1).
 *  Recebe CMD_WORLD(0x30) e CMD_ROSTER(0x31) do lider; faz uplink CMD_UPLINK(0x32)
 *  na sua vez (slot = worldRx + slot*450ms). Mapa: voce=triangulo azul,
 *  lider=triangulo amarelo, outros=bolinhas; alerta=estrada vermelha.
 * ============================================================================
 */
#include <TinyGPSPlus.h>
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include "LoRaMESH.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

GigaDisplay_GFX          gfx;
Arduino_GigaDisplayTouch touch;
TinyGPSPlus  gps;
LoRaMESH     lora(&Serial2);

const uint16_t BCAST=2047, LEADER_ID=0;
const uint8_t  CMD_WORLD=0x30, CMD_ROSTER=0x31, CMD_UPLINK=0x32, CMD_JOIN=0x33;
const int      MAXN=8, ROUTE_MAX=140, HIST_N=12;
const double   R_EARTH=6371000.0;
const unsigned long SLOT_MS=450, CYCLE_MS=(unsigned long)MAXN*SLOT_MS, NODE_TTL=12000, LEAD_TTL=8000;

// config. room 0 = nao configurado -> tela inicial Criar/Entrar
uint32_t room=0; uint8_t mySlot=1;
char codeBuf[6]=""; int codeLen=0;
bool leaderRole=false; uint16_t routeSeq=0; unsigned long lastCycle=0; double lastAddLat=0,lastAddLon=0; bool haveAdd=false;
uint8_t uiPage=0, pendingRole=0; bool searching=false;   // seguidor procurando a sala existir

// mundo
struct Node{ bool active; double lat,lon; bool fix; bool alert; unsigned long lastMs; uint8_t color; };
Node world[MAXN];
struct Geo{ double lat,lon; }; Geo route[ROUTE_MAX]; int routeN=0,routeHead=0; uint16_t lastRouteSeq=0; bool haveRouteSeq=false;
char rname[MAXN][16]; uint8_t rcolor[MAXN]; uint32_t ruid[MAXN]; bool haveRoster=false;
uint32_t myUid=0; bool joined=false; uint8_t curSlot=0; uint8_t myColor=1; char myName[16]="Carro";

double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0;
unsigned long myLastFix=0, worldRxMs=0, slotDue=0, lastDraw=0, lastDbg=0;
bool pendingUplink=false, myAlert=false; unsigned long myAlertUntil=0;
float mapMPP=2.0f; bool touchWasDown=false;

#define C_BG 0x1925
#define C_CARD 0x10E4
#define C_LINE 0x2945
#define C_BLUE 0x3C7F
#define C_AMBER 0xE548
#define C_ROUTE 0x2CF1
#define C_ROUTEC 0x11A6
#define C_TRAV 0x6B8F
#define C_TRAVC 0x31A6
#define C_RED 0xE207
#define C_WHITE 0xFFFF
#define C_MUT 0x8CB5
#define C_YEL 0xFE60
const double OFFROUTE_M=30.0;
static const uint16_t PALETTE[8]={0x3C7F,0x2CF1,0x9694,0xEC88,0xE36E,0x2648,0xFD20,0x07FF};
uint16_t colorOf(uint8_t i){ return PALETTE[i&7]; }

int SCR_W,SCR_H,CXp,CYp, abX,abY,abR;

double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH*2*atan2(sqrt(a),sqrt(1-a));
}
void w2s(double lat,double lon,double clat,double clon,double head,int&sx,int&sy){
  double east=R_EARTH*cos(radians(clat))*radians(lon-clon), north=R_EARTH*radians(lat-clat);
  double h=radians(head), up=north*cos(h)+east*sin(h), ri=east*cos(h)-north*sin(h);
  sx=CXp+(int)(ri/mapMPP); sy=CYp-(int)(up/mapMPP);
}
int32_t getLE32(const uint8_t*p){ return (int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24)); }
void putLE32(uint8_t*p,int32_t v){ p[0]=v&0xFF;p[1]=(v>>8)&0xFF;p[2]=(v>>16)&0xFF;p[3]=(v>>24)&0xFF; }
uint32_t roomOf(const uint8_t*p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16); }
void routeAdd(double la,double lo){ route[routeHead].lat=la; route[routeHead].lon=lo; routeHead=(routeHead+1)%ROUTE_MAX; if(routeN<ROUTE_MAX)routeN++; }

void handleSerial(){
  if(!Serial.available()) return; char c=Serial.read();
  if(c=='R'){ uint32_t v=0; for(int i=0;i<5;i++){ while(!Serial.available()){} char d=Serial.read(); if(d>='0'&&d<='9')v=v*10+(d-'0'); } room=v; Serial.print("sala="); Serial.println(room); }
  else if(c=='F'){ while(!Serial.available()){} int n=Serial.read()-'0'; if(n>=1&&n<MAXN){ curSlot=n; joined=true; leaderRole=false; searching=false; Serial.print("slot="); Serial.println(curSlot);} }
  else if(c=='L'){ leaderRole=true; if(room==0)room=48291; curSlot=0; joined=true; searching=false; ruid[0]=myUid; strncpy(rname[0],myName,15); rname[0][15]=0; rcolor[0]=myColor; haveRoster=true; Serial.println("LIDER"); }
  else if(c=='X'){ room=0; uiPage=0; leaderRole=false; joined=false; searching=false; codeLen=0; codeBuf[0]=0; Serial.println("saiu"); }
}

void readGPS(){
  while(Serial1.available()) gps.encode(Serial1.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=1.5f) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000);
}
void sendUplink(){
  uint8_t p[13]; int o=0; p[o++]=room&0xFF;p[o++]=(room>>8)&0xFF;p[o++]=(room>>16)&0xFF; p[o++]=curSlot;
  uint8_t fl=0; if(myFix)fl|=2; if(myAlert)fl|=4; p[o++]=fl;
  putLE32(p+o,(int32_t)(myLat*1e7)); o+=4; putLE32(p+o,(int32_t)(myLon*1e7)); o+=4;
  lora.PrepareFrameCommand(LEADER_ID,CMD_UPLINK,p,o); lora.SendPacket();
}
void sendJoin(){
  uint8_t p[24]; int o=0; p[o++]=room&0xFF;p[o++]=(room>>8)&0xFF;p[o++]=(room>>16)&0xFF;
  putLE32(p+o,(int32_t)myUid); o+=4; p[o++]=myColor; int L=strlen(myName); if(L>15)L=15; p[o++]=(uint8_t)L; memcpy(p+o,myName,L); o+=L;
  lora.PrepareFrameCommand(LEADER_ID,CMD_JOIN,p,o); lora.SendPacket();
}
// ---- LIDER: broadcast do mundo + roster ----
void sendWorld(){
  uint8_t p[4+8+72+3+HIST_N*8]; int o=0;
  p[o++]=room&0xFF; p[o++]=(room>>8)&0xFF; p[o++]=(room>>16)&0xFF; p[o++]=(uint8_t)(millis()/CYCLE_MS);
  for(int k=0;k<MAXN;k++) p[o++]=rcolor[k];
  for(int k=0;k<MAXN;k++){ uint8_t fl=0; if(world[k].active)fl|=1; if(world[k].fix)fl|=2; if(world[k].alert)fl|=4;
    p[o++]=fl; putLE32(p+o,(int32_t)(world[k].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(world[k].lon*1e7)); o+=4; }
  int nh=routeN<HIST_N?routeN:HIST_N; p[o++]=(uint8_t)nh; p[o++]=routeSeq&0xFF; p[o++]=(routeSeq>>8)&0xFF;
  int idx=(routeHead-nh+ROUTE_MAX)%ROUTE_MAX;
  for(int i=0;i<nh;i++){ int j=(idx+i)%ROUTE_MAX; putLE32(p+o,(int32_t)(route[j].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(route[j].lon*1e7)); o+=4; }
  lora.PrepareFrameCommand(BCAST,CMD_WORLD,p,o); lora.SendPacket();
}
void sendRoster(){
  uint8_t p[3+MAXN*21]; int o=0; p[o++]=room&0xFF; p[o++]=(room>>8)&0xFF; p[o++]=(room>>16)&0xFF;
  for(int k=0;k<MAXN;k++){ putLE32(p+o,(int32_t)ruid[k]); o+=4; p[o++]=rcolor[k]; int L=strlen(rname[k]); if(L>15)L=15; p[o++]=(uint8_t)L; memcpy(p+o,rname[k],L); o+=L; }
  lora.PrepareFrameCommand(BCAST,CMD_ROSTER,p,o); lora.SendPacket();
}
void leaderRecordOwnPath(){
  if(myFix && (!haveAdd || haversine(lastAddLat,lastAddLon,myLat,myLon)>=5.0)){ routeAdd(myLat,myLon); routeSeq++; lastAddLat=myLat; lastAddLon=myLon; haveAdd=true; }
}
void handleRx(uint8_t cmd,uint8_t*p,uint8_t plen){
  if(leaderRole){   // LIDER: recebe uplink e join
    if(cmd==CMD_UPLINK && plen>=13 && roomOf(p)==room){ uint8_t sl=p[3]; if(sl>=1&&sl<MAXN){
      uint8_t fl=p[4]; world[sl].active=true; world[sl].fix=fl&2; world[sl].alert=fl&4;
      world[sl].lat=getLE32(p+5)/1e7; world[sl].lon=getLE32(p+9)/1e7; world[sl].lastMs=millis(); } }
    else if(cmd==CMD_JOIN && plen>=9 && roomOf(p)==room){
      uint32_t uid=(uint32_t)getLE32(p+3); uint8_t col=p[7]; int L=p[8]; if(L>15)L=15;
      char nm[16]; if(9+L<=plen){ memcpy(nm,p+9,L); nm[L]=0; } else nm[0]=0;
      int slot=-1; for(int k=1;k<MAXN;k++) if(ruid[k]==uid){ slot=k; break; }
      if(slot<0) for(int k=1;k<MAXN;k++) if(ruid[k]==0){ slot=k; break; }
      if(slot>=1){ ruid[slot]=uid; if(col<8)rcolor[slot]=col; if(nm[0]){ strncpy(rname[slot],nm,15); rname[slot][15]=0; } haveRoster=true; sendRoster(); }
    }
    return;
  }
  if(cmd==CMD_ROSTER && plen>=3 && roomOf(p)==room){
    int o=3; for(int k=0;k<MAXN&&o+6<=plen;k++){ ruid[k]=(uint32_t)getLE32(p+o); o+=4; rcolor[k]=p[o++]; int L=p[o++]; if(L>15)L=15; if(o+L>plen)break; memcpy(rname[k],p+o,L); rname[k][L]=0; o+=L; }
    haveRoster=true;
    if(!joined){ for(int k=1;k<MAXN;k++) if(ruid[k]==myUid){ curSlot=k; joined=true; break; } }
    return;
  }
  if(cmd==CMD_WORLD && plen>=4+8+72+3 && roomOf(p)==room){
    worldRxMs=millis(); searching=false; int o=4;   // achou a sala (ouviu o lider)
    for(int k=0;k<MAXN;k++) world[k].color=p[o++];
    for(int k=0;k<MAXN;k++){ uint8_t fl=p[o]; world[k].active=fl&1; world[k].fix=fl&2; world[k].alert=fl&4;
      world[k].lat=getLE32(p+o+1)/1e7; world[k].lon=getLE32(p+o+5)/1e7; if(world[k].active) world[k].lastMs=millis(); o+=9; }
    int nh=p[o++]; uint16_t seqN=(uint16_t)p[o]|((uint16_t)p[o+1]<<8); o+=2;
    for(int i=0;i<nh;i++){ double la=getLE32(p+o)/1e7, lo=getLE32(p+o+4)/1e7; o+=8;
      uint16_t seq=seqN-(nh-1)+i; if(!haveRouteSeq||(int16_t)(seq-lastRouteSeq)>0){ routeAdd(la,lo); lastRouteSeq=seq; haveRouteSeq=true; } }
    slotDue=worldRxMs+(unsigned long)curSlot*SLOT_MS; pendingUplink=true;
  }
}
void readLoRa(){ int g=0; uint16_t id; uint8_t cmd=0,p[240],plen=0;
  while(g++<8 && lora.ReceivePacketCommand(&id,&cmd,p,&plen,15)) handleRx(cmd,p,plen); }

// ---- desenho ----
void triMarker(int cx,int cy,uint16_t col,int s){
  gfx.fillTriangle(cx,cy-s, cx-(int)(0.62f*s),cy+(int)(0.72f*s), cx+(int)(0.62f*s),cy+(int)(0.72f*s), col);
  gfx.fillTriangle(cx,cy-s, cx,cy+(int)(0.32f*s), cx+(int)(0.62f*s),cy+(int)(0.72f*s), col); // corpo cheio
}
void dotMarker(int cx,int cy,uint16_t col,int r){ gfx.fillCircle(cx,cy,r,col); gfx.drawCircle(cx,cy,r,C_WHITE); }
void roadSeg(int x0,int y0,int x1,int y1,uint16_t cas,uint16_t fil,int wc,int wf){
  for(int d=-(wc/2);d<=wc/2;d++){ gfx.drawLine(x0+d,y0,x1+d,y1,cas); gfx.drawLine(x0,y0+d,x1,y1+d,cas); }
  for(int d=-(wf/2);d<=wf/2;d++){ gfx.drawLine(x0+d,y0,x1+d,y1,fil); gfx.drawLine(x0,y0+d,x1,y1+d,fil); }
}
void card(int x,int y,int w,int h){ gfx.fillRoundRect(x,y,w,h,7,C_CARD); gfx.drawRoundRect(x,y,w,h,7,C_LINE); }
// texto com fonte de verdade (y = base da linha). Depois volta pra fonte padrao.
void txt(const GFXfont*f,int x,int y,uint16_t col,const char*s){ gfx.setFont(f); gfx.setTextSize(1); gfx.setTextColor(col); gfx.setCursor(x,y); gfx.print(s); gfx.setFont(NULL); }
int txtW(const GFXfont*f,const char*s){ int16_t x1,y1; uint16_t w,h; gfx.setFont(f); gfx.setTextSize(1); gfx.getTextBounds(s,0,0,&x1,&y1,&w,&h); gfx.setFont(NULL); return w; }
void txtC(const GFXfont*f,int cx,int y,uint16_t col,const char*s){ txt(f,cx-txtW(f,s)/2,y,col,s); }
int nearestRouteIdx(double la,double lo){ int best=-1; double bd=1e18;
  for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; double d=haversine(la,lo,route[idx].lat,route[idx].lon); if(d<bd){bd=d;best=k;} }
  return best; }

void drawMap(){
  gfx.fillScreen(C_BG);
  double clat=myLat,clon=myLon,chead=myHeading;
  if(!myFix){ gfx.setTextColor(C_AMBER); gfx.setTextSize(3); gfx.setCursor(30,CYp-20); gfx.print("PROCURANDO GPS..."); triMarker(CXp,CYp,C_BLUE,16); return; }
  int myNear=nearestRouteIdx(clat,clon);
  double myBest=1e9; if(myNear>=0){ int mi=(routeHead-routeN+myNear+ROUTE_MAX)%ROUTE_MAX; myBest=haversine(clat,clon,route[mi].lat,route[mi].lon); }
  int psx=-1,psy=-1; double plat=0,plon=0; bool havePrev=false;
  for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; int sx,sy; w2s(route[idx].lat,route[idx].lon,clat,clon,chead,sx,sy);
    bool vis=(sx>=-20&&sx<SCR_W+20&&sy>=-20&&sy<SCR_H+20);
    if(vis&&psx>=0){ bool gap=havePrev&&haversine(plat,plon,route[idx].lat,route[idx].lon)>40.0;
      if(!gap){ if(k>myNear) roadSeg(psx,psy,sx,sy,C_ROUTEC,C_ROUTE,10,5); else roadSeg(psx,psy,sx,sy,C_TRAVC,C_TRAV,8,3); } }
    psx=vis?sx:-1; psy=sy; plat=route[idx].lat; plon=route[idx].lon; havePrev=true;
  }
  int aSlot=-1; for(int k=0;k<MAXN;k++) if(world[k].active&&world[k].alert){ aSlot=k; break; }
  if(aSlot>=0){ int aN=nearestRouteIdx(world[aSlot].lat,world[aSlot].lon);
    if(aN>=0&&myNear>=0){ int a=min(aN,myNear),b=max(aN,myNear),qx=-1,qy=-1;
      for(int k=a;k<=b;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; int sx,sy; w2s(route[idx].lat,route[idx].lon,clat,clon,chead,sx,sy);
        if(qx>=0) roadSeg(qx,qy,sx,sy,0x6800,C_RED,12,6); qx=sx; qy=sy; } } }
  int meSlot=joined?curSlot:255;
  for(int k=0;k<MAXN;k++){ if(!world[k].active||k==meSlot) continue; if(millis()-world[k].lastMs>NODE_TTL) continue;
    int sx,sy; w2s(world[k].lat,world[k].lon,clat,clon,chead,sx,sy); if(sx<-30||sx>SCR_W+30||sy<-30||sy>SCR_H+30) continue;
    bool al=world[k].alert; uint16_t mc=al?C_RED:(k==0?C_AMBER:colorOf(world[k].color));
    if(k==0) triMarker(sx,sy,mc,15); else dotMarker(sx,sy,mc,9);
    gfx.setTextColor(mc); gfx.setTextSize(1); gfx.setCursor(sx+13,sy-5); gfx.print(haveRoster?rname[k]:(k==0?"Lider":"Carro")); }
  triMarker(CXp,CYp,C_BLUE,16);
  // FORA DE ROTA
  if(routeN>3 && myBest>OFFROUTE_M && ((millis()/350)%2==0)){
    for(int t=0;t<6;t++) gfx.drawRect(t,t,SCR_W-1-2*t,SCR_H-1-2*t,C_YEL);
    gfx.setTextColor(C_YEL); gfx.setTextSize(3); gfx.setCursor(CXp-110,20); gfx.print("FORA DE ROTA");
  }
  bool off=(worldRxMs==0||millis()-worldRxMs>LEAD_TTL);
  if(off){ card(SCR_W/2-110,8,220,30); gfx.setTextColor(C_RED); gfx.setTextSize(2); gfx.setCursor(SCR_W/2-90,16); gfx.print("LIDER OFFLINE"); }
}
void drawUI(){
  char b[32];
  card(10,10,200,54); gfx.fillCircle(30,37,6,C_BLUE);
  gfx.setTextColor(C_WHITE); gfx.setTextSize(2); gfx.setCursor(46,20); snprintf(b,sizeof(b),"Sala %lu",(unsigned long)room); gfx.print(b);
  gfx.setTextColor(C_MUT); gfx.setTextSize(1); gfx.setCursor(46,44); gfx.print(leaderRole?"voce e o LIDER":(joined?"seguidor":"entrando..."));
  // roster
  int rw=170, rx=SCR_W-rw-8, ry=10, rh=24;
  for(int k=0;k<MAXN;k++){ if(!world[k].active||millis()-world[k].lastMs>NODE_TTL) continue; card(rx,ry,rw,rh);
    bool me=(joined&&k==(int)curSlot), ld=(k==0); uint16_t col=me?C_BLUE:(ld?C_AMBER:colorOf(world[k].color));
    if(me||ld) gfx.fillTriangle(rx+13,ry+4,rx+6,ry+19,rx+20,ry+19,col); else gfx.fillCircle(rx+13,ry+12,5,col);
    gfx.setTextColor(world[k].alert?C_RED:C_WHITE); gfx.setTextSize(1); gfx.setCursor(rx+28,ry+8);
    gfx.print(haveRoster?rname[k]:(ld?"Lider":"Carro")); ry+=rh+4; if(ry>SCR_H-90) break; }
  // distancia ao lider
  if(leaderRole){ int c=0; for(int k=1;k<MAXN;k++) if(world[k].active&&millis()-world[k].lastMs<NODE_TTL)c++;
    card(10,SCR_H-84,200,74); gfx.setTextColor(C_MUT); gfx.setTextSize(1); gfx.setCursor(24,SCR_H-76); gfx.print("LIDERANDO");
    gfx.setTextColor(C_WHITE); gfx.setTextSize(4); gfx.setCursor(20,SCR_H-56); snprintf(b,sizeof(b),"%d segs",c); gfx.print(b); }
  else if(world[0].active && myFix){ double d=haversine(myLat,myLon,world[0].lat,world[0].lon);
    card(10,SCR_H-84,200,74); gfx.setTextColor(C_MUT); gfx.setTextSize(1); gfx.setCursor(24,SCR_H-76); gfx.print("LIDER");
    gfx.setTextColor(C_WHITE); gfx.setTextSize(5); gfx.setCursor(20,SCR_H-60);
    if(d>=1000) snprintf(b,sizeof(b),"%.1fkm",d/1000.0); else snprintf(b,sizeof(b),"%dm",(int)d); gfx.print(b); }
  // FAB alerta (toggle: fica aceso ate desligar)
  bool on=myAlert;
  gfx.fillCircle(abX,abY,abR, on?C_RED:C_CARD); gfx.drawCircle(abX,abY,abR,C_RED);
  uint16_t tc=on?C_WHITE:C_RED; gfx.fillTriangle(abX,abY-16,abX-16,abY+13,abX+16,abY+13,tc);
  gfx.fillRect(abX-2,abY-6,4,11,on?C_RED:C_CARD); gfx.fillRect(abX-2,abY+8,4,4,on?C_RED:C_CARD);
}
// ---- tela inicial: escolher o papel DESTA saida ----
void homeRects(int&bx,int&bw,int&bh,int&by1,int&by2){ bw=SCR_W-80; bx=40; bh=132; by1=168; by2=by1+bh+22; }
void drawHome(){
  gfx.fillScreen(C_BG);
  txt(&FreeSansBold24pt7b,40,78,C_WHITE,"TRILHA");
  txt(&FreeSans12pt7b,42,112,C_MUT,"Escolha o papel para esta saida");
  int bx,bw,bh,by1,by2; homeRects(bx,bw,bh,by1,by2);
  // CRIAR (preenchido)
  gfx.fillRoundRect(bx,by1,bw,bh,16,C_ROUTE);
  txt(&FreeSansBold24pt7b,bx+40,by1+66,C_BG,"CRIAR SALA");
  txt(&FreeSans12pt7b,bx+42,by1+98,C_BG,"voce vira o lider");
  // ENTRAR (contorno)
  gfx.drawRoundRect(bx,by2,bw,bh,16,C_BLUE); gfx.drawRoundRect(bx+1,by2+1,bw-2,bh-2,16,C_BLUE); gfx.drawRoundRect(bx+2,by2+2,bw-4,bh-4,16,C_BLUE);
  txt(&FreeSansBold24pt7b,bx+40,by2+66,C_BLUE,"ENTRAR NA SALA");
  txt(&FreeSans12pt7b,bx+42,by2+98,C_MUT,"voce vira seguidor");
}
void homeTouch(int tx,int ty){ int bx,bw,bh,by1,by2; homeRects(bx,bw,bh,by1,by2);
  if(tx>=bx&&tx<=bx+bw){ if(ty>=by1&&ty<=by1+bh){ pendingRole=1; uiPage=1; codeLen=0; codeBuf[0]=0; }
    else if(ty>=by2&&ty<=by2+bh){ pendingRole=0; uiPage=1; codeLen=0; codeBuf[0]=0; } } }
// ---- teclado do codigo da sala ----
static const char* KPLAB[12]={"1","2","3","4","5","6","7","8","9","<","0","OK"};
void kpRect(int i,int&x,int&y,int&w,int&h){
  int kpW=(int)(SCR_W*0.6),kpX=(SCR_W-kpW)/2,kpTop=(int)(SCR_H*0.30);
  int kw=kpW/3, kh=(SCR_H-kpTop-10)/4, r=i/3,c=i%3; x=kpX+c*kw+4; y=kpTop+r*kh+4; w=kw-8; h=kh-8;
}
void drawKeypad(){
  gfx.fillScreen(C_BG);
  txt(&FreeSans12pt7b,24,34,C_MUT, pendingRole==1?"Criar sala - digite o codigo":"Entrar - digite o codigo da sala");
  int bw=66, gap=14, bx=(SCR_W-(bw*5+gap*4))/2, by=52;
  for(int i=0;i<5;i++){ int x=bx+i*(bw+gap); gfx.drawRoundRect(x,by,bw,bw,10,i<codeLen?C_ROUTE:C_LINE);
    if(i<codeLen){ char c[2]={codeBuf[i],0}; txtC(&FreeSansBold24pt7b,x+bw/2,by+bw/2+13,C_WHITE,c); } }
  for(int i=0;i<12;i++){ int x,y,w,h; kpRect(i,x,y,w,h); gfx.fillRoundRect(x,y,w,h,10,C_CARD); gfx.drawRoundRect(x,y,w,h,10,C_LINE);
    uint16_t c=(i==9)?C_RED:(i==11?C_ROUTE:C_WHITE); txtC(&FreeSansBold18pt7b,x+w/2,y+h/2+8,c,KPLAB[i]); }
}
void keypadTouch(int tx,int ty){
  for(int i=0;i<12;i++){ int x,y,w,h; kpRect(i,x,y,w,h);
    if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){
      if(i==9){ if(codeLen>0) codeBuf[--codeLen]=0; else uiPage=0; }
      else if(i==11){ if(codeLen==5){ room=atol(codeBuf); if(room==0)room=1; leaderRole=(pendingRole==1);
        if(leaderRole){ curSlot=0; joined=true; ruid[0]=myUid; strncpy(rname[0],myName,15); rname[0][15]=0; rcolor[0]=myColor; haveRoster=true; searching=false; }
        else { joined=false; curSlot=0; searching=true; worldRxMs=0; } } }
      else if(codeLen<5){ codeBuf[codeLen++]=KPLAB[i][0]; codeBuf[codeLen]=0; }
      return;
    }
  }
}
// tela de busca (seguidor procurando a sala)
void searchRects(int&x,int&y,int&w,int&h){ w=220; h=60; x=(SCR_W-w)/2; y=SCR_H-h-40; }
void drawSearching(){
  gfx.fillScreen(C_BG);
  char rm[10]; snprintf(rm,sizeof(rm),"%lu",(unsigned long)room);
  txtC(&FreeSans12pt7b, CXp, 130, C_MUT, "Procurando sala");
  txtC(&FreeSansBold24pt7b, CXp, 195, C_WHITE, rm);
  int nd=(millis()/450)%4; char aw[40]="aguardando o lider"; int L=strlen(aw); for(int i=0;i<nd&&L+i<38;i++)aw[L+i]='.'; aw[L+nd]=0;
  txtC(&FreeSans12pt7b, CXp, 245, C_MUT, aw);
  int x,y,w,h; searchRects(x,y,w,h); gfx.drawRoundRect(x,y,w,h,14,C_LINE); gfx.drawRoundRect(x+1,y+1,w-2,h-2,14,C_LINE);
  txtC(&FreeSansBold18pt7b, CXp, y+h/2+8, C_MUT, "VOLTAR");
}
void handleTouch(){
  GDTpoint_t p[5]; uint8_t n=touch.getTouchPoints(p);
  if(n>0){ int tx=p[0].y, ty=(SCR_H-1)-p[0].x;   // mapeamento original do GIGA
    if(!touchWasDown){
      if(room==0){ if(uiPage==0) homeTouch(tx,ty); else keypadTouch(tx,ty); }
      else if(searching){ int x,y,w,h; searchRects(x,y,w,h); if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ room=0; searching=false; uiPage=0; joined=false; } }
      else if((tx-abX)*(tx-abX)+(ty-abY)*(ty-abY) <= (abR+8)*(abR+8)) myAlert=!myAlert;   // liga/desliga
    }
    touchWasDown=true; } else touchWasDown=false;
}

void setup(){
  Serial.begin(115200); Serial1.begin(9600); Serial2.begin(9600); delay(150);
  lora.begin(false); lora.localread(); myUid=lora.localUniqueId;
  gfx.begin(); gfx.setRotation(1);
  SCR_W=gfx.width(); SCR_H=gfx.height(); CXp=SCR_W/2; CYp=SCR_H/2;
  abR=40; abX=SCR_W-abR-14; abY=SCR_H-abR-14;
  touch.begin();
  for(int k=0;k<MAXN;k++){ world[k]=Node(); world[k].color=k; snprintf(rname[k],16,k==0?"Lider":"Carro %d",k); rcolor[k]=k; ruid[k]=0; }
  gfx.startBuffering(); gfx.fillScreen(C_BG); gfx.endBuffering();
  Serial.print("== GRUPO GIGA (seguidor) == sala="); Serial.print(room); Serial.print(" slot="); Serial.println(mySlot);
}
void loop(){
  handleSerial(); readGPS(); readLoRa(); handleTouch();
  unsigned long now=millis();
  if(leaderRole||joined){ int msi=leaderRole?0:curSlot; world[msi].active=true; world[msi].fix=myFix; world[msi].alert=myAlert;
    world[msi].lat=myLat; world[msi].lon=myLon; world[msi].lastMs=now; }
  if(room!=0){
    if(leaderRole){ leaderRecordOwnPath(); static uint8_t cyc=0; if(now-lastCycle>=CYCLE_MS){ if(myFix){ sendWorld(); if((cyc++%3)==0) sendRoster(); } lastCycle=now; } }
    else if(!joined){ static unsigned long lj=0; if(now-lj>1500){ sendJoin(); lj=now; } }
    else if(pendingUplink && now>=slotDue){ sendUplink(); pendingUplink=false; }
  }
  if(now-lastDraw>250){ gfx.startBuffering(); if(room==0){ if(uiPage==0) drawHome(); else drawKeypad(); } else if(searching) drawSearching(); else { drawMap(); drawUI(); } gfx.endBuffering(); lastDraw=now; }
  if(now-lastDbg>2000){ int c=0; for(int k=0;k<MAXN;k++) if(world[k].active&&now-world[k].lastMs<NODE_TTL)c++;
    Serial.print("GIGA seg fix="); Serial.print(myFix?"S":"N"); Serial.print(" world="); Serial.print(worldRxMs?"ok":"--"); Serial.print(" nos="); Serial.println(c); lastDbg=now; }
}
