#pragma once
// ============================================================================
//  CFG - a configuracao persistida, com versao de esquema e migracao
// ============================================================================
//
// Este arquivo NAO sabe o que e NVS: ele define o struct, os padroes, a
// validacao (clamp) e a migracao entre esquemas. Quem le e grava e hal/nvs.h.
// Assim a migracao e testavel no PC (mts_core_test.js espelha cfgMigra).
//
// HISTORICO DOS ESQUEMAS (namespace "grupo" da NVS, chaves iguais as do
// grupo_ws das telas S3 de proposito - as tres telas P4 ja tem nome/slot/pwr):
//   0 = mts_p4 / mts_campo ate v100: name, color, theme, zoom, slot, pwr
//   1 = mts_campo v101: + pwrv (marca de que o pwr foi acertado apos o recuo
//       automatico que gravava sem o usuario saber)
//   2 = mts v200: + cfgv (versao do esquema). O slot passa a valer 0..7
//       (era 0..3 na v101); 12 slots de 500 ms.
//   3 = mts v201: o slot volta a valer 0..3 (4 slots de 500 ms, quadro de 2 s).
//       Um slot 4..7 gravado pelo v200 e trazido a 0 pelo cfgValida - e isso
//       COLIDE com quem ja esta no 0 (em geral a ancora). Por isso o ajuste nao
//       e silencioso: o boot poe uma faixa na tela (app/config.h).

#include <stdint.h>
#include <string.h>
#include "params.h"

struct Cfg {
  char    nome[16];
  uint8_t cor;         // 0..N_CORES-1 (paleta em app/telas/ui.h)
  uint8_t tema;        // 0 dia, 1 noite
  uint8_t zoom;        // 0..MAPA_NZOOM-1
  uint8_t slot;        // 0..FROTA_MAX_CARROS-1: endereco na rede, nao preferencia
  int8_t  pwr;         // dBm no chip; SO muda por comando do usuario
  uint32_t boots;      // contador de boots (nome do diario sem hora de GPS)
};

#define CFG_N_CORES 8

inline void cfgPadrao(Cfg& c)
{
  memset(&c, 0, sizeof(c));
  strncpy(c.nome, "Carro", sizeof(c.nome) - 1);
  c.cor = 0; c.tema = 1; c.zoom = 2; c.slot = 0; c.pwr = RF_PWR_PADRAO; c.boots = 0;
}

// Devolve uma mascara do que precisou ser corrigido (para o log dizer).
enum CfgAjuste : uint8_t {
  CFG_AJ_NOME = 1, CFG_AJ_COR = 2, CFG_AJ_TEMA = 4, CFG_AJ_ZOOM = 8,
  CFG_AJ_SLOT = 16, CFG_AJ_PWR = 32
};

inline uint8_t cfgValida(Cfg& c)
{
  uint8_t aj = 0;
  c.nome[sizeof(c.nome) - 1] = 0;
  if (!c.nome[0]) { strncpy(c.nome, "Carro", sizeof(c.nome) - 1); aj |= CFG_AJ_NOME; }
  if (c.cor >= CFG_N_CORES) { c.cor = 0; aj |= CFG_AJ_COR; }
  if (c.tema > 1) { c.tema = 1; aj |= CFG_AJ_TEMA; }
  if (c.zoom >= MAPA_NZOOM) { c.zoom = 2; aj |= CFG_AJ_ZOOM; }
  if (c.slot >= FROTA_MAX_CARROS) { c.slot = 0; aj |= CFG_AJ_SLOT; }
  if (c.pwr < RF_PWR_MIN || c.pwr > RF_PWR_MAX) { c.pwr = RF_PWR_PADRAO; aj |= CFG_AJ_PWR; }
  return aj;
}

// MIGRACAO. Entra com o que a NVS tinha (esquema lido e a marca pwrv da v101) e
// sai com o struct no esquema atual. Devolve true se algo foi migrado (e ai o
// chamador regrava e registra no diario).
//
// A regra do pwr: ate o esquema 0 o valor gravado podia ter sido escrito pelo
// recuo automatico ou pelo resgate de boot, sem o usuario saber - e foi (campo
// de 09-28, duas telas em 6 dBm sem ninguem pedir). Como nao ha como distinguir
// "6 porque o dono quis" de "6 porque o firmware baixou", a PRIMEIRA subida a
// partir de um esquema sem a marca pwrv traz ao padrao quem estiver ABAIXO dele.
// So sobe, nunca desce: ate 2026-09-29 era "volta ao padrao", e quem tinha
// escolhido 14 ou 22 descia para 10 sem aviso na tela. E um acerto unico,
// anunciado, para limpar o que os mecanismos removidos deixaram. Do esquema 1 em
// diante o pwr e do usuario.
inline bool cfgMigra(Cfg& c, uint8_t esquemaLido, uint8_t pwrvLido, uint8_t& esquemaSaida)
{
  bool mudou = false;
  uint8_t e = esquemaLido;
  if (e == 0 && pwrvLido >= 101) e = 1;        // v101 gravou pwrv, nao cfgv
  if (e < 1) {
    if (c.pwr < RF_PWR_PADRAO) c.pwr = RF_PWR_PADRAO;   // ver o bloco acima: so sobe
    e = 1; mudou = true;
  }
  if (e < 2) {
    // v101 tinha 4 slots; qualquer slot valido continua valido em 12. Nada a
    // fazer alem de marcar o esquema.
    e = 2; mudou = true;
  }
  if (e < 3) {
    // De 12 slots para 4: quem estava em 0..3 continua onde estava. Quem estava
    // em 4..7 nao tem mais slot - o cfgValida, chamado logo depois, o traz a 0 e
    // marca CFG_AJ_SLOT, que vira aviso na tela.
    e = 3; mudou = true;
  }
  esquemaSaida = e;
  return mudou;
}
