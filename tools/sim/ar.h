#pragma once
// ============================================================================
//  AR - o que esta ENTRE as telas no simulador: posicao dos carros, propagacao,
//  colisao. Global (fora dos namespaces dos nos): e o unico meio de um no
//  saber do outro, como no mundo de verdade.
// ============================================================================
//
// PROPAGACAO, CALIBRADA NO CARRO (dados_campo/*_2026-09-30, antena DENTRO do
// carro, 10 dBm no chip = 20 dBm na antena):
//   medido:  ~50 m -> -60 dBm | ~350 m -> -84..-93 dBm | > ~450 m -> perde tudo
//   modelo:  log-distancia, PL(d) = 31,7 + 10 n log10(d), n = 3,2
//            -> 50 m: -66 | 350 m: -93 | 600 m: -100 dBm
//   ruido de fundo medido no carro: -88..-95 dBm (linha N do diario) -> -90
//   SF9 demodula ate SNR -12,5 dB -> RSSI minimo ~ -102,5, com desvanecimento
//   gaussiano de 5 dB por pacote: e o que faz a entrega cair aos poucos.
// COLISAO: dois pacotes que se sobrepoem no receptor - o mais forte sobrevive se
// estiver 6 dB acima (captura do LoRa); senao os dois se perdem.
// SURDEZ: um radio transmitindo nao recebe nada que se sobreponha ao seu TX.

#include <cstdint>
#include <cmath>
#include <vector>

namespace ar {

struct Cfg {
  double pl0 = 31.7;          // perda a 1 m, 915 MHz
  double n = 3.2;             // expoente (calibrado no carro, antena dentro)
  double ruidoDbm = -90.0;
  double snrMinDb = -12.5;    // SF9
  double fadingDb = 5.0;      // desvio do desvanecimento por pacote
  double ganhoPaDb = 10.0;    // E22: chip 10 dBm -> 20 na antena
  double capturaDb = 6.0;
  uint32_t semente = 1;
};
extern Cfg cfg;

// O cenario diz onde cada carro esta em cada instante GLOBAL (us).
struct Pose { double lat, lon, rumoGraus, velKmh; };
typedef Pose (*PosFn)(int no, uint64_t tG);
extern PosFn posicao;

struct Rx { std::vector<uint8_t> b; float rssi, snr; uint64_t fimG; int de; };

struct Est { uint64_t tx = 0, entregues = 0, alcance = 0, colisao = 0, surdoTx = 0, desligado = 0; };
extern Est est[8];             // por RECEPTOR (tx por transmissor)

void transmite(int de, uint64_t iniG, uint64_t durUs, const uint8_t* b, int n, int8_t pwrChipDbm);
// O que terminou no ar em (desdeG, ateG] e chega ao no 'para'. 'ligado' = o
// radio dele existe e responde.
void coleta(int para, bool ligado, uint64_t desdeG, uint64_t ateG, std::vector<Rx>& out);
double distanciaM(int a, int b, uint64_t tG);
void limpaVelhos(uint64_t antesDeG);
// O RSSI que o chip de 'para' le AGORA (GetRssiInst): o pacote mais forte no ar
// neste instante, ou -200 se ninguem fala. E o que a linha N do diario mede.
double rssiNoAr(int para, uint64_t tG);

}  // namespace ar
