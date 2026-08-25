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

// AIR-TIME MEDIDO, nao estimado. 58,0 ms de startTransmit ate o TX_DONE, com
// 16 B em SF7/BW125/CR4-5, medido nas duas placas (jitter de 27 us, diferenca de
// 95 us entre placas). O getTimeOnAir() da biblioteca calcula 51,5 ms - a
// diferenca sao os comandos SPI e a rampa do PA, que ocupam o canal do mesmo
// jeito. O tdma_core usa este numero para recuar a ancora do beacon, entao usar
// o valor teorico aqui deslocaria o frame inteiro em 6,5 ms.
#define RADIO_AIRTIME_US  58000UL

static SPIClass spiLoRa(FSPI);
static SX1262   g_lora = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_NRST,
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
  radioChecaFios();     // avisa e segue: o begin abaixo diz se da para continuar

  spiLoRa.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  int st = g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio: begin() FALHOU (%d)\n", st);
    if (st == -707) Serial.println("radio:   -707 = tensao do TCXO. Confira RF_TCXO (2,2 V).");
    g_loraOk = false;
    return false;
  }
  // Sem setRfSwitchPins o PA externo nunca liga: transmite "nada" sem erro.
  g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
  // DEPOIS do setOutputPower que o begin() ja fez. Com o OCP de fabrica (60 mA)
  // a saida trava em ~19 dBm em vez de 22, sem erro e sem aviso.
  g_lora.setCurrentLimit(RF_OCP);
  g_lora.setDio1Action(loraOnIrq);

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
  Serial.printf("radio: no ar em %.1f MHz, sala 0x%06lX\n", RF_FREQ, (unsigned long)g_loraRoom);
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
inline void radioTransmiteRoster()
{
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
  if (g_carros[0].alerta) k.flags |= TDMA_FL_ALERT;
  if (g_carros[0].lider)  k.flags |= TDMA_FL_LEADER;
  k.lat = g_meuLat; k.lon = g_meuLon;
  // rumo e velocidade sempre existiram no pacote (secao 7 do plano) e iam
  // zerados; agora giram o marcador deste carro na tela dos outros
  k.heading = g_meuRumo;
  k.speed   = g_meuVel;

  uint8_t buf[TDMA_PKT_N];
  tdmaPack(buf, k);

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
  if (g_loraTxEmVoo && millis() - g_loraTxDesdeMs > 400) {
    g_loraTxEmVoo = false;
    g_txPreso++;
    int st = g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
    if (st == RADIOLIB_ERR_NONE) {
      g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
      g_lora.setCurrentLimit(RF_OCP);      // sempre DEPOIS do setOutputPower do begin
      g_lora.setDio1Action(loraOnIrq);
      g_lora.startReceive();
      Serial.printf("radio: TX preso - radio RECONFIGURADO do zero (%lu). "
                    "Se isto se repete, o 5V do modulo esta afundando no TX.\n",
                    (unsigned long)g_txPreso);
    } else {
      // NAO desliga o g_loraOk: um modulo que apagou por queda de 5 V volta
      // sozinho quando a tensao volta, e o proximo ciclo tenta de novo.
      Serial.printf("radio: TX preso e begin() falhou (%d) - tentando de novo no proximo ciclo\n", st);
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
      g_lora.finishTransmit();
      g_lora.startReceive();

    } else if (flags & RADIOLIB_SX126X_IRQ_RX_DONE) {
      // O TAMANHO vem do pacote, nao de uma constante: posicao tem 16 bytes e
      // roster tem 23. Ler sempre 16 truncaria o nome sem dar erro nenhum.
      uint8_t buf[RADIO_PKT_MAX];
      int n = (int)g_lora.getPacketLength();
      if (n > RADIO_PKT_MAX) n = RADIO_PKT_MAX;
      int st = g_lora.readData(buf, n);
      if (st == RADIOLIB_ERR_NONE && n >= TDMA_PKT_N) {
        uint32_t sala = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16);
        if (sala != g_loraRoom) {
          g_rxForaDaSala++;              // outro grupo na mesma frequencia
        } else {
          g_rxOk++;
          g_rxRssi = g_lora.getRSSI();
          g_rxSnr  = g_lora.getSNR();

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
              // O no 0 e a ancora: o pacote de POSICAO dele e tambem o beacon de
              // sincronia, e e o que segura a rede enquanto ninguem tem PPS.
              //
              // LIMITE CONHECIDO do modo beacon: o receptor recua o air-time e
              // acha o INICIO do TX, mas nao sabe em que ponto do slot o no 0
              // estava quando transmitiu - o loop dele pode ter atrasado o TX em
              // ate (slot - guarda - airtime) = 47 ms, e esse erro entra inteiro
              // na ancora de todos. Por isso, SEM PPS use slots baixos (s0..s2):
              // um slot alto somado a esse deslocamento pode vazar no frame
              // seguinte. Com PPS em todos os nos nada disto existe.
              if (k.slot == 0 && g_tdma.nodeId != 0 && g_tdma.sync != TDMA_SYNC_PPS) {
                tdmaOnBeacon(g_tdma, rxUs);
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
  if (!g_loraTxEmVoo && tdmaShouldTx(g_tdma)) {
    // As TRES primeiras falas sao roster, depois um a cada oito.
    // As tres do inicio existem porque o nome tem de aparecer rapido: com so
    // "um a cada oito", quem liga o aparelho fica ate 8 s como CARRO 3 na tela
    // dos outros, e o dono acha que nao funcionou. Depois disso a informacao
    // quase nunca muda, e uma vez a cada 8 s e de sobra.
    static uint32_t falas = 0;
    bool souRoster = (falas < 3) || (falas % 8 == 0);
    falas++;
    if (souRoster) radioTransmiteRoster();
    else           radioTransmiteMinhaPosicao();
  }
}

// O no 0 nao tem quem o sincronize: ele ancora em si mesmo para a rede ter um
// inicio. Sem isto ninguem transmite nunca, porque tdmaShouldTx() exige ancora.
inline void radioAncoraSeSouZero()
{
  if (g_loraOk && g_tdma.nodeId == 0 && !g_tdma.haveAnchor) {
    tdmaOnBeacon(g_tdma, tdmaNowUs());   // mesmo relogio de 64 bits do core
  }
}

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
  g_lora.standby();
  g_lora.setOutputPower(g_rfPwr);
  g_lora.setCurrentLimit(RF_OCP);
  g_lora.startReceive();
  Serial.printf("radio: potencia agora e %d dBm (~%d dBm na antena com o PA)\n",
                g_rfPwr, g_rfPwr + 7);
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
