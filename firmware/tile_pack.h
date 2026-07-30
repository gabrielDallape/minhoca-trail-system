/*
 * ============================================================================
 *  TILE PACK - leitor do container de mapa offline gerado por tools/tiles/tiles.py
 *
 *  Le tiles RGB565 direto por SETOR, SEM filesystem. Motivo: o aparelho perde
 *  energia sem aviso (chave do carro). Sem FAT nao existe tabela de alocacao
 *  nem diretorio para corromper - o pior caso e um tile individual sair errado,
 *  e o CRC32 do indice torna isso DETECTAVEL (desenha tile cinza em vez de
 *  lixo). De quebra e mais rapido: o endereco do tile e aritmetica, sem
 *  travessia de diretorio nem cache de FAT no caminho do frame.
 *
 *  NAO depende de SD/SPI/SDMMC: a leitura entra por um callback. O mesmo codigo
 *  serve para SD por SPI no ESP32-S3 (SD.h) e para SDIO no ESP32-P4
 *  (sdmmc_read_sectors), trocando so a funcao passada em tilePackOpen().
 *
 *  Layout (identico ao do tiles.py - se mudar um lado, mude o outro):
 *    setor 0            cabecalho + tabela de niveis
 *    setor indexSector  indice: por slot, {setor u32, crc32 u32} (setor 0 = ausente)
 *    setor dataSector.. tiles, cada um ocupando tileSectors setores
 *
 *    slot = indexOffset[nivel] + (y - ymin) * w + (x - xmin)
 * ============================================================================
 */
#pragma once
#include <stdint.h>
#include <string.h>

#define TILEPACK_SECTOR      512
#define TILEPACK_MAX_LEVELS  16
#define TILEPACK_HDR_SIZE    36
#define TILEPACK_LVL_SIZE    20
#define TILEPACK_IDX_SIZE    8
#define TILEPACK_PIXFMT_RGB565 0

// Le `count` setores de 512B a partir de `startSector`. Devolve false em erro.
// `user` e repassado sem interpretacao (ponteiro do objeto SD, por exemplo).
typedef bool (*TilePackReader)(uint32_t startSector, uint32_t count, void* dst, void* user);

struct TilePackLevel {
  uint8_t  zoom;
  uint32_t xmin, ymin;
  uint16_t w, h;
  uint32_t indexOffset;
};

struct TilePack {
  TilePackReader read;
  void*    user;
  bool     ok;
  uint16_t version, tilePx;
  uint8_t  pixFmt, nLevels;
  uint32_t indexSector, dataSector, tileSectors, nTiles;
  TilePackLevel levels[TILEPACK_MAX_LEVELS];
  uint8_t  sectorBuf[TILEPACK_SECTOR];   // scratch p/ cabecalho e indice
  // diagnostico: quantos tiles sairam com CRC errado desde o boot. Se isto
  // sobe com o tempo, o cartao esta degradando (read disturb) - troque.
  uint32_t crcErrors, readErrors;
};

// ------------------------------------------------------- leitura little-endian
static inline uint16_t tpLE16(const uint8_t* p){ return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static inline uint32_t tpLE32(const uint8_t* p){
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ------------------------------------------------------------------- CRC32
// Implementado aqui (e nao via esp_rom_crc32_le) para garantir bit-a-bit o mesmo
// resultado do zlib.crc32 do Python, sem depender de convencao de ROM.
// Tabela de 16 entradas (nibble): 64 bytes de rodata em vez de 1KB, e rapido o
// suficiente - 128KB levam poucos ms.
static const uint32_t TP_CRC_NIB[16] = {
  0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
  0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
  0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
  0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};
inline uint32_t tilePackCrc32(const void* data, uint32_t len, uint32_t crc = 0){
  const uint8_t* p = (const uint8_t*)data;
  crc = ~crc;
  while (len--) {
    crc ^= *p++;
    crc = (crc >> 4) ^ TP_CRC_NIB[crc & 0x0F];
    crc = (crc >> 4) ^ TP_CRC_NIB[crc & 0x0F];
  }
  return ~crc;
}

// --------------------------------------------------------------------- open
inline bool tilePackOpen(TilePack& tp, TilePackReader reader, void* user){
  memset(&tp, 0, sizeof(tp));
  tp.read = reader; tp.user = user; tp.ok = false;
  if (!reader) return false;
  if (!reader(0, 1, tp.sectorBuf, user)) { tp.readErrors++; return false; }

  const uint8_t* h = tp.sectorBuf;
  if (memcmp(h, "TRILHAMP", 8) != 0) return false;      // nao e um tile pack

  tp.version     = tpLE16(h + 8);
  tp.tilePx      = tpLE16(h + 10);
  tp.pixFmt      = h[12];
  tp.nLevels     = h[13];
  tp.indexSector = tpLE32(h + 16);
  tp.dataSector  = tpLE32(h + 20);
  tp.tileSectors = tpLE32(h + 24);
  tp.nTiles      = tpLE32(h + 28);

  if (tp.version != 1) return false;
  if (tp.pixFmt != TILEPACK_PIXFMT_RGB565) return false;
  if (tp.nLevels == 0 || tp.nLevels > TILEPACK_MAX_LEVELS) return false;
  if (tp.tilePx == 0 || tp.tileSectors == 0) return false;
  // o cabecalho + a tabela de niveis tem de caber no setor 0
  if ((uint32_t)TILEPACK_HDR_SIZE + (uint32_t)tp.nLevels * TILEPACK_LVL_SIZE > TILEPACK_SECTOR) return false;

  for (uint8_t i = 0; i < tp.nLevels; i++) {
    const uint8_t* L = h + TILEPACK_HDR_SIZE + (uint32_t)i * TILEPACK_LVL_SIZE;
    tp.levels[i].zoom        = L[0];
    tp.levels[i].xmin        = tpLE32(L + 4);
    tp.levels[i].ymin        = tpLE32(L + 8);
    tp.levels[i].w           = tpLE16(L + 12);
    tp.levels[i].h           = tpLE16(L + 14);
    tp.levels[i].indexOffset = tpLE32(L + 16);
  }
  tp.ok = true;
  return true;
}

// Bytes de um tile (256x256 RGB565 = 131072). Use p/ dimensionar o buffer.
inline uint32_t tilePackTileBytes(const TilePack& tp){
  return (uint32_t)tp.tilePx * tp.tilePx * 2;
}

inline int tilePackLevelIdx(const TilePack& tp, uint8_t zoom){
  for (uint8_t i = 0; i < tp.nLevels; i++) if (tp.levels[i].zoom == zoom) return i;
  return -1;
}

// Zoom disponivel mais proximo do pedido (o mapa nao "apaga" se faltar o nivel).
inline int tilePackNearestZoom(const TilePack& tp, uint8_t zoom){
  int best = -1, bestd = 1000;
  for (uint8_t i = 0; i < tp.nLevels; i++) {
    int d = (int)tp.levels[i].zoom - (int)zoom; if (d < 0) d = -d;
    if (d < bestd) { bestd = d; best = i; }
  }
  return best;
}

// slot no indice, ou -1 se o tile esta fora da grade gravada.
inline int32_t tilePackSlot(const TilePack& tp, uint8_t zoom, uint32_t x, uint32_t y){
  int li = tilePackLevelIdx(tp, zoom);
  if (li < 0) return -1;
  const TilePackLevel& L = tp.levels[li];
  if (x < L.xmin || y < L.ymin) return -1;
  uint32_t dx = x - L.xmin, dy = y - L.ymin;
  if (dx >= L.w || dy >= L.h) return -1;
  return (int32_t)(L.indexOffset + dy * (uint32_t)L.w + dx);
}

// Le o registro do indice do slot: setor do tile (0 = ausente) e o CRC esperado.
inline bool tilePackEntry(TilePack& tp, int32_t slot, uint32_t& sector, uint32_t& crc){
  if (!tp.ok || slot < 0) return false;
  uint32_t byteOff = (uint32_t)slot * TILEPACK_IDX_SIZE;
  uint32_t sec = tp.indexSector + byteOff / TILEPACK_SECTOR;
  uint32_t off = byteOff % TILEPACK_SECTOR;
  if (!tp.read(sec, 1, tp.sectorBuf, tp.user)) { tp.readErrors++; return false; }
  sector = tpLE32(tp.sectorBuf + off);
  crc    = tpLE32(tp.sectorBuf + off + 4);
  return true;
}

// ----------------------------------------------------------------- read tile
// dst precisa ter tilePackTileBytes() bytes (aloque na PSRAM: 128KB por tile).
// checkCrc=true custa alguns ms; use no diagnostico ou 1x a cada N tiles, nao
// necessariamente em todo carregamento.
//
// Retorno:
//    TILEPACK_OK        tile carregado
//    TILEPACK_ABSENT    nao existe tile nessa posicao (agua/fora da area) -> pintar fundo
//    TILEPACK_IOERROR   o cartao nao respondeu -> cair para o mapa de fallback
//    TILEPACK_BADCRC    tile corrompido -> pintar tile cinza (NAO desenhar lixo)
enum TilePackResult : uint8_t { TILEPACK_OK = 0, TILEPACK_ABSENT, TILEPACK_IOERROR, TILEPACK_BADCRC };

inline TilePackResult tilePackReadTile(TilePack& tp, uint8_t zoom, uint32_t x, uint32_t y,
                                       void* dst, bool checkCrc){
  if (!tp.ok) return TILEPACK_IOERROR;
  int32_t slot = tilePackSlot(tp, zoom, x, y);
  if (slot < 0) return TILEPACK_ABSENT;

  uint32_t sector = 0, crc = 0;
  if (!tilePackEntry(tp, slot, sector, crc)) return TILEPACK_IOERROR;
  if (sector == 0) return TILEPACK_ABSENT;

  if (!tp.read(sector, tp.tileSectors, dst, tp.user)) { tp.readErrors++; return TILEPACK_IOERROR; }

  if (checkCrc) {
    uint32_t got = tilePackCrc32(dst, tilePackTileBytes(tp));
    if (got != crc) { tp.crcErrors++; return TILEPACK_BADCRC; }
  }
  return TILEPACK_OK;
}

// ------------------------------------------------------- slippy map <-> geo
// Mesma matematica do tiles.py. Web Mercator, tile 256px.
inline void tilePackDegToTile(double lat, double lon, uint8_t z, uint32_t& x, uint32_t& y){
  double n = (double)(1UL << z);
  if (lat >  85.05112878) lat =  85.05112878;      // limite do Mercator
  if (lat < -85.05112878) lat = -85.05112878;
  double fx = (lon + 180.0) / 360.0 * n;
  double s = sin(lat * 0.017453292519943295);
  double fy = (1.0 - log((1.0 + s) / (1.0 - s)) / (2.0 * 3.14159265358979323846)) / 2.0 * n;
  if (fx < 0) fx = 0; if (fx > n - 1) fx = n - 1;
  if (fy < 0) fy = 0; if (fy > n - 1) fy = n - 1;
  x = (uint32_t)fx; y = (uint32_t)fy;
}

// Pixel dentro do tile (0..tilePx-1) correspondente a lat/lon. E o que centra
// o mapa: o tile diz QUAL azulejo, isto diz ONDE dentro dele voce esta.
inline void tilePackDegToPixel(double lat, double lon, uint8_t z, uint16_t tilePx,
                               uint32_t& x, uint32_t& y, uint16_t& px, uint16_t& py){
  double n = (double)(1UL << z);
  if (lat >  85.05112878) lat =  85.05112878;
  if (lat < -85.05112878) lat = -85.05112878;
  double fx = (lon + 180.0) / 360.0 * n;
  double s = sin(lat * 0.017453292519943295);
  double fy = (1.0 - log((1.0 + s) / (1.0 - s)) / (2.0 * 3.14159265358979323846)) / 2.0 * n;
  if (fx < 0) fx = 0; if (fx > n - 1) fx = n - 1;
  if (fy < 0) fy = 0; if (fy > n - 1) fy = n - 1;
  x = (uint32_t)fx; y = (uint32_t)fy;
  px = (uint16_t)((fx - (double)x) * tilePx);
  py = (uint16_t)((fy - (double)y) * tilePx);
}

// Metros por pixel na latitude dada - use p/ desenhar rastro e aneis de
// distancia na mesma escala do fundo.
inline double tilePackMetersPerPixel(double lat, uint8_t z, uint16_t tilePx){
  return 156543.03392 * cos(lat * 0.017453292519943295) / (double)(1UL << z) * (256.0 / tilePx);
}
