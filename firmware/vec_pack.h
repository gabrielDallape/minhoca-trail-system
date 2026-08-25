/*
 * ============================================================================
 *  VEC PACK - leitor do mapa vetorial gerado por tools/mapa/vetor.py
 *
 *  Le vias, agua, mata e POI direto por SETOR, SEM filesystem, pelo mesmo motivo
 *  do tile_pack.h: o aparelho perde energia sem aviso (chave do carro), e sem FAT
 *  nao existe tabela de alocacao para corromper. O CRC32 por setor torna a
 *  degradacao do cartao DETECTAVEL em vez de virar lixo na tela.
 *
 *  POR QUE VETOR E NAO IMAGEM
 *  MEDIDO: o Brasil em tiles RGB565 na resolucao que esta tela usa daria 55
 *  milhoes de tiles, 7 TB. O cartao tem 29 GB. Em vetor da ~1,4 GB, porque
 *  imagem guarda todos os pixels - inclusive os vazios - e o Brasil e quase todo
 *  vazio. De quebra, o mesmo dado serve todos os zooms.
 *
 *  ORCAMENTO DE LEITURA, medido neste cartao (1,8 MB/s, 12,4 ms de busca):
 *      nivel     km/setor   p50     p99    pior
 *      detalhe      4,9    13 ms   18 ms   80 ms
 *      medio       19,6    14 ms   37 ms  248 ms
 *      geral       39,1    15 ms   37 ms  135 ms
 *  Metade dos setores le em 13 ms, dos quais 12,4 sao a BUSCA - ou seja, o dado
 *  praticamente nao custa. Por isso o setor foi dimensionado pela cobertura da
 *  tela e nao "pelo maior possivel": a primeira versao usava setores de 19,6 km no
 *  nivel de detalhe e o pior caso era 837 ms - uma travada visivel ao entrar em
 *  Brasilia.
 *
 *  Layout (identico ao de vetor.py - se mudar um lado, mude o outro):
 *    setor 0            cabecalho + tabela de niveis
 *    setor indexSector  indice: por slot {setor u32, bytes u32, crc32 u32}
 *    setor dataSector.. blocos, cada um alinhado em setor de 512 B
 *
 *    slot = indexOffset[nivel] + (y - ymin) * w + (x - xmin)
 * ============================================================================
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#define VECPACK_SECTOR      512
#define VECPACK_MAX_LEVELS  8
#define VECPACK_HDR_SIZE    32
#define VECPACK_LVL_SIZE    20
#define VECPACK_IDX_SIZE    12

// Classes. O numero esta gravado no cartao: NUNCA renumerar, so acrescentar.
enum VecClasse : uint8_t {
  VEC_ESTRADA = 1,   // asfalto
  VEC_TRILHA  = 2,   // track/path/unclassified - o que se anda em off road
  VEC_RUA     = 3,
  VEC_PEDESTRE= 4,
  VEC_RIO     = 5,
  VEC_CORREGO = 6,
  VEC_LAGO    = 7,   // area
  VEC_MATA    = 8,   // area
  VEC_USO     = 9,   // area
  VEC_PROTEGIDA=10,  // area
  VEC_FERROVIA=11,
  VEC_PISTA   = 12,
  VEC_LIMITE  = 13,
  VEC_POI     = 14,
};

// POI. Os quatro ultimos sao os que so existem em mapa de trilha e sao o motivo
// de nao usar mapa de rua: porteira, cancela, mata-burro e VAU DE RIO.
enum VecPoi : uint8_t {
  VECP_COMBUSTIVEL=1, VECP_AGUA=2, VECP_HOSPITAL=3, VECP_FARMACIA=4,
  VECP_COMIDA=5, VECP_POUSADA=6, VECP_CAMPING=7, VECP_MECANICO=8,
  VECP_BORRACHARIA=9, VECP_MERCADO=10, VECP_POLICIA=11, VECP_ABRIGO=12,
  VECP_MIRANTE=13, VECP_PICO=14, VECP_CACHOEIRA=15, VECP_NASCENTE=16,
  VECP_CAVERNA=17, VECP_PORTEIRA=18, VECP_CANCELA=19, VECP_MATA_BURRO=20,
  VECP_VAU=21, VECP_BLOQUEIO=22, VECP_TORRE=23, VECP_POCO=24,
};

typedef bool (*VecPackReader)(uint32_t startSector, uint32_t count, void* dst, void* user);

struct VecLevel {
  uint8_t  zoom;
  uint16_t tolMetros;      // simplificacao aplicada na geracao; 0 = pacote antigo
  uint32_t xmin, ymin;
  uint16_t w, h;
  uint32_t indexOffset;
};

struct VecPack {
  VecPackReader read;
  void*    user;
  bool     ok;
  uint16_t version;
  uint8_t  nLevels;
  uint32_t indexSector, dataSector, nSlots, nDataSectors;
  // Tamanho do MAIOR bloco do pacote. E com isto que se dimensiona o buffer de
  // leitura: sem ele o firmware chuta, e chutar deu errado - a primeira versao
  // usou 448 KB contra 615 KB reais, e os 5 setores maiores do Brasil (as
  // maiores cidades) devolviam TOOBIG e nao eram desenhados. 0 = pacote antigo.
  uint32_t maiorBloco;
  VecLevel levels[VECPACK_MAX_LEVELS];
  uint8_t  sectorBuf[VECPACK_SECTOR];
  uint32_t crcErrors, readErrors, truncated;
};

// ------------------------------------------------------- leitura little-endian
static inline uint16_t vpLE16(const uint8_t* p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t vpLE32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// CRC32 igual ao do tile_pack.h (e ao zlib.crc32 do Python). Tabela de nibble:
// 64 bytes de rodata em vez de 1 KB.
static const uint32_t VP_CRC_NIB[16] = {
  0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
  0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
  0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
  0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};
inline uint32_t vecPackCrc32(const void* data, uint32_t len, uint32_t crc = 0){
  const uint8_t* p = (const uint8_t*)data;
  crc = ~crc;
  while (len--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ VP_CRC_NIB[crc & 0x0F];
    crc = (crc >> 4) ^ VP_CRC_NIB[crc & 0x0F];
  }
  return ~crc;
}

// --------------------------------------------------------------------- open
inline bool vecPackOpen(VecPack& vp, VecPackReader reader, void* user){
  memset(&vp, 0, sizeof(vp));
  vp.read = reader; vp.user = user; vp.ok = false;
  if (!reader) return false;
  if (!reader(0, 1, vp.sectorBuf, user)) { vp.readErrors++; return false; }

  const uint8_t* h = vp.sectorBuf;
  if (memcmp(h, "MTSVECT1", 8) != 0) return false;

  vp.version      = vpLE16(h + 8);
  vp.nLevels      = h[10];
  vp.indexSector  = vpLE32(h + 12);
  vp.dataSector   = vpLE32(h + 16);
  vp.nSlots       = vpLE32(h + 20);
  vp.nDataSectors = vpLE32(h + 24);
  vp.maiorBloco   = vpLE32(h + 28);

  if (vp.version != 1) return false;
  if (vp.nLevels == 0 || vp.nLevels > VECPACK_MAX_LEVELS) return false;
  if (vp.indexSector == 0 || vp.dataSector <= vp.indexSector) return false;
  if ((uint32_t)VECPACK_HDR_SIZE + (uint32_t)vp.nLevels * VECPACK_LVL_SIZE > VECPACK_SECTOR)
    return false;

  for (uint8_t i = 0; i < vp.nLevels; i++) {
    const uint8_t* L = h + VECPACK_HDR_SIZE + (uint32_t)i * VECPACK_LVL_SIZE;
    vp.levels[i].zoom        = L[0];
    vp.levels[i].tolMetros   = vpLE16(L + 2);
    vp.levels[i].xmin        = vpLE32(L + 4);
    vp.levels[i].ymin        = vpLE32(L + 8);
    vp.levels[i].w           = vpLE16(L + 12);
    vp.levels[i].h           = vpLE16(L + 14);
    vp.levels[i].indexOffset = vpLE32(L + 16);
  }
  vp.ok = true;
  return true;
}

// ----------------------------------------------------- escolha do nivel
// Largura de um setor no chao, em metros, na latitude dada.
inline double vecPackSectorMeters(const VecPack& vp, int li, double lat){
  return 156543.03392 * 256.0 * cos(lat * 0.017453292519943295)
       / (double)(1UL << vp.levels[li].zoom);
}

// Quantos setores a tela pega neste nivel. O +2 e o pior caso: a tela pode estar
// atravessada na divisa em cada eixo.
inline int vecPackSectorCount(const VecPack& vp, int li, double mPorPx,
                              int telaW, int telaH, double lat){
  double m = vecPackSectorMeters(vp, li, lat);
  int nx = (int)((double)telaW * mPorPx / m) + 2;
  int ny = (int)((double)telaH * mPorPx / m) + 2;
  return nx * ny;
}

// Escolhe o nivel pela escala em uso.
//
// A REGRA E A SIMPLIFICACAO, NAO O TAMANHO DO SETOR. Cada nivel foi gerado com
// uma tolerancia de simplificacao (1 m, 8 m, 24 m): usar um nivel cuja tolerancia
// passa de ~1,5 pixel mostra uma linha visivelmente "cortando canto", e usar um
// nivel detalhado demais faz desenhar milhares de vertices que caem no mesmo
// pixel - o que na cidade e a diferenca entre 200 e 100 mil pontos por quadro.
//
// A primeira versao decidia pela largura do setor ("tem de ser o dobro da tela")
// e ERRAVA: a 2 m/px o nivel de detalhe precisaria de 5.120 m e tem 4.892, entao
// o aparelho caia calado para o mapa grosseiro justamente no zoom para o qual o
// nivel foi feito. O auto-teste na tela pegou isso.
//
// tolMetros = 0 significa pacote gerado antes deste campo existir; ai vale o
// criterio antigo, mas pelo NUMERO DE SETORES, que e o custo real de leitura.
inline int vecPackLevelForScale(const VecPack& vp, double mPorPx, int telaW,
                                int telaH, double lat, int orcamentoSetores = 6){
  const double MAX_PX = 1.5;
  int escolha = -1;
  for (int i = 0; i < vp.nLevels; i++) {
    if (vp.levels[i].tolMetros == 0) continue;
    if ((double)vp.levels[i].tolMetros > mPorPx * MAX_PX) continue;  // grosseiro demais
    escolha = i;                       // serve; segue procurando um mais geral
    // para de subir quando a leitura ainda cabe no orcamento
    if (vecPackSectorCount(vp, i, mPorPx, telaW, telaH, lat) <= orcamentoSetores) break;
  }
  if (escolha >= 0) return escolha;

  // nenhum nivel e fino o bastante (zoom muito fechado): usa o mais detalhado.
  // Nunca devolve "nada": ficar sem mapa e pior que mapa simplificado.
  if (vp.levels[0].tolMetros != 0) return 0;

  for (int i = 0; i < vp.nLevels; i++) {
    escolha = i;
    if (vecPackSectorCount(vp, i, mPorPx, telaW, telaH, lat) <= orcamentoSetores) break;
  }
  return escolha < 0 ? 0 : escolha;
}

// ------------------------------------------------------------- endereco
inline int32_t vecPackSlot(const VecPack& vp, int li, uint32_t x, uint32_t y){
  if (li < 0 || li >= vp.nLevels) return -1;
  const VecLevel& L = vp.levels[li];
  if (x < L.xmin || y < L.ymin) return -1;
  uint32_t dx = x - L.xmin, dy = y - L.ymin;
  if (dx >= L.w || dy >= L.h) return -1;
  return (int32_t)(L.indexOffset + dy * (uint32_t)L.w + dx);
}

inline bool vecPackEntry(VecPack& vp, int32_t slot,
                         uint32_t& sector, uint32_t& len, uint32_t& crc){
  if (!vp.ok || slot < 0 || (uint32_t)slot >= vp.nSlots) return false;
  uint32_t byteOff = (uint32_t)slot * VECPACK_IDX_SIZE;
  uint32_t sec = vp.indexSector + byteOff / VECPACK_SECTOR;
  uint32_t off = byteOff % VECPACK_SECTOR;
  // Um registro de 12 B nunca cruza a borda de 512 B? Cruza sim: 512/12 nao e
  // inteiro. Por isso le DOIS setores quando o registro comeca perto do fim.
  if (off + VECPACK_IDX_SIZE <= VECPACK_SECTOR) {
    if (!vp.read(sec, 1, vp.sectorBuf, vp.user)) { vp.readErrors++; return false; }
    sector = vpLE32(vp.sectorBuf + off);
    len    = vpLE32(vp.sectorBuf + off + 4);
    crc    = vpLE32(vp.sectorBuf + off + 8);
  } else {
    uint8_t junta[VECPACK_IDX_SIZE];
    uint32_t primeiro = VECPACK_SECTOR - off;
    if (!vp.read(sec, 1, vp.sectorBuf, vp.user)) { vp.readErrors++; return false; }
    memcpy(junta, vp.sectorBuf + off, primeiro);
    if (!vp.read(sec + 1, 1, vp.sectorBuf, vp.user)) { vp.readErrors++; return false; }
    memcpy(junta + primeiro, vp.sectorBuf, VECPACK_IDX_SIZE - primeiro);
    sector = vpLE32(junta);
    len    = vpLE32(junta + 4);
    crc    = vpLE32(junta + 8);
  }
  return true;
}

enum VecPackResult : uint8_t {
  VECPACK_OK = 0,
  VECPACK_ABSENT,     // nao ha dado nesse setor (mar, fora do pais) -> so o fundo
  VECPACK_IOERROR,
  VECPACK_BADCRC,     // cartao degradando -> NAO desenhar, o lixo vira rabisco
  VECPACK_TOOBIG      // o buffer do chamador nao cabe o setor
};

// dst deve estar na PSRAM. Dimensione por vecPackMaxBlock() ou, na pratica, 256 KB.
inline VecPackResult vecPackReadBlock(VecPack& vp, int li, uint32_t x, uint32_t y,
                                      void* dst, uint32_t dstCap, uint32_t& outLen,
                                      bool checkCrc){
  outLen = 0;
  if (!vp.ok) return VECPACK_IOERROR;
  int32_t slot = vecPackSlot(vp, li, x, y);
  if (slot < 0) return VECPACK_ABSENT;

  uint32_t sector = 0, len = 0, crc = 0;
  if (!vecPackEntry(vp, slot, sector, len, crc)) return VECPACK_IOERROR;
  if (sector == 0 || len == 0) return VECPACK_ABSENT;

  // A leitura e por SETOR, entao grava nsec*512 bytes - sempre >= len. Conferir
  // so o len deixaria o ultimo setor escrever ate 511 bytes fora do buffer, que
  // e o tipo de estouro que corrompe outra coisa e so aparece muito depois.
  uint32_t nsec = (len + VECPACK_SECTOR - 1) / VECPACK_SECTOR;
  if (nsec * VECPACK_SECTOR > dstCap) { vp.truncated++; return VECPACK_TOOBIG; }
  if (!vp.read(sector, nsec, dst, vp.user)) { vp.readErrors++; return VECPACK_IOERROR; }

  if (checkCrc && vecPackCrc32(dst, len) != crc) { vp.crcErrors++; return VECPACK_BADCRC; }
  outLen = len;
  return VECPACK_OK;
}

// ============================================================ percurso
// Anda pelo bloco sem copiar nada: os ponteiros apontam PARA DENTRO do buffer
// lido. Enquanto o cursor estiver em uso, nao releia por cima do buffer.
struct VecCursor {
  const uint8_t* p;
  const uint8_t* fim;
  uint16_t camadas;      // camadas que faltam
  uint16_t feicoes;      // feicoes que faltam na camada atual
  uint8_t  classe;
};

inline bool vecCursorInit(VecCursor& c, const void* bloco, uint32_t len){
  c.p = (const uint8_t*)bloco;
  c.fim = c.p + len;
  c.camadas = c.feicoes = 0;
  c.classe = 0;
  if (len < 2) return false;
  c.camadas = vpLE16(c.p); c.p += 2;
  return true;
}

// Avanca para a proxima camada. Devolve false quando acabou o bloco.
inline bool vecCursorLayer(VecCursor& c){
  // pula o que sobrou da camada atual sem interpretar
  while (c.feicoes) {
    if (c.p + 2 > c.fim) { c.feicoes = 0; c.camadas = 0; return false; }
    if (c.classe == VEC_POI) {
      if (c.p + 6 > c.fim) return false;
      uint8_t ln = c.p[5];
      c.p += 6 + ln;
    } else {
      uint16_t n = vpLE16(c.p);
      c.p += 2 + (uint32_t)n * 4;
    }
    c.feicoes--;
  }
  if (!c.camadas || c.p + 3 > c.fim) return false;
  c.classe  = c.p[0];
  c.feicoes = vpLE16(c.p + 1);
  c.p += 3;
  c.camadas--;
  return true;
}

// Proxima linha da camada. pts aponta para pares u16 (x,y) locais do setor.
inline bool vecCursorLine(VecCursor& c, const uint8_t*& pts, uint16_t& n){
  if (!c.feicoes || c.classe == VEC_POI) return false;
  if (c.p + 2 > c.fim) { c.feicoes = 0; return false; }
  n = vpLE16(c.p);
  c.p += 2;
  if (c.p + (uint32_t)n * 4 > c.fim) { c.feicoes = 0; return false; }
  pts = c.p;
  c.p += (uint32_t)n * 4;
  c.feicoes--;
  return true;
}

// Proximo POI. nome NAO tem terminador: use nomeLen.
inline bool vecCursorPoi(VecCursor& c, uint16_t& x, uint16_t& y, uint8_t& sub,
                         const char*& nome, uint8_t& nomeLen){
  if (!c.feicoes || c.classe != VEC_POI) return false;
  if (c.p + 6 > c.fim) { c.feicoes = 0; return false; }
  x = vpLE16(c.p); y = vpLE16(c.p + 2); sub = c.p[4]; nomeLen = c.p[5];
  c.p += 6;
  if (c.p + nomeLen > c.fim) { c.feicoes = 0; return false; }
  nome = (const char*)c.p;
  c.p += nomeLen;
  c.feicoes--;
  return true;
}

// ============================================================ geometria
// Web Mercator, igual ao tilePackDegToTile e ao deg2num do vetor.py. A copia e
// LITERAL de proposito: se as tres divergirem, o mapa desenha deslocado e nada
// acusa.
inline void vecPackDegToTileF(double lat, double lon, uint8_t z, double& fx, double& fy){
  double n = (double)(1UL << z);
  if (lat >  85.05112878) lat =  85.05112878;
  if (lat < -85.05112878) lat = -85.05112878;
  fx = (lon + 180.0) / 360.0 * n;
  double s = sin(lat * 0.017453292519943295);
  fy = (1.0 - log((1.0 + s) / (1.0 - s)) / (2.0 * 3.14159265358979323846)) / 2.0 * n;
}

// Quais setores cobrem a tela. Devolve quantos coube em x0..x1 / y0..y1.
// Usa a MEIA-DIAGONAL nos dois eixos: com o mapa orientado pelo rumo a tela
// gira sobre o mundo, e o retangulo alinhado que a cobre em qualquer angulo e o
// da diagonal. Custa ~1 setor a mais de leitura no pior caso; errar para menos
// custaria um canto de mapa vazio que aparece e some conforme o carro vira.
inline void vecPackCoverage(const VecPack& vp, int li, double lat, double lon,
                            double mPorPx, int telaW, int telaH,
                            uint32_t& x0, uint32_t& y0, uint32_t& x1, uint32_t& y1){
  double fx, fy;
  vecPackDegToTileF(lat, lon, vp.levels[li].zoom, fx, fy);
  double metrosSetor = vecPackSectorMeters(vp, li, lat);
  double meia = 0.5 * sqrt((double)telaW * telaW + (double)telaH * telaH)
              * mPorPx / metrosSetor;
  double ax = fx - meia, bx = fx + meia;
  double ay = fy - meia, by = fy + meia;
  x0 = (uint32_t)(ax < 0 ? 0 : ax);
  y0 = (uint32_t)(ay < 0 ? 0 : ay);
  x1 = (uint32_t)(bx < 0 ? 0 : bx);
  y1 = (uint32_t)(by < 0 ? 0 : by);
}

// Transformacao setor -> tela, calculada UMA vez por setor.
//
// Sem isto seria preciso um Mercator inverso (exp + atan) por ponto. Com isto
// cada ponto custa QUATRO multiplicacoes e duas somas, porque dentro de um setor
// a relacao entre coordenada local e pixel e exatamente linear - o dado ja esta
// em Mercator, que e a mesma projecao da tela.
//
// Virou MATRIZ 2x2 (era um par de escalas) por causa do mapa orientado pelo
// rumo: rotacao e uma transformacao linear como a escala, entao o custo por
// ponto so ganha duas multiplicacoes - e o descarte por janela continua em
// inteiro, so que a janela agora sai da INVERSA da matriz (bbox da tela girada
// em coordenada local).
struct VecXform { float ox, oy, a, b, c, d; };   // tela = (ox,oy) + [a b; c d]*(lx,ly)

inline void vecXformInit(VecXform& xf, const VecPack& vp, int li,
                         uint32_t tileX, uint32_t tileY,
                         double lat, double lon, double mPorPx,
                         int telaW, int telaH,
                         float rotSen = 0.0f, float rotCos = 1.0f){
  uint8_t z = vp.levels[li].zoom;
  double fx, fy;
  vecPackDegToTileF(lat, lon, z, fx, fy);
  // pixels por unidade de setor
  double k = vecPackSectorMeters(vp, li, lat) / mPorPx;
  double s = k / 65536.0;
  // No plano do tile o norte e -y; girar o rumo para CIMA da tela e:
  //   X = cx + k*( u*cos + v*sen )      u = leste (x do tile)
  //   Y = cy + k*(-u*sen + v*cos )      v = sul   (y do tile)
  double u0 = (double)tileX - fx, v0 = (double)tileY - fy;
  xf.a  = (float)( s * rotCos);
  xf.b  = (float)( s * rotSen);
  xf.c  = (float)(-s * rotSen);
  xf.d  = (float)( s * rotCos);
  xf.ox = (float)(telaW * 0.5 + k * ( u0 * rotCos + v0 * rotSen));
  xf.oy = (float)(telaH * 0.5 + k * (-u0 * rotSen + v0 * rotCos));
}

static inline void vecPoint(const VecXform& xf, const uint8_t* p, int i,
                            int& sx, int& sy){
  uint16_t lx = vpLE16(p + i * 4);
  uint16_t ly = vpLE16(p + i * 4 + 2);
  sx = (int)(xf.ox + (float)lx * xf.a + (float)ly * xf.b);
  sy = (int)(xf.oy + (float)lx * xf.c + (float)ly * xf.d);
}
