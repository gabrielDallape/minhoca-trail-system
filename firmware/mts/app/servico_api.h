#pragma once
// ============================================================================
//  SERVICO (api) - as duas perguntas que toda tela faz
// ============================================================================
//
// Declaradas aqui, definidas em app/servico.h, que vem por ultimo no .ino.
// As telas (app/telas/*) incluem so este arquivo: elas nao precisam saber o
// que o servico faz, so que precisam chama-lo sempre que esperam o dedo.
//
// A regra do v101 continua: quem precisa rodar periodicamente entra no
// servico(), e TODO laco de tela o chama - o esperaToque, o esperaServindo e o
// laco do mapa. Ninguem chama o radio por conta propria.

#include <stdint.h>

// Tudo que precisa acontecer enquanto o aparelho espera: GPS, rede, diario,
// watchdog. Chamado a cada ~8 ms por qualquer tela.
void servico();

// "Da para gastar custoUs agora sem perder a minha janela de TX?" - a pergunta
// que o mapa faz ANTES de pintar, e o cartao antes de gravar.
bool servicoCabe(uint32_t custoUs);
