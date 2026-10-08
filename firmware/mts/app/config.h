#pragma once
// ============================================================================
//  CONFIG - carrega, migra, valida e grava a configuracao (core/cfg + hal/nvs)
// ============================================================================

#include "estado.h"
#include "../core/cfg.h"
#include "../hal/nvs.h"

// Devolve true se migrou ou corrigiu algo (o chamador registra no diario).
inline bool configCarrega(char* relato, size_t n)
{
  uint8_t esquema = 0, pwrv = 0;
  nvsLeCfg(g_cfg, esquema, pwrv);
  uint8_t novo = esquema;
  bool migrou = cfgMigra(g_cfg, esquema, pwrv, novo);
  const uint8_t slotLido = g_cfg.slot;
  uint8_t aj = cfgValida(g_cfg);
  // SLOT QUE DEIXOU DE EXISTIR (v200 tinha 8, v201 tem 4): o aparelho cai no 0 e
  // colide com quem ja esta la. Faixa na tela ate o dono escolher na configuracao
  // - o valor 0 e gravado, entao o aviso aparece neste boot e no relato do diario.
  if (aj & CFG_AJ_SLOT) {
    char av[64];
    snprintf(av, sizeof(av), "SLOT %u NAO EXISTE MAIS - FOI PARA 0. CONFIRA NA CONFIGURACAO", (unsigned)slotLido);
    avisoDefine(av);
    Serial.printf("cfg: *** %s ***\n", av);
  }
  if (migrou || aj) {
    snprintf(relato, n, "cfg: esquema %u -> %u%s%s%s%s%s%s%s", (unsigned)esquema, (unsigned)novo,
             migrou ? " migrado" : "",
             (aj & CFG_AJ_NOME) ? " nome" : "", (aj & CFG_AJ_COR) ? " cor" : "",
             (aj & CFG_AJ_TEMA) ? " tema" : "", (aj & CFG_AJ_ZOOM) ? " zoom" : "",
             (aj & CFG_AJ_SLOT) ? " slot" : "", (aj & CFG_AJ_PWR) ? " pwr" : "");
    nvsGravaCfg(g_cfg, novo);
  } else {
    relato[0] = 0;
  }
  g_cfgEsquema = novo;
  return migrou || aj;
}

inline void configGrava() { nvsGravaCfg(g_cfg, g_cfgEsquema); }
