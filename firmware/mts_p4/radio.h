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
#pragma once
#include <RadioLib.h>
#include "hardware.h"
#include "gps.h"
#include "../tdma_core.h"

// AIR-TIME. O valor de SF7 era MEDIDO: 58,0 ms de startTransmit ate o TX_DONE,
// com 16 B em SF7/BW125/CR4-5, nas duas placas (jitter de 27 us). O
// getTimeOnAir() da biblioteca calculava 51,5 ms - a diferenca de 6,5 ms sao os
// comandos SPI e a rampa do PA, que ocupam o canal do mesmo jeito.
//
// Com SF9 (2026-08-30) o numero abaixo e CALCULADO, nao medido, e por isso vale
// dizer de onde vem:
//   simbolo   Ts = 2^9 / 125000            = 4,096 ms
//   preambulo (8 + 4,25) x Ts              = 50,2 ms
//   carga     28 simbolos x Ts             = 114,7 ms
//                                            -------
//   teorico                                  164,9 ms
//   + os 6,5 ms de SPI e rampa que a medida de SF7 revelou
//                                            -------
//                                          ~171,4 ms  -> 175000 com folga
//
// O tdma_core usa isto para recuar a ancora do beacon e para dimensionar a
// janela de TX. Se o valor real vier diferente, aparece como 'perdi' subindo ou
// colisao entre slots vizinhos - MEDIR na primeira oportunidade (e22_ping ou o
// carimbo do TX_DONE) e substituir por medida, como era em SF7.
#define RADIO_AIRTIME_US  175000UL

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

// RECUO AUTOMATICO DE POTENCIA.
//
// POR QUE (2026-08-29): uma das tres telas saiu de fabrica com a potencia padrao
// de 22 dBm na NVS enquanto as outras duas tinham 6. Nessa placa, 2 de cada 3
// transmissoes TRAVAVAM - o mesmo colapso medido na escada de potencia daquele
// dia, em que 6 dBm passa e 8 dBm ja falha em 100%. E ela era o SLOT 0: a rede
// inteira dependia dela para se sincronizar, e ficou muda por isso.
//
// O pior nao foi o defeito: foi que nao havia como consertar sem cabo, e o
// serial daquela placa nao chega a lugar nenhum. Entao o radio passa a se
// defender sozinho - se as transmissoes nao completam, ele nao insiste numa
// potencia que este hardware nao sustenta.
//
// NAO sobe de volta sozinho, de proposito: subir e o que causou o problema, e
// uma subida automatica viraria um ciclo de travar/recuar sem fim. Voltar a
// subir e decisao de quem mexeu na alimentacao, pela tela de diagnostico.
static uint8_t  g_txPresoSeguidos = 0;
#define RADIO_PRESO_P_RECUAR  3      // TX travados seguidos antes de recuar
#define RADIO_RECUO_DB        4      // quanto desce por vez
#define RADIO_PWR_PISO        2      // nao desce abaixo disto
// Quem sabe gravar na NVS e o .ino (e ele quem conhece o namespace). O radio so
// avisa que a potencia mudou.
static void (*g_pwrPersiste)(int8_t) = nullptr;
// POTENCIA EM VARIAVEL, nao so no #define: a escada de potencia da bancada
// regravou o firmware seis vezes num dia so para trocar este numero. Vem da
// NVS ("pwr", padrao RF_PWR) e muda pelo serial ('p' + numero). Existe tambem
// porque um dos tres modulos e defeituoso (TCXO nao parte em TX forte) e so
// opera ate ~2-4 dBm: o mesmo binario serve o modulo bom e o capado.
static int8_t   g_rfPwr = RF_PWR;
static uint8_t  g_loraSeq = 0;
static uint32_t g_loraRoom = 0;
static uint32_t g_rxOk = 0, g_rxRuim = 0, g_rxForaDaSala = 0;
static float    g_rxRssi = 0, g_rxSnr = 0;

// Recebe o pacote decodificado. Definido no mundo.h, que e quem conhece os
// carros - o radio nao deve saber o que e um carro.
void mundoRecebePacote(const TdmaPkt& k);
void mundoRecebeRoster(uint8_t slot, uint8_t cor, bool lider, const char* nome);

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
  // DEPOIS do setOutputPower que o begin() ja fez. Com o OCP de fabrica (60 mA)
  // a saida trava em ~19 dBm em vez de 22, sem erro e sem aviso.
  g_lora.setCurrentLimit(RF_OCP);

  // A PA-OPT-TABLE E PARA SX126x NU, NAO PARA MODULO COM PA EXTERNO.
  //
  // A partir da RadioLib 7.5 o setOutputPower(p) escolhe paDutyCycle/hpMax/paVal numa
  // tabela de 32 entradas medida num SX1262 SEM amplificador (issue #1628). Ela NAO e
  // monotonica: pedir +4 dBm programa paVal +22; pedir +6 programa paDutyCycle 3,
  // hpMax 1, paVal +21; pedir +7 cai para hpMax 2, paVal +17.
  //
  // Ou seja: corrente e transiente NAO escalam com o numero pedido, e qualquer escada
  // de potencia feita com ela compara configuracoes arbitrarias de PA em vez de medir
  // potencia. Foi o que aconteceu na escada de 2026-08-28.
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

  g_loraRoom = radioHashSala(codigoSala);
  tdmaInit(g_tdma, meuSlot, TDMA_SLOTS, TDMA_FRAME_S, TDMA_GUARD_US);
  tdmaSetAirtime(g_tdma, RADIO_AIRTIME_US);

  int32_t folga = tdmaHeadroomUs(g_tdma, RADIO_AIRTIME_US);
  Serial.printf("radio: slot %u de %d | frame %ds | slot %lu us | folga %ld us\n",
                meuSlot, TDMA_SLOTS, TDMA_FRAME_S,
                (unsigned long)g_tdma.slotUs, (long)folga);
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
                "(~%d dBm na antena com o PA)\n",
                RF_FREQ, (unsigned long)g_loraRoom, g_rfPwr,
                g_rfPwr + RADIO_GANHO_PA_DB);
  if (g_rfPwr < RF_PWR) {
    // A potencia vem da NVS e sobrevive a regravacao. Uma escada de bancada que
    // deixou 2 dBm gravados manda o aparelho para a trilha com 20 dB a menos -
    // cerca de 10x menos alcance - sem nada na tela dizendo isso. Grita no boot.
    Serial.printf("radio: *** ATENCAO: %d dBm e MENOS que o padrao (%d dBm). Veio "
                  "da NVS, nao do firmware. Use 'p%d' no serial para voltar. ***\n",
                  g_rfPwr, RF_PWR, RF_PWR);
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
#define ROSTER_FL       0x80        // bit livre nas flags: 0x01/0x02/0x04 sao FIX/ALERTA/LIDER
#define ROSTER_NOME_N   16
#define ROSTER_PKT_N    (7 + ROSTER_NOME_N)     // 23 bytes
#define RADIO_PKT_MAX   ROSTER_PKT_N

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
// isso a pergunta e feita AQUI, antes, com relogio nosso.
//
// 5 ms e folga larga: depois de um comando o SX1262 solta o BUSY em dezenas de
// microssegundos; a espera mais longa e a calibracao pos-reset, ~1 ms (radioChecaFios).
inline bool radioBusyLivre()
{
  uint32_t t0 = micros();
  while (digitalRead(PIN_LORA_BUSY) && (micros() - t0) < 5000) { }
  return digitalRead(PIN_LORA_BUSY) == LOW;
}

inline void radioTransmiteRoster()
{
  // Ver radioBusyLivre acima: sem esta guarda o laco principal morre dentro do
  // startTransmit. Marca TX em voo para o vigia dos 400 ms fazer a reconfiguracao
  // pelo caminho que ja existe - e para o 'preso' ser contado uma vez so, la.
  if (!radioBusyLivre()) {
    g_loraTxEmVoo   = true;
    g_loraTxDesdeMs = millis();
    Serial.println("radio: BUSY preso alto - NAO transmiti (roster); o vigia reconfigura");
    return;
  }
  uint8_t buf[RADIO_PKT_MAX];
  int n = rosterPack(buf, g_loraRoom, g_tdma.nodeId, g_loraSeq++,
                     g_carros[0].cor, g_carros[0].lider, g_carros[0].nome);
  g_lora.standby();
  g_loraTxEmVoo = true;
  g_loraTxDesdeMs = millis();
  int st = g_lora.startTransmit(buf, n);
  if (st != RADIOLIB_ERR_NONE) {
    g_loraTxEmVoo = false;
    Serial.printf("radio: startTransmit(roster) -> %d\n", st);
    g_lora.startReceive();
  }
}

inline void radioTransmiteMinhaPosicao()
{
  TdmaPkt k;
  k.room  = g_loraRoom;
  k.slot  = g_tdma.nodeId;
  k.seq   = g_loraSeq++;
  k.flags = 0;
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

  uint8_t buf[TDMA_PKT_N];
  tdmaPack(buf, k);

  // Mesma guarda do roster - ver radioBusyLivre().
  if (!radioBusyLivre()) {
    g_loraTxEmVoo   = true;
    g_loraTxDesdeMs = millis();
    Serial.println("radio: BUSY preso alto - NAO transmiti (posicao); o vigia reconfigura");
    return;
  }

  g_lora.standby();          // ver o item 2 do cabecalho: sem isto, -1 sempre
  g_loraTxEmVoo = true;
  g_loraTxDesdeMs = millis();
  int st = g_lora.startTransmit(buf, TDMA_PKT_N);
  if (st != RADIOLIB_ERR_NONE) {
    g_loraTxEmVoo = false;
    Serial.printf("radio: startTransmit -> %d\n", st);
    g_lora.startReceive();
  }
}

inline void radioTrocaPotencia(int8_t nova);   // definida adiante; o recuo usa

inline void radioAtualiza()
{
  if (!g_loraOk) return;

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
  // parado mais de 400 ms, um TX que COMPLETOU chega aqui parecendo travado, e o
  // "conserto" era reconfigurar um radio que estava perfeito.
  //
  // E o laco fica parado mesmo: a requisicao HTTPS do eco bloqueia por centenas
  // de ms. Medido em 2026-08-29 - das tres telas na bancada, so a que estava com
  // WiFi ativo acusava 'preso', e o log mostra a acusacao vindo na linha
  // seguinte ao "eco: subiu -> HTTP 200". As outras duas, mesma potencia e mesmo
  // firmware, ficaram em preso=0.
  //
  // Uma leitura de SPI resolve - mas NAO BASTA IGNORAR O CASO.
  //
  // A primeira versao desta correcao so pulava o bloco quando via o TX_DONE no
  // chip, na expectativa de que o tratador de IRQ abaixo cuidasse. So que se a
  // ISR NAO correu (borda perdida no DIO1), a flag dela esta baixa, o tratador
  // nao roda, e ninguem abaixa o g_loraTxEmVoo: o no transmite UM pacote e fica
  // mudo para sempre, com tx congelado e preso=0. Trocar um falso positivo por
  // um travamento permanente e um negocio pessimo. Medido em 2026-08-29.
  //
  // Agora, se o chip diz que terminou, a gente ASSUME o evento: levanta a flag
  // e deixa o tratador finalizar de verdade neste mesmo ciclo.
  bool venceuOTempo = g_loraTxEmVoo && (millis() - g_loraTxDesdeMs > 400) && !g_loraIrq;
  if (venceuOTempo && (g_lora.getIrqFlags() & RADIOLIB_SX126X_IRQ_TX_DONE)) {
    g_loraIrqUs = (uint64_t)esp_timer_get_time();   // so o RX usa o carimbo
    g_loraIrq = true;
    venceuOTempo = false;
  }
  if (venceuOTempo) {
    g_loraTxEmVoo = false;
    g_txPreso++;
    g_txPresoSeguidos++;

    // PERGUNTE AO CHIP POR QUE FALHOU, ANTES DE CONSERTAR.
    //
    // A sequencia de um TX e: XOSC liga -> PLL prende -> PA liga -> RAMPA -> pacote
    // -> TX_DONE (datasheet 13.4.1). Um "preso" e sempre uma falha num dos tres
    // primeiros passos, e o chip diz em qual - de graca, numa leitura de SPI.
    //
    // Nenhum bit aceso + TX que nao completou = o OCP cortou a corrente do PA. O
    // chip nao reporta isso, entao a AUSENCIA de erro tambem e diagnostico.
    //
    // O e22_ping ja sabia ler isto desde 2026-08-24; o firmware de producao nao, e
    // por isso cada 'preso' era so um numero em vez de um veredito.
    uint16_t err = g_lora.getDeviceErrors();
    g_ultErrDisp = err;
    Serial.printf("radio: TX preso (%lu) - erros do chip = 0x%04X%s%s%s\n",
                  (unsigned long)g_txPreso, err,
                  (err & (1u << 5)) ? "  XOSC_NAO_PARTIU(TCXO)" : "",
                  (err & (1u << 6)) ? "  PLL_NAO_PRENDEU"       : "",
                  (err & (1u << 8)) ? "  RAMPA_DO_PA"           : "");
    if (!err) Serial.println("radio:   nenhum erro no chip -> suspeita de corte por OCP "
                             "(queda de tensao no 5V do modulo)");

    int st = g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
    if (st == RADIOLIB_ERR_NONE) {
      #if RF_SWITCH_TROCADO
  g_lora.setRfSwitchPins(PIN_LORA_TXEN, PIN_LORA_RXEN);   // ver RF_SWITCH_TROCADO
#else
  g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
#endif
      g_lora.setCurrentLimit(RF_OCP);      // o begin() deixa 60 mA; ver radioInicia
      g_lora.setDio1Action(loraOnIrq);
      g_lora.clearDeviceErrors();          // o begin refez o cold start: limpa o aviso
      g_lora.startReceive();
      Serial.printf("radio: TX preso - radio RECONFIGURADO do zero (%lu). "
                    "Se isto se repete, o 5V do modulo esta afundando no TX.\n",
                    (unsigned long)g_txPreso);
      // O RECUO SO ACONTECE COM O CHIP VIVO - e por isso ele mora AQUI DENTRO.
      //
      // Ate 2026-09-08 este bloco ficava fora do 'if', e entao um modulo que nem
      // respondia ao begin() ainda contava 'preso' e derrubava a potencia. A auditoria
      // chamou de catraca: a falha e transitoria, a punicao era permanente (grava na
      // NVS e nao sobe de volta sozinho).
      //
      // E agora o veredito considera o que o CHIP disse (err, lido no topo do bloco):
      //   RAMPA_DO_PA, ou nenhum erro (corte por OCP) -> e potencia: recuar ajuda
      //   XOSC / PLL                                  -> e TCXO ou modulo: recuar nao ajuda
#if RADIO_RECUO_AUTO
      const bool culpaDaPotencia = (err == 0) || (err & (1u << 8));
      if (culpaDaPotencia && g_txPresoSeguidos >= RADIO_PRESO_P_RECUAR
          && g_rfPwr > RADIO_PWR_PISO) {
        int8_t nova = (int8_t)(g_rfPwr - RADIO_RECUO_DB);
        if (nova < RADIO_PWR_PISO) nova = RADIO_PWR_PISO;
        Serial.printf("radio: *** %u TX travados em %d dBm (erros=0x%04X) - RECUANDO "
                      "para %d dBm (~%d na antena) ***\n",
                      g_txPresoSeguidos, g_rfPwr, err, nova, nova + RADIO_GANHO_PA_DB);
        Serial.println("radio:     nao subo de volta sozinho - use a tela de diagnostico");
        Serial.println("radio:     depois de melhorar a alimentacao do modulo.");
        radioTrocaPotencia(nova);
        if (g_pwrPersiste) g_pwrPersiste(nova);
        g_txPresoSeguidos = 0;
      } else if (g_txPresoSeguidos >= RADIO_PRESO_P_RECUAR) {
        Serial.printf("radio: %u TX travados, mas os erros do chip (0x%04X) nao apontam "
                      "potencia - NAO recuo.\n", g_txPresoSeguidos, err);
      }
#endif
    } else {
      // NAO desliga o g_loraOk: um modulo que apagou por queda de 5 V volta
      // sozinho quando a tensao volta, e o proximo ciclo tenta de novo.
      // E NAO conta para o recuo: chip que nao responde nao e questao de potencia.
      Serial.printf("radio: TX preso e begin() falhou (%d) - tentando de novo no proximo ciclo\n", st);
      if (g_txPresoSeguidos) g_txPresoSeguidos--;
    }

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
      g_txPresoSeguidos = 0;      // completou: a potencia atual se sustenta
      g_lora.finishTransmit();
      g_lora.startReceive();

    } else if (flags & RADIOLIB_SX126X_IRQ_RX_DONE) {
      // O TAMANHO vem do pacote, nao de uma constante: posicao tem 16 bytes e
      // roster tem 23. Ler sempre 16 truncaria o nome sem dar erro nenhum.
      uint8_t buf[RADIO_PKT_MAX];
      int n = (int)g_lora.getPacketLength();

      // ZERO NAO E "PACOTE VAZIO" - E "LEIA TUDO", E ISSO ESTOURA A PILHA.
      //
      // Para o RadioLib, readData(buf, 0) significa "descubra o tamanho e leia o
      // pacote inteiro" (SX126x.cpp:608). Como 'buf' tem 23 BYTES E ESTA NA PILHA,
      // um pacote maior que isso escreve por cima da pilha da tarefa - e a falha
      // nao aparece aqui: aparece depois, em qualquer lugar, como Load access fault.
      //
      // Medido em 2026-09-08: com as duas telas trocando pacotes, a gino reiniciava
      // ~10 vezes em 3 minutos com "Core 1 panic'ed (Load access fault)" cujo
      // backtrace caia dentro da ISR da UART do GPS (uart_check_buf_full) - um
      // lugar que nao tem nada a ver com o radio. Pilha corrompida faz isso.
      //
      // Um TX sozinho nunca expoe o bug, porque nao ha RX. So aparece com dois nos.
      if (n <= 0 || n > RADIO_PKT_MAX) {
        g_rxRuim++;
        Serial.printf("radio: RX com tamanho invalido (%d) - descartado\n", n);
        g_lora.startReceive();
        return;
      }
      int st = g_lora.readData(buf, n);
      if (st == RADIOLIB_ERR_NONE && n >= TDMA_PKT_N) {
        uint32_t sala = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16);
        if (sala != g_loraRoom) {
          g_rxForaDaSala++;              // outro grupo na mesma frequencia
        } else {
          g_rxOk++;
          g_rxRssi = g_lora.getRSSI();
          g_rxSnr  = g_lora.getSNR();
          // MODO MEDIDA ('m' no serial): uma linha por PACOTE, nao a cada 5 s.
          // Comparar duas antenas exige estatistica - o RSSI oscila varios dB de
          // pacote para pacote por multipercurso, e uma leitura solta nao decide
          // nada. Com 1 amostra por segundo por vizinho, um minuto ja da mediana
          // confiavel. Formato cru de proposito, para o script parsear.
          if (g_rssiVerboso)
            Serial.printf("MEDIDA %u %.0f %.1f\n", (unsigned)buf[3], g_rxRssi, g_rxSnr);

          if (rosterEh(buf, n)) {
            // Roster NAO ancora o tempo: ele sai no mesmo slot, mas e mais longo,
            // entao o instante do rxDone cai mais tarde que o de um pacote de
            // posicao. Usa-lo como beacon deslocaria a ancora de ~6 ms a cada
            // oito quadros - um erro periodico, que e o pior tipo de erro.
            char nome[ROSTER_NOME_N + 1];
            rosterNome(buf, nome, sizeof(nome));
            mundoRecebeRoster(buf[3], buf[6], (buf[5] & TDMA_FL_LEADER) != 0, nome);
          } else {
            TdmaPkt k;
            if (tdmaUnpack(buf, (uint8_t)n, k)) {
              // QUALQUER pacote de posicao serve de beacon: quem vira a
              // referencia e decidido pelo tdmaBeaconAceita (menor slot ouvido,
              // nunca maior que o proprio). Antes so o slot 0 ancorava, e com o
              // aparelho do slot 0 fora do ar a rede inteira ficava MUDA - era o
              // bug do teste de campo de 2026-08-28, lido no serial das telas
              // como sinc=- e tx=0. Ver a eleicao no tdma_core.h.
              //
              // LIMITE CONHECIDO do modo beacon: o receptor recua o air-time e o
              // offset do slot da fonte, e assim acha o INICIO do frame, mas nao
              // sabe em que ponto do slot a fonte estava quando transmitiu - o
              // loop dela pode ter atrasado o TX em ate (slot - guarda - airtime)
              // = 47 ms, e esse erro entra inteiro na ancora de todos. Por isso,
              // SEM PPS use slots baixos (s0..s2): um slot alto somado a esse
              // deslocamento pode vazar no frame seguinte. Com PPS nao existe.
              bool srcPps = (k.flags & TDMA_FL_PPS) != 0;
              if (tdmaBeaconAceita(g_tdma, k.slot, srcPps)) {
                uint8_t antes     = g_tdma.anchorSrc;
                bool    eraAncora = g_tdma.isAnchor;
                tdmaOnBeaconFrom(g_tdma, rxUs, k.slot, srcPps);
                if (antes != k.slot || eraAncora)
                  Serial.printf("radio: fonte de tempo agora e o slot %u%s%s\n", k.slot,
                                srcPps ? " (grade UTC, tem PPS)" : "",
                                eraAncora ? " - eu era a ancora e cedi o lugar" : "");
              }
              mundoRecebePacote(k);
            }
          }
        }
      } else {
        g_rxRuim++;                      // CRC ruim: conta como qualidade do enlace
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
inline int8_t radioPotencia()    { return g_rfPwr; }
inline bool   radioFiosOk()      { return g_loraFiosOk; }
// "Da para me deixar parado agora?" Quem vai bloquear o laco por centenas de ms
// (a requisicao do eco, o cartao, a pintura da tela cheia) pergunta antes. Um
// bloqueio no meio da minha janela de TDMA custa o slot inteiro do quadro.
inline bool   radioTxEmVoo()     { return g_loraTxEmVoo; }
inline int    radioBeginSt()     { return g_loraBeginSt; }
inline int8_t radioPotenciaAnt() { return (int8_t)(g_rfPwr + RADIO_GANHO_PA_DB); }

// Troca o slot EM EXECUCAO, para o seletor da tela de configuracao. O slot e a
// identidade deste aparelho no ar, e ate aqui so mudava pelo serial ('s0'..'s7')
// com reinicio - o que exigia um laptop na trilha. O tdmaInit zera a ancora de
// proposito: com o slot novo, a posicao no frame precisa ser reaprendida (o
// proximo beacon ou par de PPS re-ancora em ate 2 s; o no 0 ancora em si mesmo).
// Troca a potencia EM EXECUCAO ('p' + numero no serial). A ordem e normativa:
// setCurrentLimit SEMPRE depois do setOutputPower, senao o OCP volta a
// estrangular a saida (datasheet SX1261/2 tab. 5-2, ja mordeu uma vez).
inline void radioTrocaPotencia(int8_t nova)
{
  if (nova < -9) nova = -9;
  if (nova > 22) nova = 22;
  g_rfPwr = nova;
  if (!g_loraOk) return;
  // NAO MEXER NO CHIP COM TX EM VOO - e esperar de verdade, nao so avisar.
  //
  // Medido em 2026-09-08, na primeira escada com o firmware novo: trocar a potencia
  // durante uma transmissao deu ESP_RST_PANIC (motivo 4) na hora, e o resgate do boot
  // ainda desceu a potencia 4 dB por causa disso. O 'preso' contado nesse degrau nao
  // era do radio: era deste caminho.
  //
  // O standby() aborta o TX em andamento e deixa g_loraTxEmVoo alto; 400 ms depois o
  // vigia conta um preso que nao houve, e o pior e o que aconteceu antes disso.
  //
  // Um pacote em SF9 dura ~175 ms, entao 400 ms de espera cobrem o pior caso com folga.
  // Espera a IRQ direto, sem chamar radioAtualiza(): ela pode chamar de volta esta
  // funcao pelo recuo automatico, e isso seria recursao.
  if (g_loraTxEmVoo) {
    Serial.println("radio: TX em voo - esperando terminar antes de trocar a potencia");
    uint32_t t0 = millis();
    while ((millis() - t0) < 400) {
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
      return;
    }
  }
  g_lora.standby();
  g_lora.setOutputPower(g_rfPwr, false);              // mesma regra do radioInicia
  g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U); // DEPOIS do setOutputPower
  g_lora.setCurrentLimit(RF_OCP);
  g_lora.startReceive();
  g_txPresoSeguidos = 0;      // potencia nova, contagem nova
  Serial.printf("radio: potencia agora e %d dBm (~%d dBm na antena, ganho do modulo +10)\n",
                g_rfPwr, g_rfPwr + RADIO_GANHO_PA_DB);
}

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
