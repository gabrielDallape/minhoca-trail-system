/*
 * Auto-teste do vec_pack.h e do dem_pack.h.
 *
 * Monta os dois containers na RAM e exercita os leitores, INCLUSIVE os caminhos
 * de erro - que sao os que nunca acontecem na bancada e sempre acontecem na
 * trilha: magic errado, versao errada, setor ausente, CRC ruim, buffer pequeno,
 * falha de leitura do cartao.
 *
 * Nao precisa de cartao nem de tela. Roda em qualquer ESP32:
 *     .\tools\build.ps1 firmware\vec_selftest -Board devkit -Upload -Port COMx
 */
#include "../vec_pack.h"
#include "../dem_pack.h"

static int passou = 0, falhou = 0;

static void ok(const char* nome, bool cond) {
  Serial.printf("  %s %s\n", cond ? "PASS" : "FALHA", nome);
  if (cond) passou++; else falhou++;
}
static void okEq(const char* nome, long got, long esp) {
  bool c = (got == esp);
  if (c) Serial.printf("  PASS %s\n", nome);
  else   Serial.printf("  FALHA %s: obtive %ld, esperava %ld\n", nome, got, esp);
  if (c) passou++; else falhou++;
}

// ============================================================ cartao falso
static uint8_t* g_img = nullptr;
static uint32_t g_imgSetores = 0;
static bool g_falhaLeitura = false;

static bool leitor(uint32_t setor, uint32_t n, void* dst, void* user) {
  (void)user;
  if (g_falhaLeitura) return false;
  if (setor + n > g_imgSetores) return false;
  memcpy(dst, g_img + (size_t)setor * 512, (size_t)n * 512);
  return true;
}

static void pw16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void pw32(uint8_t* p, uint32_t v) {
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}

// ============================================== monta um vec pack de mentira
// Dois niveis, grade 3x2 no nivel 0. O setor (xmin+1, ymin) tem duas camadas:
// uma trilha de 3 pontos e um POI. O slot 43 existe de proposito: com registro
// de 12 bytes, 512/12 = 42,67, entao o registro 43 CRUZA a borda de setor - e o
// caminho que a primeira versao do leitor errava.
static const uint32_t V_IDX_SEC = 1, V_IDX_N = 2;
static const uint32_t V_DAT_SEC = V_IDX_SEC + V_IDX_N;

static uint32_t montaVec() {
  const uint32_t nSlots = 3 * 2 + 8 * 8;        // nivel 0 e nivel 1
  memset(g_img, 0, 512 * 16);

  uint8_t* h = g_img;
  memcpy(h, "MTSVECT1", 8);
  pw16(h + 8, 1);            // versao
  h[10] = 2;                 // niveis
  pw32(h + 12, V_IDX_SEC);
  pw32(h + 16, V_DAT_SEC);
  pw32(h + 20, nSlots);
  pw32(h + 24, 4);           // setores de dado (nao usado pelo leitor)

  // nivel 0: zoom 13, tolerancia 1 m, xmin 100, ymin 200, 3x2, offset 0
  uint8_t* L = h + 32;
  L[0] = 13; pw16(L + 2, 1);
  pw32(L + 4, 100); pw32(L + 8, 200); pw16(L + 12, 3); pw16(L + 14, 2);
  pw32(L + 16, 0);
  // nivel 1: zoom 11, tolerancia 8 m, xmin 25, ymin 50, 8x8, offset 6
  L = h + 32 + 20;
  L[0] = 11; pw16(L + 2, 8);
  pw32(L + 4, 25); pw32(L + 8, 50); pw16(L + 12, 8); pw16(L + 14, 8);
  pw32(L + 16, 6);

  // ---- bloco: 2 camadas
  uint8_t bloco[256];
  uint32_t o = 0;
  pw16(bloco + o, 2); o += 2;                       // 2 camadas
  bloco[o++] = VEC_TRILHA; pw16(bloco + o, 1); o += 2;   // 1 feicao
  pw16(bloco + o, 3); o += 2;                       // 3 pontos
  pw16(bloco + o, 0);     pw16(bloco + o + 2, 0);     o += 4;
  pw16(bloco + o, 32768); pw16(bloco + o + 2, 16384); o += 4;
  pw16(bloco + o, 65535); pw16(bloco + o + 2, 65535); o += 4;
  bloco[o++] = VEC_POI;    pw16(bloco + o, 1); o += 2;
  pw16(bloco + o, 100); pw16(bloco + o + 2, 200); o += 4;
  bloco[o++] = VECP_VAU;
  const char* nm = "Vau do Sapo";
  bloco[o++] = (uint8_t)strlen(nm);
  memcpy(bloco + o, nm, strlen(nm)); o += strlen(nm);
  const uint32_t blocoLen = o;
  memcpy(g_img + (size_t)V_DAT_SEC * 512, bloco, blocoLen);

  // ---- indice
  uint8_t* idx = g_img + (size_t)V_IDX_SEC * 512;
  // slot 1 = (x 101, y 200) no nivel 0
  pw32(idx + 1 * 12,     V_DAT_SEC);
  pw32(idx + 1 * 12 + 4, blocoLen);
  pw32(idx + 1 * 12 + 8, vecPackCrc32(bloco, blocoLen));
  // slot 43: cruza a borda de 512 B (43*12 = 516)
  pw32(idx + 43 * 12,     V_DAT_SEC);
  pw32(idx + 43 * 12 + 4, blocoLen);
  pw32(idx + 43 * 12 + 8, vecPackCrc32(bloco, blocoLen));
  // slot 2: CRC de proposito errado
  pw32(idx + 2 * 12,     V_DAT_SEC);
  pw32(idx + 2 * 12 + 4, blocoLen);
  pw32(idx + 2 * 12 + 8, 0xDEADBEEF);

  g_imgSetores = V_DAT_SEC + 4;
  return blocoLen;
}

static void testaVec() {
  Serial.println("\n[1] vec_pack: cabecalho e caminhos de erro");
  uint32_t blocoLen = montaVec();
  VecPack vp;

  ok("abre", vecPackOpen(vp, leitor, nullptr));
  okEq("niveis", vp.nLevels, 2);
  okEq("zoom do nivel 0", vp.levels[0].zoom, 13);
  okEq("offset do nivel 1", vp.levels[1].indexOffset, 6);

  g_img[0] = 'X';
  VecPack mau;
  ok("magic errado e recusado", !vecPackOpen(mau, leitor, nullptr));
  g_img[0] = 'M';
  pw16(g_img + 8, 99);
  ok("versao errada e recusada", !vecPackOpen(mau, leitor, nullptr));
  pw16(g_img + 8, 1);
  ok("reabre depois de consertar", vecPackOpen(vp, leitor, nullptr));

  g_falhaLeitura = true;
  ok("falha do cartao e recusada", !vecPackOpen(mau, leitor, nullptr));
  g_falhaLeitura = false;

  Serial.println("\n[2] vec_pack: endereco");
  okEq("slot de (101,200) no nivel 0", vecPackSlot(vp, 0, 101, 200), 1);
  okEq("slot de (100,201) no nivel 0", vecPackSlot(vp, 0, 100, 201), 3);
  okEq("fora da grade a esquerda", vecPackSlot(vp, 0, 99, 200), -1);
  okEq("fora da grade abaixo", vecPackSlot(vp, 0, 100, 202), -1);
  okEq("slot do nivel 1 soma o offset", vecPackSlot(vp, 1, 25, 50), 6);
  okEq("nivel inexistente", vecPackSlot(vp, 5, 25, 50), -1);

  Serial.println("\n[3] vec_pack: leitura do bloco");
  static uint8_t buf[2048];
  uint32_t len = 0;
  okEq("le o bloco", vecPackReadBlock(vp, 0, 101, 200, buf, sizeof(buf), len, true),
       VECPACK_OK);
  okEq("tamanho do bloco", len, blocoLen);
  okEq("setor sem dado", vecPackReadBlock(vp, 0, 102, 201, buf, sizeof(buf), len, true),
       VECPACK_ABSENT);
  okEq("CRC ruim e detectado",
       vecPackReadBlock(vp, 0, 102, 200, buf, sizeof(buf), len, true), VECPACK_BADCRC);
  okEq("contador de CRC subiu", vp.crcErrors, 1);
  // buffer que cabe o len mas nao o setor inteiro: o estouro classico
  okEq("buffer menor que o setor e recusado",
       vecPackReadBlock(vp, 0, 101, 200, buf, blocoLen, len, false), VECPACK_TOOBIG);

  Serial.println("\n[4] vec_pack: registro de indice que cruza a borda de setor");
  // 43*12 = 516 > 512, entao o registro comeca no setor 1 e termina no setor 2
  uint32_t s = 0, l = 0, c = 0;
  ok("le o registro 43", vecPackEntry(vp, 43, s, l, c));
  okEq("setor do registro 43", s, V_DAT_SEC);
  okEq("tamanho do registro 43", l, blocoLen);

  Serial.println("\n[5] vec_pack: percurso do bloco");
  vecPackReadBlock(vp, 0, 101, 200, buf, sizeof(buf), len, false);
  VecCursor cur;
  ok("inicia o cursor", vecCursorInit(cur, buf, len));
  ok("primeira camada", vecCursorLayer(cur));
  okEq("classe da primeira camada", cur.classe, VEC_TRILHA);
  const uint8_t* pts = nullptr; uint16_t n = 0;
  ok("primeira linha", vecCursorLine(cur, pts, n));
  okEq("pontos da linha", n, 3);
  okEq("x do ponto 1", vpLE16(pts + 4), 32768);
  ok("nao ha segunda linha", !vecCursorLine(cur, pts, n));
  ok("segunda camada", vecCursorLayer(cur));
  okEq("classe da segunda camada", cur.classe, VEC_POI);
  uint16_t px, py; uint8_t sub, nl; const char* nome;
  ok("le o POI", vecCursorPoi(cur, px, py, sub, nome, nl));
  okEq("subclasse do POI", sub, VECP_VAU);
  okEq("tamanho do nome", nl, 11);
  ok("nome do POI", memcmp(nome, "Vau do Sapo", 11) == 0);
  ok("acabou o bloco", !vecCursorLayer(cur));

  Serial.println("\n[6] vec_pack: geometria");
  double fx, fy;
  vecPackDegToTileF(0.0, 0.0, 1, fx, fy);
  ok("Mercator na origem", fabs(fx - 1.0) < 1e-9 && fabs(fy - 1.0) < 1e-9);
  vecPackDegToTileF(-23.5505, -46.6333, 13, fx, fy);
  // CALCULADO com o deg2num do vetor.py, nao estimado. A primeira versao deste
  // teste trazia 3037,02 / 4523,55 escritos de cabeca, e as duas assercoes
  // seguintes cairam por causa disso - teste errado acusando codigo certo custa
  // tanto tempo quanto bug de verdade.
  ok("Mercator na Praca da Se", fabs(fx - 3034.8334) < 0.01 && fabs(fy - 4647.6652) < 0.01);

  VecXform xf;
  vecXformInit(xf, vp, 0, 3034, 4647, -23.5505, -46.6333, 2.0, 1280, 720);
  int sx, sy;
  // o ponto local (0,0) do setor que CONTEM a Praca da Se tem de cair a esquerda
  // e acima do centro da tela
  uint8_t p0[4] = {0, 0, 0, 0};
  vecPoint(xf, p0, 0, sx, sy);
  ok("canto do setor fica acima e a esquerda do centro", sx < 640 && sy < 360);
  // e o canto oposto, a direita e abaixo
  uint8_t p1[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  vecPoint(xf, p1, 0, sx, sy);
  ok("canto oposto fica abaixo e a direita", sx > 640 && sy > 360);

  Serial.println("\n[7] vec_pack: escolha de nivel pela escala");
  okEq("tolerancia do nivel 0 veio do cabecalho", vp.levels[0].tolMetros, 1);
  okEq("tolerancia do nivel 1 veio do cabecalho", vp.levels[1].tolMetros, 8);
  okEq("0,4 m/px usa o detalhe",
       vecPackLevelForScale(vp, 0.4, 1280, 720, 0.0), 0);
  // O BUG QUE ESTE TESTE PEGOU: a regra antiga ("setor >= 2x a tela") devolvia 1
  // aqui, porque o setor de z13 tem 4.892 m e ela exigia 5.120. O aparelho
  // largava o nivel de detalhe no zoom para o qual ele foi feito.
  okEq("2 m/px ainda usa o detalhe", vecPackLevelForScale(vp, 2.0, 1280, 720, 0.0), 0);
  okEq("12 m/px sobe de nivel (tol de 1 m nao paga 8 setores)",
       vecPackLevelForScale(vp, 12.0, 1280, 720, 0.0), 1);
  // tolerancia grosseira demais para o zoom nao pode ser escolhida
  vp.levels[1].tolMetros = 400;
  okEq("nivel simplificado demais e recusado",
       vecPackLevelForScale(vp, 2.0, 1280, 720, 0.0), 0);
  // pacote antigo, sem o campo: cai no criterio por numero de setores
  vp.levels[0].tolMetros = 0; vp.levels[1].tolMetros = 0;
  ok("pacote sem tolerancia ainda escolhe algum nivel",
     vecPackLevelForScale(vp, 12.0, 1280, 720, 0.0) >= 0);
  vp.levels[0].tolMetros = 1; vp.levels[1].tolMetros = 8;
}

// ============================================== monta um dem pack de mentira
static const uint32_t D_N = 5;          // bloco 5x5 para o teste caber
static const uint32_t D_IDX_SEC = 1, D_DAT_SEC = 2;

static void montaDem() {
  memset(g_img, 0, 512 * 16);
  uint8_t* h = g_img;
  memcpy(h, "MTSDEM01", 8);
  pw16(h + 8, 1);
  pw16(h + 10, D_N);
  h[12] = 10;                            // 10 blocos por grau
  pw32(h + 16, D_IDX_SEC);
  pw32(h + 20, D_DAT_SEC);
  pw32(h + 24, 4);                       // 2x2 slots
  pw32(h + 28, (uint32_t)(int32_t)((-24 + 90) * 10));   // lin0
  pw32(h + 32, (uint32_t)(int32_t)((-47 + 180) * 10));  // col0
  pw16(h + 36, 2); pw16(h + 38, 2);
  pw32(h + 40, 1);                       // 1 setor por bloco

  // rampa: sobe 100 m por linha indo para o SUL (linha 0 = norte = mais alto)
  uint8_t bloco[512];
  memset(bloco, 0, sizeof(bloco));
  for (uint32_t r = 0; r < D_N; r++)
    for (uint32_t c = 0; c < D_N; c++)
      pw16(bloco + (r * D_N + c) * 2, (uint16_t)(int16_t)(1000 + r * 100));
  memcpy(g_img + (size_t)D_DAT_SEC * 512, bloco, 512);

  uint8_t* idx = g_img + (size_t)D_IDX_SEC * 512;
  pw32(idx + 0 * 8, D_DAT_SEC);
  pw32(idx + 0 * 8 + 4, demPackCrc32(bloco, D_N * D_N * 2));
  g_imgSetores = D_DAT_SEC + 1;
}

static void testaDem() {
  Serial.println("\n[8] dem_pack: cabecalho e endereco");
  montaDem();
  DemPack dp;
  ok("abre", demPackOpen(dp, leitor, nullptr));
  okEq("amostras por lado", dp.n, D_N);
  okEq("blocos por grau", dp.porGrau, 10);

  // O bloco 0 cobre lat -24,0..-23,9 e lon -47,0..-46,9.
  okEq("slot do ponto dentro", demPackSlot(dp, -23.95, -46.95), 0);
  okEq("slot do vizinho a leste", demPackSlot(dp, -23.95, -46.85), 1);
  okEq("slot do vizinho ao norte", demPackSlot(dp, -23.85, -46.95), 2);
  okEq("fora da grade", demPackSlot(dp, -20.0, -46.95), -1);

  // ARMADILHA DO HEMISFERIO SUL: com (int) em vez de floor, -23,95 truncaria
  // para -23 e o bloco sairia errado. O Brasil inteiro esta em latitude negativa,
  // entao esse erro deslocaria o relevo do pais todo.
  okEq("latitude negativa usa floor e nao truncamento",
       demPackSlot(dp, -23.999, -46.999), 0);

  Serial.println("\n[9] dem_pack: leitura e amostragem");
  static uint8_t bloco[512];
  okEq("le o bloco", demPackReadBlock(dp, 0, bloco, sizeof(bloco), true), DEMPACK_OK);
  okEq("slot vazio e ausente", demPackReadBlock(dp, 1, bloco, sizeof(bloco), true),
       DEMPACK_ABSENT);
  okEq("buffer pequeno e recusado", demPackReadBlock(dp, 0, bloco, 100, true),
       DEMPACK_TOOBIG);

  okEq("amostra do topo (norte)", demAmostra(dp, bloco, 0, 0), 1000);
  okEq("amostra da base (sul)", demAmostra(dp, bloco, D_N - 1, 0), 1400);
  okEq("amostra fora e presa na borda", demAmostra(dp, bloco, -5, 99), 1000);

  // o canto NORTE do bloco e lat -23,9; o canto SUL e -24,0. Como a rampa sobe
  // para o sul, o sul tem de dar mais alto.
  float alN = demAltitude(dp, bloco, -23.9001, -46.95);
  float alS = demAltitude(dp, bloco, -23.9999, -46.95);
  ok("norte do bloco e o valor da linha 0", fabsf(alN - 1000.0f) < 15.0f);
  ok("sul do bloco e mais alto (a rampa nao esta espelhada)", alS > alN + 300.0f);

  float meio = demAltitude(dp, bloco, -23.95, -46.95);
  ok("interpolacao no meio fica entre os dois", meio > alN + 100 && meio < alS - 100);

  Serial.println("\n[10] dem_pack: sombreado e curva de nivel");
  float mx, my;
  demPassoMetros(dp, -23.95, mx, my);
  // O BLOCO DESTE TESTE E 5x5, NAO 121x121. Cravar "entre 80 e 130 m" aqui era
  // erro do teste: com N=5 o passo real e 2.783 m. Confere-se a FORMULA, e
  // separadamente o valor com o N de verdade.
  float esperado = (float)(111320.0 / (dp.porGrau * (dp.n - 1)));
  ok("passo norte-sul segue a formula", fabsf(my - esperado) < 1.0f);
  ok("passo leste-oeste encurta com a latitude", mx < my);
  // com o bloco real (121x121, 10 por grau) tem de dar os ~92,8 m do SRTM3
  DemPack real = dp; real.n = 121;
  float rx, ry;
  demPassoMetros(real, -23.95, rx, ry);
  ok("com 121x121 o passo e o do SRTM de 90 m", ry > 88.0f && ry < 98.0f);

  // terreno plano: o sombreado tem de dar sin(45) = 0,707 -> ~180
  uint8_t planoBloco[512];
  memset(planoBloco, 0, sizeof(planoBloco));
  for (uint32_t i = 0; i < D_N * D_N; i++) pw16(planoBloco + i * 2, 500);
  uint8_t s = demSombra(dp, planoBloco, 2, 2, mx, my);
  ok("plano da a luz de referencia", s > 170 && s < 190);
  uint8_t sr = demSombra(dp, bloco, 2, 2, mx, my);
  ok("rampa sombreia diferente do plano", sr != s);

  okEq("intervalo de curva e um numero redondo", demIntervaloCurva(2.0) % 10, 0);
  ok("zoom mais aberto usa intervalo maior",
     demIntervaloCurva(12.0) >= demIntervaloCurva(2.0));

  float t = 0;
  ok("aresta que cruza a curva", demCruza(90, 110, 100, t));
  ok("fracao do cruzamento", fabsf(t - 0.5f) < 0.01f);
  ok("aresta que nao cruza", !demCruza(90, 95, 100, t));
  ok("aresta plana nao cruza", !demCruza(100, 100, 100, t));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n=== MTS | auto-teste de vec_pack.h e dem_pack.h ===");

  g_img = (uint8_t*)malloc(512 * 16);
  if (!g_img) { Serial.println("sem memoria"); return; }

  testaVec();
  testaDem();

  Serial.printf("\n%d PASS, %d FALHA\n", passou, falhou);
  Serial.println(falhou ? "TEM COISA ERRADA." : "tudo certo.");
  free(g_img);
}

void loop() { delay(1000); }
