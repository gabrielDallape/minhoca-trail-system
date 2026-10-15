// RADIO: SX1262 (E22-900M30S) falando so no proprio slot de TDMA.
//
// Este arquivo e a casca de hardware do tdma_core.h: ele nao decide NADA sobre
// tempo. Quem diz "agora e a sua vez" e o tdmaShouldTx(); aqui so se cuida do
// chip, do pacote e de entregar o que chegou para o mundo.h.
//
// TRES COISAS APRENDIDAS NA BANCADA, todas custaram tempo e nenhuma e obvia:
//
// 1. setPacketReceivedAction e setPacketSentAction sao A MESMA interrupcao no
//    SX126x (RadioLib 7.6, SX126x_config.cpp:20 e :28 - ambas chamam
//    setDio1Action). Registrar as duas faz a segunda apagar a primeira. Aqui ha
//    UMA ISR, e o loop pergunta ao chip qual evento foi, lendo getIrqFlags()
//    ANTES de readData()/finishTransmit(), que limpam o registrador.
//
// 2. startTransmit() NAO volta para standby sozinho. Chamado com o radio em RX
//    continuo - o estado normal deste firmware - devolve -1 em 100% das vezes.
//    Por isso o standby() explicito antes de cada TX.
//
// 3. BUSY e DIO1 sao pads vizinhos no modulo (13 e 14) e GPIOs vizinhos no
//    header (49 e 50). Trocados, o begin() ainda da OK e o RX parece funcionar:
//    so a transmissao quebra, com cara de defeito de solda. O checaFios() abaixo
//    descobre isso em 5 ms, no boot, antes de qualquer teste.
//
// O QUE MUDOU NA v101 (campo de 2026-09-28), em ordem de importancia:
//
//   - A POTENCIA NUNCA MUDA SOZINHA. O recuo automatico (3 presos -> -4 dB na
//     NVS) foi REMOVIDO, junto com o resgate de boot do .ino. No lugar, um modo
//     seguro em RAM que so entra apos 3 reinicios violentos seguidos (caixa.h).
//   - O diario ganha uma linha R por pacote recebido, T por transmissao (com o
//     air-time MEDIDO, o numero que o RETOMAR pede desde julho) e N com o ruido
//     de fundo lido num slot vazio. E o que separa "sinal fraco" de "receptor
//     surdo" de "ruido no receptor" - o log v100 nao distinguia.
//   - Pacote de posicao com CAUDA DE TRILHA (rota_core.h): o lider manda os
//     pontos numerados do proprio trajeto e preenche o vao de quem perdeu sinal.
//   - Air-time CALCULADO por tamanho (radioAirtimeUs), com static_assert de que
//     o maior pacote cabe no slot. O roster de 23 B ja nao cabia no slot de 250 ms
//     da v100 - por isso o TDMA foi para 4 slots de 500 ms (hardware.h).
//   - Guarda contra RELOGIO QUE VOLTA: a FaustoA registrou millis() indo de
//     38639 para 6098 no mesmo segundo UTC antes do watchdog. Com o esp_timer
//     andando para tras o vigia acusava 'preso' e o TDMA perdia a janela; agora
//     o salto e detectado, registrado e a ancora e deslocada junto.
#pragma once
#include <RadioLib.h>
#include "hardware.h"
#include "gps.h"
#include "../tdma_core.h"
#include "registro.h"
#include "caixa.h"
#include "rota_core.h"

// ------------------------------------------------------------------ air-time
// CALCULADO pela formula do datasheet (SX1261/2, 6.1.4), nao mais cravado. Em
// SF7 o valor medido (58,0 ms para 16 B) ficou 6,5 ms acima do teorico: sao os
// comandos SPI e a rampa do PA, que ocupam o canal do mesmo jeito. Esses 6,5 ms
// entram como RADIO_TX_EXTRA_US. A linha T do diario traz o air-time MEDIDO de
// cada transmissao (startTransmit -> TX_DONE) - se ele vier diferente disto, e
// este numero que se corrige, e nao o contrario.
//
//   Ts        = 2^SF / BW                      4,096 ms em SF9/BW125
//   preambulo = (RF_PRE + 4,25) x Ts           50,2 ms
//   carga     = 8 + ceil((8xPL - 4xSF + 28 + 16) / (4xSF)) x CR   simbolos
//               16 B -> 28 simbolos (114,7 ms)   70 B -> 88 (360,4 ms)
//
// O LDRO (low data rate optimize) so entra com Ts >= 16,38 ms (SF11+ em BW125),
// que e o criterio da propria RadioLib; em SF9 nao muda nada, mas a formula sabe.
#define RADIO_TX_EXTRA_US 6500UL
constexpr uint32_t radioSimboloUs() { return (uint32_t)(((uint64_t)(1UL << RF_SF) * 1000000ULL) / (uint32_t)(RF_BW * 1000.0f)); }
constexpr bool     radioLdro()      { return radioSimboloUs() >= 16380; }
constexpr uint32_t radioSimbolosCarga(uint32_t bytes) {
  return 8 + (uint32_t)(((8 * (int32_t)bytes - 4 * RF_SF + 28 + 16) <= 0) ? 0
           : ((8 * (int32_t)bytes - 4 * RF_SF + 28 + 16) + 4 * (RF_SF - (radioLdro() ? 2 : 0)) - 1)
             / (4 * (RF_SF - (radioLdro() ? 2 : 0)))) * RF_CR;
}
constexpr uint32_t radioAirtimeUs(uint32_t bytes) {
  return ((4 * RF_PRE + 17) * radioSimboloUs()) / 4
       + radioSimbolosCarga(bytes) * radioSimboloUs() + RADIO_TX_EXTRA_US;
}

// O MAIOR pacote que este firmware transmite: posicao + cauda de trilha cheia.
// E ele que dimensiona a janela de TX no tdma_core (um pacote menor so sobra
// folga). O roster (23 B) e menor que isto.
#define RADIO_PKT_MAX      (TRILHA_PKT_N(TRILHA_PONTOS_PKT))
#define RADIO_AIRTIME_US   radioAirtimeUs(RADIO_PKT_MAX)
#define RADIO_SLOT_US      ((uint32_t)TDMA_FRAME_S * 1000000UL / TDMA_SLOTS)
static_assert(RADIO_PKT_MAX <= 255, "pacote maior que o FIFO do SX1262");
static_assert(RADIO_AIRTIME_US + TDMA_GUARD_US + 40000UL <= RADIO_SLOT_US,
              "o maior pacote nao cabe no slot com 40 ms de janela para o laco: "
              "reduza TRILHA_PONTOS_PKT ou TDMA_SLOTS (ver hardware.h)");
static_assert(radioAirtimeUs(16) > 160000UL && radioAirtimeUs(16) < 180000UL,
              "a formula do air-time saiu do esperado para 16 B em SF9 (171 ms)");

// O VIGIA DE TX PRESO tem de esperar mais que o maior pacote. Era 400 ms fixos
// na v100; com o pacote de 70 B (417 ms) isso contaria 'preso' em TODA
// transmissao longa e refaria o begin() do chip no meio dela.
#define RADIO_VIGIA_MS     (RADIO_AIRTIME_US / 1000UL + 200UL)

// Flag do pacote de posicao: "ha cauda de trilha depois do byte 15" (rota_core.h).
// 0x01/0x02/0x04/0x08 sao do tdma_core; 0x80 e o roster.
#define TDMA_FL_TRILHA  0x40

// Tamanhos do roster (a logica esta mais abaixo, em "ROSTER"). Ficam aqui em
// cima porque o boot imprime o air-time dele junto com o dos outros pacotes.
#define ROSTER_FL       0x80        // bit livre nas flags: 0x01/0x02/0x04 sao FIX/ALERTA/LIDER
#define ROSTER_NOME_N   16
#define ROSTER_PKT_N    (7 + ROSTER_NOME_N)     // 23 bytes

// GetDeviceErrors (0x17) e ClearDeviceErrors (0x07) sao o UNICO lugar em que o chip
// diz POR QUE um TX nao completou - TCXO, PLL ou rampa do PA. A RadioLib os declara
// 'protected' (SX126x.h:856-857), entao herdamos so para reexpo-los.
//
// Por que assim e nao de outro jeito: RADIOLIB_GODMODE=1 abriria a biblioteca inteira
// (o MeshCore faz isso, mas ele precisa de muito mais); e falar SPI na mao em paralelo
// com a biblioteca - como o e22_ping faz, onde e legitimo por ser sketch de bancada -
// disputaria o barramento com as transacoes dela. Herdar custa quatro linhas.
class SX1262Mts : public SX1262 {
 public:
  SX1262Mts(Module* m) : SX1262(m) {}
  using SX126x::getDeviceErrors;
  using SX126x::clearDeviceErrors;
  Module* modulo() { return getMod(); }
};

static SPIClass spiLoRa(FSPI);
static SX1262Mts g_lora = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_NRST,
                                     PIN_LORA_BUSY, spiLoRa,
                                     SPISettings(2000000, MSBFIRST, SPI_MODE0));
static Tdma     g_tdma;

// O carimbo e de 64 BITS (esp_timer), nao micros(). O micros() do Arduino e o
// esp_timer truncado a 32 bits: ele estoura em 71,6 min e o tdma_core compara
// este carimbo com o relogio de 64 bits - a diferenca saltava 2^32 us e a ancora
// se deslocava ~967 ms (2^32 mod 1e6). Sintoma: a rede funciona na bancada e
// morre inteira depois de uma hora e dez de trilha.
static volatile bool     g_loraIrq = false;
static volatile uint64_t g_loraIrqUs = 0;
void IRAM_ATTR loraOnIrq(){ g_loraIrqUs = (uint64_t)esp_timer_get_time(); g_loraIrq = true; }

static bool     g_loraOk = false;
static bool     g_loraTxEmVoo = false;
static uint32_t g_loraTxDesdeMs = 0;
static uint32_t g_txPreso = 0;
// GUARDADOS, nao so impressos. O radioChecaFios() e o begin() dizem exatamente
// qual e o defeito de uma placa que nao transmite - mas ate aqui isso so existia
// como texto no serial, e ha placa nesta frota cujo serial nao chega a lugar
// nenhum (o console dela sai por um conector que nao esta ligado). Guardados,
// eles viajam no eco para o servidor e a placa se explica de longe.
// Modo medida: imprime uma linha por pacote recebido, para comparar antenas com
// estatistica em vez de impressao. Ligado pelo serial ('m'), nao persiste - e
// ferramenta de bancada, e a 1 linha por segundo por vizinho sujaria o log.
static bool     g_rssiVerboso = false;
static bool     g_loraFiosOk  = false;
static int      g_loraBeginSt = 999;      // 999 = nem chegou a tentar
// Ultimo GetDeviceErrors lido num TX que nao completou. Guardado, nao so impresso,
// pelo mesmo motivo dos dois acima: ha placa nesta frota cujo serial nao chega a
// lugar nenhum, e este numero diz QUAL das tres etapas do TX falhou. Viaja no eco e
// cabe na tela de diagnostico.
static uint16_t g_ultErrDisp  = 0;
static uint8_t  g_txPresoSeguidos = 0;    // so diagnostico: quantos seguidos (nao recua nada)

// POTENCIA. Duas variaveis, de proposito:
//   g_rfPwrConfig = a que o USUARIO escolheu (NVS, 'p' no serial, tela de diag).
//   g_rfPwr       = a que esta PROGRAMADA no chip agora.
// Elas so diferem no MODO SEGURO (radioModoSeguro): tres reinicios violentos
// seguidos (caixa.h) poem um teto de RADIO_PWR_SEGURO dBm neste boot, em RAM. A
// configurada nunca e tocada por codigo - ver hardware.h, "A POTENCIA NUNCA MUDA
// SOZINHA". Ate a v100 havia um so numero e dois mecanismos que o baixavam e
// gravavam; foi assim que o campo de 09-28 inteiro rodou a 6 dBm sem ninguem saber.
static int8_t   g_rfPwrConfig = RF_PWR;
static int8_t   g_rfPwr = RF_PWR;
static bool     g_rfSeguro = false;
static uint8_t  g_loraSeq = 0;
static uint32_t g_loraRoom = 0;
static uint32_t g_rxOk = 0, g_rxRuim = 0, g_rxForaDaSala = 0;
static float    g_rxRssi = 0, g_rxSnr = 0;

// TX: o que esta no ar, para a linha T e para a caixa-preta.
static uint64_t g_txIniUs = 0;
static uint8_t  g_txLen = 0;
static char     g_txTipo = 0;              // 'P' posicao/trilha, 'R' roster
static uint32_t g_txUsInFrame = 0;         // onde no quadro o TX comecou
static bool     g_txCatchup = false;
static uint8_t  g_txPontos = 0;
static uint32_t g_txAirUltUs = 0, g_txAirMaxUs = 0, g_txAirMinUs = 0xFFFFFFFFu;
static uint32_t g_quadroTx = 0;            // minhas falas, para alternar catch-up
static uint32_t g_catchups = 0;

// PERDA POR SEQUENCIA, por remetente. O seq do pacote sempre existiu e nunca foi
// lido: com ele a perda e CONTADA por slot de origem (seq pulou de 7 para 10 =
// dois perdidos), em vez de estimada cruzando o tx do outro com o rx daqui.
static bool     g_seqTem[TDMA_SLOTS];
static uint8_t  g_seqUlt[TDMA_SLOTS];
static uint32_t g_seqPerd[TDMA_SLOTS], g_rxPorSlot[TDMA_SLOTS];
static uint32_t g_seqPerdTotal = 0;

// RUIDO DE FUNDO: RSSI instantaneo (GetRssiInst) lido num slot em que ninguem
// fala. Se a entrega e de 50% a -97 dBm num radio que ouve ate -129, ou o piso
// de ruido subiu (step-down, tela, lataria) ou o receptor esta surdo - e este e o
// numero que separa as duas hipoteses.
static float    g_ruidoUlt = 0;
static uint32_t g_ruidoN = 0;
static float    g_ruidoSoma = 0, g_ruidoMin = 0, g_ruidoMax = -200;
static uint32_t g_ruidoFrame = 0xFFFFFFFFu;

// Relogio que andou para tras (ver cabecalho). Contado e registrado.
static uint32_t g_relogioVoltou = 0;

// Recebe o pacote decodificado. Definido no mundo.h, que e quem conhece os
// carros - o radio nao deve saber o que e um carro.
void mundoRecebePacote(const TdmaPkt& k);
void mundoRecebeRoster(uint8_t slot, uint8_t cor, bool lider, const char* nome);
int  mundoRecebeTrilha(const TdmaPkt& k, const uint8_t* buf, int len);
inline bool trajetoPontoSeq(uint16_t seq16, double& lat, double& lon);
inline bool mundoAckMin(uint16_t& ackAteMin, uint16_t& ackDeMax, uint32_t agoraMs, uint32_t ttlMs);

inline uint32_t radioUtc() { return g_gpsHoraOk ? g_gpsSecDia : 0; }

// ------------------------------------------------------------------ fiacao
// Depois que o NRST e solto, o SX126x segura o BUSY ALTO por ~1 ms carregando a
// calibracao, enquanto o DIO1 fica baixo (nao ha IRQ pendente). Quem sobe e o
// BUSY - e so isso ja identifica qual fio e qual.
inline bool radioChecaFios()
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
    Serial.println("radio: *** BUSY e DIO1 TROCADOS no chicote! ***");
    Serial.printf ("radio:     o BUSY esta no GPIO%d, deveria estar no GPIO%d\n",
                   PIN_LORA_DIO1, PIN_LORA_BUSY);
    Serial.println("radio:     o begin() vai passar e o RX vai parecer bom, mas");
    Serial.println("radio:     nenhuma transmissao vai completar. Troque os dois fios.");
    return false;
  }
  if (nBusy <= nDio1 * 4) {
    Serial.println("radio: fiacao inconclusiva - o modulo respondeu ao reset?");
    return false;
  }
  return true;
}

// ------------------------------------------------------------------- inicio
// O nome do grupo vira 24 bits para o pacote. Dois grupos na mesma trilha usam a
// mesma frequencia e o mesmo sync word: e este campo que impede um carro de
// aparecer no mapa do grupo errado.
inline uint32_t radioHashSala(const char* cod)
{
  uint32_t h = 2166136261u;                       // FNV-1a
  for (const char* p = cod; p && *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return h & 0xFFFFFF;
}

// A configuracao que o begin() NAO faz e que tem de ser refeita toda vez que ele
// roda (boot e vigia). Uma funcao so, para o vigia nao esquecer nada - na v100 o
// vigia esquecia o setDio2AsRfSwitch, o setOutputPower(false) e a rampa.
inline void radioConfiguraPosBegin()
{
  // O begin() do RadioLib chama setDio2AsRfSwitch(true) INCONDICIONALMENTE
  // (SX126x.cpp:58) e o setRfSwitchPins abaixo so configura GPIOs do MCU - nao desfaz
  // nada no chip. Quem controla o switch aqui sao o RXEN e o TXEN; o DIO2 fica solto
  // no pad 8 do modulo, chaveando a toa. O Meshtastic desliga explicitamente quando ha
  // TXEN/RXEN externos, e e o que se faz aqui.
  g_lora.setDio2AsRfSwitch(false);
  // Sem setRfSwitchPins o PA externo nunca liga: transmite "nada" sem erro.
#if RF_SWITCH_TROCADO
  g_lora.setRfSwitchPins(PIN_LORA_TXEN, PIN_LORA_RXEN);   // ver RF_SWITCH_TROCADO
#else
  g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
#endif
  // DEPOIS do begin(), que grava 60 mA em tres pontos. Com o OCP de fabrica a
  // saida trava em ~19 dBm em vez de 22, sem erro e sem aviso.
  g_lora.setCurrentLimit(RF_OCP);

  // A PA-OPT-TABLE E PARA SX126x NU, NAO PARA MODULO COM PA EXTERNO.
  //
  // A partir da RadioLib 7.5 o setOutputPower(p) escolhe paDutyCycle/hpMax/paVal numa
  // tabela de 32 entradas medida num SX1262 SEM amplificador (issue #1628). Ela NAO e
  // monotonica: pedir +4 dBm programa paVal +22; pedir +6 programa paDutyCycle 3,
  // hpMax 1, paVal +21; pedir +7 cai para hpMax 2, paVal +17.
  //
  // Com 'false' o PA fica fixo (paDutyCycle 0x04, hpMax 0x07) e paVal = o dBm pedido -
  // o comportamento pre-7.5. E o que o Meshtastic faz em toda placa com PA externo
  // (SX126X_NO_POWER_OPTIMIZATION_TABLE, commit 80f8611).
  g_lora.setOutputPower(g_rfPwr, false);

  // DEPOIS do setOutputPower, sempre: ele chama setTxParams com a rampa fixa em 200 us
  // (SX126x_config.cpp:730) e apaga qualquer setPaRampTime anterior.
  //
  // 800 us e o valor que o Meshtastic usa na variante T-Beam 1W, com o comentario
  // "requires >800us stabilization (default is 200us)". Rampa 4x mais lenta = o PA
  // externo sobe a corrente devagar, e a fonte tem tempo de acompanhar.
  g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);

  // ~2 dB de sensibilidade de RX. Esta no docs/MONTAGEM.md desde sempre e nunca tinha
  // entrado no codigo. As sensibilidades do datasheet SAO as boosted.
  g_lora.setRxBoostedGainMode(true);

  g_lora.setDio1Action(loraOnIrq);
  // LIMPAR OS ERROS DO CHIP NO BOOT. O XOSC_START_ERR e ESPERADO no POR quando ha
  // TCXO: o datasheet (SX1261/2 rev 2.2, 13.3.6) diz que o chip ainda nao sabe que
  // esta sendo clocado por um TCXO e que "o usuario deve simplesmente limpar essa
  // flag". O registrador e LATCHEADO - sem limpar aqui, todo GetDeviceErrors futuro
  // devolve o aviso de boot e nao se distingue de uma falha real.
  //
  // Isto nao e detalhe: foi um GetDeviceErrors lido SEM clear antes que condenou o
  // modulo da tela A como defeituoso em 2026-08-24.
  g_lora.clearDeviceErrors();

  // TIMEOUT DE SPI: 100 ms em vez dos 1000 da biblioteca. Toda transacao da
  // RadioLib espera o BUSY baixar com este timeout; um modulo que apagou por
  // queda do 5 V e ficou com o BUSY alto segurava cada comando por 1 s, e o
  // begin() do vigia tem dezenas deles. O laco parava por 20-30 s de uma vez -
  // tempo de sobra para qualquer watchdog. Depois de um comando o SX1262 solta o
  // BUSY em dezenas de us; a espera mais longa legitima e a calibracao (~3,5 ms).
  g_lora.modulo()->spiConfig.timeout = 100;
}

inline bool radioInicia(uint8_t meuSlot, const char* codigoSala)
{
  Serial.println("radio: conferindo a fiacao...");
  g_loraFiosOk = radioChecaFios();   // avisa e segue: o begin abaixo diz se da para continuar

  spiLoRa.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  int st = g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
  g_loraBeginSt = st;
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio: begin() FALHOU (%d)\n", st);
    if (st == -707) Serial.println("radio:   -707 = tensao do TCXO. Confira RF_TCXO (2,2 V).");
    g_loraOk = false;
    return false;
  }
  radioConfiguraPosBegin();

  g_loraRoom = radioHashSala(codigoSala);
  tdmaInit(g_tdma, meuSlot, TDMA_SLOTS, TDMA_FRAME_S, TDMA_GUARD_US);
  tdmaSetAirtime(g_tdma, RADIO_AIRTIME_US);
  for (int s = 0; s < TDMA_SLOTS; s++) { g_seqTem[s] = false; g_seqPerd[s] = 0; g_rxPorSlot[s] = 0; }

  int32_t folga = tdmaHeadroomUs(g_tdma, RADIO_AIRTIME_US);
  Serial.printf("radio: slot %u de %d | frame %ds | slot %lu us | folga %ld us\n",
                meuSlot, TDMA_SLOTS, TDMA_FRAME_S,
                (unsigned long)g_tdma.slotUs, (long)folga);
  Serial.printf("radio: air-time calculado SF%d/BW%.0f: 16 B = %lu us | roster %d B = %lu us | "
                "trilha %d B = %lu us (vigia %lu ms)\n", RF_SF, RF_BW,
                (unsigned long)radioAirtimeUs(16), (int)ROSTER_PKT_N,
                (unsigned long)radioAirtimeUs(ROSTER_PKT_N), (int)RADIO_PKT_MAX,
                (unsigned long)RADIO_AIRTIME_US, (unsigned long)RADIO_VIGIA_MS);
  if (folga <= 0) {
    // Nao e opiniao: com folga negativa o pacote nao cabe no slot e a rede colide
    // por construcao. Melhor gritar no boot do que caçar perda de pacote depois.
    Serial.println("radio: *** O PACOTE NAO CABE NO SLOT ***");
    Serial.printf ("radio:     aumente TDMA_FRAME_S ou reduza TDMA_SLOTS (max %lu nos)\n",
                   (unsigned long)tdmaMaxNodes(g_tdma, RADIO_AIRTIME_US));
  }
  if (g_tdma.gridSensitive) {
    Serial.println("radio: ATENCAO: com este frame, misturar nos em grade UTC e GPS desalinha a rede.");
  }

  g_lora.startReceive();
  g_loraOk = true;
  Serial.printf("radio: no ar em %.1f MHz, sala 0x%06lX, potencia %d dBm "
                "(~%d dBm na antena com o PA)%s\n",
                RF_FREQ, (unsigned long)g_loraRoom, g_rfPwr,
                g_rfPwr + RADIO_GANHO_PA_DB,
                g_rfSeguro ? "  *** MODO SEGURO: teto neste boot ***" : "");
  if (g_rfPwrConfig < RF_PWR) {
    // A potencia vem da NVS e sobrevive a regravacao. Uma escada de bancada que
    // deixou 2 dBm gravados manda o aparelho para a trilha com 20 dB a menos -
    // cerca de 10x menos alcance - sem nada na tela dizendo isso. Grita no boot.
    Serial.printf("radio: *** ATENCAO: %d dBm e MENOS que o padrao (%d dBm). Veio "
                  "da NVS, nao do firmware. Use 'p%d' no serial para voltar. ***\n",
                  g_rfPwrConfig, RF_PWR, RF_PWR);
  }
  return true;
}

// ==================================================================== ROSTER
// QUEM E CADA SLOT. O pacote de posicao nao tem espaco para nome: sao 16 bytes e
// 15 deles ja tem dono. Aumenta-lo custaria air-time no pacote que roda a 1 Hz,
// e o air-time e o que dimensiona o slot - ficaria mais caro para sempre por uma
// informacao que muda quase nunca.
//
// Entao vai um pacote SEPARADO, no MESMO slot, alternado com o de posicao: de
// oito em oito quadros o no manda quem ele e em vez de onde esta. Custa uma
// atualizacao de posicao a cada oito segundos, e posicao a gente tem de sobra.
//
// Fica aqui e nao no tdma_core.h de proposito: o tdma_core trata de TEMPO e da
// carga minima que a rede precisa para existir. Nome e cor sao do MTS, nao do
// TDMA - uma rede de sensores usaria o mesmo tdma_core sem nada disto.
static_assert(ROSTER_PKT_N <= RADIO_PKT_MAX, "roster maior que o pacote maximo");

inline int rosterPack(uint8_t* p, uint32_t room, uint8_t slot, uint8_t seq,
                      uint8_t cor, bool lider, const char* nome)
{
  p[0] = room & 0xFF; p[1] = (room >> 8) & 0xFF; p[2] = (room >> 16) & 0xFF;
  p[3] = slot;
  p[4] = seq;
  p[5] = ROSTER_FL | (lider ? TDMA_FL_LEADER : 0);
  p[6] = cor;
  memset(p + 7, 0, ROSTER_NOME_N);
  strncpy((char*)(p + 7), nome, ROSTER_NOME_N);
  return ROSTER_PKT_N;
}

inline bool rosterEh(const uint8_t* p, int len)
{
  return len >= ROSTER_PKT_N && (p[5] & ROSTER_FL);
}

// O nome chega de OUTRO aparelho, entao e texto de procedencia desconhecida indo
// direto para a tela. O CRC do LoRa ja barra corrupcao no ar, mas nao barra um no
// com firmware diferente, memoria suja ou nome com byte que a fonte nao tem -
// qualquer um desses vira lixo desenhado por cima da interface. Aqui so passa o
// que e imprimivel, e o corte garante o terminador.
inline void rosterNome(const uint8_t* p, char* destino, size_t n)
{
  size_t i = 0;
  for (; i + 1 < n && i < ROSTER_NOME_N; i++) {
    char c = (char)p[7 + i];
    if (c == 0) break;
    destino[i] = (c >= 32 && c < 127) ? c : '?';
  }
  destino[i] = 0;
  if (!destino[0]) strncpy(destino, "sem nome", n - 1);
}

// O grupo so existe depois que o usuario cria ou entra num, e ate la o radio ja
// esta no ar numa sala neutra. Trocar a sala e trocar o filtro: a partir daqui
// so entram no mapa os pacotes de quem digitou o mesmo codigo.
inline void radioTrocaSala(const char* cod)
{
  uint32_t nova = radioHashSala(cod);
  if (nova == g_loraRoom) return;
  g_loraRoom = nova;
  g_rxOk = g_rxRuim = g_rxForaDaSala = 0;
  for (int s = 0; s < TDMA_SLOTS; s++) g_seqTem[s] = false;
  Serial.printf("radio: sala agora e 0x%06lX (codigo %s)\n", (unsigned long)nova, cod);
}

// ------------------------------------------------------------------- ciclo
// O BUSY PRECISA ESTAR BAIXO ANTES DE MANDAR TRANSMITIR.
//
// O startTransmit() do RadioLib termina assim (SX126x.cpp:1143):
//     while(this->mod->hal->digitalRead(this->mod->getGpio())) { yield(); }
// e esse getGpio() e o BUSY. O laco NAO TEM TIMEOUT. Um modulo que apagou no meio do
// TX - o brownout de 5 V descrito no vigia mais abaixo - segura o BUSY alto, e entao
// a chamada NUNCA RETORNA: o laco principal morre ali dentro, o vigia de TX preso
// nunca chega a rodar, e o aparelho fica mudo e cego sem uma linha no log.
//
// As transacoes de SPI da biblioteca tem timeout proprio; esta espera nao tem. Por
// isso a pergunta e feita AQUI, antes, com relogio nosso. E a caixa-preta carimba
// CP_TX_START em volta do startTransmit: se um dia ele nao voltar, o boot seguinte
// diz exatamente isso.
//
// 5 ms e folga larga: depois de um comando o SX1262 solta o BUSY em dezenas de
// microssegundos; a espera mais longa e a calibracao pos-reset, ~1 ms (radioChecaFios).
inline bool radioBusyLivre()
{
  caixaEtapa(CP_TX_BUSY);
  uint32_t t0 = micros();
  while (digitalRead(PIN_LORA_BUSY) && (micros() - t0) < 5000) { }
  return digitalRead(PIN_LORA_BUSY) == LOW;
}

// O caminho comum de toda transmissao: guarda do BUSY, standby, startTransmit,
// carimbos para a linha T e para a caixa-preta.
inline void radioTransmite(const uint8_t* buf, int n, char tipo)
{
  if (!radioBusyLivre()) {
    // Marca TX em voo para o vigia fazer a reconfiguracao pelo caminho que ja
    // existe - e para o 'preso' ser contado uma vez so, la.
    g_loraTxEmVoo   = true;
    g_loraTxDesdeMs = millis();
    g_txTipo = tipo; g_txLen = (uint8_t)n;
    Serial.printf("radio: BUSY preso alto - NAO transmiti (%c); o vigia reconfigura\n", tipo);
    registroLinha("!,%lu,busy preso,%c", (unsigned long)millis(), tipo);
    return;
  }
  uint32_t usInFrame = 0, frameIdx = 0;
  tdmaUsInFrame(g_tdma, usInFrame, frameIdx);

  caixaEtapa(CP_TX_START);
  g_lora.standby();          // ver o item 2 do cabecalho: sem isto, -1 sempre
  g_loraTxEmVoo = true;
  g_loraTxDesdeMs = millis();
  g_txTipo = tipo; g_txLen = (uint8_t)n; g_txUsInFrame = usInFrame;
  g_txIniUs = (uint64_t)esp_timer_get_time();
  int st = g_lora.startTransmit((uint8_t*)buf, n);
  if (st != RADIOLIB_ERR_NONE) {
    g_loraTxEmVoo = false;
    Serial.printf("radio: startTransmit(%c) -> %d\n", tipo, st);
    registroLinha("!,%lu,startTransmit falhou,%c,%d", (unsigned long)millis(), tipo, st);
    g_lora.startReceive();
    caixaEtapa(CP_LACO);
    return;
  }
  caixaEtapa(CP_TX_VOO);
}

inline void radioTransmiteRoster()
{
  uint8_t buf[RADIO_PKT_MAX];
  int n = rosterPack(buf, g_loraRoom, g_tdma.nodeId, g_loraSeq++,
                     g_carros[0].cor, g_carros[0].lider, g_carros[0].nome);
  g_txCatchup = false; g_txPontos = 0;
  radioTransmite(buf, n, 'R');
}

// POSICAO + CAUDA DE TRILHA (rota_core.h). Todo no manda o ack; so o LIDER manda
// pontos - o rastro de um seguidor nao e caminho para ninguem, e 48 bytes a menos
// no ar por seguidor sao 250 ms de canal por quadro.
inline void radioTransmiteMinhaPosicao()
{
  TdmaPkt k;
  k.room  = g_loraRoom;
  k.slot  = g_tdma.nodeId;
  k.seq   = g_loraSeq++;
  k.flags = TDMA_FL_TRILHA;
  if (g_meuFix)          k.flags |= TDMA_FL_FIX;
  // Anuncia que o MEU relogio esta na grade UTC. Quem me ouve prefere esta fonte
  // a qualquer beacon de grade arbitraria - e o que impede a rede de se partir em
  // duas quando o primeiro carro pega fix.
  if (g_tdma.sync == TDMA_SYNC_PPS && !g_tdma.holdover) k.flags |= TDMA_FL_PPS;
  if (g_carros[0].alerta) k.flags |= TDMA_FL_ALERT;
  if (g_carros[0].lider)  k.flags |= TDMA_FL_LEADER;
  k.lat = g_meuLat; k.lon = g_meuLon;
  // rumo e velocidade sempre existiram no pacote (secao 7 do plano) e iam
  // zerados; agora giram o marcador deste carro na tela dos outros
  k.heading = g_meuRumo;
  k.speed   = g_meuVel;

  uint8_t buf[RADIO_PKT_MAX];
  tdmaPack(buf, k);

  uint16_t base = 0; int nPts = 0; bool catchup = false;
  if (g_carros[0].lider && g_trajN > 0) {
    uint16_t ackAteMin = ROTA_SEQ_NENHUMA, ackDeMax = ROTA_SEQ_NENHUMA;
    bool temAck = mundoAckMin(ackAteMin, ackDeMax, millis(), 5000UL * TDMA_FRAME_S * 3);
    catchup = trilhaEscolheBloco(g_trajSeqUlt, g_trajN, ackAteMin, ackDeMax, temAck, g_quadroTx,
                                 TRILHA_PONTOS_PKT, base, nPts);
  }
  g_quadroTx++;
  int n = trilhaCodifica(buf, g_trajEpoca, base, rotaAck16(g_rotaC), rotaAckDe16(g_rotaC), nPts,
    [&](int i, double& la, double& lo) {
      if (!trajetoPontoSeq((uint16_t)(base + i), la, lo)) { la = g_meuLat; lo = g_meuLon; }
    });
  g_txCatchup = catchup; g_txPontos = buf[23];
  if (catchup) g_catchups++;
  radioTransmite(buf, n, 'P');
}

inline void radioTrocaPotencia(int8_t nova);   // definida adiante

// RELOGIO QUE VOLTA. O esp_timer e monotonico por construcao; se ele andou para
// tras, algo de hardware aconteceu (a FaustoA registrou -33 s antes de um reset
// por watchdog, 2026-09-28). Nao ha como consertar a causa daqui, mas da para
// nao piorar: sem isto o vigia contava 'preso' (millis - inicio virava enorme),
// o TDMA calculava a posicao no quadro com (agora - ancora) negativo e perdia
// a janela, e o log nao dizia nada. Agora: uma linha "!" com o tamanho do salto,
// e os carimbos absolutos (ancora, PPS, nascimento, inicio do TX) deslocados
// junto - a FASE da rede se preserva se o salto foi instantaneo.
inline void radioGuardaRelogio()
{
  static uint64_t ult = 0;
  uint64_t now = tdmaNowUs();
  if (ult && now + 1000 < ult) {
    uint64_t salto = ult - now;
    g_relogioVoltou++;
    Serial.printf("radio: *** RELOGIO VOLTOU %llu us (esp_timer) - deslocando ancoras ***\n",
                  (unsigned long long)salto);
    registroLinha("!,%lu,relogio voltou,%llu us,motivo desconhecido - ver caixa-preta",
                  (unsigned long)millis(), (unsigned long long)salto);
    g_tdma.anchorUs  = (g_tdma.anchorUs  > salto) ? g_tdma.anchorUs  - salto : 0;
    g_tdma.lastPpsUs = (g_tdma.lastPpsUs > salto) ? g_tdma.lastPpsUs - salto : 0;
    g_tdma.bornUs    = (g_tdma.bornUs    > salto) ? g_tdma.bornUs    - salto : 0;
    g_loraTxDesdeMs  = millis();
    g_txIniUs        = now;
  }
  ult = now;
}

// RUIDO DE FUNDO, uma amostra por quadro num slot em que ninguem fala. O slot
// escolhido e o MAIOR que nao e meu e nao tem carro ativo (a frota de 3 deixa o
// slot 3 vazio). Lido no meio do slot, longe das guardas, so com o radio em RX e
// sem TX em voo. Se os 4 slots estiverem ocupados nao ha onde medir - conta-se.
inline void radioMedeRuido()
{
  if (g_loraTxEmVoo || !g_tdma.haveAnchor) return;
  uint32_t usInFrame, frameIdx;
  if (!tdmaUsInFrame(g_tdma, usInFrame, frameIdx)) return;
  if (frameIdx == g_ruidoFrame) return;
  int livre = -1;
  for (int s = TDMA_SLOTS - 1; s >= 0; s--) {
    if (s == g_tdma.nodeId) continue;
    bool ocupado = false;
    for (int i = 1; i < MUNDO_MAX_CARROS; i++)
      if (g_carros[i].ativo && g_carros[i].slot == s) { ocupado = true; break; }
    if (!ocupado) { livre = s; break; }
  }
  if (livre < 0) return;
  uint32_t ini = tdmaSlotStartUs(g_tdma, (uint8_t)livre);
  uint32_t g = tdmaGuardNow(g_tdma);
  if (usInFrame < ini + g * 2 || usInFrame > ini + g_tdma.slotUs - g * 2) return;
  g_ruidoFrame = frameIdx;
  caixaEtapa(CP_RX_RSSI);
  float r = g_lora.getRSSI(false);
  caixaEtapa(CP_LACO);
  g_ruidoUlt = r; g_ruidoN++; g_ruidoSoma += r;
  if (g_ruidoN == 1 || r < g_ruidoMin) g_ruidoMin = r;
  if (r > g_ruidoMax) g_ruidoMax = r;
  registroLinha("N,%lu,%lu,%.0f,%d", (unsigned long)millis(), (unsigned long)radioUtc(), r, livre);
}

inline void radioAtualiza()
{
  if (!g_loraOk) return;
  radioGuardaRelogio();

  // 1) ancorar o tempo. O PPS so vale se vier pareado com o segundo do NMEA.
  if (g_parePronto) {
    g_parePronto = false;
    tdmaOnPps(g_tdma, g_parePpsUs, g_pareSec);
  }

  // TX_DONE que nunca chegou. A causa mais comum NAO e fio: e o E22 APAGANDO no
  // meio do TX quando o 5 V dele afunda (medido em campo 2026-08-24, tela A a
  // 10 dBm: preso crescia 1:1 com tx). Um modulo que resetou esqueceu TODA a
  // configuracao - so finishTransmit+startReceive o deixava escutando na
  // frequencia de fabrica, surdo para o grupo. Entao o vigia REFAZ o begin()
  // completo: custa ~100 ms uma vez, contra um no morto ate reiniciar.
  //
  // ANTES DE ACUSAR, PERGUNTE AO CHIP. Este vigia roda ANTES do tratador de IRQ
  // logo abaixo, e o relogio dele e o do LACO - nao o do radio. Se o laco ficou
  // parado mais que o vigia, um TX que COMPLETOU chega aqui parecendo travado, e
  // o "conserto" era reconfigurar um radio que estava perfeito. Medido em
  // 2026-08-29 com o HTTPS do eco segurando o laco.
  //
  // E se o chip diz que terminou mas a ISR nao correu (borda perdida no DIO1),
  // a gente ASSUME o evento: levanta a flag e deixa o tratador finalizar. Sem
  // isto o no transmitia UM pacote e ficava mudo para sempre (2026-08-29).
  bool venceuOTempo = g_loraTxEmVoo && (millis() - g_loraTxDesdeMs > RADIO_VIGIA_MS) && !g_loraIrq;
  if (venceuOTempo && (g_lora.getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE)) {
    g_loraIrqUs = (uint64_t)esp_timer_get_time();   // so o RX usa o carimbo
    g_loraIrq = true;
    venceuOTempo = false;
  }
  if (venceuOTempo) {
    g_loraTxEmVoo = false;
    g_txPreso++;
    g_txPresoSeguidos++;

    // PERGUNTE AO CHIP POR QUE FALHOU. A sequencia de um TX e: XOSC liga -> PLL
    // prende -> PA liga -> RAMPA -> pacote -> TX_DONE (datasheet 13.4.1). Um
    // "preso" e sempre uma falha num dos tres primeiros passos, e o chip diz em
    // qual - de graca, numa leitura de SPI. Nenhum bit aceso + TX que nao
    // completou = o OCP cortou a corrente do PA.
    uint16_t err = g_lora.getDeviceErrors();
    g_ultErrDisp = err;
    Serial.printf("radio: TX preso (%lu) - erros do chip = 0x%04X%s%s%s\n",
                  (unsigned long)g_txPreso, err,
                  (err & (1u << 5)) ? "  XOSC_NAO_PARTIU(TCXO)" : "",
                  (err & (1u << 6)) ? "  PLL_NAO_PRENDEU"       : "",
                  (err & (1u << 8)) ? "  RAMPA_DO_PA"           : "");
    if (!err) Serial.println("radio:   nenhum erro no chip -> suspeita de corte por OCP "
                             "(queda de tensao no 5V do modulo)");
    registroLinha("!,%lu,tx preso,%lu,%c,%u B,err 0x%04X,pwr %d,%lu ms",
                  (unsigned long)millis(), (unsigned long)g_txPreso, g_txTipo ? g_txTipo : '?',
                  (unsigned)g_txLen, err, (int)g_rfPwr,
                  (unsigned long)(millis() - g_loraTxDesdeMs));

    caixaEtapa(CP_TX_VIGIA);
    int st = g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
    if (st == RADIOLIB_ERR_NONE) {
      radioConfiguraPosBegin();
      g_lora.startReceive();
      Serial.printf("radio: TX preso - radio RECONFIGURADO do zero (%lu). "
                    "Se isto se repete, o 5V do modulo esta afundando no TX.\n",
                    (unsigned long)g_txPreso);
      // NAO HA RECUO DE POTENCIA AQUI. Ate a v100 tres presos seguidos desciam
      // 4 dB e gravavam na NVS; a regra agora e que a potencia so muda por
      // comando (hardware.h). O que o chip disse (err) esta no log e no eco:
      // quem decide o que fazer com isso e quem olha o numero.
    } else {
      // NAO desliga o g_loraOk: um modulo que apagou por queda de 5 V volta
      // sozinho quando a tensao volta, e o proximo ciclo tenta de novo.
      Serial.printf("radio: TX preso e begin() falhou (%d) - tentando de novo no proximo ciclo\n", st);
    }
    caixaEtapa(CP_LACO);
  }

  // 2) o que o radio tem a dizer
  if (g_loraIrq) {
    // O carimbo tem 64 bits e a ISR pode escreve-lo entre as duas metades da
    // leitura num CPU de 32 bits - copia com a interrupcao suspensa.
    noInterrupts();
    g_loraIrq = false;
    uint64_t rxUs = g_loraIrqUs;
    interrupts();
    uint32_t flags = g_lora.getIrqFlags();

    if (flags & RADIOLIB_SX126X_IRQ_TX_DONE) {
      g_loraTxEmVoo = false;
      g_txPresoSeguidos = 0;
      g_lora.finishTransmit();
      g_lora.startReceive();
      caixaEtapa(CP_LACO);
      // LINHA T: o air-time MEDIDO (startTransmit -> TX_DONE) e o numero que o
      // slot inteiro depende e que so tinha sido medido em SF7. Se vier
      // sistematicamente acima de radioAirtimeUs(len), o RADIO_TX_EXTRA_US esta
      // curto e a janela esta mentindo.
      uint32_t air = (rxUs > g_txIniUs) ? (uint32_t)(rxUs - g_txIniUs) : 0;
      g_txAirUltUs = air;
      if (air > g_txAirMaxUs) g_txAirMaxUs = air;
      if (air && air < g_txAirMinUs) g_txAirMinUs = air;
      registroLinha("T,%lu,%lu,%u,%c,%u,%lu,%lu,%d,%u",
                    (unsigned long)millis(), (unsigned long)radioUtc(),
                    (unsigned)(uint8_t)(g_loraSeq - 1), g_txTipo, (unsigned)g_txLen,
                    (unsigned long)air, (unsigned long)g_txUsInFrame,
                    g_txCatchup ? 1 : 0, (unsigned)g_txPontos);
      g_txTipo = 0;

    } else if (flags & RADIOLIB_SX126X_IRQ_RX_DONE) {
      // O TAMANHO vem do pacote, nao de uma constante: posicao tem 16 bytes,
      // roster 23 e trilha ate 70. Ler sempre 16 truncaria sem dar erro nenhum.
      uint8_t buf[RADIO_PKT_MAX];
      int n = (int)g_lora.getPacketLength();

      // ZERO NAO E "PACOTE VAZIO" - E "LEIA TUDO", E ISSO ESTOURA A PILHA.
      //
      // Para o RadioLib, readData(buf, 0) significa "descubra o tamanho e leia o
      // pacote inteiro" (SX126x.cpp:608). Como 'buf' esta NA PILHA, um pacote
      // maior que ele escreve por cima da pilha da tarefa - e a falha nao aparece
      // aqui: aparece depois, em qualquer lugar, como Load access fault.
      //
      // Medido em 2026-09-08: com as duas telas trocando pacotes, a gino reiniciava
      // ~10 vezes em 3 minutos com "Core 1 panic'ed (Load access fault)" cujo
      // backtrace caia dentro da ISR da UART do GPS (uart_check_buf_full) - um
      // lugar que nao tem nada a ver com o radio. Pilha corrompida faz isso.
      if (n <= 0 || n > RADIO_PKT_MAX) {
        g_rxRuim++;
        Serial.printf("radio: RX com tamanho invalido (%d) - descartado\n", n);
        registroLinha("R,%lu,%lu,-1,-1,%d,%.0f,%.1f,0,0,tamanho", (unsigned long)millis(),
                      (unsigned long)radioUtc(), n, g_lora.getRSSI(), g_lora.getSNR());
        g_lora.startReceive();
        caixaEtapa(CP_LACO);
        return;
      }
      caixaEtapa(CP_RX_LE);
      int st = g_lora.readData(buf, n);
      caixaEtapa(CP_LACO);
      // RSSI/SNR/erro de frequencia SEMPRE, inclusive com CRC ruim: o pacote
      // corrompido a -95 dBm e o que diz se o CRC ruim e sinal fraco ou ruido.
      float rssi = g_lora.getRSSI();
      float snr  = g_lora.getSNR();
      float ferr = g_lora.getFrequencyError();
      if (st == RADIOLIB_ERR_NONE && n >= TDMA_PKT_N) {
        uint32_t sala = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16);
        if (sala != g_loraRoom) {
          g_rxForaDaSala++;              // outro grupo na mesma frequencia
          registroLinha("R,%lu,%lu,%u,%u,%d,%.0f,%.1f,%.0f,1,0,sala 0x%06lX",
                        (unsigned long)millis(), (unsigned long)radioUtc(),
                        (unsigned)buf[3], (unsigned)buf[4], n, rssi, snr, ferr, (unsigned long)sala);
        } else {
          g_rxOk++;
          g_rxRssi = rssi;
          g_rxSnr  = snr;
          uint8_t slot = buf[3], seq = buf[4];
          // PERDA POR SEQUENCIA deste remetente: quantos pacotes dele pularam
          // desde o ultimo que ouvi. 0 = nenhum. Roster e posicao usam o mesmo
          // contador la, entao a conta vale para os dois.
          uint32_t perdidos = 0;
          if (slot < TDMA_SLOTS) {
            if (g_seqTem[slot]) perdidos = (uint8_t)(seq - g_seqUlt[slot] - 1);
            g_seqTem[slot] = true; g_seqUlt[slot] = seq;
            g_seqPerd[slot] += perdidos; g_rxPorSlot[slot]++;
            g_seqPerdTotal += perdidos;
          }
          // MODO MEDIDA ('m' no serial): uma linha por PACOTE, nao a cada 5 s.
          // Comparar duas antenas exige estatistica - o RSSI oscila varios dB de
          // pacote para pacote por multipercurso, e uma leitura solta nao decide
          // nada. Formato cru de proposito, para o script parsear.
          if (g_rssiVerboso)
            Serial.printf("MEDIDA %u %.0f %.1f\n", (unsigned)slot, rssi, snr);

          if (rosterEh(buf, n)) {
            // Roster NAO ancora o tempo: ele sai no mesmo slot, mas e mais longo,
            // entao o instante do rxDone cai mais tarde que o de um pacote de
            // posicao. Usa-lo como beacon deslocaria a ancora de ~6 ms a cada
            // oito quadros - um erro periodico, que e o pior tipo de erro.
            char nome[ROSTER_NOME_N + 1];
            rosterNome(buf, nome, sizeof(nome));
            mundoRecebeRoster(slot, buf[6], (buf[5] & TDMA_FL_LEADER) != 0, nome);
            registroLinha("R,%lu,%lu,%u,%u,%d,%.0f,%.1f,%.0f,1,%lu,roster",
                          (unsigned long)millis(), (unsigned long)radioUtc(),
                          (unsigned)slot, (unsigned)seq, n, rssi, snr, ferr, (unsigned long)perdidos);
          } else {
            TdmaPkt k;
            if (tdmaUnpack(buf, (uint8_t)n, k)) {
              // QUALQUER pacote de posicao serve de beacon: quem vira a
              // referencia e decidido pelo tdmaBeaconAceita (menor slot ouvido,
              // nunca maior que o proprio). Antes so o slot 0 ancorava, e com o
              // aparelho do slot 0 fora do ar a rede inteira ficava MUDA - era o
              // bug do teste de campo de 2026-08-28. Ver a eleicao no tdma_core.h.
              //
              // O PACOTE TEM TAMANHO VARIAVEL AGORA (16 a 70 B), e o rxDone vem
              // no FIM dele. O tdma_core desconta um air-time FIXO (o do maior
              // pacote); aqui o carimbo e adiantado pela diferenca, para a borda
              // calculada ser a do INICIO do pacote real. Com PPS nos dois lados
              // isto nao e usado; sem PPS, e o que evita um erro de ate 240 ms.
              //
              // LIMITE CONHECIDO do modo beacon: o receptor nao sabe em que ponto
              // da janela a fonte estava quando transmitiu (ate 63 ms), e esse
              // erro entra na ancora de todos. SEM PPS use slots baixos.
              bool srcPps = (k.flags & TDMA_FL_PPS) != 0;
              if (tdmaBeaconAceita(g_tdma, k.slot, srcPps)) {
                uint8_t antes     = g_tdma.anchorSrc;
                bool    eraAncora = g_tdma.isAnchor;
                uint64_t rxAjust = rxUs + (RADIO_AIRTIME_US - radioAirtimeUs((uint32_t)n));
                tdmaOnBeaconFrom(g_tdma, rxAjust, k.slot, srcPps);
                if (antes != k.slot || eraAncora)
                  Serial.printf("radio: fonte de tempo agora e o slot %u%s%s\n", k.slot,
                                srcPps ? " (grade UTC, tem PPS)" : "",
                                eraAncora ? " - eu era a ancora e cedi o lugar" : "");
              }
              mundoRecebePacote(k);
              int novos = 0;
              if (k.flags & TDMA_FL_TRILHA) novos = mundoRecebeTrilha(k, buf, n);
              registroLinha("R,%lu,%lu,%u,%u,%d,%.0f,%.1f,%.0f,1,%lu,pos,%d,%u,%d",
                            (unsigned long)millis(), (unsigned long)radioUtc(),
                            (unsigned)slot, (unsigned)seq, n, rssi, snr, ferr,
                            (unsigned long)perdidos, (int)k.flags,
                            (unsigned)((k.flags & TDMA_FL_TRILHA) ? buf[23] : 0), novos);
            }
          }
        }
      } else {
        g_rxRuim++;                      // CRC ruim: conta como qualidade do enlace
        registroLinha("R,%lu,%lu,-1,-1,%d,%.0f,%.1f,%.0f,0,0,crc %d",
                      (unsigned long)millis(), (unsigned long)radioUtc(), n, rssi, snr, ferr, st);
      }
      g_lora.startReceive();

    } else {
      // Borda sem flag no chip: e residuo do proprio startReceive() ao rearmar.
      // NAO rearmar aqui - rearmar gera a piscada seguinte e o tratador vira a
      // propria fonte de interrupcao. Medido: 900 eventos com o radio parado.
    }
  }

  // 3) e minha vez de falar?
  tdmaTick(g_tdma);
  // A eleicao vem DEPOIS do tick, porque e o tick quem atualiza o holdover em que
  // ela se apoia. Sem esta linha um no que nao ouve ninguem nunca ancora e
  // portanto nunca transmite - foi assim que duas telas passaram um dia inteiro
  // de campo com tx=0 e nada na tela dizendo o porque.
  if (tdmaEleicao(g_tdma))
    Serial.printf("radio: ASSUMI a ancora da rede (slot %u) - ninguem mais estava "
                  "dando o tempo\n", g_tdma.nodeId);
  if (!g_loraTxEmVoo && tdmaShouldTx(g_tdma)) {
    // A PRIMEIRA FALA E SEMPRE POSICAO. Isso nao e ordem arbitraria: o roster
    // NAO ancora ninguem (e mais longo, e usa-lo como referencia deslocaria a
    // ancora de todos - ver o tratador de RX). Comecando por tres rosters, um no
    // que acabou de assumir a ancora passava ~3 s emitindo nada que os outros
    // pudessem seguir - tempo de sobra para o vizinho estourar o proprio relogio
    // de eleicao e virar uma SEGUNDA ancora.
    //
    // Foi assim que a rede se partiu em duas na bancada, em 2026-08-29: slot 0
    // assumiu aos 3 s, so mandou roster, e o slot 1 (relogio em 4 s) assumiu
    // tambem. Os dois ficaram com "fonte=EU" e grades diferentes.
    //
    // Depois da primeira, os tres rosters seguidos continuam - o nome tem de
    // aparecer rapido, senao quem liga fica ate 8 s como "CARRO 3" na tela dos
    // outros e o dono acha que nao funcionou.
    static uint32_t falas = 0;
    bool souRoster = (falas > 0) && ((falas <= 3) || (falas % 8 == 0));
    falas++;
    if (souRoster) radioTransmiteRoster();
    else           radioTransmiteMinhaPosicao();
  }

  // 4) ruido de fundo, quando ha um slot vazio para ouvir
  if (!g_loraTxEmVoo) radioMedeRuido();
}

// ANTES: so o no 0 ancorava, e ancorava aqui, no setup. Com o aparelho do slot 0
// desligado, longe ou sem GPS, ninguem assumia e a rede inteira ficava muda.
// AGORA a ancora e ELEITA em execucao pelo tdmaEleicao(), chamado dentro do
// radioAtualiza(): o menor slot presente assume sozinho apos alguns segundos sem
// ouvir ninguem. Nao ha nada a fazer no setup. A funcao fica, vazia, para nao
// quebrar quem a chama.
inline void radioAncoraSeSouZero() { /* a eleicao cuida disto agora */ }

// Potencia de TX em execucao, para o log e para a tela de diagnostico. Existe
// porque este numero mora na NVS e SOBREVIVE a regravacao: sem le-lo em algum
// lugar visivel nao havia como saber com quantos dBm um aparelho saiu para a
// trilha - e foi essa a duvida que sobrou do teste de campo de 2026-08-28.
inline int8_t radioPotencia()       { return g_rfPwr; }
inline int8_t radioPotenciaConfig() { return g_rfPwrConfig; }
inline bool   radioEmModoSeguro()   { return g_rfSeguro; }
inline bool   radioFiosOk()         { return g_loraFiosOk; }
// "Da para me deixar parado agora?" Quem vai bloquear o laco por centenas de ms
// (a requisicao do eco, o cartao, a pintura da tela cheia) pergunta antes. Um
// bloqueio no meio da minha janela de TDMA custa o slot inteiro do quadro.
inline bool   radioTxEmVoo()     { return g_loraTxEmVoo; }
inline int    radioBeginSt()     { return g_loraBeginSt; }
inline int8_t radioPotenciaAnt() { return (int8_t)(g_rfPwr + RADIO_GANHO_PA_DB); }

// Aplica no chip a potencia que esta em g_rfPwr. Espera um TX em voo terminar
// ANTES: trocar a potencia durante uma transmissao deu ESP_RST_PANIC na hora
// (medido 2026-09-08) - o standby() aborta o TX e deixa g_loraTxEmVoo alto.
// Espera a IRQ direto, sem chamar radioAtualiza(). Devolve false se nao aplicou.
inline bool radioAplicaPotencia()
{
  if (!g_loraOk) return false;
  if (g_loraTxEmVoo) {
    Serial.println("radio: TX em voo - esperando terminar antes de trocar a potencia");
    uint32_t t0 = millis();
    while ((millis() - t0) < RADIO_VIGIA_MS) {
      if (g_loraIrq || (g_lora.getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE)) {
        noInterrupts(); g_loraIrq = false; interrupts();
        g_loraTxEmVoo = false;
        g_lora.finishTransmit();
        break;
      }
      delay(2);
    }
    if (g_loraTxEmVoo) {          // nao completou: o vigia cuida, nao mexemos agora
      Serial.println("radio: TX nao completou - potencia NAO trocada (mande de novo)");
      return false;
    }
  }
  g_lora.standby();
  g_lora.setOutputPower(g_rfPwr, false);              // mesma regra do radioInicia
  g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U); // DEPOIS do setOutputPower
  g_lora.setCurrentLimit(RF_OCP);
  g_lora.startReceive();
  g_txPresoSeguidos = 0;
  Serial.printf("radio: potencia agora e %d dBm (~%d dBm na antena, ganho do modulo +10)%s\n",
                g_rfPwr, g_rfPwr + RADIO_GANHO_PA_DB, g_rfSeguro ? " [modo seguro]" : "");
  return true;
}

// COMANDO EXPLICITO do usuario ('p' no serial, tela de diagnostico). E o UNICO
// caminho que muda a potencia configurada - e ele desliga o modo seguro, porque
// quem mandou o comando sabe o que esta fazendo. Quem grava na NVS e o .ino.
inline void radioTrocaPotencia(int8_t nova)
{
  if (nova < -9) nova = -9;
  if (nova > 22) nova = 22;
  g_rfPwrConfig = nova;
  g_rfSeguro = false;
  g_rfPwr = nova;
  registroLinha("!,%lu,potencia por comando,%d dBm", (unsigned long)millis(), (int)nova);
  radioAplicaPotencia();
}

// MODO SEGURO (caixa.h): teto de RADIO_PWR_SEGURO dBm SO NESTE BOOT, em RAM.
// Chamado no boot, antes do radioInicia, quando a caixa-preta conta
// RADIO_SEGURO_APOS reinicios violentos seguidos. Nao grava nada: no boot
// seguinte a potencia configurada volta sozinha - e se o aparelho reiniciar de
// novo, o teto entra de novo. E o ciclo de reinicio infinito que fica impossivel,
// nao a potencia do usuario.
inline void radioModoSeguro(bool liga)
{
  g_rfSeguro = liga;
  int8_t alvo = g_rfPwrConfig;
  if (liga && alvo > RADIO_PWR_SEGURO) alvo = RADIO_PWR_SEGURO;
  if (alvo == g_rfPwr) return;
  g_rfPwr = alvo;
  if (g_loraOk) radioAplicaPotencia();
}

// Troca o slot EM EXECUCAO, para o seletor da tela de configuracao. O slot e a
// identidade deste aparelho no ar, e ate aqui so mudava pelo serial ('s0'..'s3')
// com reinicio - o que exigia um laptop na trilha. O tdmaInit zera a ancora de
// proposito: com o slot novo, a posicao no frame precisa ser reaprendida (o
// proximo beacon ou par de PPS re-ancora em ate 2 s).
inline void radioTrocaSlot(uint8_t novo)
{
  if (novo >= TDMA_SLOTS) return;
  g_meuSlot = novo;
  g_carros[0].slot = novo;
  if (!g_loraOk) return;
  tdmaInit(g_tdma, novo, TDMA_SLOTS, TDMA_FRAME_S, TDMA_GUARD_US);
  tdmaSetAirtime(g_tdma, RADIO_AIRTIME_US);
  radioAncoraSeSouZero();
  Serial.printf("radio: slot agora e %u (aplicado sem reiniciar)\n", novo);
}
