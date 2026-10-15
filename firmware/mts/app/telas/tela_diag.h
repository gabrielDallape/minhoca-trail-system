#pragma once
// ============================================================================
//  TELA DE DIAGNOSTICO - numeros crus, sem interpretacao bonita
// ============================================================================
//
// POR QUE EXISTE. Em 2026-08-28 tres aparelhos foram para a trilha e dois nao
// se falaram; nada na tela dizia o porque. Na trilha nao ha serial nem WiFi.
// Verde e o esperado, vermelho e o que explica a falha. Tres colunas x cinco
// linhas: TODO contador de erro que existe no firmware aparece aqui e na linha
// E do diario - se um numero nao esta aqui, ele nao existe.
//
// A celula da POTENCIA e tocavel: esse numero mora na NVS e sobrevive a
// regravacao; sem um controle na tela a unica forma de corrigi-lo era um cabo.

#include "ui.h"
#include "../estado.h"
#include "../config.h"
#include "../rede.h"
#include "../registro.h"
#include "../../hal/relogio.h"
#if MTS_TEM_GPS
#include "../../hal/gps.h"
#endif
#if MTS_TEM_SD
#include "../../hal/sd.h"
#endif

template <typename TFT>
void telaDiag(TFT& tft)
{
  const int COLS = 3, LINS = 5, GAPX = 10, GAPY = 8;
  const int topo = CAB_H + 12;
  const int base = TELA_H - 48 - 64;
  const int cw   = (TELA_W - 2 * M - (COLS - 1) * GAPX) / COLS;
  const int ch   = (base - topo - 12 - (LINS - 1) * GAPY) / LINS;
  Ret rVolta = { M, (int16_t)base, 240, 64 };
  Ret rPwr   = { (int16_t)(M + cw + GAPX), (int16_t)topo, (int16_t)cw, (int16_t)ch };

  auto celula = [&](int col, int lin, const char* rot, const char* val, uint16_t cor) {
    const int x = M + col * (cw + GAPX), y = topo + lin * (ch + GAPY);
    tft.fillRoundRect(x, y, cw, ch, 10, C_SURF);
    tft.setTextDatum(top_left); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK3);
    tft.drawString(rot, x + 12, y + 6);
    tft.setFont(&fonts::FreeSansBold12pt7b); tft.setTextColor(cor);
    tft.drawString(val, x + 12, y + ch - 30);
    tft.setFont(&fonts::Font0);
  };

  auto pintaCelulas = [&]() {
    char v[56];
    const uint32_t agora = millis();
#if MTS_TEM_RADIO
    const char* rRot; const char* rVal; uint16_t rCor;
    if (!g_rede.ok) {
      rRot = "RADIO - NAO SUBIU"; rCor = C_RED;
      switch (g_rhw.beginSt) {
        case -2:   rVal = "nao responde - ver 5V, NSS e MISO"; break;
        case -707: rVal = "TCXO - conferir 2,2 V";             break;
        case 999:  rVal = "nem chegou a iniciar";              break;
        default:   rVal = "falhou ao iniciar";                 break;
      }
    } else if (!g_rhw.fiosOk) { rRot = "RADIO - FIACAO SUSPEITA"; rVal = "BUSY e DIO1 trocados?"; rCor = C_WARN; }
    else { rRot = "RADIO"; rVal = "no ar"; rCor = C_OK; }
    celula(0, 0, rRot, rVal, rCor);

    // Mostra o que esta NO CHIP; se o pedido ainda nao entrou (TX em voo), diz.
    if (redePotencia() != redePotenciaConfig())
      snprintf(v, sizeof(v), "%d dBm -> %d pendente >", redePotencia(), redePotenciaConfig());
    else snprintf(v, sizeof(v), "%d dBm (~%d na antena) >", redePotencia(), radioHwPotenciaAntena(redePotencia()));
    const bool travando = g_rede.txPreso > 0 && g_rede.tdma.txCount > 0 && g_rede.txPreso * 2 > g_rede.tdma.txCount;
    celula(1, 0, travando ? "POTENCIA - ALTA DEMAIS?" : redeEmModoSeguro() ? "POTENCIA - REINICIOS SEGUIDOS" : "POTENCIA (tocar para trocar)",
           v, (travando || redeEmModoSeguro()) ? C_RED : C_OK);

    snprintf(v, sizeof(v), "%d de 0..%d | quadro %d s", g_cfg.slot, FROTA_MAX_CARROS - 1, TDMA_FRAME_S);
    celula(2, 0, "MEU SLOT", v, g_frota.conflitos ? C_RED : C_INK);

    uint16_t cs = C_OK;
    const Tdma& t = g_rede.tdma;
    if (!t.haveAnchor)                { strcpy(v, "NENHUMA - nao transmito"); cs = C_RED; }
    else if (t.sync == TDMA_SYNC_PPS) { strcpy(v, "PPS do GPS"); }
    else if (t.isAnchor)              { strcpy(v, t.anchorSrcPps ? "eu (UTC)" : "eu sou a referencia"); }
    else snprintf(v, sizeof(v), "do slot %u%s", t.anchorSrc, t.anchorSrcPps ? " (UTC)" : " (grupo)");
    if (t.holdover && t.haveAnchor) { strncat(v, " holdover", sizeof(v) - strlen(v) - 1); cs = C_WARN; }
    celula(0, 1, "SINCRONIA", v, cs);

    snprintf(v, sizeof(v), "%lu enviados, %lu perdidos", (unsigned long)t.txCount, (unsigned long)t.missedTx);
    celula(1, 1, "TRANSMITI", v, t.txCount ? C_OK : C_RED);

    snprintf(v, sizeof(v), "%lu bons, %lu erro, %lu perd.seq", (unsigned long)g_rede.rxOk, (unsigned long)g_rede.rxRuim, (unsigned long)g_rede.seqPerdTotal);
    celula(2, 1, "RECEBI", v, g_rede.rxOk ? C_OK : C_WARN);

    if (g_rede.rxOk) snprintf(v, sizeof(v), "%.0f dBm  SNR %.1f", g_rede.rxRssi, g_rede.rxSnr);
    else strcpy(v, "nunca ouvi ninguem");
    celula(0, 2, "SINAL DO ULTIMO PACOTE", v, !g_rede.rxOk ? C_WARN : (g_rede.rxRssi > -110 ? C_OK : C_WARN));

    // TX preso subindo junto com tx = o E22 apagando no meio da transmissao por
    // queda do 5 V. E defeito de alimentacao, nao de radio.
    snprintf(v, sizeof(v), "%lu presos, err 0x%04X, %lu rebegin", (unsigned long)g_rede.txPreso, g_rhw.ultErrChip, (unsigned long)g_rhw.rebegins);
    celula(1, 2, g_rede.txPreso ? "TX PRESO - CONFIRA O 5V!" : "TX PRESO", v, g_rede.txPreso ? C_RED : C_OK);

    if (g_rede.ruidoN) snprintf(v, sizeof(v), "%.0f dBm (med %.0f, %lu am.)", g_rede.ruidoUlt, g_rede.ruidoSoma / g_rede.ruidoN, (unsigned long)g_rede.ruidoN);
    else strcpy(v, "ainda nao medi");
    celula(2, 2, "RUIDO DE FUNDO (slot vazio)", v, g_rede.ruidoN ? (g_rede.ruidoUlt < -100 ? C_OK : C_WARN) : C_INK2);

    // VERSAO INCOMPATIVEL NO AR: o problema das telas v7/v8 do campo, agora visivel
    if (g_frota.versaoErrada) snprintf(v, sizeof(v), "%lu pacotes v%u (eu v%d), ha %lus", (unsigned long)g_frota.versaoErrada,
                                       (unsigned)g_frota.versaoErradaUlt, PROTO_VERSAO, (unsigned long)((agora - g_frota.versaoErradaMs) / 1000));
    else snprintf(v, sizeof(v), "nenhum (eu falo v%d)", PROTO_VERSAO);
    celula(0, 3, g_frota.versaoErrada ? "FIRMWARE INCOMPATIVEL NO AR!" : "PROTOCOLO", v, g_frota.versaoErrada ? C_RED : C_OK);

    snprintf(v, sizeof(v), "%lu outra sala, %lu conflito slot", (unsigned long)g_rede.rxForaDaSala, (unsigned long)g_frota.conflitos);
    celula(1, 3, "OUTROS NO AR", v, g_frota.conflitos ? C_RED : C_INK);

    snprintf(v, sizeof(v), "air %lu/%lu us | pior laco %lu us", (unsigned long)g_rede.txAirUltUs, (unsigned long)REDE_AIRTIME_US,
             (unsigned long)g_orc.atrasoMaxUs);
    celula(2, 3, "TEMPO (medido / calculado)", v, (g_rede.txAirUltUs && g_rede.txAirUltUs > REDE_AIRTIME_US + 20000) ? C_WARN : C_INK);
#endif
#if MTS_TEM_GPS
    if (gpsFixValido()) snprintf(v, sizeof(v), "fix com %u sat (vista %u)", g_gps.sats, g_gps.vista);
    else snprintf(v, sizeof(v), "SEM FIX (%u a vista, %lu ruins)", g_gps.vista, (unsigned long)g_gps.ruins);
    celula(0, 4, "GPS", v, gpsFixValido() ? C_OK : C_WARN);
    if (gpsPpsValido()) snprintf(v, sizeof(v), "1 Hz, %lu pulsos, %lu par velho", (unsigned long)gpsPpsCnt(), (unsigned long)g_gps.parVelho);
    else if (gpsPpsCnt()) snprintf(v, sizeof(v), "irregular (%lu pulsos)", (unsigned long)gpsPpsCnt());
    else strcpy(v, "sem pulso (normal sem fix)");
    celula(1, 4, "PPS", v, gpsPpsValido() ? C_OK : C_INK2);
#endif
    {
      uint32_t sdErr = 0;
#if MTS_TEM_SD
      sdErr = g_sd.erros;
#endif
      snprintf(v, sizeof(v), "%s %lu lin, %lu perd, sd %lu err, rel %lu", registroAtivo() ? "ok" : "PARADO",
               (unsigned long)g_reg.anel.linhas, (unsigned long)g_reg.anel.descartadas, (unsigned long)sdErr,
               (unsigned long)g_relogio.voltou);
      celula(2, 4, "DIARIO / CARTAO / RELOGIO", v, (!registroAtivo() || sdErr || g_relogio.voltou) ? C_WARN : C_OK);
    }
  };

  tft.fillScreen(C_BG);
  cabecalho(tft, "diagnostico", false);
  tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
  pintaCelulas();
  botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
  tft.setTextDatum(top_right); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK3);
  char rod[48]; snprintf(rod, sizeof(rod), "fw %d proto %d - atualiza 1x/s", MTS_VERSAO, PROTO_VERSAO);
  tft.drawString(rod, TELA_W - M, base + 22);
  tft.setFont(&fonts::Font0);

  uint32_t tAtu = millis();
  while (true) {
    int16_t x, y;
    if (esperaToque(tft, x, y, 250)) {
      if (dentro(rVolta, x, y)) return;
#if MTS_TEM_RADIO
      if (dentro(rPwr, x, y)) {
        // Degraus, nao 1 em 1 dBm: cada toque sobe um degrau, e depois do maior
        // volta ao MENOR (quem esta aqui quase sempre veio consertar um radio que
        // nao transmite). E comando explicito e vai para a NVS.
        static const int8_t DEGRAUS[] = { 2, 6, 10, 14, 17, 22 };
        const int nD = sizeof(DEGRAUS) / sizeof(DEGRAUS[0]);
        int i = 0;
        while (i < nD && DEGRAUS[i] <= redePotenciaConfig()) i++;
        if (i >= nD) i = 0;
        // COMANDO EXPLICITO: o unico caminho (com o 'p' do serial) que muda a potencia gravada.
        redeTrocaPotencia(DEGRAUS[i]);
        g_cfg.pwr = redePotenciaConfig();
        avisoDefine("");
        configGrava();
        g_rede.txPreso = 0; g_rede.tdma.txCount = 0;   // os presos velhos sao da potencia anterior
        pintaCelulas();
      }
#endif
      continue;
    }
    if (millis() - tAtu > 1000) { tAtu = millis(); pintaCelulas(); }
  }
}
