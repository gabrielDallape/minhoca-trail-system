#pragma once
// ============================================================================
//  VIZINHOS - os GRUPOS que estao no ar e nao sao o meu. Sem hardware.
// ============================================================================
//
// POR QUE EXISTE (teste no carro de 2026-09-30): as duas telas tocaram em CRIAR
// varias vezes e ficaram em grupos diferentes. O radio de cada uma recebia a
// outra - com -31 a -47 dBm, carros colados - e o firmware descartava os pacotes
// por serem de outra sala, como deve. Na tela do Gino nao aparecia nada, e
// ninguem tinha como saber por que.
//
// O pacote leva a sala como HASH de 16 bits, nunca o codigo digitado. Mas o hash
// e tudo que o filtro usa: entrar "pelo hash" e exatamente tao bom quanto
// digitar o codigo. Entao a rede, em vez de so descartar o pacote de outra sala,
// anota aqui de quem ele era. A tela ENTRAR mostra a lista e entra com um toque
// (a sessao guarda a sala como "#79FD" - ver protoSalaDoCodigo em proto.h).
//
// O nome vem do ROSTER (a cada ROSTER_CADA falas; nas falas 1, 3 e 5 logo que o
// grupo e criado). Ate ele chegar, o grupo aparece pelo hash.

#include <stdint.h>
#include <string.h>
#include "params.h"
#include "tempo.h"

#define VIZ_MAX        4                     // grupos lembrados ao mesmo tempo
#define VIZ_TTL_MS     30000UL               // sem ouvir por 30 s: some da lista (em segundos, nao quadros)

struct GrupoNoAr {
  bool     ativo;
  uint16_t sala;
  bool     temLider;        // ouvi alguem com FL_LIDER neste grupo
  bool     temNome;
  bool     nomeDoLider;     // o nome guardado e o do lider (prefere sobre o de seguidor)
  char     nome[ROSTER_NOME_N + 1];
  int16_t  rssi;            // do ultimo pacote
  uint16_t slots;           // bit por slot ouvido recentemente
  uint32_t slotMs[16];      // quando cada slot foi ouvido
  uint32_t ultMs;
};

struct Vizinhos {
  GrupoNoAr g[VIZ_MAX];
  uint16_t  salaNeutra;     // a sala de quem nao esta em grupo: nao e um grupo
};

inline void vizinhosInicia(Vizinhos& v, uint16_t salaNeutra)
{
  memset(&v, 0, sizeof(v));
  v.salaNeutra = salaNeutra;
}

// Um pacote de OUTRA sala chegou. 'nome' = nullptr se nao era roster.
inline void vizinhosOuve(Vizinhos& v, uint16_t sala, uint8_t slot, bool lider, const char* nome,
                         int16_t rssi, uint32_t agoraMs)
{
  if (sala == v.salaNeutra) return;
  int livre = -1, velho = 0;
  GrupoNoAr* g = nullptr;
  for (int i = 0; i < VIZ_MAX; i++) {
    if (v.g[i].ativo && v.g[i].sala == sala) { g = &v.g[i]; break; }
    if (!v.g[i].ativo && livre < 0) livre = i;
    if (v.g[i].ultMs < v.g[velho].ultMs) velho = i;
  }
  if (!g) {
    g = &v.g[livre >= 0 ? livre : velho];      // cheio: substitui o ouvido ha mais tempo
    memset(g, 0, sizeof(*g));
    g->ativo = true; g->sala = sala;
  }
  g->ultMs = agoraMs; g->rssi = rssi;
  if (slot < 16) { g->slots |= (uint16_t)(1u << slot); g->slotMs[slot] = agoraMs; }
  if (lider) g->temLider = true;
  if (nome && nome[0] && (lider || !g->nomeDoLider)) {
    strncpy(g->nome, nome, ROSTER_NOME_N); g->nome[ROSTER_NOME_N] = 0;
    g->temNome = true; g->nomeDoLider = lider;
  }
}

// Esquece quem calou. Devolve quantos grupos continuam no ar.
inline int vizinhosEnvelhece(Vizinhos& v, uint32_t agoraMs)
{
  int n = 0;
  for (int i = 0; i < VIZ_MAX; i++) {
    GrupoNoAr& g = v.g[i];
    if (!g.ativo) continue;
    if (tempoDesde(agoraMs, g.ultMs) > VIZ_TTL_MS) { g.ativo = false; continue; }
    for (int s = 0; s < 16; s++)
      if ((g.slots & (1u << s)) && tempoDesde(agoraMs, g.slotMs[s]) > VIZ_TTL_MS) g.slots &= (uint16_t)~(1u << s);
    n++;
  }
  return n;
}

inline int vizinhosCarros(const GrupoNoAr& g)
{
  int n = 0;
  for (int s = 0; s < 16; s++) if (g.slots & (1u << s)) n++;
  return n;
}

// A lista para a tela: grupos com LIDER primeiro, depois pelo sinal. Devolve n.
inline int vizinhosOrdena(const Vizinhos& v, int* idx, int max)
{
  int n = 0;
  for (int i = 0; i < VIZ_MAX && n < max; i++) if (v.g[i].ativo) idx[n++] = i;
  for (int a = 0; a < n; a++)
    for (int b = a + 1; b < n; b++) {
      const GrupoNoAr& A = v.g[idx[a]]; const GrupoNoAr& B = v.g[idx[b]];
      bool troca = (B.temLider && !A.temLider) || (B.temLider == A.temLider && B.rssi > A.rssi);
      if (troca) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    }
  return n;
}
