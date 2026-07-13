/*
 * ============================================================================
 *  ODOMETRO GPS  -  Arduino GIGA R1 WiFi + MKR GPS Shield + GIGA Display Shield
 * ============================================================================
 *  Versao 4 (revisada) - GPS por UART + TinyGPSPlus + tela GFX + toque.
 *
 *  >>> POR QUE UART E NAO I2C? <<<
 *  Pelo HEADER, o MKR GPS Shield se comunica por UART (Serial). O I2C do
 *  modulo (SAM-M8Q) so esta disponivel no conector ESLOV. Por isso tentar
 *  I2C nos pinos 11/12 do header nunca funcionou.
 *
 *  >>> LIGACAO (UART) <<<
 *    Shield VCC     -> GIGA 3V3     (o modulo e 3,3V; o pino "5V" do header
 *                                    NAO alimenta o GPS!)
 *    Shield GND     -> GIGA GND
 *    Shield 13 (RX) -> GIGA 0 (RX0) (e por aqui que o dado do GPS chega)
 *    Shield 14 (TX) -> GIGA 1 (TX0)
 *    GIGA Display Shield encaixado direto no GIGA.
 *
 *  Serial1 do GIGA = pino 0 (RX) / pino 1 (TX). O u-blox manda NMEA a 9600.
 *
 *  >>> BIBLIOTECAS <<<  TinyGPSPlus, Arduino_GigaDisplay_GFX, Arduino_GigaDisplayTouch
 *
 *  >>> O QUE ESPERAR <<<
 *    - Dentro do escritorio: COMUNICACAO=OK, FIX=NAO, SAT=0, VEL=0 (normal).
 *    - Perto de janela / na rua: FIX=SIM, satelites, velocidade; e os
 *      waypoints sao plotados a cada 10 m de deslocamento.
 *    - A porta USB tambem imprime um status a cada 1 s (para depuracao).
 * ============================================================================
 */

#include <TinyGPSPlus.h>
#include "Arduino_GigaDisplay_GFX.h"
#include "Arduino_GigaDisplayTouch.h"

GigaDisplay_GFX gfx;
Arduino_GigaDisplayTouch touch;
TinyGPSPlus gps;

// ----------------------------------------------------- parametros ajustaveis
const float PX_PER_MM        = 5.25f;   // densidade da tela (~5,25 px/mm) - calibrar c/ regua
const float MAP_M_PER_PX     = 200.0f / (10.0f * PX_PER_MM); // 10 mm de tela = 200 m reais
int         PANEL_W          = 160;     // largura do painel (spec: 15 mm ~ 79 px; +largo p/ ler)
const float WAYPOINT_STEP_M  = 10.0f;   // grava um ponto a cada 10 m
const float JITTER_FLOOR_M   = 2.5f;    // ignora movimento menor que isso (ruido parado)
const float JUMP_CEIL_M      = 1000.0f; // ignora saltos absurdos entre leituras
const int   MIN_SATS_FOR_DIST= 4;       // so soma distancia com fix decente (>=4 satelites)
const int   MAX_POINTS       = 10000;   // memoria de waypoints

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

// ----------------------------------------------------- geometria (no setup)
int SCR_W, SCR_H, PANEL_X, PLOT_W, PLOT_H, PLOT_CX, PLOT_CY;
int btnX, btnY, btnW, btnH;

// ----------------------------------------------------- memoria de waypoints
struct Pt { float x; float y; };        // metros relativos a origem (Leste, Norte)
Pt  points[MAX_POINTS];
int numPoints = 0;

// ----------------------------------------------------- estado do GPS / odometro
bool   originSet = false;
double originLat = 0, originLon = 0, lastLat = 0, lastLon = 0;
float  totalDistance = 0, distSinceWaypoint = 0;
int    satellites = 0;
float  speedKmh = 0;
bool   gpsCommOk = false, gpsFix = false, everComm = false;
unsigned long lastByteMs = 0, lastFixMs = 0, lastDraw = 0, lastDbg = 0;

// ----------------------------------------------------- desenho / toque
int  lastSX = -1, lastSY = -1;
bool touchWasDown = false;
int  dbgTX = -1, dbgTY = -1;

// ============================================================================
void setup() {
  Serial.begin(115200);                 // USB (depuracao)
  Serial1.begin(9600);                  // GPS por UART (pinos 0/1)

  gfx.begin();
  gfx.setRotation(0);                   // retrato; toque alinhado ao desenho
  SCR_W = gfx.width();
  SCR_H = gfx.height();
  computeLayout();
  touch.begin();

  gfx.fillScreen(C_BLACK);
  drawStaticUI();
}

// ============================================================================
void loop() {
  readGPS();                            // le UART, alimenta o parser, ajusta gpsCommOk

  if (gps.satellites.isValid()) satellites = gps.satellites.value();
  if (gps.speed.isValid())      speedKmh   = gps.speed.kmph();

  // Nova posicao valida? registra fix e (se fix decente) acumula distancia.
  if (gps.location.isValid() && gps.location.isUpdated()) {
    lastFixMs = millis();
    if (gps.satellites.isValid() && gps.satellites.value() >= MIN_SATS_FOR_DIST)
      processFix(gps.location.lat(), gps.location.lng());
  }
  gpsFix = (lastFixMs != 0) && (millis() - lastFixMs < 3000);

  handleTouch();

  if (millis() - lastDraw > 250)  { drawData(); lastDraw = millis(); }
  if (millis() - lastDbg  > 1000) { debugSerial(); lastDbg  = millis(); }
}

// ============================================================================
//  LEITURA DO GPS (UART)
// ============================================================================
void readGPS() {
  while (Serial1.available()) {
    gps.encode(Serial1.read());
    lastByteMs = millis();
    everComm = true;
  }
  // Comunicacao OK = recebendo NMEA nos ultimos 2 s (independe de ter fix).
  gpsCommOk = everComm && (millis() - lastByteMs < 2000);
}

void debugSerial() {
  Serial.print("COM="); Serial.print(gpsCommOk ? "OK " : "-- ");
  Serial.print(" FIX="); Serial.print(gpsFix ? "SIM" : "NAO");
  Serial.print(" SAT="); Serial.print(satellites);
  Serial.print(" VEL="); Serial.print(speedKmh, 1);
  Serial.print(" DIST="); Serial.print(totalDistance, 0);
  Serial.print(" PTS="); Serial.print(numPoints);
  if (gps.location.isValid()) {
    Serial.print(" LAT="); Serial.print(gps.location.lat(), 6);
    Serial.print(" LON="); Serial.print(gps.location.lng(), 6);
  }
  Serial.print(" (NMEA chars="); Serial.print(gps.charsProcessed());
  Serial.print(", checksum_fail="); Serial.print(gps.failedChecksum());
  Serial.println(")");
}

// ============================================================================
//  CALCULO DE DISTANCIA / COORDENADAS
// ============================================================================
double haversine(double la1, double lo1, double la2, double lo2) {
  const double R = 6371000.0;
  double dLa = radians(la2 - la1), dLo = radians(lo2 - lo1);
  double a = sin(dLa / 2) * sin(dLa / 2) +
             cos(radians(la1)) * cos(radians(la2)) * sin(dLo / 2) * sin(dLo / 2);
  return R * 2 * atan2(sqrt(a), sqrt(1 - a));
}

void latlonToXY(double lat, double lon, float &x, float &y) {
  const double R = 6371000.0;
  x = (float)(R * radians(lon - originLon) * cos(radians(originLat)));  // Leste
  y = (float)(R * radians(lat - originLat));                           // Norte
}

void processFix(double lat, double lon) {
  if (!originSet) {                     // primeiro fix bom = origem do mapa
    originSet = true;
    originLat = lat; originLon = lon; lastLat = lat; lastLon = lon;
    addPoint(lat, lon);
    return;
  }
  double d = haversine(lastLat, lastLon, lat, lon);
  if (d < JITTER_FLOOR_M || d > JUMP_CEIL_M) return;   // filtra ruido e saltos

  totalDistance     += d;
  distSinceWaypoint += d;
  lastLat = lat; lastLon = lon;

  if (distSinceWaypoint >= WAYPOINT_STEP_M) {
    distSinceWaypoint = 0;
    addPoint(lat, lon);
  }
}

void addPoint(double lat, double lon) {
  if (numPoints >= MAX_POINTS) return;  // memoria cheia
  float x, y;
  latlonToXY(lat, lon, x, y);
  points[numPoints].x = x; points[numPoints].y = y;
  numPoints++;
  plotPoint(numPoints - 1);
}

// ============================================================================
//  DESENHO DO MAPA
// ============================================================================
void plotPoint(int i) {
  int sx = PLOT_CX + (int)(points[i].x / MAP_M_PER_PX);
  int sy = PLOT_CY - (int)(points[i].y / MAP_M_PER_PX);     // Norte para cima
  if (sx < 1 || sx >= PLOT_W - 1 || sy < 1 || sy >= SCR_H - 1) {
    lastSX = sx; lastSY = sy; return;                       // fora da area visivel
  }
  if (lastSX >= 0 && lastSX < PLOT_W)
    gfx.drawLine(lastSX, lastSY, sx, sy, C_CYAN);
  gfx.fillCircle(sx, sy, 2, C_YEL);
  lastSX = sx; lastSY = sy;
}

void drawCrosshair() {
  gfx.drawFastHLine(PLOT_CX - 8, PLOT_CY, 16, C_GREY);
  gfx.drawFastVLine(PLOT_CX, PLOT_CY - 8, 16, C_GREY);
}

void drawScaleBar() {
  int bar = (int)(10.0f * PX_PER_MM);
  int bx = 12, by = SCR_H - 24;
  gfx.drawFastHLine(bx, by, bar, C_WHITE);
  gfx.drawFastVLine(bx, by - 4, 8, C_WHITE);
  gfx.drawFastVLine(bx + bar, by - 4, 8, C_WHITE);
  gfx.setTextSize(1); gfx.setTextColor(C_WHITE);
  gfx.setCursor(bx, by - 16); gfx.print("200 m");
}

// ============================================================================
//  INTERFACE
// ============================================================================
void computeLayout() {
  PANEL_X = SCR_W - PANEL_W;
  PLOT_W  = SCR_W - PANEL_W; PLOT_H = SCR_H;
  PLOT_CX = PLOT_W / 2;      PLOT_CY = PLOT_H / 2;
  btnW = PANEL_W - 16; btnH = 56;
  btnX = PANEL_X + 8;  btnY = SCR_H - btnH - 12;
}

void drawStaticUI() {
  gfx.drawFastVLine(PANEL_X, 0, SCR_H, C_DGREY);
  drawCrosshair();
  drawScaleBar();
  gfx.drawRect(btnX, btnY, btnW, btnH, C_RED);
  drawData();
}

int panelBlock(int y, const char* label, const char* value, uint16_t col) {
  int x = PANEL_X + 8;
  gfx.fillRect(PANEL_X + 1, y, PANEL_W - 1, 40, C_BLACK);
  gfx.setTextSize(1); gfx.setTextColor(C_GREY);
  gfx.setCursor(x, y); gfx.print(label);
  gfx.setTextSize(2); gfx.setTextColor(col);
  gfx.setCursor(x, y + 12); gfx.print(value);
  return y + 42;
}

void drawData() {
  char b[24];
  int y = 10;
  y = panelBlock(y, "COMUNICACAO", gpsCommOk ? "OK" : "FALHA", gpsCommOk ? C_GREEN : C_RED);
  y = panelBlock(y, "FIX GPS", gpsFix ? "SIM" : "NAO", gpsFix ? C_GREEN : C_ORANG);
  snprintf(b, sizeof(b), "%d", satellites);            y = panelBlock(y, "SATELITES", b, C_CYAN);
  snprintf(b, sizeof(b), "%.1f km/h", speedKmh);       y = panelBlock(y, "VELOCIDADE", b, C_WHITE);
  snprintf(b, sizeof(b), "%.0f m", totalDistance);     y = panelBlock(y, "DISTANCIA", b, C_YEL);
  snprintf(b, sizeof(b), "%d/%d", numPoints, MAX_POINTS); y = panelBlock(y, "WAYPOINTS", b, C_GREY);

  gfx.fillRect(PANEL_X + 1, y, PANEL_W - 1, 12, C_BLACK);
  if (dbgTX >= 0) {
    snprintf(b, sizeof(b), "toque %d,%d", dbgTX, dbgTY);
    gfx.setTextSize(1); gfx.setTextColor(C_DGREY);
    gfx.setCursor(PANEL_X + 8, y); gfx.print(b);
  }

  gfx.setTextSize(2); gfx.setTextColor(C_RED);
  gfx.setCursor(btnX + 18, btnY + 18); gfx.print("RESET");
}

// ============================================================================
//  TOQUE / RESET
// ============================================================================
void handleTouch() {
  GDTpoint_t p[5];
  uint8_t n = touch.getTouchPoints(p);
  if (n > 0) {
    dbgTX = p[0].x; dbgTY = p[0].y;
    bool inBtn = (dbgTX >= btnX && dbgTX <= btnX + btnW && dbgTY >= btnY && dbgTY <= btnY + btnH);
    if (inBtn && !touchWasDown) doReset();
    touchWasDown = true;
  } else {
    touchWasDown = false;
  }
}

void doReset() {
  numPoints = 0; totalDistance = 0; distSinceWaypoint = 0;
  originSet = false; lastSX = -1; lastSY = -1;
  gfx.fillRect(0, 0, PLOT_W, SCR_H, C_BLACK);
  drawCrosshair(); drawScaleBar(); drawData();
  gfx.fillRect(btnX + 1, btnY + 1, btnW - 2, btnH - 2, C_RED);   // feedback visual
  gfx.setTextSize(2); gfx.setTextColor(C_WHITE);
  gfx.setCursor(btnX + 18, btnY + 18); gfx.print("RESET");
  delay(120);
  gfx.fillRect(btnX + 1, btnY + 1, btnW - 2, btnH - 2, C_BLACK);
  gfx.drawRect(btnX, btnY, btnW, btnH, C_RED);
}
