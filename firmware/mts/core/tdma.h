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
// O que este firmware pede ao tdma_core, e o que muda com o quadro de 6 s:
//   - tdmaTick: holdover apos (TDMA_HOLDOVER_QUADROS x quadro + 0,5 s) = 24,5 s
//     sem ancora. O maior silencio NORMAL de uma fonte e 1 quadro (roster
//     intercalado, ROSTER_CADA), mas com 46-62 % de entrega (campo de 09-28)
//     perder o beacon do quadro vizinho e comum: com 2 quadros (12,5 s, ate
//     2026-09-29) a folga era de 0,5 s e cada perda dessas virava holdover +
//     eleicao. Com 4, sao precisos 3 beacons seguidos perdidos.
//   - tdmaEleicao: um no que nunca ouviu ninguem espera 3 s + slot x 6 s antes
//     de ancorar em si. O slot 7 espera 45 s - so sem GPS, porque com PPS a
//     ancora vem do pulso (10 s firmes) e a eleicao nem entra.
//   - escuta silenciosa: uma ancora que nao e o slot 0 cala 1 quadro a cada 16
//     (96 s) para ouvir se ha ancora de slot menor.

// O caminho e relativo a RAIZ do sketch (arduino-cli copia o sketch para o build e
// resolve este include pelo -I da pasta original): firmware/tdma_core.h.
#include "params.h"          // ANTES: define TDMA_HOLDOVER_QUADROS para o core
#include "../tdma_core.h"

// A guarda em holdover triplica (TDMA_HOLDOVER_GUARD): 60 ms. Ainda cabe.
static_assert(TDMA_GUARD_US * TDMA_HOLDOVER_GUARD < TDMA_SLOT_US / 4,
              "a guarda de holdover come mais de um quarto do slot");
