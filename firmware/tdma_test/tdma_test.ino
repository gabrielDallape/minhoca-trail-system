/*
 * FASE 2 - TDMA free-running (3 nos, coordenadas falsas).
 *
 * Prova que os slots funcionam ANTES de depender do GPS: a ancora do frame e o
 * pacote do no 0 (TDMA_SYNC_BEACON). Na fase 3 troca-se por tdmaOnPps() e mais
 * NADA neste sketch muda - e o ponto da interface em tdma_core.h.
 *
 * O que medir aqui (com 3 nos, que e o minimo onde COLISAO existe):
 *   - rxCRC subindo = colisao ou link ruim
 *   - slotErrado subindo = alinhamento ruim (guarda pequena / loop travado)
 *   - missedTx subindo = o loop perdeu a janela do proprio slot
 *   - idade de cada peer estavel = ninguem passando fome (starvation)
 *
 * Papel: digitar 0..7 no serial define o nodeId (salva na NVS). O no 0 e a
 * ancora - ligue ele primeiro.
 *
 * HARDWARE: mesma fiacao do e22_ping (ver avisos la: 5V, capacitor de bulk,
 * antena antes de energizar). Precisa de 8 GPIOs livres para o E22 -> use um
 * devkit, nao a tela (o painel RGB da 7B nao deixa 8 pinos livres).
 *
 *   .\tools\build.ps1 firmware\tdma_test -Board devkit -Upload -Port COMx
 */
#include <RadioLib.h>
#include <Preferences.h>
#include "../tdma_core.h"

#define BANCADA 1   // 1 = posicao fake (andando em circulo). 0 = espera GPS (fase 4).

// ---------------------------------------------------------------- pinagem
// Igual ao e22_ping. Devkit: pinos de sobra.
#define PIN_SCK   12
#define PIN_MISO  13
#define PIN_MOSI  11
#define PIN_NSS   10
#define PIN_BUSY   9
#define PIN_DIO1   8
#define PIN_NRST  14
#define PIN_RXEN  17
#define PIN_TXEN  18

// ---------------------------------------------------------------- radio RF
#define RF_FREQ  915.0f
#define RF_BW    125.0f
#define RF_SF    7
#define RF_CR    5
#define RF_SYNC  0x12
#define RF_PWR   22
#define RF_PRE   8
#define RF_TCXO  1.8f

// ---------------------------------------------------------------- TDMA
// 8 slots num frame de 1s -> slot de 125ms. Com air-time de ~40-60ms (16B em
// SF7/BW125) + guarda de 15ms sobra folga. CONFERIR com o numero MEDIDO pelo
// e22_ping: o setup imprime a folga e recusa a configuracao se nao couber.
#define N_SLOTS    8
#define FRAME_SECS 1
#define GUARD_US   15000UL
#define ROOM       1

SPIClass    spiLoRa(FSPI);
SX1262      radio = new Module(PIN_NSS, PIN_DIO1, PIN_NRST, PIN_BUSY, spiLoRa, SPISettings(2000000, MSBFIRST, SPI_MODE0));
Preferences prefs;
Tdma        tdma;

// tabela local de vizinhos. Na integracao com o display isso vira o world[] do
// trilha_core.h; aqui fica local p/ o sketch de bancada nao arrastar route[].
struct Peer {
  bool     active;
  double   lat, lon;
  uint8_t  seq, flags;
  float    rssi, snr;
  uint64_t lastUs;
  uint32_t rxCount, lostCount, wrongSlot;
  bool     haveSeq;
};
Peer peers[N_SLOTS];

uint8_t  nodeId = 0, seqOut = 0;
volatile bool rxFlag = false, txFlag = false;
uint32_t rxCRC = 0;
uint64_t lastReport = 0;
double   myLat = -23.5500, myLon = -46.6300;
float    myHeading = 0;

void IRAM_ATTR onRxDone(){ rxFlag = true; }
void IRAM_ATTR onTxDone(){ txFlag = true; }

void setup(){
  Serial.begin(115200);
  delay(400);
  Serial.println("\n== TDMA TEST (fase 2, ancora por beacon) ==");

  prefs.begin("tdma", false);
  nodeId = prefs.getUChar("id", 0);
  prefs.end();

  tdmaInit(tdma, nodeId, N_SLOTS, FRAME_SECS, GUARD_US);
  for (int i = 0; i < N_SLOTS; i++) peers[i] = Peer();

  spiLoRa.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);   // antes do begin: RadioLib nao inicializa o bus
  int st = radio.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, RF_PWR, RF_PRE, RF_TCXO);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] begin FALHOU: %d  (-707 = TCXO: tente 1.6 ou radio.XTAL=true)\n", st);
    while (true) delay(1000);
  }
  radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);   // sem isto o PA nao liga
  radio.setCurrentLimit(140.0);                // sem isto a potencia trava abaixo de 1W
  radio.setPacketReceivedAction(onRxDone);
  radio.setPacketSentAction(onTxDone);

  // Valida a configuracao contra o air-time REAL antes de rodar.
  uint32_t at = radio.getTimeOnAir(TDMA_PKT_N);
  tdmaSetAirtime(tdma, at);   // a ancora do beacon desconta isto (rxDone = FIM do pacote)
  int32_t  hr = tdmaHeadroomUs(tdma, at);
  Serial.printf("[tdma] id=%u slots=%u frame=%lus slot=%lums guarda=%lums\n",
    nodeId, (unsigned)N_SLOTS, (unsigned long)FRAME_SECS,
    (unsigned long)(tdma.slotUs / 1000), (unsigned long)(GUARD_US / 1000));
  Serial.printf("[tdma] airtime(%dB)=%lums -> folga no slot=%ldms | caberia %lu nos\n",
    TDMA_PKT_N, (unsigned long)(at / 1000), (long)(hr / 1000),
    (unsigned long)tdmaMaxNodes(tdma, at));
  if (!tdmaFits(tdma, at)) {
    Serial.println("[tdma] ERRO: o pacote NAO cabe no slot. Reduza N_SLOTS ou aumente FRAME_SECS.");
    while (true) delay(1000);
  }
  if (nodeId >= N_SLOTS) {
    Serial.printf("[tdma] ERRO: nodeId %u >= N_SLOTS %u\n", nodeId, (unsigned)N_SLOTS);
    while (true) delay(1000);
  }
  // Nao e erro - a config e valida se TODOS os nos rotularem anchorSec em UTC.
  // Mas com este frame um unico no rotulando na grade GPS desalinha a rede
  // inteira em silencio, entao o aviso fica visivel no boot. Ver tdma_core.h.
  if (tdma.gridSensitive) {
    Serial.printf("[tdma] AVISO: frame de %lus e SENSIVEL a grade de tempo "
                  "(18 %% %lu = %lu).\n",
      (unsigned long)FRAME_SECS, (unsigned long)FRAME_SECS,
      (unsigned long)(TDMA_GRID_OFFSET_SEC % FRAME_SECS));
    Serial.println("[tdma]        Todo no TEM de alimentar tdmaOnPps com segundo UTC.");
    Serial.println("[tdma]        Em u-blox: CFG-TP5 com gridUtcGps=0. Frames imunes: 1,2,3,6,9,18.");
  }

  // O no 0 e a ancora: comeca o relogio nele mesmo, sem esperar ninguem.
  if (nodeId == 0) { tdmaOnBeacon(tdma, tdmaNowUs()); Serial.println("[tdma] sou a ANCORA (no 0)"); }
  else             Serial.println("[tdma] aguardando beacon do no 0...");

  radio.startReceive();
}

void fakeMove(){
#if BANCADA
  // cada no anda em circulo, com raio/fase por id -> da p/ ver movimento e
  // conferir que as distancias mudam de forma coerente.
  static uint64_t t0 = tdmaNowUs();
  float t = (tdmaNowUs() - t0) / 1e6f;
  float ph = nodeId * 1.7f;
  float r = 0.0025f + nodeId * 0.0004f;      // ~250m
  myLat = -23.5500 + r * sinf(t / 12.0f + ph);
  myLon = -46.6300 + r * cosf(t / 12.0f + ph);
  myHeading = fmodf(t * 8.0f + nodeId * 40.0f, 360.0f);
#endif
}

void transmitPos(){
  TdmaPkt k;
  k.room = ROOM; k.slot = nodeId; k.seq = ++seqOut;
  k.flags = TDMA_FL_FIX | (nodeId == 0 ? TDMA_FL_LEADER : 0);
  k.lat = myLat; k.lon = myLon; k.heading = myHeading; k.speed = 30 + nodeId;

  uint8_t p[TDMA_PKT_N];
  tdmaPack(p, k);
  int st = radio.startTransmit(p, TDMA_PKT_N);
  if (st != RADIOLIB_ERR_NONE) Serial.printf("[tx] startTransmit -> %d\n", st);
}

void loop(){
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '7') {
      nodeId = c - '0';
      prefs.begin("tdma", false); prefs.putUChar("id", nodeId); prefs.end();
      Serial.printf("[cfg] nodeId=%u salvo - REINICIANDO\n", nodeId);
      delay(200); ESP.restart();   // reinicia: nodeId muda o slot e a ancora
    }
  }

  if (txFlag) { txFlag = false; radio.finishTransmit(); radio.startReceive(); }

  if (rxFlag) {
    rxFlag = false;
    uint64_t rxUs = tdmaNowUs();          // marcar ANTES de qualquer trabalho
    uint8_t p[TDMA_PKT_N];
    int st = radio.readData(p, TDMA_PKT_N);
    if (st == RADIOLIB_ERR_NONE) {
      TdmaPkt k;
      if (tdmaUnpack(p, TDMA_PKT_N, k) && k.room == ROOM && k.slot < N_SLOTS && k.slot != nodeId) {
        Peer& q = peers[k.slot];
        // perda por salto de sequencia (seq e uint8 -> diferenca circular)
        if (q.haveSeq) { uint8_t d = (uint8_t)(k.seq - q.seq); if (d > 1) q.lostCount += (d - 1); }
        q.seq = k.seq; q.haveSeq = true;
        q.active = true; q.lat = k.lat; q.lon = k.lon; q.flags = k.flags;
        q.rssi = radio.getRSSI(); q.snr = radio.getSNR(); q.lastUs = rxUs; q.rxCount++;

        // chegou no slot que era esperado? divergencia = desalinhamento
        uint32_t usInFrame, frameIdx;
        if (tdmaUsInFrame(tdma, usInFrame, frameIdx)) {
          if (tdmaSlotAt(tdma, usInFrame) != k.slot) q.wrongSlot++;
        }
        // ANCORA: o pacote do no 0 marca o inicio do frame (fase 3 troca por PPS).
        // A checagem de sync NAO e decorativa: se o PPS entrar por cima do beacon,
        // o sync alterna PPS/BEACON a cada segundo, o frameBase para de avancar
        // (ele so incrementa quando o sync anterior JA era beacon) e o no cala.
        // O beacon so vale enquanto nao houver PPS - ele e o fallback, nao um
        // segundo relogio somado ao primeiro.
        if (k.slot == 0 && nodeId != 0 && tdma.sync != TDMA_SYNC_PPS) tdmaOnBeacon(tdma, rxUs);
      }
    } else {
      rxCRC++;      // CRC ruim: colisao ou link ruim. E o alarme principal aqui.
      radio.startReceive();
    }
  }

  tdmaTick(tdma);       // atualiza holdover
  fakeMove();
  if (tdmaShouldTx(tdma)) transmitPos();

  if (tdmaNowUs() - lastReport > 5000000ULL) {
    lastReport = tdmaNowUs();
    Serial.printf("\n== id=%u sync=%s%s | ancoras=%lu tx=%lu perdiJanela=%lu | rxCRC=%lu\n",
      nodeId,
      tdma.sync == TDMA_SYNC_PPS ? "PPS" : (tdma.sync == TDMA_SYNC_BEACON ? "beacon" : "NENHUMA"),
      tdma.holdover ? " HOLDOVER" : "",
      (unsigned long)tdma.anchorCount, (unsigned long)tdma.txCount,
      (unsigned long)tdma.missedTx, (unsigned long)rxCRC);
    for (int i = 0; i < N_SLOTS; i++) {
      if (!peers[i].active) continue;
      uint32_t ageMs = (uint32_t)((tdmaNowUs() - peers[i].lastUs) / 1000);
      Serial.printf("   slot%d rx=%lu perdidos=%lu slotErrado=%lu rssi=%.0f snr=%.1f idade=%lums %s\n",
        i, (unsigned long)peers[i].rxCount, (unsigned long)peers[i].lostCount,
        (unsigned long)peers[i].wrongSlot, peers[i].rssi, peers[i].snr,
        (unsigned long)ageMs, (peers[i].flags & TDMA_FL_LEADER) ? "[lider]" : "");
    }
  }
}
