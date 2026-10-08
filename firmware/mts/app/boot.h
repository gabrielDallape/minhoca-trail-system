#pragma once
// ============================================================================
//  BOOT - o que o aparelho faz ao ligar, SEM a tela. O mts.ino e o simulador
//  (tools/sim) chamam as mesmas funcoes, na mesma ordem.
// ============================================================================
//
//   appBootNucleo()  config, frota, rota, caixa-preta, epoca, alarme de
//                    reinicios, watchdog, radio. ANTES do painel: o radio sobe
//                    aqui porque estar no ar e estado do aparelho, nao da tela.
//   appBootDiario()  depois do cartao montado: abre o diario e grava o boot.
//   (a sessao guardada: sessaoRecuperaNoBoot(), app/sessao.h)
//
// O painel, o splash e o fundo de mapa ficam no mts.ino - sao a casca.

#include <stdint.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#include "../core/params.h"
#include "../core/frota.h"
#include "../core/rota.h"
#include "../core/trajeto.h"
#include "../core/orcamento.h"
#include "../hal/caixa.h"
#include "../hal/wdt.h"
#include "../hal/nvs.h"
#include "estado.h"
#include "config.h"
#include "registro.h"
#include "rede.h"
#if MTS_TEM_GPS
#include "../hal/gps.h"
#endif

struct BootInfo {
  esp_reset_reason_t motivo;
  bool       temCaixa;          // havia caixa-preta valida de um reset violento
  CaixaPreta caixaAnterior;
  bool       seguro;            // alarme de reinicios seguidos (so aviso)
  char       relatoCfg[96];     // o que a migracao/validacao da config corrigiu
};
static BootInfo g_boot;

inline void appRelataCaixa(const CaixaPreta& c, esp_reset_reason_t motivo, bool paraLog)
{
  const int rom = (int)esp_rom_get_reset_reason(0);
  if (paraLog) {
    registroLinha("C,%lu,motivo %d,rom 0x%02X,seguidos %lu,etapa %s,antes %s,ha %lu ms,ultimoMs %lu,tx %lu,rx %lu,presos %lu,flushes %lu,utc %lu,pwr %d,txTipo %c,txHa %lu ms",
                  (unsigned long)millis(), (int)motivo, rom, (unsigned long)c.resetsViolentos,
                  caixaNomeEtapa(c.etapa), caixaNomeEtapa(c.etapaAnt), (unsigned long)(c.ultimoMs - c.etapaMs),
                  (unsigned long)c.ultimoMs, (unsigned long)c.txCount, (unsigned long)c.rxCount, (unsigned long)c.presos,
                  (unsigned long)c.flushes, (unsigned long)c.utc, (int)c.pwr, c.txTipo ? (char)c.txTipo : '-',
                  (unsigned long)(c.txTipo ? (c.ultimoMs - c.txDesdeMs) : 0));
    return;
  }
  Serial.printf("\n*** CAIXA-PRETA: o boot anterior morreu (motivo %d, rom 0x%02X, %lu seguidos) ***\n", (int)motivo, rom, (unsigned long)c.resetsViolentos);
  Serial.printf("***   etapa em curso: %s (antes: %s), iniciada ha %lu ms; ultima volta em %lu ms\n",
                caixaNomeEtapa(c.etapa), caixaNomeEtapa(c.etapaAnt), (unsigned long)(c.ultimoMs - c.etapaMs), (unsigned long)c.ultimoMs);
  Serial.printf("***   tx=%lu rx=%lu presos=%lu flushes=%lu utc=%lu pwr=%d tx em voo=%c ha %lu ms\n",
                (unsigned long)c.txCount, (unsigned long)c.rxCount, (unsigned long)c.presos, (unsigned long)c.flushes,
                (unsigned long)c.utc, (int)c.pwr, c.txTipo ? (char)c.txTipo : '-', (unsigned long)(c.txTipo ? (c.ultimoMs - c.txDesdeMs) : 0));
  Serial.println("***   motivo: 4=PANIC 5=INT_WDT 6=TASK_WDT 7=outro WDT (LP/RTC/super) 9=BROWNOUT 14=PWR_GLITCH");
  Serial.println("***   rom (P4): 0x07 MWDT 0x09/0x0D RWDT 0x0F brownout 0x10 LP_WDT 0x12 super WDT 0x13 glitch 0x1B PSDET\n");
}

inline void appBootNucleo()
{
  BootInfo& b = g_boot;
#if MTS_TEM_GPS
  // ANTES do painel: a primeira fixacao e o gargalo de ligar o aparelho.
  gpsInicia();
  Serial.printf("gps: Serial1 a %d bps (RX=%d TX=%d PPS=%d)\n", GPS_BAUD, PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_PPS);
#endif

  configCarrega(b.relatoCfg, sizeof(b.relatoCfg));
  if (b.relatoCfg[0]) Serial.println(b.relatoCfg);
  frotaInicia(g_frota);
  frotaIdentidade(g_frota, g_cfg.nome, g_cfg.cor, g_cfg.slot);
  rotaLimpaCore(g_rotaC);
  orcamentoInicia(g_orc);

  // CAIXA-PRETA DO BOOT ANTERIOR: le antes de qualquer coisa que possa demorar.
  b.motivo = esp_reset_reason();
  uint32_t violentos = 0;
  b.temCaixa = caixaBoot(b.motivo, b.caixaAnterior, violentos);
  // Depois do caixaBoot: a epoca grava na NVS, e o carimbo CP_NVS nao pode cair
  // na caixa do boot anterior antes de ela ser lida.
  trajetoInicia(g_traj, nvsProximaEpoca((uint8_t)(esp_random() & 0xFF)));   // epoca nova a cada boot
  if (b.temCaixa) appRelataCaixa(b.caixaAnterior, b.motivo, false);
  else if (caixaMotivoViolento(b.motivo))
    Serial.printf("*** o boot anterior morreu (motivo %d, rom 0x%02X) e a caixa-preta nao estava valida ***\n",
                  (int)b.motivo, (int)esp_rom_get_reset_reason(0));
  // ALARME DE REINICIOS: so avisa. A potencia continua a do usuario.
  b.seguro = false;
  if (violentos >= RADIO_SEGURO_APOS) {
    b.seguro = true;
    char av[64];
    snprintf(av, sizeof(av), "%lu REINICIOS SEGUIDOS - CONFERIR O 5V DO RADIO", (unsigned long)violentos);
    avisoDefine(av);
    Serial.printf("rede: *** %s (potencia %d dBm, NAO alterada) ***\n", av, (int)g_cfg.pwr);
  }

  Serial.printf("wdt: %s (%d s, loopTask)\n", wdtInicia() ? "ativo" : "NAO configurou", WDT_TIMEOUT_S);

  // O radio sobe AQUI, nao ao entrar na trilha: estar no ar e estado do aparelho.
  // ATENCAO: daqui em diante ele TRANSMITE quando chegar o slot. Antena antes.
  {
    Sessao s0; nvsLeSessao(s0);
    redeInicia(g_cfg.slot, s0.ativa ? s0.gCod : "SEMSALA", g_cfg.pwr, b.seguro);
  }
}

// Com o cartao montado (ou nao): o diario e a linha de boot.
inline void appBootDiario()
{
  BootInfo& b = g_boot;
  uint32_t bootId = nvsIncrementaBoots();
  if (registroInicia(redeUtc(), bootId)) Serial.printf("log: %s\n", registroCaminho());
  else Serial.println("log: NAO ABRIU - o teste vai voltar sem dado (confira o cartao)");
  registroLinha("!,%lu,boot,%s,slot %d,pwr %d,motivo %d,rom 0x%02X,pwrCfg %d,seguro %d,fw %d,proto %d,epoca %u,cfgv %u",
                (unsigned long)millis(), g_cfg.nome, (int)g_cfg.slot, (int)redePotencia(), (int)b.motivo,
                (int)esp_rom_get_reset_reason(0), (int)g_cfg.pwr, b.seguro ? 1 : 0, (int)MTS_VERSAO, (int)PROTO_VERSAO,
                (unsigned)g_traj.epoca, (unsigned)g_cfgEsquema);
  if (b.relatoCfg[0]) registroLinha("!,%lu,%s", (unsigned long)millis(), b.relatoCfg);
  if (b.temCaixa) appRelataCaixa(b.caixaAnterior, b.motivo, true);
  if (avisoAtivo()) registroLinha("!,%lu,AVISO,%s", (unsigned long)millis(), g_avisoTexto);
}
