#pragma once
// ============================================================================
//  SESSAO - criar, entrar, abrir a trilha, sair. A LOGICA, sem tela nenhuma.
// ============================================================================
//
// Ate 2026-10-01 isto morava no mts.ino, misturado com os toques e o desenho.
// Saiu daqui por um motivo: o SIMULADOR (tools/sim) roda duas, tres telas
// virtuais com o firmware de verdade, e precisa apertar "CRIAR" e "ENTRAR" pelo
// MESMO caminho que o dedo aperta. Logica copiada para o simulador divergiria em
// silencio - foi exatamente assim que o bug do icone (core/tempo.h) passou por
// todos os testes: eles testavam pedacos, nunca o aparelho inteiro.
//
// Quem chama: as telas (mts.ino) e o simulador. Nenhuma funcao daqui desenha.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../core/frota.h"
#include "../core/rota.h"
#include "../core/trajeto.h"
#include "../hal/nvs.h"
#include "estado.h"
#include "rede.h"

// g_ses (a sessao) mora em estado.h: as telas tambem a leem.

// Sai do grupo (SAIR na trilha, DESCARTAR na inicial, CANCELAR na sala de espera).
inline void sessaoEncerra()
{
  g_ses.ativa = false; g_ses.gNome[0] = 0; g_ses.gCod[0] = 0;
  nvsGravaSessao(g_ses);
  redeTrocaSala("SEMSALA");        // sai do grupo = sai da sala: para de falar nele
  frotaLimpaOutros(g_frota);
  rotaLimpaCore(g_rotaC);
  // Fora do grupo eu nao guio nem peco socorro: sem isto o aparelho seguia
  // anunciando LIDER (e talvez ALERTA) na sala neutra.
  g_frota.c[0].lider = false;
  g_frota.c[0].alerta = false;
}

// CRIAR confirmado: entro na SALA DE ESPERA do grupo novo, ja como lider (o
// roster do criador diz LIDER desde a espera). A sessao so e gravada no ABRIR.
inline void sessaoCriaSala(const char* gCod)
{
  g_frota.c[0].lider = true;
  redeTrocaSala(gCod);
  frotaLimpaOutros(g_frota);       // a sala nova nasce vazia
  rotaLimpaCore(g_rotaC);
}

// ABRIR TRILHA na sala de espera: a sessao vira permanente (RETOMAR apos religar).
inline void sessaoAbreComoLider(const char* gNome, const char* gCod)
{
  g_ses.ativa = true; g_ses.lider = true;
  strncpy(g_ses.gNome, gNome, sizeof(g_ses.gNome) - 1); g_ses.gNome[sizeof(g_ses.gNome) - 1] = 0;
  strncpy(g_ses.gCod,  gCod,  sizeof(g_ses.gCod) - 1);  g_ses.gCod[sizeof(g_ses.gCod) - 1] = 0;
  nvsGravaSessao(g_ses);
  // Trilha nova, rastro novo (como o v101): sem isto o lider transmitia tudo
  // desde o boot - o caminho ate a entrada da trilha, ou a trilha anterior -
  // e quem entrasse passava ~20 min recebendo historico. A numeracao
  // continua (so a EPOCA diz "recomecei"), entao ninguem ve um vao falso.
  trajetoLimpa(g_traj);
}

// ENTRAR: 'cod' e o codigo digitado ("13520") ou o grupo escolhido na lista do
// que esta no ar ("#79FD", core/vizinhos.h). 'nomeGrupo' vazio = "Grupo <cod>".
inline void sessaoEntra(const char* cod, const char* nomeGrupo)
{
  g_ses.ativa = true; g_ses.lider = false;
  // O nome do grupo nao viaja no ar - so o hash. Da LISTA vem "Grupo de <lider>";
  // pelo codigo digitado, o seguidor mostra o codigo.
  if (nomeGrupo && nomeGrupo[0]) strncpy(g_ses.gNome, nomeGrupo, sizeof(g_ses.gNome) - 1);
  else snprintf(g_ses.gNome, sizeof(g_ses.gNome), "Grupo %s", cod);
  g_ses.gNome[sizeof(g_ses.gNome) - 1] = 0;
  strncpy(g_ses.gCod, cod, sizeof(g_ses.gCod) - 1); g_ses.gCod[sizeof(g_ses.gCod) - 1] = 0;
  nvsGravaSessao(g_ses);
  g_frota.c[0].lider = false;
  redeTrocaSala(cod);
  frotaLimpaOutros(g_frota);
  rotaLimpaCore(g_rotaC);
  trajetoLimpa(g_traj);
}

// Entrando na tela da trilha (depois de ABRIR, ENTRAR ou RETOMAR).
inline void sessaoComecaTrilha()
{
  g_frota.c[0].lider = g_ses.lider;
  g_frota.c[0].alerta = false;        // pedido de socorro nao atravessa sessoes
}

// RELIGOU NO MEIO DA TRILHA: a sessao vira uma OFERTA (RETOMAR) na tela
// inicial, nunca entra sozinha - uma tela presa numa sessao velha parecia
// travada e so saia regravando. Devolve true se ha o que retomar.
inline bool sessaoRecuperaNoBoot()
{
  nvsLeSessao(g_ses);
  if (!g_ses.ativa) return false;
  g_frota.c[0].lider = g_ses.lider;
  redeTrocaSala(g_ses.gCod);
  return true;
}
