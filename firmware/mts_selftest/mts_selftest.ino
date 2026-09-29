/*
 * MTS SELFTEST - exercita o core do firmware/mts na PLACA, sem radio, GPS, tela
 * ou cartao. Roda em qualquer ESP32 (P4, S3, devkit). Imprime PASS/FAIL no serial.
 *
 *   .\tools\build.ps1 firmware\mts_selftest -Board p4 -Upload -Port COMx
 *
 * O mesmo roteiro existe em Node (web/bancada-trilha/mts_core_test.js), que roda
 * no PC sem placa e ainda audita o fonte. Este aqui fecha a lacuna do compilador
 * de verdade: int16 rolante, double no RISC-V, memmove, static_assert do core.
 * Os includes usam os MESMOS headers que o firmware (nao ha copia).
 */
#include "../mts/core/params.h"
#include "../mts/core/airtime.h"
#include "../mts/core/proto.h"
#include "../mts/core/geo.h"
#include "../mts/core/cfg.h"
#include "../mts/core/anel.h"
#include "../mts/core/trajeto.h"
#include "../mts/core/rota.h"
#include "../mts/core/frota.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, nome) do { if (cond) { g_pass++; Serial.printf("PASS %s\n", nome); } \
                               else { g_fail++; Serial.printf("FAIL %s\n", nome); } } while (0)

static Frota   g_f;
static Trajeto g_t;
static Rota    g_r;

void setup()
{
  Serial.begin(115200);
  delay(1500);
  Serial.printf("\n=== mts_selftest (core do firmware/mts) fw v%d proto v%d ===\n", MTS_VERSAO, PROTO_VERSAO);

  // [0] contas de tempo (as mesmas dos static_assert, agora impressas)
  Serial.printf("air-time 16 B = %lu us | 23 B = %lu us | 68 B = %lu us | slot %lu us\n",
                (unsigned long)airtimeUs(16), (unsigned long)airtimeUs(PROTO_ROSTER_N),
                (unsigned long)airtimeUs(68), (unsigned long)TDMA_SLOT_US);
  CHECK(airtimeUs(68) + TDMA_GUARD_US + 40000UL <= TDMA_SLOT_US, "68 B cabem no slot com 40 ms de folga");
  CHECK(TRILHA_PKT_N(TRILHA_PONTOS_PKT) == 68, "pacote cheio = 68 B");
  CHECK(protoSalaHash("12345") != protoSalaHash("12346"), "hash da sala distingue codigos vizinhos");

  // [1] proto: pack/unpack, versao, roster
  {
    uint8_t p[70];
    ProtoPos k = { protoSalaHash("12345"), 3, 200, (uint8_t)(FL_FIX | FL_LIDER | FL_TRILHA), -23.123456, -46.654321, 271.0f, 37 };
    protoPackPos(p, k);
    ProtoPos d;
    CHECK(protoUnpackPos(p, 16, d) && d.sala == k.sala && d.slot == 3 && d.seq == 200 && d.flags == k.flags, "cabecalho atravessa o pack/unpack");
    CHECK(fabs(d.lat - k.lat) < 2e-7 && fabs(d.lon - k.lon) < 2e-7, "lat/lon em 1e-7 grau");
    CHECK(d.rumo == 270.0f && d.vel == 37, "rumo em graus/2 e velocidade");
    int rej = 0;
    for (int b0 = 0; b0 < 256; b0++) { p[0] = (uint8_t)b0; if (!protoUnpackPos(p, 16, d)) rej++; }
    CHECK(rej == 255, "todo byte 0 diferente da versao e rejeitado");
    p[0] = PROTO_VERSAO; p[3] = FROTA_MAX_CARROS;
    CHECK(!protoUnpackPos(p, 16, d), "slot invalido e rejeitado");
    uint8_t r[32]; char nome[ROSTER_NOME_N + 1];
    int n = protoPackRoster(r, 7, 2, 9, 5, true, "Gino\x01xyz");
    protoRosterNome(r, nome, sizeof(nome));
    CHECK(n == PROTO_ROSTER_N && protoEhRoster(r, n) && protoRosterCor(r) == 5 && strcmp(nome, "Gino?xyz") == 0, "roster: nome sanitizado, cor, tamanho");
  }

  // [2] geo
  {
    double la, lo;
    geoAvanca(-23.5, -46.6, 90.0f, 100.0, la, lo);
    CHECK(fabs(geoHaversine(-23.5, -46.6, la, lo) - 100.0) < 0.5, "avancar 100 m para leste = 100 m");
    CHECK(fabs(geoRumo(-23.5, -46.6, la, lo) - 90.0f) < 0.5f, "rumo de volta = 90");
  }

  // [3] frota: TTLs e extrapolacao com relogio injetado
  {
    frotaInicia(g_f); frotaIdentidade(g_f, "eu", 0, 0);
    ProtoPos k = { 1, 2, 1, (uint8_t)(FL_FIX | FL_LIDER), -23.5, -46.6, 90.0f, 36 };
    bool al;
    int i = frotaRecebePos(g_f, k, 1000, al);
    CHECK(i == 1 && g_f.c[1].lider && !g_f.c[1].temNome, "primeiro pacote cria o carro");
    k.slot = 0;
    CHECK(frotaRecebePos(g_f, k, 1000, al) == -2 && g_f.conflitos == 1, "meu slot = conflito");
    double la, lo; bool ext;
    frotaEstimativa(g_f.c[1], 7000, la, lo, ext);
    CHECK(ext && fabs(geoHaversine(-23.5, -46.6, la, lo) - 60.0) < 0.5, "6 s a 36 km/h = 60 m a frente");
    frotaEstimativa(g_f.c[1], 31000, la, lo, ext);
    CHECK(fabs(geoHaversine(-23.5, -46.6, la, lo) - 120.0) < 0.5, "congela em 12 s (120 m)");
    CHECK(frotaSinal(g_f.c[1], 1000 + FROTA_SINAL_VELHO_MS) == SINAL_VIVO && frotaSinal(g_f.c[1], 1001 + FROTA_SINAL_VELHO_MS) == SINAL_VELHO, "VELHO apos FROTA_SINAL_VELHO_MS");
    bool nm;
    frotaRecebeRoster(g_f, 2, 5, true, "Gino", 1000 + FROTA_SOME_MS, nm);
    CHECK(nm && g_f.c[1].ultimoMs == 1000, "roster nao conta como sinal de vida");
    CHECK(frotaEnvelhece(g_f, 1000 + FROTA_SOME_MS) == 0 && frotaEnvelhece(g_f, 1001 + FROTA_SOME_MS) == 1, "some apos FROTA_SOME_MS");
  }

  // [4] cfg: migracao
  {
    Cfg c; cfgPadrao(c); c.pwr = 6; uint8_t e;
    CHECK(cfgMigra(c, 0, 0, e) && e == CFG_ESQUEMA && c.pwr == RF_PWR_PADRAO, "esquema 0 sem pwrv: pwr abaixo do padrao sobe");
    c.pwr = 14; CHECK(cfgMigra(c, 0, 0, e) && c.pwr == 14, "esquema 0 sem pwrv com 14 dBm: continua 14 (so sobe)");
    c.pwr = 6; CHECK(cfgMigra(c, 0, 101, e) && c.pwr == 6, "v101: pwr e do usuario");
    c.slot = 9; c.zoom = 7; CHECK((cfgValida(c) & (CFG_AJ_SLOT | CFG_AJ_ZOOM)) == (CFG_AJ_SLOT | CFG_AJ_ZOOM) && c.slot == 0, "cfgValida clampa slot e zoom");
  }

  // [5] anel: descarte do mais antigo por linha inteira
  {
    static char buf[64]; Anel a; anelInicia(a, buf, sizeof(buf));
    anelEscreve(a, "linha1\n", 7); anelEscreve(a, "linha2\n", 7);
    char grande[60]; memset(grande, 'x', 59); grande[59] = '\n';
    anelEscreve(a, grande, 60);
    CHECK(a.descartadas == 2 && a.pendentes == 60 && a.linhas == 3, "anel cheio descarta linhas inteiras antigas");
    const char* p1; const char* p2; uint32_t n1, n2;
    CHECK(anelFatias(a, p1, n1, p2, n2) == 60 && n1 + n2 == 60, "fatias somam o pendente");
  }

  // [6] trajeto + rota + catch-up (o roteiro do catchup_selftest, com o core novo)
  {
    trajetoInicia(g_t, 7); rotaLimpaCore(g_r);
    for (int i = 0; i < 40; i++) trajetoTalvezAdicione(g_t, -23.15 + i * 0.0001, -46.55 + i * 0.0001);
    CHECK(g_t.n == 40 && g_t.seqUlt == 39, "40 pontos de 10+ m entram numerados");
    CHECK(!trajetoTalvezAdicione(g_t, -23.15 + 39 * 0.0001 + 1e-6, -46.55 + 39 * 0.0001), "passo menor que ROTA_PASSO_M nao entra");
    uint16_t base; int n;
    bool c = trilhaEscolheBloco(g_t.seqUlt, g_t.n, ROTA_SEQ_NENHUMA, ROTA_SEQ_NENHUMA, false, 0, TRILHA_PONTOS_PKT, base, n);
    CHECK(!c && n == 10 && base == 30, "sem ack: bloco recente de 10 a partir de 30");
    uint8_t pkt[80];
    int len = trilhaCodifica(pkt, g_t.epoca, base, 0, 0, n, [&](int i, double& la, double& lo) { trajetoPontoSeq(g_t, (uint16_t)(base + i), la, lo); });
    uint8_t ep; uint16_t b2, a1, a2; int novos = 0;
    trilhaDecodifica(pkt, len, ep, b2, a1, a2, [&](uint16_t s, double la, double lo) { if (rotaInsere(g_r, rotaSeqVirtual(g_r, ep, s), la, lo)) novos++; });
    CHECK(novos == 10 && g_r.n == 10 && rotaAck16(g_r) == 39 && rotaAckDe16(g_r) == 30, "seguidor recebe 10 pontos e acusa ackAte=39 ackDe=30");
    c = trilhaEscolheBloco(g_t.seqUlt, g_t.n, 39, 30, true, 1, TRILHA_PONTOS_PKT, base, n);
    CHECK(c && base == 20 && n == 10, "com historico faltando, o quadro impar traz o bloco 20..29");
  }

  // [7] as travas do catch-up (revisao de 2026-09-29), com os headers de verdade
  {
    // km[] anda junto quando a rota enche
    rotaLimpaCore(g_r);
    for (int i = 0; i < ROTA_MAX + 300; i++) rotaInsere(g_r, (uint32_t)i, -23.15 + i * 0.00009, -46.55);
    float pior = 0, soma = 0;
    for (int i = 1; i < g_r.n; i++) {
      soma += sqrtf(rotaD2(g_r.p[i - 1].lat, g_r.p[i - 1].lon, g_r.p[i].lat, g_r.p[i].lon));
      float e = fabsf(g_r.km[i] - soma); if (e > pior) pior = e;
    }
    Serial.printf("     rota cheia: pior erro de km = %.3f m\n", pior);
    CHECK(g_r.n == ROTA_MAX && g_r.km[0] == 0 && pior < 2.0f, "rota cheia: km[] recomeca no novo p[0] e acompanha a distancia");

    // PISO: um vao abaixo do ponto mais antigo do lider nao prende o ack
    rotaLimpaCore(g_r);
    for (uint16_t s = 0; s < 20; s++) rotaInsere(g_r, rotaSeqVirtual(g_r, 5, s), -23.15 + s * 1e-4, -46.55);
    for (uint16_t s = 100; s < 120; s++) rotaInsere(g_r, rotaSeqVirtual(g_r, 5, s), -23.15 + s * 1e-4, -46.55);
    CHECK(rotaAck16(g_r) == 19 && rotaTopo16(g_r) == 119, "vao 20..99: ack preso em 19, topo 119");
    rotaDefinePiso(g_r, 5, 50);
    CHECK(rotaAck16(g_r) == 19, "piso 50 (o lider ainda tem 50..99): o ack continua em 19");
    rotaDefinePiso(g_r, 5, 100);
    CHECK(rotaAck16(g_r) == 119, "piso 100 (o vao inteiro ja nao existe): o ack passa por cima e vai a 119");
    rotaDefinePiso(g_r, 9, 200);
    CHECK(rotaAck16(g_r) == 119, "piso de outra epoca e ignorado");
    uint16_t s0 = 0;
    rotaInsere(g_r, rotaSeqVirtual(g_r, 6, s0), -23.0, -46.0);                // o lider reiniciou
    CHECK(!g_r.temPiso, "epoca nova zera o piso");

    // quem nao ouve o lider nao dita o bloco
    frotaInicia(g_f); frotaIdentidade(g_f, "eu", 0, 0);
    ProtoPos k = { 1, 1, 1, (uint8_t)FL_FIX, -23.5, -46.6, 0, 0 }; bool al;
    frotaRecebePos(g_f, k, 1000, al); k.slot = 2; frotaRecebePos(g_f, k, 1000, al);
    frotaAck(g_f, 1, 300, 0, 500, 1000);                       // ouve (topo = meu head)
    frotaAck(g_f, 2, ROTA_SEQ_NENHUMA, ROTA_SEQ_NENHUMA, ROTA_SEQ_NENHUMA, 1000);   // surdo
    uint16_t am, dm;
    CHECK(frotaAckMin(g_f, 500, am, dm, 1000) && am == 300, "o surdo fica fora: o bloco e o do que ouve (ack 300)");
    frotaAck(g_f, 2, 100, 0, (uint16_t)(500 - ROTA_OUVE_PONTOS - 1), 1000);
    CHECK(frotaAckMin(g_f, 500, am, dm, 1000) && am == 300, "topo mais que ROTA_OUVE_PONTOS atras = nao me ouve");
    frotaAck(g_f, 2, 100, 0, 495, 1000);
    CHECK(frotaAckMin(g_f, 500, am, dm, 1000) && am == 100, "topo perto do head: volta a contar (ack 100 manda)");
  }

  Serial.printf("\n%d PASS, %d FAIL\n", g_pass, g_fail);
}

void loop() { delay(1000); }
