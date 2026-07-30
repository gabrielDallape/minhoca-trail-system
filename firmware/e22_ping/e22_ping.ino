/*
 * FASE 1 - RADIO CRU: ping-pong SX1262 (E22-900M30S) + MEDICAO de air-time.
 *
 * Objetivo: provar o link e COLHER OS NUMEROS que dimensionam o slot do TDMA
 * (fase 2). O plano estima air-time de 40-60ms p/ 16 bytes em SF7/BW125 -
 * aqui a gente MEDE em vez de estimar.
 *
 * Papel: no 0 manda beacon 1Hz; nos 1..N respondem com eco (offset por id, p/
 * dois respondentes nao colidirem). Trocar papel: digitar 0/1/2 no serial
 * (salva na NVS, igual ao grupo_ws).
 *
 * HARDWARE (ver AVISOS abaixo - nao energizar sem antena):
 *   E22-900M30S: VCC=5V (nao 3.3V!) + capacitor de bulk >=470uF perto do modulo.
 *   Pico de TX >600mA -> sem o capacitor a placa RESETA ao transmitir.
 *   Antena TX915 no IPEX ANTES de energizar. Manter longe da antena do GPS
 *   (1W em 915MHz dessensibiliza o receptor GNSS de 1575MHz).
 *   Logica SPI = 3.3V (casa direto, sem level shifter).
 *
 * Gravar: --fqbn "...CDCOnBoot=cdc"  (Serial pela USB nativa; libera 43/44)
 *   .\tools\build.ps1 firmware\e22_ping -Board devkit -Upload -Port COMx
 */
#include <RadioLib.h>
#include <Preferences.h>

// ---------------------------------------------------------------- pinagem
// BOARD_DEVKIT=1 -> ESP32-S3-DevKitC-1 (bancada de radio: pinos de sobra)
// BOARD_DEVKIT=0 -> Waveshare 7B. ATENCAO: na 7B sobram so 7 GPIOs livres
//   (11,12,13,15,16,43,44) e o E22 precisa de 8 com RXEN/TXEN. Nessa placa
//   roda so com NRST amarrado no 3V3 (perde reset por software) E sem PPS.
//   Por isso a bancada de radio deveria ser um devkit, nao a tela.
#define BOARD_DEVKIT 1

#if BOARD_DEVKIT
  #define PIN_SCK   12
  #define PIN_MISO  13
  #define PIN_MOSI  11
  #define PIN_NSS   10
  #define PIN_BUSY   9
  #define PIN_DIO1   8
  #define PIN_NRST  14
  #define PIN_RXEN  17
  #define PIN_TXEN  18
#else
  // Waveshare 7B, aperto de pinos: NRST em NC (amarrar no 3V3 com pull-up).
  #define PIN_SCK   12
  #define PIN_MISO  13
  #define PIN_MOSI  11
  #define PIN_NSS   15
  #define PIN_BUSY  16
  #define PIN_DIO1  43
  #define PIN_NRST  RADIOLIB_NC
  #define PIN_RXEN  44
  #define PIN_TXEN  RADIOLIB_NC   // <- nao ha pino: o PA nao vai ligar. Ver aviso.
#endif

// ---------------------------------------------------------------- radio RF
// Mesmos parametros do plano (PLANO_IMPLEMENTACAO_MESH.md secao 5).
#define RF_FREQ   915.0f
#define RF_BW     125.0f
#define RF_SF     7
#define RF_CR     5        // RadioLib usa 5..8 = 4/5..4/8
#define RF_SYNC   0x12
#define RF_PWR    22       // MAXIMO do SX1262; o PA do E22 soma ~7dB (~29,6dBm medido)
#define RF_PRE    8
#define RF_TCXO   1.8f     // a etiqueta do lote diz TCXO 32MHz. Se der -707: testar 1.6

#define PKT_N     16       // tamanho do pacote real do projeto (secao 7 do plano)
#define BEACON_MS 1000

SPIClass    spiLoRa(FSPI);
SX1262      radio = new Module(PIN_NSS, PIN_DIO1, PIN_NRST, PIN_BUSY, spiLoRa, SPISettings(2000000, MSBFIRST, SPI_MODE0));
Preferences prefs;

uint8_t nodeId = 0;
volatile bool rxFlag = false, txFlag = false;
volatile uint32_t txDoneUs = 0;   // marcado DENTRO da ISR: e o instante real do fim do TX

uint32_t txStartUs = 0, seqOut = 0;
uint32_t rxOk = 0, rxBad = 0, lostCnt = 0;
uint32_t lastSeqIn = 0; bool haveSeqIn = false;
uint32_t lastBeacon = 0, lastReport = 0;
// estatistica do tempo de TX medido (startTransmit -> txDone). E O NUMERO QUE
// DIMENSIONA O SLOT: air-time + overhead de SPI/driver.
uint32_t txMin = 0xFFFFFFFF, txMax = 0, txSum = 0, txCnt = 0;
float lastRssi = 0, lastSnr = 0;

void IRAM_ATTR onRxDone(){ rxFlag = true; }
void IRAM_ATTR onTxDone(){ txDoneUs = micros(); txFlag = true; }

// -------------------------------------------------------------- diagnostico
void explainBeginError(int st){
  Serial.printf("[radio] begin() FALHOU: %d\n", st);
  if (st == RADIOLIB_ERR_SPI_CMD_FAILED || st == -707) {
    Serial.println("  -707 = quase sempre TCXO errado. Tente nesta ordem:");
    Serial.println("   1) RF_TCXO 1.6 em vez de 1.8");
    Serial.println("   2) 'radio.XTAL = true;' antes do begin (lotes do E22 com XTAL)");
  }
  if (st == RADIOLIB_ERR_CHIP_NOT_FOUND) {
    Serial.println("  chip nao encontrado -> confira MISO/MOSI/SCK/NSS, BUSY e o 5V do modulo");
  }
}

void printAirTimeTable(){
  Serial.println("[airtime] getTimeOnAir (SF7/BW125/CR4_5), calculado pela lib:");
  const int sizes[] = {8, 16, 24, 32, 64};
  for (int i = 0; i < 5; i++) {
    uint32_t us = radio.getTimeOnAir(sizes[i]);
    Serial.printf("   %2d B -> %6lu us (%.1f ms)\n", sizes[i], (unsigned long)us, us / 1000.0f);
  }
  // Dimensionamento: slots_por_frame = frame_us / (air_time + guard).
  uint32_t at = radio.getTimeOnAir(PKT_N);
  uint32_t slot = at + 15000;   // guard de 15ms (secao 6 do plano)
  Serial.printf("[airtime] com %dB + guarda 15ms -> slot ~%lu ms -> ~%lu slots/s\n",
                PKT_N, (unsigned long)(slot / 1000), (unsigned long)(1000000UL / slot));
  Serial.println("[airtime] CONFERIR com calculadora externa: getTimeOnAir tem bug conhecido de CR.");
}

void setup(){
  Serial.begin(115200);
  delay(400);
  Serial.println("\n== E22 PING (fase 1) ==");

  prefs.begin("e22", false);
  nodeId = prefs.getUChar("id", 0);
  prefs.end();
  Serial.printf("[cfg] nodeId=%u  (digite 0/1/2 no serial p/ trocar)\n", nodeId);

#if !BOARD_DEVKIT
  Serial.println("[AVISO] BOARD_DEVKIT=0: nao ha pino p/ TXEN nesta placa.");
  Serial.println("        Sem TXEN o PA do E22 NAO liga -> 'transmite nada'.");
  Serial.println("        Use um devkit p/ a bancada de radio, ou libere um GPIO.");
#endif

  spiLoRa.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);   // RadioLib NAO inicializa o bus

  int st = radio.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, RF_PWR, RF_PRE, RF_TCXO);
  if (st != RADIOLIB_ERR_NONE) { explainBeginError(st); while (true) delay(1000); }
  Serial.println("[radio] begin OK");

  // ARMADILHA 1: sem setRfSwitchPins o PA nunca liga. ORDEM: (rxEn, txEn).
  radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);
  // ARMADILHA 2: OCP padrao (~60mA) estrangula a potencia. Liberar p/ ~1W.
  st = radio.setCurrentLimit(140.0);
  if (st != RADIOLIB_ERR_NONE) Serial.printf("[radio] setCurrentLimit -> %d\n", st);

  radio.setPacketReceivedAction(onRxDone);
  radio.setPacketSentAction(onTxDone);

  printAirTimeTable();

  radio.startReceive();
  Serial.println("[radio] escutando\n");
}

// monta o pacote de teste: [id][seq LE32][padding] = PKT_N bytes
void buildPkt(uint8_t* p, uint8_t id, uint32_t seq){
  memset(p, 0, PKT_N);
  p[0] = id;
  p[1] = seq & 0xFF; p[2] = (seq >> 8) & 0xFF; p[3] = (seq >> 16) & 0xFF; p[4] = (seq >> 24) & 0xFF;
}

void transmit(uint8_t* p){
  txFlag = false; txDoneUs = 0;
  txStartUs = micros();
  int st = radio.startTransmit(p, PKT_N);
  if (st != RADIOLIB_ERR_NONE) Serial.printf("[tx] startTransmit -> %d\n", st);
}

void loop(){
  // troca de papel pelo serial (mesmo padrao do grupo_ws.ino)
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '7') {
      nodeId = c - '0';
      prefs.begin("e22", false); prefs.putUChar("id", nodeId); prefs.end();
      Serial.printf("[cfg] nodeId=%u (salvo)\n", nodeId);
    }
  }

  // fim de transmissao -> mede o tempo REAL de TX e volta a escutar
  if (txFlag) {
    txFlag = false;
    uint32_t dt = txDoneUs - txStartUs;
    if (dt < txMin) txMin = dt;
    if (dt > txMax) txMax = dt;
    txSum += dt; txCnt++;
    radio.finishTransmit();
    radio.startReceive();
  }

  if (rxFlag) {
    rxFlag = false;
    uint8_t p[PKT_N];
    int st = radio.readData(p, PKT_N);
    if (st == RADIOLIB_ERR_NONE) {
      rxOk++;
      lastRssi = radio.getRSSI();
      lastSnr  = radio.getSNR();
      uint8_t  srcId = p[0];
      uint32_t seq = (uint32_t)p[1] | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);

      // perda por salto de contador (mesma tecnica do legacy/lora_follow_counter)
      if (srcId == 0) {
        if (haveSeqIn && seq > lastSeqIn + 1) lostCnt += (seq - lastSeqIn - 1);
        lastSeqIn = seq; haveSeqIn = true;
      }
      Serial.printf("[rx] de=%u seq=%lu rssi=%.1f snr=%.1f\n", srcId, (unsigned long)seq, lastRssi, lastSnr);

      // no !=0 responde com eco. Offset por id p/ dois respondentes nao colidirem.
      if (nodeId != 0 && srcId == 0) {
        delay(20 * nodeId);
        uint8_t out[PKT_N]; buildPkt(out, nodeId, seq);
        transmit(out);
      }
    } else {
      rxBad++;   // CRC ruim: o pacote chegou corrompido (conta como qualidade de link)
      radio.startReceive();
    }
  }

  // no 0: beacon 1Hz
  if (nodeId == 0 && millis() - lastBeacon >= BEACON_MS) {
    lastBeacon = millis();
    uint8_t out[PKT_N]; buildPkt(out, 0, ++seqOut);
    transmit(out);
  }

  if (millis() - lastReport > 5000) {
    lastReport = millis();
    Serial.printf("-- id=%u txCnt=%lu txUs(min/med/max)=%lu/%lu/%lu | rxOk=%lu rxCRC=%lu perdidos=%lu | rssi=%.1f snr=%.1f\n",
      nodeId, (unsigned long)txCnt,
      (unsigned long)(txCnt ? txMin : 0), (unsigned long)(txCnt ? txSum / txCnt : 0), (unsigned long)txMax,
      (unsigned long)rxOk, (unsigned long)rxBad, (unsigned long)lostCnt, lastRssi, lastSnr);
  }
}
