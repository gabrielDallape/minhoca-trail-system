/*
 * AUTO-TESTE do tile_pack.h. Roda em QUALQUER ESP32 - sem cartao SD, sem radio,
 * sem GPS, sem fiacao. Grave numa das telas e leia o serial.
 *
 * Como testa sem cartao: monta um container COMPLETO na RAM (tiles de 16x16 em
 * vez de 256x256, para caber) e passa um "reader" que le desse buffer em vez do
 * SD. O parser exercitado e exatamente o mesmo que vai ler o cartao.
 *
 * Confere: CRC32 contra vetor canonico, cabecalho, calculo de slot, leitura de
 * tile, e os caminhos de ERRO (magic invalido, versao, tile ausente, CRC ruim,
 * falha de I/O) - que sao os que decidem se a tela mostra tile cinza ou lixo.
 *
 *   .\tools\build.ps1 firmware\tile_selftest -Upload -Port COMx
 */
#include "../tile_pack.h"

int gPass = 0, gFail = 0;
void check(bool ok, const char* what){
  if (ok) { gPass++; Serial.printf("  PASS  %s\n", what); }
  else    { gFail++; Serial.printf("  FAIL  %s   <-------\n", what); }
}
void checkEqU(uint32_t got, uint32_t want, const char* what){
  if (got == want) { gPass++; Serial.printf("  PASS  %s (=%lu)\n", what, (unsigned long)got); }
  else { gFail++; Serial.printf("  FAIL  %s: got=%lu want=%lu   <-------\n", what,
                                (unsigned long)got, (unsigned long)want); }
}

// ---------------------------------------------------------- container em RAM
// tilePx=16 -> 16*16*2 = 512 bytes = 1 setor exato por tile.
#define T_PX      16
#define T_SECS    1
#define T_BYTES   (T_PX * T_PX * 2)
#define T_W       2
#define T_H       2
#define T_ZOOM    15
#define T_XMIN    12136UL
#define T_YMIN    18589UL
#define N_SECTORS (2 + T_W * T_H)          // setor0 + indice + 4 tiles
uint8_t fakeDisk[N_SECTORS * TILEPACK_SECTOR];
bool    failIO = false;                     // p/ testar o caminho de erro de leitura

static void put16(uint8_t* p, uint16_t v){ p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v){
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

// reader que le do buffer em RAM (no lugar do SD)
bool ramReader(uint32_t startSector, uint32_t count, void* dst, void* user){
  (void)user;
  if (failIO) return false;
  if (startSector + count > N_SECTORS) return false;
  memcpy(dst, fakeDisk + (size_t)startSector * TILEPACK_SECTOR, (size_t)count * TILEPACK_SECTOR);
  return true;
}

// monta o container igual ao que o tiles.py grava
void buildFakeDisk(bool corruptTile3, bool absentTile2){
  memset(fakeDisk, 0, sizeof(fakeDisk));

  uint8_t* h = fakeDisk;
  memcpy(h, "TRILHAMP", 8);
  put16(h + 8, 1);              // version
  put16(h + 10, T_PX);          // tilePx
  h[12] = TILEPACK_PIXFMT_RGB565;
  h[13] = 1;                    // nLevels
  put32(h + 16, 1);             // indexSector
  put32(h + 20, 2);             // dataSector
  put32(h + 24, T_SECS);        // tileSectors
  put32(h + 28, T_W * T_H);     // nTiles
  uint8_t* L = h + TILEPACK_HDR_SIZE;
  L[0] = T_ZOOM;
  put32(L + 4, T_XMIN);
  put32(L + 8, T_YMIN);
  put16(L + 12, T_W);
  put16(L + 14, T_H);
  put32(L + 16, 0);             // indexOffset

  // tiles + indice
  uint8_t* idx = fakeDisk + 1 * TILEPACK_SECTOR;
  for (int slot = 0; slot < T_W * T_H; slot++) {
    uint32_t sec = 2 + slot;
    uint8_t* tile = fakeDisk + (size_t)sec * TILEPACK_SECTOR;
    for (int i = 0; i < T_BYTES; i++) tile[i] = (uint8_t)((i * 7 + slot * 31) & 0xFF);
    uint32_t crc = tilePackCrc32(tile, T_BYTES);
    if (absentTile2 && slot == 2) { put32(idx + slot * 8, 0); put32(idx + slot * 8 + 4, 0); continue; }
    put32(idx + slot * 8, sec);
    put32(idx + slot * 8 + 4, crc);
    if (corruptTile3 && slot == 3) tile[100] ^= 0xFF;   // grava o CRC certo e depois estraga
  }
}

// ---------------------------------------------------------------------- casos
void testCrc(){
  Serial.println("\n[1] CRC32 compativel com zlib (o gravador usa zlib)");
  const char* s = "123456789";
  checkEqU(tilePackCrc32(s, 9), 0xCBF43926UL, "vetor canonico 123456789");
  checkEqU(tilePackCrc32("", 0), 0UL, "string vazia = 0");
  uint8_t z[512]; memset(z, 0, sizeof(z));
  checkEqU(tilePackCrc32(z, 512), 0xB2AA7578UL, "512 bytes zerados");
  // incremental == de uma vez
  uint8_t d[64]; for (int i = 0; i < 64; i++) d[i] = i * 3;
  uint32_t whole = tilePackCrc32(d, 64);
  uint32_t part = tilePackCrc32(d + 32, 32, tilePackCrc32(d, 32));
  checkEqU(part, whole, "CRC incremental == de uma vez");
}

void testOpen(){
  Serial.println("\n[2] abertura e cabecalho");
  buildFakeDisk(false, false);
  TilePack tp;
  check(tilePackOpen(tp, ramReader, NULL), "abriu o container");
  check(tp.ok, "flag ok");
  checkEqU(tp.tilePx, T_PX, "tilePx");
  checkEqU(tp.nLevels, 1, "nLevels");
  checkEqU(tp.indexSector, 1, "indexSector");
  checkEqU(tp.dataSector, 2, "dataSector");
  checkEqU(tp.tileSectors, T_SECS, "tileSectors");
  checkEqU(tilePackTileBytes(tp), T_BYTES, "bytes por tile");
  checkEqU(tp.levels[0].xmin, T_XMIN, "xmin do nivel");
  checkEqU(tp.levels[0].w, T_W, "largura do nivel");
  checkEqU((uint32_t)tilePackLevelIdx(tp, T_ZOOM), 0UL, "achou o zoom 15");
  check(tilePackLevelIdx(tp, 12) < 0, "zoom 12 nao existe -> -1");
  checkEqU((uint32_t)tilePackNearestZoom(tp, 9), 0UL, "zoom mais proximo cai no unico nivel");
}

void testRejects(){
  Serial.println("\n[3] rejeita container invalido (nao trava, nao le lixo)");
  TilePack tp;

  buildFakeDisk(false, false);
  fakeDisk[3] = 'X';                       // quebra o magic
  check(!tilePackOpen(tp, ramReader, NULL), "magic invalido recusado");
  check(!tp.ok, "flag ok fica falsa");

  buildFakeDisk(false, false);
  put16(fakeDisk + 8, 99);                 // versao futura
  check(!tilePackOpen(tp, ramReader, NULL), "versao desconhecida recusada");

  buildFakeDisk(false, false);
  fakeDisk[12] = 7;                        // formato de pixel estranho
  check(!tilePackOpen(tp, ramReader, NULL), "pixfmt desconhecido recusado");

  buildFakeDisk(false, false);
  fakeDisk[13] = 0;                        // zero niveis
  check(!tilePackOpen(tp, ramReader, NULL), "nLevels=0 recusado");

  buildFakeDisk(false, false);
  fakeDisk[13] = TILEPACK_MAX_LEVELS + 1;  // niveis demais
  check(!tilePackOpen(tp, ramReader, NULL), "nLevels acima do maximo recusado");

  buildFakeDisk(false, false);
  put32(fakeDisk + 24, 0);                 // tileSectors=0 levaria a divisao/leitura zero
  check(!tilePackOpen(tp, ramReader, NULL), "tileSectors=0 recusado");

  buildFakeDisk(false, false);
  failIO = true;
  check(!tilePackOpen(tp, ramReader, NULL), "falha de I/O no setor 0 recusada");
  failIO = false;

  check(!tilePackOpen(tp, NULL, NULL), "reader nulo recusado");
}

void testSlots(){
  Serial.println("\n[4] calculo de slot e limites da grade");
  buildFakeDisk(false, false);
  TilePack tp; tilePackOpen(tp, ramReader, NULL);

  checkEqU((uint32_t)tilePackSlot(tp, T_ZOOM, T_XMIN, T_YMIN), 0UL, "canto NO = slot 0");
  checkEqU((uint32_t)tilePackSlot(tp, T_ZOOM, T_XMIN + 1, T_YMIN), 1UL, "x+1 = slot 1");
  checkEqU((uint32_t)tilePackSlot(tp, T_ZOOM, T_XMIN, T_YMIN + 1), 2UL, "y+1 = slot 2 (row-major)");
  checkEqU((uint32_t)tilePackSlot(tp, T_ZOOM, T_XMIN + 1, T_YMIN + 1), 3UL, "canto SE = slot 3");

  check(tilePackSlot(tp, T_ZOOM, T_XMIN - 1, T_YMIN) < 0, "x abaixo do minimo -> -1");
  check(tilePackSlot(tp, T_ZOOM, T_XMIN + T_W, T_YMIN) < 0, "x acima do maximo -> -1");
  check(tilePackSlot(tp, T_ZOOM, T_XMIN, T_YMIN - 1) < 0, "y abaixo do minimo -> -1");
  check(tilePackSlot(tp, T_ZOOM, T_XMIN, T_YMIN + T_H) < 0, "y acima do maximo -> -1");
  check(tilePackSlot(tp, 9, T_XMIN, T_YMIN) < 0, "zoom inexistente -> -1");
  check(tilePackSlot(tp, T_ZOOM, 0, 0) < 0, "x=0,y=0 fora da grade -> -1 (nao estoura)");
}

void testRead(){
  Serial.println("\n[5] leitura de tile e os quatro resultados possiveis");
  static uint8_t buf[T_BYTES];

  buildFakeDisk(false, false);
  TilePack tp; tilePackOpen(tp, ramReader, NULL);

  check(tilePackReadTile(tp, T_ZOOM, T_XMIN, T_YMIN, buf, true) == TILEPACK_OK, "tile 0 com CRC OK");
  // conteudo tem de ser o padrao gravado (slot 0: i*7)
  bool same = true;
  for (int i = 0; i < T_BYTES; i++) if (buf[i] != (uint8_t)((i * 7) & 0xFF)) { same = false; break; }
  check(same, "conteudo do tile confere byte a byte");
  check(tilePackReadTile(tp, T_ZOOM, T_XMIN + 1, T_YMIN + 1, buf, true) == TILEPACK_OK, "tile 3 com CRC OK");
  checkEqU(tp.crcErrors, 0UL, "nenhum erro de CRC ate aqui");

  // fora da grade -> ABSENT (pinta fundo, nao e erro)
  check(tilePackReadTile(tp, T_ZOOM, T_XMIN + 99, T_YMIN, buf, true) == TILEPACK_ABSENT, "fora da grade -> ABSENT");

  // slot marcado como ausente no indice (agua / area sem dados)
  buildFakeDisk(false, true);
  tilePackOpen(tp, ramReader, NULL);
  check(tilePackReadTile(tp, T_ZOOM, T_XMIN, T_YMIN + 1, buf, true) == TILEPACK_ABSENT, "setor 0 no indice -> ABSENT");

  // tile corrompido -> BADCRC (pinta cinza; JAMAIS desenhar o lixo)
  buildFakeDisk(true, false);
  tilePackOpen(tp, ramReader, NULL);
  check(tilePackReadTile(tp, T_ZOOM, T_XMIN + 1, T_YMIN + 1, buf, true) == TILEPACK_BADCRC, "tile corrompido -> BADCRC");
  checkEqU(tp.crcErrors, 1UL, "contador de erros de CRC subiu");
  // sem checar CRC o mesmo tile "passa": e por isso que o CRC importa
  check(tilePackReadTile(tp, T_ZOOM, T_XMIN + 1, T_YMIN + 1, buf, false) == TILEPACK_OK,
        "sem checar CRC o tile corrompido passa (mostra por que checar)");

  // erro de cartao -> IOERROR (cair para o mapa de fallback, nao travar)
  buildFakeDisk(false, false);
  tilePackOpen(tp, ramReader, NULL);
  failIO = true;
  check(tilePackReadTile(tp, T_ZOOM, T_XMIN, T_YMIN, buf, true) == TILEPACK_IOERROR, "falha de leitura -> IOERROR");
  check(tp.readErrors > 0, "contador de erros de leitura subiu");
  failIO = false;
}

void testGeo(){
  Serial.println("\n[6] Mercator: mesmos numeros do tiles.py");
  uint32_t x, y;
  // valores conferidos contra o tools/tiles/tiles.py para -23.5505,-46.6333
  tilePackDegToTile(-23.5505, -46.6333, 15, x, y);
  checkEqU(x, 12139UL, "z15 x de Sao Paulo");
  checkEqU(y, 18590UL, "z15 y de Sao Paulo");
  tilePackDegToTile(-23.5505, -46.6333, 16, x, y);
  checkEqU(x, 24278UL, "z16 x");
  checkEqU(y, 37181UL, "z16 y");
  tilePackDegToTile(-23.5505, -46.6333, 13, x, y);
  checkEqU(x, 3034UL, "z13 x");
  checkEqU(y, 4647UL, "z13 y");

  // y cresce para o sul (latitude negativa no Brasil)
  uint32_t x1, y1, x2, y2;
  tilePackDegToTile(-23.50, -46.65, 15, x1, y1);
  tilePackDegToTile(-23.60, -46.65, 15, x2, y2);
  check(y2 > y1, "y cresce para o sul");

  // pixel dentro do tile
  uint16_t px, py;
  tilePackDegToPixel(-23.5505, -46.6333, 15, 256, x, y, px, py);
  check(px < 256 && py < 256, "pixel dentro dos limites do tile");
  Serial.printf("        tile z15 x=%lu y=%lu pixel=(%u,%u)\n",
                (unsigned long)x, (unsigned long)y, px, py);

  // escala: em z15 na latitude de SP o pixel vale ~4,4 m
  double mpp = tilePackMetersPerPixel(-23.55, 15, 256);
  Serial.printf("        z15: %.2f m/pixel | z16: %.2f | z13: %.2f\n",
                mpp, tilePackMetersPerPixel(-23.55, 16, 256),
                tilePackMetersPerPixel(-23.55, 13, 256));
  check(mpp > 3.5 && mpp < 5.5, "m/pixel em z15 na faixa esperada");
  check(tilePackMetersPerPixel(-23.55, 16, 256) < mpp, "zoom maior = menos metros por pixel");
}

void setup(){
  Serial.begin(115200);
  delay(600);
  Serial.println("\n=========================================");
  Serial.println(" AUTO-TESTE tile_pack.h (sem SD/radio/GPS)");
  Serial.println("=========================================");
  Serial.printf("container de teste na RAM: %d setores (%d bytes)\n",
                N_SECTORS, (int)sizeof(fakeDisk));

  testCrc();
  testOpen();
  testRejects();
  testSlots();
  testRead();
  testGeo();

  Serial.printf("\n=========================================\n");
  Serial.printf(" RESULTADO: %d PASS, %d FAIL\n", gPass, gFail);
  Serial.println(gFail == 0 ? " TUDO OK" : " HA FALHAS - ver linhas com <-------");
  Serial.println("=========================================");
}

void loop(){ delay(1000); }
