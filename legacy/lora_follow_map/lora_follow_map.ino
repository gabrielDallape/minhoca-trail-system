/*
 * ============================================================================
 *  SEGUIDOR (GIGA) - Mapa estilo Waze: EU no centro + rastro do LIDER (MKR)
 * ============================================================================
 *  - GPS proprio do GIGA -> Serial1 (pinos 0/1), NMEA 9600, TinyGPSPlus.
 *      Diz ONDE EU ESTOU e para onde estou indo (course), para orientar o mapa.
 *  - LoRa master         -> Serial2 (pinos 18/19). Recebe a posicao do LIDER
 *      (pacote de aplicacao cmd 0x11, enviado pelo MKR/slave).
 *
 *  A TELA:
 *   - EU fico SEMPRE no centro (triangulo verde apontando pra cima).
 *   - Mapa HEADING-UP: gira conforme meu rumo (Waze). Parado, mantem o ultimo.
 *   - LIDER = circulo laranja; rastro (breadcrumb) do caminho dele em ciano.
 *   - Se o lider estiver fora da tela: seta na borda apontando pra ele.
 *   - Linha fina de mim ate o lider + distancia.
 *   - Painel a direita: LINK LoRa, meu FIX, FIX do lider, distancia, sats, vel.
 *   - Botao RESET (toque) limpa o rastro.
 *
 *  Dentro de casa (sem fix): o painel ja certifica o LINK e a comunicacao dos
 *  GPS; o mapa enche quando pega ceu. USB imprime status a cada 1 s.
 * ============================================================================
 */
#include <TinyGPSPlus.h>
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"
#include "LoRaMESH.h"

GigaDisplay_GFX          gfx;
Arduino_GigaDisplayTouch touch;
TinyGPSPlus              gps;      // meu GPS (Serial1)
LoRaMESH                 lora(&Serial2);

// ----------------------------------------------------- parametros ajustaveis
const bool  HEADING_UP    = true;    // true = gira com meu rumo (Waze); false = norte pra cima
const float PX_PER_MM     = 5.25f;   // densidade da tela (calibrar c/ regua)
const float MAP_M_PER_PX  = 200.0f / (10.0f * PX_PER_MM);   // 10 mm = 200 m (~3,81 m/px)
int         PANEL_W       = 160;
const float TRAIL_STEP_M  = 3.0f;    // grava ponto do rastro do lider a cada 3 m
const int   TRAIL_MAX     = 700;     // tamanho do rastro (ring buffer)
const float MIN_SPD_COURSE= 1.5f;    // km/h minimo pra confiar no rumo do GPS
const uint8_t APP_CMD_GPS = 0x11;

// ----------------------------------------------------- cores (RGB565)
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_YEL   0xFFE0
#define C_CYAN  0x07FF
#define C_GREY  0x8410
#define C_DGREY 0x39E7
#define C_ORANG 0xFD20
#define C_BLUE  0x2D7F

// ----------------------------------------------------- geometria
int SCR_W, SCR_H, PANEL_X, PLOT_W, PLOT_H, PLOT_CX, PLOT_CY;
int btnX, btnY, btnW, btnH;

// ----------------------------------------------------- meu estado (GIGA)
double myLat = 0, myLon = 0;
bool   myFix = false, myCommOk = false, everMyComm = false;
int    mySats = 0;
float  mySpeed = 0, myHeading = 0;          // rumo em graus (0=Norte)
unsigned long myLastByteMs = 0, myLastFixMs = 0;

// ----------------------------------------------------- estado do lider (via LoRa)
double ldrLat = 0, ldrLon = 0;
bool   ldrFix = false;
int    ldrSats = 0;
unsigned long lastRxMs = 0;
uint32_t pktCount = 0;
bool   linkOk = false;

// ----------------------------------------------------- rastro do lider (lat/lon)
struct Geo { double lat; double lon; };
Geo   trail[TRAIL_MAX];
int   trailCount = 0, trailHead = 0;        // ring buffer
double lastTrailLat = 0, lastTrailLon = 0;
bool  haveTrail = false;

// ----------------------------------------------------- desenho / toque
unsigned long lastDraw = 0, lastDbg = 0;
bool touchWasDown = false;

const double R_EARTH = 6371000.0;

// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);          // meu GPS
  Serial2.begin(9600);          // LoRa (comando)
  delay(150);
  lora.begin(false);            // le meu ID (deve ser 0 = master)

  gfx.begin();
  gfx.setRotation(0);
  SCR_W = gfx.width();
  SCR_H = gfx.height();
  computeLayout();
  touch.begin();

  gfx.startBuffering();          // desenha tudo em RAM e da UM flush (sem tearing)
  gfx.fillScreen(C_BLACK);
  drawStaticUI();
  gfx.endBuffering();
}

// ============================================================================
void loop() {
  readMyGPS();
  readLoRa();

  linkOk = (lastRxMs != 0) && (millis() - lastRxMs < 3000);
  myFix  = (myLastFixMs != 0) && (millis() - myLastFixMs < 3000);

  handleTouch();

  if (millis() - lastDraw > 300) {
    gfx.startBuffering();        // quadro inteiro em RAM -> UM flush (sem flicker)
    drawMap();
    drawPanel();
    gfx.endBuffering();
    lastDraw = millis();
  }
  if (millis() - lastDbg  > 1000) { debugSerial(); lastDbg = millis(); }
}

// ============================================================================
//  ENTRADAS
// ============================================================================
void readMyGPS() {
  while (Serial1.available()) {
    gps.encode(Serial1.read());
    myLastByteMs = millis();
    everMyComm = true;
  }
  myCommOk = everMyComm && (millis() - myLastByteMs < 2000);

  if (gps.satellites.isValid()) mySats  = gps.satellites.value();
  if (gps.speed.isValid())      mySpeed = gps.speed.kmph();
  if (gps.course.isValid() && mySpeed >= MIN_SPD_COURSE) myHeading = gps.course.deg();

  if (gps.location.isValid() && gps.location.isUpdated()) {
    myLat = gps.location.lat();
    myLon = gps.location.lng();
    myLastFixMs = millis();
  }
}

void readLoRa() {
  if (!Serial2.available()) return;
  uint16_t id = 0xFFFF;
  uint8_t  cmd = 0, p[240], plen = 0;
  if (lora.ReceivePacketCommand(&id, &cmd, p, &plen, 300)) {
    if (cmd == APP_CMD_GPS && plen >= 10) {
      lastRxMs = millis();
      pktCount++;
      ldrFix  = p[0] & 0x01;
      ldrSats = p[1];
      int32_t lat = (int32_t)((uint32_t)p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24));
      int32_t lon = (int32_t)((uint32_t)p[6] | ((uint32_t)p[7] << 8) | ((uint32_t)p[8] << 16) | ((uint32_t)p[9] << 24));
      if (ldrFix) {
        ldrLat = lat / 1e7;
        ldrLon = lon / 1e7;
        addTrail(ldrLat, ldrLon);
      }
    }
  }
}

void addTrail(double lat, double lon) {
  if (haveTrail) {
    double d = haversine(lastTrailLat, lastTrailLon, lat, lon);
    if (d < TRAIL_STEP_M) return;      // moveu pouco: nao grava
  }
  trail[trailHead].lat = lat;
  trail[trailHead].lon = lon;
  trailHead = (trailHead + 1) % TRAIL_MAX;
  if (trailCount < TRAIL_MAX) trailCount++;
  lastTrailLat = lat; lastTrailLon = lon; haveTrail = true;
}

// ============================================================================
//  GEO
// ============================================================================
double haversine(double la1, double lo1, double la2, double lo2) {
  double dLa = radians(la2 - la1), dLo = radians(lo2 - lo1);
  double a = sin(dLa/2)*sin(dLa/2) + cos(radians(la1))*cos(radians(la2))*sin(dLo/2)*sin(dLo/2);
  return R_EARTH * 2 * atan2(sqrt(a), sqrt(1 - a));
}

double bearingTo(double la1, double lo1, double la2, double lo2) {
  double y = sin(radians(lo2 - lo1)) * cos(radians(la2));
  double x = cos(radians(la1))*sin(radians(la2)) -
             sin(radians(la1))*cos(radians(la2))*cos(radians(lo2 - lo1));
  double b = degrees(atan2(y, x));
  return (b < 0) ? b + 360 : b;
}

// Converte um ponto geografico para tela, com EU no centro e (opcional) heading-up.
void worldToScreen(double lat, double lon, int &sx, int &sy) {
  double east  = R_EARTH * cos(radians(myLat)) * radians(lon - myLon);   // metros a Leste de mim
  double north = R_EARTH * radians(lat - myLat);                         // metros ao Norte de mim
  double up, right;
  if (HEADING_UP) {
    double h = radians(myHeading);
    up    =  north * cos(h) + east * sin(h);
    right =  east  * cos(h) - north * sin(h);
  } else {
    up = north; right = east;
  }
  sx = PLOT_CX + (int)(right / MAP_M_PER_PX);
  sy = PLOT_CY - (int)(up    / MAP_M_PER_PX);
}

// ============================================================================
//  DESENHO DO MAPA (EU no centro)
// ============================================================================
void drawMap() {
  gfx.fillRect(0, 0, PLOT_W, SCR_H, C_BLACK);   // limpa area do mapa

  if (!myFix) {                                 // sem minha posicao, nao da pra centrar
    gfx.setTextSize(2); gfx.setTextColor(C_ORANG);
    gfx.setCursor(14, PLOT_CY - 20); gfx.print("SEM FIX (EU)");
    gfx.setTextSize(1); gfx.setTextColor(C_GREY);
    gfx.setCursor(14, PLOT_CY + 6);  gfx.print("aguardando o ceu...");
    drawMeMarker();
    drawNorthTag();
    return;
  }

  // rastro do lider
  int psx = -1, psy = -1;
  for (int k = 0; k < trailCount; k++) {
    int idx = (trailHead - trailCount + k + TRAIL_MAX) % TRAIL_MAX;
    int sx, sy;
    worldToScreen(trail[idx].lat, trail[idx].lon, sx, sy);
    bool vis = (sx >= 0 && sx < PLOT_W && sy >= 0 && sy < SCR_H);
    if (vis) {
      if (psx >= 0) gfx.drawLine(psx, psy, sx, sy, C_CYAN);
      gfx.fillCircle(sx, sy, 1, C_CYAN);
    }
    psx = sx; psy = sy;
  }

  // lider (posicao atual)
  if (linkOk && ldrFix) {
    int lx, ly;
    worldToScreen(ldrLat, ldrLon, lx, ly);
    bool vis = (lx >= 0 && lx < PLOT_W && ly >= 0 && ly < SCR_H);
    if (vis) {
      gfx.drawLine(PLOT_CX, PLOT_CY, lx, ly, C_DGREY);   // linha ate o lider
      gfx.fillCircle(lx, ly, 5, C_ORANG);
      gfx.drawCircle(lx, ly, 8, C_WHITE);
    } else {
      drawEdgeArrow(lx, ly);                              // seta na borda
    }
  }

  drawMeMarker();
  drawNorthTag();
}

// EU: triangulo verde no centro, apontando pra cima (=minha direcao no heading-up)
void drawMeMarker() {
  int cx = PLOT_CX, cy = PLOT_CY;
  gfx.fillTriangle(cx, cy - 10, cx - 7, cy + 8, cx + 7, cy + 8, C_GREEN);
  gfx.drawTriangle(cx, cy - 10, cx - 7, cy + 8, cx + 7, cy + 8, C_WHITE);
}

// Seta na borda do mapa apontando para o lider (quando ele esta fora da tela)
void drawEdgeArrow(int lx, int ly) {
  float dx = lx - PLOT_CX, dy = ly - PLOT_CY;
  float len = sqrt(dx*dx + dy*dy);
  if (len < 1) return;
  dx /= len; dy /= len;
  int margin = 20;
  int ex = PLOT_CX + (int)(dx * (PLOT_CX - margin));
  int ey = PLOT_CY + (int)(dy * (PLOT_CY - margin));
  ex = constrain(ex, margin, PLOT_W - margin);
  ey = constrain(ey, margin, SCR_H - margin);
  gfx.fillCircle(ex, ey, 6, C_ORANG);
  gfx.drawLine(ex, ey, ex - (int)(dx*14) + (int)(dy*7), ey - (int)(dy*14) - (int)(dx*7), C_ORANG);
  gfx.drawLine(ex, ey, ex - (int)(dx*14) - (int)(dy*7), ey - (int)(dy*14) + (int)(dx*7), C_ORANG);
}

// Indicador de Norte (gira junto quando heading-up)
void drawNorthTag() {
  int nx = 26, ny = 30;
  double h = HEADING_UP ? radians(myHeading) : 0;
  // vetor "norte" na tela: (right,up) = (-sin h, cos h)
  float ux = -sin(h), uy = cos(h);
  gfx.drawLine(nx, ny, nx + (int)(ux*14), ny - (int)(uy*14), C_RED);
  gfx.setTextSize(1); gfx.setTextColor(C_RED);
  gfx.setCursor(nx + (int)(ux*16) - 3, ny - (int)(uy*16) - 3); gfx.print("N");
}

// ============================================================================
//  PAINEL
// ============================================================================
void computeLayout() {
  PANEL_X = SCR_W - PANEL_W;
  PLOT_W  = SCR_W - PANEL_W; PLOT_H = SCR_H;
  PLOT_CX = PLOT_W / 2;      PLOT_CY = PLOT_H / 2;
  btnW = PANEL_W - 16; btnH = 52;
  btnX = PANEL_X + 8;  btnY = SCR_H - btnH - 12;
}

void drawStaticUI() {
  gfx.drawFastVLine(PANEL_X, 0, SCR_H, C_DGREY);
  gfx.drawRect(btnX, btnY, btnW, btnH, C_RED);
  drawPanel();
}

int panelBlock(int y, const char* label, const char* value, uint16_t col) {
  int x = PANEL_X + 8;
  gfx.fillRect(PANEL_X + 1, y, PANEL_W - 1, 36, C_BLACK);
  gfx.setTextSize(1); gfx.setTextColor(C_GREY, C_BLACK);        // fundo opaco
  gfx.setCursor(x, y); gfx.print(label);
  gfx.setTextSize(2); gfx.setTextColor(col, C_BLACK);           // fundo opaco
  gfx.setCursor(x, y + 11); gfx.print(value);
  return y + 38;
}

void drawPanel() {
  char b[24];
  gfx.fillRect(PANEL_X + 1, 0, PANEL_W - 1, SCR_H, C_BLACK);    // limpa painel inteiro
  int y = 8;
  y = panelBlock(y, "LINK LoRa", linkOk ? "OK" : "SEM", linkOk ? C_GREEN : C_RED);
  y = panelBlock(y, "EU (FIX)",  myFix ? "SIM" : "NAO", myFix ? C_GREEN : C_ORANG);
  y = panelBlock(y, "LIDER (FIX)", (linkOk && ldrFix) ? "SIM" : "NAO", (linkOk && ldrFix) ? C_GREEN : C_ORANG);

  if (myFix && linkOk && ldrFix) {
    double dist = haversine(myLat, myLon, ldrLat, ldrLon);
    if (dist >= 1000) snprintf(b, sizeof(b), "%.2f km", dist / 1000.0);
    else              snprintf(b, sizeof(b), "%.0f m", dist);
    y = panelBlock(y, "DIST LIDER", b, C_YEL);
    double brg = bearingTo(myLat, myLon, ldrLat, ldrLon);
    snprintf(b, sizeof(b), "%.0f%c", brg, (char)247);
    y = panelBlock(y, "RUMO->LIDER", b, C_CYAN);
  } else {
    y = panelBlock(y, "DIST LIDER", "--", C_GREY);
    y = panelBlock(y, "RUMO->LIDER", "--", C_GREY);
  }

  snprintf(b, sizeof(b), "%.1f km/h", mySpeed);           y = panelBlock(y, "MINHA VEL", b, C_WHITE);
  snprintf(b, sizeof(b), "%d / %d", mySats, ldrSats);     y = panelBlock(y, "SAT eu/lid", b, C_GREY);
  snprintf(b, sizeof(b), "%lu", (unsigned long)pktCount); y = panelBlock(y, "PACOTES", b, C_DGREY);

  gfx.drawRect(btnX, btnY, btnW, btnH, C_RED);                  // borda do botao
  gfx.setTextSize(2); gfx.setTextColor(C_RED, C_BLACK);
  gfx.setCursor(btnX + 16, btnY + 16); gfx.print("RESET");
}

// ============================================================================
//  TOQUE / RESET / DEBUG
// ============================================================================
void handleTouch() {
  GDTpoint_t p[5];
  uint8_t n = touch.getTouchPoints(p);
  if (n > 0) {
    int tx = p[0].x, ty = p[0].y;
    bool inBtn = (tx >= btnX && tx <= btnX + btnW && ty >= btnY && ty <= btnY + btnH);
    if (inBtn && !touchWasDown) { trailCount = 0; trailHead = 0; haveTrail = false; }
    touchWasDown = true;
  } else {
    touchWasDown = false;
  }
}

void debugSerial() {
  Serial.print("LINK="); Serial.print(linkOk ? "OK" : "--");
  Serial.print(" pkt="); Serial.print(pktCount);
  Serial.print(" | EU fix="); Serial.print(myFix ? "S" : "N");
  Serial.print(" sat="); Serial.print(mySats);
  Serial.print(" hd="); Serial.print(myHeading, 0);
  if (myFix) { Serial.print(" ("); Serial.print(myLat,5); Serial.print(","); Serial.print(myLon,5); Serial.print(")"); }
  Serial.print(" | LIDER fix="); Serial.print(ldrFix ? "S" : "N");
  Serial.print(" sat="); Serial.print(ldrSats);
  if (ldrFix) { Serial.print(" ("); Serial.print(ldrLat,5); Serial.print(","); Serial.print(ldrLon,5); Serial.print(")"); }
  Serial.print(" | myNMEA="); Serial.print(gps.charsProcessed());
  Serial.println();
}
