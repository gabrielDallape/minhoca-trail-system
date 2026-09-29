/*
 * CATCH-UP SELFTEST - exercita o rota_core.h do mts_campo sem radio, GPS ou tela.
 * Roda em qualquer ESP32 (P4, S3, devkit). Imprime PASS/FAIL no serial.
 *
 *   .\tools\build.ps1 firmware\catchup_selftest -Board p4 -Upload -Port COMx
 *
 * O mesmo roteiro existe em Node (web/bancada-trilha/catchup_test.js), que roda
 * no PC sem placa. Este aqui e o que fecha a lacuna do compilador de verdade
 * (int16 rolante, lround, memmove no alvo).
 */
#include "../mts_campo/rota_core.h"

// O rota_core.h nao inclui o hardware.h de proposito (e sem hardware); o numero
// de pontos por pacote vem de la. Tem de bater com TRILHA_PONTOS_PKT do
// hardware.h - o teste "pacote cheio = 68 B" acusa se divergir.
#define TRILHA_PONTOS_PKT 10

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, nome) do { if (cond) { g_pass++; Serial.printf("PASS %s\n", nome); } \
                               else { g_fail++; Serial.printf("FAIL %s\n", nome); } } while (0)

// Um "lider" de mentira: trajeto numerado, 5 m por ponto rumo nordeste.
struct Lider {
  double lat[2000], lon[2000];
  uint16_t head = 0xFFFF; int n = 0; uint8_t epoca = 7;
  void anda() {
    int i = n++;
    lat[i] = -23.15 + i * 0.00004; lon[i] = -46.55 + i * 0.00003;
    head++;
  }
  bool ponto(uint16_t seq, double& la, double& lo) const {
    int atras = (int)(uint16_t)(head - seq);
    if (atras < 0 || atras >= n) return false;
    la = lat[n - 1 - atras]; lo = lon[n - 1 - atras]; return true;
  }
};

// Um quadro: o lider monta o pacote a partir do ack do seguidor; se 'entrega',
// o seguidor recebe e devolve o ack novo.
struct Ack { uint16_t ate = ROTA_SEQ_NENHUMA, de = ROTA_SEQ_NENHUMA; bool tem = false; };
static int quadro(Lider& L, Rota& R, Ack& ack, uint32_t q, bool entrega, bool& catchup)
{
  uint8_t pkt[80];
  uint16_t base; int n;
  catchup = trilhaEscolheBloco(L.head, L.n, ack.ate, ack.de, ack.tem, q, TRILHA_PONTOS_PKT, base, n);
  int len = trilhaCodifica(pkt, L.epoca, base, 0, 0, n, [&](int i, double& la, double& lo) {
    if (!L.ponto((uint16_t)(base + i), la, lo)) { la = 0; lo = 0; }
  });
  if (len != TRILHA_PKT_N(pkt[23])) { Serial.println("FAIL tamanho codificado inconsistente"); g_fail++; }
  if (!entrega) return 0;
  uint16_t b, a1, a2; uint8_t epLida; int novos = 0;
  const uint8_t ep = pkt[16];      // a epoca e lida do pacote antes de entregar (como o mundo.h faz)
  int got = trilhaDecodifica(pkt, len, epLida, b, a1, a2,
    [&](uint16_t s, double la, double lo) {
      if (rotaInsere(R, rotaSeqVirtual(R, ep, s), la, lo)) novos++;
    });
  (void)got;
  ack.ate = rotaAck16(R); ack.de = rotaAckDe16(R); ack.tem = true;
  return novos;
}

static bool rotaContinua(const Rota& R) {
  for (int i = 1; i < R.n; i++) if (R.p[i].vseq != R.p[i - 1].vseq + 1) return false;
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== catchup_selftest (rota_core.h) ===");

  // 1) tamanhos do pacote
  CHECK(TRILHA_PKT_N(0) == 24, "pacote sem pontos = 24 B");
  CHECK(TRILHA_PKT_N(TRILHA_PONTOS_PKT) == 68, "pacote cheio = 68 B");

  // 2) codec: ida e volta com delta e corte por estouro
  {
    uint8_t pkt[80];
    double la[5] = { -23.1, -23.1001, -23.1002, -23.5, -23.5001 };   // o 4o salta 44 km
    double lo[5] = { -46.5, -46.5001, -46.5002, -46.5,  -46.5001 };
    int len = trilhaCodifica(pkt, 3, 100, 0xBEEF, 0x1234, 5, [&](int i, double& a, double& b) { a = la[i]; b = lo[i]; });
    uint8_t ep; uint16_t base, ack, ackDe; int cnt = 0; double ula = 0;
    int n = trilhaDecodifica(pkt, len, ep, base, ack, ackDe, [&](uint16_t s, double a, double b) { cnt++; ula = a; (void)s; (void)b; });
    CHECK(n == 3 && cnt == 3, "delta que nao cabe em int16 corta o bloco (3 de 5)");
    CHECK(ep == 3 && base == 100 && ack == 0xBEEF && ackDe == 0x1234, "epoca/base/acks atravessam o codec");
    CHECK(fabs(ula - (-23.1002)) < 2e-5, "ponto reconstruido por delta dentro de 2e-5 grau");
    CHECK(len == TRILHA_PKT_N(3), "tamanho codificado bate com TRILHA_PKT_N");
  }

  // 3) fluxo normal sem perda: nunca catch-up, rota continua
  {
    Lider L; Rota R; rotaLimpaCore(R); Ack ack; int catchups = 0;
    for (uint32_t q = 0; q < 40; q++) {
      for (int k = 0; k < 3; k++) L.anda();
      bool c; quadro(L, R, ack, q, true, c);
      if (c) catchups++;
    }
    CHECK(rotaContinua(R), "sem perda: rota continua");
    CHECK(R.n == L.n, "sem perda: rota tem todos os pontos do lider");
    CHECK(catchups == 0, "sem perda: nenhum quadro de catch-up");
  }

  // 4) vao de 20 quadros perdidos: a rota fica com salto, o catch-up fecha
  {
    Lider L; Rota R; rotaLimpaCore(R); Ack ack; bool c;
    uint32_t q = 0;
    for (; q < 10; q++) { for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q, true, c); }
    for (; q < 30; q++) { for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q, false, c); }
    // o primeiro pacote depois do vao traz so os recentes: salto conhecido
    for (int k = 0; k < 3; k++) L.anda();
    quadro(L, R, ack, q++, true, c);
    bool temSalto = false;
    for (int i = 1; i < R.n; i++) if (rotaVaoAntes(R, i)) temSalto = true;
    CHECK(temSalto, "apos o vao: a rota registra o salto de numero");
    CHECK(ack.ate < L.head - 30, "apos o vao: o ack aponta para antes do buraco");
    int catchups = 0, quadros = 0;
    while (!rotaContinua(R) && quadros < 60) {
      for (int k = 0; k < 3; k++) L.anda();
      quadro(L, R, ack, q++, true, c);
      if (c) catchups++;
      quadros++;
    }
    CHECK(rotaContinua(R), "catch-up fecha o buraco");
    for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q++, true, c);   // o quadro seguinte traz os 3 novos
    CHECK(R.n == L.n && rotaContinua(R), "depois do catch-up a rota tem todos os pontos");
    CHECK(catchups >= 5 && catchups <= 9, "60 pontos perdidos fecham em 5-9 quadros de catch-up");
    Serial.printf("     (fechou em %d quadros, %d de catch-up)\n", quadros, catchups);
  }

  // 5) lider reinicia (epoca muda): pontos novos vao DEPOIS dos velhos
  {
    Lider L; Rota R; rotaLimpaCore(R); Ack ack; bool c;
    for (uint32_t q = 0; q < 10; q++) { for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q, true, c); }
    int antes = R.n;
    Lider L2; L2.epoca = 9;         // reiniciou: head volta a 0xFFFF, epoca nova
    for (uint32_t q = 0; q < 5; q++) { for (int k = 0; k < 3; k++) L2.anda(); quadro(L2, R, ack, q, true, c); }
    CHECK(R.n == antes + L2.n, "epoca nova: os pontos entram sem colidir com os velhos");
    CHECK(R.p[R.n - 1].vseq > R.p[antes - 1].vseq, "epoca nova: os pontos novos ficam no fim");
    int catchups = 0;
    for (uint32_t q = 5; q < 25; q++) { for (int k = 0; k < 3; k++) L2.anda(); quadro(L2, R, ack, q, true, c); if (c) catchups++; }
    CHECK(catchups <= 8, "epoca nova: o catch-up para quando a rota nova fica continua");
  }

  // 6) seguidor que chega tarde: recebe os recentes primeiro, depois o resto
  {
    Lider L; Rota R; rotaLimpaCore(R); Ack ack; bool c;
    for (int k = 0; k < 200; k++) L.anda();
    uint32_t q = 0;
    quadro(L, R, ack, q++, true, c);
    CHECK(R.n == TRILHA_PONTOS_PKT, "tardio: o primeiro pacote traz os 10 mais recentes");
    CHECK(ack.de == (uint16_t)(L.head - 9) && ack.ate == L.head, "tardio: ackDe aponta o primeiro que tem, ackAte o head");
    int quadros = 0;
    while (R.n < L.n && quadros < 80) { quadro(L, R, ack, q++, true, c); quadros++; }
    CHECK(R.n == L.n && rotaContinua(R), "tardio: o historico inteiro chega e fica continuo (de tras para a frente)");
    Serial.printf("     (200 pontos em %d quadros)\n", quadros);
  }

  // 7) ordem: pontos fora de ordem entram no lugar certo
  {
    Rota R; rotaLimpaCore(R);
    rotaInsere(R, 10, 1, 1); rotaInsere(R, 12, 3, 3); rotaInsere(R, 11, 2, 2); rotaInsere(R, 11, 9, 9);
    CHECK(R.n == 3 && R.p[1].vseq == 11 && R.p[1].lat == 2, "insercao ordenada e repetido ignorado");
    CHECK(R.prefixo == 2, "prefixo continuo cobre os tres");
    CHECK(fabs(R.km[2] - (R.km[1] + sqrtf(rotaD2(2, 2, 3, 3)))) < 1.0f, "km recalculado apos insercao no meio");
  }

  // 8) rolagem do seq de 16 bits
  {
    Lider L; Rota R; rotaLimpaCore(R); Ack ack; bool c;
    L.head = 0xFFF0;
    for (uint32_t q = 0; q < 20; q++) { for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q, true, c); }
    CHECK(rotaContinua(R) && R.n == L.n, "seq de 16 bits rola por 0xFFFF sem quebrar a rota");
  }

  // 9) perda aleatoria de 50% (o campo de 09-28): tudo recuperado no fim
  {
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFFu; return (double)seed / 0x7FFFFFFF; };
    Lider L; Rota R; rotaLimpaCore(R); Ack ack; bool c; int catchups = 0;
    for (uint32_t q = 0; q < 300; q++) { for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q, rnd() > 0.5, c); if (c) catchups++; }
    for (uint32_t q = 300; q < 340; q++) { for (int k = 0; k < 3; k++) L.anda(); quadro(L, R, ack, q, true, c); if (c) catchups++; }
    CHECK(rotaContinua(R) && R.n == L.n, "50% de perda por 300 quadros: tudo recuperado");
    Serial.printf("     (%d catch-ups)\n", catchups);
  }

  Serial.printf("\n%d PASS, %d FAIL\n", g_pass, g_fail);
}

void loop() { delay(1000); }
