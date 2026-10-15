// A ROTA DO LIDER, e a fila que sai dela.
//
// Isto conserta o buraco que o firmware tinha desde o comeco: o g_trajeto e a
// MINHA trilha, montada do meu proprio GPS. Para o lider isso basta - o caminho
// dele e o rastro dele. Para um SEGUIDOR nao existia nada a frente: o roxo do
// mapa ficava vazio, e "seguir o lider" - que e o produto - nao funcionava.
//
// Agora ha duas linhas:
//   g_trajeto[]  onde EU passei          (meu GPS, azul, atras)
//   g_rota       onde o LIDER passou     (radio, roxo, a frente)
//
// E com a rota existindo, sai de graca a coisa mais util do aparelho: projetar
// cada carro nela da a QUILOMETRAGEM de cada um, e disso vem a ordem da fila e o
// aviso de quem saiu da trilha.
//
// DESDE A v101 A ROTA E NUMERADA E ORDENADA (rota_core.h): cada ponto traz o
// numero que o lider lhe deu, os que chegam atrasados pelo catch-up entram no
// lugar certo, e um salto de numero e um vao conhecido que o mapa tracejа. A
// logica mora no rota_core.h, sem hardware, para poder ser testada no PC.
#pragma once
#include <math.h>
#include "rota_core.h"

#define FORA_DA_TRILHA_M 150.0    // alem disto o carro nao esta na trilha

static Rota g_rotaC;
// Os nomes antigos continuam valendo para quem desenha (mapa.h) e para o resumo
// de "mudou?" do grupo.h - assim a troca do miolo nao espalha edicao por tudo.
#define g_rota    (g_rotaC.p)
#define g_rotaKm  (g_rotaC.km)
#define g_rotaN   (g_rotaC.n)

// Chegou um ponto numerado do lider (do bloco de trilha do pacote). A epoca
// distingue um lider que reiniciou; o resto e o rota_core.
inline void rotaRecebePonto(uint8_t epoca, uint16_t seq16, double lat, double lon)
{
  uint32_t v = rotaSeqVirtual(g_rotaC, epoca, seq16);
  rotaInsere(g_rotaC, v, lat, lon);
}

inline void rotaLimpa() { rotaLimpaCore(g_rotaC); }

// Onde este ponto cai na rota. Devolve false se a rota ainda nao existe.
//   idx    = indice do ponto mais proximo
//   metros = distancia ATE a rota (nao ao longo dela)
inline bool rotaProjeta(double lat, double lon, int& idx, float& metros)
{
  if (g_rotaN < 2) return false;
  float melhor = 1e18f; int mi = 0;
  for (int i = 0; i < g_rotaN; i++) {
    float d2 = rotaD2(lat, lon, g_rota[i].lat, g_rota[i].lon);
    if (d2 < melhor) { melhor = d2; mi = i; }
  }
  idx = mi;
  metros = sqrtf(melhor);
  return true;
}

// Quilometragem de um carro na trilha. Devolve -1 se nao da para dizer.
inline float filaKm(int carro)
{
  if (!g_carros[carro].ativo || !g_carros[carro].fix) return -1;
  int idx; float fora;
  if (!rotaProjeta(g_carros[carro].lat, g_carros[carro].lon, idx, fora)) return -1;
  return g_rotaKm[idx];
}

inline float filaMinhaKm()
{
  if (!g_meuFix) return -1;
  int idx; float fora;
  if (!rotaProjeta(g_meuLat, g_meuLon, idx, fora)) return -1;
  return g_rotaKm[idx];
}

// ESTE E O AVISO MAIS UTIL DO APARELHO, e ele nasce de tapar um furo.
//
// A projecao acha o ponto mais proximo da rota SEMPRE - inclusive para um carro
// que pegou a estrada paralela. Sem limiar, ele apareceria na fila numa posicao
// inventada. Com limiar, o erro vira informacao: "o Tuninho saiu da trilha ha
// 400 m", que e o que diz para onde voltar.
inline bool filaForaDaTrilha(int carro, float& metrosFora)
{
  if (!g_carros[carro].ativo || !g_carros[carro].fix) return false;
  int idx;
  if (!rotaProjeta(g_carros[carro].lat, g_carros[carro].lon, idx, metrosFora)) return false;
  return metrosFora > FORA_DA_TRILHA_M;
}

// A ORDEM VEM DA QUILOMETRAGEM, nunca da distancia em linha reta.
//
// Numa curva de retorno um carro 200 m atras NO CAMINHO pode estar a 30 m de voce
// em linha reta, na curva de cima. Ordenando por linha reta ele apareceria a sua
// frente, e numa serra com dez curvas dessas a fila inteira embaralharia - o
// recurso pareceria quebrado sem ninguem entender por que.
//
// Preenche 'ordem' com os indices dos carros, do mais adiantado para o mais
// atrasado, e devolve quantos entraram. O proprio aparelho entra como -1.
inline int filaOrdena(int* ordem, int max, int& minhaPos)
{
  struct { int i; float km; } v[MUNDO_MAX_CARROS + 1];
  int n = 0;
  for (int i = 0; i < MUNDO_MAX_CARROS && n < MUNDO_MAX_CARROS; i++) {
    if (i != 0 && !g_carros[i].ativo) continue;
    float km = (i == 0) ? filaMinhaKm() : filaKm(i);
    if (km < 0) continue;
    float fora;
    if (i != 0 && filaForaDaTrilha(i, fora)) continue;   // fora da trilha nao tem lugar na fila
    v[n].i = i; v[n].km = km; n++;
  }
  // insercao: n <= 8, e a lista ja chega quase ordenada de um quadro para o outro
  for (int a = 1; a < n; a++) {
    auto t = v[a]; int b = a - 1;
    while (b >= 0 && v[b].km < t.km) { v[b + 1] = v[b]; b--; }
    v[b + 1] = t;
  }
  minhaPos = -1;
  int fora = 0;
  for (int a = 0; a < n && a < max; a++) {
    ordem[a] = v[a].i;
    if (v[a].i == 0) minhaPos = a;
    fora++;
  }
  return fora;
}
