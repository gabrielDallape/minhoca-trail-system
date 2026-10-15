/*
 * ============================================================================
 *  DEM PACK - leitor do relevo gerado por tools/mapa/relevo.py
 *
 *  Da tres coisas com o mesmo dado, e e por isso que se guarda a ELEVACAO e nao
 *  a curva de nivel pronta:
 *      curva de nivel .... interpolada na hora, no intervalo que couber no zoom
 *      sombreado ......... o relevo aparece sem precisar ler numero
 *      altitude .......... quantos metros voce esta
 *  Curva pronta em vetor daria mais pontos que toda a malha viaria do Brasil e
 *  so serviria para desenhar linha.
 *
 *  BLOCO DE 0,1 GRAU. O .hgt de origem tem 1201x1201 amostras por grau, e
 *  1201 = 10 x 120 + 1: o grau divide EXATAMENTE em 10x10 blocos de 121x121, com
 *  blocos vizinhos compartilhando a linha da borda. Essa borda compartilhada e o
 *  que permite calcular o sombreado - que precisa do vizinho de cada amostra -
 *  sem ler o bloco do lado.
 *
 *  MEDIDO: bloco de 29,0 KB, 12,4 ms de busca + 16,5 ms de dado = ~29 ms. Ler o
 *  grau inteiro custaria 1,6 s, inviavel por quadro.
 *
 *  As amostras ja vem em LITTLE-ENDIAN (o .hgt e big-endian; a troca aconteceu no
 *  PC). O aparelho nao gasta um ciclo trocando byte no caminho do quadro.
 *
 *  ENDERECO E ARITMETICA PURA, sem Mercator: o relevo e amostrado em GRAUS.
 *      linha  = floor((lat +  90) * 10)
 *      coluna = floor((lon + 180) * 10)
 *
 *  Layout (identico ao de relevo.py - se mudar um lado, mude o outro):
 *    setor 0            cabecalho
 *    setor indexSector  indice: por slot {setor u32, crc32 u32}
 *    setor dataSector.. blocos de tamanho fixo (58 setores)
 * ============================================================================
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#define DEMPACK_SECTOR   512
#define DEMPACK_IDX_SIZE 8
#define DEM_VOID         (-32768)

typedef bool (*DemPackReader)(uint32_t startSector, uint32_t count, void* dst, void* user);

struct DemPack {
  DemPackReader read;
  void*    user;
  bool     ok;
  uint16_t version, n;        // n = amostras por lado (121)
  uint8_t  porGrau;           // blocos por grau, em cada eixo (10)
  uint32_t indexSector, dataSector, nSlots, blocoSetores;
  int32_t  lin0, col0;        // origem da grade, em decimos de grau + 900/1800
  uint16_t w, h;
  uint8_t  sectorBuf[DEMPACK_SECTOR];
  uint32_t crcErrors, readErrors;
};

static inline uint16_t dpLE16(const uint8_t* p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t dpLE32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const uint32_t DP_CRC_NIB[16] = {
  0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
  0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
  0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
  0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};
inline uint32_t demPackCrc32(const void* data, uint32_t len, uint32_t crc = 0){
  const uint8_t* p = (const uint8_t*)data;
  crc = ~crc;
  while (len--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ DP_CRC_NIB[crc & 0x0F];
    crc = (crc >> 4) ^ DP_CRC_NIB[crc & 0x0F];
  }
  return ~crc;
}

inline bool demPackOpen(DemPack& dp, DemPackReader reader, void* user){
  memset(&dp, 0, sizeof(dp));
  dp.read = reader; dp.user = user; dp.ok = false;
  if (!reader) return false;
  if (!reader(0, 1, dp.sectorBuf, user)) { dp.readErrors++; return false; }
  const uint8_t* h = dp.sectorBuf;
  if (memcmp(h, "MTSDEM01", 8) != 0) return false;

  dp.version      = dpLE16(h + 8);
  dp.n            = dpLE16(h + 10);
  dp.porGrau      = h[12];
  dp.indexSector  = dpLE32(h + 16);
  dp.dataSector   = dpLE32(h + 20);
  dp.nSlots       = dpLE32(h + 24);
  dp.lin0         = (int32_t)dpLE32(h + 28);
  dp.col0         = (int32_t)dpLE32(h + 32);
  dp.w            = dpLE16(h + 36);
  dp.h            = dpLE16(h + 38);
  dp.blocoSetores = dpLE32(h + 40);

  if (dp.version != 1) return false;
  if (dp.n < 2 || dp.porGrau == 0 || dp.blocoSetores == 0) return false;
  if (dp.indexSector == 0 || dp.dataSector <= dp.indexSector) return false;
  if ((uint32_t)dp.w * dp.h != dp.nSlots) return false;
  dp.ok = true;
  return true;
}

inline uint32_t demPackBlockBytes(const DemPack& dp){
  return (uint32_t)dp.n * dp.n * 2;
}

// Bloco que contem o ponto. Devolve -1 fora da grade.
inline int32_t demPackSlot(const DemPack& dp, double lat, double lon){
  if (!dp.ok) return -1;
  // floor, nao truncamento: lat negativa com (int) truncaria para cima e
  // deslocaria o bloco inteiro no hemisferio sul - onde o Brasil todo esta.
  int32_t lin = (int32_t)floor((lat + 90.0) * dp.porGrau) - dp.lin0;
  int32_t col = (int32_t)floor((lon + 180.0) * dp.porGrau) - dp.col0;
  if (lin < 0 || col < 0 || lin >= (int32_t)dp.h || col >= (int32_t)dp.w) return -1;
  return lin * (int32_t)dp.w + col;
}

// Slot a partir da linha/coluna ABSOLUTAS da grade global de 0,1 grau. Serve para
// percorrer blocos sem reconverter lat/lon a cada um.
inline int32_t demPackSlotLC(const DemPack& dp, int32_t lin, int32_t col){
  int32_t l = lin - dp.lin0, c = col - dp.col0;
  if (l < 0 || c < 0 || l >= (int32_t)dp.h || c >= (int32_t)dp.w) return -1;
  return l * (int32_t)dp.w + c;
}

// Canto SUDOESTE do bloco (lin,col), em graus.
inline void demPackCantoSO(const DemPack& dp, int32_t lin, int32_t col,
                           double& lat, double& lon){
  lat = (double)lin / dp.porGrau - 90.0;
  lon = (double)col / dp.porGrau - 180.0;
}

enum DemPackResult : uint8_t {
  DEMPACK_OK = 0, DEMPACK_ABSENT, DEMPACK_IOERROR, DEMPACK_BADCRC, DEMPACK_TOOBIG
};

// dst precisa de blocoSetores*512 bytes (29.696). Aloque na PSRAM.
inline DemPackResult demPackReadBlock(DemPack& dp, int32_t slot,
                                      void* dst, uint32_t dstCap, bool checkCrc){
  if (!dp.ok) return DEMPACK_IOERROR;
  if (slot < 0 || (uint32_t)slot >= dp.nSlots) return DEMPACK_ABSENT;
  if (dp.blocoSetores * DEMPACK_SECTOR > dstCap) return DEMPACK_TOOBIG;

  uint32_t byteOff = (uint32_t)slot * DEMPACK_IDX_SIZE;   // 8 divide 512: nunca cruza
  uint32_t sec = dp.indexSector + byteOff / DEMPACK_SECTOR;
  uint32_t off = byteOff % DEMPACK_SECTOR;
  if (!dp.read(sec, 1, dp.sectorBuf, dp.user)) { dp.readErrors++; return DEMPACK_IOERROR; }
  uint32_t setor = dpLE32(dp.sectorBuf + off);
  uint32_t crc   = dpLE32(dp.sectorBuf + off + 4);
  if (setor == 0) return DEMPACK_ABSENT;                  // mar, ou fora do pais

  if (!dp.read(setor, dp.blocoSetores, dst, dp.user)) { dp.readErrors++; return DEMPACK_IOERROR; }
  if (checkCrc && demPackCrc32(dst, demPackBlockBytes(dp)) != crc) {
    dp.crcErrors++; return DEMPACK_BADCRC;
  }
  return DEMPACK_OK;
}

// -------------------------------------------------------------- amostragem
// LINHA 0 DO BLOCO E O NORTE. Inverter isto e o erro classico deste formato: o
// relevo sai espelhado no eixo norte-sul e continua "parecendo relevo", entao
// ninguem nota ate comparar com o terreno de verdade.
static inline int16_t demAmostra(const DemPack& dp, const void* bloco, int r, int c){
  if (r < 0) r = 0; if (r >= dp.n) r = dp.n - 1;
  if (c < 0) c = 0; if (c >= dp.n) c = dp.n - 1;
  return (int16_t)dpLE16((const uint8_t*)bloco + ((uint32_t)r * dp.n + c) * 2);
}

// Onde o ponto cai dentro do bloco, em unidades de amostra (com fracao).
inline void demPosNoBloco(const DemPack& dp, double lat, double lon,
                          double& r, double& c){
  double fr = (lat + 90.0) * dp.porGrau;
  double fc = (lon + 180.0) * dp.porGrau;
  fr -= floor(fr);
  fc -= floor(fc);
  r = (1.0 - fr) * (dp.n - 1);      // fracao 1,0 = topo do bloco = norte
  c = fc * (dp.n - 1);
}

// Altitude com interpolacao bilinear. Sem ela o numero pula de degrau em degrau
// a cada 90 m andados, e o mostrador de altitude fica nervoso parado no lugar.
inline float demAltitude(const DemPack& dp, const void* bloco, double lat, double lon){
  double r, c;
  demPosNoBloco(dp, lat, lon, r, c);
  int r0 = (int)floor(r), c0 = (int)floor(c);
  float fr = (float)(r - r0), fc = (float)(c - c0);
  float a = demAmostra(dp, bloco, r0,     c0);
  float b = demAmostra(dp, bloco, r0,     c0 + 1);
  float d = demAmostra(dp, bloco, r0 + 1, c0);
  float e = demAmostra(dp, bloco, r0 + 1, c0 + 1);
  return (a * (1 - fc) + b * fc) * (1 - fr) + (d * (1 - fc) + e * fc) * fr;
}

// Metros no chao entre duas amostras vizinhas, na latitude dada.
inline void demPassoMetros(const DemPack& dp, double lat, float& mx, float& my){
  double grauPorAmostra = 1.0 / ((double)dp.porGrau * (dp.n - 1));
  my = (float)(grauPorAmostra * 111320.0);
  mx = (float)(grauPorAmostra * 111320.0 * cos(lat * 0.017453292519943295));
}

// ---------------------------------------------------------------- sombreado
// Sombreado padrao de carta topografica: luz vindo do NOROESTE a 45 graus de
// altura. A convencao nao e estetica - vem da cartografia impressa, e o olho
// humano interpreta relevo iluminado de outra direcao como CAVIDADE em vez de
// morro (a "ilusao do relevo invertido"). Mudar a direcao inverte o morro.
//
// Devolve 0..255, onde 128 e terreno plano.
inline uint8_t demSombra(const DemPack& dp, const void* bloco, int r, int c,
                         float mx, float my, float exagero = 2.0f){
  float dzdx = (demAmostra(dp, bloco, r, c + 1) - demAmostra(dp, bloco, r, c - 1))
             / (2.0f * mx) * exagero;
  // r+1 e mais ao SUL: o sinal invertido aqui mantem o gradiente no sentido
  // geografico, e nao no sentido da matriz
  float dzdy = (demAmostra(dp, bloco, r - 1, c) - demAmostra(dp, bloco, r + 1, c))
             / (2.0f * my) * exagero;

  // PRODUTO ESCALAR, e nao declive-e-aspecto. A formula de livro calcula
  //     declive = atan(|grad|);  aspecto = atan2(...);  v = f(sin, cos, cos)
  // que sao CINCO transcendentais por amostra. Como o sombreado roda por bloco de
  // 8 px, sao 14.400 amostras por quadro - MEDIDO, era boa parte dos 246 ms de
  // desenho. O resultado e identico: iluminar uma superficie e o cosseno do
  // angulo entre a normal e a luz, ou seja o produto escalar dos dois vetores
  // normalizados. Sobra UMA raiz quadrada, e o vetor da luz e constante.
  //
  // normal da superficie = (-dz/dx, -dz/dy, 1), ainda nao normalizada
  // luz a 45 graus de altura vindo do NOROESTE (azimute 315):
  //     Lx = cos(45)*sin(315) = -0.5   Ly = cos(45)*cos(315) = +0.5
  //     Lz = sin(45) = 0.7071
  const float LX = -0.5f, LY = 0.5f, LZ = 0.70710678f;
  float d = -dzdx * LX + -dzdy * LY + LZ;
  float n = sqrtf(dzdx * dzdx + dzdy * dzdy + 1.0f);
  float v = d / n;
  if (v < 0) v = 0;
  if (v > 1) v = 1;
  return (uint8_t)(v * 255.0f);
}

// ------------------------------------------------------------ curva de nivel
// Intervalo que nao polui a tela: alvo de ~80 px entre curvas na escala atual.
// Escolhe da escada 10/20/50/100/200/500 m, porque curva em intervalo "quebrado"
// (37 m) e ilegivel - o leitor conta curvas para saber o desnivel.
inline int demIntervaloCurva(double mPorPx, float declivIcoMedio = 0.08f){
  float metrosPorTela = (float)(80.0 * mPorPx) * declivIcoMedio;
  static const int ESCADA[] = { 10, 20, 50, 100, 200, 500, 1000 };
  for (int i = 0; i < 7; i++) if (ESCADA[i] >= metrosPorTela) return ESCADA[i];
  return 1000;
}

// Testa se a aresta entre duas amostras cruza a curva de nivel `nivel`, e onde.
// Base para marching squares: o chamador percorre a grade e liga os cruzamentos.
static inline bool demCruza(int16_t a, int16_t b, int nivel, float& t){
  if ((a < nivel) == (b < nivel)) return false;
  if (a == b) return false;
  t = (float)(nivel - a) / (float)(b - a);
  return true;
}
