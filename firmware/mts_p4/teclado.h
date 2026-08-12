// Teclado na tela, para digitar o nome do aparelho.
//
// Existe porque nao ha teclado fisico: o unico jeito de nomear o carro e o dedo.
// Teclas grandes de proposito - isto vai ser usado dentro de um veiculo, muitas
// vezes com a mao suja e o carro balancando.
#pragma once
#include "ui.h"

#define TECLADO_MAX 14

// Devolve true se confirmou, false se cancelou. 'texto' entra com o valor atual e
// sai com o digitado.
template <typename GFX>
bool tecladoTexto(GFX& g, const char* titulo, char* texto, size_t tamMax)
{
  static const char* LINHAS[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
  const int NL = 3;

  bool maiuscula = true;
  char buf[TECLADO_MAX + 1];
  strncpy(buf, texto, TECLADO_MAX); buf[TECLADO_MAX] = 0;
  size_t lim = (tamMax - 1 < TECLADO_MAX) ? tamMax - 1 : TECLADO_MAX;

  const int TW = 108, TH = 78, GAP = 10;
  const int baseY = 300;

  auto retTecla = [&](int li, int col) -> Ret {
    int n = strlen(LINHAS[li]);
    int larg = n * TW + (n - 1) * GAP;
    int x0 = (g.width() - larg) / 2;
    return Ret{ (int16_t)(x0 + col * (TW + GAP)), (int16_t)(baseY + li * (TH + GAP)), TW, TH };
  };

  Ret bShift = { 60, (int16_t)(baseY + 2 * (TH + GAP)), 150, TH };
  Ret bApaga = { (int16_t)(g.width() - 210), (int16_t)(baseY + 2 * (TH + GAP)), 150, TH };
  Ret bEspaco = { (int16_t)(g.width() / 2 - 200), (int16_t)(baseY + 3 * (TH + GAP)), 400, TH };
  Ret bCancel = { 60, (int16_t)(baseY + 3 * (TH + GAP)), 220, TH };
  Ret bOk = { (int16_t)(g.width() - 280), (int16_t)(baseY + 3 * (TH + GAP)), 220, TH };

  auto desenha = [&]() {
    g.fillScreen(C_BG);
    cabecalho(g, titulo, false);

    // caixa do texto
    g.fillRoundRect(g.width() / 2 - 400, 140, 800, 96, 12, C_SURF);
    g.drawRoundRect(g.width() / 2 - 400, 140, 800, 96, 12, C_ORANGE);
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold24pt7b);
    g.setTextColor(strlen(buf) ? C_INK : C_INK3);
    g.drawString(strlen(buf) ? buf : "sem nome", g.width() / 2, 188);
    g.setFont(&fonts::FreeSans9pt7b);
    g.setTextColor(C_INK3);
    g.drawString(String(strlen(buf)) + " / " + String((int)lim), g.width() / 2, 258);

    g.setFont(&fonts::FreeSansBold18pt7b);
    for (int li = 0; li < NL; li++) {
      int n = strlen(LINHAS[li]);
      for (int c = 0; c < n; c++) {
        Ret r = retTecla(li, c);
        g.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_SURF);
        g.setTextColor(C_INK);
        g.setTextDatum(middle_center);
        char s[2] = { maiuscula ? LINHAS[li][c] : (char)tolower(LINHAS[li][c]), 0 };
        g.drawString(s, r.x + r.w / 2, r.y + r.h / 2);
      }
    }
    botao(g, bShift,  maiuscula ? "abc" : "ABC", C_INK2, false);
    botao(g, bApaga,  "APAGA", C_INK2, false);
    botao(g, bEspaco, "ESPACO", C_INK2, false);
    botao(g, bCancel, "CANCELAR", C_INK3, false);
    botao(g, bOk,     "OK", C_ORANGE, true);
    g.setFont(&fonts::Font0);
  };

  desenha();

  while (true) {
    int16_t x, y;
    if (!esperaToque(g, x, y)) continue;

    if (dentro(bOk, x, y))     { strncpy(texto, buf, tamMax - 1); texto[tamMax - 1] = 0; return true; }
    if (dentro(bCancel, x, y)) return false;
    if (dentro(bShift, x, y))  { maiuscula = !maiuscula; desenha(); continue; }
    if (dentro(bApaga, x, y))  { size_t n = strlen(buf); if (n) buf[n - 1] = 0; desenha(); continue; }
    if (dentro(bEspaco, x, y)) { size_t n = strlen(buf); if (n && n < lim) { buf[n] = ' '; buf[n + 1] = 0; } desenha(); continue; }

    for (int li = 0; li < NL; li++) {
      int n = strlen(LINHAS[li]);
      for (int c = 0; c < n; c++) {
        if (dentro(retTecla(li, c), x, y)) {
          size_t k = strlen(buf);
          if (k < lim) {
            buf[k] = maiuscula ? LINHAS[li][c] : (char)tolower(LINHAS[li][c]);
            buf[k + 1] = 0;
            // como num celular: a primeira letra sai maiuscula, as seguintes nao
            if (k == 0) maiuscula = false;
          }
          desenha();
          li = NL; break;
        }
      }
    }
  }
}
