#pragma once
// ============================================================================
//  AIRTIME - quanto tempo um pacote LoRa ocupa o canal (formula do datasheet)
// ============================================================================
//
// SX1261/2 datasheet, 6.1.4. CALCULADO pelos parametros de params.h, nao cravado.
// Em SF7 o valor medido (58,0 ms para 16 B) ficou 6,5 ms acima do teorico: sao os
// comandos SPI e a rampa do PA, que ocupam o canal do mesmo jeito. Esses 6,5 ms
// entram como AIRTIME_EXTRA_US. A linha T do diario traz o air-time MEDIDO de cada
// transmissao (startTransmit -> TX_DONE): se vier sistematicamente diferente disto,
// e este numero que se corrige, nunca o contrario.
//
//   Ts        = 2^SF / BW                         4,096 ms em SF9/BW125
//   preambulo = (RF_PRE + 4,25) x Ts              50,2 ms
//   carga     = 8 + ceil((8xPL - 4xSF + 28 + 16) / (4xSF)) x CR   simbolos
//               16 B -> 28 simbolos (114,7 ms)    68 B -> 88 (360,4 ms)
//
// O LDRO (low data rate optimize) so entra com Ts >= 16,38 ms (SF11+ em BW125),
// que e o criterio da propria RadioLib; em SF9 nao muda nada, mas a formula sabe.

#include <stdint.h>
#include "params.h"

#define AIRTIME_EXTRA_US 6500UL

constexpr uint32_t airSimboloUs()
{
  return (uint32_t)(((uint64_t)(1UL << RF_SF) * 1000000ULL) / (uint32_t)(RF_BW_KHZ * 1000.0f));
}
constexpr bool airLdro() { return airSimboloUs() >= 16380; }
constexpr int32_t airSfEfetivo() { return RF_SF - (airLdro() ? 2 : 0); }
constexpr uint32_t airSimbolosCarga(uint32_t bytes)
{
  return 8 + (uint32_t)(((8 * (int32_t)bytes - 4 * RF_SF + 28 + 16) <= 0) ? 0
         : ((8 * (int32_t)bytes - 4 * RF_SF + 28 + 16) + 4 * airSfEfetivo() - 1) / (4 * airSfEfetivo())) * RF_CR;
}
constexpr uint32_t airtimeUs(uint32_t bytes)
{
  return ((4 * RF_PRE + 17) * airSimboloUs()) / 4
       + airSimbolosCarga(bytes) * airSimboloUs() + AIRTIME_EXTRA_US;
}

static_assert(airtimeUs(16) > 160000UL && airtimeUs(16) < 180000UL,
              "a formula do air-time saiu do esperado para 16 B em SF9 (~171 ms)");
static_assert(airtimeUs(68) > 400000UL && airtimeUs(68) < 430000UL,
              "a formula do air-time saiu do esperado para 68 B em SF9 (~417 ms)");
