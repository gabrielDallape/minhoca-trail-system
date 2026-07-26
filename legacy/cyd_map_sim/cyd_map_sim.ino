/*
 * CYD (ESP32) - SIMULACAO do mapa estilo Waze (sem LoRa, sem GPS).
 * -------------------------------------------------------------------------
 * Mostra como vai ficar a tela do seguidor:
 *   - EU no centro (triangulo verde), mapa HEADING-UP (gira com meu rumo).
 *   - LIDER (circulo laranja) se movendo, com RASTRO (breadcrumb) em ciano.
 *   - Seta na borda quando o lider sai da tela + linha/distancia ate ele.
 *   - Painel: LINK, LIDER fix, distancia, rumo, satelites, velocidade, pacotes.
 * Tudo com dados FALSOS (animacao) so pra visualizar o layout no CYD real.
 * Desenho num sprite offscreen -> sem flicker.
 */
#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>
#include <math.h>

static LGFX tft;

// ---- layout (320x240 paisagem) ----
const int SCR_W = 320, SCR_H = 240;
const int PANEL_W = 72;           // painel mais estreito -> mapa maior
const int MW = SCR_W - PANEL_W;   // largura do mapa = 210
const int MH = SCR_H;             // 240
const int CX = MW / 2;            // centro do mapa (eu)
const int CY = MH / 2;
const int PX = SCR_W - PANEL_W;   // inicio do painel (x=210)

LGFX_Sprite canvas(&tft);         // buffer do mapa

// ---- cores ----
#define C_BG    0x0841
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GREEN 0x07E0
#define C_CYAN  0x07FF
#define C_ORANG 0xFD20
#define C_YEL   0xFFE0
#define C_RED   0xF800
#define C_GREY  0x8410
#define C_DGREY 0x39E7

const float MPP = 2.0f;           // metros por pixel (zoom)

// ---- estado simulado (metros, referencial absoluto) ----
float meX = 0, meY = 0, heading = 0;   // minha posicao e rumo (graus)
float t = 0;                            // tempo de simulacao
uint32_t pkt = 0;

struct Pt { float x, y; };
const int TRAIL_MAX = 400;
Pt trail[TRAIL_MAX];
int trailN = 0, trailHead = 0;
float lastTX = 1e9, lastTY = 1e9;

void addTrail(float x, float y) {
  float dx = x - lastTX, dy = y - lastTY;
  if (dx*dx + dy*dy < 9.0f) return;    // move >3m
  trail[trailHead] = {x, y};
  trailHead = (trailHead + 1) % TRAIL_MAX;
  if (trailN < TRAIL_MAX) trailN++;
  lastTX = x; lastTY = y;
}

// converte ponto absoluto (metros) -> tela, com EU no centro e heading-up
void worldToScreen(float ax, float ay, int &sx, int &sy) {
  float dx = ax - meX;              // leste
  float dy = ay - meY;              // norte
  float h = heading * (float)M_PI / 180.0f;
  float up    =  dy * cosf(h) + dx * sinf(h);
  float right =  dx * cosf(h) - dy * sinf(h);
  sx = CX + (int)(right / MPP);
  sy = CY - (int)(up    / MPP);
}

void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BLACK);

  canvas.setColorDepth(8);        // 8 bits -> ~60KB (cabe na RAM do ESP32)
  if (!canvas.createSprite(MW, MH)) {
    tft.setTextColor(C_RED); tft.setCursor(10,10); tft.print("sem memoria p/ sprite");
  }
  drawPanelStatic();
}

// ---- painel (lado direito) ----
void drawPanelStatic() {
  tft.fillRect(PX, 0, PANEL_W, SCR_H, C_BLACK);
  tft.drawFastVLine(PX, 0, SCR_H, C_DGREY);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setTextSize(1);
  tft.setCursor(PX + 4, 6); tft.print("DEMO");
}

void panelVal(int y, const char* label, const char* val, uint16_t col) {
  tft.setTextSize(1); tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(PX + 4, y); tft.print(label);
  tft.setTextSize(2); tft.setTextColor(col, C_BLACK);
  tft.setCursor(PX + 4, y + 10); tft.printf("%-5s", val);
}

void loop() {
  // ---- avanca a simulacao ----
  t += 0.05f;
  heading = 40.0f * sinf(t * 0.1f);            // rumo balanca +-40 graus
  float h = heading * (float)M_PI / 180.0f;
  float v = 8.0f * 0.05f;                        // eu ando pra frente
  meX += v * sinf(h);
  meY += v * cosf(h);

  // lider: a frente de mim, distancia varia (as vezes sai da tela)
  float lead = 120.0f + 260.0f * (0.5f + 0.5f * sinf(t * 0.15f));
  float lateral = 70.0f * sinf(t * 0.30f);
  float lx = meX + lead * sinf(h) + lateral * cosf(h);
  float ly = meY + lead * cosf(h) - lateral * sinf(h);
  addTrail(lx, ly);
  pkt++;

  // ---- desenha o mapa no sprite ----
  canvas.fillScreen(C_BG);

  // rastro
  int psx = -9999, psy = -9999;
  for (int k = 0; k < trailN; k++) {
    int idx = (trailHead - trailN + k + TRAIL_MAX) % TRAIL_MAX;
    int sx, sy; worldToScreen(trail[idx].x, trail[idx].y, sx, sy);
    if (psx > -9999) canvas.drawLine(psx, psy, sx, sy, C_CYAN);
    canvas.fillCircle(sx, sy, 1, C_CYAN);
    psx = sx; psy = sy;
  }

  // lider
  int lsx, lsy; worldToScreen(lx, ly, lsx, lsy);
  bool vis = (lsx >= 0 && lsx < MW && lsy >= 0 && lsy < MH);
  if (vis) {
    canvas.drawLine(CX, CY, lsx, lsy, C_DGREY);
    canvas.fillCircle(lsx, lsy, 5, C_ORANG);
    canvas.drawCircle(lsx, lsy, 8, C_WHITE);
  } else {
    // seta na borda apontando pro lider
    float ax = lsx - CX, ay = lsy - CY, L = sqrtf(ax*ax + ay*ay);
    ax /= L; ay /= L;
    int ex = CX + (int)(ax * (CX - 14)); ex = constrain(ex, 12, MW - 12);
    int ey = CY + (int)(ay * (CY - 14)); ey = constrain(ey, 12, MH - 12);
    canvas.fillCircle(ex, ey, 6, C_ORANG);
  }

  // EU (centro)
  canvas.fillTriangle(CX, CY-10, CX-7, CY+8, CX+7, CY+8, C_GREEN);
  canvas.drawTriangle(CX, CY-10, CX-7, CY+8, CX+7, CY+8, C_WHITE);

  // indicador de Norte (gira)
  float ux = -sinf(h), uy = cosf(h);
  canvas.drawLine(18, 20, 18 + (int)(ux*12), 20 - (int)(uy*12), C_RED);
  canvas.setTextColor(C_RED); canvas.setTextSize(1);
  canvas.setCursor(18 + (int)(ux*15) - 2, 20 - (int)(uy*15) - 3); canvas.print("N");

  canvas.pushSprite(0, 0);

  // ---- painel ----
  float dx = lx - meX, dy = ly - meY;
  float dist = sqrtf(dx*dx + dy*dy);
  float brg = atan2f(dx, dy) * 180.0f / (float)M_PI; if (brg < 0) brg += 360;
  char b[16];
  panelVal(28,  "LINK",   "OK",  C_GREEN);
  panelVal(60,  "LIDER",  "SIM", C_GREEN);
  snprintf(b, sizeof(b), "%d m", (int)dist);      panelVal(92,  "DIST", b, C_YEL);
  snprintf(b, sizeof(b), "%d", (int)brg);         panelVal(124, "RUMO", b, C_CYAN);
  snprintf(b, sizeof(b), "%d/%d", 9, 8);          panelVal(156, "SAT", b, C_WHITE);
  snprintf(b, sizeof(b), "%.0f", 8.0f*3.6f);      panelVal(188, "VEL", b, C_WHITE);
  snprintf(b, sizeof(b), "%lu", (unsigned long)pkt); panelVal(212, "PACOTES", b, C_DGREY);

  delay(40);
}
