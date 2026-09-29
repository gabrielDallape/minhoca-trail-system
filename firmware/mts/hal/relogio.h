#pragma once
// ============================================================================
//  RELOGIO - o tempo monotonico do aparelho, e a guarda contra ele voltar
// ============================================================================
//
// O carimbo e de 64 BITS (esp_timer), nunca micros(): o micros() do Arduino e o
// esp_timer truncado a 32 bits, estoura em 71,6 min, e o tdma_core compara os
// carimbos com o relogio de 64 bits - a rede funcionava na bancada e morria
// inteira depois de uma hora e dez de trilha.
//
// RELOGIO QUE VOLTA. O esp_timer e monotonico por construcao; se ele andou para
// tras, algo de hardware aconteceu (a FaustoA registrou millis 38639 -> 6098 no
// mesmo segundo UTC antes de um reset por watchdog, 2026-09-28). Nao ha como
// consertar a causa daqui, mas da para nao piorar: detectar, contar, e devolver
// o tamanho do salto para quem tem carimbos absolutos (ancora do TDMA, inicio do
// TX) desloca-los junto e preservar a FASE da rede.

#include <stdint.h>
#include "esp_timer.h"

inline uint64_t relogioUs() { return (uint64_t)esp_timer_get_time(); }
inline uint32_t relogioMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

struct Relogio {
  uint64_t ultUs;
  uint32_t voltou;       // quantas vezes andou para tras
  uint64_t ultSaltoUs;   // tamanho do ultimo salto
};
static Relogio g_relogio = { 0, 0, 0 };

// Chamar uma vez por volta do servico. Devolve o salto (us) se o relogio voltou,
// 0 se nao.
inline uint64_t relogioGuarda()
{
  uint64_t agora = relogioUs();
  uint64_t salto = 0;
  if (g_relogio.ultUs && agora + 1000 < g_relogio.ultUs) {
    salto = g_relogio.ultUs - agora;
    g_relogio.voltou++;
    g_relogio.ultSaltoUs = salto;
  }
  g_relogio.ultUs = agora;
  return salto;
}
