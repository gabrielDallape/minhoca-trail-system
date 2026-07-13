/*
 * MODO GRUPO - CYD/Waveshare (ESP32 + LovyanGFX). "Casca" de hardware.
 * Toda a logica/protocolo/temas vem do nucleo compartilhado ../trilha_core.h.
 * Aqui: tela (LovyanGFX), toque, LoRa (Serial1 35/22), GPS (Serial2 27) e
 * persistencia (Preferences/NVS). Papel escolhido por saida (Criar/Entrar).
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include "../trilha_core.h"
TRILHA_CORE_DEFINE

static LGFX  tft;
LoRaMESH     lora(&Serial1);
TinyGPSPlus  gps;
Preferences  prefs;

int SCR_W,SCR_H,CXp,CYp;
float mapMPP=2.0f;
bool touchWasDown=false;
uint8_t uiPage=0, pendingRole=0; bool editName=false;
char codeBuf[6]="", nameBuf[16]=""; int codeLen=0;
unsigned long lastDraw=0, lastAlertTx=0;

// ---------- persistencia (NVS) ----------
void saveCfg(){ prefs.begin("grupo",false); prefs.putUInt("room",g_room); prefs.putUChar("role",g_role); prefs.putUChar("slot",g_slot);
  prefs.putUChar("color",g_color); prefs.putUChar("theme",(uint8_t)g_theme); prefs.putString("name",g_name); prefs.end(); }
void loadCfg(){ prefs.begin("grupo",true); g_room=prefs.getUInt("room",0); g_role=prefs.getUChar("role",0); g_slot=prefs.getUChar("slot",1);
  g_color=prefs.getUChar("color",0); g_theme=prefs.getUChar("theme",0); String n=prefs.getString("name","Carro"); prefs.end();
  n.toCharArray(g_name,16); if(g_name[0]==0) strcpy(g_name,"Carro"); if(g_room>99999)g_room=0; }

// ---------- LoRa ----------
void sendWorld(){ uint8_t p[240]; int n=packWorld(p); lora.PrepareFrameCommand(BCAST,CMD_WORLD,p,n); lora.SendPacket(); }
void sendRoster(){ uint8_t p[240]; int n=packRoster(p); lora.PrepareFrameCommand(BCAST,CMD_ROSTER,p,n); lora.SendPacket(); }
void sendUplink(){ uint8_t p[16]; int n=packUplink(p); lora.PrepareFrameCommand(LEADER_ID,CMD_UPLINK,p,n); lora.SendPacket(); }
void sendJoin(){ uint8_t p[24]; int n=packJoin(p); lora.PrepareFrameCommand(LEADER_ID,CMD_JOIN,p,n); lora.SendPacket(); }
void readLoRa(){ int g=0; uint16_t id; uint8_t cmd=0,p[240],plen=0;
  while(g++<8 && lora.ReceivePacketCommand(&id,&cmd,p,&plen,15)) parseRx(cmd,p,plen);
  if(wantRoster){ wantRoster=false; sendRoster(); } }
void readGPS(){ while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=1.5f) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000); }

// ---------- texto (LovyanGFX) ----------
void txt(const lgfx::IFont& f,int x,int y,uint16_t col,const char*s,textdatum_t d=textdatum_t::top_left){ tft.setFont(&f); tft.setTextColor(col); tft.setTextDatum(d); tft.drawString(s,x,y); }

// ---------- desenho: primitivas ----------
void card(int x,int y,int w,int h){ tft.fillRoundRect(x,y,w,h,6,C_CARD); tft.drawRoundRect(x,y,w,h,6,C_LINE); }
void triMk(int cx,int cy,uint16_t col,int s){ tft.fillTriangle(cx,cy-s,cx-(int)(0.62f*s),cy+(int)(0.72f*s),cx+(int)(0.62f*s),cy+(int)(0.72f*s),col); tft.drawTriangle(cx,cy-s,cx-(int)(0.62f*s),cy+(int)(0.72f*s),cx+(int)(0.62f*s),cy+(int)(0.72f*s),C_WHITE); }
void dotMk(int cx,int cy,uint16_t col,int r){ tft.fillCircle(cx,cy,r,col); tft.drawCircle(cx,cy,r,C_WHITE); }
void roadSeg(int x0,int y0,int x1,int y1,uint16_t cas,uint16_t fil,int wc,int wf){
  for(int d=-(wc/2);d<=wc/2;d++){ tft.drawLine(x0+d,y0,x1+d,y1,cas); tft.drawLine(x0,y0+d,x1,y1+d,cas); }
  for(int d=-(wf/2);d<=wf/2;d++){ tft.drawLine(x0+d,y0,x1+d,y1,fil); tft.drawLine(x0,y0+d,x1,y1+d,fil); } }
void chromeBg(){ if(C_FL&FL_GRID){ for(int x=30;x<SCR_W;x+=30) tft.drawFastVLine(x,20,SCR_H-20,C_LINE); for(int y=30;y<SCR_H;y+=30) tft.drawFastHLine(0,y,SCR_W,C_LINE); }
  if(C_FL&FL_CROSS){ tft.drawFastHLine(CXp-12,CYp,24,C_ROUTE); tft.drawFastVLine(CXp,CYp-12,24,C_ROUTE); } }
void chromeFg(){ if(!(C_FL&FL_CORNERS))return; int L=16,m=4; uint16_t c=C_ROUTE;
  tft.drawFastHLine(m,m,L,c);tft.drawFastVLine(m,m,L,c); tft.drawFastHLine(SCR_W-m-L,m,L,c);tft.drawFastVLine(SCR_W-m-1,m,L,c);
  tft.drawFastHLine(m,SCR_H-m-1,L,c);tft.drawFastVLine(m,SCR_H-m-L,L,c); tft.drawFastHLine(SCR_W-m-L,SCR_H-m-1,L,c);tft.drawFastVLine(SCR_W-m-1,SCR_H-m-L,L,c); }

// ---------- telas ----------
void gearRect(int&x,int&y,int&w,int&h){ w=150;h=28;x=SCR_W-w;y=2; }
void gearIcon(int cx,int cy,int r,uint16_t col){ for(int a=0;a<360;a+=45){float rad=a*3.14159f/180.0f; tft.fillCircle(cx+(int)(cos(rad)*r),cy+(int)(sin(rad)*r),2,col);} tft.fillCircle(cx,cy,r,col); tft.fillCircle(cx,cy,r/2,C_BG); }
void homeRects(int&bx,int&bw,int&bh,int&by1,int&by2){ bw=SCR_W-24;bx=12;bh=48;by1=90;by2=by1+bh+12; }
void drawHome(){
  tft.fillScreen(C_BG);
  txt(fonts::FreeSansBold18pt7b,12,18,C_WHITE,"TRILHA");
  int gcx=SCR_W-20,gcy=16; gearIcon(gcx,gcy,9,C_MUT);
  txt(fonts::FreeSansBold9pt7b,gcx-16,gcy,C_WHITE,g_name,textdatum_t::middle_right);
  int bx,bw,bh,by1,by2; homeRects(bx,bw,bh,by1,by2);
  tft.fillRoundRect(bx,by1,bw,bh,10,C_ROUTE); txt(fonts::FreeSansBold12pt7b,bx+bw/2,by1+bh/2,C_BG,"CRIAR SALA",textdatum_t::middle_center);
  tft.drawRoundRect(bx,by2,bw,bh,10,C_BLUE); tft.drawRoundRect(bx+1,by2+1,bw-2,bh-2,10,C_BLUE);
  txt(fonts::FreeSansBold12pt7b,bx+bw/2,by2+bh/2,C_BLUE,"ENTRAR NA SALA",textdatum_t::middle_center);
}
void kpRect(int i,int&x,int&y,int&w,int&h){ int kw=(int)(SCR_W*0.62)/3,kx=(SCR_W-kw*3)/2,ky=(int)(SCR_H*0.30),kh=(SCR_H-ky-6)/4,r=i/3,c=i%3; x=kx+c*kw+3;y=ky+r*kh+3;w=kw-6;h=kh-6; }
static const char* KP[12]={"1","2","3","4","5","6","7","8","9","<","0","OK"};
void drawKeypad(){ tft.fillScreen(C_BG);
  txt(fonts::FreeSans9pt7b,SCR_W/2,8,C_MUT, pendingRole==1?"Criar - codigo":"Entrar - codigo",textdatum_t::top_center);
  int bw=SCR_W/8,bx=(SCR_W-(bw*5+4*5))/2,by=(int)(SCR_H*0.14);
  for(int i=0;i<5;i++){int x=bx+i*(bw+5); tft.drawRoundRect(x,by,bw,bw,5,i<codeLen?C_ROUTE:C_LINE);
    if(i<codeLen){char c[2]={codeBuf[i],0}; txt(fonts::FreeSansBold12pt7b,x+bw/2,by+bw/2,C_WHITE,c,textdatum_t::middle_center);} }
  for(int i=0;i<12;i++){int x,y,w,h;kpRect(i,x,y,w,h); tft.fillRoundRect(x,y,w,h,5,C_CARD); tft.drawRoundRect(x,y,w,h,5,C_LINE);
    uint16_t c=(i==9)?C_RED:(i==11?C_ROUTE:C_WHITE); txt(fonts::FreeSansBold12pt7b,x+w/2,y+h/2,c,KP[i],textdatum_t::middle_center);} }
void keypadTouch(int tx,int ty){ for(int i=0;i<12;i++){int x,y,w,h;kpRect(i,x,y,w,h);
  if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ if(i==9){ if(codeLen>0)codeBuf[--codeLen]=0; else uiPage=0; }
    else if(i==11){ if(codeLen==5){ g_room=atol(codeBuf); if(g_room==0)g_room=1; g_role=pendingRole;
      if(isLeader()){ g_slot=0; ruid[0]=myUid; strncpy(rname[0],g_name,15); rname[0][15]=0; rcolor[0]=g_color; haveRoster=true; joined=true; curSlot=0; searching=false; }
      else { joined=false; curSlot=0; searching=true; worldRxMs=0; } saveCfg(); } }
    else if(codeLen<5){ codeBuf[codeLen++]=KP[i][0]; codeBuf[codeLen]=0; } return; } } }

void themeBtnRect(int i,int&x,int&y,int&w,int&h){ int bw=(SCR_W-16)/3; x=8+i*bw+2;y=64;w=bw-4;h=24; }
void abcRect(int i,int&x,int&y,int&w,int&h){ int cols=7,kx=6,ky=96,kw=(SCR_W-12)/cols,kh=(SCR_H-ky-4)/4,r=i/cols,c=i%cols; x=kx+c*kw+2;y=ky+r*kh+2;w=kw-4;h=kh-4; }
void drawSettings(){ tft.fillScreen(C_BG);
  txt(fonts::FreeSans9pt7b,8,4,C_MUT,"Configuracoes");
  tft.drawRoundRect(8,22,SCR_W-16,34,6,C_ROUTE); txt(fonts::FreeSansBold18pt7b,16,39,C_WHITE, nameBuf[0]?nameBuf:"...",textdatum_t::middle_left);
  for(int i=0;i<3;i++){int x,y,w,h;themeBtnRect(i,x,y,w,h); bool sel=(i==g_theme);
    tft.fillRoundRect(x,y,w,h,5, sel?THEMES[i].route:C_CARD); tft.drawRoundRect(x,y,w,h,5, sel?THEMES[i].route:C_LINE);
    txt(fonts::FreeSansBold9pt7b,x+w/2,y+h/2, sel?THEMES[i].bg:C_WHITE, THEMES[i].name, textdatum_t::middle_center);}
  for(int i=0;i<28;i++){int x,y,w,h;abcRect(i,x,y,w,h); tft.fillRoundRect(x,y,w,h,3,C_CARD); tft.drawRoundRect(x,y,w,h,3,C_LINE);
    char lb[3]; uint16_t c=C_WHITE; if(i<26){lb[0]='A'+i;lb[1]=0;} else if(i==26){strcpy(lb,"<");c=C_RED;} else {strcpy(lb,"OK");c=C_ROUTE;}
    txt(fonts::FreeSansBold9pt7b,x+w/2,y+h/2,c,lb,textdatum_t::middle_center);} }
void settingsTouch(int tx,int ty){
  for(int i=0;i<3;i++){int x,y,w,h;themeBtnRect(i,x,y,w,h); if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ applyTheme(i); saveCfg(); return; } }
  for(int i=0;i<28;i++){int x,y,w,h;abcRect(i,x,y,w,h); if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ int L=strlen(nameBuf);
    if(i<26){ if(L<12){nameBuf[L]='A'+i;nameBuf[L+1]=0;} } else if(i==26){ if(L>0)nameBuf[L-1]=0; }
    else { if(L>0){ strncpy(g_name,nameBuf,15); g_name[15]=0; if(isLeader()){strncpy(rname[0],g_name,15);rname[0][15]=0;} saveCfg(); } editName=false; } return; } } }

void drawSearching(){ tft.fillScreen(C_BG); char rm[10]; snprintf(rm,sizeof(rm),"%lu",(unsigned long)g_room);
  txt(fonts::FreeSans9pt7b,SCR_W/2,(int)(SCR_H*0.22),C_MUT,"Procurando sala",textdatum_t::top_center);
  txt(fonts::FreeSansBold18pt7b,SCR_W/2,(int)(SCR_H*0.42),C_WHITE,rm,textdatum_t::middle_center);
  int nd=(millis()/450)%4; char aw[24]="aguardando o lider"; int L=strlen(aw); for(int i=0;i<nd;i++)aw[L+i]='.'; aw[L+nd]=0;
  txt(fonts::FreeSans9pt7b,SCR_W/2,(int)(SCR_H*0.6),C_MUT,aw,textdatum_t::top_center);
  int w=110,h=34,x=(SCR_W-w)/2,y=SCR_H-h-8; tft.drawRoundRect(x,y,w,h,8,C_LINE); txt(fonts::FreeSansBold12pt7b,SCR_W/2,y+h/2,C_MUT,"VOLTAR",textdatum_t::middle_center); }

void drawMap(){
  tft.fillScreen(C_BG); chromeBg();
  double clat=myLat,clon=myLon,chead=myHeading;
  if(!myFix){ txt(fonts::FreeSansBold12pt7b,SCR_W/2,CYp,C_AMBER,"PROCURANDO GPS",textdatum_t::middle_center); triMk(CXp,CYp,C_BLUE,11); chromeFg(); return; }
  int myNear=nearestRouteIdx(clat,clon);
  double myBest=1e9; if(myNear>=0){ int mi=(routeHead-routeN+myNear+ROUTE_MAX)%ROUTE_MAX; myBest=haversine(clat,clon,route[mi].lat,route[mi].lon); }
  int psx=-1,psy=-1; double plat=0,plon=0; bool hp=false;
  for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; int sx,sy; worldToScreen(route[idx].lat,route[idx].lon,clat,clon,chead,mapMPP,CXp,CYp,sx,sy);
    bool vis=(sx>=-16&&sx<SCR_W+16&&sy>=-16&&sy<SCR_H+16);
    if(vis&&psx>=0){ bool gap=hp&&haversine(plat,plon,route[idx].lat,route[idx].lon)>40.0;
      if(!gap){ if(k>myNear) roadSeg(psx,psy,sx,sy,C_ROUTEC,C_ROUTE,7,3); else roadSeg(psx,psy,sx,sy,C_TRAVC,C_TRAV,5,2); } }
    psx=vis?sx:-1; psy=sy; plat=route[idx].lat; plon=route[idx].lon; hp=true; }
  int aSlot=-1; for(int k=0;k<MAXN;k++) if(world[k].active&&world[k].alert){aSlot=k;break;}
  if(aSlot>=0){ int aN=nearestRouteIdx(world[aSlot].lat,world[aSlot].lon); if(aN>=0&&myNear>=0){ int a=min(aN,myNear),b=max(aN,myNear),qx=-1,qy=-1;
    for(int k=a;k<=b;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; int sx,sy; worldToScreen(route[idx].lat,route[idx].lon,clat,clon,chead,mapMPP,CXp,CYp,sx,sy); if(qx>=0) roadSeg(qx,qy,sx,sy,0x6800,C_RED,9,4); qx=sx;qy=sy; } } }
  int meSlot=isLeader()?0:(joined?curSlot:255);
  for(int k=0;k<MAXN;k++){ if(!world[k].active||k==meSlot) continue; if(millis()-world[k].lastMs>NODE_TTL) continue;
    int sx,sy; worldToScreen(world[k].lat,world[k].lon,clat,clon,chead,mapMPP,CXp,CYp,sx,sy); if(sx<-20||sx>SCR_W+20||sy<-20||sy>SCR_H+20) continue;
    bool al=world[k].alert; uint16_t mc=al?C_RED:(k==0?C_AMBER:colorOf(world[k].color));
    if(k==0) triMk(sx,sy,mc,10); else dotMk(sx,sy,mc,6);
    txt(fonts::FreeSans9pt7b,sx+8,sy-6,mc,haveRoster?rname[k]:(k==0?"Lider":"Carro")); }
  triMk(CXp,CYp,C_BLUE,11);
  if(!isLeader() && routeN>3 && myBest>OFFROUTE_M && ((millis()/350)%2==0)){ for(int t=0;t<4;t++) tft.drawRect(t,t,SCR_W-1-2*t,SCR_H-1-2*t,C_YEL); txt(fonts::FreeSansBold12pt7b,SCR_W/2,14,C_YEL,"FORA DE ROTA",textdatum_t::top_center); }
  chromeFg();
  bool off=!isLeader()&&(worldRxMs==0||millis()-worldRxMs>LEAD_TTL);
  if(off){ card(SCR_W/2-70,4,140,20); txt(fonts::FreeSansBold9pt7b,SCR_W/2,14,C_RED,"LIDER OFFLINE",textdatum_t::middle_center); }
}
int abX,abY,abR;
void drawUI(){ char b[24];
  card(4,4,120,34); tft.fillCircle(16,21,4,isLeader()?C_AMBER:C_BLUE);
  snprintf(b,sizeof(b),"Sala %lu",(unsigned long)g_room); txt(fonts::FreeSansBold9pt7b,26,12,C_WHITE,b);
  txt(fonts::FreeSans9pt7b,26,26,C_MUT,isLeader()?"LIDER":(joined?"seguidor":"entrando..."));
  // roster (direita)
  int rw=104,rx=SCR_W-rw-4,ry=4,rh=17;
  for(int k=0;k<MAXN;k++){ if(!world[k].active||millis()-world[k].lastMs>NODE_TTL) continue; card(rx,ry,rw,rh);
    bool me=(!isLeader()&&joined&&k==(int)curSlot)||(isLeader()&&k==0),ld=(k==0); uint16_t col=me?C_BLUE:(ld?C_AMBER:colorOf(world[k].color));
    if(me||ld) tft.fillTriangle(rx+10,ry+3,rx+5,ry+14,rx+15,ry+14,col); else tft.fillCircle(rx+10,ry+9,5,col);
    txt(fonts::FreeSans9pt7b,rx+20,ry+3,world[k].alert?C_RED:C_WHITE,haveRoster?rname[k]:(ld?"Lider":"Carro"));
    if(!me&&myFix&&world[k].fix){ double d=haversine(myLat,myLon,world[k].lat,world[k].lon); char ds[8]; if(d>=1000)snprintf(ds,8,"%.1fk",d/1000.0);else snprintf(ds,8,"%dm",(int)d); txt(fonts::FreeSans9pt7b,rx+rw-4,ry+3,C_MUT,ds,textdatum_t::top_right); }
    ry+=rh+3; if(ry>SCR_H-66) break; }
  // card inferior esq (velocidade/distancia)
  card(4,SCR_H-46,118,42);
  if(isLeader()){ int c=0; for(int k=1;k<MAXN;k++) if(world[k].active&&millis()-world[k].lastMs<NODE_TTL)c++;
    txt(fonts::FreeSans9pt7b,12,SCR_H-42,C_MUT,"VEL km/h"); snprintf(b,sizeof(b),"%d",(int)(mySpeed+0.5)); txt(fonts::FreeSansBold18pt7b,12,SCR_H-28,C_WHITE,b);
    snprintf(b,sizeof(b),"%d seg",c); txt(fonts::FreeSans9pt7b,64,SCR_H-24,C_MUT,b); }
  else if(world[0].active&&myFix){ double d=haversine(myLat,myLon,world[0].lat,world[0].lon);
    txt(fonts::FreeSans9pt7b,12,SCR_H-42,C_MUT,"DIST LIDER"); if(d>=1000)snprintf(b,sizeof(b),"%.1fk",d/1000.0);else snprintf(b,sizeof(b),"%dm",(int)d); txt(fonts::FreeSansBold18pt7b,12,SCR_H-28,C_WHITE,b); }
  // FAB alerta
  abR=20;abX=SCR_W-abR-6;abY=SCR_H-abR-6; bool on=myAlert;
  tft.fillCircle(abX,abY,abR,on?C_RED:C_CARD); tft.drawCircle(abX,abY,abR,C_RED);
  uint16_t tc=on?C_WHITE:C_RED; tft.fillTriangle(abX,abY-11,abX-11,abY+9,abX+11,abY+9,tc); tft.fillRect(abX-1,abY-4,3,7,on?C_RED:C_CARD); tft.fillRect(abX-1,abY+5,3,3,on?C_RED:C_CARD);
}

void handleTouch(){ int32_t tx,ty;
  if(tft.getTouch(&tx,&ty)){ if(!touchWasDown){
    if(g_room==0){ if(editName) settingsTouch(tx,ty); else if(uiPage==0){ int gx,gy,gw,gh; gearRect(gx,gy,gw,gh);
        if(tx>=gx&&tx<=gx+gw&&ty>=gy&&ty<=gy+gh){ editName=true; strncpy(nameBuf,g_name,15); nameBuf[15]=0; if(!strcmp(nameBuf,"Carro"))nameBuf[0]=0; }
        else { int bx,bw,bh,by1,by2; homeRects(bx,bw,bh,by1,by2); if(tx>=bx&&tx<=bx+bw){ if(ty>=by1&&ty<=by1+bh){pendingRole=1;uiPage=1;codeLen=0;codeBuf[0]=0;} else if(ty>=by2&&ty<=by2+bh){pendingRole=0;uiPage=1;codeLen=0;codeBuf[0]=0;} } } }
      else keypadTouch(tx,ty); }
    else if(searching){ int w=110,h=34,x=(SCR_W-w)/2,y=SCR_H-h-8; if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ g_room=0;searching=false;uiPage=0;joined=false;saveCfg(); } }
    else { if((tx-abX)*(tx-abX)+(ty-abY)*(ty-abY)<=(abR+6)*(abR+6)) myAlert=!myAlert; }
  } touchWasDown=true; } else touchWasDown=false; }

void setup(){
  Serial.begin(115200); loadCfg(); applyTheme(g_theme);
  tft.init(); tft.setRotation(1);
  uint16_t cal[8]={549,3553,619,389,3613,3475,3618,378}; tft.setTouchCalibrate(cal);
  SCR_W=tft.width(); SCR_H=tft.height(); CXp=SCR_W/2; CYp=SCR_H/2;
  Serial1.begin(9600,SERIAL_8N1,35,22); Serial2.begin(9600,SERIAL_8N1,27,-1); delay(150);
  lora.localread(); myUid=lora.localUniqueId;
  for(int k=0;k<MAXN;k++){ world[k]=Node(); world[k].color=rcolor[k]?rcolor[k]:k; if(rname[k][0]==0) snprintf(rname[k],16,k==0?"Lider":"Carro %d",k); }
  if(isLeader()&&g_room){ ruid[0]=myUid; strncpy(rname[0],g_name,15); rname[0][15]=0; rcolor[0]=g_color; haveRoster=true; joined=true; curSlot=0; }
  tft.fillScreen(C_BG);
  Serial.print("== GRUPO CYD == sala="); Serial.print(g_room); Serial.print(" uid="); Serial.println(myUid);
}
void loop(){
  readGPS(); readLoRa(); handleTouch();
  unsigned long now=millis();
  int ms=isLeader()?0:curSlot;
  if(isLeader()||joined){ world[ms].active=true; world[ms].fix=myFix; world[ms].alert=myAlert; world[ms].lat=myLat; world[ms].lon=myLon; world[ms].lastMs=now; world[ms].color=g_color; }
  if(g_room!=0){
    if(isLeader()){ leaderRecordOwnPath(); static uint8_t cyc=0; if(now-lastCycle>=CYCLE_MS){ if(myFix){ sendWorld(); if((cyc++%3)==0) sendRoster(); } lastCycle=now; } }
    else if(!joined){ static unsigned long lj=0; if(now-lj>1500){ sendJoin(); lj=now; } }
    else if(pendingUplink && now>=slotDue){ sendUplink(); pendingUplink=false; }
  }
  if(now-lastDraw>250){ if(g_room==0){ if(editName) drawSettings(); else if(uiPage==0) drawHome(); else drawKeypad(); } else if(searching) drawSearching(); else { drawMap(); drawUI(); } lastDraw=now; }
}
