#pragma once
// ============================================================================
//  MAPA - o desenho do mundo: fundo do cartao, rota do lider, meu rastro, carros
// ============================================================================
//
// O que veio do grupo_ws (telas S3), e por que cada coisa existe:
//   ESTRADA, nao linha: contorno grosso escuro + nucleo por cima. E o que faz a
//   mesma cor sobreviver a fundo claro e escuro.
//   TRES ESTADOS DO TRAJETO com ESPESSURA diferente, nao so cor (sol, daltonismo):
//     falta andar (roxo)      15/9    <- o mais grosso: e para onde voce vai
//     ja percorrido (azul)    11/5    <- memoria, nao instrucao
//     trecho em alerta (verm) 17/9    <- o mais grosso de todos
//   VAO VIRA PONTE TRACEJADA: salto de numero (rota_core) ou mais de 40 m entre
//   pontos = perda de sinal. Linha cheia mentiria; nao ligar sumiria o trajeto.
//   ROTULOS QUE NAO SE EMPILHAM e MARCADORES POR FORMA (lider = seta, seguidor = disco).
//
// MAPA ORIENTADO PELO RUMO (heading-up), pedido em campo em 2026-08-24. O rumo
// persegue o alvo com filtro (0,4 por quadro) para o mapa nao chicotear.
//
// NOVO NESTE FIRMWARE: os outros carros sao desenhados na POSICAO ESTIMADA
// (core/frota.h): a recebida, avancada pelo rumo e velocidade ate 12 s. Com um
// pacote a cada 6 s, sem isso o marcador pulava. Depois de 30 s sem pacote o
// marcador fica VAZADO (so contorno) e a faixa diz SEM SINAL.

#include "ui.h"
#include "mapa_fundo.h"
#include "fila.h"
#include "../estado.h"
#include "../../core/geo.h"

// ---------------------------------------------------------------- projecao

inline void mapaOrientaPeloRumo()
{
  float d = geoDeltaRumo(g_mapaRotG, g_eu.rumo);
  g_mapaRotG += d * 0.4f;
  while (g_mapaRotG >= 360.0f) g_mapaRotG -= 360.0f;
  while (g_mapaRotG < 0.0f)    g_mapaRotG += 360.0f;
  const float r = g_mapaRotG * 0.017453292f;
  g_rotSen = sinf(r); g_rotCos = cosf(r);
}

// Equirretangular centrada em MIM, girada em -rumo (a frente do carro sobe).
inline void paraTela(double lat, double lon, int cx, int cy, double mPorPx, int& x, int& y)
{
  const double r = M_PI / 180.0;
  double dx = (lon - g_eu.lon) * r * GEO_R_TERRA * cos(g_eu.lat * r);
  double dy = (lat - g_eu.lat) * r * GEO_R_TERRA;
  double rx = dx * g_rotCos - dy * g_rotSen;
  double ry = dx * g_rotSen + dy * g_rotCos;
  x = cx + (int)(rx / mPorPx);
  y = cy - (int)(ry / mPorPx);
}

// ---------------------------------------------------------------- tracos
// Engrossa NA PERPENDICULAR da reta: metade das chamadas da versao que
// engrossava nos dois eixos, e a espessura fica regular.
template <typename G>
void trecho(G& g, int x0, int y0, int x1, int y1, uint16_t cCont, uint16_t cNucleo, int wCont, int wNucleo)
{
  int dx = x1 - x0, dy = y1 - y0;
  float L = sqrtf((float)(dx * dx + dy * dy));
  if (L < 0.5f) L = 0.5f;
  float nx = -dy / L, ny = dx / L;
  for (int d = -wCont / 2; d <= wCont / 2; d++)
    g.drawLine(x0 + (int)(nx * d), y0 + (int)(ny * d), x1 + (int)(nx * d), y1 + (int)(ny * d), cCont);
  for (int d = -wNucleo / 2; d <= wNucleo / 2; d++)
    g.drawLine(x0 + (int)(nx * d), y0 + (int)(ny * d), x1 + (int)(nx * d), y1 + (int)(ny * d), cNucleo);
}

template <typename G>
void tracejado(G& g, int x0, int y0, int x1, int y1, uint16_t cor)
{
  int dx = x1 - x0, dy = y1 - y0;
  int n = (int)(sqrtf((float)(dx * dx + dy * dy)) / 12.0f);
  if (n < 1) n = 1;
  for (int i = 0; i < n; i += 2) {
    int ax = x0 + dx * i / n,       ay = y0 + dy * i / n;
    int bx = x0 + dx * (i + 1) / n, by = y0 + dy * (i + 1) / n;
    g.drawLine(ax, ay, bx, by, cor);
    g.drawLine(ax, ay + 1, bx, by + 1, cor);
  }
}

// Seta que aponta o RUMO. O entalhe atras e o que faz a frente ser inconfundivel.
// 'vazada' = so contorno: carro sem sinal ha mais de FROTA_SINAL_VELHO_MS.
template <typename G>
void marcaSetaRumo(G& g, int x, int y, uint16_t cor, int r, float rumoGraus, bool vazada = false)
{
  float a = rumoGraus * 0.017453292f;
  float sa = sinf(a), ca = cosf(a);
  auto P = [&](float pl, float pf, int& ox, int& oy) {
    ox = x + (int)lroundf(pl * ca + pf * sa);
    oy = y + (int)lroundf(pl * sa - pf * ca);
  };
  auto seta = [&](float rr, uint16_t c) {
    int tx, ty, lx, ly, rx, ry, nx, ny;
    P(0, rr * 1.15f, tx, ty); P(-rr * 0.85f, -rr * 0.85f, lx, ly);
    P(rr * 0.85f, -rr * 0.85f, rx, ry); P(0, -rr * 0.35f, nx, ny);
    g.fillTriangle(tx, ty, lx, ly, nx, ny, c);
    g.fillTriangle(tx, ty, nx, ny, rx, ry, c);
  };
  seta((float)r + 3.0f, C_CASING);
  seta((float)r, cor);
  if (vazada) seta((float)r - 4.0f, C_BG);
}

template <typename G>
void marcaDisco(G& g, int x, int y, uint16_t cor, int r, int slot, bool vazada = false)
{
  g.fillCircle(x, y, r + 3, C_CASING);
  g.fillCircle(x, y, r, cor);
  if (vazada) g.fillCircle(x, y, r - 4, C_BG);
  if (slot >= 0) {
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(vazada ? cor : C_CASING);
    char s[4]; snprintf(s, sizeof(s), "%d", slot);
    g.drawString(s, x, y + 1);
    g.setFont(&fonts::Font0);
  }
}

// ---------------------------------------------------------------- o quadro
template <typename G>
void mapaDesenha(G& g, int w, int h, double mPorPx, uint32_t agoraMs)
{
  const int cx = w / 2, cy = h / 2;
  mapaOrientaPeloRumo();
  g.fillRect(0, 0, w, h, C_BG);

  // O fundo do cartao entra AQUI, antes de tudo que vem da rede. Sem cartao,
  // fundoDesenha devolve 0 e o resto continua identico.
  if (g_eu.fix) fundoDesenha(g, g_fundo, w, h, mPorPx, g_eu.lat, g_eu.lon);

  if (!g_eu.fix) {
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold24pt7b);
    g.setTextColor(C_WARN);
    g.drawString("PROCURANDO GPS", cx, cy);
    g.setFont(&fonts::Font0);
    return;
  }

  // aneis de distancia e o "N" que passeia pelo anel (a bussola da tela)
  for (int r = 90; r < (h / 2 + 120); r += 90) g.drawCircle(cx, cy, r, C_SURF);
  {
    const int nx = cx - (int)(g_rotSen * 180.0f), ny = cy - (int)(g_rotCos * 180.0f);
    g.fillCircle(nx, ny, 17, C_SURF); g.drawCircle(nx, ny, 17, C_LINE);
    g.setTextDatum(middle_center); g.setFont(&fonts::FreeSansBold12pt7b); g.setTextColor(C_INK2);
    g.drawString("N", nx, ny + 1);
  }

  // onde eu estou no meu caminho, e o trecho em ALERTA: entre mim e o carro que
  // pediu socorro, pintado de vermelho no meu rastro
  int meu = trajetoMaisPerto(g_traj, g_eu.lat, g_eu.lon);
  int aLo = -1, aHi = -2;
  for (int k = 1; k < FROTA_MAX_CARROS; k++) {
    const Carro& c = g_frota.c[k];
    if (!c.ativo || !c.alerta || !c.fix || !c.temPos) continue;
    int pi = trajetoMaisPerto(g_traj, c.lat, c.lon);
    if (pi >= 0 && meu >= 0) { aLo = min(meu, pi); aHi = max(meu, pi); }
    break;
  }

  uint32_t tT = micros();

  // O CAMINHO DO LIDER, roxo, do ponto onde EU estou para a frente. Desenhado
  // ANTES do meu rastro para o azul ficar por cima onde os dois se sobrepoem.
  if (g_rotaC.n >= 2) {
    int meuIdx; float fora;
    if (rotaProjeta(g_eu.lat, g_eu.lon, meuIdx, fora)) {
      // ALERTA A FRENTE: quem pediu socorro esta adiante de mim no caminho do
      // lider (o proprio lider, ou outro seguidor). O vermelho no MEU rastro so
      // cobre quem esta atras; sem isto o trecho ate ele nao acendia (ja era
      // assim no v101). Pinta de vermelho, na rota, de mim ate ele.
      int alIdx = -1;
      for (int k = 1; k < FROTA_MAX_CARROS; k++) {
        const Carro& c = g_frota.c[k];
        if (!c.ativo || !c.alerta || !c.fix || !c.temPos) continue;
        int ai; float af;
        if (rotaProjeta(c.lat, c.lon, ai, af) && af < FORA_DA_TRILHA_M && ai > meuIdx) alIdx = ai;
        break;
      }
      int rx = -1, ry = 0; double rlat = 0, rlon = 0; bool rAnt = false;
      bool vaoPend = false;
      for (int i = meuIdx; i < g_rotaC.n; i++) {
        const RotaPonto& p = g_rotaC.p[i];
        int x, y; paraTela(p.lat, p.lon, cx, cy, mPorPx, x, y);
        bool ultimo = (i == g_rotaC.n - 1);
        if (rotaVaoAntes(g_rotaC, i)) vaoPend = true;
        if (rx >= 0 && !ultimo && abs(x - rx) < 3 && abs(y - ry) < 3) continue;
        bool vis = (x > -30 && x < w + 30 && y > -30 && y < h + 30);
        if (vis && rx >= 0) {
          bool vao = vaoPend || (rAnt && geoHaversine(rlat, rlon, p.lat, p.lon) > 40.0);
          if (vao) tracejado(g, rx, ry, x, y, C_VAO);
          else if (i <= alIdx) trecho(g, rx, ry, x, y, C_CASING, C_RED, 17, 9);
          else     trecho(g, rx, ry, x, y, C_ROTA_C, C_ROTA, 15, 9);
        }
        vaoPend = false;
        rx = vis ? x : -1; ry = y; rlat = p.lat; rlon = p.lon; rAnt = true;
      }
    }
  }
  // MEU RASTRO, azul (e o trecho de alerta em vermelho). Pula pontos que caem no
  // mesmo pixel: o custo passa a depender da TELA, nao do historico.
  int px = -1, py = 0; double plat = 0, plon = 0; bool temAnt = false;
  for (int i = 0; i < g_traj.n; i++) {
    const TrajPonto& p = trajetoEm(g_traj, i);
    int x, y; paraTela(p.lat, p.lon, cx, cy, mPorPx, x, y);
    bool ultimo = (i == g_traj.n - 1);
    if (px >= 0 && !ultimo && abs(x - px) < 3 && abs(y - py) < 3) continue;
    bool vis = (x > -30 && x < w + 30 && y > -30 && y < h + 30);
    if (vis && px >= 0) {
      bool vao = temAnt && geoHaversine(plat, plon, p.lat, p.lon) > 40.0;
      if (vao)                       tracejado(g, px, py, x, y, C_VAO);
      else if (i > aLo && i <= aHi)  trecho(g, px, py, x, y, C_CASING, C_RED, 17, 9);
      else                           trecho(g, px, py, x, y, C_RASTRO_C, C_RASTRO, 11, 5);
    }
    px = vis ? x : -1; py = y; plat = p.lat; plon = p.lon; temAnt = true;
  }
  g_usTraj = micros() - tT;
  uint32_t tC = micros();

  // OS OUTROS CARROS na posicao estimada, com rotulo que nao empilha
  int lx[FROTA_MAX_CARROS], ly[FROTA_MAX_CARROS], nL = 0;
  for (int k = 1; k < FROTA_MAX_CARROS; k++) {
    const Carro& c = g_frota.c[k];
    double la, lo; bool vazada;
    if (!frotaParaDesenhar(c, agoraMs, la, lo, vazada)) continue;   // a decisao mora no core
    int x, y; paraTela(la, lo, cx, cy, mPorPx, x, y);
    if (x < -30 || x > w + 30 || y < -30 || y > h + 30) continue;
    uint16_t cor = c.alerta ? C_RED : CORES_MAPA[c.cor % N_CORES];
    // rumo RELATIVO ao mapa girado: a seta aponta para onde o carro vai no MUNDO
    if (c.lider) marcaSetaRumo(g, x, y, cor, 15, c.rumo - g_mapaRotG, vazada);
    else         marcaDisco(g, x, y, cor, 13, c.slot, vazada);
    int ax = x + 20, ay = y - 10;
    for (int t = 0; t < FROTA_MAX_CARROS; t++) {
      bool bateu = false;
      for (int i = 0; i < nL; i++)
        if (abs(lx[i] - ax) < 130 && abs(ly[i] - ay) < 22) { ay = ly[i] + 24; bateu = true; break; }
      if (!bateu) break;
    }
    if (nL < FROTA_MAX_CARROS) { lx[nL] = ax; ly[nL] = ay; nL++; }
    g.setTextDatum(top_left);
    g.setFont(&fonts::FreeSans12pt7b);
    g.setTextColor(C_CASING);
    g.drawString(c.nome, ax + 1, ay + 1);     // halo = cor do contorno
    g.setTextColor(vazada ? C_INK3 : cor);
    g.drawString(c.nome, ax, ay);
  }

  // eu, sempre no centro, seta essencialmente para CIMA (o filtro persegue o alvo)
  marcaSetaRumo(g, cx, cy, CORES_MAPA[g_frota.c[0].cor % N_CORES], 18, g_eu.rumo - g_mapaRotG);
  g_usCarros = micros() - tC;

  // escala, centrada embaixo
  int esc = (int)(100.0 / mPorPx);
  if (esc > 20 && esc < w - 60) {
    int x0 = w / 2 - esc / 2;
    g.drawFastHLine(x0, h - 24, esc, C_INK2);
    g.drawFastVLine(x0, h - 30, 12, C_INK2);
    g.drawFastVLine(x0 + esc, h - 30, 12, C_INK2);
    g.setTextDatum(bottom_center); g.setFont(&fonts::FreeSans9pt7b); g.setTextColor(C_INK2);
    g.drawString("100 m", w / 2, h - 32);
  }
  g.setFont(&fonts::Font0);
}
