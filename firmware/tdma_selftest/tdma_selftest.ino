/*
 * AUTO-TESTE da matematica do tdma_core.h.
 *
 * Roda em QUALQUER ESP32 - nao precisa de radio, nem de GPS, nem de fiacao.
 * Da para gravar numa das telas Waveshare e conferir tudo pelo serial antes de
 * ter o E22 na mao. Imprime PASS/FAIL por caso e um resumo no fim.
 *
 * O relogio e injetado (TDMA_TEST_CLOCK) -> os testes sao deterministas e
 * instantaneos: nao ha espera em tempo real.
 *
 *   .\tools\build.ps1 firmware\tdma_selftest -Upload -Port COMx
 */
#define TDMA_TEST_CLOCK
#include "../tdma_core.h"

uint64_t tdmaTestNowUs = 0;   // o relogio que os testes controlam

int gPass = 0, gFail = 0;

void check(bool ok, const char* what){
  if (ok) { gPass++; Serial.printf("  PASS  %s\n", what); }
  else    { gFail++; Serial.printf("  FAIL  %s   <-------\n", what); }
}
void checkEqU(uint32_t got, uint32_t want, const char* what){
  bool ok = (got == want);
  if (ok) { gPass++; Serial.printf("  PASS  %s (=%lu)\n", what, (unsigned long)got); }
  else    { gFail++; Serial.printf("  FAIL  %s: got=%lu want=%lu   <-------\n", what, (unsigned long)got, (unsigned long)want); }
}

// ---------------------------------------------------------------------------
void testConfig(){
  Serial.println("\n[1] configuracao e capacidade");
  Tdma t; tdmaInit(t, 3, 8, 1, 15000);
  checkEqU(t.frameUs, 1000000UL, "frameUs de 1s");
  checkEqU(t.slotUs, 125000UL, "slotUs = frame/8 = 125ms");
  // air-time de 50ms + guarda 15ms = 65ms, cabe em 125ms -> folga 60ms
  checkEqU((uint32_t)tdmaHeadroomUs(t, 50000), 60000UL, "folga com airtime 50ms");
  check(tdmaFits(t, 50000), "airtime 50ms cabe");
  check(!tdmaFits(t, 120000), "airtime 120ms NAO cabe");
  checkEqU(tdmaMaxNodes(t, 50000), 15UL, "caberiam 15 nos com airtime 50ms");
  // 50 nos em frame de 3s (o cenario do plano): slot de 60ms
  Tdma b; tdmaInit(b, 0, 50, 3, 15000);
  checkEqU(b.slotUs, 60000UL, "50 slots em 3s = 60ms/slot");
  check(!tdmaFits(b, 50000), "airtime 50ms NAO cabe em slot de 60ms com guarda 15ms");
}

// ---------------------------------------------------------------------------
void testNoAnchor(){
  Serial.println("\n[2] sem ancora nao se transmite");
  Tdma t; tdmaInit(t, 2, 8, 1, 15000);
  tdmaTestNowUs = 500000;
  uint32_t u, f;
  check(!tdmaUsInFrame(t, u, f), "usInFrame falha sem ancora");
  bool tx = false;
  for (int i = 0; i < 100; i++) { tdmaTestNowUs += 10000; if (tdmaShouldTx(t)) tx = true; }
  check(!tx, "shouldTx nunca dispara sem ancora");
}

// ---------------------------------------------------------------------------
void testSlotWindow(){
  Serial.println("\n[3] a janela de TX cai dentro do meu slot");
  const uint8_t ID = 3;
  Tdma t; tdmaInit(t, ID, 8, 1, 15000);
  tdmaTestNowUs = 1000000;
  tdmaOnBeacon(t, tdmaTestNowUs);

  // slot 3 -> [3*125000 + 7500 , 3*125000 + 125000 - 7500] = [382500, 492500]
  uint32_t firstTx = 0;
  for (uint32_t step = 0; step < 1000; step++) {
    tdmaTestNowUs = 1000000 + step * 1000;          // passos de 1ms
    if (tdmaShouldTx(t)) { firstTx = step * 1000; break; }
  }
  check(firstTx >= 382500 && firstTx <= 492500, "primeiro TX dentro da janela do slot 3");
  Serial.printf("        (transmitiu em %lu us do frame; janela 382500..492500)\n", (unsigned long)firstTx);
  check(firstTx >= tdmaSlotStartUs(t, ID), "TX nao vaza para o slot anterior");
  check(firstTx < tdmaSlotStartUs(t, ID) + t.slotUs, "TX nao vaza para o slot seguinte");
}

// ---------------------------------------------------------------------------
void testOneTxPerFrame(){
  Serial.println("\n[4] exatamente 1 TX por frame");
  Tdma t; tdmaInit(t, 1, 8, 1, 15000);
  tdmaTestNowUs = 0;
  tdmaOnBeacon(t, 0);

  // varre 10 frames em passos de 1ms e conta os TX
  uint32_t txs = 0;
  for (uint32_t ms = 0; ms < 10000; ms++) {
    tdmaTestNowUs = (uint64_t)ms * 1000;
    // re-ancora a cada frame, como o beacon real faria
    if (ms > 0 && ms % 1000 == 0) tdmaOnBeacon(t, tdmaTestNowUs);
    if (tdmaShouldTx(t)) txs++;
  }
  checkEqU(txs, 10UL, "10 TX em 10 frames");
  checkEqU(t.txCount, 10UL, "contador interno txCount");

  // chamar duas vezes no MESMO instante da janela nao duplica
  Tdma u; tdmaInit(u, 0, 8, 1, 15000);
  tdmaTestNowUs = 0; tdmaOnBeacon(u, 0);
  tdmaTestNowUs = 20000;                              // dentro do slot 0
  bool a = tdmaShouldTx(u), b = tdmaShouldTx(u);
  check(a && !b, "segunda chamada no mesmo frame retorna false");
}

// ---------------------------------------------------------------------------
void testMissedWindow(){
  Serial.println("\n[5] janela perdida e contabilizada (loop travado)");
  Tdma t; tdmaInit(t, 1, 8, 1, 15000);
  tdmaTestNowUs = 0; tdmaOnBeacon(t, 0);
  // salta direto para DEPOIS da janela do slot 1 (que acaba em 242500)
  tdmaTestNowUs = 300000;
  check(!tdmaShouldTx(t), "nao transmite fora da janela");
  checkEqU(t.missedTx, 1UL, "missedTx registrou a janela perdida");
  // e nao transmite atrasado dentro do mesmo frame
  tdmaTestNowUs = 350000;
  check(!tdmaShouldTx(t), "nao transmite atrasado no mesmo frame");
}

// ---------------------------------------------------------------------------
void testLongRun(){
  Serial.println("\n[6] aritmetica de 64 bits (onde micros() estouraria)");
  Tdma t; tdmaInit(t, 5, 8, 1, 15000);
  // micros() (uint32) estoura em ~4295 s. Vamos MUITO alem disso.
  uint64_t base = 10000ULL * 1000000ULL;    // 10.000 s
  tdmaTestNowUs = base;
  tdmaOnBeacon(t, base);
  uint32_t u, f;
  tdmaTestNowUs = base + 700000;            // 700ms no frame
  check(tdmaUsInFrame(t, u, f), "usInFrame ok depois de 10.000s");
  checkEqU(u, 700000UL, "posicao no frame correta apos overflow de 32 bits");

  // 1 hora de operacao continua, re-ancorando: contagem de TX bate
  Tdma h; tdmaInit(h, 2, 8, 1, 15000);
  tdmaTestNowUs = base; tdmaOnBeacon(h, base);
  for (uint32_t ms = 0; ms < 3600UL * 1000UL; ms += 5) {   // passos de 5ms, 1h
    tdmaTestNowUs = base + (uint64_t)ms * 1000ULL;
    if (ms > 0 && ms % 1000 == 0) tdmaOnBeacon(h, tdmaTestNowUs);
    tdmaShouldTx(h);
  }
  checkEqU(h.txCount, 3600UL, "3600 TX em 1 hora (1/frame de 1s)");
  checkEqU(h.missedTx, 0UL, "nenhuma janela perdida em 1 hora");
}

// ---------------------------------------------------------------------------
void testHoldover(){
  Serial.println("\n[7] holdover quando a ancora para de chegar");
  Tdma t; tdmaInit(t, 1, 8, 1, 15000);
  tdmaTestNowUs = 0; tdmaOnBeacon(t, 0);
  tdmaTick(t);
  check(!t.holdover, "sem holdover logo depois da ancora");
  checkEqU(tdmaGuardNow(t), 15000UL, "guarda normal");

  tdmaTestNowUs = 1400000;   // 1,4s: ainda dentro do TTL de 1,5s
  tdmaTick(t);
  check(!t.holdover, "1,4s sem ancora: ainda nao e holdover");

  tdmaTestNowUs = 2000000;   // 2s: passou do TTL
  tdmaTick(t);
  check(t.holdover, "2s sem ancora: entrou em holdover");
  checkEqU(tdmaGuardNow(t), 45000UL, "guarda triplicada no holdover");
  // continua transmitindo pelo relogio interno (nao para a rede)
  uint32_t txs = 0;
  for (uint32_t ms = 2000; ms < 5000; ms++) { tdmaTestNowUs = (uint64_t)ms * 1000; if (tdmaShouldTx(t)) txs++; }
  check(txs >= 2, "segue transmitindo em holdover (nao para a rede)");
}

// ---------------------------------------------------------------------------
void testPps(){
  Serial.println("\n[8] ancora por PPS usa o segundo UTC (frame de 3s)");
  Tdma t; tdmaInit(t, 0, 50, 3, 15000);
  // borda do PPS no segundo UTC 7. 7 % 3 = 1 -> estamos a 1s do inicio do frame
  tdmaTestNowUs = 5000000;
  tdmaOnPps(t, tdmaTestNowUs, 7);
  uint32_t u, f;
  check(tdmaUsInFrame(t, u, f), "usInFrame com PPS");
  checkEqU(u, 1000000UL, "offset = (7 % 3) * 1s");

  tdmaTestNowUs = 5000000 + 250000;   // 250ms depois da borda
  tdmaUsInFrame(t, u, f);
  checkEqU(u, 1250000UL, "offset acompanha o tempo desde a borda");

  // segundo UTC 9 e multiplo de 3 -> inicio exato do frame
  tdmaOnPps(t, tdmaTestNowUs, 9);
  tdmaUsInFrame(t, u, f);
  checkEqU(u, 0UL, "UTC 9 (multiplo de 3) cai no inicio do frame");
  check(t.sync == TDMA_SYNC_PPS, "sync marcado como PPS");
}

// ---------------------------------------------------------------------------
void testNoOverlap(){
  Serial.println("\n[9] janelas de slots diferentes NAO se sobrepoem (prova de nao-colisao)");
  const uint8_t N = 8; const uint32_t G = 15000;
  Tdma t; tdmaInit(t, 0, N, 1, G);
  bool overlap = false;
  uint32_t prevHi = 0;
  for (uint8_t s = 0; s < N; s++) {
    uint32_t lo = tdmaSlotStartUs(t, s) + G / 2;
    uint32_t hi = tdmaSlotStartUs(t, s) + t.slotUs - G / 2;
    if (s > 0 && lo <= prevHi) overlap = true;
    prevHi = hi;
  }
  check(!overlap, "nenhuma sobreposicao entre as 8 janelas");
  // a separacao entre o fim de uma janela e o inicio da proxima e a guarda
  uint32_t gap = (tdmaSlotStartUs(t, 1) + G / 2) - (tdmaSlotStartUs(t, 0) + t.slotUs - G / 2);
  checkEqU(gap, G, "intervalo entre janelas vizinhas = guarda inteira");

  // e o slot calculado a partir do tempo bate com o dono da janela
  bool ok = true;
  for (uint8_t s = 0; s < N; s++) {
    uint32_t mid = tdmaSlotStartUs(t, s) + t.slotUs / 2;
    if (tdmaSlotAt(t, mid) != s) ok = false;
  }
  check(ok, "tdmaSlotAt no meio de cada slot devolve o slot certo");
}

// ---------------------------------------------------------------------------
void testPacket(){
  Serial.println("\n[10] pacote de 16 bytes: round-trip");
  TdmaPkt a;
  a.room = 0xABCDEF;                 // 24 bits cheios
  a.slot = 5; a.seq = 200;
  a.flags = TDMA_FL_FIX | TDMA_FL_ALERT;
  a.lat = -23.5505199;               // hemisferio SUL e OESTE (sinal negativo)
  a.lon = -46.6333094;
  a.heading = 270.0f; a.speed = 88;

  uint8_t p[TDMA_PKT_N];
  tdmaPack(p, a);
  TdmaPkt b;
  check(tdmaUnpack(p, TDMA_PKT_N, b), "unpack aceita 16 bytes");
  check(!tdmaUnpack(p, 15, b), "unpack recusa 15 bytes");

  checkEqU(b.room, 0xABCDEFUL, "room de 24 bits preservado");
  checkEqU(b.slot, 5UL, "slot");
  checkEqU(b.seq, 200UL, "seq");
  check(b.flags == a.flags, "flags");
  check(tdmaPktFix(b) && tdmaPktAlert(b) && !tdmaPktLeader(b), "leitura das flags");
  // 1e-7 grau = ~1,1cm. O erro do round-trip tem de ser menor que isso.
  double dLat = fabs(b.lat - a.lat), dLon = fabs(b.lon - a.lon);
  Serial.printf("        erro lat=%.9f lon=%.9f grau\n", dLat, dLon);
  check(dLat < 1e-7 && dLon < 1e-7, "lat/lon negativos com erro < 1e-7 grau");
  check(fabs(b.heading - 270.0f) <= 2.0f, "heading dentro da resolucao de 2 graus");
  checkEqU(b.speed, 88UL, "speed");

  // heading fora de faixa deve normalizar, nao estourar o byte
  TdmaPkt c = a; c.heading = 725.0f;   // 725 - 720 = 5
  tdmaPack(p, c); tdmaUnpack(p, TDMA_PKT_N, b);
  check(b.heading >= 0 && b.heading < 360.0f, "heading 725 normalizado para dentro de 0..360");
  TdmaPkt d = a; d.heading = -30.0f;   // -30 -> 330
  tdmaPack(p, d); tdmaUnpack(p, TDMA_PKT_N, b);
  check(b.heading >= 328.0f && b.heading <= 332.0f, "heading -30 normalizado para ~330");
}

// ---------------------------------------------------------------------------
// Grade de tempo: GPS e UTC diferem 18s INTEIROS. A borda do pulso e a mesma nas
// duas, mas o ROTULO nao - e tdmaUsInFrame() usa (anchorSec % frameSecs). Se um
// no rotular em UTC e outro em GPS, os frames se deslocam de (18 % frameSecs).
// Este teste trava o contrato: quais frames sao imunes, e a prova de que 4 e 5
// (os que 25-50 nos exigem em SF7) NAO sao.
void testTimeGrid(){
  Serial.println("\n[11] grade de tempo GPS x UTC (18s) e frameSecs sensivel");

  check( tdmaFrameSecsGridSafe(1),  "frameSecs 1 e imune");
  check( tdmaFrameSecsGridSafe(2),  "frameSecs 2 e imune");
  check( tdmaFrameSecsGridSafe(3),  "frameSecs 3 e imune");
  check(!tdmaFrameSecsGridSafe(4),  "frameSecs 4 e SENSIVEL a grade");
  check(!tdmaFrameSecsGridSafe(5),  "frameSecs 5 e SENSIVEL a grade");
  check( tdmaFrameSecsGridSafe(6),  "frameSecs 6 e imune");
  check( tdmaFrameSecsGridSafe(9),  "frameSecs 9 e imune");
  check( tdmaFrameSecsGridSafe(18), "frameSecs 18 e imune");

  // o flag chega no struct, que e por onde o sketch avisa no boot
  Tdma a; tdmaInit(a, 0, 8, 1, 15000);
  check(!a.gridSensitive, "frame de 1s nao levanta gridSensitive");
  Tdma b; tdmaInit(b, 0, 50, 4, 20000);
  check(b.gridSensitive, "frame de 4s (50 nos em SF7) levanta gridSensitive");

  // A PROVA do estrago: dois nos identicos, mesma borda de PPS, mas um rotulando
  // em UTC e outro na grade GPS (+18). Com frameSecs=4 eles discordam do frame.
  const uint32_t utcSec = 1000;
  const uint32_t gpsSec = utcSec + TDMA_GRID_OFFSET_SEC;   // 1018

  Tdma u4; tdmaInit(u4, 0, 8, 4, 15000);
  Tdma g4; tdmaInit(g4, 0, 8, 4, 15000);
  tdmaTestNowUs = 5000000ULL;
  tdmaOnPps(u4, tdmaTestNowUs, utcSec);
  tdmaOnPps(g4, tdmaTestNowUs, gpsSec);
  uint32_t uu, ug, fu, fg;
  tdmaUsInFrame(u4, uu, fu);
  tdmaUsInFrame(g4, ug, fg);
  checkEqU(ug > uu ? ug - uu : uu - ug, 2000000UL,
           "frame 4s: nos em grades diferentes ficam 2s deslocados");

  // e a imunidade, com o mesmo cenario e frameSecs que divide 18
  Tdma u6; tdmaInit(u6, 0, 8, 6, 15000);
  Tdma g6; tdmaInit(g6, 0, 8, 6, 15000);
  tdmaOnPps(u6, tdmaTestNowUs, utcSec);
  tdmaOnPps(g6, tdmaTestNowUs, gpsSec);
  tdmaUsInFrame(u6, uu, fu);
  tdmaUsInFrame(g6, ug, fg);
  checkEqU(ug > uu ? ug - uu : uu - ug, 0UL,
           "frame 6s: mesmas grades diferentes, ZERO deslocamento");
}

void setup(){
  Serial.begin(115200);
  delay(600);
  Serial.println("\n=========================================");
  Serial.println(" AUTO-TESTE tdma_core.h (sem radio/GPS)");
  Serial.println("=========================================");

  testConfig();
  testNoAnchor();
  testSlotWindow();
  testOneTxPerFrame();
  testMissedWindow();
  testLongRun();
  testHoldover();
  testPps();
  testNoOverlap();
  testPacket();
  testTimeGrid();

  Serial.printf("\n=========================================\n");
  Serial.printf(" RESULTADO: %d PASS, %d FAIL\n", gPass, gFail);
  Serial.println(gFail == 0 ? " TUDO OK" : " HA FALHAS - ver linhas com <-------");
  Serial.println("=========================================");
}

void loop(){ delay(1000); }
