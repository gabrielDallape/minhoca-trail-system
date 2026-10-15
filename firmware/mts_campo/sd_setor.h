// Leitura do cartao POR SETOR, que e o que o vec_pack.h e o dem_pack.h pedem.
//
// O PROBLEMA QUE ESTE ARQUIVO RESOLVE
// O SD_MMC do Arduino expoe readRAW(buffer, setor) - UM setor por chamada. Um
// bloco de relevo tem 58 setores e um setor de mapa pode ter 800: fazer 800
// chamadas paga 800 vezes o custo de comando do cartao. O caminho rapido e o
// disk_read() do FatFs, que aceita contagem e emite uma leitura multi-setor so.
//
// Para chamar disk_read e preciso o numero do drive (pdrv), que o SD_MMC guarda
// como membro protegido. Em vez de forcar o acesso, este arquivo DESCOBRE o pdrv:
// le o setor 0 pelos dois caminhos e ve qual drive devolve o mesmo conteudo.
// Se nenhum bater, cai para o readRAW em laco - mais lento, mas o mapa aparece.
// Ficar sem mapa por causa de um detalhe de API seria o pior desfecho possivel.
#pragma once
#include <stdint.h>
#include <string.h>
#include "SD_MMC.h"

extern "C" {
#include "diskio.h"
}

#define SD_SETOR 512

struct SdSetor {
  bool     ok = false;
  bool     multi = false;      // true = disk_read com contagem; false = readRAW em laco
  uint8_t  pdrv = 0xFF;
  uint32_t leituras = 0, setores = 0, erros = 0;
};

static SdSetor g_sd;

// Uma regiao do cartao (particao). O leitor soma a base ao setor pedido, entao o
// pacote nao precisa saber onde foi gravado.
struct SdArea { uint32_t base = 0; };

inline bool sdSetorInit(SdSetor& s) {
  s = SdSetor();
  uint8_t a[SD_SETOR], b[SD_SETOR];
  if (!SD_MMC.readRAW(a, 0)) return false;

  // Descobre o pdrv comparando o setor 0. Sao poucos drives; 4 tentativas bastam.
  for (uint8_t d = 0; d < 4; d++) {
    if (disk_read(d, b, 0, 1) != 0) continue;
    if (memcmp(a, b, SD_SETOR) != 0) continue;
    // confere de novo num setor diferente, para nao casar por acaso com um
    // cartao cheio de zeros
    uint8_t c1[SD_SETOR], c2[SD_SETOR];
    bool r1 = SD_MMC.readRAW(c1, 1);
    bool r2 = (disk_read(d, c2, 1, 1) == 0);
    if (r1 && r2 && memcmp(c1, c2, SD_SETOR) != 0) continue;
    s.pdrv = d; s.multi = true; break;
  }
  s.ok = true;
  return true;
}

inline bool sdLeArea(uint32_t setor, uint32_t n, void* dst, void* user) {
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
    if (!SD_MMC.readRAW(p + (size_t)i * SD_SETOR, base + setor + i)) {
      g_sd.erros++;
      return false;
    }
  }
  return true;
}

// --------------------------------------------------------------- particoes
// Acha a particao cujo primeiro setor comeca com `magic`. O mapa e gravado em
// particao BRUTA (tipo 0xDA, "dado sem filesystem"), nao em arquivo: e a mesma
// razao do tile_pack.h - sem FAT nao ha tabela de alocacao para corromper quando
// o carro e desligado na chave.
inline bool sdAchaParticao(const char magic[8], uint32_t& baseSetor, uint32_t& tamanho) {
  uint8_t mbr[SD_SETOR], pri[SD_SETOR];
  SdArea zero;
  if (!sdLeArea(0, 1, mbr, &zero)) return false;
  if (mbr[510] != 0x55 || mbr[511] != 0xAA) return false;   // sem tabela de particao

  for (int i = 0; i < 4; i++) {
    const uint8_t* e = mbr + 0x1BE + i * 16;
    uint32_t ini = (uint32_t)e[8] | ((uint32_t)e[9] << 8)
                 | ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
    uint32_t n   = (uint32_t)e[12] | ((uint32_t)e[13] << 8)
                 | ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
    if (!ini || !n) continue;
    SdArea a; a.base = ini;
    if (!sdLeArea(0, 1, pri, &a)) continue;
    if (memcmp(pri, magic, 8) == 0) { baseSetor = ini; tamanho = n; return true; }
  }
  return false;
}
