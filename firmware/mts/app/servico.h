#pragma once
// ============================================================================
//  SERVICO - o unico ponto por onde TODA tela serve o aparelho
// ============================================================================
//
// POR QUE ISTO E UMA FUNCAO SO (campo de 2026-09-28): o log da trilha voltou
// com 57 linhas em 27 min, porque o diario so rodava no gancho do esperaToque -
// e a tela da trilha nao usa esperaToque. Agora a regra e uma: quem precisa
// rodar periodicamente entra AQUI, e TODO laco de tela chama servico(). Se
// amanha aparecer um quarto laco, ele chama servico() e pronto.
//
// ORDEM DENTRO DO SERVICO, e por que:
//   1. relogio  - se voltou, desloca as ancoras ANTES de o radio calcular fase
//   2. GPS      - consome o NMEA (para a UART nao transbordar) e casa o PPS
//   3. minha posicao -> frota[0] e trajeto (o radio monta o pacote disto)
//   4. rede     - vigia, RX, TX no slot, ruido
//   5. frota    - envelhece (TTLs)
//   6. wifi     - so se compilado, e so se a rede diz que pode
//   7. diario   - linha E a 1 Hz, G quando muda, flush por orcamento
//   8. caixa-preta fecha a volta; watchdog alimentado
//
// ORCAMENTO: o servico mede a propria volta (orcamentoVolta) e o mapa pergunta
// servicoCabe(custo) antes de pintar. E a inversao de prioridade que tirou o
// 'perdi' de 10-30 % da tela gino.

#include <Arduino.h>
#include "../core/params.h"
#include "../core/orcamento.h"
#include "../hal/pinos.h"
#include "../hal/relogio.h"
#include "../hal/caixa.h"
#include "../hal/wdt.h"
#include "estado.h"
#include "registro.h"
#include "rede.h"
#include "servico_api.h"
#if MTS_TEM_GPS
#include "../hal/gps.h"
#endif
#if MTS_TEM_WIFI
#include "wifi_ota.h"
#endif

static uint32_t g_regUltEstadoMs = 0;
static bool     g_regUltFix = false;
static uint32_t g_regUltPpsRun = 0;
static uint32_t g_regUltVersaoErrada = 0;

// A maquina de estados: a transicao vai para o diario e para o serial.
inline void estadoVai(AppEstado e)
{
  if (e == g_estado) return;
  registroLinha("!,%lu,tela,%s->%s", (unsigned long)millis(), EST_NOMES[g_estado], EST_NOMES[e]);
  g_estado = e;
}

// ---------------------------------------------------------------- posicao
// Minha posicao entra na frota[0] e no trajeto. Com GPS: gpsFixValido() e nao
// o flag cru, porque o fix envelhece. Sem GPS: simulador que anda de verdade
// (o rumo oscila, a posicao integra o rumo - como um carro).
inline void servicoPosicao()
{
#if MTS_TEM_GPS
  g_eu.fix = gpsFixValido();
  if (g_eu.fix) { g_eu.lat = g_gps.lat; g_eu.lon = g_gps.lon; }
  g_eu.sats = g_gps.sats;
  g_eu.rumo = g_gps.rumo;
  g_eu.vel  = (g_gps.velKmh > 254.0f) ? 255 : (uint8_t)(g_gps.velKmh + 0.5f);
#else
  static uint32_t t0 = 0; static double rumo = 0.6; static uint32_t passo = 0;
  if (millis() - t0 >= 1000) {
    t0 = millis(); passo++;
    const double V = 8.0;
    rumo += 0.10 * sin(passo * 0.13) + 0.06 * sin(passo * 0.041 + 1.7);
    g_eu.lat += cos(rumo) * V / 111320.0;
    g_eu.lon += sin(rumo) * V / (111320.0 * cos(g_eu.lat * M_PI / 180.0));
    g_eu.fix = true; g_eu.sats = 9;
    double rg = fmod(rumo * 180.0 / M_PI, 360.0); if (rg < 0) rg += 360.0;
    g_eu.rumo = (float)rg; g_eu.vel = (uint8_t)(V * 3.6 + 0.5);
  }
#endif
  Carro& eu = g_frota.c[0];
  eu.fix = g_eu.fix;
  if (g_eu.fix) {
    eu.lat = g_eu.lat; eu.lon = g_eu.lon; eu.rumo = g_eu.rumo; eu.vel = g_eu.vel;
    eu.temPos = true; eu.posMs = millis();
    trajetoTalvezAdicione(g_traj, g_eu.lat, g_eu.lon);
  }
}

// ---------------------------------------------------------------- diario
// E = estado, 1 Hz: a fotografia do no, com posicao e hora em toda linha.
inline void servicoLogEstado(uint32_t agoraMs)
{
  const Tdma& t = g_rede.tdma;
  uint32_t sdErr = 0;
#if MTS_TEM_SD
  sdErr = g_sd.erros;
#endif
  uint8_t sats = 0;
#if MTS_TEM_GPS
  sats = g_gps.sats;
#endif
  // DIARIO_E_COLUNAS campos (core/diario.h): a ordem e a do cabecalho.
  registroLinha("E,%lu,%lu,%.6f,%.6f,%d,%d,%s,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%u,%d,%lu,%lu,"
                "%lu,%.0f,%.0f,%lu,%lu,%d,%d,%lu,%lu,%d,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d",
    (unsigned long)agoraMs, (unsigned long)redeUtc(), g_eu.lat, g_eu.lon,
    (int)g_cfg.slot, (int)t.sync, t.isAnchor ? "EU" : "rede",
    (unsigned long)t.txCount, (unsigned long)t.missedTx, (unsigned long)g_rede.txPreso,
    (unsigned long)g_rede.rxOk, (unsigned long)g_rede.rxRuim, (unsigned long)g_frota.conflitos,
    (int)g_rede.rxRssi, (int)redePotencia(), (int)(g_eu.fix ? 1 : 0), (unsigned)sats,
    (int)(t.holdover ? 1 : 0), (unsigned long)g_orc.atrasoMaxUs, (unsigned long)orcamentoLimiteUs(t),
    (unsigned long)g_rede.seqPerdTotal, g_rede.ruidoUlt, g_rede.ruidoN ? (g_rede.ruidoSoma / g_rede.ruidoN) : 0.0f,
    (unsigned long)g_rede.txAirUltUs, (unsigned long)g_rede.txAirMaxUs, (int)g_rotaC.n, (int)g_rotaC.prefixo,
    (unsigned long)g_rede.catchups, (unsigned long)g_relogio.voltou, (int)redePotenciaConfig(),
    (int)(redeEmModoSeguro() ? 1 : 0), (unsigned long)g_reg.flushes, (unsigned long)g_reg.piorFlushUs,
    (unsigned long)g_reg.anel.descartadas, (unsigned long)g_frota.versaoErrada,
    (unsigned long)g_orc.negados, (unsigned long)g_orc.estouros, (unsigned long)sdErr, frotaQuantos(g_frota));
  orcamentoRelatou(g_orc);
}
// A linha E acima tem 40 campos: 1 tipo + 39 valores. Contado a mao contra o cabecalho.
static_assert(DIARIO_E_COLUNAS == 40, "a linha E do servico.h e o cabecalho de core/diario.h divergiram");

// G = GPS, so quando MUDA: fix indo e voltando e a hipotese central da instabilidade.
inline void servicoLogGps(uint32_t agoraMs)
{
#if MTS_TEM_GPS
  bool fix = g_eu.fix;
  bool mudouFix = (fix != g_regUltFix);
  bool caiuPps  = (g_rede.tdma.ppsRun < g_regUltPpsRun);
  if (mudouFix || caiuPps) {
    registroLinha("G,%lu,%s,%u,%u,%lu,%lu,%d", (unsigned long)agoraMs,
      mudouFix ? (fix ? "fix ON" : "fix OFF") : "pps reset",
      (unsigned)g_gps.sats, (unsigned)g_gps.vista, (unsigned long)g_rede.tdma.ppsRun,
      (unsigned long)gpsPpsCnt(), (int)g_rede.tdma.sync);
    g_regUltFix = fix;
  }
  g_regUltPpsRun = g_rede.tdma.ppsRun;
#endif
}

// ---------------------------------------------------------------- o servico
void servico()
{
  const uint32_t agora = millis();

  // 1) relogio
  uint64_t salto = relogioGuarda();
  if (salto) redeRelogioVoltou(salto);

  // 2) GPS
#if MTS_TEM_GPS
  caixaEtapa(CP_GPS);
  gpsAtualiza();
  caixaEtapa(CP_LACO);
#endif
  // 3) minha posicao
  servicoPosicao();
  // 4) rede
  redeAtualiza();
  // 5) frota envelhece; aviso de versao incompativel. Com o relogio LIDO DE NOVO:
  //    o redeAtualiza acima carimba quem chegou com um millis() posterior ao
  //    'agora' do comeco desta volta (core/tempo.h conta o estrago que isso fez).
  const uint32_t agoraFrota = millis();
  frotaEnvelhece(g_frota, agoraFrota);
  vizinhosEnvelhece(g_viz, agoraFrota);
  if (g_frota.versaoErrada != g_regUltVersaoErrada) {
    g_regUltVersaoErrada = g_frota.versaoErrada;
    char t[64];
    snprintf(t, sizeof(t), "APARELHO COM FIRMWARE INCOMPATIVEL NO AR (proto v%u)", (unsigned)g_frota.versaoErradaUlt);
    avisoTmp(t, agora, 30000);
  }
  // 6) wifi
#if MTS_TEM_WIFI
  caixaEtapa(CP_WIFI);
  wifiAtualiza(g_cfg.nome);
  caixaEtapa(CP_LACO);
#endif

  // 7) diario e orcamento
  orcamentoVolta(g_orc, micros(), orcamentoLimiteUs(g_rede.tdma));
  caixaVolta(g_rede.tdma.txCount, g_rede.rxOk, g_rede.txPreso, g_reg.flushes,
             redeUtc(), redePotencia(), (uint8_t)g_rede.txTipo, g_rede.txDesdeMs);
  if (registroAtivo()) {
    servicoLogGps(agora);
    if (agora - g_regUltEstadoMs >= 1000) { g_regUltEstadoMs = agora; servicoLogEstado(agora); }
    registroTick(g_orc, g_rede.tdma, redeTxEmVoo(), agora);
  }

  // 8) o boot vingou; watchdog
  static bool bootOk = false;
  if (!bootOk && agora > BOOT_VINGOU_MS) { bootOk = true; caixaBootOk(); }
  wdtAlimenta();
}

bool servicoCabe(uint32_t custoUs)
{
#if MTS_TEM_RADIO
  return orcamentoCabe(g_orc, g_rede.tdma, redeTxEmVoo(), custoUs);
#else
  (void)custoUs; return true;
#endif
}
