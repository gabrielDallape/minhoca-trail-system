// Peças visuais do MTS na tela P4 (1280x720).
//
// A paleta sai da PROPRIA arte do logo, amostrada com o gerador: o fundo quase
// preto, o laranja do sol e o bege das letras. Assim a abertura e as telas nao
// parecem dois aplicativos diferentes colados.
#pragma once
#include <stdint.h>

// ------------------------------------------------------------------ paleta
#define C_BG      0x0861   // (12,12,12)  fundo, igual ao da arte
#define C_SURF    0x18E3   // cartao/painel, um degrau acima do fundo
#define C_LINE    0x39E7   // divisorias
#define C_INK     0xFFFF   // texto principal
#define C_INK2    0x9CD3   // texto secundario
#define C_INK3    0x6B4D   // texto apagado
#define C_ORANGE  0xD243   // laranja do sol da arte
#define C_TAN     0xDE35   // bege das letras "MTS"
#define C_RED     0xC000
#define C_GREEN   0x2606

// --------------------------------------------------------------- geometria
struct Ret { int16_t x, y, w, h; };
inline bool dentro(const Ret& r, int16_t px, int16_t py) {
  return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// ----------------------------------------------------------------- widgets
// Botao "cheio" (acao principal) ou "vazado" (acao secundaria). O vazado nao e
// so estetica: numa tela de carro, dois botoes cheios lado a lado competem pela
// atencao e o motorista erra o alvo.
template <typename GFX>
void botao(GFX& g, const Ret& r, const char* rotulo, uint16_t cor, bool cheio, bool premido = false)
{
  const int raio = 14;
  if (cheio) {
    g.fillRoundRect(r.x, r.y, r.w, r.h, raio, premido ? C_TAN : cor);
    g.setTextColor(C_BG);
  } else {
    g.fillRoundRect(r.x, r.y, r.w, r.h, raio, premido ? C_SURF : C_BG);
    g.drawRoundRect(r.x, r.y, r.w, r.h, raio, cor);
    g.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, raio - 1, cor);
    g.setTextColor(cor);
  }
  g.setTextDatum(middle_center);
  g.setFont(&fonts::FreeSansBold18pt7b);
  g.drawString(rotulo, r.x + r.w / 2, r.y + r.h / 2);
  g.setFont(&fonts::Font0);
}

// Engrenagem desenhada em vetor: oito dentes e um furo. Vetor e nao imagem
// porque assim ela acompanha qualquer tamanho e qualquer cor de tema depois.
template <typename GFX>
void engrenagem(GFX& g, int cx, int cy, int raio, uint16_t cor)
{
  const int dentes = 8;
  for (int i = 0; i < dentes; i++) {
    float a = (float)i * 6.2831853f / dentes;
    int x = cx + (int)(cosf(a) * raio);
    int y = cy + (int)(sinf(a) * raio);
    g.fillCircle(x, y, raio / 3, cor);
  }
  g.fillCircle(cx, cy, (raio * 3) / 4, cor);
  g.fillCircle(cx, cy, raio / 3, C_BG);
}

// Cabecalho comum: marca a esquerda, engrenagem a direita (quando houver).
template <typename GFX>
Ret cabecalho(GFX& g, const char* titulo, bool comEngrenagem)
{
  g.fillRect(0, 0, g.width(), 92, C_BG);
  g.drawFastHLine(0, 92, g.width(), C_LINE);

  g.setTextDatum(middle_left);
  g.setFont(&fonts::FreeSansBold24pt7b);
  g.setTextColor(C_TAN);
  g.drawString("MTS", 40, 46);

  g.setFont(&fonts::FreeSans12pt7b);
  g.setTextColor(C_INK3);
  int lx = 40 + g.textWidth("MTS") + 18;
  g.drawFastVLine(lx - 9, 22, 48, C_LINE);
  g.drawString(titulo, lx, 48);
  g.setFont(&fonts::Font0);

  Ret eng = { (int16_t)(g.width() - 108), 14, 64, 64 };
  if (comEngrenagem) engrenagem(g, eng.x + 32, eng.y + 32, 22, C_INK2);
  return eng;
}

// ------------------------------------------------------------------- toque
// Espera o dedo SAIR (borda de subida) e devolve onde ele estava. Usar a saida e
// nao a entrada permite arrastar o dedo para fora do botao e desistir - que e o
// comportamento que todo mundo espera de um toque.
template <typename GFX>
bool esperaToque(GFX& g, int16_t& x, int16_t& y, uint32_t limiteMs = 0)
{
  uint32_t t0 = millis();
  int16_t ux = -1, uy = -1;
  bool tocou = false;
  while (true) {
    int16_t tx, ty;
    if (g.getTouch(&tx, &ty)) { ux = tx; uy = ty; tocou = true; }
    else if (tocou) { x = ux; y = uy; return true; }
    if (limiteMs && (millis() - t0) > limiteMs) return false;
    delay(8);
  }
}
