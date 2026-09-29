#pragma once
// ============================================================================
//  ORCAMENTO DE TEMPO - quem manda no laco
// ============================================================================
//
// POR QUE ESTE ARQUIVO EXISTE
//
// No mts_p4 as TELAS sao donas do tempo: cada uma e um while(true) proprio, e o
// radio so continua vivo porque servicoDeFundo() esta pendurado dentro do
// esperaToque() (ui.h). Isso inverte a prioridade de um sistema de tempo real -
// a UI decide quando o TDMA respira.
//
// O preco disso esta medido no proprio codigo:
//
//   janela util de TX ................ 55.000 us   (slot 250ms - airtime - guarda)
//   janela util em HOLDOVER .......... 15.000 us   (a guarda triplica)
//   um quadro de mapa com cache frio . 220.000 a 300.000 us
//   WiFi.scanNetworks() x2 ........... 6.000.000 us
//
// Um quadro frio perde a janela inteira. Dai vem o 'perdi', e quando o TX fica
// pela metade, o 'preso' - que ate a v100, com o recuo automatico, derrubava 4 dB
// e GRAVAVA NA NVS. Foi assim que as duas telas voltaram do campo de 2026-09-10
// em 10 dBm tendo saido em 14 (o recuo nao existe mais - hardware.h).
//
// AQUI A PRIORIDADE E INVERTIDA: o laco serve o radio primeiro, pergunta quanto
// tempo sobra ate a proxima janela, e so entao entrega esse resto para a tela.
// Quem quiser gastar tempo PEDE. Nada mais bloqueia por conta propria.
//
// A peca central ja existia e nunca tinha sido chamada por ninguem:
// tdmaUsUntilMySlot() (tdma_core.h:561). Ela sabe exatamente quantos
// microssegundos faltam ate a janela. Todo este arquivo e a consequencia de
// finalmente perguntar isso antes de gastar.
//
// SEM HARDWARE: so depende do struct Tdma. Da para exercitar no PC.

#include <stdint.h>
#include "../tdma_core.h"

// ---------------------------------------------------------------- margens
//
// RESERVA: o que NAO se entrega a ninguem, mesmo quando ha tempo. Cobre o custo
// do proprio radioAtualiza no ciclo seguinte (SPI do startTransmit, standby,
// radioBusyLivre que faz spin de ate 5 ms) mais a imprecisao de quem estimou
// errado o proprio custo. 8 ms e o dobro do pior caso medido do caminho de TX.
#define ORC_RESERVA_US      8000UL

// Abaixo disto nao vale a pena comecar nada: o custo de decidir ja e maior que
// a fatia. A tela devolve o controle e tenta no proximo tick.
#define ORC_MINIMO_US       2000UL

// Escrita no cartao so com MUITO espaco morto. Um microSD de consumo pode
// entrar em garbage collection interno e segurar centenas de ms numa unica
// chamada - e nao ha como saber antes. 300 ms e a folga que absorve um GC tipico
// sem encostar no vigia de 400 ms do radio.h:485.
#define ORC_CARTAO_US     300000UL

// ------------------------------------------------------- estado observado
//
// Estatistica do proprio laco. E o unico numero que o projeto NUNCA teve: hoje
// o atraso do laco so aparece indiretamente, como missedTx depois do estrago.
// Medir aqui e o que permite provar (ou refutar) que a reescrita funcionou.
struct Orcamento {
  uint32_t ultimoTickUs;      // carimbo da ultima volta completa
  uint32_t atrasoMaxUs;       // pior volta desde o ultimo relato
  uint32_t atrasoSomaUs;      // para a media
  uint32_t atrasoN;
  uint32_t estouros;          // voltas que passaram do orcamento de holdover
  uint32_t negados;           // pedidos recusados por falta de tempo
  uint32_t concedidos;
};

inline void orcamentoInicia(Orcamento& o) {
  o.ultimoTickUs = 0; o.atrasoMaxUs = 0; o.atrasoSomaUs = 0; o.atrasoN = 0;
  o.estouros = 0; o.negados = 0; o.concedidos = 0;
}

// Chamado UMA VEZ por volta do laco, no fim. Mede quanto a volta inteira levou.
//
// O limite de estouro e a janela de HOLDOVER (15 ms), nao a normal (55 ms), de
// proposito: holdover e o regime em que a rede esta fragil - GPS sem fix, ancora
// velha - e e exatamente quando o carro esta na mata. Dimensionar pelo caso bom
// seria dimensionar para o caso em que nada importa.
inline void orcamentoVolta(Orcamento& o, uint32_t agoraUs, uint32_t limiteEstouroUs) {
  if (o.ultimoTickUs) {
    uint32_t d = agoraUs - o.ultimoTickUs;      // wrap de 32 bits: subtracao continua certa
    if (d > o.atrasoMaxUs) o.atrasoMaxUs = d;
    o.atrasoSomaUs += d; o.atrasoN++;
    if (d > limiteEstouroUs) o.estouros++;
  }
  o.ultimoTickUs = agoraUs;
}

inline uint32_t orcamentoMediaUs(const Orcamento& o) {
  return o.atrasoN ? (o.atrasoSomaUs / o.atrasoN) : 0;
}

// Zera a janela de estatistica sem perder os acumuladores de falha. Os estouros
// e negados NAO sao zerados: eles sao a conta da viagem inteira.
inline void orcamentoRelatou(Orcamento& o) {
  o.atrasoMaxUs = 0; o.atrasoSomaUs = 0; o.atrasoN = 0;
}

// --------------------------------------------------------- a conta central
//
// Quanto tempo posso gastar AGORA sem invadir a minha janela de transmissao.
//
// txEmVoo trava tudo: com um pacote no ar, o TX_DONE pode chegar a qualquer
// instante e precisa ser servido. Nao e questao de orcamento, e de nao estar
// ocupado. E a mesma regra do radioTxEmVoo() que o wifi_ota.h ja respeita
// (radio.h:725) e que o pinta() do mapa nunca respeitou.
inline uint32_t orcamentoUs(const Tdma& t, bool txEmVoo) {
  if (txEmVoo) return 0;

  // Sem ancora o no nao transmite, entao nao ha janela para proteger. Devolve um
  // quadro inteiro: e o regime de arranque, em que pintar a tela e mais util que
  // guardar um slot que nao existe.
  if (!t.haveAnchor) return t.frameUs;

  uint32_t ate = tdmaUsUntilMySlot(t);
  if (ate <= ORC_RESERVA_US) return 0;
  return ate - ORC_RESERVA_US;
}

// "Da para gastar custoUs agora?" - a pergunta que toda operacao cara faz antes.
//
// Quem responde honestamente o proprio custo faz o sistema funcionar; quem chuta
// para baixo volta a perder janela. Por isso as camadas do mapa se cronometram
// (g_usRelevo, g_usVias... em mapa_fundo.h:88) e usam a MEDIDA da ultima vez
// como estimativa da proxima.
inline bool orcamentoCabe(Orcamento& o, const Tdma& t, bool txEmVoo, uint32_t custoUs) {
  if (custoUs < ORC_MINIMO_US) custoUs = ORC_MINIMO_US;
  if (orcamentoUs(t, txEmVoo) >= custoUs) { o.concedidos++; return true; }
  o.negados++;
  return false;
}

// Limite de estouro do regime atual, para o orcamentoVolta.
inline uint32_t orcamentoLimiteUs(const Tdma& t) {
  uint32_t g = tdmaGuardNow(t);        // ja triplica sozinho em holdover
  return g / 2;                        // meia guarda = o erro maximo tolerado por no
}
