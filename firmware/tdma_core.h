/*
 * ============================================================================
 *  TDMA CORE - fatiamento do tempo em frames/slots para a rede "siga o lider".
 *
 *  Problema que resolve: hoje o seguidor so fala QUANDO OUVE o lider
 *  (hub-and-spoke). Com 3+ carros isso colide. Aqui o tempo e dividido em
 *  slots e cada no fala SO no seu -> zero colisao, sem lider reencaminhando.
 *
 *  A FONTE DE TEMPO fica atras de uma interface, de proposito:
 *    TDMA_SYNC_BEACON  fase 2 - ancora no beacon do no 0 (sem GPS, na bancada)
 *    TDMA_SYNC_PPS     fase 3 - ancora no pulso 1Hz do GPS (relogio comum real)
 *  Trocar de uma para a outra NAO muda o resto do codigo: e so chamar
 *  tdmaOnBeacon() ou tdmaOnPps(). Toda a decisao de "e o meu slot?" e a mesma.
 *
 *  O frame e medido em SEGUNDOS INTEIROS. Isso nao e estetica: o PPS marca a
 *  borda do segundo UTC, entao um frame de N segundos cai sempre no mesmo lugar
 *  do relogio de todos os nos. Frame fracionario nao ancora em PPS.
 *
 *  Independente de radio e de hardware (nao inclui RadioLib nem trilha_core.h)
 *  -> da para testar a matematica sem placa nenhuma.
 * ============================================================================
 */
#pragma once
#include <Arduino.h>

// -------------------------------------------------- relogio monotonico (us)
// millis()/micros() estouram (micros em ~71min). esp_timer_get_time() e int64.
//
// TDMA_TEST_CLOCK: ponto de costura p/ teste. Definindo esta macro antes do
// include, o tempo passa a vir de uma variavel que o teste controla -> da para
// verificar as janelas de slot de forma determinista, sem esperar em tempo real
// e sem radio nenhum (ver firmware/tdma_selftest).
#ifdef TDMA_TEST_CLOCK
  extern uint64_t tdmaTestNowUs;
  inline uint64_t tdmaNowUs(){ return tdmaTestNowUs; }
#elif defined(ESP_PLATFORM) || defined(ESP32)
  #include "esp_timer.h"
  inline uint64_t tdmaNowUs(){ return (uint64_t)esp_timer_get_time(); }
#else
  inline uint64_t tdmaNowUs(){ return (uint64_t)micros(); }
#endif

enum TdmaSync : uint8_t { TDMA_SYNC_NONE = 0, TDMA_SYNC_BEACON = 1, TDMA_SYNC_PPS = 2 };

// Sem ancora ha mais de 1,5s consideramos HOLDOVER: o relogio interno continua
// contando (drift do ESP32 ~2ms/dia, irrelevante em minutos), mas alargamos a
// guarda porque a confianca no alinhamento caiu.
static const uint32_t TDMA_ANCHOR_TTL_US  = 1500000UL;
static const uint32_t TDMA_HOLDOVER_GUARD = 3;          // multiplicador da guarda

struct Tdma {
  uint8_t  nodeId;      // 0..nSlots-1, unico por carro. 0 = ancora na fase 2
  uint8_t  nSlots;
  uint32_t frameSecs;   // frame em segundos INTEIROS (casa com o PPS)
  uint32_t frameUs;
  uint32_t slotUs;      // frameUs / nSlots
  uint32_t guardUs;     // absorve jitter de ISR/SPI/driver

  TdmaSync sync;
  uint64_t anchorUs;    // instante local da ultima ancora (beacon ou borda PPS)
  uint32_t anchorSec;   // (PPS) segundo UTC daquela borda
  bool     haveAnchor;
  bool     holdover;

  // Contador MONOTONICO de frames, usado no modo beacon.
  // Sem ele haveria bug: o beacon re-ancora a cada frame, entao um frameIdx
  // derivado so de (agora - ancora)/frameUs voltaria a ZERO em cada beacon; como
  // txFrameDone tambem comeca em 0, a guarda de "1 TX por frame" concluiria que
  // o no ja falou e ele PARARIA de transmitir para sempre depois do 1o frame.
  // No modo PPS nao e necessario (o indice vem do UTC, que ja e absoluto).
  uint32_t frameBase;

  uint32_t txFrameDone; // ultimo frame em que ja transmiti -> 1 TX por frame
  bool     haveTxFrame;

  // diagnostico
  uint32_t anchorCount, txCount, missedTx;
};

// airtimeUs deve ser o valor MEDIDO na fase 1 (e22_ping), nao a estimativa.
inline void tdmaInit(Tdma& t, uint8_t nodeId, uint8_t nSlots, uint32_t frameSecs, uint32_t guardUs){
  t.nodeId = nodeId;
  t.nSlots = nSlots ? nSlots : 1;
  t.frameSecs = frameSecs ? frameSecs : 1;
  t.frameUs = t.frameSecs * 1000000UL;
  t.slotUs = t.frameUs / t.nSlots;
  t.guardUs = guardUs;
  t.sync = TDMA_SYNC_NONE;
  t.anchorUs = 0; t.anchorSec = 0;
  t.haveAnchor = false; t.holdover = false;
  t.frameBase = 0;
  t.txFrameDone = 0; t.haveTxFrame = false;
  t.anchorCount = 0; t.txCount = 0; t.missedTx = 0;
}

// Cabe? slot tem de segurar o pacote no ar + a guarda, com folga.
// Retorna a folga em us (negativa = configuracao impossivel).
inline int32_t tdmaHeadroomUs(const Tdma& t, uint32_t airtimeUs){
  return (int32_t)t.slotUs - (int32_t)airtimeUs - (int32_t)t.guardUs;
}
inline bool tdmaFits(const Tdma& t, uint32_t airtimeUs){ return tdmaHeadroomUs(t, airtimeUs) > 0; }

// Maximo de nos que caberia num frame, dado o air-time medido.
inline uint32_t tdmaMaxNodes(const Tdma& t, uint32_t airtimeUs){
  uint32_t per = airtimeUs + t.guardUs;
  return per ? (t.frameUs / per) : 0;
}

// ---------------------------------------------------------------- ancoragem
// FASE 2: chamado quando chega o pacote do no 0. rxUs = instante local do RX.
// O beacon do no 0 marca o inicio do frame (ele fala no slot 0).
inline void tdmaOnBeacon(Tdma& t, uint64_t rxUs){
  if (t.haveAnchor && t.sync == TDMA_SYNC_BEACON && rxUs >= t.anchorUs) {
    // quantos frames se passaram desde a ancora anterior. Se for 0, este beacon
    // caiu no MESMO frame (eco/duplicata) -> nao avanca, senao abriria uma
    // segunda janela de TX no mesmo frame.
    t.frameBase += (uint32_t)((rxUs - t.anchorUs) / t.frameUs);
  }
  t.sync = TDMA_SYNC_BEACON;
  t.anchorUs = rxUs; t.anchorSec = 0;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}

// FASE 3: chamado da flag levantada pela ISR do PPS.
//   ppsUs  = esp_timer_get_time() gravado DENTRO da ISR (nada mais na ISR!)
//   utcSec = segundo UTC daquela borda. O NMEA diz QUAL segundo e; o PPS da a
//            borda. Cuidado: a sentenca NMEA chega DEPOIS do pulso, entao o
//            segundo a casar com a borda e (ss do ultimo NMEA) + 1.
inline void tdmaOnPps(Tdma& t, uint64_t ppsUs, uint32_t utcSec){
  t.sync = TDMA_SYNC_PPS;
  t.anchorUs = ppsUs; t.anchorSec = utcSec;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}

inline uint32_t tdmaGuardNow(const Tdma& t){
  return t.holdover ? t.guardUs * TDMA_HOLDOVER_GUARD : t.guardUs;
}

// Atualiza holdover. Chamar no loop do radio antes de decidir o TX.
inline void tdmaTick(Tdma& t){
  if (!t.haveAnchor) return;
  uint64_t now = tdmaNowUs();
  t.holdover = (now - t.anchorUs) > TDMA_ANCHOR_TTL_US;
}

// ------------------------------------------------------- posicao no frame
// us decorridos desde o inicio do frame corrente (0 .. frameUs-1).
// Retorna false se ainda nao ha ancora (nao transmitir nesse estado).
inline bool tdmaUsInFrame(const Tdma& t, uint32_t& usInFrame, uint32_t& frameIdx){
  if (!t.haveAnchor) return false;
  uint64_t now = tdmaNowUs();
  uint64_t since = now - t.anchorUs;

  if (t.sync == TDMA_SYNC_PPS) {
    // A borda do PPS pode cair no MEIO de um frame: o offset dentro do frame e
    // dado pelo segundo UTC, nao pela borda.
    uint32_t secInFrame = t.anchorSec % t.frameSecs;
    uint64_t off = (uint64_t)secInFrame * 1000000ULL + since;
    usInFrame = (uint32_t)(off % t.frameUs);
    frameIdx  = (uint32_t)((t.anchorSec / t.frameSecs) + (off / t.frameUs));
  } else {
    usInFrame = (uint32_t)(since % t.frameUs);
    // frameBase mantem o indice monotonico atraves das re-ancoras do beacon;
    // o termo (since / frameUs) cobre os frames em que o beacon NAO chegou.
    frameIdx  = t.frameBase + (uint32_t)(since / t.frameUs);
  }
  return true;
}

inline uint32_t tdmaSlotStartUs(const Tdma& t, uint8_t slot){ return (uint32_t)slot * t.slotUs; }

// Qual slot esta ativo agora (util p/ conferir se o pacote veio no slot certo).
inline uint8_t tdmaSlotAt(const Tdma& t, uint32_t usInFrame){
  uint8_t s = (uint8_t)(usInFrame / t.slotUs);
  return s < t.nSlots ? s : (uint8_t)(t.nSlots - 1);
}

// ------------------------------------------------------------ decisao de TX
// true UMA vez por frame, quando estamos dentro da janela util do meu slot:
//   [inicio + guarda/2 , inicio + slotUs - guarda/2]
// A meia-guarda de cada lado deixa o pacote centrado no slot: sobra margem
// tanto p/ atraso de entrada quanto p/ o tempo no ar nao vazar pro vizinho.
inline bool tdmaShouldTx(Tdma& t){
  uint32_t usInFrame, frameIdx;
  if (!tdmaUsInFrame(t, usInFrame, frameIdx)) return false;
  if (t.haveTxFrame && t.txFrameDone == frameIdx) return false;   // ja falei neste frame

  uint32_t g = tdmaGuardNow(t);
  uint32_t half = g / 2;
  uint32_t start = tdmaSlotStartUs(t, t.nodeId);
  if (t.slotUs <= g) return false;                                // slot menor que a guarda: config invalida
  uint32_t lo = start + half;
  uint32_t hi = start + t.slotUs - half;

  if (usInFrame >= lo && usInFrame <= hi) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true; t.txCount++;
    return true;
  }
  // perdi a janela do meu slot neste frame (loop travado?) -> contabiliza
  if (usInFrame > hi && !(t.haveTxFrame && t.txFrameDone == frameIdx)) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true; t.missedTx++;
  }
  return false;
}

// Quanto falta (us) para o inicio da minha janela. Serve p/ vTaskDelayUntil,
// que e como o radio deve esperar: acorda na hora, sem polling.
inline uint32_t tdmaUsUntilMySlot(const Tdma& t){
  uint32_t usInFrame, frameIdx;
  if (!tdmaUsInFrame(t, usInFrame, frameIdx)) return t.slotUs;
  uint32_t lo = tdmaSlotStartUs(t, t.nodeId) + tdmaGuardNow(t) / 2;
  if (usInFrame <= lo) return lo - usInFrame;
  return (t.frameUs - usInFrame) + lo;   // proximo frame
}

// ====================================================================
//  PACOTE DE POSICAO - 16 bytes (secao 7 do PLANO_IMPLEMENTACAO_MESH.md)
//  lat/lon em int32 = grau*1e7 -> ~1,1cm de resolucao. Little-endian.
//  Na fase 2 o pacote do no 0 e TAMBEM o beacon de sincronia: um formato so.
// ====================================================================
#define TDMA_PKT_N 16
#define TDMA_FL_FIX    0x01
#define TDMA_FL_ALERT  0x02
#define TDMA_FL_LEADER 0x04

struct TdmaPkt {
  uint32_t room;      // 24 bits
  uint8_t  slot;      // slot TDMA = identidade
  uint8_t  seq;       // sequencia rolante (detecta perda/reordem)
  uint8_t  flags;
  double   lat, lon;
  float    heading;   // graus
  uint8_t  speed;     // km/h
};

inline void tdmaPutLE32(uint8_t* p, int32_t v){
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
inline int32_t tdmaGetLE32(const uint8_t* p){
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

inline void tdmaPack(uint8_t* p, const TdmaPkt& k){
  p[0] = k.room & 0xFF; p[1] = (k.room >> 8) & 0xFF; p[2] = (k.room >> 16) & 0xFF;
  p[3] = k.slot;
  p[4] = k.seq;
  p[5] = k.flags;
  tdmaPutLE32(p + 6,  (int32_t)(k.lat * 1e7));
  tdmaPutLE32(p + 10, (int32_t)(k.lon * 1e7));
  float h = k.heading; while (h < 0) h += 360.0f; while (h >= 360.0f) h -= 360.0f;
  p[14] = (uint8_t)(h / 2.0f);    // graus/2 cabe em 1 byte (resolucao de 2 graus)
  p[15] = k.speed;
}

inline bool tdmaUnpack(const uint8_t* p, uint8_t len, TdmaPkt& k){
  if (len < TDMA_PKT_N) return false;
  k.room = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  k.slot = p[3];
  k.seq  = p[4];
  k.flags = p[5];
  k.lat = tdmaGetLE32(p + 6)  / 1e7;
  k.lon = tdmaGetLE32(p + 10) / 1e7;
  k.heading = p[14] * 2.0f;
  k.speed = p[15];
  return true;
}

inline bool tdmaPktFix(const TdmaPkt& k){ return k.flags & TDMA_FL_FIX; }
inline bool tdmaPktAlert(const TdmaPkt& k){ return k.flags & TDMA_FL_ALERT; }
inline bool tdmaPktLeader(const TdmaPkt& k){ return k.flags & TDMA_FL_LEADER; }
