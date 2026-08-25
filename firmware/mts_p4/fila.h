// A ROTA DO LIDER, e a fila que sai dela.
//
// Isto conserta o buraco que o firmware tinha desde o comeco: o g_trajeto e a
// MINHA trilha, montada do meu proprio GPS. Para o lider isso basta - o caminho
// dele e o rastro dele. Para um SEGUIDOR nao existia nada a frente: o roxo do
// mapa ficava vazio, e "seguir o lider" - que e o produto - nao funcionava.
//
// Agora ha duas linhas:
//   g_trajeto[]  onde EU passei          (meu GPS, azul, atras)
//   g_rota[]     onde o LIDER passou     (radio, roxo, a frente)
//
// E com a rota existindo, sai de graca a coisa mais util do aparelho: projetar
// cada carro nela da a QUILOMETRAGEM de cada um, e disso vem a ordem da fila e o
// aviso de quem saiu da trilha.
#pragma once
#include <math.h>

#define ROTA_MAX      1400        // ~7 km com passo de 5 m
#define ROTA_PASSO_M   5.0
#define FORA_DA_TRILHA_M 150.0    // alem disto o carro nao esta na trilha

static Ponto  g_rota[ROTA_MAX];
static float  g_rotaKm[ROTA_MAX];      // metros acumulados desde o inicio da rota
static int    g_rotaN = 0;

// DISTANCIA PLANA, EM METROS AO QUADRADO. A projecao roda para cada carro contra
// ate 1400 pontos, uma vez por quadro: com haversine seriam ~11 mil senos e
// cossenos por segundo. Em alguns quilometros de trilha a Terra e plana o
// bastante, e o que se compara aqui e QUAL ponto esta mais perto - nao a
// distancia em si, que so e calculada uma vez, no fim, e ai sim com haversine.
inline float rotaD2(double la1, double lo1, double la2, double lo2)
{
  const double k = 111320.0;
  double dy = (la2 - la1) * k;
  double dx = (lo2 - lo1) * k * cos(la1 * M_PI / 180.0);
  return (float)(dx * dx + dy * dy);
}

// Acrescenta um ponto a rota do lider. Chamado quando chega pacote de quem tem a
// bandeira de lider. So grava se andou o passo minimo - lider parado no
// checkpoint encheria o buffer de pontos identicos e comeria a rota inteira.
inline void rotaAdiciona(double lat, double lon)
{
  if (g_rotaN > 0) {
    const Ponto& u = g_rota[g_rotaN - 1];
    double d = sqrt(rotaD2(u.lat, u.lon, lat, lon));
    if (d < ROTA_PASSO_M) return;
    if (g_rotaN < ROTA_MAX) g_rotaKm[g_rotaN] = g_rotaKm[g_rotaN - 1] + (float)d;
  } else {
    g_rotaKm[0] = 0;
  }
  if (g_rotaN < ROTA_MAX) {
    g_rota[g_rotaN] = { lat, lon };
    g_rotaN++;
  } else {
    // Buffer cheio: descarta o comeco. A rota longe demais atras nao ajuda mais
    // ninguem a seguir, e a alternativa - parar de gravar - congelaria o caminho
    // do lider justamente quando ele esta mais longe.
    memmove(g_rota,   g_rota + 1,   (ROTA_MAX - 1) * sizeof(Ponto));
    memmove(g_rotaKm, g_rotaKm + 1, (ROTA_MAX - 1) * sizeof(float));
    g_rota[ROTA_MAX - 1] = { lat, lon };
    g_rotaKm[ROTA_MAX - 1] = g_rotaKm[ROTA_MAX - 2]
                           + (float)sqrt(rotaD2(g_rota[ROTA_MAX - 2].lat,
                                                g_rota[ROTA_MAX - 2].lon, lat, lon));
  }
}

inline void rotaLimpa() { g_rotaN = 0; }

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
