#pragma once
// ============================================================================
//  RADIO (hal) - o SX1262 do E22-900M30S, e SO o chip
// ============================================================================
//
// Este arquivo nao sabe o que e slot, sala, carro ou pacote. Ele liga o chip com
// TODAS as armadilhas ja pagas (lista abaixo), transmite um buffer, entrega o que
// chegou com RSSI/SNR, e responde "por que o TX nao completou". Quem decide QUANDO
// falar e app/rede.h sobre o tdma_core.
//
// AS ARMADILHAS, cada uma com a fonte (CLAUDE.md, docs/AUDITORIA_RADIO.md):
//   1. setPacketReceivedAction e setPacketSentAction sao A MESMA interrupcao no
//      SX126x. Aqui ha UMA ISR, e o laco pergunta ao chip qual evento foi lendo
//      getIrqFlags() ANTES de readData()/finishTransmit(), que limpam o registrador.
//   2. startTransmit() NAO volta para standby sozinho: chamado com o radio em RX
//      continuo devolve -1 sempre. Por isso o standby() antes de cada TX.
//   3. BUSY e DIO1 sao pads vizinhos (13/14) e GPIOs vizinhos (49/50). Trocados,
//      o begin() passa, o RX parece bom e NENHUMA transmissao completa.
//      radioHwChecaFios() descobre isso em 5 ms, no boot.
//   4. TCXO em 2,2 V (manual E22-M V1.2). NAO usar XTAL=true. -707 no boot e TCXO;
//      -707 em runtime e brownout.
//   5. O begin() chama setDio2AsRfSwitch(true) incondicionalmente: desliga-se
//      depois. O PA externo precisa de RXEN/TXEN de verdade, nunca juntos.
//   6. setCurrentLimit(140) DEPOIS do begin() (que grava 60 mA em tres pontos);
//      140 e o teto - acima o RadioLib recusa E NAO APLICA, em silencio.
//   7. setOutputPower(p, false): a paOptTable da RadioLib 7.5+ e para SX1262 nu
//      e nao e monotonica. Com PA externo, tabela desligada (como o Meshtastic).
//   8. setPaRampTime DEPOIS do setOutputPower, que reseta a rampa para 200 us.
//   9. clearDeviceErrors() no boot: XOSC_START_ERR e ESPERADO com TCXO e o
//      registrador e latcheado. Foi um GetDeviceErrors lido sem clear que
//      condenou o modulo da tela A em 2026-08-24.
//  10. startTransmit() termina num while(BUSY) SEM timeout (SX126x.cpp:1143).
//      radioHwBusyLivre() pergunta antes, com relogio nosso.
//  11. readData(buf, 0) significa "leia tudo" e estoura a pilha. O tamanho e
//      conferido contra o buffer antes.
//  12. Timeout de SPI de 100 ms em vez de 1000: um modulo apagado com BUSY alto
//      segurava cada comando por 1 s, e o begin() tem dezenas. Vale desde ANTES do
//      primeiro begin().
//  13. begin() com o chip MUDO leva >= 10 s (findChip: 10 resets, cada um com 1 s
//      de standby() insistindo) - mais que o WDT. Era boot-loop com o 5 V do
//      modulo caido. radioHwResponde() pergunta em microssegundos antes, e o
//      begin() roda com o vigia pausado.

#include <RadioLib.h>
#include <Arduino.h>
#include "pinos.h"
#include "caixa.h"
#include "wdt.h"
#include "../core/params.h"

// GetDeviceErrors/ClearDeviceErrors sao 'protected' no RadioLib; herdar custa
// quatro linhas e nao abre a biblioteca inteira (RADIOLIB_GODMODE).
class SX1262Mts : public SX1262 {
 public:
  SX1262Mts(Module* m) : SX1262(m) {}
  using SX126x::getDeviceErrors;
  using SX126x::clearDeviceErrors;
  using SX126x::getPacketType;
  Module* modulo() { return getMod(); }
};

static SPIClass  spiLoRa(FSPI);
static SX1262Mts g_lora = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_NRST,
                                     PIN_LORA_BUSY, spiLoRa,
                                     SPISettings(2000000, MSBFIRST, SPI_MODE0));

// A ISR so carimba (64 bits) e levanta a flag. Nada mais.
static volatile bool     g_loraIrq = false;
static volatile uint64_t g_loraIrqUs = 0;
void IRAM_ATTR loraOnIrq() { g_loraIrqUs = (uint64_t)esp_timer_get_time(); g_loraIrq = true; }

struct RadioHw {
  bool    ok = false;
  bool    fiosOk = false;
  int     beginSt = 999;        // 999 = nem tentou; 0 = subiu; -2 chip mudo; -707 TCXO
  int8_t  pwr = RF_PWR_PADRAO;  // o que esta PROGRAMADO no chip
  uint16_t ultErrChip = 0;      // ultimo GetDeviceErrors lido num TX que nao completou
  uint32_t rebegins = 0;        // begin() de recuperacao (vigia)
};
static RadioHw g_rhw;

// ------------------------------------------------------------------ fiacao
// Depois que o NRST e solto, o SX126x segura o BUSY ALTO por ~1 ms carregando a
// calibracao, enquanto o DIO1 fica baixo. Quem sobe e o BUSY - so isso identifica.
inline bool radioHwChecaFios()
{
  pinMode(PIN_LORA_NRST, OUTPUT);
  pinMode(PIN_LORA_BUSY, INPUT);
  pinMode(PIN_LORA_DIO1, INPUT);
  digitalWrite(PIN_LORA_NRST, LOW);
  delayMicroseconds(500);
  digitalWrite(PIN_LORA_NRST, HIGH);
  uint32_t nBusy = 0, nDio1 = 0, t0 = micros();
  while (micros() - t0 < 5000) {
    nBusy += digitalRead(PIN_LORA_BUSY);
    nDio1 += digitalRead(PIN_LORA_DIO1);
  }
  if (nDio1 > nBusy * 4) {
    Serial.println("radio: *** BUSY e DIO1 TROCADOS no chicote! O begin() vai passar, o RX vai "
                   "parecer bom e nenhuma transmissao vai completar. Troque os dois fios. ***");
    return false;
  }
  if (nBusy <= nDio1 * 4) {
    Serial.println("radio: fiacao inconclusiva - o modulo respondeu ao reset?");
    return false;
  }
  return true;
}

// A configuracao que o begin() NAO faz e que tem de ser refeita toda vez que ele
// roda (boot e vigia). Uma funcao so, para o vigia nao esquecer nada.
inline void radioHwPosBegin(int8_t pwr)
{
  g_lora.setDio2AsRfSwitch(false);                    // armadilha 5
#if RF_SWITCH_TROCADO
  g_lora.setRfSwitchPins(PIN_LORA_TXEN, PIN_LORA_RXEN);
#else
  g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
#endif
  g_lora.setCurrentLimit(RF_OCP_MA);                  // armadilha 6
  g_lora.setOutputPower(pwr, false);                  // armadilha 7
  // 800 us: o valor da variante T-Beam 1 W do Meshtastic ("requires >800us
  // stabilization"). O PA externo sobe a corrente devagar e a fonte acompanha.
  g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U); // armadilha 8
  g_lora.setRxBoostedGainMode(true);                  // ~2 dB de sensibilidade (I7)
  g_lora.setDio1Action(loraOnIrq);
  g_lora.clearDeviceErrors();                         // armadilha 9
  g_lora.modulo()->spiConfig.timeout = 100;           // armadilha 12
  g_rhw.pwr = pwr;
}

// O chip esta do outro lado do SPI? Reset, espera o BUSY cair (a calibracao de
// partida leva ~3 ms) e um GetStatus (0xC0) cru. Um SX1262 vivo responde com o
// modo em STBY_RC..TX (bits 6:4 = 2..6); MISO parado da 0x00/0xFF. Custa no maximo
// RADIO_SONDA_MS, contra >= 10 s do begin() num chip mudo (armadilha 13).
#define RADIO_SONDA_MS 50
inline bool radioHwResponde()
{
  pinMode(PIN_LORA_NSS, OUTPUT);
  digitalWrite(PIN_LORA_NSS, HIGH);
  pinMode(PIN_LORA_BUSY, INPUT);
  pinMode(PIN_LORA_NRST, OUTPUT);
  digitalWrite(PIN_LORA_NRST, LOW);
  delayMicroseconds(500);
  digitalWrite(PIN_LORA_NRST, HIGH);
  delayMicroseconds(200);
  const uint32_t t0 = millis();
  while (digitalRead(PIN_LORA_BUSY) && (millis() - t0) < RADIO_SONDA_MS) delay(1);
  if (digitalRead(PIN_LORA_BUSY)) return false;              // BUSY preso: sem energia ou travado
  spiLoRa.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LORA_NSS, LOW);
  spiLoRa.transfer(0xC0);                                    // GetStatus
  const uint8_t st = spiLoRa.transfer(0x00);
  digitalWrite(PIN_LORA_NSS, HIGH);
  spiLoRa.endTransaction();
  const uint8_t modo = (st >> 4) & 0x07;
  return modo >= 2 && modo <= 6;
}

inline bool radioHwBegin(int8_t pwr, bool avisaMudo = true)
{
  if (!radioHwResponde()) {
    g_rhw.beginSt = RADIOLIB_ERR_CHIP_NOT_FOUND;
    if (avisaMudo) Serial.println("radio: chip nao responde ao GetStatus - begin() NAO tentado (5 V, NSS, MISO, BUSY)");
    return false;
  }
  wdtPausa();
  int st = g_lora.begin(RF_FREQ_MHZ, RF_BW_KHZ, RF_SF, RF_CR, RF_SYNC, pwr, RF_PRE, RF_TCXO_V);
  wdtRetoma();
  g_rhw.beginSt = st;
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio: begin() FALHOU (%d)%s\n", st,
                  st == -707 ? " - -707 no boot = tensao do TCXO (RF_TCXO_V 2,2)" :
                  st == -2   ? " - -2 = chip mudo: 5 V, NSS e MISO" : "");
    return false;
  }
  radioHwPosBegin(pwr);
  g_lora.startReceive();
  return true;
}

inline bool radioHwInicia(int8_t pwr)
{
  Serial.println("radio: conferindo a fiacao...");
  g_rhw.fiosOk = radioHwChecaFios();     // avisa e segue: o begin diz se da para continuar
  spiLoRa.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  g_lora.modulo()->spiConfig.timeout = 100;           // armadilha 12, ja no primeiro begin()
  // M16: os pinos do switch entram ANTES do begin, para o RXEN nao flutuar
  // durante os ~100 ms dele (so o TXEN tem pull-down no chicote).
#if RF_SWITCH_TROCADO
  g_lora.setRfSwitchPins(PIN_LORA_TXEN, PIN_LORA_RXEN);
#else
  g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
#endif
  g_rhw.ok = radioHwBegin(pwr);
  return g_rhw.ok;
}

// O radio NAO RESPONDEU no boot: nova tentativa, calada se o chip continua mudo.
// Chamado pela rede a cada RADIO_RETENTA_MS. Custa o teste rapido (microssegundos
// com o BUSY baixo, ate RADIO_SONDA_MS com ele preso) - nunca o begin() cego.
inline bool radioHwTentaDeNovo(int8_t pwr)
{
  g_rhw.ok = radioHwBegin(pwr, false);
  return g_rhw.ok;
}

// Vigia: refaz o begin() completo. Um modulo que apagou por queda do 5 V
// esqueceu TODA a configuracao - so finishTransmit+startReceive o deixaria
// escutando na frequencia de fabrica, surdo para o grupo.
inline bool radioHwRebegin(int8_t pwr)
{
  caixaEtapa(CP_TX_VIGIA);
  g_rhw.rebegins++;
  bool ok = radioHwBegin(pwr);
  caixaEtapa(CP_LACO);
  return ok;
}

// O CHIP AINDA ESTA CONFIGURADO? Um E22 que perdeu o 5 V e voltou ENTRE dois TX
// reinicia com a configuracao de fabrica (pacote GFSK, outra frequencia, outro
// sync word). O TX seguinte COMPLETA normalmente - com os parametros errados - e
// a tela fica transmitindo para o nada e surda, sem nenhum TX preso para o vigia
// ver. Achado no simulador (tools/sim, cenario 'rapido'), 2026-10-01. Um
// GetPacketType custa um comando SPI: configurado = LoRa (0x01); de fabrica = 0x00.
inline bool radioHwConfigOk() { return g_lora.getPacketType() == RADIOLIB_SX126X_PACKET_TYPE_LORA; }

// DIAGNOSTICO: o nivel do pino DIO1 AGORA e os eventos pendentes no chip (SPI).
// Separa "a linha ficou alta com um evento parado" (ninguem limpou) de "o chip
// tem o evento mas o pino nao sobe" (o sinal nao chega a placa).
inline void radioHwDiag(int& dio1, uint32_t& flags) { dio1 = digitalRead(PIN_LORA_DIO1); flags = g_lora.getIrqFlags(); }

// ------------------------------------------------------------------- TX
// O BUSY PRECISA ESTAR BAIXO ANTES DE MANDAR TRANSMITIR (armadilha 10). 5 ms e
// folga larga: apos um comando o SX1262 solta o BUSY em dezenas de us; a espera
// mais longa legitima e a calibracao pos-reset, ~1 ms.
inline bool radioHwBusyLivre()
{
  caixaEtapa(CP_TX_BUSY);
  uint32_t t0 = micros();
  while (digitalRead(PIN_LORA_BUSY) && (micros() - t0) < 5000) { }
  bool livre = digitalRead(PIN_LORA_BUSY) == LOW;
  if (livre) caixaEtapa(CP_LACO);
  return livre;
}

// standby + startTransmit. Devolve o codigo do RadioLib (0 = no ar).
inline int radioHwTransmite(const uint8_t* buf, int n)
{
  caixaEtapa(CP_TX_START);
  g_lora.standby();                                   // armadilha 2
  int st = g_lora.startTransmit((uint8_t*)buf, n);
  if (st != RADIOLIB_ERR_NONE) { g_lora.startReceive(); caixaEtapa(CP_LACO); return st; }
  caixaEtapa(CP_TX_VOO);
  return st;
}

// Le e limpa a flag da ISR de forma atomica (M14).
inline bool radioHwIrqPega(uint64_t& us)
{
  noInterrupts();
  bool f = g_loraIrq;
  if (f) { g_loraIrq = false; us = g_loraIrqUs; }
  interrupts();
  return f;
}
inline void radioHwIrqForca(uint64_t us)
{
  noInterrupts();
  g_loraIrqUs = us; g_loraIrq = true;
  interrupts();
}

inline uint32_t radioHwIrqFlags() { return g_lora.getIrqFlags(); }
inline bool radioHwTxDoneNoChip() { return (g_lora.getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE) != 0; }

inline void radioHwFimTx()
{
  g_lora.finishTransmit();
  g_lora.startReceive();
  caixaEtapa(CP_LACO);
}
inline void radioHwRx() { g_lora.startReceive(); }

// Le o pacote recebido. Devolve o st do readData; n = tamanho anunciado; -1 se o
// tamanho nao cabe (armadilha 11) - e ai o pacote e descartado e o RX rearmado.
inline int radioHwLeRx(uint8_t* buf, int max, int& n, float& rssi, float& snr, float& ferr)
{
  n = (int)g_lora.getPacketLength();
  if (n <= 0 || n > max) {
    rssi = g_lora.getRSSI(); snr = g_lora.getSNR(); ferr = 0;
    g_lora.startReceive();
    return -1;
  }
  caixaEtapa(CP_RX_LE);
  int st = g_lora.readData(buf, n);
  caixaEtapa(CP_LACO);
  // RSSI/SNR/erro de frequencia SEMPRE, inclusive com CRC ruim: o pacote
  // corrompido a -95 dBm e o que diz se o CRC ruim e sinal fraco ou ruido.
  rssi = g_lora.getRSSI();
  snr  = g_lora.getSNR();
  ferr = g_lora.getFrequencyError();
  return st;
}

// RSSI instantaneo (GetRssiInst) - o ruido de fundo, lido com o radio em RX e
// ninguem falando.
inline float radioHwRssiInst()
{
  caixaEtapa(CP_RX_RSSI);
  float r = g_lora.getRSSI(false);
  caixaEtapa(CP_LACO);
  return r;
}

// PERGUNTE AO CHIP POR QUE FALHOU. A sequencia de um TX e XOSC -> PLL -> PA ->
// rampa -> pacote -> TX_DONE (datasheet 13.4.1). Nenhum bit aceso + TX que nao
// completou = o OCP cortou a corrente do PA (queda de tensao no 5 V).
inline uint16_t radioHwErrosChip()
{
  uint16_t err = g_lora.getDeviceErrors();
  g_rhw.ultErrChip = err;
  return err;
}
inline const char* radioHwErroNome(uint16_t err, char* fora, size_t n)
{
  snprintf(fora, n, "0x%04X%s%s%s%s", err,
           (err & (1u << 5)) ? " XOSC_NAO_PARTIU(TCXO)" : "",
           (err & (1u << 6)) ? " PLL_NAO_PRENDEU" : "",
           (err & (1u << 8)) ? " RAMPA_DO_PA" : "",
           err ? "" : " (nenhum -> suspeita de corte por OCP: 5V do modulo afundou)");
  return fora;
}

// Aplica a potencia no chip. O chamador GARANTE que nao ha TX em voo: trocar a
// potencia com TX em voo deu ESP_RST_PANIC na hora (medido 2026-09-08).
inline void radioHwAplicaPotencia(int8_t pwr)
{
  g_lora.standby();
  g_lora.setOutputPower(pwr, false);
  g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);   // DEPOIS do setOutputPower
  g_lora.setCurrentLimit(RF_OCP_MA);
  g_lora.startReceive();
  g_rhw.pwr = pwr;
}

inline int8_t radioHwPotenciaAntena(int8_t pwr) { return (int8_t)(pwr + RADIO_GANHO_PA_DB); }
