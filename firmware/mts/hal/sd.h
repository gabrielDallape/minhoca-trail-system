#pragma once
// ============================================================================
//  SD - o cartao: montagem, leitura por SETOR (mapa) e arquivo (diario)
// ============================================================================
//
// DOIS MUNDOS NO MESMO CARTAO, e eles nao se tocam:
//   - o MAPA mora em particoes BRUTAS (tipo 0xDA, achadas pelo magic no MBR),
//     fora de qualquer filesystem. Sem FAT nao ha tabela de alocacao para
//     corromper quando o carro e desligado na chave. NUNCA formatar nem
//     escrever nelas: este arquivo so LE setores ali.
//   - o DIARIO mora na particao FAT, em /log. E o unico lugar em que o firmware
//     escreve no cartao.
//
// LEITURA MULTI-SETOR. O SD_MMC do Arduino expoe readRAW(buffer, setor) - UM
// setor por chamada. Um setor de mapa pode ter 800 setores: 800 comandos ao
// cartao. O caminho rapido e o disk_read() do FatFs, que aceita contagem. Para
// chama-lo e preciso o numero do drive, que o SD_MMC guarda protegido; aqui ele
// e DESCOBERTO comparando o setor 0 pelos dois caminhos. Se nao bater, cai para
// o readRAW em laco - mais lento, mas o mapa aparece.

#include <stdint.h>
#include <string.h>
#include "FS.h"
#include "SD_MMC.h"
extern "C" {
#include "diskio.h"
}
#include "pinos.h"
#include "caixa.h"

#define SD_SETOR 512

struct SdEstado {
  bool     montado = false;
  bool     ok = false;         // leitura por setor operacional
  bool     multi = false;      // disk_read com contagem
  uint8_t  pdrv = 0xFF;
  uint32_t leituras = 0, setores = 0, erros = 0;
};
static SdEstado g_sd;

struct SdArea { uint32_t base = 0; };     // uma particao: o leitor soma a base

inline bool sdMonta()
{
  SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0, SD_PIN_D1, SD_PIN_D2, SD_PIN_D3);
#ifdef SOC_SDMMC_IO_POWER_EXTERNAL
  SD_MMC.setPowerChannel(SD_LDO_CANAL);
#endif
  g_sd.montado = SD_MMC.begin("/sdcard", false);
  return g_sd.montado;
}

inline bool sdSetorInicia()
{
  if (!g_sd.montado) return false;
  uint8_t a[SD_SETOR], b[SD_SETOR];
  if (!SD_MMC.readRAW(a, 0)) return false;
  for (uint8_t d = 0; d < 4; d++) {
    if (disk_read(d, b, 0, 1) != 0) continue;
    if (memcmp(a, b, SD_SETOR) != 0) continue;
    // confere num setor diferente, para nao casar por acaso com um cartao de zeros
    uint8_t c1[SD_SETOR], c2[SD_SETOR];
    bool r1 = SD_MMC.readRAW(c1, 1);
    bool r2 = (disk_read(d, c2, 1, 1) == 0);
    if (r1 && r2 && memcmp(c1, c2, SD_SETOR) != 0) continue;
    g_sd.pdrv = d; g_sd.multi = true; break;
  }
  g_sd.ok = true;
  return true;
}

// A assinatura que vec_pack.h/dem_pack.h pedem (callback de leitura).
inline bool sdLeArea(uint32_t setor, uint32_t n, void* dst, void* user)
{
  SdArea* ar = (SdArea*)user;
  uint32_t base = ar ? ar->base : 0;
  g_sd.leituras++;
  g_sd.setores += n;
  if (g_sd.multi) {
    if (disk_read(g_sd.pdrv, (BYTE*)dst, base + setor, n) == 0) return true;
    g_sd.erros++;
    return false;
  }
  uint8_t* p = (uint8_t*)dst;
  for (uint32_t i = 0; i < n; i++) {
    if (!SD_MMC.readRAW(p + (size_t)i * SD_SETOR, base + setor + i)) { g_sd.erros++; return false; }
  }
  return true;
}

// Acha a particao cujo primeiro setor comeca com `magic` (tipo 0xDA, sem FS).
inline bool sdAchaParticao(const char magic[8], uint32_t& baseSetor, uint32_t& tamanho)
{
  uint8_t mbr[SD_SETOR], pri[SD_SETOR];
  SdArea zero;
  if (!sdLeArea(0, 1, mbr, &zero)) return false;
  if (mbr[510] != 0x55 || mbr[511] != 0xAA) return false;
  for (int i = 0; i < 4; i++) {
    const uint8_t* e = mbr + 0x1BE + i * 16;
    uint32_t ini = (uint32_t)e[8] | ((uint32_t)e[9] << 8) | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
    uint32_t n   = (uint32_t)e[12] | ((uint32_t)e[13] << 8) | ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
    if (!ini || !n) continue;
    SdArea a; a.base = ini;
    if (!sdLeArea(0, 1, pri, &a)) continue;
    if (memcmp(pri, magic, 8) == 0) { baseSetor = ini; tamanho = n; return true; }
  }
  return false;
}

// ---------------------------------------------------------------- arquivo
// So o diario escreve. Uma File so, aberta uma vez por boot.
static File g_sdArq;

inline bool sdArquivoAbre(const char* dir, const char* caminho)
{
  if (!g_sd.montado) return false;
  SD_MMC.mkdir(dir);
  g_sdArq = SD_MMC.open(caminho, FILE_WRITE);
  return (bool)g_sdArq;
}
inline size_t sdArquivoEscreve(const uint8_t* p, size_t n) { return g_sdArq ? g_sdArq.write(p, n) : 0; }
inline void   sdArquivoFlush() { if (g_sdArq) g_sdArq.flush(); }
inline void   sdArquivoFecha() { if (g_sdArq) { g_sdArq.close(); } }
inline bool   sdArquivoAberto() { return (bool)g_sdArq; }
