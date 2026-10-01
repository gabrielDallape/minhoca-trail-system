#pragma once
// ============================================================================
//  CARTAO (SIMULADOR) - substitui firmware/mts/hal/sd.h. So o DIARIO: grava em
//  tools/sim/out/<no>/log/..., no MESMO formato do cartao de verdade - os
//  scripts de campo (tools/campo/*.py) leem a simulacao igual leem a trilha.
// ============================================================================
#include <stdint.h>
#include <string.h>
#include "pinos.h"
#include "caixa.h"

struct SdEstado {
  bool     montado = false;
  bool     ok = false;
  bool     multi = false;
  uint8_t  pdrv = 0xFF;
  uint32_t leituras = 0, setores = 0, erros = 0;
};
static SdEstado g_sd;
static FILE* s_sdArq = nullptr;
static std::string s_sdRaiz;      // a pasta deste no (o simulador define antes do boot)

inline bool sdMonta() { g_sd.montado = true; return true; }
inline bool sdArquivoAbre(const char* dir, const char* caminho)
{
  if (!g_sd.montado) return false;
  std::filesystem::create_directories(s_sdRaiz + dir);
  s_sdArq = fopen((s_sdRaiz + caminho).c_str(), "wb");
  return s_sdArq != nullptr;
}
inline size_t sdArquivoEscreve(const uint8_t* p, size_t n) { return s_sdArq ? fwrite(p, 1, n, s_sdArq) : 0; }
inline void   sdArquivoFlush() { if (s_sdArq) fflush(s_sdArq); }
inline void   sdArquivoFecha() { if (s_sdArq) { fclose(s_sdArq); s_sdArq = nullptr; } }
inline bool   sdArquivoAberto() { return s_sdArq != nullptr; }
