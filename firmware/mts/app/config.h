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
  uint8_t aj = cfgValida(g_cfg);
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
