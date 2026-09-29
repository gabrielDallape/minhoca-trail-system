#pragma once
// ============================================================================
//  GEO - distancia, rumo e a extrapolacao de posicao entre pacotes
// ============================================================================
// Sem hardware. Para os poucos quilometros de uma trilha a projecao
// equirretangular basta; Mercator so paga a pena em escala de continente.

#include <math.h>
#include <stdint.h>

#define GEO_R_TERRA   6371000.0
#define GEO_M_POR_GRAU 111320.0
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline double geoHaversine(double la1, double lo1, double la2, double lo2)
{
  const double r = M_PI / 180.0;
  double dla = (la2 - la1) * r, dlo = (lo2 - lo1) * r;
  double a = sin(dla / 2) * sin(dla / 2)
           + cos(la1 * r) * cos(la2 * r) * sin(dlo / 2) * sin(dlo / 2);
  return 2 * GEO_R_TERRA * atan2(sqrt(a), sqrt(1 - a));
}

// Distancia ao QUADRADO em metros, sem trigonometria por ponto: serve para
// ORDENAR (achar o mais perto), que e o que o mapa faz 1200 vezes por quadro.
inline float geoD2(double la1, double lo1, double la2, double lo2)
{
  double dy = (la2 - la1) * GEO_M_POR_GRAU;
  double dx = (lo2 - lo1) * GEO_M_POR_GRAU * cos(la1 * M_PI / 180.0);
  return (float)(dx * dx + dy * dy);
}

// Rumo inicial de 1 para 2, em graus (0 = norte, 90 = leste).
inline float geoRumo(double la1, double lo1, double la2, double lo2)
{
  const double r = M_PI / 180.0;
  double dlo = (lo2 - lo1) * r;
  double y = sin(dlo) * cos(la2 * r);
  double x = cos(la1 * r) * sin(la2 * r) - sin(la1 * r) * cos(la2 * r) * cos(dlo);
  double b = atan2(y, x) / r;
  if (b < 0) b += 360.0;
  return (float)b;
}

// Anda 'metros' no 'rumo' a partir de (lat, lon). E a extrapolacao entre dois
// pacotes de 6 s: com o rumo e a velocidade do ultimo pacote, o marcador do
// outro carro continua andando na tela em vez de pular a cada quadro.
inline void geoAvanca(double lat, double lon, float rumoGraus, double metros,
                      double& lat2, double& lon2)
{
  const double r = rumoGraus * M_PI / 180.0;
  lat2 = lat + (metros * cos(r)) / GEO_M_POR_GRAU;
  lon2 = lon + (metros * sin(r)) / (GEO_M_POR_GRAU * cos(lat * M_PI / 180.0));
}

// Diferenca de rumo em (-180, 180]
inline float geoDeltaRumo(float de, float para)
{
  float d = para - de;
  while (d > 180.0f)  d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return d;
}
