#pragma once
// ============================================================================
//  PROTO - o pacote no ar, VERSIONADO
// ============================================================================
//
// Todo pacote comeca com o byte de versao do protocolo. Um no que ouve versao
// diferente ignora o pacote E conta/avisa: no campo de 2026 houve telas em v7 e
// v8 na mesma trilha e ninguem sabia por que nao se viam. Agora a tela diz.
//
// CABECALHO (16 bytes, little-endian) - o mesmo para posicao, trilha e roster:
//   [0]      PROTO_VERSAO
//   [1..2]   sala (hash de 16 bits do codigo do grupo)
//   [3]      slot TDMA = identidade do remetente (0..FROTA_MAX_CARROS-1)
//   [4]      seq rolante (perda por remetente)
//   [5]      flags: FIX ALERTA LIDER PPS . . TRILHA ROSTER
//   [6..9]   lat  int32 grau x 1e7   (posicao) | [6] cor, [7..22] nome (roster)
//   [10..13] lon  int32 grau x 1e7
//   [14]     rumo em graus/2
//   [15]     velocidade km/h
//   [16..]   cauda de trilha (core/rota.h) quando FL_TRILHA
//
// Por que a sala encolheu de 24 para 16 bits: o byte que sobrou virou a versao.
// Dois grupos na mesma trilha com o mesmo hash de 16 bits = 1 em 65536, e o
// unico efeito seria ver o carro do outro grupo no mapa.
//
// SEM Arduino: pack/unpack sao inteiros puros, testados no PC.

#include <stdint.h>
#include <string.h>
#include "params.h"

#define PROTO_CAB_N     16
#define PROTO_ROSTER_N  (7 + ROSTER_NOME_N)     // 23 bytes

#define FL_FIX      0x01
#define FL_ALERTA   0x02
#define FL_LIDER    0x04
#define FL_PPS      0x08     // "meu relogio esta na grade UTC" (fonte preferida)
#define FL_TRILHA   0x40     // ha cauda de trilha a partir do byte 16
#define FL_ROSTER   0x80     // este pacote e um roster, nao uma posicao

struct ProtoPos {
  uint16_t sala;
  uint8_t  slot, seq, flags;
  double   lat, lon;
  float    rumo;       // graus, 0 = norte
  uint8_t  vel;        // km/h
};

inline void protoPutLE32(uint8_t* p, int32_t v)
{
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
inline int32_t protoGetLE32(const uint8_t* p)
{
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}
inline void protoPutLE16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
inline uint16_t protoGetLE16(const uint8_t* p)  { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

// O codigo do grupo vira 16 bits (FNV-1a de 32 dobrado). E este campo que impede um
// carro de aparecer no mapa do grupo errado - sync word e frequencia sao iguais.
inline uint16_t protoSalaHash(const char* cod)
{
  uint32_t h = 2166136261u;
  for (const char* p = cod; p && *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return (uint16_t)((h ^ (h >> 16)) & 0xFFFF);
}

inline void protoCabecalho(uint8_t* p, uint16_t sala, uint8_t slot, uint8_t seq, uint8_t flags)
{
  p[0] = PROTO_VERSAO;
  protoPutLE16(p + 1, sala);
  p[3] = slot; p[4] = seq; p[5] = flags;
}

inline int protoPackPos(uint8_t* p, const ProtoPos& k)
{
  protoCabecalho(p, k.sala, k.slot, k.seq, (uint8_t)(k.flags & ~FL_ROSTER));
  protoPutLE32(p + 6,  (int32_t)(k.lat * 1e7));
  protoPutLE32(p + 10, (int32_t)(k.lon * 1e7));
  float h = k.rumo;
  while (h < 0) h += 360.0f;
  while (h >= 360.0f) h -= 360.0f;
  p[14] = (uint8_t)(h / 2.0f);     // graus/2 cabe em 1 byte (resolucao de 2 graus)
  p[15] = k.vel;
  return PROTO_CAB_N;
}

// So o cabecalho: versao, tamanho e slot valido. NAO confere a sala - quem chama
// decide se o pacote de outra sala e contado ou descartado.
inline uint8_t protoVersao(const uint8_t* p, int len) { return len >= 1 ? p[0] : 0; }
inline bool protoVersaoOk(const uint8_t* p, int len) { return len >= PROTO_CAB_N && p[0] == PROTO_VERSAO; }
inline uint16_t protoSala(const uint8_t* p) { return protoGetLE16(p + 1); }
inline bool protoEhRoster(const uint8_t* p, int len) { return len >= PROTO_ROSTER_N && (p[5] & FL_ROSTER); }

inline bool protoUnpackPos(const uint8_t* p, int len, ProtoPos& k)
{
  if (!protoVersaoOk(p, len)) return false;
  if (p[5] & FL_ROSTER) return false;
  k.sala  = protoGetLE16(p + 1);
  k.slot  = p[3];
  k.seq   = p[4];
  k.flags = p[5];
  if (k.slot >= FROTA_MAX_CARROS) return false;
  k.lat = protoGetLE32(p + 6)  / 1e7;
  k.lon = protoGetLE32(p + 10) / 1e7;
  k.rumo = p[14] * 2.0f;
  k.vel  = p[15];
  return true;
}

// ---------------------------------------------------------------- roster
// QUEM E CADA SLOT. O pacote de posicao nao tem espaco para nome, entao vai um
// pacote separado, no MESMO slot, intercalado com a posicao (ROSTER_CADA).
// Nome e cor sao do MTS, nao do TDMA - por isso moram aqui e nao no tdma_core.
inline int protoPackRoster(uint8_t* p, uint16_t sala, uint8_t slot, uint8_t seq,
                           uint8_t cor, bool lider, const char* nome)
{
  protoCabecalho(p, sala, slot, seq, (uint8_t)(FL_ROSTER | (lider ? FL_LIDER : 0)));
  p[6] = cor;
  memset(p + 7, 0, ROSTER_NOME_N);
  strncpy((char*)(p + 7), nome, ROSTER_NOME_N);
  return PROTO_ROSTER_N;
}

// O nome chega de OUTRO aparelho: texto de procedencia desconhecida indo direto
// para a tela. So passa o imprimivel, e o corte garante o terminador.
inline void protoRosterNome(const uint8_t* p, char* destino, size_t n)
{
  size_t i = 0;
  for (; i + 1 < n && i < ROSTER_NOME_N; i++) {
    char c = (char)p[7 + i];
    if (c == 0) break;
    destino[i] = (c >= 32 && c < 127) ? c : '?';
  }
  destino[i] = 0;
  if (!destino[0]) { strncpy(destino, "sem nome", n - 1); destino[n - 1] = 0; }
}
inline uint8_t protoRosterCor(const uint8_t* p) { return p[6]; }

static_assert(PROTO_ROSTER_N == 23, "roster mudou de tamanho: refaca a conta de air-time");
