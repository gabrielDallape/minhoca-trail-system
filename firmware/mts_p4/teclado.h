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

// 32: o campo mais longo que passa aqui e senha de WiFi (WPA aceita ate 63,
// mas 32 cobre senha domestica; 24 ja deixou senha de fora).
#define TECLADO_MAX 32

// ------------------------------------------------------- teclado numerico
// Para o codigo do grupo, que funciona como senha. Cinco digitos, mesmo formato
// do grupo_ws das telas S3.
// Devolve true se confirmou. 'valor' entra vazio e sai com o digitado.
template <typename TFT>
bool tecladoNumero(TFT& tft, const char* sub, const char* dica, char* valor, int nDig)
{
  char buf[8] = {0};

  // GEOMETRIA DERIVADA DA TELA, nunca cravada.
  // A versao anterior usava TH=96 e gy=250 fixos, medidos na tela de 1280 de
  // altura (a de 5", em retrato). Na de 7" - 1024x600 - as quatro fileiras
  // terminavam em 676: a terceira caia por cima dos botoes de baixo e a quarta,
  // com o "0" e o APAGA, ficava fora da tela. Aqui a altura da tecla e o que
  // SOBRA depois do cabecalho, do mostrador e dos botoes, dividido por quatro.
  const int GAP  = 14;
  const int BTH  = (TELA_H < 700) ? 76 : 88;        // altura dos botoes de baixo
  const int MOSY = CAB_H + 26;                      // topo do mostrador de digitos
  const int MOSH = (TELA_H < 700) ? 84 : 110;
  const int gy   = MOSY + MOSH + 18;
  const int base = TELA_H - M - BTH - 14;           // ultima linha util
  const int TH   = (base - gy - 3 * GAP) / 4;
  const int TW   = (TELA_W < 900) ? 128 : 156;
  const int gx   = (TELA_W - (3 * TW + 2 * GAP)) / 2;

  auto rTecla = [&](int i) -> Ret {          // 0..8 = "1".."9", 9 = "0"
    int l = (i == 9) ? 3 : i / 3, c = (i == 9) ? 1 : i % 3;
    return Ret{ (int16_t)(gx + c * (TW + GAP)), (int16_t)(gy + l * (TH + GAP)), (int16_t)TW, (int16_t)TH };
  };
  Ret bApaga  = { (int16_t)(gx + 2 * (TW + GAP)), (int16_t)(gy + 3 * (TH + GAP)), (int16_t)TW, (int16_t)TH };
  Ret bVolta  = { M, (int16_t)(TELA_H - M - BTH), 240, (int16_t)BTH };
  Ret bEntrar = { (int16_t)(TELA_W - M - 300), (int16_t)(TELA_H - M - BTH), 300, (int16_t)BTH };

  LGFX_Sprite mostrador(&tft);
  mostrador.setPsram(true); mostrador.setColorDepth(16);
  mostrador.createSprite(TELA_W, MOSH);

  auto pintaMostrador = [&]() {
    mostrador.fillScreen(C_BG);
    int n = strlen(buf);
    const int cw = (TELA_W < 900) ? 76 : 92, cg = 16;
    int larg = nDig * cw + (nDig - 1) * cg;
    int x0 = (TELA_W - larg) / 2;
    for (int i = 0; i < nDig; i++) {
      int x = x0 + i * (cw + cg);
      bool tem = i < n;
      mostrador.fillRoundRect(x, 2, cw, MOSH - 4, 12, C_SURF);
      mostrador.drawRoundRect(x, 2, cw, MOSH - 4, 12, tem ? C_SUN : C_LINE);
      if (tem) {
        mostrador.setTextDatum(middle_center);
        mostrador.setFont(&fonts::FreeSansBold24pt7b);
        mostrador.setTextColor(C_INK);
        char s[2] = { buf[i], 0 };
        mostrador.drawString(s, x + cw / 2, MOSH / 2);
      }
    }
    mostrador.setFont(&fonts::Font0);
    mostrador.pushSprite(0, MOSY);
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
  tft.drawString(dica, TELA_W / 2, CAB_H / 2 + 2);
  for (int i = 0; i < 10; i++) { char s[2] = { (char)((i == 9) ? '0' : '1' + i), 0 }; pintaTecla(rTecla(i), s, false); }
  pintaTecla(bApaga, "<", false);
  botao(tft, bVolta, "< VOLTAR", "", C_INK2, false);
  botao(tft, bEntrar, "ENTRAR", "", C_SUN, true);
  pintaMostrador();

  while (true) {
    int16_t x, y;
    while (!tft.getTouch(&x, &y)) { if (g_hookServico) g_hookServico(); delay(6); }
    int alvo = -1;
    for (int i = 0; i < 10; i++) if (dentro(rTecla(i), x, y)) { alvo = i; break; }
    bool ehApaga = dentro(bApaga, x, y);
    if (alvo >= 0) { char s[2] = { (char)((alvo == 9) ? '0' : '1' + alvo), 0 }; pintaTecla(rTecla(alvo), s, true); }
    else if (ehApaga) pintaTecla(bApaga, "<", true);

    int16_t ux = x, uy = y, tx, ty;
    while (tft.getTouch(&tx, &ty)) { ux = tx; uy = ty; if (g_hookServico) g_hookServico(); delay(6); }

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
      for (int k = 0; k < 2; k++) { tft.fillRect(0, MOSY, TELA_W, MOSH, C_BG); esperaServindo(70); pintaMostrador(); esperaServindo(70); }
    }
  }
}

template <typename TFT>
bool tecladoTexto(TFT& tft, const char* sub, char* texto, size_t tamMax,
                  bool ehSenha = false)
{
  // LAYOUT DE CELULAR (refeito em 2026-08-25, depois de a tela de senha do WiFi
  // ser declarada "completamente bugada" - com razao):
  //   - fileira de NUMEROS sempre visivel. A versao anterior escondia os
  //     numeros atras de DOIS toques no modo, e senha sem numero quase nao existe.
  //   - o botao da esquerda cicla ABC -> abc -> simbolos.
  //   - modo SENHA (ehSenha): comeca em minusculas, NAO troca de caixa sozinho
  //     (a troca automatica e coisa de nome proprio; numa senha ela muda o que
  //     voce digita sem avisar), e os textos falam de senha, nao de nome.
  // As fileiras de simbolos tem OS MESMOS comprimentos das de letras (10/9/7)
  // de proposito: a geometria calculada nao muda, so o que esta nas teclas.
  static const char* L_LETRAS[3] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
  static const char* L_SIMB[3]   = { "@#$%&*()_-", "+=/:;\"',.", "!?~<>[]" };
  static const char* L_NUMS      = "1234567890";
  uint8_t modo = ehSenha ? 1 : 0;       // 0 = ABC, 1 = abc, 2 = simbolos
  // li 0 = numeros (fixa); 1..3 = letras ou simbolos conforme o modo
  auto fileira = [&](int li) -> const char* {
    if (li == 0) return L_NUMS;
    return (modo == 2) ? L_SIMB[li - 1] : L_LETRAS[li - 1];
  };
  const size_t lim = (tamMax - 1 < TECLADO_MAX) ? tamMax - 1 : TECLADO_MAX;

  // GEOMETRIA DERIVADA DA TELA. A versao anterior cravava TW=112, medido na tela
  // de 1280 de largura. Em 1024 a fileira de cima (10 teclas) precisava de 1210 px
  // e comecava em x = -93, com as primeiras letras fora da tela; e a fileira do
  // ZXCVBNM ia ate 1047, passando POR CIMA do botao APAGA. Era isso que aparecia
  // como "botao em cima de botao" e "o apagar em cima das letras".
  //
  // A largura da tecla sai da fileira MAIS LONGA (10 teclas): se ela cabe, todas
  // cabem. A altura sai do que sobra entre o campo de texto e a margem de baixo,
  // dividido pelas CINCO faixas (numeros + 3 fileiras + acoes). Na tela de 600
  // isso da 77 px de tecla - ainda acima do alvo de toque com luva (76).
  const int GAP     = 10;
  const int CAMPO_Y = (TELA_H < 700) ? 14 : 20;
  const int CAMPO_H = (TELA_H < 700) ? 88 : 104;
  const int larguraUtil = TELA_W - 2 * M;
  const int TW = (larguraUtil - 9 * GAP) / 10;
  const int Y0 = CAMPO_Y + CAMPO_H + ((TELA_H < 700) ? 14 : 26);
  const int TH = (TELA_H - M - Y0 - 4 * GAP) / 5;

  // Nas laterais da fileira de 7 ficam o MODO e o APAGA. A largura deles e o
  // que sobra dos dois lados das 7 teclas - calculado, nao cravado.
  const int larg3 = 7 * TW + 6 * GAP;
  const int LAT   = (larguraUtil - larg3 - 2 * GAP) / 2;

  char buf[TECLADO_MAX + 1];
  strncpy(buf, texto, TECLADO_MAX); buf[TECLADO_MAX] = 0;

  auto tecla = [&](int li, int col) -> Ret {
    int n = strlen(fileira(li));
    int larg = n * TW + (n - 1) * GAP;
    int x0 = (li == 3) ? (M + LAT + GAP) : (TELA_W - larg) / 2;
    return Ret{ (int16_t)(x0 + col * (TW + GAP)), (int16_t)(Y0 + li * (TH + GAP)),
                (int16_t)TW, (int16_t)TH };
  };
  const int y3 = Y0 + 3 * (TH + GAP), y4 = Y0 + 4 * (TH + GAP);
  const int LADO = (larguraUtil - GAP * 2) / 5;      // cancelar / espaco / salvar
  Ret bShift  = { M, (int16_t)y3, (int16_t)LAT, (int16_t)TH };
  Ret bApaga  = { (int16_t)(TELA_W - M - LAT), (int16_t)y3, (int16_t)LAT, (int16_t)TH };
  Ret bCancel = { M, (int16_t)y4, (int16_t)LADO, (int16_t)TH };
  Ret bEspaco = { (int16_t)(M + LADO + GAP), (int16_t)y4,
                  (int16_t)(larguraUtil - 2 * (LADO + GAP)), (int16_t)TH };
  Ret bSalvar = { (int16_t)(TELA_W - M - LADO), (int16_t)y4, (int16_t)LADO, (int16_t)TH };

  // sprite pequeno so do campo: e a unica coisa que muda ao digitar
  LGFX_Sprite campo(&tft);
  campo.setPsram(true);
  campo.setColorDepth(16);
  campo.createSprite(TELA_W, CAMPO_H);

  auto pintaCampo = [&]() {
    campo.fillScreen(C_BG);
    // A caixa acompanha a tela: 600 px fixos ocupavam 59% de 1024 e 47% de 1280,
    // parecendo dois desenhos diferentes. E a altura vem do proprio sprite.
    const int cw = larguraUtil * 2 / 3, cx = (TELA_W - cw) / 2, cy = 4;
    const int ch = CAMPO_H - 12;
    campo.fillRoundRect(cx, cy, cw, ch, 14, C_SURF);
    campo.drawRoundRect(cx, cy, cw, ch, 14, C_SUN);
    campo.setTextDatum(middle_center);
    // texto longo desce para a fonte menor - com 24pt, 32 caracteres estouravam
    // a caixa pelos dois lados
    campo.setFont(strlen(buf) > 16 ? (const lgfx::IFont*)&fonts::FreeSansBold18pt7b
                                   : (const lgfx::IFont*)&fonts::FreeSansBold24pt7b);
    campo.setTextColor(strlen(buf) ? C_INK : C_INK3);
    campo.drawString(strlen(buf) ? buf : (ehSenha ? "senha da rede" : "sem nome"),
                     TELA_W / 2, cy + ch / 2);
    // o contador vira aviso quando enche - senao o texto so para de aceitar
    // letra e o usuario nao entende por que
    bool cheio = strlen(buf) >= lim;
    campo.setTextDatum(middle_right);
    campo.setFont(&fonts::FreeSans9pt7b);
    campo.setTextColor(cheio ? C_SUN : C_INK3);
    char c[16]; snprintf(c, sizeof(c), "%d / %d", (int)strlen(buf), (int)lim);
    campo.drawString(c, TELA_W - M, cy + ch / 2);
    campo.setFont(&fonts::Font0);
    campo.pushSprite(0, CAMPO_Y);
  };

  auto teclaChar = [&](int li, int k) -> char {
    char c = fileira(li)[k];
    // so LETRA muda de caixa; numero (li 0) e simbolo passam direto
    return (li > 0 && modo == 1) ? (char)tolower(c) : c;
  };
  auto pintaTecla = [&](int li, int k, bool aceso) {
    Ret r = tecla(li, k);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 12, aceso ? C_SUN : C_SURF2);
    tft.setTextDatum(middle_center);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextColor(aceso ? C_BG : C_INK);
    char s[2] = { teclaChar(li, k), 0 };
    tft.drawString(s, r.x + r.w / 2, r.y + r.h / 2);
    tft.setFont(&fonts::Font0);
  };
  // o botao mostra para ONDE vai (convencao dos teclados de celular)
  auto pintaShift = [&]() {
    static const char* PROX[3] = { "abc", "#$%", "ABC" };
    // botao() "vazado" NAO pinta o fundo (so contorno + texto): repintar por
    // cima empilhava abc/#$%/ABC um sobre o outro. Limpa o retangulo antes.
    tft.fillRect(bShift.x, bShift.y, bShift.w, bShift.h, C_BG);
    botao(tft, bShift, PROX[modo], "", C_INK2, false);
  };

  // ---- desenho completo, uma vez so, DIRETO na tela.
  // Nada de sprite de tela cheia aqui: copiar 1,8 MB da PSRAM para a PSRAM e
  // lento o bastante para VER pintando de cima para baixo - parecia transicao de
  // slide. Desenhar direto e mais rapido, e como isto acontece uma vez por tela
  // (nao por toque), nao ha o que piscar.
  tft.fillScreen(C_BG);
  cabecalho(tft, sub, false);
  for (int li = 0; li < 4; li++)
    for (int k = 0, n = strlen(fileira(li)); k < n; k++) pintaTecla(li, k, false);
  pintaShift();
  botao(tft, bApaga, "APAGA", "", C_INK2, false);
  botao(tft, bEspaco, "ESPACO", "", C_INK2, false);
  botao(tft, bCancel, "CANCELAR", "", C_INK3, false);
  botao(tft, bSalvar, "SALVAR", "", C_SUN, true);
  tft.setTextDatum(bottom_center);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  // ancorado no RODAPE: o 596 fixo (medido na tela de 600) caia em cima dos
  // botoes na tela de 720
  tft.drawString(ehSenha ? "a senha fica gravada so neste aparelho"
                         : "o nome aparece para os outros carros do grupo",
                 tft.width() / 2, TELA_H - 4);
  tft.setFont(&fonts::Font0);
  pintaCampo();

  // redesenha so as teclas que mudam (a fileira de numeros e fixa)
  auto repintaLetras = [&]() {
    for (int li = 1; li < 4; li++)
      for (int k = 0, n = strlen(fileira(li)); k < n; k++) pintaTecla(li, k, false);
  };

  // ---- laco: acende ao ENCOSTAR, aplica ao SOLTAR
  while (true) {
    int16_t x, y;
    while (!tft.getTouch(&x, &y)) { if (g_hookServico) g_hookServico(); delay(6); }      // espera encostar

    int li = -1, kk = -1;
    for (int a = 0; a < 4 && li < 0; a++)
      for (int b = 0, n = strlen(fileira(a)); b < n; b++)
        if (dentro(tecla(a, b), x, y)) { li = a; kk = b; break; }

    if (li >= 0) pintaTecla(li, kk, true);       // retorno imediato ao dedo

    int16_t ux = x, uy = y, tx, ty;
    while (tft.getTouch(&tx, &ty)) { ux = tx; uy = ty; if (g_hookServico) g_hookServico(); delay(6); }   // espera soltar

    if (li >= 0) {
      pintaTecla(li, kk, false);
      if (dentro(tecla(li, kk), ux, uy)) {       // saiu de cima -> desiste
        size_t p = strlen(buf);
        if (p < lim) {
          buf[p] = teclaChar(li, kk);
          buf[p + 1] = 0;
          // so a primeira letra fica maiuscula (nome proprio). NUNCA em senha:
          // trocar a caixa sozinho no meio de uma senha muda o que a pessoa
          // digita sem avisar - foi um dos "bugs" relatados na tela do WiFi.
          if (!ehSenha && p == 0 && modo == 0) { modo = 1; repintaLetras(); pintaShift(); }
        }
        pintaCampo();
      }
      continue;
    }

    if (dentro(bSalvar, ux, uy)) { campo.deleteSprite(); strncpy(texto, buf, tamMax - 1); texto[tamMax - 1] = 0; return true; }
    if (dentro(bCancel, ux, uy)) { campo.deleteSprite(); return false; }
    if (dentro(bShift, ux, uy))  { modo = (uint8_t)((modo + 1) % 3); repintaLetras(); pintaShift(); continue; }
    if (dentro(bApaga, ux, uy))  { size_t n = strlen(buf); if (n) { buf[n - 1] = 0; pintaCampo(); } continue; }
    if (dentro(bEspaco, ux, uy)) { size_t n = strlen(buf); if (n && n < lim) { buf[n] = ' '; buf[n + 1] = 0; pintaCampo(); } continue; }
  }
}
