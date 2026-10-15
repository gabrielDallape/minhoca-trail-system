#pragma once
// ============================================================================
//  TELA INICIAL - as duas acoes (criar / entrar), o estado do aparelho, retomar
// ============================================================================
//
// ESTADO DE VERDADE, nao rotulo fixo: tres pastilhas (GPS, RADIO, CARTAO) com
// icone e numero, repintadas sozinhas 1x por segundo. Tocar na faixa das
// pastilhas abre o diagnostico - e o gesto que a pessoa ja tenta.

#include "ui.h"
#include "mapa_fundo.h"
#include "../estado.h"
#include "../rede.h"
#if MTS_TEM_GPS
#include "../../hal/gps.h"
#endif

struct TelaInicialAlvos { Ret criar, entrar, eng, retomar, descarta, diag; };
static TelaInicialAlvos g_ini;

template <typename TFT>
void pintaPastilhas(TFT& tft)
{
  const int x0 = M + 110;
  const int py = (CAB_H - 52) / 2;
  tft.fillRect(x0 - 4, py - 2, TELA_W - x0 - 96, 56, C_BG);
  g_ini.diag = { (int16_t)(x0 - 4), (int16_t)(py - 2), (int16_t)(TELA_W - x0 - 96), 56 };
  avisoDesenha(tft, TELA_W, CAB_H + 4);

  char vg[12] = "--"; uint16_t cg = C_INK3;
#if MTS_TEM_GPS
  if (gpsFixValido())      { snprintf(vg, sizeof(vg), "%u", g_gps.sats); cg = C_OK; }
  else if (g_gps.frases)   { strcpy(vg, "...");  cg = C_WARN; }
  else                     { strcpy(vg, "X");    cg = C_RED;  }
#endif
  char vr[12] = "--"; uint16_t cr = C_INK3;
#if MTS_TEM_RADIO
  if      (!g_rede.ok)                              { strcpy(vr, "X");     cr = C_RED;  }
  else if (g_frota.versaoErrada && millis() - g_frota.versaoErradaMs < 60000UL) { strcpy(vr, "v?"); cr = C_WARN; }
  else if (g_rede.tdma.holdover)                    { strcpy(vr, "~");     cr = C_WARN; }
  else if (g_rede.tdma.sync == TDMA_SYNC_PPS)       { strcpy(vr, "PPS");   cr = C_OK;   }
  else if (g_rede.tdma.sync == TDMA_SYNC_BEACON)    { strcpy(vr, "GRUPO"); cr = C_OK;   }
  else                                              { strcpy(vr, "...");   cr = C_WARN; }
#endif
  int px = x0;
  px += pastilhaIc(tft, px, py, 0, vg, cg) + 12;
  px += pastilhaIc(tft, px, py, 1, vr, cr) + 12;
  pastilhaIc(tft, px, py, 2, "", (g_fundo.temVec || g_fundo.temDem) ? C_OK : C_SUN, !(g_fundo.temVec || g_fundo.temDem));
}

template <typename TFT>
void desenhaInicial(TFT& tft, int premido = -1)
{
  tft.fillScreen(C_BG);
  cabecalho(tft, "", true, &g_ini.eng);
  pintaPastilhas(tft);
  tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);

  const int GAP = 14;
  const int bw = (TELA_W - 2 * M - GAP) / 2;
  int topo = CAB_H + 16;
  const int base = TELA_H - BARRA_H;

  // A FAIXA DE RETOMAR so existe se houver sessao guardada, e empurra os cartoes.
  g_ini.retomar = { 0, 0, 0, 0 }; g_ini.descarta = { 0, 0, 0, 0 };
  if (g_ses.ativa) {
    const int fh = 92;
    g_ini.retomar  = { M, (int16_t)topo, (int16_t)(TELA_W - 2 * M - 92 - 10), (int16_t)fh };
    g_ini.descarta = { (int16_t)(TELA_W - M - 92), (int16_t)topo, 92, (int16_t)fh };
    tft.fillRoundRect(g_ini.retomar.x, g_ini.retomar.y, g_ini.retomar.w, g_ini.retomar.h, 14, C_SURF);
    tft.fillRoundRect(g_ini.retomar.x, g_ini.retomar.y + g_ini.retomar.h - 5, g_ini.retomar.w, 5, 3, C_CASING);
    icComboio(tft, g_ini.retomar.x + 36, g_ini.retomar.y + fh / 2, 34, C_SUN);
    tft.setFont(&fonts::FreeSansBold18pt7b); tft.setTextDatum(middle_left); tft.setTextColor(C_INK);
    tft.drawString(g_ses.gNome, g_ini.retomar.x + 68, g_ini.retomar.y + fh / 2);
    tft.setFont(&fonts::FreeSansBold12pt7b); tft.setTextDatum(middle_right); tft.setTextColor(C_SUN);
    tft.drawString("RETOMAR", g_ini.retomar.x + g_ini.retomar.w - 20, g_ini.retomar.y + fh / 2);
    // o X NAO e enfeite: sem ele uma sessao velha ficaria oferecida para sempre
    botaoIc(tft, g_ini.descarta, 2, C_SURF, C_INK3);
    topo += fh + 14;
  }
  const int bh = base - topo - 16;
  g_ini.criar  = { M, (int16_t)topo, (int16_t)bw, (int16_t)bh };
  g_ini.entrar = { (int16_t)(TELA_W - M - bw), (int16_t)topo, (int16_t)bw, (int16_t)bh };

  auto cartao = [&](const Ret& r, const char* verbo, const char* dica, bool forte, bool aceso, bool lupa) {
    const uint16_t fundo  = forte ? C_SUN : C_SURF;
    const uint16_t frente = forte ? C_BG  : C_INK;
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 22, fundo);
    if (aceso) tft.fillRoundRect(r.x, r.y, r.w, 7, 3, C_CASING);
    else       tft.fillRoundRect(r.x, r.y + r.h - 7, r.w, 7, 3, C_CASING);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 22, forte ? C_CASING : C_LINE);
    const int cx = r.x + r.w / 2;
    const int s  = (r.h > 300) ? 104 : 76;
    if (lupa) icLupa(tft, cx, r.y + (int)(r.h * 0.34f), s, frente);
    else      icMais(tft, cx, r.y + (int)(r.h * 0.34f), s, frente);
    tft.setFont(&fonts::FreeSansBold24pt7b); tft.setTextDatum(middle_center); tft.setTextColor(frente);
    tft.drawString(verbo, cx, r.y + r.h - 92);
    tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(forte ? C_CASING : C_INK2);
    tft.drawString(dica, cx, r.y + r.h - 44);
    tft.setFont(&fonts::Font0);
  };
  cartao(g_ini.criar,  "CRIAR GRUPO", "voce guia o comboio",       true,  premido == 0, false);
  cartao(g_ini.entrar, "ENTRAR",      "com o codigo de quem guia", false, premido == 1, true);

  // Rodape: quem e este aparelho, e o comboio com a contagem.
  tft.drawFastHLine(0, TELA_H - BARRA_H, TELA_W, C_LINE);
  const int ry = TELA_H - BARRA_H / 2;
  tft.fillRoundRect(M, ry - 13, 26, 26, 7, CORES_MAPA[g_cfg.cor]);
  tft.setFont(&fonts::FreeSansBold18pt7b); tft.setTextDatum(middle_left); tft.setTextColor(C_INK);
  tft.drawString(g_cfg.nome, M + 40, ry);
  icComboio(tft, TELA_W - M - 92, ry, 26, C_INK3);
  tft.setTextDatum(middle_right); tft.setTextColor(C_INK3);
  char nq[12]; snprintf(nq, sizeof(nq), "%d", frotaQuantos(g_frota));
  tft.drawString(g_ses.ativa ? nq : "-", TELA_W - M, ry);
  // versao do firmware e do protocolo, em voz baixa: e o que se confere quando
  // duas telas nao se veem
  tft.setFont(&fonts::FreeSans9pt7b); tft.setTextDatum(middle_center); tft.setTextColor(C_INK3);
  char vs[24]; snprintf(vs, sizeof(vs), "fw %d  proto %d", MTS_VERSAO, PROTO_VERSAO);
  tft.drawString(vs, TELA_W / 2, ry);
  tft.setFont(&fonts::Font0);
}
