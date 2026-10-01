#pragma once
// ============================================================================
//  TEMPO - "quanto tempo faz desde t", sem dar a volta. Sem hardware.
// ============================================================================
//
// O BUG QUE FEZ ESTE ARQUIVO EXISTIR (teste no carro de 2026-09-30): na tela do
// Gino o ICONE da FaustoA nao aparecia e o tracado so as vezes. O servico() lia
// 'agora = millis()' no comeco, DEPOIS processava o radio - que carimbava a
// FaustoA com o millis() do recebimento, alguns ms a frente - e so entao
// envelhecia a frota com o 'agora' velho. 'agora - ultimoMs' dava negativo, em
// uint32_t isso e ~4 bilhoes, e o carro era apagado logo depois de chegar. O
// tracado sobrevivia porque mora na rota, nao na frota. Na FaustoA o mesmo bug
// acertava menos (sorteio de milissegundo): no diario, a coluna carrosAtivos do
// Gino ficou em 1 o teste inteiro, a da FaustoA em 2.
//
// A regra daqui para a frente: TODA diferenca "agora - carimbo" do core passa
// por tempoDesde(). Carimbo no futuro (ate meio dia de millis, o que nunca e
// rolagem legitima) conta como "agora mesmo". O teste do core audita isso.

#include <stdint.h>

inline uint32_t tempoDesde(uint32_t agoraMs, uint32_t carimboMs)
{
  const int32_t d = (int32_t)(agoraMs - carimboMs);
  return d < 0 ? 0u : (uint32_t)d;
}
