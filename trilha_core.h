/*
 * ============================================================================
 *  TRILHA CORE  —  "codigo padrao" compartilhado por TODOS os aparelhos.
 *  Contem o que NAO depende de hardware: protocolo LoRa, estado do grupo,
 *  temas (cores), regras e empacotamento/desempacotamento de pacotes.
 *
 *  Cada aparelho (CYD/Waveshare = LovyanGFX ; GIGA = GigaDisplay_GFX) tem a
 *  sua "casca" (o .ino) que cuida SO da tela, do toque, do LoRa e de salvar.
 *  A casca chama as funcoes daqui e desenha lendo este estado.
 *
 *  Uso na casca:
 *    - Enviar: monta com packWorld/packRoster/packUplink/packJoin e manda no LoRa.
 *    - Receber: passa (cmd,payload,len) pra parseRx() -> atualiza o estado.
 *    - Persistir: a casca salva/carrega g_room,g_role,g_slot,g_color,g_name,themeIdx.
 * ============================================================================
 */
#pragma once
#include <Arduino.h>

// ---------------- protocolo ----------------
static const uint16_t BCAST      = 2047;
static const uint16_t LEADER_ID  = 0;
static const uint8_t  CMD_WORLD  = 0x30;   // lider -> broadcast (estado do mundo)
static const uint8_t  CMD_ROSTER = 0x31;   // lider -> broadcast (nomes+cores+uid)
static const uint8_t  CMD_UPLINK = 0x32;   // seguidor -> lider (posicao)
static const uint8_t  CMD_JOIN   = 0x33;   // seguidor -> lider (entrar)
static const int      MAXN       = 8;
static const int      ROUTE_MAX  = 140;
static const int      HIST_N     = 12;
static const double   R_EARTH    = 6371000.0;
static const float    STEP_M     = 5.0f;
static const unsigned long SLOT_MS  = 450;
static const unsigned long CYCLE_MS = (unsigned long)MAXN*SLOT_MS;
static const unsigned long NODE_TTL = 12000;
static const unsigned long LEAD_TTL = 8000;
static const double   OFFROUTE_M = 30.0;

// ---------------- temas ----------------
#define RGB16(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
#define FL_CORNERS 1
#define FL_GRID    2
#define FL_CROSS   4
struct Theme{ uint16_t bg,card,line,you,leader,route,routec,trav,travc,red,mut; uint8_t fl; const char* name; };
static const Theme THEMES[3]={
 { RGB16(11,9,6),   RGB16(29,24,16), RGB16(61,49,25), RGB16(40,216,255),  RGB16(255,210,62),  RGB16(255,149,0),  RGB16(90,58,0),  RGB16(90,70,34), RGB16(50,40,20), RGB16(255,59,48), RGB16(179,155,111), FL_CORNERS,       "RALLY" },
 { RGB16(10,13,9),  RGB16(21,27,16), RGB16(47,61,32), RGB16(232,255,207), RGB16(255,122,26),  RGB16(166,255,77), RGB16(40,70,18), RGB16(60,74,42), RGB16(35,45,26), RGB16(255,47,32), RGB16(127,144,104),FL_GRID|FL_CROSS, "TATICO" },
 { RGB16(10,12,16), RGB16(24,30,38), RGB16(32,42,52), RGB16(0,229,255),   RGB16(255,255,255), RGB16(0,229,255),  RGB16(16,50,60), RGB16(38,66,74), RGB16(28,46,52), RGB16(255,56,96), RGB16(102,114,126),0,                "HUD" },
};
static const uint16_t PALETTE[8]={0x3C7F,0x2CF1,0x9694,0xEC88,0xE36E,0x2648,0xFD20,0x07FF};
inline uint16_t colorOf(uint8_t i){ return PALETTE[i&7]; }

// cores ativas (a casca desenha usando estas variaveis)
inline uint16_t &C(uint8_t i){ static uint16_t c[13]; return c[i]; }
enum{ CBG,CCARD,CLINE,CBLUE,CAMBER,CROUTE,CROUTEC,CTRAV,CTRAVC,CRED,CMUT,CYEL,CFLAGS };
#define C_BG    C(CBG)
#define C_CARD  C(CCARD)
#define C_LINE  C(CLINE)
#define C_BLUE  C(CBLUE)
#define C_AMBER C(CAMBER)
#define C_ROUTE C(CROUTE)
#define C_ROUTEC C(CROUTEC)
#define C_TRAV  C(CTRAV)
#define C_TRAVC C(CTRAVC)
#define C_RED   C(CRED)
#define C_MUT   C(CMUT)
#define C_YEL   C(CYEL)
#define C_FL    C(CFLAGS)
static const uint16_t C_WHITE=0xFFFF;
inline int themeIdxRef(){ static int t=0; return t; }   // (nao usar direto; ver g_theme)
extern int g_theme;
inline void applyTheme(int i){ if(i<0||i>2)i=0; g_theme=i; const Theme&t=THEMES[i];
  C_BG=t.bg;C_CARD=t.card;C_LINE=t.line;C_BLUE=t.you;C_AMBER=t.leader;C_ROUTE=t.route;C_ROUTEC=t.routec;C_TRAV=t.trav;C_TRAVC=t.travc;C_RED=t.red;C_MUT=t.mut;C_YEL=0xFE60;C_FL=t.fl; }

// ---------------- estado do grupo ----------------
struct Node{ bool active; double lat,lon; bool fix; bool alert; unsigned long lastMs; uint8_t color; };
struct Geo { double lat,lon; };

// definidos no .ino (uma vez por sketch) via TRILHA_CORE_DEFINE
extern Node world[MAXN];
extern Geo  route[ROUTE_MAX];
extern int  routeN, routeHead;
extern uint16_t routeSeq, lastRouteSeq;
extern bool haveRouteSeq;
extern char rname[MAXN][16];
extern uint8_t rcolor[MAXN];
extern uint32_t ruid[MAXN];
extern bool haveRoster;
extern uint32_t g_room; extern uint8_t g_role, g_slot, g_color; extern char g_name[16];
extern int g_theme;
extern uint32_t myUid; extern bool joined; extern uint8_t curSlot;
extern double myLat, myLon; extern bool myFix; extern int mySats; extern float mySpeed, myHeading;
extern unsigned long myLastFix, worldRxMs, slotDue, lastCycle;
extern bool pendingUplink, myAlert, searching, wantRoster;
extern double lastAddLat, lastAddLon; extern bool haveAdd;

inline bool isLeader(){ return g_role==1; }

// macro que a casca coloca UMA vez pra criar as variaveis
#define TRILHA_CORE_DEFINE \
  Node world[MAXN]; Geo route[ROUTE_MAX]; int routeN=0,routeHead=0; uint16_t routeSeq=0,lastRouteSeq=0; bool haveRouteSeq=false; \
  char rname[MAXN][16]; uint8_t rcolor[MAXN]; uint32_t ruid[MAXN]; bool haveRoster=false; \
  uint32_t g_room=0; uint8_t g_role=0,g_slot=1,g_color=0; char g_name[16]="Carro"; int g_theme=0; \
  uint32_t myUid=0; bool joined=false; uint8_t curSlot=0; \
  double myLat=0,myLon=0; bool myFix=false; int mySats=0; float mySpeed=0,myHeading=0; \
  unsigned long myLastFix=0,worldRxMs=0,slotDue=0,lastCycle=0; \
  bool pendingUplink=false,myAlert=false,searching=false,wantRoster=false; \
  double lastAddLat=0,lastAddLon=0; bool haveAdd=false;

// ---------------- helpers ----------------
inline double haversine(double la1,double lo1,double la2,double lo2){
  double dLa=radians(la2-la1),dLo=radians(lo2-lo1);
  double a=sin(dLa/2)*sin(dLa/2)+cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH*2*atan2(sqrt(a),sqrt(1-a));
}
inline void putLE32(uint8_t*p,int32_t v){ p[0]=v&0xFF;p[1]=(v>>8)&0xFF;p[2]=(v>>16)&0xFF;p[3]=(v>>24)&0xFF; }
inline int32_t getLE32(const uint8_t*p){ return (int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24)); }
inline uint32_t roomOf(const uint8_t*p){ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16); }
inline void routeAdd(double la,double lo){ route[routeHead].lat=la; route[routeHead].lon=lo; routeHead=(routeHead+1)%ROUTE_MAX; if(routeN<ROUTE_MAX)routeN++; }
inline int nearestRouteIdx(double la,double lo){ int best=-1; double bd=1e18;
  for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; double d=haversine(la,lo,route[idx].lat,route[idx].lon); if(d<bd){bd=d;best=k;} } return best; }
inline void leaderRecordOwnPath(){
  if(myFix && (!haveAdd || haversine(lastAddLat,lastAddLon,myLat,myLon)>=STEP_M)){ routeAdd(myLat,myLon); routeSeq++; lastAddLat=myLat; lastAddLon=myLon; haveAdd=true; } }

// ---------------- protocolo: montar ----------------
inline int packWorld(uint8_t*p){ int o=0;
  p[o++]=g_room&0xFF; p[o++]=(g_room>>8)&0xFF; p[o++]=(g_room>>16)&0xFF; p[o++]=(uint8_t)(millis()/CYCLE_MS);
  for(int k=0;k<MAXN;k++) p[o++]=rcolor[k];
  for(int k=0;k<MAXN;k++){ uint8_t fl=0; if(world[k].active)fl|=1; if(world[k].fix)fl|=2; if(world[k].alert)fl|=4;
    p[o++]=fl; putLE32(p+o,(int32_t)(world[k].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(world[k].lon*1e7)); o+=4; }
  int nh=routeN<HIST_N?routeN:HIST_N; p[o++]=(uint8_t)nh; p[o++]=routeSeq&0xFF; p[o++]=(routeSeq>>8)&0xFF;
  int idx=(routeHead-nh+ROUTE_MAX)%ROUTE_MAX;
  for(int i=0;i<nh;i++){ int j=(idx+i)%ROUTE_MAX; putLE32(p+o,(int32_t)(route[j].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(route[j].lon*1e7)); o+=4; }
  return o; }
inline int packRoster(uint8_t*p){ int o=0; p[o++]=g_room&0xFF; p[o++]=(g_room>>8)&0xFF; p[o++]=(g_room>>16)&0xFF;
  for(int k=0;k<MAXN;k++){ putLE32(p+o,(int32_t)ruid[k]); o+=4; p[o++]=rcolor[k]; int L=strlen(rname[k]); if(L>15)L=15; p[o++]=(uint8_t)L; memcpy(p+o,rname[k],L); o+=L; }
  return o; }
inline int packUplink(uint8_t*p){ int o=0; p[o++]=g_room&0xFF; p[o++]=(g_room>>8)&0xFF; p[o++]=(g_room>>16)&0xFF; p[o++]=curSlot;
  uint8_t fl=0; if(myFix)fl|=2; if(myAlert)fl|=4; p[o++]=fl; putLE32(p+o,(int32_t)(myLat*1e7)); o+=4; putLE32(p+o,(int32_t)(myLon*1e7)); o+=4; return o; }
inline int packJoin(uint8_t*p){ int o=0; p[o++]=g_room&0xFF; p[o++]=(g_room>>8)&0xFF; p[o++]=(g_room>>16)&0xFF;
  putLE32(p+o,(int32_t)myUid); o+=4; p[o++]=g_color; int L=strlen(g_name); if(L>15)L=15; p[o++]=(uint8_t)L; memcpy(p+o,g_name,L); o+=L; return o; }

// ---------------- protocolo: receber (atualiza o estado) ----------------
inline void parseRx(uint8_t cmd,uint8_t*p,uint8_t plen){
  if(isLeader()){
    if(cmd==CMD_UPLINK && plen>=13 && roomOf(p)==g_room){ uint8_t sl=p[3]; if(sl>=1&&sl<MAXN){
      uint8_t fl=p[4]; world[sl].active=true; world[sl].fix=fl&2; world[sl].alert=fl&4;
      world[sl].lat=getLE32(p+5)/1e7; world[sl].lon=getLE32(p+9)/1e7; world[sl].lastMs=millis(); } }
    else if(cmd==CMD_JOIN && plen>=9 && roomOf(p)==g_room){
      uint32_t uid=(uint32_t)getLE32(p+3); uint8_t col=p[7]; int L=p[8]; if(L>15)L=15;
      char nm[16]; if(9+L<=plen){ memcpy(nm,p+9,L); nm[L]=0; } else nm[0]=0;
      int slot=-1; for(int k=1;k<MAXN;k++) if(ruid[k]==uid){ slot=k; break; }
      if(slot<0) for(int k=1;k<MAXN;k++) if(ruid[k]==0){ slot=k; break; }
      if(slot>=1){ ruid[slot]=uid; if(col<8)rcolor[slot]=col; if(nm[0]){ strncpy(rname[slot],nm,15); rname[slot][15]=0; } haveRoster=true; wantRoster=true; } }
    return;
  }
  if(cmd==CMD_ROSTER && plen>=3 && roomOf(p)==g_room){
    int o=3; for(int k=0;k<MAXN&&o+6<=plen;k++){ ruid[k]=(uint32_t)getLE32(p+o); o+=4; rcolor[k]=p[o++]; int L=p[o++]; if(L>15)L=15; if(o+L>plen)break; memcpy(rname[k],p+o,L); rname[k][L]=0; o+=L; }
    haveRoster=true; if(!joined){ for(int k=1;k<MAXN;k++) if(ruid[k]==myUid){ curSlot=k; joined=true; break; } }
    return;
  }
  if(cmd==CMD_WORLD && plen>=4+8+72+3 && roomOf(p)==g_room){
    worldRxMs=millis(); searching=false; int o=4;
    for(int k=0;k<MAXN;k++) world[k].color=p[o++];
    for(int k=0;k<MAXN;k++){ uint8_t fl=p[o]; world[k].active=fl&1; world[k].fix=fl&2; world[k].alert=fl&4;
      world[k].lat=getLE32(p+o+1)/1e7; world[k].lon=getLE32(p+o+5)/1e7; if(world[k].active) world[k].lastMs=millis(); o+=9; }
    int nh=p[o++]; uint16_t seqN=(uint16_t)p[o]|((uint16_t)p[o+1]<<8); o+=2;
    for(int i=0;i<nh;i++){ double la=getLE32(p+o)/1e7, lo=getLE32(p+o+4)/1e7; o+=8;
      uint16_t seq=seqN-(nh-1)+i; if(!haveRouteSeq||(int16_t)(seq-lastRouteSeq)>0){ routeAdd(la,lo); lastRouteSeq=seq; haveRouteSeq=true; } }
    slotDue=worldRxMs+(unsigned long)curSlot*SLOT_MS; pendingUplink=true;
  }
}

// projeta lat/lon -> tela (heading-up), com centro e rumo do observador
inline void worldToScreen(double lat,double lon,double clat,double clon,double head,float mpp,int CX,int CY,int&sx,int&sy){
  double east=R_EARTH*cos(radians(clat))*radians(lon-clon), north=R_EARTH*radians(lat-clat);
  double h=radians(head), up=north*cos(h)+east*sin(h), ri=east*cos(h)-north*sin(h);
  sx=CX+(int)(ri/mpp); sy=CY-(int)(up/mpp);
}
