// Teclado na tela, para nomear o aparelho.
//
// REGRA DE DESEMPENHO desta tela, aprendida errando: NAO redesenhe tudo a cada
// tecla. A primeira versao limpava a tela inteira por letra (piscava preto); a
// segunda desenhava num sprite de tela cheia e empurrava 1,8 MB por letra (parecia
// transicao de slide). As duas erram no mesmo ponto - o que muda ao apertar uma
// letra e o CAMPO e a TECLA, nada mais.
//
// Entao: desenha tudo UMA vez, e depois so as regioes sujas.
//   - campo de texto -> sprite pequeno de 1280x104 (266 KB), empurrado inteiro
//   - tecla apertada -> dois retangulos, direto na tela
// Sem limpeza de tela cheia em lugar nenhum, entao nao ha o que piscar.
#pragma once
#include "ui.h"

#define TECLADO_MAX 24

// ------------------------------------------------------- teclado numerico
// Para o codigo do grupo, que funciona como senha. Cinco digitos, mesmo formato
// do grupo_ws das telas S3.
// Devolve true se confirmou. 'valor' entra vazio e sai com o digitado.
template <typename TFT>
bool tecladoNumero(TFT& tft, const char* sub, const char* dica, char* valor, int nDig)
{
  char buf[8] = {0};
  const int TW = 150, TH = 96, GAP = 14;
  const int gx = (tft.width() - (3 * TW + 2 * GAP)) / 2;
  const int gy = 250;

  auto rTecla = [&](int i) -> Ret {          // 0..8 = "1".."9", 9 = "0"
    int l = (i == 9) ? 3 : i / 3, c = (i == 9) ? 1 : i % 3;
    return Ret{ (int16_t)(gx + c * (TW + GAP)), (int16_t)(gy + l * (TH + GAP)), TW, TH };
  };
  Ret bApaga  = { (int16_t)(gx + 2 * (TW + GAP)), (int16_t)(gy + 3 * (TH + GAP)), TW, TH };
  Ret bVolta  = { M, (int16_t)(tft.height() - 108), 240, 88 };
  Ret bEntrar = { (int16_t)(tft.width() - M - 300), (int16_t)(tft.height() - 108), 300, 88 };

  LGFX_Sprite mostrador(&tft);
  mostrador.setPsram(true); mostrador.setColorDepth(16);
  mostrador.createSprite(tft.width(), 110);

  auto pintaMostrador = [&]() {
    mostrador.fillScreen(C_BG);
    int n = strlen(buf);
    const int cw = 92, cg = 16;
    int larg = nDig * cw + (nDig - 1) * cg;
    int x0 = (tft.width() - larg) / 2;
    for (int i = 0; i < nDig; i++) {
      int x = x0 + i * (cw + cg);
      bool tem = i < n;
      mostrador.fillRoundRect(x, 6, cw, 96, 12, C_SURF);
      mostrador.drawRoundRect(x, 6, cw, 96, 12, tem ? C_SUN : C_LINE);
      if (tem) {
        mostrador.setTextDatum(middle_center);
        mostrador.setFont(&fonts::FreeSansBold24pt7b);
        mostrador.setTextColor(C_INK);
        char s[2] = { buf[i], 0 };
        mostrador.drawString(s, x + cw / 2, 56);
      }
    }
    mostrador.setFont(&fonts::Font0);
    mostrador.pushSprite(0, 120);
  };
  auto pintaTecla = [&](const Ret& r, const char* s, bool aceso) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 14, aceso ? C_SUN : C_SURF2);
    tft.setTextDatum(middle_center);
    tft.setFont(&fonts::FreeSansBold24pt7b);
    tft.setTextColor(aceso ? C_BG : C_INK);
    tft.drawString(s, r.x + r.w / 2, r.y + r.h / 2);
    tft.setFont(&fonts::Font0);
  };

  tft.fillScreen(C_BG);
  cabecalho(tft, sub, false);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString(dica, tft.width() / 2, 96);
  for (int i = 0; i < 10; i++) { char s[2] = { (char)((i == 9) ? '0' : '1' + i), 0 }; pintaTecla(rTecla(i), s, false); }
  pintaTecla(bApaga, "<", false);
  botao(tft, bVolta, "< VOLTAR", "", C_INK2, false);
  botao(tft, bEntrar, "ENTRAR", "", C_SUN, true);
  pintaMostrador();

  while (true) {
    int16_t x, y;
    while (!tft.getTouch(&x, &y)) delay(6);
    int alvo = -1;
    for (int i = 0; i < 10; i++) if (dentro(rTecla(i), x, y)) { alvo = i; break; }
    bool ehApaga = dentro(bApaga, x, y);
    if (alvo >= 0) { char s[2] = { (char)((alvo == 9) ? '0' : '1' + alvo), 0 }; pintaTecla(rTecla(alvo), s, true); }
    else if (ehApaga) pintaTecla(bApaga, "<", true);

    int16_t ux = x, uy = y, tx, ty;
    while (tft.getTouch(&tx, &ty)) { ux = tx; uy = ty; delay(6); }

    if (alvo >= 0) {
      char s[2] = { (char)((alvo == 9) ? '0' : '1' + alvo), 0 };
      pintaTecla(rTecla(alvo), s, false);
      if (dentro(rTecla(alvo), ux, uy)) {
        int n = strlen(buf);
        if (n < nDig) { buf[n] = s[0]; buf[n + 1] = 0; pintaMostrador(); }
      }
      continue;
    }
    if (ehApaga) {
      pintaTecla(bApaga, "<", false);
      if (dentro(bApaga, ux, uy)) { int n = strlen(buf); if (n) { buf[n - 1] = 0; pintaMostrador(); } }
      continue;
    }
    if (dentro(bVolta, ux, uy))  { mostrador.deleteSprite(); return false; }
    if (dentro(bEntrar, ux, uy)) {
      if ((int)strlen(buf) == nDig) { mostrador.deleteSprite(); strcpy(valor, buf); return true; }
      // faltam digitos: pisca o mostrador em vez de nao fazer nada
      for (int k = 0; k < 2; k++) { tft.fillRect(0, 120, tft.width(), 110, C_BG); delay(70); pintaMostrador(); delay(70); }
    }
  }
}

template <typename TFT>
bool tecladoTexto(TFT& tft, const char* sub, char* texto, size_t tamMax)
{
  static const char* LINHAS[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
  const int TW = 112, TH = 82, GAP = 10;
  const int Y0 = 150;
  const int CAMPO_Y = 20, CAMPO_H = 104;
  const size_t lim = (tamMax - 1 < TECLADO_MAX) ? tamMax - 1 : TECLADO_MAX;

  bool maiuscula = true;
  char buf[TECLADO_MAX + 1];
  strncpy(buf, texto, TECLADO_MAX); buf[TECLADO_MAX] = 0;

  auto tecla = [&](int li, int col) -> Ret {
    int n = strlen(LINHAS[li]);
    int larg = n * TW + (n - 1) * GAP;
    int x0 = (li == 2) ? (M + 173 + GAP) : (tft.width() - larg) / 2;
    return Ret{ (int16_t)(x0 + col * (TW + GAP)), (int16_t)(Y0 + li * (TH + GAP)), TW, TH };
  };
  const int y3 = Y0 + 2 * (TH + GAP), y4 = Y0 + 3 * (TH + GAP);
  Ret bShift  = { M, (int16_t)y3, 173, TH };
  Ret bApaga  = { (int16_t)(tft.width() - M - 173), (int16_t)y3, 173, TH };
  Ret bCancel = { M, (int16_t)y4, 234, TH };
  Ret bEspaco = { (int16_t)(M + 234 + GAP), (int16_t)y4, (int16_t)(tft.width() - 2 * M - 2 * (234 + GAP)), TH };
  Ret bSalvar = { (int16_t)(tft.width() - M - 234), (int16_t)y4, 234, TH };

  // sprite pequeno so do campo: e a unica coisa que muda ao digitar
  LGFX_Sprite campo(&tft);
  campo.setPsram(true);
  campo.setColorDepth(16);
  campo.createSprite(tft.width(), CAMPO_H);

  auto pintaCampo = [&]() {
    campo.fillScreen(C_BG);
    const int cw = 600, cx = (tft.width() - cw) / 2, cy = 26 - CAMPO_Y;
    campo.fillRoundRect(cx, cy, cw, 92, 14, C_SURF);
    campo.drawRoundRect(cx, cy, cw, 92, 14, C_SUN);
    campo.setTextDatum(middle_center);
    campo.setFont(&fonts::FreeSansBold24pt7b);
    campo.setTextColor(strlen(buf) ? C_INK : C_INK3);
    campo.drawString(strlen(buf) ? buf : "sem nome", tft.width() / 2, cy + 46);
    // o contador vira aviso quando enche - senao o texto so para de aceitar
    // letra e o usuario nao entende por que
    bool cheio = strlen(buf) >= lim;
    campo.setTextDatum(middle_right);
    campo.setFont(&fonts::FreeSans9pt7b);
    campo.setTextColor(cheio ? C_SUN : C_INK3);
    char c[16]; snprintf(c, sizeof(c), "%d / %d", (int)strlen(buf), (int)lim);
    campo.drawString(c, tft.width() - M, cy + 46);
    campo.setFont(&fonts::Font0);
    campo.pushSprite(0, CAMPO_Y);
  };

  auto pintaTecla = [&](int li, int k, bool aceso) {
    Ret r = tecla(li, k);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 12, aceso ? C_SUN : C_SURF2);
    tft.setTextDatum(middle_center);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextColor(aceso ? C_BG : C_INK);
    char s[2] = { maiuscula ? LINHAS[li][k] : (char)tolower(LINHAS[li][k]), 0 };
    tft.drawString(s, r.x + r.w / 2, r.y + r.h / 2);
    tft.setFont(&fonts::Font0);
  };

  // ---- desenho completo, uma vez so, DIRETO na tela.
  // Nada de sprite de tela cheia aqui: copiar 1,8 MB da PSRAM para a PSRAM e
  // lento o bastante para VER pintando de cima para baixo - parecia transicao de
  // slide. Desenhar direto e mais rapido, e como isto acontece uma vez por tela
  // (nao por toque), nao ha o que piscar.
  tft.fillScreen(C_BG);
  cabecalho(tft, sub, false);
  for (int li = 0; li < 3; li++)
    for (int k = 0, n = strlen(LINHAS[li]); k < n; k++) pintaTecla(li, k, false);
  botao(tft, bShift, "ABC", "", C_INK2, false);
  botao(tft, bApaga, "APAGA", "", C_INK2, false);
  botao(tft, bEspaco, "ESPACO", "", C_INK2, false);
  botao(tft, bCancel, "CANCELAR", "", C_INK3, false);
  botao(tft, bSalvar, "SALVAR", "", C_SUN, true);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("o nome aparece para os outros carros do grupo", tft.width() / 2, 596);
  tft.setFont(&fonts::Font0);
  pintaCampo();

  // redesenha so as letras (quando troca maiuscula/minuscula)
  auto repintaLetras = [&]() {
    for (int li = 0; li < 3; li++)
      for (int k = 0, n = strlen(LINHAS[li]); k < n; k++) pintaTecla(li, k, false);
  };

  // ---- laco: acende ao ENCOSTAR, aplica ao SOLTAR
  while (true) {
    int16_t x, y;
    while (!tft.getTouch(&x, &y)) delay(6);      // espera encostar

    int li = -1, kk = -1;
    for (int a = 0; a < 3 && li < 0; a++)
      for (int b = 0, n = strlen(LINHAS[a]); b < n; b++)
        if (dentro(tecla(a, b), x, y)) { li = a; kk = b; break; }

    if (li >= 0) pintaTecla(li, kk, true);       // retorno imediato ao dedo

    int16_t ux = x, uy = y, tx, ty;
    while (tft.getTouch(&tx, &ty)) { ux = tx; uy = ty; delay(6); }   // espera soltar

    if (li >= 0) {
      pintaTecla(li, kk, false);
      if (dentro(tecla(li, kk), ux, uy)) {       // saiu de cima -> desiste
        size_t p = strlen(buf);
        if (p < lim) {
          buf[p] = maiuscula ? LINHAS[li][kk] : (char)tolower(LINHAS[li][kk]);
          buf[p + 1] = 0;
          if (p == 0) { maiuscula = false; repintaLetras(); }  // so a primeira
        }
        pintaCampo();
      }
      continue;
    }

    if (dentro(bSalvar, ux, uy)) { campo.deleteSprite(); strncpy(texto, buf, tamMax - 1); texto[tamMax - 1] = 0; return true; }
    if (dentro(bCancel, ux, uy)) { campo.deleteSprite(); return false; }
    if (dentro(bShift, ux, uy))  { maiuscula = !maiuscula; repintaLetras(); continue; }
    if (dentro(bApaga, ux, uy))  { size_t n = strlen(buf); if (n) { buf[n - 1] = 0; pintaCampo(); } continue; }
    if (dentro(bEspaco, ux, uy)) { size_t n = strlen(buf); if (n && n < lim) { buf[n] = ' '; buf[n + 1] = 0; pintaCampo(); } continue; }
  }
}
