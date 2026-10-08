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
  // revisao v201 (2026-10-08): orcamento, pintura, ruido, config
  uint32_t orcNegados, orcConcedidos, lacoPiorUs;
  uint32_t pinturas, pinturasNegadas;
  uint32_t ruidoN; float ruidoMax, ruidoMed;
  uint32_t conflitos, versaoErrada;
  uint8_t  cfgSlot, cfgEsquema;
  char     aviso[64];         // a faixa vermelha permanente (g_avisoTexto)
  uint32_t ppsRun, gpsBacklog, gpsParVelho, ancoras;   // disciplina do PPS e re-ancoras
  uint8_t  ancoraSrc;
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
  void     (*dio1Morto)(bool);          // o sinal do DIO1 deixa de chegar a placa
  // MODELO DE PINTURA do mapa: periodo, custo quente, custo frio, 1 frio a cada N
  // pinturas (0 = nunca), e se o pedido ao orcamento e o custo REAL ou o que a
  // tela_trilha.h pede (max do ultimo quadro e do pior recente, zerado a cada 16).
  void     (*pintura)(uint32_t periodoMs, uint32_t quenteUs, uint32_t frioUs, uint32_t frioCada, bool modoFirmware);
  // NVS "de fabrica" ANTES do boot: um aparelho que vem do v200 (esquema 2, slot 0..7)
  void     (*semeiaNvs)(uint8_t cfgv, uint8_t slot, const char* nome, uint8_t cor);
};
