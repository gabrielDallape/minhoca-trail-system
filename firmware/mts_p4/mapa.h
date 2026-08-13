// O MAPA.
//
// Desenha o mundo (mundo.h) na tela inteira. Hoje o fundo e liso; quando o cartao
// entrar, os tiles vao por baixo e NADA MAIS muda aqui - o trajeto e os carros ja
// desenham por cima.
//
// DUAS REGRAS QUE VEM DA PESQUISA, e nao do gosto:
//
// 1. CONTORNO EM TUDO (casing). Cada linha e cada marcador leva uma borda escura
//    por baixo. E o que permite a mesma cor funcionar sobre fundo claro e escuro -
//    sem isso, paleta unica para os dois temas e matematicamente impossivel acima
//    de 3,94:1. E a tecnica do nav_arrow+stroke do OsmAnd e das vias de todo
//    estilo de mapa (casing largo escuro + nucleo claro por cima).
//
// 2. NORTH-UP, so o icone gira. Girar tiles por software mata o FPS e o
//    acelerador do P4 so rotaciona 90 graus.
#pragma once
#include "ui.h"
#include "mundo.h"

#define MAPA_M_POR_PX_PADRAO  0.9

// linha com contorno: primeiro a borda grossa escura, depois o nucleo
template <typename G>
void linhaContornada(G& g, int x0, int y0, int x1, int y1, uint16_t cor, int esp)
{
  for (int d = -(esp + 1); d <= (esp + 1); d++) {
    g.drawLine(x0 + d, y0, x1 + d, y1, C_CASING);
    g.drawLine(x0, y0 + d, x1, y1 + d, C_CASING);
  }
  for (int d = -esp / 2; d <= esp / 2; d++) {
    g.drawLine(x0 + d, y0, x1 + d, y1, cor);
    g.drawLine(x0, y0 + d, x1, y1 + d, cor);
  }
}

// marcador de carro: contorno preto, preenchimento na cor, e o numero do slot -
// acima de ~8 carros nao se acrescenta cor, se usa numero (nenhuma fonte manda
// distinguir 25 categorias por cor; a recomendacao e rotulo direto)
template <typename G>
void marcadorCarro(G& g, int x, int y, uint16_t cor, int slot, bool lider, bool alerta)
{
  int r = lider ? 17 : 14;
  g.fillCircle(x, y, r + 3, C_CASING);
  g.fillCircle(x, y, r, alerta ? C_RED : cor);
  if (lider) {                       // o lider ganha um anel, nao so tamanho:
    g.drawCircle(x, y, r + 5, cor);  // codificacao redundante, para sobreviver
    g.drawCircle(x, y, r + 6, cor);  // ao sol e ao daltonismo
  }
  if (slot > 0) {
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(C_CASING);
    char s[4]; snprintf(s, sizeof(s), "%d", slot);
    g.drawString(s, x, y + 1);
    g.setFont(&fonts::Font0);
  }
}

// Desenha o mapa inteiro na area dada. mPorPx = metros por pixel (zoom).
template <typename G>
void mapaDesenha(G& g, int x0, int y0, int w, int h, double mPorPx)
{
  const int cx = x0 + w / 2, cy = y0 + h / 2;

  // fundo. Quando o cartao entrar, aqui vao os tiles - e so isto muda.
  g.fillRect(x0, y0, w, h, C_BG);
#if !MTS_TEM_SD
  // enquanto nao ha tiles, uma grade discreta da nocao de movimento e escala
  for (int gx = cx % 120; gx < w; gx += 120) g.drawFastVLine(x0 + gx, y0, h, C_SURF);
  for (int gy = cy % 120; gy < h; gy += 120) g.drawFastHLine(x0, y0 + gy, w, C_SURF);
#endif

  // trajeto ja percorrido: do mais antigo para o mais novo
  int px = 0, py = 0;
  for (int i = 0; i < g_trajN; i++) {
    const Ponto& p = trajetoEm(i);
    int x, y; paraTela(p.lat, p.lon, cx, cy, mPorPx, x, y);
    if (i > 0 && (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h))
      linhaContornada(g, px, py, x, y, CORES_MAPA[4], 3);   // ciano: ja andei
    px = x; py = y;
  }

  // os outros carros
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
    if (!g_carros[i].ativo || !g_carros[i].fix) continue;
    int x, y; paraTela(g_carros[i].lat, g_carros[i].lon, cx, cy, mPorPx, x, y);
    if (x < x0 - 40 || x > x0 + w + 40 || y < y0 - 40 || y > y0 + h + 40) continue;
    marcadorCarro(g, x, y, CORES_MAPA[g_carros[i].cor % N_CORES], i,
                  g_carros[i].lider, g_carros[i].alerta);
  }

  // eu, sempre no centro
  marcadorCarro(g, cx, cy, CORES_MAPA[g_carros[0].cor % N_CORES], 0,
                g_carros[0].lider, false);
  g.drawCircle(cx, cy, 26, C_INK3);

  // escala: sem ela o mapa nao diz se aquilo sao 50 m ou 5 km
  int esc = (int)(100.0 / mPorPx);
  g.drawFastHLine(x0 + 20, y0 + h - 26, esc, C_INK2);
  g.drawFastVLine(x0 + 20, y0 + h - 32, 12, C_INK2);
  g.drawFastVLine(x0 + 20 + esc, y0 + h - 32, 12, C_INK2);
  g.setTextDatum(bottom_left);
  g.setFont(&fonts::FreeSans9pt7b);
  g.setTextColor(C_INK2);
  g.drawString("100 m", x0 + 24, y0 + h - 32);
  g.setFont(&fonts::Font0);
}
