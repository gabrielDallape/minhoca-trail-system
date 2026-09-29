#pragma once
// ============================================================================
//  ORCAMENTO DE TEMPO - quem manda no laco
// ============================================================================
//
// As telas sao lacos que esperam o dedo, e o radio so vive porque o servico e
// chamado de dentro delas. Isso inverte a prioridade de um sistema de tempo
// real: a UI decidia quando o TDMA respirava, e um quadro de mapa frio (220-300
// ms) caindo em cima da janela de TX era o 'perdi' de 10-30 % medido no campo.
//
// AQUI A PRIORIDADE E INVERTIDA: quem quiser gastar tempo PERGUNTA quanto falta
// para a proxima janela (tdmaUsUntilMySlot) e so gasta se cabe. Nada mais
// bloqueia por conta propria. Sem hardware: so depende do struct Tdma.

#include <stdint.h>
#include "params.h"
#include "tdma.h"

struct Orcamento {
  uint32_t ultimoTickUs;      // carimbo da ultima volta completa
  uint32_t atrasoMaxUs;       // pior volta desde o ultimo relato
  uint32_t atrasoSomaUs;
  uint32_t atrasoN;
  uint32_t estouros;          // voltas que passaram do limite do regime
  uint32_t negados;           // pedidos recusados por falta de tempo
  uint32_t concedidos;
};

inline void orcamentoInicia(Orcamento& o)
{
  o.ultimoTickUs = 0; o.atrasoMaxUs = 0; o.atrasoSomaUs = 0; o.atrasoN = 0;
  o.estouros = 0; o.negados = 0; o.concedidos = 0;
}

// Uma vez por volta do laco. O limite de estouro e meia guarda do regime atual
// (holdover triplica): e o erro maximo que cada no pode introduzir.
inline void orcamentoVolta(Orcamento& o, uint32_t agoraUs, uint32_t limiteEstouroUs)
{
  if (o.ultimoTickUs) {
    uint32_t d = agoraUs - o.ultimoTickUs;      // wrap de 32 bits: a subtracao continua certa
    if (d > o.atrasoMaxUs) o.atrasoMaxUs = d;
    o.atrasoSomaUs += d; o.atrasoN++;
    if (d > limiteEstouroUs) o.estouros++;
  }
  o.ultimoTickUs = agoraUs;
}

inline uint32_t orcamentoMediaUs(const Orcamento& o) { return o.atrasoN ? (o.atrasoSomaUs / o.atrasoN) : 0; }
inline void orcamentoRelatou(Orcamento& o) { o.atrasoMaxUs = 0; o.atrasoSomaUs = 0; o.atrasoN = 0; }
inline uint32_t orcamentoLimiteUs(const Tdma& t) { return tdmaGuardNow(t) / 2; }

// Quanto posso gastar AGORA sem invadir a minha janela. TX em voo trava tudo: o
// TX_DONE pode chegar a qualquer instante e precisa ser servido.
inline uint32_t orcamentoUs(const Tdma& t, bool txEmVoo)
{
  if (txEmVoo) return 0;
  if (!t.haveAnchor) return t.frameUs;   // sem ancora nao ha janela a proteger
  uint32_t ate = tdmaUsUntilMySlot(t);
  if (ate <= ORC_RESERVA_US) return 0;
  return ate - ORC_RESERVA_US;
}

inline bool orcamentoCabe(Orcamento& o, const Tdma& t, bool txEmVoo, uint32_t custoUs)
{
  if (custoUs < ORC_MINIMO_US) custoUs = ORC_MINIMO_US;
  if (orcamentoUs(t, txEmVoo) >= custoUs) { o.concedidos++; return true; }
  o.negados++;
  return false;
}
