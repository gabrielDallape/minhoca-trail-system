/*
 * MODO GRUPO - Waveshare ESP32-S3-Touch-LCD-7B (1024x600 RGB). "Casca" de hardware.
 * Logica/protocolo/temas/estado: nucleo compartilhado ../trilha_core.h (mesmo do CYD).
 * Hardware: display LGFX_WS7B (RGB) + expansor CH32V003 (I2C 0x24) + touch GT911 (0x5D),
 * tudo via Wire; LoRa Serial1 44/43; GPS Serial2 6. Anti-flicker: sprite offscreen na PSRAM.
 * UI redesenhada pro tamanho 1024x600 (fontes/botoes grandes, cartoes nos cantos).
 * Gravar: porta UART/CH343 (switch UART1), CDCOnBoot=default.
 */
#include <Wire.h>
#include "LGFX_WS7B.h"
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include "../trilha_core.h"
TRILHA_CORE_DEFINE

#define BANCADA 0   // 0 = GPS REAL (teste de rua). 1 = bancada (posicao fake).

static LGFX        lcd;          // hardware (painel RGB)
static LGFX_Sprite tft(&lcd);    // buffer offscreen na PSRAM (anti-flicker): tudo desenha aqui
LoRaMESH     lora(&Serial1);
TinyGPSPlus  gps;
Preferences  prefs;

int SCR_W,SCR_H,CXp,CYp;
float mapMPP=2.0f;
bool touchWasDown=false;
uint8_t uiPage=0, pendingRole=0; bool editName=false;
char codeBuf[6]="", nameBuf[16]=""; int codeLen=0;
unsigned long lastDraw=0,lastDbg=0,txUp=0,loopMax=0;
int abX,abY,abR,exX,exY,exW,exH;
int zpX,zpY,zmX,zmY,zSz;   // botoes de zoom + / -
// ---- modo PONTO-A-PONTO (1:1), sem broadcast/flooding ----
#define P2P 1
#define P2P_CMD 0x11    // seguidor->lider: so posicao
#define P2P_TRAIL 0x12  // lider->seguidor: posicao + TRAJETO (breadcrumb do caminho do lider)
#define MY_LORA_ID 1              // ID deste modulo (escravo 13683). REGRA LoRaMESH: escravo->mestre usa o PROPRIO ID.
#define C_ORANGE 0xFD20           // laranja (borda de perda de sinal)
bool pendingReply=false; unsigned long replyDue=0, lastP2PTx=0;
uint16_t peerAck=0; bool peerAckKnown=false;   // (LIDER) ate onde o seguidor ja tem o trajeto -> catch-up

// ---------- expansor CH32V003 (0x24) + toque GT911 (0x5D), via Wire ----------
bool ioExt(uint8_t reg,uint8_t val){ Wire.beginTransmission(0x24); Wire.write(reg); Wire.write(val); return Wire.endTransmission()==0; }
// liga painel/backlight ANTES do lcd.init(), mantendo o touch EM reset (IO1=0).
void powerUpPanel(){
  Wire.begin(8,9); Wire.setClock(400000);
  ioExt(0x02,0xFF);              // expansor: todos saida
  ioExt(0x03,0x5C); delay(50);   // painel on (IO2/3/6), USB (IO5=0), touch em reset (IO1=0)
}
// confirma que o GT911 bootou: Product ID (0x8140) deve ser "911"
bool gt911Probe(){
  Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x40);
  if(Wire.endTransmission(false)!=0) return false;
  Wire.requestFrom(0x5D,3); char id[3]={0};
  for(int i=0;i<3 && Wire.available();i++) id[i]=Wire.read();
  return id[0]=='9'&&id[1]=='1'&&id[2]=='1';
}
// reset do GT911 DEPOIS do painel ja estar varrendo. Sequencia CANONICA do datasheet:
// INT em nivel baixo ESTAVEL antes de soltar o RST (define endereco 0x5D + modo scan).
// Instavel com 1 tentativa -> repete ate o Product ID confirmar.
bool resetTouch(){
  for(int t=0;t<4;t++){
    pinMode(4,OUTPUT); digitalWrite(4,LOW);   // INT LOW primeiro
    ioExt(0x03,0x5C); delay(20);              // RST LOW (touch em reset), INT ja LOW
    ioExt(0x03,0x5E);                         // solta RST; GT911 amostra INT=LOW -> addr 0x5D + scan
    delay(60);                                // mantem INT LOW > 50ms (datasheet)
    pinMode(4,INPUT); delay(60);              // INT float, GT911 estabiliza
    // garante modo normal (0 em 0x8040) caso tenha vindo de sleep
    Wire.beginTransmission(0x5D); Wire.write(0x80); Wire.write(0x40); Wire.write((uint8_t)0); Wire.endTransmission();
    delay(20);
    if(gt911Probe()){ Serial.printf("[touch] GT911 OK na tentativa %d\n",t); return true; }
    Serial.printf("[touch] tentativa %d falhou, repetindo\n",t);
  }
  Serial.println("[touch] GT911 NAO respondeu apos 4 tentativas");
  return false;
}
// leitura ESTAVEL: o GT911 so avisa em frames; entre frames mantemos o ultimo estado (dedo down/up)
bool getTouchWS(int32_t*px,int32_t*py){
  static bool down=false; static int32_t lx=0,ly=0; static unsigned long lastSeen=0;
  uint8_t st=0;
  Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x4E);
  if(Wire.endTransmission(false)==0){ Wire.requestFrom(0x5D,1); if(Wire.available()) st=Wire.read(); }
  if(st&0x80){   // frame novo pronto (bit7=1)
    uint8_t nt=st&0x0F;
    if(nt>=1 && nt<=5){
      Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x50); Wire.endTransmission(false);
      Wire.requestFrom(0x5D,4);
      if(Wire.available()>=4){ uint8_t xl=Wire.read(),xh=Wire.read(),yl=Wire.read(),yh=Wire.read(); lx=xl|(xh<<8); ly=yl|(yh<<8); down=true; lastSeen=millis(); }
    } else down=false;   // release (nt=0) ou contagem invalida
  }
  // CLEAR INCONDICIONAL (a versao que FUNCIONOU: 384 frames, 11 cliques, sala 0->1 na tela inicial).
  // Reconhece SEMPRE - o GT911 so gera o proximo frame apos o ACK. Reverter isso pra condicional quebrou.
  Wire.beginTransmission(0x5D); Wire.write(0x81); Wire.write(0x4E); Wire.write((uint8_t)0); Wire.endTransmission();
  if(down && millis()-lastSeen>300) down=false;   // rede de seguranca contra release perdido
  *px=lx; *py=ly; return down;
}

// ---------- persistencia (NVS) ----------
void saveCfg(){ prefs.begin("grupo",false); prefs.putUInt("room",g_room); prefs.putUChar("role",g_role); prefs.putUChar("slot",g_slot);
  prefs.putUChar("color",g_color); prefs.putUChar("theme",(uint8_t)g_theme); prefs.putString("name",g_name); prefs.end(); }
void loadCfg(){ prefs.begin("grupo",true); g_room=prefs.getUInt("room",0); g_role=prefs.getUChar("role",0); g_slot=prefs.getUChar("slot",1);
  g_color=prefs.getUChar("color",0); g_theme=prefs.getUChar("theme",0); String n=prefs.getString("name","Carro"); prefs.end();
  n.toCharArray(g_name,16); if(g_name[0]==0) strcpy(g_name,"Carro"); if(g_room>99999)g_room=0; }

// ---------- LoRa / GPS ----------
void sendWorld(){ uint8_t p[240]; int n=packWorld(p); lora.PrepareFrameCommand(BCAST,CMD_WORLD,p,n); lora.SendPacket(); }
void sendRoster(){ uint8_t p[240]; int n=packRoster(p); lora.PrepareFrameCommand(BCAST,CMD_ROSTER,p,n); lora.SendPacket(); }
void sendUplink(){ uint8_t p[16]; int n=packUplink(p);
  lora.PrepareFrameCommand(LEADER_ID,CMD_UPLINK,p,n); lora.SendPacket(); txUp++; }  // endereçado ao lider (ID0)
void sendJoin(){ uint8_t p[24]; int n=packJoin(p); lora.PrepareFrameCommand(LEADER_ID,CMD_JOIN,p,n); lora.SendPacket(); }
// P2P: manda MINHA posicao+alerta DIRETO pro peer (unicast, sem broadcast)
void sendP2P(){ uint8_t p[12]; uint8_t fl=0; if(myFix)fl|=1; if(myAlert)fl|=2; if(haveRouteSeq)fl|=4; p[0]=fl;
  putLE32(p+1,(int32_t)(myLat*1e7)); putLE32(p+5,(int32_t)(myLon*1e7));
  p[9]=lastRouteSeq&0xFF; p[10]=(lastRouteSeq>>8)&0xFF;   // ACK: ate que ponto do trajeto ja recebi (catch-up)
  uint16_t myid=lora.localId?lora.localId:MY_LORA_ID;   // usa o ID REAL do modulo (13683=1, 15794=2...)
  lora.PrepareFrameCommand(myid,P2P_CMD,p,11); lora.SendPacket(); txUp++; }  // escravo manda com o PROPRIO id
// P2P LIDER: manda posicao + TRAJETO (ultimos pontos do caminho) unicast pro seguidor (id 1)
#define TRAIL_N 24
#define P2P_TXID 1   // par 13680(lider/id0)+13683(seguidor/id1): lider->destino=1, seguidor->proprio=1
void sendTrail(){ uint8_t p[13+TRAIL_N*8]; int o=0;
  uint8_t fl=0; if(myFix)fl|=1; if(myAlert)fl|=2;
  // escolhe o bloco: catch-up (a partir do que o seguidor JA tem) ou os ultimos pontos (fluxo normal)
  uint16_t oldest=(uint16_t)(routeSeq-(routeN>0?routeN-1:0));   // seq do ponto mais antigo que ainda guardo
  uint16_t base;
  bool behind = peerAckKnown && ((int16_t)(peerAck-routeSeq) < -TRAIL_N);   // seguidor > TRAIL_N atras
  if(behind){ fl|=8;                                    // modo CATCH-UP: preenche o vao da perda de sinal
    base=(uint16_t)(peerAck+1);
    if((int16_t)(base-oldest)<0) base=oldest;           // perdeu alem do meu buffer -> mando o mais antigo que tenho
  } else base=(routeN<TRAIL_N)?oldest:(uint16_t)(routeSeq-TRAIL_N+1);
  p[o++]=fl;
  putLE32(p+o,(int32_t)(myLat*1e7)); o+=4; putLE32(p+o,(int32_t)(myLon*1e7)); o+=4;   // posicao atual do lider (sempre)
  int off=(int16_t)(routeSeq-base); if(off<0)off=0; if(routeN>0&&off>routeN-1)off=routeN-1;
  int nh=off+1; if(nh>TRAIL_N)nh=TRAIL_N; if(routeN==0)nh=0;
  int idx=(routeHead-1-off+4*ROUTE_MAX)%ROUTE_MAX;      // slot do ponto 'base'
  p[o++]=(uint8_t)nh; p[o++]=base&0xFF; p[o++]=(base>>8)&0xFF;
  for(int i=0;i<nh;i++){ int j=(idx+i)%ROUTE_MAX; putLE32(p+o,(int32_t)(route[j].lat*1e7)); o+=4; putLE32(p+o,(int32_t)(route[j].lon*1e7)); o+=4; }
  lora.PrepareFrameCommand(P2P_TXID,P2P_TRAIL,p,o); lora.SendPacket(); txUp++; }
void readLoRa(){ int g=0; uint16_t id; uint8_t cmd=0,p[240],plen=0;
  while(g++<3 && lora.ReceivePacketCommand(&id,&cmd,p,&plen,4)){   // timeout curto -> nao trava o loop
#if P2P
    if(!joined) continue;   // tela inicial: radio quieto
    if(isLeader()){
      // LIDER recebe a POSICAO do seguidor (0x11) -> world[1]
      if(cmd==P2P_CMD && plen>=9){ uint8_t fl=p[0]; world[1].active=true; world[1].fix=fl&1; world[1].alert=fl&2;
        world[1].lat=getLE32(p+1)/1e7; world[1].lon=getLE32(p+5)/1e7; world[1].lastMs=millis(); worldRxMs=millis();
        if(plen>=11){ uint16_t ack=(uint16_t)p[9]|((uint16_t)p[10]<<8); peerAck=(fl&4)?ack:0; peerAckKnown=true; }  // ACK p/ catch-up
        continue; }
    } else {
      // SEGUIDOR recebe posicao + TRAJETO do lider (0x12) -> world[0] + reconstroi route[]
      if(cmd==P2P_TRAIL && plen>=12){
        uint8_t fl=p[0]; world[0].active=true; world[0].fix=fl&1; world[0].alert=fl&2;
        world[0].lat=getLE32(p+1)/1e7; world[0].lon=getLE32(p+5)/1e7; world[0].lastMs=millis(); worldRxMs=millis();
        bool isCatchup=fl&8; int nh=p[9]; uint16_t base=(uint16_t)p[10]|((uint16_t)p[11]<<8); int o=12;
        for(int i=0;i<nh && o+8<=plen;i++){ double la=getLE32(p+o)/1e7, lo=getLE32(p+o+4)/1e7; o+=8;
          uint16_t seq=(uint16_t)(base+i); int16_t d=(int16_t)(seq-lastRouteSeq);
          if(!haveRouteSeq){ routeAdd(la,lo); lastRouteSeq=seq; haveRouteSeq=true; }   // 1o ponto
          else if(d==1){ routeAdd(la,lo); lastRouteSeq=seq; }                          // contiguo: sempre
          else if(d>1 && isCatchup){ routeAdd(la,lo); lastRouteSeq=seq; } }            // salto so no catch-up (perda>buffer)
        pendingReply=true; replyDue=millis(); continue; }
      if(cmd==P2P_CMD && plen>=9){ uint8_t fl=p[0]; world[0].active=true; world[0].fix=fl&1; world[0].alert=fl&2;
        world[0].lat=getLE32(p+1)/1e7; world[0].lon=getLE32(p+5)/1e7; world[0].lastMs=millis(); worldRxMs=millis();
        pendingReply=true; replyDue=millis(); continue; }
    }
    continue;   // P2P: nao cai no parseRx (protocolo de grupo)
#endif
    parseRx(cmd,p,plen); }
  if(wantRoster){ wantRoster=false; sendRoster(); } }
void readGPS(){ while(Serial2.available()) gps.encode(Serial2.read());
  if(gps.satellites.isValid()) mySats=gps.satellites.value();
  if(gps.speed.isValid()) mySpeed=gps.speed.kmph();
  if(gps.course.isValid() && mySpeed>=1.5f) myHeading=gps.course.deg();
  if(gps.location.isValid() && gps.location.isUpdated()){ myLat=gps.location.lat(); myLon=gps.location.lng(); myLastFix=millis(); }
  myFix=(myLastFix!=0)&&(millis()-myLastFix<3000); }

// ---------- helpers de desenho ----------
void txt(const lgfx::IFont& f,int x,int y,uint16_t col,const char*s,textdatum_t d=textdatum_t::top_left){ tft.setFont(&f); tft.setTextSize(1); tft.setTextColor(col); tft.setTextDatum(d); tft.drawString(s,x,y); }
void txtBig(const lgfx::IFont& f,int sz,int x,int y,uint16_t col,const char*s,textdatum_t d){ tft.setFont(&f); tft.setTextSize(sz); tft.setTextColor(col); tft.setTextDatum(d); tft.drawString(s,x,y); tft.setTextSize(1); }
void card(int x,int y,int w,int h){ tft.fillRoundRect(x,y,w,h,10,C_CARD); tft.drawRoundRect(x,y,w,h,10,C_LINE); }
void triMk(int cx,int cy,uint16_t col,int s){ tft.fillTriangle(cx,cy-s,cx-(int)(0.62f*s),cy+(int)(0.72f*s),cx+(int)(0.62f*s),cy+(int)(0.72f*s),col); tft.drawTriangle(cx,cy-s,cx-(int)(0.62f*s),cy+(int)(0.72f*s),cx+(int)(0.62f*s),cy+(int)(0.72f*s),C_WHITE); }
void dotMk(int cx,int cy,uint16_t col,int r){ tft.fillCircle(cx,cy,r,col); tft.drawCircle(cx,cy,r,C_WHITE); }
void roadSeg(int x0,int y0,int x1,int y1,uint16_t cas,uint16_t fil,int wc,int wf){
  for(int d=-(wc/2);d<=wc/2;d++){ tft.drawLine(x0+d,y0,x1+d,y1,cas); tft.drawLine(x0,y0+d,x1,y1+d,cas); }
  for(int d=-(wf/2);d<=wf/2;d++){ tft.drawLine(x0+d,y0,x1+d,y1,fil); tft.drawLine(x0,y0+d,x1,y1+d,fil); } }
#define C_GHOST 0x7C53   // ponte tracejada onde o sinal caiu (nao deixa vao)
void drawDashed(int x0,int y0,int x1,int y1,uint16_t c){
  float dx=x1-x0,dy=y1-y0,len=sqrtf(dx*dx+dy*dy); if(len<1)return; dx/=len;dy/=len;
  for(float s=0;s<len;s+=16){ float e=fminf(s+8.0f,len);
    tft.drawLine((int)(x0+dx*s),(int)(y0+dy*s),(int)(x0+dx*e),(int)(y0+dy*e),c);
    tft.drawLine((int)(x0+dx*s),(int)(y0+dy*s)+1,(int)(x0+dx*e),(int)(y0+dy*e)+1,c); } }
// numero "heroi" (grande, cara de instrumento) + unidade menor
void bigNum(int x,int y,const char*num,const char*unit,uint16_t col){
  tft.setFont(&fonts::Font7); tft.setTextSize(1); tft.setTextColor(col); tft.setTextDatum(top_left); tft.drawString(num,x,y);
  int w=tft.textWidth(num);
  txt(fonts::FreeSansBold12pt7b,x+w+8,y+34,C_MUT,unit,textdatum_t::top_left);
}
void chromeBg(){ if(C_FL&FL_GRID){ for(int x=38;x<SCR_W;x+=38) tft.drawFastVLine(x,0,SCR_H,C_LINE); for(int y=38;y<SCR_H;y+=38) tft.drawFastHLine(0,y,SCR_W,C_LINE); } }
void chromeFg(){ if(!(C_FL&FL_CORNERS))return; int L=28,m=12; uint16_t c=C_ROUTE;
  for(int t=0;t<3;t++){ tft.drawFastHLine(m,m+t,L,c);tft.drawFastVLine(m+t,m,L,c); tft.drawFastHLine(SCR_W-m-L,m+t,L,c);tft.drawFastVLine(SCR_W-m-1-t,m,L,c);
    tft.drawFastHLine(m,SCR_H-m-1-t,L,c);tft.drawFastVLine(m+t,SCR_H-m-L,L,c); tft.drawFastHLine(SCR_W-m-L,SCR_H-m-1-t,L,c);tft.drawFastVLine(SCR_W-m-1-t,SCR_H-m-L,L,c); } }
void gearIcon(int cx,int cy,int r,uint16_t col){ for(int a=0;a<360;a+=45){float rad=a*3.14159f/180.0f; tft.fillCircle(cx+(int)(cos(rad)*r),cy+(int)(sin(rad)*r),3,col);} tft.fillCircle(cx,cy,r,col); tft.fillCircle(cx,cy,r/2,C_BG); }

// ---------- retangulos (compartilhados por draw + touch) ----------
void gearRect(int&x,int&y,int&w,int&h){ w=210;h=64;x=SCR_W-w-6;y=8; }
void homeRects(int&bx,int&bw,int&bh,int&by1,int&by2){ bw=(int)(SCR_W*0.48);bx=(SCR_W-bw)/2;bh=(int)(SCR_H*0.14);by1=(int)(SCR_H*0.50);by2=(int)(SCR_H*0.70); }
void kpRect(int i,int&x,int&y,int&w,int&h){ int kw=(int)(SCR_W*0.42)/3,kx=(SCR_W-kw*3)/2,ky=(int)(SCR_H*0.32),kh=((int)(SCR_H*0.95)-ky)/4,r=i/3,c=i%3; x=kx+c*kw+5;y=ky+r*kh+5;w=kw-10;h=kh-10; }
void themeBtnRect(int i,int&x,int&y,int&w,int&h){ int bw=(int)(SCR_W*0.8)/3; x=(int)(SCR_W*0.1)+i*bw+4;y=(int)(SCR_H*0.24);w=bw-8;h=(int)(SCR_H*0.10); }
void abcRect(int i,int&x,int&y,int&w,int&h){ int cols=7,kx=(int)(SCR_W*0.04),ky=(int)(SCR_H*0.40),kw=(int)(SCR_W*0.92)/cols,kh=((int)(SCR_H*0.96)-ky)/4,r=i/cols,c=i%cols; x=kx+c*kw+4;y=ky+r*kh+4;w=kw-8;h=kh-8; }

// ---------- telas ----------
static const char* KP[12]={"1","2","3","4","5","6","7","8","9","<","0","OK"};

void drawHome(){
  tft.fillScreen(C_BG); chromeFg();
  txtBig(fonts::FreeSansBold24pt7b,2,SCR_W/2,(int)(SCR_H*0.20),C_WHITE,"TRILHA",textdatum_t::middle_center);
  txt(fonts::FreeSans12pt7b,SCR_W/2,(int)(SCR_H*0.33),C_ROUTE,"MODO GRUPO",textdatum_t::middle_center);
  int gx,gy,gw,gh; gearRect(gx,gy,gw,gh); int gcx=SCR_W-34,gcy=gy+gh/2; gearIcon(gcx,gcy,15,C_MUT);
  txt(fonts::FreeSansBold12pt7b,gcx-32,gcy,C_WHITE,g_name,textdatum_t::middle_right);
  int bx,bw,bh,by1,by2; homeRects(bx,bw,bh,by1,by2);
  tft.fillRoundRect(bx,by1,bw,bh,16,C_ROUTE); txt(fonts::FreeSansBold18pt7b,bx+bw/2,by1+bh/2,C_BG,"CRIAR GRUPO",textdatum_t::middle_center);
  tft.drawRoundRect(bx,by2,bw,bh,16,C_BLUE); tft.drawRoundRect(bx+1,by2+1,bw-2,bh-2,16,C_BLUE);
  txt(fonts::FreeSansBold18pt7b,bx+bw/2,by2+bh/2,C_BLUE,"ENTRAR NO GRUPO",textdatum_t::middle_center);
  char b[32]; snprintf(b,sizeof(b),"LoRa %lu  -  GPS %s",(unsigned long)myUid, myFix?"OK":(mySats>0?"buscando":"--"));
  txt(fonts::FreeSans9pt7b,SCR_W/2,(int)(SCR_H*0.93),C_MUT,b,textdatum_t::middle_center);
}

void drawKeypad(){ tft.fillScreen(C_BG); chromeFg();
  txt(fonts::FreeSans12pt7b,SCR_W/2,(int)(SCR_H*0.06),C_MUT, pendingRole==1?"CRIAR GRUPO - digite um codigo de 5 digitos":"ENTRAR - digite o codigo do grupo",textdatum_t::middle_center);
  int bw=(int)(SCR_W*0.085),gap=(int)(SCR_W*0.02),tot=bw*5+gap*4,bx=(SCR_W-tot)/2,by=(int)(SCR_H*0.13),bh=(int)(bw*1.15);
  for(int i=0;i<5;i++){int x=bx+i*(bw+gap); tft.fillRoundRect(x,by,bw,bh,12,C_CARD); tft.drawRoundRect(x,by,bw,bh,12,i<codeLen?C_ROUTE:C_LINE);
    if(i<codeLen){char c[2]={codeBuf[i],0}; txtBig(fonts::FreeSansBold24pt7b,1,x+bw/2,by+bh/2,C_WHITE,c,textdatum_t::middle_center);} }
  for(int i=0;i<12;i++){int x,y,w,h;kpRect(i,x,y,w,h); uint16_t bgc=(i==11)?C_ROUTE:C_CARD; tft.fillRoundRect(x,y,w,h,14,bgc); tft.drawRoundRect(x,y,w,h,14,C_LINE);
    uint16_t c=(i==9)?C_RED:(i==11?C_BG:C_WHITE); txtBig(fonts::FreeSansBold24pt7b,1,x+w/2,y+h/2,c,KP[i],textdatum_t::middle_center);} }
void keypadTouch(int tx,int ty){ for(int i=0;i<12;i++){int x,y,w,h;kpRect(i,x,y,w,h);
  if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ if(i==9){ if(codeLen>0)codeBuf[--codeLen]=0; else uiPage=0; }
    else if(i==11){ if(codeLen==5){ g_room=atol(codeBuf); if(g_room==0)g_room=1; g_role=pendingRole;
      if(isLeader()){ g_slot=0; ruid[0]=myUid; strncpy(rname[0],g_name,15); rname[0][15]=0; rcolor[0]=g_color; haveRoster=true; joined=true; curSlot=0; searching=false; }
      else { joined=false; curSlot=0; searching=true; worldRxMs=0; } saveCfg(); } }
    else if(codeLen<5){ codeBuf[codeLen++]=KP[i][0]; codeBuf[codeLen]=0; } return; } } }

void drawSettings(){ tft.fillScreen(C_BG); chromeFg();
  txt(fonts::FreeSans12pt7b,(int)(SCR_W*0.1),(int)(SCR_H*0.04),C_MUT,"NOME DO CARRO");
  tft.drawRoundRect((int)(SCR_W*0.1),(int)(SCR_H*0.08),(int)(SCR_W*0.8),(int)(SCR_H*0.10),10,C_ROUTE);
  txtBig(fonts::FreeSansBold24pt7b,1,(int)(SCR_W*0.12),(int)(SCR_H*0.13),C_WHITE, nameBuf[0]?nameBuf:"...",textdatum_t::middle_left);
  for(int i=0;i<3;i++){int x,y,w,h;themeBtnRect(i,x,y,w,h); bool sel=(i==g_theme);
    tft.fillRoundRect(x,y,w,h,8, sel?THEMES[i].route:C_CARD); tft.drawRoundRect(x,y,w,h,8, sel?THEMES[i].route:C_LINE);
    txt(fonts::FreeSansBold12pt7b,x+w/2,y+h/2, sel?THEMES[i].bg:C_WHITE, THEMES[i].name, textdatum_t::middle_center);}
  for(int i=0;i<28;i++){int x,y,w,h;abcRect(i,x,y,w,h); tft.fillRoundRect(x,y,w,h,6,C_CARD); tft.drawRoundRect(x,y,w,h,6,C_LINE);
    char lb[3]; uint16_t c=C_WHITE; if(i<26){lb[0]='A'+i;lb[1]=0;} else if(i==26){strcpy(lb,"<");c=C_RED;} else {strcpy(lb,"OK");c=C_ROUTE;}
    txt(fonts::FreeSansBold12pt7b,x+w/2,y+h/2,c,lb,textdatum_t::middle_center);} }
void settingsTouch(int tx,int ty){
  for(int i=0;i<3;i++){int x,y,w,h;themeBtnRect(i,x,y,w,h); if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ applyTheme(i); saveCfg(); return; } }
  for(int i=0;i<28;i++){int x,y,w,h;abcRect(i,x,y,w,h); if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ int L=strlen(nameBuf);
    if(i<26){ if(L<12){nameBuf[L]='A'+i;nameBuf[L+1]=0;} } else if(i==26){ if(L>0)nameBuf[L-1]=0; }
    else { if(L>0){ strncpy(g_name,nameBuf,15); g_name[15]=0; if(isLeader()){strncpy(rname[0],g_name,15);rname[0][15]=0;} saveCfg(); } editName=false; } return; } } }

void drawSearching(){ tft.fillScreen(C_BG); chromeFg(); char rm[10]; snprintf(rm,sizeof(rm),"%lu",(unsigned long)g_room);
  txt(fonts::FreeSans12pt7b,SCR_W/2,(int)(SCR_H*0.24),C_MUT,"PROCURANDO GRUPO",textdatum_t::middle_center);
  txtBig(fonts::FreeSansBold24pt7b,2,SCR_W/2,(int)(SCR_H*0.44),C_WHITE,rm,textdatum_t::middle_center);
  int nd=(millis()/450)%4; char aw[24]="aguardando o lider"; int L=strlen(aw); for(int i=0;i<nd;i++)aw[L+i]='.'; aw[L+nd]=0;
  txt(fonts::FreeSans12pt7b,SCR_W/2,(int)(SCR_H*0.62),C_MUT,aw,textdatum_t::middle_center);
  int w=(int)(SCR_W*0.18),h=(int)(SCR_H*0.10),x=(SCR_W-w)/2,y=(int)(SCR_H*0.80); tft.drawRoundRect(x,y,w,h,10,C_LINE); txt(fonts::FreeSansBold12pt7b,SCR_W/2,y+h/2,C_MUT,"VOLTAR",textdatum_t::middle_center); }

void drawMap(){
  tft.fillScreen(C_BG); chromeBg();
  // aneis de distancia (radar) + norte
  for(int r=(int)(SCR_H*0.14); r<(int)(SCR_H*0.46); r+=(int)(SCR_H*0.16)) tft.drawCircle(CXp,CYp,r,C_LINE);
  txt(fonts::FreeSansBold9pt7b,SCR_W/2,10,C_MUT,"N",textdatum_t::top_center); tft.fillTriangle(SCR_W/2,26,SCR_W/2-5,34,SCR_W/2+5,34,C_ROUTE);
  double clat=myLat,clon=myLon,chead=myHeading;
  if(!myFix){ txtBig(fonts::FreeSansBold18pt7b,1,SCR_W/2,CYp,C_AMBER,"PROCURANDO GPS",textdatum_t::middle_center); triMk(CXp,CYp,C_BLUE,20); }
  else {
    int myNear=nearestRouteIdx(clat,clon);
    double myBest=1e9; if(myNear>=0){ int mi=(routeHead-routeN+myNear+ROUTE_MAX)%ROUTE_MAX; myBest=haversine(clat,clon,route[mi].lat,route[mi].lon); }
    int psx=-1,psy=-1; double plat=0,plon=0; bool hp=false;
    int meSlot=isLeader()?0:(joined?curSlot:255);
    // ALERTA: o trecho do caminho ENTRE mim e o OUTRO carro fica VERMELHO (qualquer um que apertar).
    // Antes isto olhava SO o primeiro no ativo, entao num grupo cheio o lider so
    // via o slot 1 e os seguidores so viam o lider: quem apertasse o sino num slot
    // alto nao acendia em NINGUEM. Agora procura qualquer carro em alerta e, se nao
    // houver, cai no primeiro peer (que e quem interessa quando o alerta e o meu).
    int peer=-1, first=-1;
    for(int k=0;k<MAXN;k++){
      if(k==meSlot||!world[k].active||millis()-world[k].lastMs>=NODE_TTL) continue;
      if(first<0) first=k;
      if(world[k].alert && peer<0) peer=k;   // prioriza quem esta pedindo socorro
    }
    bool peerAlert = (peer>=0);
    if(peer<0) peer=first;
    bool alertOn = myAlert || peerAlert;
    int aLo=-1,aHi=-2; if(alertOn&&peer>=0&&myNear>=0){ int pIdx=nearestRouteIdx(world[peer].lat,world[peer].lon); if(pIdx>=0){ aLo=min(myNear,pIdx); aHi=max(myNear,pIdx); } }
    for(int k=0;k<routeN;k++){ int idx=(routeHead-routeN+k+ROUTE_MAX)%ROUTE_MAX; int sx,sy; worldToScreen(route[idx].lat,route[idx].lon,clat,clon,chead,mapMPP,CXp,CYp,sx,sy);
      bool vis=(sx>=-20&&sx<SCR_W+20&&sy>=-20&&sy<SCR_H+20);
      if(vis&&psx>=0){ bool gap=hp&&haversine(plat,plon,route[idx].lat,route[idx].lon)>40.0;
        bool red=(k>aLo&&k<=aHi);   // este segmento esta no trecho entre eu e o outro
        if(!gap){ if(red) roadSeg(psx,psy,sx,sy, 0x6800, C_RED, 11,6);
                  else if(k>myNear) roadSeg(psx,psy,sx,sy, C_ROUTEC, C_ROUTE, 10,5);
                  else roadSeg(psx,psy,sx,sy, C_TRAVC, C_TRAV, 8,3); }
        else drawDashed(psx,psy,sx,sy,C_GHOST); }   // vao (perda de sinal) -> ponte tracejada, nunca some
      psx=vis?sx:-1; psy=sy; plat=route[idx].lat; plon=route[idx].lon; hp=true; }
    // (o TRAJETO do lider e desenhado acima via route[]/roadSeg - o seguidor segue esse caminho)
    // rotulos: guarda onde cada nome foi escrito para nao empilhar texto. Numa fila
    // de trilha lenta os carros ficam a 30m (15px em 2m/px) e os nomes, de 24px de
    // altura, viravam uma mancha sobre os proprios marcadores.
    int lblX[MAXN],lblY[MAXN],nLbl=0;
    for(int k=0;k<MAXN;k++){ if(!world[k].active||k==meSlot) continue; if(millis()-world[k].lastMs>NODE_TTL) continue;
      int sx,sy; worldToScreen(world[k].lat,world[k].lon,clat,clon,chead,mapMPP,CXp,CYp,sx,sy); if(sx<-24||sx>SCR_W+24||sy<-24||sy>SCR_H+24) continue;
      bool al=world[k].alert; uint16_t mc=al?C_RED:(k==0?C_AMBER:colorOf(world[k].color));
      if(k==0) triMk(sx,sy,mc,15); else dotMk(sx,sy,mc,9);
      int lx=sx+12, ly=sy-8;
      for(int pass=0;pass<MAXN;pass++){ bool bateu=false;
        for(int i=0;i<nLbl;i++) if(abs(lblX[i]-lx)<80 && abs(lblY[i]-ly)<20){ ly=lblY[i]+20; bateu=true; break; }
        if(!bateu) break; }
      lblX[nLbl]=lx; lblY[nLbl]=ly; if(nLbl<MAXN-1) nLbl++;
      txt(fonts::FreeSans12pt7b,lx,ly,mc,haveRoster?rname[k]:(k==0?"Lider":"Carro")); }
    triMk(CXp,CYp,C_BLUE,20);
    // BORDA piscando (caixa vazia): VERMELHO=alerta > LARANJA=perda de sinal > AMARELO=fora do trajeto
    bool lost = joined && worldRxMs!=0 && millis()-worldRxMs>4000;   // so acusa perda depois de ja ter recebido sinal
    bool offRoute = !isLeader() && routeN>3 && myBest>OFFROUTE_M;
    if((millis()/300)%2==0){
      if(alertOn){ for(int t=0;t<6;t++) tft.drawRect(t,t,SCR_W-1-2*t,SCR_H-1-2*t,C_RED); }
      else if(lost){ for(int t=0;t<6;t++) tft.drawRect(t,t,SCR_W-1-2*t,SCR_H-1-2*t,C_ORANGE);
        txtBig(fonts::FreeSansBold18pt7b,1,SCR_W/2,26,C_ORANGE,isLeader()?"REDUZA":"SINAL PERDIDO",textdatum_t::top_center); }
      else if(offRoute){ for(int t=0;t<6;t++) tft.drawRect(t,t,SCR_W-1-2*t,SCR_H-1-2*t,C_YEL);
        txtBig(fonts::FreeSansBold18pt7b,1,SCR_W/2,26,C_YEL,"FORA DO TRAJETO",textdatum_t::top_center); }
    }
  }
  chromeFg();
}

void drawUI(){ char b[24];
  int m=(int)(SCR_W*0.024);
  // canto sup esq: GRUPO + SAIR
  card(m,(int)(SCR_H*0.04),200,66);
  txt(fonts::FreeSans9pt7b,m+14,(int)(SCR_H*0.04)+12,C_AMBER,"GRUPO");
  snprintf(b,sizeof(b),"%lu",(unsigned long)g_room); txt(fonts::FreeSansBold24pt7b,m+14,(int)(SCR_H*0.04)+30,C_WHITE,b);
  exX=m;exY=(int)(SCR_H*0.04)+76;exW=200;exH=42; tft.fillRoundRect(exX,exY,exW,exH,10,C_CARD); tft.drawRoundRect(exX,exY,exW,exH,10,C_RED);
  txt(fonts::FreeSansBold12pt7b,exX+exW/2,exY+exH/2,C_RED,"SAIR DO GRUPO",textdatum_t::middle_center);
  // canto sup dir: roster
  int rw=(int)(SCR_W*0.26),rx=SCR_W-rw-m,ry=(int)(SCR_H*0.04); int active=0; for(int k=0;k<MAXN;k++) if(world[k].active&&millis()-world[k].lastMs<NODE_TTL)active++;
  int rh=44+active*30; card(rx,ry,rw,rh);
  snprintf(b,sizeof(b),"NA TRILHA - %d",active); txt(fonts::FreeSans9pt7b,rx+14,ry+12,C_MUT,b);
  int yy=ry+38;
  for(int k=0;k<MAXN;k++){ if(!world[k].active||millis()-world[k].lastMs>NODE_TTL) continue;
    bool me=(!isLeader()&&joined&&k==(int)curSlot)||(isLeader()&&k==0),ld=(k==0); uint16_t col=me?C_BLUE:(ld?C_AMBER:colorOf(world[k].color));
    if(me||ld) tft.fillTriangle(rx+18,yy+4,rx+11,yy+20,rx+25,yy+20,col); else tft.fillCircle(rx+18,yy+13,7,col);
    txt(fonts::FreeSans12pt7b,rx+34,yy+4,world[k].alert?C_RED:C_WHITE,haveRoster?rname[k]:(ld?"Lider":"Carro"));
    if(!me&&myFix&&world[k].fix){ double d=haversine(myLat,myLon,world[k].lat,world[k].lon); char ds[8]; if(d>=1000)snprintf(ds,8,"%.1fk",d/1000.0);else snprintf(ds,8,"%dm",(int)d); txt(fonts::FreeSans12pt7b,rx+rw-14,yy+4,C_MUT,ds,textdatum_t::top_right); }
    yy+=30; }
  // canto inf esq: numero heroi
  int sy=SCR_H-(int)(SCR_H*0.04)-116; card(m,sy,250,116);
  if(isLeader()){
    double dmin=-1; for(int k=1;k<MAXN;k++) if(world[k].active&&world[k].fix&&millis()-world[k].lastMs<NODE_TTL){ double d=haversine(myLat,myLon,world[k].lat,world[k].lon); if(dmin<0||d<dmin)dmin=d; }
    txt(fonts::FreeSans9pt7b,m+16,sy+12,C_MUT,"SEGUIDOR ATRAS");
    if(dmin<0) bigNum(m+16,sy+30,"--","",C_MUT);
    else { char nb[10]; const char*u; if(dmin>=1000){ snprintf(nb,10,"%.1f",dmin/1000.0); u="km"; } else { snprintf(nb,10,"%d",(int)dmin); u="m"; } bigNum(m+16,sy+30,nb,u,C_WHITE); }
    snprintf(b,sizeof(b),"voce: %d km/h",(int)(mySpeed+0.5)); txt(fonts::FreeSans9pt7b,m+16,sy+96,C_MUT,b); }
  else { txt(fonts::FreeSans9pt7b,m+16,sy+12,C_MUT,"DISTANCIA AO LIDER");
    if(world[0].active&&myFix){ double d=haversine(myLat,myLon,world[0].lat,world[0].lon); char nb[10]; const char*u;
      if(d>=1000){ snprintf(nb,10,"%.1f",d/1000.0); u="km"; } else { snprintf(nb,10,"%d",(int)d); u="m"; } bigNum(m+16,sy+30,nb,u,C_WHITE); }
    else bigNum(m+16,sy+30,"--","",C_MUT); }
  // canto inf dir: FAB alerta
  abR=42;abX=SCR_W-abR-m;abY=SCR_H-abR-(int)(SCR_H*0.04); bool on=myAlert;
  tft.fillCircle(abX,abY,abR,on?C_RED:C_CARD); tft.drawCircle(abX,abY,abR,C_RED); tft.drawCircle(abX,abY,abR-1,C_RED);
  uint16_t tc=on?C_WHITE:C_RED; tft.fillTriangle(abX,abY-20,abX-18,abY+14,abX+18,abY+14,tc); tft.fillRect(abX-2,abY-8,5,12,on?C_RED:C_CARD); tft.fillRect(abX-2,abY+8,5,5,on?C_RED:C_CARD);
  // botoes de ZOOM (+ / -) empilhados acima do FAB alerta
  zSz=60; zpX=SCR_W-zSz-m; zpY=abY-abR-16-zSz; zmX=zpX; zmY=zpY-10-zSz;   // zmY = "+" (em cima), zpY = "-" (embaixo)
  tft.fillRoundRect(zmX,zmY,zSz,zSz,12,C_CARD); tft.drawRoundRect(zmX,zmY,zSz,zSz,12,C_LINE);
  tft.fillRect(zmX+16,zmY+zSz/2-3,zSz-32,6,C_WHITE); tft.fillRect(zmX+zSz/2-3,zmY+16,6,zSz-32,C_WHITE);  // "+"
  tft.fillRoundRect(zpX,zpY,zSz,zSz,12,C_CARD); tft.drawRoundRect(zpX,zpY,zSz,zSz,12,C_LINE);
  tft.fillRect(zpX+16,zpY+zSz/2-3,zSz-32,6,C_WHITE);  // "-"
  char zb[12]; snprintf(zb,sizeof(zb),"%dm/px",(int)(mapMPP+0.5f)); txt(fonts::FreeSans9pt7b,zpX+zSz/2,zpY+zSz+4,C_MUT,zb,textdatum_t::top_center);
}

#if P2P
// pareamento 1:1: conecta direto no papel escolhido (LIDER=slot0 / SEGUIDOR=slot1). Par fixo, sem grupo.
void p2pPair(bool asLeader){ g_room=1; g_role=asLeader?1:0; joined=true; searching=false; uiPage=0;
  if(asLeader){ curSlot=0; ruid[0]=myUid; strncpy(rname[0],g_name,15); rname[0][15]=0; rcolor[0]=g_color; strncpy(rname[1],"Seguidor",15); rname[1][15]=0; }
  else        { curSlot=1; strncpy(rname[0],"Lider",15); rname[0][15]=0; }
  haveRoster=true; }
#endif
void handleTouch(){ int32_t tx,ty;
  if(getTouchWS(&tx,&ty)){ if(!touchWasDown){
    if(g_room==0){ if(editName) settingsTouch(tx,ty); else if(uiPage==0){ int gx,gy,gw,gh; gearRect(gx,gy,gw,gh);
        if(tx>=gx&&tx<=gx+gw&&ty>=gy&&ty<=gy+gh){ editName=true; strncpy(nameBuf,g_name,15); nameBuf[15]=0; if(!strcmp(nameBuf,"Carro"))nameBuf[0]=0; }
        else { int bx,bw,bh,by1,by2; homeRects(bx,bw,bh,by1,by2); if(tx>=bx&&tx<=bx+bw){ bool b1=(ty>=by1&&ty<=by1+bh),b2=(ty>=by2&&ty<=by2+bh);
#if P2P
          if(b1) p2pPair(true);        // CRIAR GRUPO = vira LIDER
          else if(b2) p2pPair(false);  // ENTRAR = vira SEGUIDOR
#else
          if(b1){pendingRole=1;uiPage=1;codeLen=0;codeBuf[0]=0;} else if(b2){pendingRole=0;uiPage=1;codeLen=0;codeBuf[0]=0;}
#endif
        } } }
      else keypadTouch(tx,ty); }
    else if(searching){ int w=(int)(SCR_W*0.18),h=(int)(SCR_H*0.10),x=(SCR_W-w)/2,y=(int)(SCR_H*0.80); if(tx>=x&&tx<=x+w&&ty>=y&&ty<=y+h){ g_room=0;searching=false;uiPage=0;joined=false;saveCfg(); } }
    else { if(tx>=exX&&tx<=exX+exW&&ty>=exY&&ty<=exY+exH){
        // LIMPA o estado do grupo. Antes o route[] e o world[] sobreviviam a saida,
        // entao ao entrar em outro codigo o trajeto antigo continuava desenhado e o
        // novo era emendado nele - com a ponte tracejada ligando os dois lugares.
        routeN=0; routeHead=0; haveRouteSeq=false; haveAdd=false; worldRxMs=0;
        peerAckKnown=false; peerAck=0;
        for(int k=0;k<MAXN;k++) world[k].active=false;
#if P2P
        g_room=0; uiPage=0; joined=false; searching=false; myAlert=false;   // 1:1: SAIR volta pra TELA INICIAL
#else
        g_room=0;searching=false;joined=false;uiPage=0;myAlert=false;codeLen=0;codeBuf[0]=0;saveCfg();
#endif
      }
      else if(tx>=zmX&&tx<=zmX+zSz&&ty>=zmY&&ty<=zmY+zSz){ mapMPP*=0.7f; if(mapMPP<0.5f)mapMPP=0.5f; }   // + : aproxima
      else if(tx>=zpX&&tx<=zpX+zSz&&ty>=zpY&&ty<=zpY+zSz){ mapMPP*=1.4f; if(mapMPP>40.0f)mapMPP=40.0f; }   // - : afasta
      else if((tx-abX)*(tx-abX)+(ty-abY)*(ty-abY)<=(abR+8)*(abR+8)) myAlert=!myAlert; }
  } touchWasDown=true; } else touchWasDown=false; }

// desenho roda no 2o nucleo (core 0) -> nao trava o toque/LoRa do loop principal
void drawTask(void*){
  for(;;){
    if(g_room==0){ if(editName) drawSettings(); else if(uiPage==0) drawHome(); else drawKeypad(); }
    else if(searching) drawSearching(); else { drawMap(); vTaskDelay(3/portTICK_PERIOD_MS); drawUI(); }
    // push em FAIXAS com respiro: da janelas ao core 1 p/ amostrar o toque durante o push
    uint16_t* buf=(uint16_t*)tft.getBuffer();
    const int BANDS=10; int bh=SCR_H/BANDS;
    for(int b=0;b<BANDS;b++){ int y0=b*bh; int h=(b==BANDS-1)?(SCR_H-y0):bh;
      lcd.pushImage(0,y0,SCR_W,h, buf+(size_t)y0*SCR_W);
      vTaskDelay(3/portTICK_PERIOD_MS); }
    // desenha devagar de proposito: o mapa muda ~1x/s, entao entre frames o core 1
    // fica LIVRE e o toque responde na hora. Feedback do alerta em <=1 frame.
    vTaskDelay(700/portTICK_PERIOD_MS);
  }
}
void setup(){
  Serial.begin(115200); loadCfg(); applyTheme(g_theme);
  powerUpPanel();          // liga painel, touch fica em reset
  lcd.init();
  SCR_W=lcd.width(); SCR_H=lcd.height(); CXp=SCR_W/2; CYp=SCR_H/2;
  tft.setPsram(true); tft.setColorDepth(16); tft.createSprite(SCR_W,SCR_H);
  resetTouch();            // AGORA reseta o GT911 (painel ja varrendo) -> touch escaneia
  Serial1.begin(9600,SERIAL_8N1,44,43); Serial2.begin(9600,SERIAL_8N1,6,-1); delay(150);
  lora.localread(); myUid=lora.localUniqueId;
  lora.config_bps(BW500, SF_LoRa_7, CR4_5);   // TRAVA o canal RF igual ao GIGA (2/7/1). config_bps() retorna false mesmo aplicando -> nao confiar no retorno.
  for(int k=0;k<MAXN;k++){ world[k]=Node(); world[k].color=rcolor[k]?rcolor[k]:k; if(rname[k][0]==0) snprintf(rname[k],16,k==0?"Lider":"Carro %d",k); }
  if(isLeader()&&g_room){ ruid[0]=myUid; strncpy(rname[0],g_name,15); rname[0][15]=0; rcolor[0]=g_color; haveRoster=true; joined=true; curSlot=0; }
#if P2P
  g_room=0; uiPage=0; joined=false; searching=false; myAlert=false;   // 1:1: comeca na TELA INICIAL; conecta ao apertar um botao
#endif
  lcd.fillScreen(C_BG);
  Serial.print("== GRUPO WS7B == grupo="); Serial.print(g_room); Serial.print(" uid="); Serial.println(myUid);
  xTaskCreatePinnedToCore(drawTask,"draw",16384,NULL,1,NULL,0);   // desenho no core 0
}
void loop(){
  // comando serial: "1" ou "2" nomeia esta tela (Tela 1 / Tela 2) e salva na flash
  while(Serial.available()){ char c=Serial.read();
    if(c=='1'||c=='2'){ snprintf(g_name,16,"Tela %c",c); saveCfg(); Serial.print("nome="); Serial.println(g_name); } }
  readGPS(); readLoRa(); handleTouch();   // 1x por volta; leitura estavel cuida da borda
#if BANCADA
  if(!myFix){ myLat=-23.5500; myLon=-46.6300; myHeading=0; myFix=true; mySats=9; }  // Waveshare fake
#endif
  unsigned long now=millis();
  { static unsigned long lt=0; unsigned long dt=now-lt; lt=now; if(dt>loopMax)loopMax=dt; }  // pior tempo de loop
  int ms=isLeader()?0:curSlot;
  if(isLeader()||joined){ world[ms].active=true; world[ms].fix=myFix; world[ms].alert=myAlert; world[ms].lat=myLat; world[ms].lon=myLon; world[ms].lastMs=now; world[ms].color=g_color; }
  // EXPIRA quem parou de reportar. Sem isto o active fica ligado para sempre: o
  // LIDER segue anunciando active=1 no WORLD e o parseRx do seguidor refresca o
  // lastMs LOCAL a cada WORLD, entao o NODE_TTL nunca dispara nos seguidores e um
  // carro que sumiu continua no mapa deles, parado na ultima posicao, para sempre.
  for(int k=0;k<MAXN;k++) if(k!=ms && world[k].active && now-world[k].lastMs>NODE_TTL) world[k].active=false;
#if P2P
  // so conversa quando CONECTADO (joined). Na tela inicial o radio fica QUIETO -> reset = comeca limpo.
  if(joined){
    if(isLeader()){
      // LIDER: grava o proprio caminho e manda posicao + TRAJETO 1 Hz pro seguidor
      leaderRecordOwnPath();
      // 1 Hz normal; durante o catch-up (seguidor muito atras) acelera pra 3 Hz pra preencher o vao rapido
      bool behind = peerAckKnown && ((int16_t)(peerAck-routeSeq) < -TRAIL_N);
      static unsigned long lp=0; if(now-lp>(behind?330UL:1000UL)){ sendTrail(); lp=now; }
    } else {
      // SEGUIDOR: responde com a posicao ao ouvir o lider; fallback 2s
      if(pendingReply && now>=replyDue){ sendP2P(); pendingReply=false; lastP2PTx=now; }
      else if(now-lastP2PTx>2000){ sendP2P(); lastP2PTx=now; }
    }
  } else { worldRxMs=0; pendingReply=false; }
#else
  if(g_room!=0){
    if(isLeader()){ leaderRecordOwnPath(); static uint8_t cyc=0; if(now-lastCycle>=CYCLE_MS){ if(myFix){ sendWorld(); if((cyc++%3)==0) sendRoster(); } lastCycle=now; } }
    else if(!joined){ static unsigned long lj=0; if(now-lj>1500){ sendJoin(); lj=now; } }
    else if(joined){ static unsigned long lu=0;
      // USA o slot: o parseRx ja calcula slotDue = worldRxMs + curSlot*SLOT_MS.
      // Antes isto era ignorado e o uplink saia num timer livre de 1200ms - e como
      // o ROSTER e broadcast, os seguidores comecavam a contar no mesmo instante,
      // entravam em fase e colidiam sistematicamente (medido: ate 100% de colisao
      // com 3 seguidores, e o canal 95% livre). Com o slot, cada um fala 450ms
      // depois do anterior: zero colisao e metade da ocupacao de ar.
      if(pendingUplink && (long)(now-slotDue)>=0){ sendUplink(); pendingUplink=false; lu=now; }
      else if(now-lu>3000){ sendUplink(); lu=now; }   // fallback: WORLD nao chegou
    }
  }
#endif
  // desenho agora roda na drawTask (core 0); o loop fica leve p/ o toque responder
  if(now-lastDbg>2000){ int c=0; for(int k=0;k<MAXN;k++) if(world[k].active&&now-world[k].lastMs<NODE_TTL)c++;
    Serial.printf("WS | grupo=%lu %s slot=%d | world=%s nos=%d fix=%d sats=%d gpsChars=%lu | txUp=%lu loopMax=%lums\n", (unsigned long)g_room,
      isLeader()?"LIDER":(searching?"searching":(joined?"seguidor":"---")), (int)curSlot, worldRxMs?"ok":"--", c, myFix?1:0, (int)mySats, (unsigned long)gps.charsProcessed(), (unsigned long)txUp, (unsigned long)loopMax); loopMax=0; lastDbg=now; }
}
