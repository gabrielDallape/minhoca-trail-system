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
 * Gravar:
 *   .\tools\build.ps1 firmware\e22_ping -Board p4 -Upload -Port COMx
 *   .\tools\build.ps1 firmware\e22_ping -Board devkit -Upload -Port COMx
 *
 * COM UM RADIO SO ja da para colher metade dos numeros: begin() OK prova SPI,
 * alimentacao e TCXO, e o txUs medido e o air-time real - transmitir nao depende
 * de haver alguem ouvindo. O rxOk fica em 0, e isso e o esperado, nao falha.
 * O segundo radio serve para medir PERDA e alcance, que e outra pergunta.
 */
#include <RadioLib.h>
#include <Preferences.h>

// ---------------------------------------------------------------- pinagem
// Tres bancadas possiveis, escolha UMA virando a chave para 1:
//
//   BOARD_P4     ESP32-P4 (5" ou 7B). E a bancada de verdade do projeto desde
//                agosto/2026: sobram os 9 pinos que o E22 pede, entao nao ha
//                concessao nenhuma. Os pinos vem de mts_p4/hardware.h - fonte
//                unica, para o teste nao poder divergir do firmware final.
//   BOARD_DEVKIT ESP32-S3-DevKitC-1, a bancada antiga.
//   nenhum dos dois  Waveshare 7B (S3). Sobram so 7 GPIOs e o E22 precisa de 9:
//                fica sem TXEN, logo o PA nao liga e ela nao transmite. So serve
//                para conferir SPI.
#define BOARD_P4     1
#define BOARD_DEVKIT 0

#if BOARD_P4
  #include "../mts_p4/hardware.h"   // pinos E parametros de RF, todos de la
  #define PIN_SCK   PIN_LORA_SCK
  #define PIN_MISO  PIN_LORA_MISO
  #define PIN_MOSI  PIN_LORA_MOSI
  #define PIN_NSS   PIN_LORA_NSS
  #define PIN_BUSY  PIN_LORA_BUSY
  #define PIN_DIO1  PIN_LORA_DIO1
  #define PIN_NRST  PIN_LORA_NRST
  #define PIN_RXEN  PIN_LORA_RXEN
  #define PIN_TXEN  PIN_LORA_TXEN
#elif BOARD_DEVKIT
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

// TESTE DE FIO TROCADO: BUSY <-> DIO1.
// Sao os pads 13 e 14 do modulo, vizinhos, e vao para IO50 e IO49, vizinhos no
// header. Trocar esses dois nao impede o begin() de funcionar - a RadioLib
// espera o "BUSY" (que seria o DIO1, parado em baixo) e segue em frente, e a
// leitura do chip da certo. So quebra na transmissao: no fim do pacote o DIO1
// sobe e nao desce ate a flag ser limpa, e como a lib acha que aquilo e o BUSY,
// toda transacao SPI seguinte expira em silencio.
// Virar para 1 e reprogramar responde a pergunta em um teste.
#define TROCA_BUSY_DIO1 0
#if TROCA_BUSY_DIO1 && BOARD_P4
  #undef  PIN_BUSY
  #undef  PIN_DIO1
  #define PIN_BUSY  PIN_LORA_DIO1
  #define PIN_DIO1  PIN_LORA_BUSY
#endif

// ---------------------------------------------------------------- radio RF
// No P4 estes valores ja vieram do hardware.h junto com os pinos. Aqui ficam so
// para as bancadas S3, e tem de ser os MESMOS - se divergirem, o air-time medido
// nao vale para dimensionar o slot do TDMA.
#if !BOARD_P4
#define RF_FREQ   915.0f
#define RF_BW     125.0f
#define RF_SF     7
#define RF_CR     5        // RadioLib usa 5..8 = 4/5..4/8
#define RF_SYNC   0x12
#define RF_PWR    22       // MAXIMO do SX1262; o PA do E22 soma ~7dB (~29,6dBm medido)
#define RF_PRE    8
#define RF_TCXO   2.2f     // manual E22-M V1.2 (2026-02-06), que existe justamente
                           // para descrever o cristal. O manual de 2018 dizia 1,8 e
                           // o default do RadioLib e 1,6: os dois erram.
#define RF_OCP    140.0f   // DEPOIS do setOutputPower, senao a saida trava em ~19dBm
#endif

#define PKT_N     16       // tamanho do pacote real do projeto (secao 7 do plano)
#define BEACON_MS 1000

SPIClass    spiLoRa(FSPI);
SX1262      radio = new Module(PIN_NSS, PIN_DIO1, PIN_NRST, PIN_BUSY, spiLoRa, SPISettings(2000000, MSBFIRST, SPI_MODE0));
Preferences prefs;

uint8_t nodeId = 0;
// TRAVA DE ANTENA. O no 0 comeca a bater beacon 1s depois do boot, em 30dBm.
// Sem antena no IPX isso reflete a potencia de volta no PA e o queima - e o unico
// jeito de destruir o modulo por software. Um checklist em papel nao protege
// disso, porque a placa reinicia sozinha (queda de energia, reset, watchdog) sem
// ninguem estar olhando. Entao a trava vive no firmware, nao na cabeca de quem
// monta: so transmite depois de alguem digitar T no serial, e o reset volta a
// travar. RECEBER e sempre liberado - ouvir sem antena nao machuca nada.
bool txLiberado = false;
// UMA SO INTERRUPCAO, e nao duas.
// A versao anterior chamava setPacketReceivedAction(onRxDone) e depois
// setPacketSentAction(onTxDone), como se fossem dois eventos separados. No
// SX126x nao sao: RadioLib 7.6, SX126x_config.cpp linhas 20 e 28, as duas
// funcoes chamam o MESMO setDio1Action(), que e um attachInterrupt() unico.
// A segunda chamada apagava a primeira - onRxDone nunca rodou, e todo pulso no
// DIO1 era contado como "transmissao concluida". Na bancada isso apareceu como
// txCnt=912 com a transmissao travada e txUs de 188 segundos.
//
// O jeito certo no SX126x: uma ISR so, e o loop pergunta ao chip QUAL evento foi,
// lendo getIrqFlags() ANTES de qualquer readData()/finishTransmit(), porque os
// dois limpam o registrador.
volatile bool irqFlag = false;
volatile uint32_t irqUs = 0;      // instante do pulso, marcado dentro da ISR

bool txEmVoo = false;             // ha um startTransmit sem TxDone correspondente
uint32_t txStartUs = 0, seqOut = 0;
uint32_t rxOk = 0, rxBad = 0, lostCnt = 0;
uint32_t irqEstranha = 0;         // pulso no DIO1 sem nenhum flag no chip
uint32_t lastSeqIn = 0; bool haveSeqIn = false;
uint32_t lastBeacon = 0, lastReport = 0;
// estatistica do tempo de TX medido (startTransmit -> txDone). E O NUMERO QUE
// DIMENSIONA O SLOT: air-time + overhead de SPI/driver.
uint32_t txMin = 0xFFFFFFFF, txMax = 0, txSum = 0, txCnt = 0;
float lastRssi = 0, lastSnr = 0;

void IRAM_ATTR onIrq(){ irqUs = micros(); irqFlag = true; }

// Contador cru, sem tocar no radio. Serve para separar "o radio pulsa sozinho"
// de "o meu tratador cria as proprias bordas ao rearmar o receptor".
volatile uint32_t brutoCnt = 0;
void IRAM_ATTR onBruto(){ brutoCnt++; }

#if BOARD_P4
// QUAL DOS DOIS FIOS E O BUSY? Descobre sozinho, em 5 ms, sem antena.
//
// Logo depois que o NRST e solto, o SX126x segura o BUSY em ALTO por ~1 ms
// enquanto carrega a calibracao; o DIO1 fica em baixo, porque nao ha IRQ nenhuma
// pendente. Entao basta soltar o reset e ver qual dos dois pinos sobe.
//
// Isto existe porque trocar BUSY com DIO1 e o erro de montagem mais facil de
// cometer (pads 13 e 14 vizinhos no modulo, IO49 e IO50 vizinhos no header) e o
// mais dificil de perceber: com eles trocados o begin() da OK, a leitura do chip
// da certo, o RX parece funcionar, e SO a transmissao quebra - com o sintoma
// enganoso de "BUSY preso em alto", que parece defeito de solda. Custou algumas
// horas de bancada. Vale 20 linhas para nunca mais.
void checaBusyDio1(){
  pinMode(PIN_LORA_NRST, OUTPUT);
  pinMode(PIN_LORA_BUSY, INPUT);
  pinMode(PIN_LORA_DIO1, INPUT);
  digitalWrite(PIN_LORA_NRST, LOW);
  delayMicroseconds(500);
  digitalWrite(PIN_LORA_NRST, HIGH);

  uint32_t n49 = 0, n50 = 0, t0 = micros();
  while (micros() - t0 < 5000) {
    n49 += digitalRead(PIN_LORA_BUSY);   // o GPIO que o hardware.h chama de BUSY
    n50 += digitalRead(PIN_LORA_DIO1);   // o GPIO que o hardware.h chama de DIO1
  }
  Serial.printf("[fios] apos o reset: GPIO%d alto %lu vezes, GPIO%d alto %lu vezes\n",
                PIN_LORA_BUSY, (unsigned long)n49, PIN_LORA_DIO1, (unsigned long)n50);

  const bool trocado = (n50 > n49 * 4);   // quem fica alto depois do reset e o BUSY
  const bool certo   = (n49 > n50 * 4);
  if (!trocado && !certo) {
    Serial.println("[fios] inconclusivo - nenhum dos dois se destacou. Confira a solda.");
    return;
  }
  Serial.printf("[fios] o BUSY do modulo esta no GPIO%d\n",
                trocado ? PIN_LORA_DIO1 : PIN_LORA_BUSY);

  if (trocado != (bool)TROCA_BUSY_DIO1) {
    Serial.println("[fios] *****************************************************");
    Serial.println("[fios] *  O FIO NAO BATE COM O FIRMWARE.                   *");
    if (trocado) {
      Serial.println("[fios] *  BUSY e DIO1 estao TROCADOS no chicote.          *");
      Serial.println("[fios] *  Troque os dois fios, ou ponha TROCA_BUSY_DIO1 1 *");
    } else {
      Serial.println("[fios] *  O chicote esta certo, mas TROCA_BUSY_DIO1 esta  *");
      Serial.println("[fios] *  em 1. Ponha 0 e reprograme.                     *");
    }
    Serial.println("[fios] *  Assim, a transmissao NAO vai completar.          *");
    Serial.println("[fios] *****************************************************");
  } else {
    Serial.println("[fios] confere com o firmware, tudo certo");
  }
}
#endif

// Configuracao completa do radio, num lugar so. Existe como funcao - e nao solta
// dentro do setup() - porque a varredura de potencia precisa reconfigurar o chip
// do zero depois de cada travada, e reproduzir a sequencia na mao seria a receita
// para as duas versoes divergirem.
int configuraRadio(){
  int st = radio.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, RF_PWR, RF_PRE, RF_TCXO);
  if (st != RADIOLIB_ERR_NONE) return st;
  // ARMADILHA 1: sem setRfSwitchPins o PA nunca liga. ORDEM: (rxEn, txEn).
  radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);
  // ARMADILHA 2: OCP padrao (~60mA) estrangula a potencia. Liberar p/ ~1W.
  radio.setCurrentLimit(RF_OCP);
  // NOTA DE DEFEITO (2026-08-24): um dos tres modulos (o que estava na tela A)
  // devolve GetDeviceErrors = XOSC_NAO_PARTIU ao armar TX acima de ~2 dBm - RX
  // perfeito, TX fraco perfeito, TX forte mata o TCXO. Foram tentados
  // setRegulatorLDO() e setTCXO(2.4, 10ms): NAO curam. E defeito interno do
  // modulo (TCXO marginal ou alimentacao dele); ficou de peca de bancada.
  radio.setDio1Action(onIrq);
  return RADIOLIB_ERR_NONE;
}

void vigiaDio1(){
  Serial.println("[vigia] 3 s observando o DIO1 SEM ninguem mexer no radio...");
  radio.clearIrqFlags(0xFFFFUL);
  radio.startReceive();
  delay(50);
  detachInterrupt(digitalPinToInterrupt(PIN_DIO1));
  brutoCnt = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_DIO1), onBruto, RISING);
  delay(3000);
  detachInterrupt(digitalPinToInterrupt(PIN_DIO1));
  Serial.printf("[vigia] %lu bordas de subida em 3 s\n", (unsigned long)brutoCnt);
  if (brutoCnt == 0) {
    Serial.println("[vigia] ZERO. O radio em repouso nao gera borda nenhuma.");
    Serial.println("[vigia] Logo as 'irqEstranha' nasciam do proprio rearme do receptor.");
  } else {
    Serial.println("[vigia] Ha bordas de verdade com o radio parado - investigar o sinal.");
  }
  radio.setDio1Action(onIrq);
  irqFlag = false;
}

// -------------------------------------------------------------- diagnostico
void explainBeginError(int st){
  Serial.printf("[radio] begin() FALHOU: %d\n", st);
  if (st == RADIOLIB_ERR_SPI_CMD_FAILED || st == -707) {
    Serial.println("  -707 = quase sempre a tensao do TCXO. Tente nesta ordem:");
    Serial.println("   1) RF_TCXO 2.2  (valor do manual E22-M V1.2 - e o que esta aqui)");
    Serial.println("   2) RF_TCXO 1.8  (valor do manual antigo, de 2018)");
    Serial.println("   3) RF_TCXO 1.6  (default do RadioLib, o menos provavel)");
    Serial.println("  NAO tente 'radio.XTAL = true': esse flag diz 'nao ha TCXO, e");
    Serial.println("  cristal passivo', e este modulo TEM TCXO no DIO3.");
  }
  if (st == RADIOLIB_ERR_CHIP_NOT_FOUND) {
    Serial.println("  chip nao encontrado -> confira MISO/MOSI/SCK/NSS, BUSY e o 5V do modulo");
    Serial.println("  meca 5V entre um pad de VCC e um de GND com a placa ligada");
  }
}

// QUEM MANDA NO PINO DO DIO1?
// Teste classico de continuidade feito por software, sem multimetro: ligo o
// pull-up interno e vejo se o pino sobe. Se subir, nao ha nada do outro lado -
// o fio esta solto. Se ficar baixo ATE com pull-up, e porque o E22 esta
// segurando ele em nivel baixo, que e o estado de repouso do DIO1: ligado.
// O pull interno e fraco (~45k) e o driver do modulo e forte, entao a
// comparacao nao tem meio-termo - e por isso que ela decide.
void diagDio1(){
  // 1) o que o CHIP acha que esta pendente. E a primeira pergunta: se o DIO1 esta
  //    alto porque ha um flag ligado, o problema e de configuracao de IRQ, nao de
  //    solda, e nenhuma quantidade de resolda resolve.
  uint32_t f = radio.getIrqFlags();
  Serial.printf("[diag] getIrqFlags = 0x%04lX  ", (unsigned long)f);
  if (!f) Serial.print("(nenhum)");
  if (f & RADIOLIB_SX126X_IRQ_TX_DONE)       Serial.print("TX_DONE ");
  if (f & RADIOLIB_SX126X_IRQ_RX_DONE)       Serial.print("RX_DONE ");
  if (f & RADIOLIB_SX126X_IRQ_HEADER_VALID)  Serial.print("HDR_OK ");
  if (f & RADIOLIB_SX126X_IRQ_HEADER_ERR)    Serial.print("HDR_ERR ");
  if (f & RADIOLIB_SX126X_IRQ_CRC_ERR)       Serial.print("CRC_ERR ");
  if (f & RADIOLIB_SX126X_IRQ_TIMEOUT)       Serial.print("TIMEOUT ");
  Serial.println();

  // 2) perfil do pino ao longo de 1 segundo inteiro. A versao anterior amostrava
  //    3 ms e concluiu "instavel" de um pino que estava simplesmente alto - janela
  //    curta demais para ver um evento de 0,5 s. Aqui tambem conto as TRANSICOES,
  //    que e o que separa "preso em alto" de "oscilando rapido".
  pinMode(PIN_DIO1, INPUT);
  int alto = 0, trans = 0, ant = digitalRead(PIN_DIO1);
  for (int i = 0; i < 1000; i++) {
    int v = digitalRead(PIN_DIO1);
    alto += v;
    if (v != ant) { trans++; ant = v; }
    delayMicroseconds(1000);
  }
  Serial.printf("[diag] DIO1 (GPIO%d) em 1 s: %d/1000 alto, %d transicoes\n",
                PIN_DIO1, alto, trans);

  // 3) so agora o teste de continuidade, e com janela longa o bastante.
  pinMode(PIN_DIO1, INPUT_PULLUP);   delay(5);
  int nUp = 0; for (int i = 0; i < 200; i++) { nUp += digitalRead(PIN_DIO1); delayMicroseconds(200); }
  pinMode(PIN_DIO1, INPUT_PULLDOWN); delay(5);
  int nDn = 0; for (int i = 0; i < 200; i++) { nDn += digitalRead(PIN_DIO1); delayMicroseconds(200); }
  pinMode(PIN_DIO1, INPUT);
  Serial.printf("[diag] pull-up %d/200 alto | pull-down %d/200 alto\n", nUp, nDn);

  if (nUp >= 190 && nDn <= 10) {
    Serial.println("[diag] >>> segue o pull nos dois sentidos = NADA ligado no pino.");
    Serial.println("[diag]     DIO1 solto: confira o pad 13 do E22 e o IO50 no P1.");
  } else if (nDn >= 190) {
    Serial.println("[diag] >>> fica ALTO ate contra o pull-down = algo dirige o pino em alto.");
    Serial.println("[diag]     Se getIrqFlags acima veio 0x0000, o alto NAO vem do radio.");
  } else if (nUp <= 10) {
    Serial.println("[diag] >>> fica BAIXO ate com pull-up = o modulo segura o pino. LIGADO, ok.");
  } else {
    Serial.println("[diag] >>> leitura mista: solda fria ou fio meio encostado.");
  }
  // pinMode nao derruba o attachInterrupt no ESP32, mas reatacho por garantia.
  radio.setDio1Action(onIrq);
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
  // O USB nativo do P4 DESCARTA o que e escrito antes de o PC abrir a porta - nao
  // guarda em buffer. Sem esta espera toda mensagem de boot se perde quando a
  // placa e ligada antes do monitor, que e o caso normal na bancada.
  uint32_t tEsp = millis();
  while (!Serial && millis() - tEsp < 3000) delay(10);
  delay(300);
  Serial.println("\n== E22 PING (fase 1) ==");

  prefs.begin("e22", false);
  nodeId = prefs.getUChar("id", 0);
  prefs.end();
  Serial.printf("[cfg] nodeId=%u  (digite 0/1/2 no serial p/ trocar)\n", nodeId);

#if BOARD_P4
  Serial.printf("[pin] placa P4 (MTS_PLACA=%d): SCK=%d MISO=%d MOSI=%d NSS=%d\n",
                MTS_PLACA, PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);
  Serial.printf("[pin] BUSY=%d DIO1=%d NRST=%d RXEN=%d TXEN=%d\n",
                PIN_BUSY, PIN_DIO1, PIN_NRST, PIN_RXEN, PIN_TXEN);
#elif !BOARD_DEVKIT
  Serial.println("[AVISO] nem P4 nem devkit: nao ha pino p/ TXEN nesta placa.");
  Serial.println("        Sem TXEN o PA do E22 NAO liga -> 'transmite nada'.");
  Serial.println("        Use um P4 ou um devkit p/ a bancada de radio.");
#endif

#if BOARD_P4
  checaBusyDio1();   // antes de tudo: os fios estao onde o firmware pensa?
#endif

  spiLoRa.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_NSS);   // RadioLib NAO inicializa o bus

  int st = configuraRadio();
  if (st != RADIOLIB_ERR_NONE) {
    // REPETIR, e nao travar calado. A versao anterior imprimia o erro uma vez e
    // caia num while(true) mudo. Como a mensagem sai no boot e o USB nativo joga
    // fora o que ninguem esta lendo, na pratica a placa ficava sem dizer nada -
    // indistinguivel de travada. Foi exatamente assim que ela apareceu na
    // bancada, e custou uma regravacao so para descobrir que havia um erro.
    while (true) {
      explainBeginError(st);
      Serial.println("[radio] (repetindo a cada 3 s - reprograme depois de corrigir)");
      delay(3000);
    }
  }
  Serial.println("[radio] begin OK");

  diagDio1();
  printAirTimeTable();

  radio.startReceive();
  Serial.println("[radio] escutando (receber nao precisa de liberacao)");
  Serial.println();
  Serial.println("  ***********************************************************");
  Serial.println("  *  A ANTENA ESTA NO CONECTOR IPX DO MODULO?               *");
  Serial.println("  *                                                         *");
  Serial.println("  *  Nao transmito nada ate voce digitar  T  aqui.          *");
  Serial.println("  *  Confira a antena ANTES. Sem ela, o primeiro beacon     *");
  Serial.println("  *  queima o amplificador.                                 *");
  Serial.println("  ***********************************************************");
  Serial.println();
}

// monta o pacote de teste: [id][seq LE32][padding] = PKT_N bytes
void buildPkt(uint8_t* p, uint8_t id, uint32_t seq){
  memset(p, 0, PKT_N);
  p[0] = id;
  p[1] = seq & 0xFF; p[2] = (seq >> 8) & 0xFF; p[3] = (seq >> 16) & 0xFF; p[4] = (seq >> 24) & 0xFF;
}

void transmit(uint8_t* p){
  // STANDBY ANTES, SEMPRE.
  // O startTransmit() da RadioLib 7.6 nao volta para standby sozinho: ele monta
  // o pacote com o chip no modo em que estiver. Chamado com o radio em RX
  // continuo - que e o estado normal deste sketch - ele devolve -1
  // (RADIOLIB_ERR_UNKNOWN, do teste de modem dentro do stageMode).
  // MEDIDO na bancada: a partir de RX da -1 em 100% das tentativas; a partir de
  // STBY_RC da 0. Uma linha, e e a diferenca entre transmitir e nao transmitir.
  radio.standby();
  txStartUs = micros();
  txEmVoo   = true;
  int st = radio.startTransmit(p, PKT_N);
  if (st != RADIOLIB_ERR_NONE) {
    txEmVoo = false;
    Serial.printf("[tx] startTransmit -> %d\n", st);
  }
}

// POR QUE O startTransmit DEVOLVE -1?
// Em RadioLib 7.6 o unico RADIOLIB_ERR_UNKNOWN no caminho de TX (SX126x.cpp,
// ramo RADIOLIB_RADIO_MODE_TX do stageMode) e quando getPacketType() nao devolve
// nenhum dos quatro modems conhecidos. getPacketType() inicializa a variavel em
// 0xFF e so a sobrescreve se o SPIreadStream der certo - entao 0xFF significa
// "a leitura SPI nao aconteceu", e a suspeita passa a ser o BUSY, que e quem
// autoriza cada transacao.
// getPacketType() e protegido na RadioLib, entao falo com o chip direto. De
// quebra isso traz o byte de STATUS do SX126x, que a API nao expoe e que diz em
// que modo o chip esta e se o ultimo comando falhou - exatamente o que falta
// saber aqui.
uint8_t leTipoPacoteCru(){
  uint32_t t0 = micros();
  while (digitalRead(PIN_BUSY)) {
    if (micros() - t0 > 20000) { Serial.println("[spi] BUSY preso em ALTO por 20ms"); return 0xFE; }
  }
  spiLoRa.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_NSS, LOW);
  spiLoRa.transfer(0x11);                       // GetPacketType
  uint8_t status = spiLoRa.transfer(0x00);
  uint8_t tipo   = spiLoRa.transfer(0x00);
  digitalWrite(PIN_NSS, HIGH);
  spiLoRa.endTransaction();

  const uint8_t modo = (status >> 4) & 0x07;
  const uint8_t cmd  = (status >> 1) & 0x07;
  const char* nomeModo[] = {"?0","?1","STBY_RC","STBY_XOSC","FS","RX","TX","?7"};
  const char* nomeCmd[]  = {"?0","reservado","dado disponivel","TIMEOUT DE COMANDO",
                            "ERRO DE PROCESSAMENTO","FALHA AO EXECUTAR","TX concluido","?7"};
  Serial.printf("[spi] status=0x%02X -> modo=%s, ultimo comando=%s\n",
                status, nomeModo[modo], nomeCmd[cmd]);
  Serial.printf("[spi] packetType=0x%02X  (0x00=GFSK 0x01=LoRa 0x02=BPSK 0x03=LR-FHSS)\n", tipo);
  return tipo;
}

// GetDeviceErrors (0x17). A RadioLib nao expoe isso, e e o unico lugar que diz
// POR QUE o chip travou: se o oscilador nao partiu, se o PLL nao prendeu, se a
// rampa do PA falhou. Sem ler aqui, "BUSY preso em alto" e so um sintoma mudo.
void leErrosDoChip(const char* quando){
  uint32_t t0 = micros();
  while (digitalRead(PIN_BUSY)) {
    if (micros() - t0 > 100000) {
      Serial.printf("[erro] (%s) BUSY preso em ALTO ha 100ms - nem da p/ perguntar\n", quando);
      return;
    }
  }
  spiLoRa.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_NSS, LOW);
  spiLoRa.transfer(0x17);
  spiLoRa.transfer(0x00);                      // status, ja lido em outro lugar
  uint8_t hi = spiLoRa.transfer(0x00);
  uint8_t lo = spiLoRa.transfer(0x00);
  digitalWrite(PIN_NSS, HIGH);
  spiLoRa.endTransaction();

  uint16_t e = ((uint16_t)hi << 8) | lo;
  Serial.printf("[erro] (%s) GetDeviceErrors = 0x%04X", quando, e);
  if (!e) { Serial.println("  -> nenhum erro"); return; }
  if (e & (1 << 0)) Serial.print("  RC64K_CALIB");
  if (e & (1 << 1)) Serial.print("  RC13M_CALIB");
  if (e & (1 << 2)) Serial.print("  PLL_CALIB");
  if (e & (1 << 3)) Serial.print("  ADC_CALIB");
  if (e & (1 << 4)) Serial.print("  IMG_CALIB");
  if (e & (1 << 5)) Serial.print("  XOSC_NAO_PARTIU(TCXO!)");
  if (e & (1 << 6)) Serial.print("  PLL_NAO_PRENDEU");
  if (e & (1 << 8)) Serial.print("  RAMPA_DO_PA");
  Serial.println();
}

void diagTx(){
  Serial.printf("[tx?] BUSY (GPIO%d) = %d  (0 = livre p/ SPI)\n",
                PIN_BUSY, digitalRead(PIN_BUSY));
  leTipoPacoteCru();

  int st = radio.standby();
  Serial.printf("[tx?] standby() -> %d | BUSY = %d\n", st, digitalRead(PIN_BUSY));
  leTipoPacoteCru();

  st = radio.setOutputPower(RF_PWR);
  Serial.printf("[tx?] setOutputPower(%d) -> %d\n", (int)RF_PWR, st);
  st = radio.setCurrentLimit(RF_OCP);
  Serial.printf("[tx?] setCurrentLimit(%d) -> %d\n", (int)RF_OCP, st);

  leErrosDoChip("antes do TX");

  uint8_t p[PKT_N]; memset(p, 0, PKT_N); p[0] = 99;
  st = radio.startTransmit(p, PKT_N);
  Serial.printf("[tx?] startTransmit -> %d | BUSY = %d\n", st, digitalRead(PIN_BUSY));

  // acompanha o BUSY de perto: quanto tempo ele fica alto e quando cai
  uint32_t t0 = micros(); uint32_t caiuEm = 0;
  for (int i = 0; i < 4000; i++) {              // ate 400 ms
    if (!digitalRead(PIN_BUSY)) { caiuEm = micros() - t0; break; }
    delayMicroseconds(100);
  }
  if (caiuEm) Serial.printf("[tx?] BUSY caiu em %lu us\n", (unsigned long)caiuEm);
  else        Serial.println("[tx?] BUSY NAO caiu em 400 ms");

  delay(100);
  Serial.printf("[tx?] flags = 0x%04lX (0x0001 = TX_DONE)\n",
                (unsigned long)radio.getIrqFlags());
  leErrosDoChip("depois do TX");
  leTipoPacoteCru();

  radio.finishTransmit();
  radio.startReceive();
  irqFlag = false; txEmVoo = false;
}

// VARREDURA DE POTENCIA. Separa "configuracao errada" de "falta de corrente".
// Se o chip completa o TX em 2 dBm e trava em 22 dBm, o problema nao esta em
// nenhum registrador - esta na fonte. E se travar em todas, ai sim a suspeita
// volta para configuracao, e a potencia deixa de ser a variavel.
void varreduraPotencia(){
  const int8_t niveis[] = { 22, 17, 14, 10, 5, 2, -5 };
  Serial.println("[pot] varrendo a potencia de saida, um TX por nivel:");
  for (int i = 0; i < 7; i++) {
    // reconfiguracao completa: a travada anterior deixa o chip num estado que so
    // o reset limpa, e sem isso o proximo nivel herdaria a falha do anterior.
    if (configuraRadio() != RADIOLIB_ERR_NONE) {
      Serial.println("[pot] o radio nao volta nem com begin() - parando");
      return;
    }
    radio.standby();
    radio.setOutputPower(niveis[i]);
    radio.setCurrentLimit(RF_OCP);     // sempre DEPOIS do setOutputPower
    radio.clearIrqFlags(0xFFFFUL);

    uint8_t p[PKT_N]; memset(p, 0, PKT_N); p[0] = 99;
    irqFlag = false;
    uint32_t t0 = micros();
    int st = radio.startTransmit(p, PKT_N);

    // NAO confio so na borda do DIO1: pergunto ao chip qual flag subiu.
    // A primeira versao desta varredura media o instante da interrupcao e deu
    // 28,9 ms para um pacote cujo air-time teorico e 51,5 ms (50,25 simbolos de
    // 1,024 ms). Menos que o minimo fisico = nao era fim de transmissao. Ler a
    // flag custa uma transacao SPI e transforma "houve um pulso" em "o chip diz
    // que terminou".
    uint32_t dtIrq = 0, dtFlag = 0; uint16_t fim = 0;
    if (st == RADIOLIB_ERR_NONE) {
      while (micros() - t0 < 800000UL) {
        if (irqFlag && !dtIrq) { dtIrq = irqUs - t0; }
        uint16_t f = (uint16_t)radio.getIrqFlags();
        if (f & RADIOLIB_SX126X_IRQ_TX_DONE) { dtFlag = micros() - t0; fim = f; break; }
        delayMicroseconds(200);
      }
    }
    Serial.printf("[pot] %3d dBm -> start=%d | 1a borda em %lu us | TX_DONE %s",
                  niveis[i], st, (unsigned long)dtIrq,
                  dtFlag ? "SIM" : "NAO");
    if (dtFlag) Serial.printf(" em %lu us (flags=0x%04X)", (unsigned long)dtFlag, fim);
    Serial.println();
    delay(300);
  }
  configuraRadio();
  radio.startReceive();
  irqFlag = false; txEmVoo = false;
  Serial.println("[pot] fim da varredura, de volta a escuta");
}

// Leitura de erros IGNORANDO o BUSY. O datasheet manda esperar o BUSY cair, mas
// quando ele nao cai e exatamente quando a informacao e necessaria - esperar
// educadamente devolve silencio. O valor pode vir corrompido; por isso leio duas
// vezes e so acredito se as duas baterem.
void leErrosForcado(){
  uint16_t v[2];
  for (int k = 0; k < 2; k++) {
    spiLoRa.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_NSS, LOW);
    spiLoRa.transfer(0x17);
    spiLoRa.transfer(0x00);
    uint8_t hi = spiLoRa.transfer(0x00);
    uint8_t lo = spiLoRa.transfer(0x00);
    digitalWrite(PIN_NSS, HIGH);
    spiLoRa.endTransaction();
    v[k] = ((uint16_t)hi << 8) | lo;
    delay(2);
  }
  Serial.printf("[erro!] GetDeviceErrors forcado = 0x%04X e 0x%04X %s\n",
                v[0], v[1], (v[0] == v[1]) ? "(iguais, confiavel)" : "(diferentes, NAO confiavel)");
  uint16_t e = v[0];
  if (e & (1 << 5)) Serial.println("[erro!]   XOSC_START_ERR - o TCXO nao partiu");
  if (e & (1 << 6)) Serial.println("[erro!]   PLL_LOCK_ERR - o PLL nao prendeu");
  if (e & (1 << 8)) Serial.println("[erro!]   PA_RAMP_ERR - a rampa do PA falhou");
}

// TRACO DOS DOIS PINOS. Chega de inferir o estado do chip por uma flag que pode
// nao ter sido lida: aqui a amostragem e do fio, 1 ms por amostra, e o desenho
// mostra QUANDO cada coisa acontece. Um TX de 16 B em SF7 dura 51,5 ms - se o
// desenho nao tiver nada em 51 ms, o pacote nao saiu.
void tracoTx(){
  static uint8_t amostra[300];
  if (configuraRadio() != RADIOLIB_ERR_NONE) { Serial.println("[traco] begin falhou"); return; }
  radio.standby();
  radio.setOutputPower(RF_PWR);
  radio.setCurrentLimit(RF_OCP);
  radio.clearIrqFlags(0xFFFFUL);

  uint8_t p[PKT_N]; memset(p, 0, PKT_N); p[0] = 99;
  irqFlag = false;
  uint32_t t0 = micros();
  int st = radio.startTransmit(p, PKT_N);
  for (int i = 0; i < 300; i++) {
    amostra[i] = (digitalRead(PIN_BUSY) ? 1 : 0) | (digitalRead(PIN_DIO1) ? 2 : 0);
    while ((int32_t)(micros() - t0) < (int32_t)((i + 1) * 1000)) { }
  }

  Serial.printf("[traco] startTransmit=%d | 300 ms, 1 ms por caracter\n", st);
  Serial.println("[traco] . = nada   B = so BUSY   D = so DIO1   # = os dois");
  for (int i = 0; i < 300; i++) {
    char c = '.';
    if      (amostra[i] == 1) c = 'B';
    else if (amostra[i] == 2) c = 'D';
    else if (amostra[i] == 3) c = '#';
    Serial.print(c);
    if ((i % 50) == 49) Serial.printf("  |%3d ms\n", i + 1);
  }
  leErrosForcado();
  configuraRadio();
  radio.startReceive();
  irqFlag = false; txEmVoo = false;
}

#if BOARD_P4
// ------------------------------------------------------------------- GPS
// Escuta o GPS por 6 s e conta pulsos de PPS no mesmo intervalo. Nao e o driver
// do projeto - e o teste de bancada que separa quatro perguntas que costumam ser
// confundidas numa so:
//   1. chega byte?            nao -> fio, baud ou o GPS sem energia
//   2. os bytes viram frase?  nao -> baud errado (lixo com $ salteado)
//   3. a frase tem fix?       nao -> antena do GPS, ou ainda buscando ceu
//   4. o PPS pulsa?           so pulsa COM fix na maioria dos modulos
volatile uint32_t ppsCnt = 0;
volatile uint32_t ppsUltimoUs = 0, ppsIntervaloUs = 0;
void IRAM_ATTR onPps(){
  uint32_t agora = micros();
  if (ppsUltimoUs) ppsIntervaloUs = agora - ppsUltimoUs;
  ppsUltimoUs = agora;
  ppsCnt++;
}

void testeGps(){
  Serial.printf("[gps] Serial1 em %d bps, RX=GPIO%d TX=GPIO%d, PPS=GPIO%d\n",
                GPS_BAUD, PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_PPS);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  pinMode(PIN_GPS_PPS, INPUT);
  ppsCnt = 0; ppsUltimoUs = 0; ppsIntervaloUs = 0;
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), onPps, RISING);

  uint32_t bytes = 0, frases = 0, mostradas = 0;
  bool viuGGA = false, viuRMC = false;
  int  fix = -1, sats = -1;
  char linha[100]; int n = 0;

  uint32_t t0 = millis();
  while (millis() - t0 < 6000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      bytes++;
      if (c == '\n' || c == '\r') {
        if (n > 5) {
          linha[n] = 0;
          frases++;
          if (mostradas < 6) { Serial.printf("[gps] %s\n", linha); mostradas++; }
          // GGA: $..GGA,hora,lat,N,lon,E,QUALIDADE,SATELITES,...
          if (strstr(linha, "GGA")) {
            viuGGA = true;
            int v = 0; char* p = linha;
            while (*p && v < 6) { if (*p == ',') v++; p++; }
            if (v == 6) { fix = atoi(p); while (*p && *p != ',') p++; if (*p) sats = atoi(p + 1); }
          }
          if (strstr(linha, "RMC")) viuRMC = true;
        }
        n = 0;
      } else if (n < (int)sizeof(linha) - 1) {
        linha[n++] = c;
      }
    }
  }
  detachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS));

  Serial.printf("[gps] em 6 s: %lu bytes, %lu frases NMEA\n",
                (unsigned long)bytes, (unsigned long)frases);
  // CONTAR BORDA NAO E MEDIR PPS.
  // Um pino de entrada solto capta ruido e produz milhares de bordas por segundo.
  // A primeira versao deste teste so contava, e por isso declarou "o PPS pulsa,
  // logo o modulo tem fix" olhando para 14506 pulsos de 3 us - que era exatamente
  // a assinatura do fio DESCONECTADO. O que caracteriza o PPS nao e existir borda,
  // e o RITMO: uma por segundo. Entao a validacao e pelo intervalo.
  const bool ppsReal = (ppsCnt >= 3 && ppsCnt <= 12 &&
                        ppsIntervaloUs > 900000UL && ppsIntervaloUs < 1100000UL);
  const bool ppsRuido = (ppsCnt > 50);

  Serial.printf("[gps] PPS: %lu pulsos em 6 s", (unsigned long)ppsCnt);
  if (ppsIntervaloUs) Serial.printf(", ultimo intervalo %lu us", (unsigned long)ppsIntervaloUs);
  if      (ppsReal)  Serial.print("  -> 1 Hz, e PPS de verdade");
  else if (ppsRuido) Serial.print("  -> RUIDO, nao e PPS: o pino esta solto");
  else if (ppsCnt)   Serial.print("  -> ritmo estranho, nao confie");
  Serial.println();

  if (!bytes) {
    // O PPS decide entre as duas causas, e por isso e olhado ANTES de acusar
    // alimentacao: pulso de 1 Hz so existe se o modulo tem energia, terra,
    // antena E fix. Havendo PPS, "o GPS nao esta ligado" e uma acusacao falsa -
    // sobra exatamente um fio suspeito, o de dados.
    if (ppsReal) {
      Serial.printf("[gps] >>> NENHUM byte, MAS o PPS pulsa a 1 Hz. Logo o modulo tem 3V3, GND,\n");
      Serial.printf("[gps]     antena e fix - so o fio de DADOS nao chega.\n");
      Serial.printf("[gps]     Confira o TXD do GPS ate o GPIO%d. E so ele.\n", PIN_GPS_RX);
    } else {
      Serial.println("[gps] >>> NENHUM byte e nenhum PPS. O GPS nao esta ligado, ou o TXD");
      Serial.printf( "[gps]     dele nao chegou no GPIO%d, ou falta o 3V3/GND dele.\n", PIN_GPS_RX);
    }
  } else if (!frases) {
    Serial.println("[gps] >>> chegam bytes mas nao formam frase: baud errado.");
  } else {
    Serial.printf("[gps] frases OK (GGA=%s RMC=%s)\n", viuGGA ? "sim" : "nao", viuRMC ? "sim" : "nao");
    if (fix <= 0) {
      Serial.printf("[gps] >>> SEM FIX (qualidade=%d, satelites=%d). O modulo fala, mas\n", fix, sats);
      Serial.println("[gps]     ainda nao ve ceu. Leve para a janela: a primeira fixacao");
      Serial.println("[gps]     de um modulo frio leva de 30 s a alguns minutos.");
      Serial.println("[gps]     Sem fix a maioria dos modulos NAO gera PPS - 0 pulso aqui e normal.");
    } else {
      Serial.printf("[gps] >>> FIX! qualidade=%d, %d satelites\n", fix, sats);
      if (!ppsReal) {
        Serial.printf("[gps] >>> mas ZERO PPS. Ha fix, entao o pulso existe: o fio do\n");
        Serial.printf("[gps]     PPS nao chegou no GPIO%d. Sem PPS nao ha TDMA.\n", PIN_GPS_PPS);
      } else {
        Serial.println("[gps] >>> PPS pulsando. E a regua de tempo do TDMA - esta tudo pronto.");
      }
    }
  }
  Serial1.end();
}
#endif

void loop(){
  // troca de papel pelo serial (mesmo padrao do grupo_ws.ino)
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '7') {
      nodeId = c - '0';
      prefs.begin("e22", false); prefs.putUChar("id", nodeId); prefs.end();
      Serial.printf("[cfg] nodeId=%u (salvo)\n", nodeId);
    }
    // De proposito NAO salvo isto na NVS: a trava tem de voltar a cada boot.
    // Gravado, ela protegeria so o primeiro liga - que e justamente o unico em
    // que voce lembra da antena.
    if (c == 't' || c == 'T') {
      txLiberado = true;
      Serial.println("[tx] LIBERADO. Assumindo que a antena esta no IPX.");
    }
    if (c == 'd' || c == 'D') diagDio1();   // repetir o teste depois de resoldar
#if BOARD_P4
    // 'f' de fios. Pulsa o NRST, entao o radio precisa ser reconfigurado depois.
    if (c == 'f' || c == 'F') {
      checaBusyDio1();
      configuraRadio();
      radio.startReceive();
      irqFlag = false; txEmVoo = false;
    }
#endif
    if (c == 'w' || c == 'W') vigiaDio1();   // 3 s so olhando o pino
    if (c == 'a' || c == 'A') printAirTimeTable();
    if (c == 'p' || c == 'P') diagTx();
    if (c == 'v' || c == 'V') varreduraPotencia();
#if BOARD_P4
    if (c == 'g' || c == 'G') testeGps();
#endif
    if (c == 'x' || c == 'X') tracoTx();
  }

  // -------------------------------------------------- pulso no DIO1
  // Ler getIrqFlags() PRIMEIRO: readData() e finishTransmit() limpam o
  // registrador, entao depois deles nao da mais para saber o que aconteceu.
  if (irqFlag) {
    irqFlag = false;
    uint32_t flags = radio.getIrqFlags();

    if (flags & RADIOLIB_SX126X_IRQ_TX_DONE) {
      // So conta se ESTE firmware pediu a transmissao. Sem essa guarda, um pulso
      // solto vira uma medicao de air-time de varios segundos - o numero que
      // dimensiona o slot do TDMA, entao contaminar aqui e caro.
      if (txEmVoo) {
        txEmVoo = false;
        uint32_t dt = irqUs - txStartUs;
        if (dt < txMin) txMin = dt;
        if (dt > txMax) txMax = dt;
        txSum += dt; txCnt++;
      }
      radio.finishTransmit();
      radio.startReceive();

    } else if (flags & RADIOLIB_SX126X_IRQ_RX_DONE) {
      uint8_t p[PKT_N];
      int st = radio.readData(p, PKT_N);
      bool respondeu = false;
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
        if (nodeId != 0 && srcId == 0 && txLiberado) {
          delay(20 * nodeId);
          uint8_t out[PKT_N]; buildPkt(out, nodeId, seq);
          transmit(out);
          respondeu = true;
        }
      } else {
        rxBad++;   // CRC ruim: o pacote chegou corrompido (conta como qualidade de link)
      }
      // Rearmar SEMPRE, menos quando acabamos de entrar em TX. A versao anterior
      // so rearmava no caminho de erro, entao o no 0 parava de escutar depois do
      // primeiro pacote recebido.
      if (!respondeu) radio.startReceive();

    } else {
      // Borda no DIO1 sem nenhum flag no chip: e residual, NAO e evento.
      // O proprio startReceive() da uma piscada curta no DIO1 ao reprogramar o
      // setDioIrqParams, e essa piscada gera uma borda de subida.
      //
      // NAO REARME AQUI. A versao anterior chamava clearIrqFlags + startReceive
      // nesta linha, e cada rearme produzia a piscada seguinte: o tratador virou
      // a propria fonte de interrupcao. Na bancada isso contou 900 eventos com o
      // radio parado, e a leitura ingenua era "o DIO1 esta com mau contato".
      // Medido: com este ramo passivo, o contador congela e nao anda mais.
      //
      // O receptor JA esta armado - nao ha nada a fazer alem de ignorar.
      irqEstranha++;
      if (flags) {
        // ai sim houve evento real que nao e TX nem RX (CRC_ERR solto, timeout):
        // limpar e rearmar e o certo, e nao realimenta nada porque flags != 0
        // so acontece de verdade.
        if (irqEstranha <= 8) {
          Serial.printf("[irq] flags=0x%04lX sem TX/RX -> limpando\n", (unsigned long)flags);
        }
        radio.clearIrqFlags(0xFFFFUL);
        radio.startReceive();
      }
    }
  }

  // no 0: beacon 1Hz
  if (nodeId == 0 && txLiberado && millis() - lastBeacon >= BEACON_MS) {
    lastBeacon = millis();
    uint8_t out[PKT_N]; buildPkt(out, 0, ++seqOut);
    transmit(out);
  }

  if (millis() - lastReport > 5000) {
    lastReport = millis();
    if (!txLiberado) {
      Serial.println("-- TX TRAVADO: confira a antena no IPX e digite T para liberar");
    }
    Serial.printf("-- id=%u txCnt=%lu txUs(min/med/max)=%lu/%lu/%lu | rxOk=%lu rxCRC=%lu perdidos=%lu | rssi=%.1f snr=%.1f\n",
      nodeId, (unsigned long)txCnt,
      (unsigned long)(txCnt ? txMin : 0), (unsigned long)(txCnt ? txSum / txCnt : 0), (unsigned long)txMax,
      (unsigned long)rxOk, (unsigned long)rxBad, (unsigned long)lostCnt, lastRssi, lastSnr);
    if (irqEstranha) {
      Serial.printf("-- irqEstranha=%lu (pulsos no DIO1 que o chip nao gerou)\n",
                    (unsigned long)irqEstranha);
    }
  }
}
