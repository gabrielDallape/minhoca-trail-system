#pragma once
// ============================================================================
//  TDMA - fatiamento do tempo. E o tdma_core.h COMPARTILHADO com a bancada web.
// ============================================================================
//
// O arquivo de verdade e firmware/tdma_core.h: e ele que a bancada
// (web/bancada-trilha/eleicao_test.js, tdma_replay.js, audit.js) le e espelha.
// Este header so o inclui e prende os parametros deste firmware a ele. Nao
// copie o tdma_core.h para ca - uma copia divergiria da bancada em silencio.
//
// O que este firmware pede ao tdma_core, com o quadro de 2 s (v201):
//   - tdmaTick: holdover apos (TDMA_HOLDOVER_QUADROS x quadro + 0,5 s) = 24,5 s
//     sem ancora - o MESMO tempo do quadro de 6 s x 4, agora 12 quadros. O maior
//     silencio NORMAL de uma fonte e 1 quadro (roster intercalado, ROSTER_CADA),
//     mas com 46-62 % de entrega (campo de 09-28) perder beacons seguidos e
//     comum: o que precisa durar e o TEMPO, nao a contagem de quadros.
//   - tdmaEleicao: um no que nunca ouviu ninguem espera 3 s + slot x 2 s antes
//     de ancorar em si. O slot 3 espera 9 s - so sem GPS, porque com PPS a
//     ancora vem do pulso (10 s firmes) e a eleicao nem entra.
//   - escuta silenciosa: uma ancora que nao e o slot 0 cala 1 quadro a cada 16
//     (32 s) para ouvir se ha ancora de slot menor.

// O caminho e relativo a RAIZ do sketch (arduino-cli copia o sketch para o build e
// resolve este include pelo -I da pasta original): firmware/tdma_core.h.
#include "params.h"          // ANTES: define TDMA_HOLDOVER_QUADROS para o core
#include "../tdma_core.h"

// A guarda em holdover triplica (TDMA_HOLDOVER_GUARD): 60 ms. Ainda cabe.
static_assert(TDMA_GUARD_US * TDMA_HOLDOVER_GUARD < TDMA_SLOT_US / 4,
              "a guarda de holdover come mais de um quarto do slot");
