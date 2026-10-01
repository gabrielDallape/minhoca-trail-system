#pragma once
// A interface de UMA tela simulada, vista de fora (sim.cpp). Cada no.cpp,
// compilado com -DNO_NS=noK, exporta um NoApi chamado api_noK.
#include <cstdint>

// O que a tela MOSTRARIA e o que o radio fez - tudo lido do firmware de verdade.
struct NoObs {
  int      carros;            // frotaQuantos: eu + os outros ativos
  bool     liderNaFrota;      // ha um LIDER ativo na frota
  bool     liderDesenhado;    // frotaParaDesenhar(lider): o icone vai para o mapa
  bool     liderVazado;       // ... vazado (sinal velho)
  int      rotaN;             // pontos do caminho do lider na minha rota
  int      rotaVaos;          // vaos conhecidos ACIMA do piso (os que ainda dao para fechar)
  uint16_t ack, topoRota;
  uint16_t trajHead; int trajN; // MEU rastro (o lider transmite isto)
  uint32_t rx, tx, txPreso, txPerdidos;
  int      sync;              // 0 nada, 1 beacon, 2 PPS
  bool     ancora, holdover;
  bool     fix;
  int      vizN; char vizNome[24]; bool vizLider; uint16_t vizSala;
  bool     radioOk, naTrilha, lider;
  uint16_t sala;
  bool     euAlerta;          // o MEU botao de alerta
  int      alertasVistos;     // quantos OUTROS carros a minha tela mostra em alerta
  uint32_t liderSilencioMs;   // ha quanto tempo nao ouco o lider
  double   distLiderM;        // distancia do icone do lider ate mim, como desenhado
};

struct NoApi {
  void     (*boot)(int no, uint64_t bootG, const char* nome, uint8_t slot, uint8_t cor, double ppm, const char* dir);
  void     (*religa)(uint64_t g);               // desligar e ligar (a RAM some, a NVS fica)
  uint64_t (*agoraG)();
  void     (*passo)();                          // uma volta do laco (servico + a tela da trilha)
  void     (*energiaRadio)(bool);               // o 5 V do E22
  void     (*gpsFix)(bool);
  void     (*criar)(const char* cod);           // CRIAR GRUPO -> sala de espera
  void     (*abrir)();                          // ABRIR TRILHA
  bool     (*entrarLista)();                    // ENTRAR -> toca no primeiro cartao da lista
  void     (*entrarCodigo)(const char* cod);    // ENTRAR -> DIGITAR CODIGO
  void     (*sair)();
  bool     (*retomar)();                        // RETOMAR na tela inicial
  void     (*obs)(NoObs&);
  void     (*fecha)();
  void     (*alerta)(bool);             // o botao de ALERTA da trilha
};
