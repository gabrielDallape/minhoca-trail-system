#pragma once
// ============================================================================
//  TRAJETO - o MEU rastro, numerado (o que o lider transmite e o seguidor pinta)
// ============================================================================
// Anel de pontos com passo minimo (ROTA_PASSO_M): um carro parado nao enche o
// buffer de pontos identicos. Cada ponto gravado ganha o proximo numero de 16
// bits; a epoca (sorteada no boot pelo app) diz a quem ouve que a numeracao
// recomecou. Sem hardware.

#include <stdint.h>
#include "params.h"
#include "geo.h"

struct TrajPonto { double lat, lon; };

struct Trajeto {
  TrajPonto p[TRAJETO_MAX];
  int       n, cab;
  uint16_t  seqUlt;       // numero do ultimo ponto (0xFFFF + primeiro = 0)
  uint8_t   epoca;
  bool      temUlt;
  double    ultLat, ultLon;
};

inline void trajetoInicia(Trajeto& t, uint8_t epoca)
{
  t.n = t.cab = 0; t.seqUlt = 0xFFFF; t.epoca = epoca; t.temUlt = false;
  t.ultLat = t.ultLon = 0;
}

inline void trajetoLimpa(Trajeto& t) { t.n = t.cab = 0; t.temUlt = false; }

// So grava se andou ROTA_PASSO_M. Devolve true se gravou.
inline bool trajetoTalvezAdicione(Trajeto& t, double lat, double lon)
{
  if (t.temUlt && geoHaversine(t.ultLat, t.ultLon, lat, lon) < ROTA_PASSO_M) return false;
  t.p[t.cab] = { lat, lon };
  t.cab = (t.cab + 1) % TRAJETO_MAX;
  if (t.n < TRAJETO_MAX) t.n++;
  t.seqUlt++;
  t.ultLat = lat; t.ultLon = lon; t.temUlt = true;
  return true;
}

inline const TrajPonto& trajetoEm(const Trajeto& t, int i)   // 0 = mais antigo
{
  return t.p[(t.cab - t.n + i + TRAJETO_MAX) % TRAJETO_MAX];
}

// O ponto com o numero seq16, para o bloco de trilha. false se ja nao o guardo.
inline bool trajetoPontoSeq(const Trajeto& t, uint16_t seq16, double& lat, double& lon)
{
  if (t.n <= 0) return false;
  int atras = (int)(uint16_t)(t.seqUlt - seq16);    // 0 = o mais novo
  if (atras < 0 || atras >= t.n) return false;
  const TrajPonto& p = trajetoEm(t, t.n - 1 - atras);
  lat = p.lat; lon = p.lon;
  return true;
}

// Qual ponto do trajeto esta mais perto de (lat,lon): onde eu estou no caminho.
// Sem trigonometria por ponto (geoD2), porque roda a cada quadro.
inline int trajetoMaisPerto(const Trajeto& t, double lat, double lon)
{
  int melhor = -1; float d = 1e30f;
  for (int i = 0; i < t.n; i++) {
    const TrajPonto& p = trajetoEm(t, i);
    float dd = geoD2(lat, lon, p.lat, p.lon);
    if (dd < d) { d = dd; melhor = i; }
  }
  return melhor;
}
