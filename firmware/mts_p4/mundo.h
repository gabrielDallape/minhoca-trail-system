// O MUNDO: onde estao os carros e por onde o lider passou.
//
// Esta e a fronteira entre o que a tela desenha e de onde o dado vem. Hoje quem
// preenche isto e um simulador; amanha e o GPS e o radio. A tela nao sabe a
// diferenca - e esse o ponto.
//
// Por que nao reusei o trilha_core.h das telas S3: ele carrega o protocolo
// LoRaMESH inteiro (packWorld/packRoster/SLOT_MS), e o P4 vai de TDMA sobre
// SX1262. O que se aproveita dele e a MATEMATICA, que esta reescrita aqui igual:
// haversine e o passo minimo de 5 m para nao encher o trajeto de pontos parados.
#pragma once
#include <math.h>
#include "hardware.h"

#define MUNDO_MAX_CARROS  8
#define TRAJETO_MAX     1200      // ~6 km guardados, com passo de 5 m
#define TRAJETO_PASSO_M    5.0

struct Carro {
  bool     ativo;
  double   lat, lon;
  bool     fix;
  bool     alerta;
  bool     lider;
  uint8_t  cor;
  char     nome[16];
  uint32_t ultimoMs;             // quando falou pela ultima vez
};

struct Ponto { double lat, lon; };

// estado global do aparelho
static Carro  g_carros[MUNDO_MAX_CARROS];
static Ponto  g_trajeto[TRAJETO_MAX];
static int    g_trajN = 0, g_trajCab = 0;
static double g_meuLat = 0, g_meuLon = 0;
static bool   g_meuFix = false;
static int    g_meuSlot = 0;
static uint8_t g_sats = 0;

// --------------------------------------------------------------- geometria
inline double haversine(double la1, double lo1, double la2, double lo2)
{
  const double R = 6371000.0, r = M_PI / 180.0;
  double dla = (la2 - la1) * r, dlo = (lo2 - lo1) * r;
  double a = sin(dla / 2) * sin(dla / 2)
           + cos(la1 * r) * cos(la2 * r) * sin(dlo / 2) * sin(dlo / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

// Projecao equirretangular, centrada em MIM. Para os poucos quilometros de uma
// trilha o erro e desprezivel e o custo e uma multiplicacao - projecao de verdade
// (Mercator) so paga a pena quando o mapa cobre continentes.
//
// NORTH-UP: o mapa nao gira; so o icone do carro. Girar dezenas de tiles por
// software mataria o FPS, e o acelerador do P4 so rotaciona em 90 graus.
inline void paraTela(double lat, double lon, int cx, int cy, double mPorPx,
                     int& x, int& y)
{
  const double R = 6371000.0, r = M_PI / 180.0;
  double dx = (lon - g_meuLon) * r * R * cos(g_meuLat * r);
  double dy = (lat - g_meuLat) * r * R;
  x = cx + (int)(dx / mPorPx);
  y = cy - (int)(dy / mPorPx);          // norte para cima
}

// --------------------------------------------------------------- trajeto
// So grava se andou TRAJETO_PASSO_M. Sem isso, um carro parado enche o buffer de
// pontos identicos e come os 6 km de historico em minutos.
inline void trajetoTalvezAdicione(double lat, double lon)
{
  static bool  temUlt = false;
  static double ultLat = 0, ultLon = 0;
  if (temUlt && haversine(ultLat, ultLon, lat, lon) < TRAJETO_PASSO_M) return;
  g_trajeto[g_trajCab] = { lat, lon };
  g_trajCab = (g_trajCab + 1) % TRAJETO_MAX;
  if (g_trajN < TRAJETO_MAX) g_trajN++;
  ultLat = lat; ultLon = lon; temUlt = true;
}

inline const Ponto& trajetoEm(int i)      // 0 = mais antigo
{
  return g_trajeto[(g_trajCab - g_trajN + i + TRAJETO_MAX) % TRAJETO_MAX];
}

inline int distanciaAte(int i)
{
  if (!g_carros[i].ativo || !g_carros[i].fix || !g_meuFix) return -1;
  return (int)haversine(g_meuLat, g_meuLon, g_carros[i].lat, g_carros[i].lon);
}

// ------------------------------------------------------------------ fonte
// TUDO que le hardware entra por aqui. Enquanto as chaves do hardware.h estao em
// zero, quem responde e o simulador - e ele anda de verdade, tracando uma trilha
// curva, para o mapa poder ser conferido sem sair de casa.
inline void mundoInicia(const char* meuNome, uint8_t minhaCor, bool souLider)
{
  for (int i = 0; i < MUNDO_MAX_CARROS; i++) g_carros[i] = Carro();
  g_meuSlot = 0;
  strncpy(g_carros[0].nome, meuNome, 15);
  g_carros[0].cor = minhaCor;
  g_carros[0].lider = souLider;
  g_carros[0].ativo = true;
  g_trajN = g_trajCab = 0;

#if !MTS_TEM_GPS
  // um ponto de partida qualquer, so para o simulador ter de onde sair
  g_meuLat = -23.5505; g_meuLon = -46.6333;
  g_meuFix = true; g_sats = 9;
#endif
}

inline void mundoAtualiza()
{
#if MTS_TEM_GPS
  // TODO: TinyGPSPlus no Serial (PIN_GPS_RX), e a borda do PPS carimbada por
  // hardware (GPIO -> ETM -> GPTIMER_ETM_TASK_CAPTURE, 12,5 ns, sem CPU) para
  // alimentar tdmaOnPps(). Ver docs/MONTAGEM.md passo 8.
#else
  // simulador: eu ando numa curva suave e os outros me seguem atrasados
  // 1 Hz: a taxa de um GPS de verdade. Antes corria a 5 Hz e obrigava o mapa a
  // repintar cinco vezes por segundo - metade da piscada vinha daqui.
  static uint32_t t0 = 0;
  static double ang = 0;
  if (millis() - t0 < 1000) return;
  t0 = millis();
  ang += 0.07;
  g_meuLat += cos(ang) * 0.00007;
  g_meuLon += sin(ang * 0.7) * 0.00009;
  g_meuFix = true;
#endif

  if (g_meuFix) {
    g_carros[0].lat = g_meuLat; g_carros[0].lon = g_meuLon; g_carros[0].fix = true;
    trajetoTalvezAdicione(g_meuLat, g_meuLon);
  }

#if !MTS_TEM_RADIO
  // os outros carros seguem o meu trajeto, cada um mais atrasado que o anterior -
  // que e exatamente o que se ve numa trilha de verdade
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
    if (!g_carros[i].ativo) continue;
    int atraso = i * 14;
    int idx = g_trajN - 1 - atraso;
    if (idx < 0) idx = 0;
    if (g_trajN > 0) {
      const Ponto& p = trajetoEm(idx);
      g_carros[i].lat = p.lat; g_carros[i].lon = p.lon;
      g_carros[i].fix = true;
      g_carros[i].ultimoMs = millis();
    }
  }
#endif
}

// Entra um carro novo (hoje pelo simulador da sala de espera; amanha pelo JOIN)
inline int mundoEntra(const char* nome, uint8_t cor)
{
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
    if (g_carros[i].ativo) continue;
    g_carros[i] = Carro();
    strncpy(g_carros[i].nome, nome, 15);
    g_carros[i].cor = cor;
    g_carros[i].ativo = true;
    g_carros[i].ultimoMs = millis();
    return i;
  }
  return -1;
}

inline int mundoQuantos()
{
  int n = 0;
  for (int i = 0; i < MUNDO_MAX_CARROS; i++) if (g_carros[i].ativo) n++;
  return n;
}
