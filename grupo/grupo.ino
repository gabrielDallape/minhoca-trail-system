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
#include <WiFi.h>
#include <WebServer.h>

static LGFX  tft;
LoRaMESH     lora(&Serial1);
TinyGPSPlus  gps;
Preferences  prefs;
WebServer    server(80);

// ---------------- protocolo ----------------
const uint16_t BCAST      = 2047;
const uint16_t LEADER_ID  = 0;
const uint8_t  CMD_WORLD  = 0x30;   // lider -> broadcast (estado do mundo)
const uint8_t  CMD_ROSTER = 0x31;   // lider -> broadcast (nomes + cores de todos)
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
struct Cfg { uint32_t room; uint8_t role; uint8_t slot; uint8_t color; char name[16]; };
Cfg cfg = { 0, 0, 1, 0, "Carro" };       // room 0 = nao configurado -> mostra teclado
bool isLeader(){ return cfg.role==1; }
char codeBuf[6]=""; int codeLen=0;       // teclado do codigo da sala

// ---------------- estado do mundo ----------------
struct Node { bool active; double lat,lon; bool fix; bool alert; unsigned long lastMs; uint8_t color; };
Node world[MAXN];
// roster (nome + cor de cada slot). Lider e a autoridade; retransmite por CMD_ROSTER.
char    rname[MAXN][16];
uint8_t rcolor[MAXN];
bool    haveRoster=false;
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
#define C_YEL   0xFE60
const double OFFROUTE_M = 30.0;
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
void saveRoster(){ prefs.begin("grupo",false); prefs.putBytes("rn",rname,sizeof(rname)); prefs.putBytes("rc",rcolor,sizeof(rcolor)); prefs.end(); }
void initRoster(){
  prefs.begin("grupo",true); size_t g=prefs.getBytes("rn",rname,sizeof(rname)); prefs.getBytes("rc",rcolor,sizeof(rcolor)); prefs.end();
  if(g<sizeof(rname)){ for(int k=0;k<MAXN;k++){ snprintf(rname[k],16,k==0?"Lider":"Carro %d",k); rcolor[k]=k; } }
  for(int k=0;k<MAXN;k++) rname[k][15]=0;
}
void loadCfg(){ prefs.begin("grupo",true); prefs.getBytes("cfg",&cfg,sizeof(cfg)); prefs.end();
  if(cfg.room>99999){ cfg.room=0; cfg.role=0; cfg.slot=1; cfg.color=0; strcpy(cfg.name,"Carro"); }
  cfg.name[15]=0; if(cfg.name[0]==0) strcpy(cfg.name,"Carro"); }
void printCfg(){ Serial.print("[cfg] sala="); Serial.print(cfg.room);
  Serial.print(" papel="); Serial.print(isLeader()?"LIDER":"SEGUIDOR");
  Serial.print(" slot="); Serial.print(cfg.slot); Serial.print(" cor="); Serial.print(cfg.color);
  Serial.print(" nome="); Serial.println(cfg.name); }
void handleSerialCfg(){
  if(!Serial.available()) return;
  char c=Serial.read();
  if(c=='L'){ cfg.role=1; cfg.slot=0; saveCfg(); printCfg(); }
  else if(c=='F'){ while(!Serial.available()){} int n=Serial.read()-'0'; if(n>=1&&n<MAXN){ cfg.role=0; cfg.slot=n; saveCfg(); printCfg(); } }
  else if(c=='C'){ while(!Serial.available()){} int n=Serial.read()-'0'; if(n>=0&&n<8){ cfg.color=n; saveCfg(); printCfg(); } }
  else if(c=='R'){ uint32_t v=0; for(int i=0;i<5;i++){ while(!Serial.available()){} char d=Serial.read(); if(d>='0'&&d<='9') v=v*10+(d-'0'); } cfg.room=v; saveCfg(); printCfg(); }
  else if(c=='P'){ printCfg(); }
  else if(c=='X'){ cfg.room=0; codeLen=0; codeBuf[0]=0; saveCfg(); printCfg(); }   // sair da sala
}

// ---------------- config por WiFi (pagina no celular) ----------------
static const char* COLOR_HEX[8] = {"#388cff","#2a9d8f","#9694d6","#e7a842","#e36e88","#2ec868","#ff9646","#22d3ee"};
String pageHtml(){
  int conn=0; for(int k=0;k<MAXN;k++) if(world[k].active && millis()-world[k].lastMs<NODE_TTL) conn++;
  String s="<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'><title>Trilha</title>";
  s+="<style>body{font-family:system-ui;background:#0d131b;color:#e6edf5;margin:0;padding:22px}h1{font-size:22px;margin:0 0 2px}"
     ".m{color:#8a97a8;font-size:13px;margin:0 0 18px}label{display:block;font-size:12px;color:#8a97a8;text-transform:uppercase;letter-spacing:.5px;margin:16px 0 6px}"
     "input,select{width:100%;padding:11px;font-size:16px;background:#111a24;border:1px solid #2a3a4a;border-radius:9px;color:#fff;box-sizing:border-box}"
     ".sw{display:flex;gap:10px;flex-wrap:wrap;margin-top:4px}.sw label{display:inline-block;width:34px;height:34px;border-radius:50%;margin:0;cursor:pointer;border:3px solid transparent}"
     ".sw input{display:none}.sw input:checked+span{outline:3px solid #fff;outline-offset:2px}.sw span{display:block;width:100%;height:100%;border-radius:50%}"
     ".r{display:flex;gap:10px}.r label{flex:1;text-align:center;padding:11px;border:1px solid #2a3a4a;border-radius:9px;text-transform:none;font-size:15px;color:#e6edf5}"
     ".r input{display:none}.r input:checked+span{color:#0d131b;font-weight:700}.r label:has(input:checked){background:#2fb0a0;border-color:#2fb0a0}"
     "button{margin-top:22px;width:100%;padding:14px;font-size:16px;font-weight:700;background:#2fb0a0;color:#04140f;border:none;border-radius:10px}"
     ".pill{display:inline-block;background:#111a24;border:1px solid #2a3a4a;border-radius:20px;padding:4px 12px;font-size:12px;color:#34c878}</style></head><body>";
  s+="<h1>Trilha — configurar</h1><p class=m>Sala e papel deste aparelho.</p>";
  s+="<p><span class=pill>"+String(conn)+" na sala</span></p>";
  s+="<form action=/save method=get>";
  s+="<label>Codigo da sala</label><input name=sala inputmode=numeric maxlength=5 value='"+String(cfg.room)+"'>";
  s+="<label>Papel</label><div class=r>";
  s+="<label><input type=radio name=role value=1 "+String(cfg.role==1?"checked":"")+"><span>Lider</span></label>";
  s+="<label><input type=radio name=role value=0 "+String(cfg.role==0?"checked":"")+"><span>Seguidor</span></label></div>";
  s+="<label>Vaga (slot) — 1 a 7 se seguidor</label><select name=slot>";
  for(int i=0;i<MAXN;i++){ s+="<option value="+String(i)+(cfg.slot==i?" selected":"")+">"+String(i)+(i==0?" (lider)":"")+"</option>"; }
  s+="</select>";
  s+="<label>Nome</label><input name=nome maxlength=15 value='"+String(cfg.name)+"'>";
  s+="<label>Cor</label><div class=sw>";
  for(int i=0;i<8;i++){ s+="<label><input type=radio name=cor value="+String(i)+(cfg.color==i?" checked":"")+"><span style='background:"+String(COLOR_HEX[i])+"'></span></label>"; }
  s+="</div>";
  if(cfg.role==1){   // so o lider edita o roster de todos
    s+="<label>Carros da sala (nome e cor)</label>";
    for(int k=1;k<MAXN;k++){
      s+="<div style='display:flex;gap:8px;align-items:center;margin-bottom:6px'>";
      s+="<span style='color:#8a97a8;width:18px'>"+String(k)+"</span>";
      s+="<input name=n"+String(k)+" maxlength=15 value='"+String(rname[k])+"' style='flex:1'>";
      s+="<select name=c"+String(k)+" style='width:64px'>";
      for(int c=0;c<8;c++) s+="<option value="+String(c)+(rcolor[k]==c?" selected":"")+">"+String(c)+"</option>";
      s+="</select></div>";
    }
  }
  s+="<button type=submit>Salvar</button></form>";
  s+="<p style='margin-top:16px'><a href=/leave style='color:#e0423c'>Sair da sala</a></p></body></html>";
  return s;
}
void handleRoot(){ server.send(200,"text/html",pageHtml()); }
void handleSave(){
  if(server.hasArg("sala")){ uint32_t v=server.arg("sala").toInt(); if(v>0&&v<=99999) cfg.room=v; }
  if(server.hasArg("role")) cfg.role=server.arg("role").toInt()?1:0;
  if(server.hasArg("slot")){ int s=server.arg("slot").toInt(); if(s>=0&&s<MAXN) cfg.slot=s; }
  if(cfg.role==1) cfg.slot=0;
  if(server.hasArg("cor")){ int c=server.arg("cor").toInt(); if(c>=0&&c<8) cfg.color=c; }
  if(server.hasArg("nome")){ String n=server.arg("nome"); n.toCharArray(cfg.name,16); }
  saveCfg();
  if(cfg.role==1){   // lider grava o roster de todos
    strncpy(rname[0],cfg.name,15); rname[0][15]=0; rcolor[0]=cfg.color;
    for(int k=1;k<MAXN;k++){
      String kn="n"+String(k), kc="c"+String(k);
      if(server.hasArg(kn)){ server.arg(kn).toCharArray(rname[k],16); rname[k][15]=0; }
      if(server.hasArg(kc)){ int c=server.arg(kc).toInt(); if(c>=0&&c<8) rcolor[k]=c; }
    }
    saveRoster(); haveRoster=true;
  }
  printCfg();
  server.sendHeader("Location","/"); server.send(303);
}
void handleLeave(){ cfg.room=0; codeLen=0; codeBuf[0]=0; saveCfg(); server.sendHeader("Location","/"); server.send(303); }
void startWiFi(){
  WiFi.mode(WIFI_AP);
  char ssid[28]; snprintf(ssid,sizeof(ssid),"Trilha-%s",cfg.name);
  WiFi.softAP(ssid);
  server.on("/",handleRoot); server.on("/save",handleSave); server.on("/leave",handleLeave);
  server.begin();
  Serial.print("WiFi AP '"); Serial.print(ssid); Serial.print("'  ->  http://"); Serial.println(WiFi.softAPIP());
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
  for(int k=0;k<MAXN;k++) p[o++]=rcolor[k];
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
void sendRoster(){
  uint8_t p[3+MAXN*17]; int o=0;
  p[o++]=cfg.room&0xFF; p[o++]=(cfg.room>>8)&0xFF; p[o++]=(cfg.room>>16)&0xFF;
  for(int k=0;k<MAXN;k++){ p[o++]=rcolor[k]; int L=strlen(rname[k]); if(L>15)L=15; p[o++]=(uint8_t)L; memcpy(p+o,rname[k],L); o+=L; }
  lora.PrepareFrameCommand(BCAST,CMD_ROSTER,p,o); lora.SendPacket();
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
    if(cmd==CMD_ROSTER && plen>=3 && roomOf(p)==cfg.room){
      int o=3;
      for(int k=0;k<MAXN && o<plen;k++){ rcolor[k]=p[o++]; int L=p[o++]; if(L>15)L=15; if(o+L>plen)break; memcpy(rname[k],p+o,L); rname[k][L]=0; o+=L; }
      haveRoster=true; return;
    }
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
  double myBest=1e9; if(myNear>=0){ int mi=(routeHead-routeN+myNear+ROUTE_MAX)%ROUTE_MAX; myBest=haversine(clat,clon,route[mi].lat,route[mi].lon); }
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
    uint16_t mc = al?C_RED:(k==0?C_AMBER:colorOf(world[k].color));
    if(k==0) triMarker(sx,sy,0,mc,13);                      // lider
    else dotMarker(sx,sy,mc,8);
    tft.setTextColor(mc); tft.setTextSize(1); tft.setCursor(sx+11,sy-4);   // etiqueta de nome
    tft.print(haveRoster?rname[k]:(k==0?"Lider":"Carro"));
  }
  triMarker(CXp,CYp,0,C_BLUE,14);                            // eu

  // FORA DE ROTA (pisca amarelo) - so seguidor, longe do caminho do lider
  if(!isLeader() && routeN>3 && myBest>OFFROUTE_M && ((millis()/350)%2==0)){
    for(int t=0;t<5;t++) tft.drawRect(t,t,SCR_W-1-2*t,SCR_H-1-2*t,C_YEL);
    tft.setTextColor(C_YEL); tft.setTextSize(2); tft.setCursor(CXp-72,16); tft.print("FORA DE ROTA");
  }
  if(leadOffline){ card(SCR_W/2-90,8,180,26); tft.setTextColor(C_RED); tft.setTextSize(1); tft.setCursor(SCR_W/2-70,16); tft.print("LIDER OFFLINE"); }
}

void drawUI(){
  char b[32];
  // status (topo esq)
  card(8,8,150,40);
  tft.fillCircle(24,28,5, isLeader()?C_AMBER:C_BLUE);
  tft.setTextColor(C_WHITE); tft.setTextSize(1); tft.setCursor(36,16); snprintf(b,sizeof(b),"Sala %lu",(unsigned long)cfg.room); tft.print(b);
  tft.setTextColor(C_MUT); tft.setCursor(36,30); tft.print(isLeader()?"voce e o LIDER":"seguidor");
  // lista de carros (roster) no canto direito
  int rw=100, rx=SCR_W-rw-6, ry=8, rh=17;
  for(int k=0;k<MAXN;k++){
    if(!world[k].active || millis()-world[k].lastMs>NODE_TTL) continue;
    card(rx,ry,rw,rh);
    bool meRow=(k==(int)cfg.slot), ldRow=(k==0);
    uint16_t col = meRow?C_BLUE : (ldRow?C_AMBER : colorOf(world[k].color));
    if(meRow||ldRow) tft.fillTriangle(rx+9,ry+3,rx+4,ry+13,rx+14,ry+13,col);
    else tft.fillCircle(rx+9,ry+8,4,col);
    tft.setTextColor(world[k].alert?C_RED:C_WHITE); tft.setTextSize(1); tft.setCursor(rx+20,ry+5);
    tft.print(haveRoster?rname[k]:(ldRow?"Lider":"Carro"));
    ry+=rh+3; if(ry>SCR_H-64) break;
  }
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

// ---- teclado do codigo da sala (mostrado quando room==0) ----
static const char* KPLAB[12]={"1","2","3","4","5","6","7","8","9","<","0","OK"};
void kpRect(int i,int&x,int&y,int&w,int&h){
  int kpW=(int)(SCR_W*0.66),kpX=(SCR_W-kpW)/2,kpTop=(int)(SCR_H*0.30);
  int kw=kpW/3, kh=(SCR_H-kpTop-8)/4, r=i/3,c=i%3;
  x=kpX+c*kw+3; y=kpTop+r*kh+3; w=kw-6; h=kh-6;
}
void drawKeypad(){
  tft.fillScreen(C_BG);
  tft.setTextColor(C_MUT); tft.setTextSize(2); tft.setCursor(14,10); tft.print("Codigo da sala");
  int bw=SCR_W/9, bx=(SCR_W-(bw*5+4*6))/2, by=(int)(SCR_H*0.15);
  for(int i=0;i<5;i++){ int x=bx+i*(bw+6); tft.drawRoundRect(x,by,bw,bw,5,i<codeLen?C_ROUTE:C_LINE);
    if(i<codeLen){ tft.setTextColor(C_WHITE); tft.setTextSize(3); tft.setCursor(x+bw/2-8,by+bw/2-10); tft.print(codeBuf[i]); } }
  for(int i=0;i<12;i++){ int x,y,w,h; kpRect(i,x,y,w,h); card(x,y,w,h);
    uint16_t c=(i==9)?C_RED:(i==11?C_GREEN:C_WHITE); tft.setTextColor(c); tft.setTextSize(3);
    tft.setCursor(x+w/2-8,y+h/2-10); tft.print(KPLAB[i]); }
}
void keypadTouch(int tx,int ty){
  for(int i=0;i<12;i++){ int x,y,w,h; kpRect(i,x,y,w,h);
    if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){
      if(i==9){ if(codeLen>0) codeBuf[--codeLen]=0; }
      else if(i==11){ if(codeLen==5){ cfg.room=atol(codeBuf); if(cfg.room==0)cfg.room=1; saveCfg(); } }
      else if(codeLen<5){ codeBuf[codeLen++]=KPLAB[i][0]; codeBuf[codeLen]=0; }
      return;
    }
  }
}
void handleTouch(){
  int32_t tx,ty;
  if(tft.getTouch(&tx,&ty)){
    if(!touchWasDown){
      if(cfg.room==0){ keypadTouch(tx,ty); }
      else { int fx=SCR_W-30, fy=SCR_H-30; if((tx-fx)*(tx-fx)+(ty-fy)*(ty-fy) <= 30*30) myAlertUntil=millis()+4000; }
    }
    touchWasDown=true;
  } else touchWasDown=false;
}

void setup(){
  Serial.begin(115200);
  loadCfg(); initRoster();
  tft.init(); tft.setRotation(1);
  { uint16_t calData[8]={549,3553,619,389,3613,3475,3618,378}; tft.setTouchCalibrate(calData); } // CYD (XPT2046); Waveshare cap ajusta depois
  SCR_W=tft.width(); SCR_H=tft.height(); CXp=SCR_W/2; CYp=SCR_H/2;
  tft.fillScreen(C_BG);
  Serial1.begin(9600,SERIAL_8N1,35,22);   // LoRa
  Serial2.begin(9600,SERIAL_8N1,27,-1);   // GPS
  delay(150);
  lora.localread();
  for(int k=0;k<MAXN;k++){ world[k]=Node(); world[k].color=rcolor[k]; }
  if(isLeader()){ strncpy(rname[0],cfg.name,15); rname[0][15]=0; rcolor[0]=cfg.color; haveRoster=true; }
  startWiFi();
  Serial.print("== GRUPO == "); printCfg();
  Serial.print("LoRa localId="); Serial.print(lora.localId); Serial.print(" uid="); Serial.println(lora.localUniqueId);
}

void loop(){
  handleSerialCfg(); server.handleClient();
  readGPS(); readLoRa(); handleTouch();
  unsigned long now=millis();

  // meu no no mundo
  world[cfg.slot].active=true; world[cfg.slot].fix=myFix; world[cfg.slot].alert=(now<myAlertUntil);
  world[cfg.slot].lat=myLat; world[cfg.slot].lon=myLon; world[cfg.slot].lastMs=now; world[cfg.slot].color=cfg.color;

  if(cfg.room!=0){
    if(isLeader()){
      leaderRecordOwnPath();
      static uint8_t cyc=0;
      if(now-lastCycle>=CYCLE_MS){ if(myFix){ sendWorld(); if((cyc++%3)==0) sendRoster(); } lastCycle=now; }   // quieto ate ter fix
    } else {
      if(pendingUplink && now>=slotDue){ sendUplink(); pendingUplink=false; }
    }
  }

  if(now-lastDraw>250){ if(cfg.room==0) drawKeypad(); else { drawMap(); drawUI(); } lastDraw=now; }
  if(now-lastDbg>2000){
    Serial.print(isLeader()?"LIDER":"SEG"); Serial.print(" fix="); Serial.print(myFix?"S":"N");
    Serial.print(" sat="); Serial.print(mySats); int n=0; for(int k=0;k<MAXN;k++) if(world[k].active&&now-world[k].lastMs<NODE_TTL)n++;
    Serial.print(" nos="); Serial.println(n); lastDbg=now;
  }
}
