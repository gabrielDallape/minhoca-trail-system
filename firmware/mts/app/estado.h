#pragma once
// ============================================================================
//  ESTADO - o estado da aplicacao, num lugar so, e a maquina de estados
// ============================================================================
//
// Tudo que mais de um modulo do app le mora aqui: configuracao, sessao, frota,
// meu trajeto, a rota do lider, minha posicao. As telas leem; quem escreve e o
// servico (GPS, rede) e as acoes do usuario. Uma linha de execucao so (o laco
// do Arduino) - por isso nao ha mutex: um double lido pela metade por outra
// tarefa viraria coordenada absurda no mapa.
//
// A MAQUINA DE ESTADOS e explicita (g_estado) mesmo com as telas sendo lacos
// bloqueantes: o servico sabe em que tela esta (o OTA da nuvem so roda na
// inicial; o mapa e o unico que pinta por orcamento), o diario registra a
// transicao, e o diagnostico mostra onde o aparelho estava.

#include <stdint.h>
#include <string.h>
#include "../core/params.h"
#include "../core/cfg.h"
#include "../core/frota.h"
#include "../core/trajeto.h"
#include "../core/rota.h"
#include "../hal/nvs.h"

enum AppEstado : uint8_t {
  EST_BOOT = 0, EST_INICIAL, EST_CONFIG, EST_DIAG, EST_WIFI,
  EST_CRIAR, EST_SALA, EST_ENTRAR, EST_TRILHA, EST_SAIR, EST_OTA, EST_N
};
static const char* const EST_NOMES[EST_N] = {
  "boot", "inicial", "config", "diag", "wifi", "criar", "sala", "entrar", "trilha", "sair", "ota"
};

static AppEstado g_estado = EST_BOOT;
static Cfg       g_cfg;
static uint8_t   g_cfgEsquema = 0;
static Sessao    g_ses;
static Frota     g_frota;
static Trajeto   g_traj;
static Rota      g_rotaC;

// Minha posicao, como o GPS a entrega (ou o simulador, sem GPS).
struct Eu {
  bool    fix;
  double  lat, lon;
  float   rumo;        // graus, 0 = norte
  uint8_t vel;         // km/h
  uint8_t sats;
};
static Eu g_eu = { false, -23.5505, -46.6333, 0, 0, 0 };   // ancora do mapa antes do fix

// UMA FAIXA VERMELHA que fica ate o aparelho reiniciar (alarme de reinicios seguidos).
static char g_avisoTexto[64] = "";
inline void avisoDefine(const char* t) { strncpy(g_avisoTexto, t ? t : "", sizeof(g_avisoTexto) - 1); g_avisoTexto[sizeof(g_avisoTexto) - 1] = 0; }
inline bool avisoAtivo() { return g_avisoTexto[0] != 0; }

// Aviso passageiro na tela (versao incompativel no ar, lider sem sinal...):
// texto + ate quando mostrar.
static char     g_avisoTmp[64] = "";
static uint32_t g_avisoTmpAteMs = 0;
inline void avisoTmp(const char* t, uint32_t agoraMs, uint32_t durMs)
{
  strncpy(g_avisoTmp, t, sizeof(g_avisoTmp) - 1); g_avisoTmp[sizeof(g_avisoTmp) - 1] = 0;
  g_avisoTmpAteMs = agoraMs + durMs;
}
inline const char* avisoTmpAtivo(uint32_t agoraMs)
{
  return (g_avisoTmp[0] && (int32_t)(g_avisoTmpAteMs - agoraMs) > 0) ? g_avisoTmp : nullptr;
}

inline void estadoVai(AppEstado e);   // definida em app/servico.h (registra no diario)
