#pragma once
// ============================================================================
//  TELAS DE GRUPO - criar, sala de espera, entrar
// ============================================================================
//
//   CRIAR  - o aparelho propoe "Grupo do <nome do carro>" e sorteia um CODIGO de
//            5 digitos. O codigo e a senha que os outros vao precisar. Quem cria
//            vira o lider.
//   ENTRAR - a LISTA DOS GRUPOS NO AR (core/vizinhos.h): um toque e entra. O
//            pacote so leva a sala como hash, e e o hash que entra na sessao
//            ("#79FD"). Digitar o codigo continua possivel, num botao.
//            Ate 2026-09-30 era so o teclado - e no teste daquele dia as duas
//            telas tocaram em CRIAR e ficaram em grupos diferentes sem saber.
//   SALA   - depois de criar, antes de abrir a trilha: o codigo em destaque e a
//            lista de quem ja entrou (QUEM ESTA NO AR, nao lista inventada).

#include "ui.h"
#include "teclado.h"
#include "../estado.h"
#include "esp_random.h"

#define GRUPO_COD_DIG   5

template <typename TFT>
bool telaSalaEspera(TFT& tft, const char* gNome, const char* gCod)
{
  Ret rSair  = { M, (int16_t)(TELA_H - M - 88), 240, 88 };
  Ret rAbrir = { (int16_t)(TELA_W - M - 380), (int16_t)(TELA_H - M - 88), 380, 88 };
  auto linhaMembro = [&](int i) -> Ret {
    return Ret{ (int16_t)(TELA_W / 2 + M), (int16_t)(190 + i * 56), (int16_t)(TELA_W / 2 - 2 * M), 50 };
  };
  auto pintaMembro = [&](int i, const Carro& c) {
    Ret r = linhaMembro(i);
    if (r.y + r.h > TELA_H - M - 88 - 8) return;    // nao invade os botoes
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 14, C_SURF);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 14, C_LINE);
    tft.fillRoundRect(r.x + 6, r.y + 6, 10, r.h - 12, 5, CORES_MAPA[c.cor % N_CORES]);
    tft.setTextDatum(middle_left); tft.setFont(&fonts::FreeSansBold12pt7b); tft.setTextColor(C_INK);
    tft.drawString(c.nome, r.x + 28, r.y + r.h / 2);
    if (c.lider) {
      tft.setTextDatum(middle_right); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_SUN);
      tft.drawString("LIDER", r.x + r.w - 18, r.y + r.h / 2);
    }
    tft.setFont(&fonts::Font0);
  };
  int nMembros = 0;
  auto pintaContagem = [&]() {
    tft.fillRect(TELA_W / 2 + M, 150, TELA_W / 2 - 2 * M, 30, C_BG);
    tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK3);
    char c[40]; snprintf(c, sizeof(c), "NO GRUPO  (%d)", nMembros);
    tft.drawString(c, TELA_W / 2 + M, 152);
    tft.setFont(&fonts::Font0);
  };
  auto pintaLista = [&]() {
    tft.fillRect(TELA_W / 2 + M, 186, TELA_W / 2 - 2 * M, TELA_H - 186 - 140, C_BG);
    int n = 0;
    for (int i = 0; i < FROTA_MAX_CARROS; i++) if (g_frota.c[i].ativo) pintaMembro(n++, g_frota.c[i]);
    nMembros = n;
    pintaContagem();
  };

  tft.fillScreen(C_BG);
  cabecalho(tft, "sala de espera", false);
  tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
  tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSansBold18pt7b); tft.setTextColor(C_TAN);
  tft.drawString(gNome, M, 150);
  const int cxw = TELA_W / 2 - M - 20, cxh = 150;
  tft.fillRoundRect(M, 208, cxw, cxh, 14, C_SURF);
  tft.fillRoundRect(M, 208, 5, cxh, 2, C_SUN);
  tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK3);
  tft.drawString("CODIGO DO GRUPO", M + 30, 232);
  tft.setTextDatum(middle_left); tft.setFont(&fonts::FreeSansBold24pt7b); tft.setTextColor(C_SUN);
  int cx = M + 30;
  const int passo = (cxw - 60) / GRUPO_COD_DIG;
  for (int i = 0; i < GRUPO_COD_DIG; i++) { char s[2] = { gCod[i], 0 }; tft.drawString(s, cx, 208 + cxh / 2 + 18); cx += passo; }
  tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK2);
  tft.drawString("dite este numero para quem for entrar", M + 30, 344);
  tft.setTextColor(C_INK3);
  tft.drawString("A trilha pode ser aberta a qualquer momento.", M, TELA_H - 168);
  tft.drawString("Quem chegar depois tambem entra com o mesmo codigo.", M, TELA_H - 140);
  botao(tft, rSair, "< CANCELAR", "", C_INK2, false);
  botao(tft, rAbrir, "ABRIR TRILHA", "", C_SUN, true);
  pintaLista();
  tft.setFont(&fonts::Font0);

  // A lista e remontada da frota e so repinta se MUDOU (nome ou quantidade).
  uint32_t assin = 0;
  while (true) {
    uint32_t a = 0;
    for (int i = 0; i < FROTA_MAX_CARROS; i++) {
      const Carro& c = g_frota.c[i];
      if (!c.ativo) continue;
      a = a * 31 + c.slot + (c.temNome ? 100u : 0u) + (c.lider ? 1000u : 0u) + c.cor * 7u;
      for (const char* p = c.nome; *p; p++) a = a * 31 + (uint8_t)*p;
    }
    if (a != assin) { assin = a; pintaLista(); }
    int16_t x, y;
    if (!esperaToque(tft, x, y, 150)) continue;
    if (dentro(rAbrir, x, y)) return true;
    if (dentro(rSair, x, y))  return false;
  }
}

template <typename TFT>
bool telaCriarGrupo(TFT& tft, const char* nomeCarro, char* gNome, char* gCod)
{
  if (!gNome[0]) snprintf(gNome, GRUPO_NOME_MAX, "Grupo do %s", nomeCarro);
  if (!gCod[0]) {
    uint32_t r = (uint32_t)esp_random() % 90000 + 10000;    // a senha, sorteada uma vez
    snprintf(gCod, 8, "%lu", (unsigned long)r);
  }
  Ret rNome  = { M, 176, (int16_t)(TELA_W - 2 * M), 130 };
  Ret rCod   = { M, 328, (int16_t)(TELA_W - 2 * M), 130 };
  Ret rVolta = { M, (int16_t)(TELA_H - M - 88), 240, 88 };
  Ret rAbrir = { (int16_t)(TELA_W - M - 380), (int16_t)(TELA_H - M - 88), 380, 88 };
  auto desenha = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "criar grupo - voce e o lider", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
    linhaCfg(tft, rNome, "NOME DO GRUPO", gNome, "tocar para mudar >", true);
    tft.fillRoundRect(rCod.x, rCod.y, rCod.w, rCod.h, 14, C_SURF);
    tft.fillRoundRect(rCod.x, rCod.y, 5, rCod.h, 2, C_SUN);
    tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK3);
    tft.drawString("CODIGO DO GRUPO", rCod.x + 30, rCod.y + 22);
    tft.setTextDatum(middle_left); tft.setFont(&fonts::FreeSansBold24pt7b); tft.setTextColor(C_SUN);
    int cx = rCod.x + 30;
    for (int i = 0; i < GRUPO_COD_DIG; i++) { char s[2] = { gCod[i], 0 }; tft.drawString(s, cx, rCod.y + 96); cx += 54; }
    tft.setTextDatum(middle_right); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK2);
    tft.drawString("quem quiser entrar precisa deste numero", rCod.x + rCod.w - 30, rCod.y + rCod.h / 2);
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    botao(tft, rAbrir, "ABRIR TRILHA", "", C_SUN, true);
    tft.setFont(&fonts::Font0);
  };
  desenha();
  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return false;
    if (dentro(rAbrir, x, y)) return true;
    if (dentro(rNome, x, y)) {
      char tmp[GRUPO_NOME_MAX];
      strncpy(tmp, gNome, sizeof(tmp)); tmp[sizeof(tmp) - 1] = 0;
      if (tecladoTexto(tft, "nome do grupo", tmp, sizeof(tmp)) && strlen(tmp)) strncpy(gNome, tmp, GRUPO_NOME_MAX - 1);
      desenha();
    }
  }
}

// ENTRAR. Devolve true com 'cod' preenchido ("13520" digitado, ou "#79FD" da
// lista) e 'nome' com o nome para mostrar (vazio = o chamador inventa).
#define ENTRAR_MAX_LISTA 3          // 3 cartoes de 96 px cabem acima dos botoes
template <typename TFT>
bool telaEntrarGrupo(TFT& tft, char* cod, char* nome, size_t nNome)
{
  cod[0] = 0; nome[0] = 0;
  Ret rVolta  = { M, (int16_t)(TELA_H - M - 88), 240, 88 };
  Ret rCodigo = { (int16_t)(TELA_W - M - 380), (int16_t)(TELA_H - M - 88), 380, 88 };
  auto cartaoRet = [&](int i) -> Ret { return Ret{ M, (int16_t)(166 + i * 100), (int16_t)(TELA_W - 2 * M), 88 }; };
  int idx[ENTRAR_MAX_LISTA]; int n = 0;

  auto pintaLista = [&]() {
    tft.fillRect(0, 150, TELA_W, TELA_H - 150 - 88 - M - 6, C_BG);
    if (n == 0) {
      tft.setTextDatum(middle_center); tft.setFont(&fonts::FreeSansBold18pt7b); tft.setTextColor(C_INK2);
      tft.drawString("procurando grupos no ar...", TELA_W / 2, 250);
      tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK3);
      tft.drawString("o grupo aparece aqui quando a tela de quem guia transmitir", TELA_W / 2, 310);
      tft.drawString("(radio ligado, a ate uns 300 m)", TELA_W / 2, 345);
      tft.setFont(&fonts::Font0);
      return;
    }
    for (int i = 0; i < n; i++) {
      const GrupoNoAr& g = g_viz.g[idx[i]];
      Ret r = cartaoRet(i);
      tft.fillRoundRect(r.x, r.y, r.w, r.h, 16, C_SURF);
      tft.drawRoundRect(r.x, r.y, r.w, r.h, 16, C_LINE);
      tft.fillRoundRect(r.x, r.y, 6, r.h, 3, g.temLider ? C_SUN : C_INK3);
      char titulo[40];
      if (g.temNome) snprintf(titulo, sizeof(titulo), "%s", g.nome);
      else           snprintf(titulo, sizeof(titulo), "grupo %04X", (unsigned)g.sala);
      tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSansBold18pt7b); tft.setTextColor(C_INK);
      tft.drawString(titulo, r.x + 30, r.y + 12);
      const char* sinal = g.rssi > -75 ? "sinal forte" : g.rssi > -95 ? "sinal bom" : "sinal fraco";
      const int nc = vizinhosCarros(g);
      char sub[96];
      snprintf(sub, sizeof(sub), "%s  |  %s  |  %d carro%s  |  grupo %04X%s",
               g.temLider ? "LIDER NO AR" : "sem lider ouvido", sinal, nc, nc == 1 ? "" : "s",
               (unsigned)g.sala, g.temNome ? "" : "  |  nome a caminho");
      tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(g.temLider ? C_SUN : C_INK3);
      tft.drawString(sub, r.x + 30, r.y + 56);
      tft.setTextDatum(middle_right); tft.setFont(&fonts::FreeSansBold18pt7b); tft.setTextColor(C_SUN);
      tft.drawString("ENTRAR >", r.x + r.w - 26, r.y + r.h / 2);
    }
    tft.setFont(&fonts::Font0);
  };
  auto desenha = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "entrar num grupo", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
    tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK2);
    tft.drawString("Grupos no ar perto de voce - toque para entrar:", M, 112);
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    botao(tft, rCodigo, "DIGITAR CODIGO", "", C_INK2, false);
    tft.setFont(&fonts::Font0);
    pintaLista();
  };

  // A lista repinta so quando MUDA (grupo, nome, lider, faixa de sinal, carros).
  uint32_t assin = 0xFFFFFFFFu;
  auto assinatura = [&]() -> uint32_t {
    uint32_t a = (uint32_t)n;
    for (int i = 0; i < n; i++) {
      const GrupoNoAr& g = g_viz.g[idx[i]];
      a = a * 31 + g.sala + (g.temLider ? 7u : 0u) + (uint32_t)vizinhosCarros(g) * 13u
        + (uint32_t)(g.rssi > -75 ? 1 : g.rssi > -95 ? 2 : 3) * 101u;
      for (const char* p = g.nome; *p; p++) a = a * 31 + (uint8_t)*p;
    }
    return a;
  };
  n = vizinhosOrdena(g_viz, idx, ENTRAR_MAX_LISTA);
  assin = assinatura();
  desenha();
  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y, 500)) {
      n = vizinhosOrdena(g_viz, idx, ENTRAR_MAX_LISTA);
      uint32_t a = assinatura();
      if (a != assin) { assin = a; pintaLista(); }
      continue;
    }
    if (dentro(rVolta, x, y)) return false;
    if (dentro(rCodigo, x, y)) {
      if (tecladoNumero(tft, "entrar num grupo", "digite o codigo que o lider passou", cod, GRUPO_COD_DIG)) return true;
      cod[0] = 0;
      desenha();
      continue;
    }
    for (int i = 0; i < n; i++) {
      if (!dentro(cartaoRet(i), x, y)) continue;
      const GrupoNoAr& g = g_viz.g[idx[i]];
      snprintf(cod, 8, "#%04X", (unsigned)g.sala);
      if (g.temNome) snprintf(nome, nNome, "Grupo de %s", g.nome);
      else           snprintf(nome, nNome, "Grupo %04X", (unsigned)g.sala);
      return true;
    }
  }
}
