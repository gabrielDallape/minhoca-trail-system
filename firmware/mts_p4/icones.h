// ICONES EM VETOR, desenhados com as primitivas do LovyanGFX.
//
// POR QUE NAO BITMAP: um jogo de icones em PNG para duas resolucoes e dois temas
// sao quatro versoes de cada desenho, e nenhuma delas escala. Aqui o icone recebe
// o TAMANHO e a COR na chamada, entao o mesmo codigo serve pastilha de 26 px e
// cartao de 46 px, tema dia e tema noite, sem nenhum arquivo a mais na flash.
//
// A ESPESSURA ACOMPANHA O TAMANHO. Traco fixo de 2 px vira fio de cabelo num
// icone de 46 e borrao num de 18. Aqui e s/11, com minimo de 2 - proporcao tirada
// do desenho do prototipo, onde o traco e 2,2 num quadro de 24.
//
// Todo icone e desenhado CENTRADO em (cx, cy) e cabe num quadrado de lado s.
// Assim quem chama posiciona pelo centro e nunca precisa saber a forma.
#pragma once
#include <math.h>

// drawWideLine recebe RAIO, nao espessura - passar a espessura direto deixa todo
// traco com o dobro da largura, e o icone vira mancha.
template <typename G>
inline void icTraco(G& g, float x0, float y0, float x1, float y1, float esp, uint16_t cor)
{
  g.drawWideLine(x0, y0, x1, y1, esp * 0.5f, cor);
}

inline float icEsp(int s) { float e = s / 11.0f; return e < 2.0f ? 2.0f : e; }

// ---------------------------------------------------------------- estado
// Antena com ondas: o radio. As ondas sao arcos concentricos abrindo para os dois
// lados, e nao circulos inteiros - circulo fechado leria como alvo, nao como sinal.
template <typename G>
void icAntena(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s);
  g.fillCircle(cx, cy, s * 0.085f + 1, cor);
  for (int i = 1; i <= 2; i++) {
    int r = (int)(s * (0.17f + i * 0.15f));
    for (float d = -e * 0.5f; d <= e * 0.5f; d += 0.8f) {
      g.drawArc(cx, cy, r + (int)d, r + (int)d, 200, 340, cor);   // onda de cima
      g.drawArc(cx, cy, r + (int)d, r + (int)d,  20, 160, cor);   // onda de baixo
    }
  }
}

// Satelite: a antena parabolica de cabeca para baixo, com o feixe saindo.
template <typename G>
void icSatelite(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), h = s * 0.5f;
  g.fillCircle(cx - (int)(h * 0.52f), cy + (int)(h * 0.52f), (int)(s * 0.13f), cor);
  for (int i = 1; i <= 2; i++) {
    int r = (int)(s * (0.24f + i * 0.17f));
    for (float d = -e * 0.5f; d <= e * 0.5f; d += 0.8f)
      g.drawArc(cx - (int)(h * 0.52f), cy + (int)(h * 0.52f), r + (int)d, r + (int)d, 265, 355, cor);
  }
  // o painel: um losango inclinado
  const float q = s * 0.19f, ox = cx + h * 0.42f, oy = cy - h * 0.46f;
  icTraco(g, ox - q, oy, ox, oy - q, e, cor);
  icTraco(g, ox, oy - q, ox + q, oy, e, cor);
  icTraco(g, ox + q, oy, ox, oy + q, e, cor);
  icTraco(g, ox, oy + q, ox - q, oy, e, cor);
}

// Mapa dobrado, com um risco por cima quando o cartao nao esta la. O risco e o
// que transforma "mapa" em "sem mapa" sem precisar de segunda cor nem de texto.
template <typename G>
void icMapa(G& g, int cx, int cy, int s, uint16_t cor, bool riscado)
{
  const float e = icEsp(s), h = s * 0.5f;
  const float x0 = cx - h, x1 = cx - h * 0.33f, x2 = cx + h * 0.33f, x3 = cx + h;
  icTraco(g, x0, cy - h * 0.55f, x1, cy - h * 0.85f, e, cor);
  icTraco(g, x1, cy - h * 0.85f, x2, cy - h * 0.55f, e, cor);
  icTraco(g, x2, cy - h * 0.55f, x3, cy - h * 0.85f, e, cor);
  icTraco(g, x0, cy + h * 0.85f, x1, cy + h * 0.55f, e, cor);
  icTraco(g, x1, cy + h * 0.55f, x2, cy + h * 0.85f, e, cor);
  icTraco(g, x2, cy + h * 0.85f, x3, cy + h * 0.55f, e, cor);
  icTraco(g, x0, cy - h * 0.55f, x0, cy + h * 0.85f, e, cor);
  icTraco(g, x3, cy - h * 0.85f, x3, cy + h * 0.55f, e, cor);
  icTraco(g, x1, cy - h * 0.85f, x1, cy + h * 0.55f, e, cor);
  icTraco(g, x2, cy - h * 0.55f, x2, cy + h * 0.85f, e, cor);
  if (riscado) icTraco(g, cx - h, cy + h, cx + h, cy - h, e * 1.35f, cor);
}

// ---------------------------------------------------------------- acao
template <typename G>
void icEngrenagem(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), r = s * 0.27f;
  for (float d = -e * 0.5f; d <= e * 0.5f; d += 0.8f) g.drawCircle(cx, cy, (int)(r + d), cor);
  for (int i = 0; i < 8; i++) {
    float a = i * (float)M_PI / 4.0f;
    icTraco(g, cx + cosf(a) * r * 1.25f, cy + sinf(a) * r * 1.25f,
               cx + cosf(a) * s * 0.48f, cy + sinf(a) * s * 0.48f, e, cor);
  }
}

template <typename G>
void icLupa(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), r = s * 0.29f;
  const float ox = cx - s * 0.08f, oy = cy - s * 0.08f;
  for (float d = -e * 0.5f; d <= e * 0.5f; d += 0.8f) g.drawCircle((int)ox, (int)oy, (int)(r + d), cor);
  icTraco(g, ox + r * 0.72f, oy + r * 0.72f, cx + s * 0.42f, cy + s * 0.42f, e * 1.15f, cor);
}

template <typename G>
void icMais(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), h = s * 0.32f;
  icTraco(g, cx, cy - h, cx, cy + h, e, cor);
  icTraco(g, cx - h, cy, cx + h, cy, e, cor);
}

template <typename G>
void icX(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), h = s * 0.30f;
  icTraco(g, cx - h, cy - h, cx + h, cy + h, e, cor);
  icTraco(g, cx + h, cy - h, cx - h, cy + h, e, cor);
}

// Chevron. dir = +1 aponta para a direita, -1 para a esquerda.
template <typename G>
void icSeta(G& g, int cx, int cy, int s, uint16_t cor, int dir)
{
  const float e = icEsp(s), h = s * 0.28f, w = s * 0.20f * dir;
  icTraco(g, cx - w, cy - h, cx + w, cy, e, cor);
  icTraco(g, cx + w, cy, cx - w, cy + h, e, cor);
}

template <typename G>
void icLapis(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), h = s * 0.36f;
  icTraco(g, cx - h, cy + h, cx + h * 0.75f, cy - h * 0.75f, e * 1.6f, cor);
  icTraco(g, cx - h, cy + h, cx - h * 0.55f, cy + h * 0.62f, e, cor);   // a ponta
}

template <typename G>
void icInfo(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), r = s * 0.38f;
  for (float d = -e * 0.5f; d <= e * 0.5f; d += 0.8f) g.drawCircle(cx, cy, (int)(r + d), cor);
  g.fillCircle(cx, cy - (int)(s * 0.17f), (int)(e * 0.75f), cor);
  icTraco(g, cx, cy - s * 0.03f, cx, cy + s * 0.20f, e, cor);
}

// ---------------------------------------------------------------- grupo
// Comboio: a trilha com tres carros em fila. E o mesmo desenho da fita e da tela
// do comboio, reduzido - o simbolo e a coisa, nao uma metafora dela.
template <typename G>
void icComboio(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), h = s * 0.44f;
  icTraco(g, cx - h, cy, cx + h, cy, e * 0.85f, cor);
  g.fillCircle(cx - (int)(h * 0.66f), cy, (int)(s * 0.11f), cor);
  g.fillCircle(cx,                     cy, (int)(s * 0.11f), cor);
  g.fillCircle(cx + (int)(h * 0.66f),  cy, (int)(s * 0.11f), cor);
}

// Cadeado. aberto = a alca sai para o lado, que e o unico jeito de a diferenca
// ser visivel a 26 px - alca mais alta ou mais baixa nao se distingue.
template <typename G>
void icCadeado(G& g, int cx, int cy, int s, uint16_t cor, bool aberto)
{
  const float e = icEsp(s), w = s * 0.36f, hc = s * 0.26f;
  g.fillRoundRect(cx - (int)w, cy + (int)(s * 0.02f), (int)(w * 2), (int)(hc * 2),
                  (int)(s * 0.10f), cor);
  const float r = s * 0.22f, ax = aberto ? cx + s * 0.16f : cx;
  for (float d = -e * 0.5f; d <= e * 0.5f; d += 0.8f)
    g.drawArc((int)ax, cy - (int)(s * 0.06f), (int)(r + d), (int)(r + d), 180, 360, cor);
  icTraco(g, ax - r, cy - s * 0.06f, ax - r, cy + s * 0.02f, e, cor);
  if (!aberto) icTraco(g, ax + r, cy - s * 0.06f, ax + r, cy + s * 0.02f, e, cor);
}

// Vassoura. A palavra ja existe no vocabulario da trilha, entao o desenho e
// literal: cabo e crina. Nao precisa de legenda porque o grupo ja chama assim.
template <typename G>
void icVassoura(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s), h = s * 0.5f;
  icTraco(g, cx + h * 0.62f, cy - h * 0.92f, cx - h * 0.12f, cy - h * 0.10f, e, cor);
  const float bx = cx - h * 0.12f, by = cy - h * 0.10f, bw = h * 0.46f;
  icTraco(g, bx - bw, by + bw * 0.55f, bx + bw, by + bw * 0.55f, e, cor);
  icTraco(g, bx - bw, by + bw * 0.55f, bx - bw * 0.62f, cy + h * 0.88f, e, cor);
  icTraco(g, bx + bw, by + bw * 0.55f, bx + bw * 0.62f, cy + h * 0.88f, e, cor);
  for (int i = -1; i <= 1; i++)
    icTraco(g, bx + i * bw * 0.42f, by + bw * 0.75f, bx + i * bw * 0.52f, cy + h * 0.82f, e * 0.8f, cor);
}

// Triangulo de alerta. E o unico icone que aparece sozinho num botao, entao ele
// tem de ser reconhecivel sem nenhuma palavra ao lado.
template <typename G>
void icAlerta(G& g, int cx, int cy, int s, uint16_t cor)
{
  const float e = icEsp(s) * 1.1f, h = s * 0.46f;
  icTraco(g, cx, cy - h, cx + h * 1.05f, cy + h * 0.72f, e, cor);
  icTraco(g, cx + h * 1.05f, cy + h * 0.72f, cx - h * 1.05f, cy + h * 0.72f, e, cor);
  icTraco(g, cx - h * 1.05f, cy + h * 0.72f, cx, cy - h, e, cor);
  icTraco(g, cx, cy - h * 0.30f, cx, cy + h * 0.20f, e, cor);
  g.fillCircle(cx, cy + (int)(h * 0.46f), (int)(e * 0.72f), cor);
}
