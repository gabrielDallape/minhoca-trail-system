#pragma once
// ============================================================================
//  RADIO (SIMULADOR) - substitui firmware/mts/hal/radio.h na arvore do simulador.
//  MESMA interface (radioHw*), mesma semantica do SX1262 que o app/rede.h usa:
//  um TX leva airtimeUs(n) e termina em TX_DONE; um pacote recebido chega como
//  RX_DONE carimbado no FIM dele; transmitindo, o chip nao ouve ninguem.
// ============================================================================
#include "pinos.h"
#include "caixa.h"
#include "wdt.h"
#include "../core/params.h"
#include "../core/airtime.h"

#define RADIOLIB_ERR_NONE            0
#define RADIOLIB_ERR_CHIP_NOT_FOUND  (-2)
#define RADIOLIB_SX126X_IRQ_TX_DONE  0x0001u
#define RADIOLIB_SX126X_IRQ_RX_DONE  0x0002u

static volatile bool     g_loraIrq = false;
static volatile uint64_t g_loraIrqUs = 0;

struct RadioHw {
  bool    ok = false;
  bool    fiosOk = false;
  int     beginSt = 999;
  int8_t  pwr = RF_PWR_PADRAO;
  uint16_t ultErrChip = 0;
  uint32_t rebegins = 0;
};
static RadioHw g_rhw;

// O "chip": o simulador liga e desliga a ENERGIA (o 5 V do E22) por cenario.
struct SimChip {
  bool     energia = true;
  bool     emTx = false;
  uint64_t txFimLocal = 0;
  uint32_t flags = 0;
  uint8_t  pkt[256];
  int      pktN = 0;
  float    rssi = 0, snr = 0;
};
static SimChip s_chip;

static void simTransmiteNoAr(const uint8_t* b, int n, uint64_t durUs);   // node_api.h

inline bool radioHwBegin(int8_t pwr, bool avisaMudo = true)
{
  if (!s_chip.energia) {
    g_rhw.beginSt = RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (avisaMudo) Serial.println("radio: chip nao responde ao GetStatus - begin() NAO tentado (sim: sem energia)");
    return false;
  }
  g_rhw.beginSt = 0; g_rhw.pwr = pwr;
  s_chip.emTx = false; s_chip.flags = 0;
  return true;
}
inline bool radioHwInicia(int8_t pwr) { g_rhw.fiosOk = true; g_rhw.ok = radioHwBegin(pwr); return g_rhw.ok; }
inline bool radioHwTentaDeNovo(int8_t pwr) { g_rhw.ok = radioHwBegin(pwr, false); return g_rhw.ok; }
inline bool radioHwRebegin(int8_t pwr)
{
  caixaEtapa(CP_TX_VIGIA); g_rhw.rebegins++;
  bool ok = radioHwBegin(pwr);
  caixaEtapa(CP_LACO);
  return ok;
}
inline bool radioHwBusyLivre() { return s_chip.energia; }

// Custos de SPI medidos na ordem de grandeza da placa (SPI de 2 MHz + comandos do
// RadioLib): o standby + escrever o FIFO + SetTx, e o readData + RSSI/SNR/ferr.
static const uint64_t SIM_CUSTO_TX_US = 700;
static const uint64_t SIM_CUSTO_RX_US = 900;
static const uint64_t SIM_CUSTO_IRQ_US = 60;

inline int radioHwTransmite(const uint8_t* buf, int n)
{
  caixaEtapa(CP_TX_START);
  simAvanca(SIM_CUSTO_TX_US);
  if (!s_chip.energia) { caixaEtapa(CP_LACO); return RADIOLIB_ERR_CHIP_NOT_FOUND; }
  const uint64_t dur = airtimeUs((uint32_t)n);
  s_chip.emTx = true;
  s_chip.txFimLocal = s_us + dur;
  simTransmiteNoAr(buf, n, dur);
  caixaEtapa(CP_TX_VOO);
  return RADIOLIB_ERR_NONE;
}

inline bool radioHwIrqPega(uint64_t& us)
{
  if (!g_loraIrq) return false;
  g_loraIrq = false; us = g_loraIrqUs;
  return true;
}
inline void radioHwIrqForca(uint64_t us) { g_loraIrqUs = us; g_loraIrq = true; }
inline uint32_t radioHwIrqFlags() { simAvanca(SIM_CUSTO_IRQ_US); return s_chip.flags; }
inline bool radioHwTxDoneNoChip() { return (s_chip.flags & RADIOLIB_SX126X_IRQ_TX_DONE) != 0; }
inline void radioHwFimTx() { s_chip.flags &= ~RADIOLIB_SX126X_IRQ_TX_DONE; caixaEtapa(CP_LACO); }
inline void radioHwRx() { s_chip.flags &= ~RADIOLIB_SX126X_IRQ_RX_DONE; }

inline int radioHwLeRx(uint8_t* buf, int max, int& n, float& rssi, float& snr, float& ferr)
{
  n = s_chip.pktN; rssi = s_chip.rssi; snr = s_chip.snr; ferr = 0;
  s_chip.flags &= ~RADIOLIB_SX126X_IRQ_RX_DONE;
  if (n <= 0 || n > max) { simAvanca(SIM_CUSTO_RX_US); return -1; }
  memcpy(buf, s_chip.pkt, (size_t)n);
  simAvanca(SIM_CUSTO_RX_US);        // DEPOIS da copia: o proximo pacote nao pisa neste
  return RADIOLIB_ERR_NONE;
}

inline float radioHwRssiInst() { return (float)ar::cfg.ruidoDbm; }
inline uint16_t radioHwErrosChip() { return 0; }
inline const char* radioHwErroNome(uint16_t err, char* fora, size_t n) { snprintf(fora, n, "0x%04X (sim)", err); return fora; }
inline void radioHwAplicaPotencia(int8_t pwr) { g_rhw.pwr = pwr; }
inline int8_t radioHwPotenciaAntena(int8_t pwr) { return (int8_t)(pwr + RADIO_GANHO_PA_DB); }
