#pragma once
// ============================================================================
//  ANEL - buffer circular de linhas de texto para o diario (sem hardware)
// ============================================================================
// registroLinha() NUNCA toca no cartao: so escreve aqui. Quem drena e o flush,
// so com espaco morto no orcamento (app/registro.h). Se o anel encher, perde-se
// a linha MAIS ANTIGA e conta-se o descarte: log com buraco silencioso e pior
// que nenhum log.

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "params.h"

struct Anel {
  char*    buf;
  uint32_t cap;
  uint32_t escreve;      // proxima posicao de escrita
  uint32_t le;           // proxima posicao a drenar
  uint32_t pendentes;    // bytes esperando drenagem
  uint32_t linhas;       // linhas aceitas
  uint32_t descartadas;  // linhas perdidas por anel cheio
};

inline void anelInicia(Anel& a, char* buf, uint32_t cap)
{
  a.buf = buf; a.cap = cap; a.escreve = a.le = a.pendentes = 0;
  a.linhas = a.descartadas = 0;
}

// Joga fora linhas inteiras, do inicio, ate caber 'precisa' bytes. Meia linha no
// CSV quebra o parser, por isso o descarte anda ate o '\n'.
inline void anelDescartaMaisAntigo(Anel& a, uint32_t precisa)
{
  while (a.cap - a.pendentes < precisa && a.pendentes > 0) {
    uint32_t n = 0;
    while (n < a.pendentes && a.buf[(a.le + n) % a.cap] != '\n') n++;
    if (n < a.pendentes) n++;
    a.le = (a.le + n) % a.cap;
    a.pendentes -= n;
    a.descartadas++;
  }
}

inline void anelEscreve(Anel& a, const char* linha, uint32_t n)
{
  if (!a.buf || n == 0) return;
  if (a.cap - a.pendentes < n) anelDescartaMaisAntigo(a, n);
  if (a.cap - a.pendentes < n) return;            // linha maior que o anel
  for (uint32_t i = 0; i < n; i++) {
    a.buf[a.escreve] = linha[i];
    a.escreve = (a.escreve + 1) % a.cap;
  }
  a.pendentes += n;
  a.linhas++;
}

// Formata e escreve uma linha (garante o '\n').
inline void anelLinhaV(Anel& a, const char* fmt, va_list ap)
{
  if (!a.buf) return;
  char linha[REG_LINHA_MAX];
  int n = vsnprintf(linha, sizeof(linha), fmt, ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(linha)) n = sizeof(linha) - 1;
  if (linha[n - 1] != '\n') linha[n++] = '\n';
  anelEscreve(a, linha, (uint32_t)n);
}

// Duas fatias contiguas para drenar (a volta do anel). Devolve o total.
inline uint32_t anelFatias(const Anel& a, const char*& p1, uint32_t& n1, const char*& p2, uint32_t& n2)
{
  uint32_t n = a.pendentes;
  uint32_t ate = a.cap - a.le;
  n1 = (n < ate) ? n : ate;
  p1 = a.buf + a.le;
  n2 = n - n1;
  p2 = a.buf;
  return n;
}

inline void anelDrenou(Anel& a, uint32_t n)
{
  if (n > a.pendentes) n = a.pendentes;
  a.le = (a.le + n) % a.cap;
  a.pendentes -= n;
}
