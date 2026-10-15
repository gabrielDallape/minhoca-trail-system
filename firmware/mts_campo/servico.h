#pragma once
// ============================================================================
//  SERVICO - o unico ponto por onde TODA tela serve o aparelho
// ============================================================================
//
// POR QUE ESTE ARQUIVO EXISTE (campo de 2026-09-28)
//
// O log da trilha voltou com 57 linhas de estado em 27 minutos, em vez de ~1640,
// com buracos de 8 minutos. Nao era o cartao: era que registroServico() so rodava
// dentro de servicoDeFundo(), pendurado no esperaToque() do ui.h - e a tela da
// trilha nao usa esperaToque. O laco do mapa (grupo.h) chamava mundoAtualiza(),
// que chamava radioAtualiza() DIRETO, e nada mais. O radio vivia; o diario, o
// orcamento e o GPS-pareado nao.
//
// A regra agora e uma so: quem precisa rodar periodicamente entra no gancho
// g_hookServico, e TODO laco de tela chama o gancho - o esperaToque, o
// esperaServindo e o laco do mapa. Ninguem chama radioAtualiza() por conta
// propria. Se amanha aparecer um quarto laco, ele chama o gancho e pronto.
//
// Os ponteiros moram aqui, e nao no ui.h, porque o mundo.h precisa deles e o
// mundo.h nao pode incluir a interface (ele e a fronteira entre dado e tela).
// 'static' num header e um por unidade de traducao - o firmware tem uma so (o
// .ino), entao e a mesma variavel em todo lugar.

#include <stdint.h>

// Tudo que precisa acontecer enquanto o aparelho espera - radio, GPS, diario.
// Definido no .ino (servicoDeFundo), que e quem conhece todos os modulos.
static void (*g_hookServico)() = nullptr;

// "Da para gastar custoUs agora sem perder a minha janela de TX?" - a pergunta
// que o laco do mapa faz ANTES de pintar (orcamento.h). Antes ele nao perguntava,
// e um quadro de 73-300 ms caindo em cima da janela de 55 ms era o 'perdi' de 10%
// da tela gino. Sem gancho registrado, a resposta e sempre sim.
static bool (*g_hookCabe)(uint32_t custoUs) = nullptr;

inline void servico() { if (g_hookServico) g_hookServico(); }
inline bool servicoCabe(uint32_t custoUs) { return !g_hookCabe || g_hookCabe(custoUs); }
