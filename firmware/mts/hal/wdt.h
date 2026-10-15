#pragma once
// ============================================================================
//  WDT - o watchdog de tarefa, configurado de proposito e nao por acaso
// ============================================================================
//
// O core Arduino sobe o task WDT (5 s) vigiando so as tarefas IDLE; o laco
// principal (loopTask) NAO e vigiado por padrao. Os resets do campo de 09-28
// foram "outro WDT" (motivo 7: LP/RTC/super watchdog), nao este - mas um laco
// preso num while sem timeout (o startTransmit do RadioLib com BUSY alto) so
// seria pego por um vigia que olhe a loopTask.
//
// Aqui: a loopTask entra no task WDT com WDT_TIMEOUT_S e e alimentada em UM
// lugar so, o servico (app/servico.h), que toda tela chama. Quem bloquear mais
// que isso sem servir - e portanto sem servir o radio - merece o reset, e a
// caixa-preta (hal/caixa.h) diz em que etapa foi.
//
// Excecao consciente: o download do OTA em fatias (app/wifi_ota.h) alimenta o
// vigia dentro do proprio laco, porque nao pode chamar o servico (gravar flash
// com o radio no ar e o que se quer evitar) e leva mais de 10 s.

#include <stdint.h>
#include "esp_task_wdt.h"
#include "esp_err.h"
#include "../core/params.h"

static bool g_wdtAtivo = false;

inline bool wdtInicia()
{
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WDT_TIMEOUT_S * 1000;
  // 0 = as tarefas IDLE saem do vigia (o reconfigure as desinscreve). De
  // proposito: o laco do Arduino gira sem ceder a CPU e mataria a IDLE do seu
  // nucleo de fome - o vigia aqui e SO da loopTask.
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;        // panic -> esp_reset_reason() = TASK_WDT e backtrace
  esp_err_t e = esp_task_wdt_reconfigure(&cfg);
  if (e == ESP_ERR_INVALID_STATE) e = esp_task_wdt_init(&cfg);   // nao estava inicializado
  if (e != ESP_OK) return false;
  if (esp_task_wdt_add(NULL) != ESP_OK && esp_task_wdt_status(NULL) != ESP_OK) return false;
  g_wdtAtivo = true;
  return true;
}

inline void wdtAlimenta() { if (g_wdtAtivo) esp_task_wdt_reset(); }

// Para as chamadas de biblioteca que PODEM passar do timeout sem que haja nada
// travado - hoje so o begin() do RadioLib, que com o chip mudo fica >= 10 s no
// findChip (10 resets x 1 s de standby tentando, SX126x.cpp:1506 e :203).
// Quem pausa retoma na mesma funcao, sem return no meio.
inline void wdtPausa()  { if (g_wdtAtivo) esp_task_wdt_delete(NULL); }
inline void wdtRetoma() { if (g_wdtAtivo) { esp_task_wdt_add(NULL); esp_task_wdt_reset(); } }
