#pragma once
// ============================================================================
//  REGISTRO - o diario de bordo no cartao (anel na PSRAM + flush por orcamento)
// ============================================================================
//
// A REGRA DURA: ESTE ARQUIVO NAO PODE CAPAR O RADIO.
//
// Um fwrite+fsync que caia num garbage collection interno do microSD bloqueia
// por centenas de ms. Isso estoura o vigia de TX preso, conta 'preso' e refaz o
// begin() do chip no meio de um TX que estava bom - o instrumento destruindo a
// medida, como o HTTPS do eco ja fez em 2026-08-29. Dai as tres regras:
//
//   1. registroLinha() NUNCA toca no cartao e NUNCA bloqueia. So escreve no anel.
//   2. O cartao so e tocado por registroTick(), e so quando ha ORC_CARTAO_US
//      (300 ms) de espaco morto ate a proxima janela de TX. Com o quadro de
//      6 s ha ate 5,5 s de folga por quadro - o flush nunca mais e adiado.
//   3. Se o anel encher, perde-se a linha MAIS ANTIGA e conta-se o descarte.
//
// O custo de cada flush e cronometrado e vai para o proprio log.

#include <stdint.h>
#include <stdarg.h>
#include "../core/params.h"
#include "../core/anel.h"
#include "../core/diario.h"
#include "../core/orcamento.h"
#include "../hal/pinos.h"
#include "../hal/caixa.h"
#if MTS_TEM_SD
#include "../hal/sd.h"
#endif

struct Registro {
  Anel     anel;
  bool     ativo;
  char     caminho[48];
  uint32_t flushes, bytesGravados, piorFlushUs, ultFlushMs, adiados;
};
static Registro g_reg;

// A unica funcao que o resto do firmware chama para registrar. Nao bloqueia.
inline void registroLinha(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  anelLinhaV(g_reg.anel, fmt, ap);
  va_end(ap);
}

// O anel na PSRAM: 64 KB de RAM interna sao caros num aparelho com framebuffer e
// caches de mapa; o anel nao e lido de ISR, entao a PSRAM serve. ALOCADO NO BOOT,
// uma vez - nada de malloc no caminho quente depois.
inline bool registroInicia(uint32_t utcSecDia, uint32_t bootId)
{
  Registro& r = g_reg;
  memset(&r, 0, sizeof(r));
  char* buf = (char*)ps_malloc(REG_BUF_N);
  if (!buf) buf = (char*)malloc(REG_BUF_N);
  if (!buf) return false;
  anelInicia(r.anel, buf, REG_BUF_N);

#if MTS_TEM_SD
  // O nome usa a hora UTC quando ha GPS; sem fix, o contador de boot.
  if (utcSecDia)
    snprintf(r.caminho, sizeof(r.caminho), "/log/%02u%02u%02u_%lu.csv",
             (unsigned)(utcSecDia / 3600), (unsigned)((utcSecDia / 60) % 60),
             (unsigned)(utcSecDia % 60), (unsigned long)bootId);
  else
    snprintf(r.caminho, sizeof(r.caminho), "/log/boot%lu.csv", (unsigned long)bootId);
  r.ativo = sdArquivoAbre("/log", r.caminho);
  if (!r.ativo) return false;
#else
  snprintf(r.caminho, sizeof(r.caminho), "(sem cartao)");
#endif
  registroLinha(DIARIO_CAB_0, MTS_VERSAO, PROTO_VERSAO);
  registroLinha(DIARIO_CAB_E);
  registroLinha(DIARIO_CAB_R);
  registroLinha(DIARIO_CAB_T);
  registroLinha(DIARIO_CAB_N);
  registroLinha(DIARIO_CAB_G);
  return true;
}

// So aqui o cartao e tocado. Devolve true se gravou.
inline bool registroTick(Orcamento& orc, const Tdma& t, bool txEmVoo, uint32_t agoraMs)
{
  Registro& r = g_reg;
  if (!r.ativo || r.anel.pendentes == 0) return false;
  bool porTempo = (agoraMs - r.ultFlushMs) >= REG_FLUSH_MS;
  bool porVolume = r.anel.pendentes >= REG_FLUSH_BYTES;
  if (!porTempo && !porVolume) return false;
  if (!orcamentoCabe(orc, t, txEmVoo, ORC_CARTAO_US)) { r.adiados++; return false; }

#if MTS_TEM_SD
  uint32_t t0 = micros();
  caixaEtapa(CP_CARTAO_FLUSH);
  const char* p1; const char* p2; uint32_t n1, n2;
  uint32_t n = anelFatias(r.anel, p1, n1, p2, n2);
  size_t w = sdArquivoEscreve((const uint8_t*)p1, n1);
  if (n2 && w == n1) w += sdArquivoEscreve((const uint8_t*)p2, n2);
  sdArquivoFlush();
  caixaEtapa(CP_LACO);
  uint32_t dt = micros() - t0;
  if (dt > r.piorFlushUs) r.piorFlushUs = dt;
  anelDrenou(r.anel, (uint32_t)w);
  r.bytesGravados += w;
  r.flushes++;
  r.ultFlushMs = agoraMs;
  (void)n;
  if (dt > 50000UL)
    registroLinha("!,%lu,flush lento,%lu us,%lu bytes", (unsigned long)agoraMs, (unsigned long)dt, (unsigned long)w);
  return true;
#else
  anelDrenou(r.anel, r.anel.pendentes); r.ultFlushMs = agoraMs;
  return false;
#endif
}

// Fecha de verdade, antes de um reinicio deliberado: um arquivo FAT sem fechar
// perde o tamanho na tabela e o cartao volta com 0 byte.
inline void registroFecha()
{
#if MTS_TEM_SD
  Registro& r = g_reg;
  if (!r.ativo) return;
  caixaEtapa(CP_CARTAO_FECHA);
  const char* p1; const char* p2; uint32_t n1, n2;
  anelFatias(r.anel, p1, n1, p2, n2);
  if (n1) sdArquivoEscreve((const uint8_t*)p1, n1);
  if (n2) sdArquivoEscreve((const uint8_t*)p2, n2);
  anelDrenou(r.anel, r.anel.pendentes);
  sdArquivoFecha();
  r.ativo = false;
  caixaEtapa(CP_LACO);
#endif
}

inline const char* registroCaminho() { return g_reg.caminho; }
inline bool registroAtivo() { return g_reg.ativo; }
