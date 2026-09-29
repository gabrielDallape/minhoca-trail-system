#pragma once
// ============================================================================
//  FILA - a projecao dos carros na rota do lider: quilometragem e "fora da trilha"
// ============================================================================
//
// Duas linhas no mapa:
//   g_traj   onde EU passei          (meu GPS, azul, atras)
//   g_rotaC  onde o LIDER passou     (radio, roxo, a frente; core/rota.h)
//
// Com a rota existindo, sai de graca a coisa mais util do aparelho: projetar
// cada carro nela da a QUILOMETRAGEM de cada um, e disso vem a ordem da fila e o
// aviso de quem saiu da trilha. A ORDEM VEM DA QUILOMETRAGEM, nunca da distancia
// em linha reta: numa curva de retorno um carro 200 m atras NO CAMINHO pode
// estar a 30 m de voce em linha reta.

#include <math.h>
#include "../estado.h"
#include "../../core/geo.h"

// Onde este ponto cai na rota. false se a rota ainda nao existe.
inline bool rotaProjeta(double lat, double lon, int& idx, float& metros)
{
  if (g_rotaC.n < 2) return false;
  float melhor = 1e30f; int mi = 0;
  for (int i = 0; i < g_rotaC.n; i++) {
    float d2 = rotaD2(lat, lon, g_rotaC.p[i].lat, g_rotaC.p[i].lon);
    if (d2 < melhor) { melhor = d2; mi = i; }
  }
  idx = mi;
  metros = sqrtf(melhor);
  return true;
}

// Quilometragem de um carro na trilha (posicao ESTIMADA). -1 se nao da para dizer.
inline float filaKm(int carro, uint32_t agoraMs)
{
  double la, lo; bool ext;
  if (!frotaEstimativa(g_frota.c[carro], agoraMs, la, lo, ext)) return -1;
  int idx; float fora;
  if (!rotaProjeta(la, lo, idx, fora)) return -1;
  return g_rotaC.km[idx];
}

inline float filaMinhaKm()
{
  if (!g_eu.fix) return -1;
  int idx; float fora;
  if (!rotaProjeta(g_eu.lat, g_eu.lon, idx, fora)) return -1;
  return g_rotaC.km[idx];
}

// A projecao acha o ponto mais proximo SEMPRE - inclusive para um carro que
// pegou a estrada paralela. Com limiar, o erro vira informacao: "saiu da trilha".
inline bool filaForaDaTrilha(int carro, uint32_t agoraMs, float& metrosFora)
{
  double la, lo; bool ext;
  if (!frotaEstimativa(g_frota.c[carro], agoraMs, la, lo, ext)) return false;
  int idx;
  if (!rotaProjeta(la, lo, idx, metrosFora)) return false;
  return metrosFora > FORA_DA_TRILHA_M;
}
