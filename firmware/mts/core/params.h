#pragma once
// ============================================================================
//  PARAMS - o UNICO lugar de cada constante do MTS
// ============================================================================
//
// Regra: nenhum outro arquivo define numero de tempo, tamanho ou radio. Quem
// precisa de um numero inclui este header. As contas que dependem uns dos outros
// estao fechadas por static_assert no fim - mudar um numero que quebra outra conta
// nao compila, em vez de virar colisao no mato.
//
// SEM Arduino, SEM ESP: este header e a raiz da camada core, que roda no PC
// (espelho em Node: web/bancada-trilha/mts_core_test.js le este arquivo e refaz
// as contas) e na placa (firmware/mts_selftest).

#include <stdint.h>

// ------------------------------------------------------------- versoes
// Versao do FIRMWARE: o manifesto do OTA (tools/publica_ota.ps1) so baixa quando
// anuncia numero MAIOR que este. Comecou em 200 para ficar acima de qualquer
// mts_campo (v101) e mts_p4 (v22) que ainda esteja gravado na frota.
#define MTS_VERSAO       200

// Versao do PROTOCOLO no ar (byte 0 de todo pacote). Diferente = ignorado E
// avisado na tela. Foi a falta disto que pos telas em v7 e v8 no mesmo campo
// sem que ninguem soubesse por que nao se viam.
//   1 = mts_campo v101 (byte 0 era o primeiro byte do hash da sala, sem versao)
//   2 = este firmware: [versao][sala16][slot][seq][flags][lat][lon][rumo][vel]
#define PROTO_VERSAO     2

// Esquema da configuracao na NVS (namespace "grupo"). Ao mudar campos, suba e
// escreva a migracao em core/cfg.h.
#define CFG_ESQUEMA      2

// ------------------------------------------------------------- TDMA
//
// QUADRO DE 6 s COM 12 SLOTS DE 500 ms (decisao do usuario, 2026-09-28).
//
//   - 6 divide 18 e 86400: imune ao problema de grade GPS x UTC do tdma_core.h
//     e a virada da meia-noite (anchorSec % frameSecs nao salta).
//   - 500 ms de slot e o que o maior pacote (68 B em SF9 = 417 ms) exige com
//     guarda de 20 ms e ~63 ms de janela para o laco decidir transmitir.
//   - 12 slots para ate 8 carros: os slots 8..11 ficam SEMPRE vazios e servem
//     para medir o ruido de fundo (linha N do diario) e de folga para crescer.
//
// O que isto custa: posicao dos outros a cada 6 s (era 2 s). A UI extrapola a
// posicao entre pacotes por rumo e velocidade (core/frota.h) para o mapa nao
// pular; o caminho completo continua chegando pelo catch-up (core/rota.h).
#define TDMA_SLOTS         12
#define TDMA_FRAME_S       6      // 1, 2, 3, 6, 9 ou 18 (divisores de 18)
#define TDMA_GUARD_US      20000UL
#define TDMA_SLOT_US       ((uint32_t)TDMA_FRAME_S * 1000000UL / TDMA_SLOTS)
#define TDMA_FRAME_MS      ((uint32_t)TDMA_FRAME_S * 1000UL)

// Carros de verdade ocupam os slots 0..FROTA_MAX_CARROS-1. O resto e silencio.
#define FROTA_MAX_CARROS   8
#define RUIDO_SLOT_INI     FROTA_MAX_CARROS     // primeiro slot sempre vazio

// ------------------------------------------------------------- radio
// Do Estudo 1 e da pesquisa de 2026-09-08. Cada numero tem motivo no README.
#define RF_FREQ_MHZ      915.0f
#define RF_BW_KHZ        125.0f
#define RF_SF            9        // +5 dB sobre SF7 = ~1,4x de alcance em mata
#define RF_CR            5        // 4/5
#define RF_SYNC          0x12
#define RF_PRE           8
#define RF_TCXO_V        2.2f     // manual E22-M V1.2. NAO usar XTAL=true.
#define RF_OCP_MA        140.0f   // teto do setCurrentLimit; acima ele NAO aplica
#define RF_PWR_PADRAO    10       // dBm no chip = ~20 dBm na antena (PA +10 dB)
#define RF_PWR_MIN       -9
#define RF_PWR_MAX       22
#define RADIO_GANHO_PA_DB 10      // tabela oficial do modulo montado, VCC 5 V

// Alarme de reinicios: apos este numero de reinicios VIOLENTOS seguidos, faixa
// vermelha e linha no diario. NAO mexe na potencia (usuario, 2026-09-29: "isso
// nao pode acontecer") - ate entao havia um teto de 2 dBm em RAM aqui.
#define RADIO_SEGURO_APOS  3
// Radio que nao respondeu no boot: nova tentativa a cada tanto (app/rede.h).
#define RADIO_RETENTA_MS   3000UL

// ------------------------------------------------------------- trajeto / rota
// PASSO DE 10 m (era 5). A conta que manda: o pacote leva TRILHA_PONTOS_PKT
// pontos por quadro e, durante o catch-up, metade dos quadros vai para o
// preenchimento. Para a rota CONVERGIR o lider nao pode produzir mais pontos
// novos do que a metade da capacidade:
//     v / passo  <=  TRILHA_PONTOS_PKT / (2 x TDMA_FRAME_S)
//     8,3 m/s (30 km/h) / 10 m = 0,83 pt/s  <=  10 / 12 = 0,83 pt/s
// Com passo de 5 m a 30 km/h o catch-up NUNCA fechava com quadro de 6 s: cada
// quadro de preenchimento abria um buraco novo do mesmo tamanho.
#define ROTA_PASSO_M       10.0
#define TRAJETO_MAX        1200    // meu rastro: 12 km a 10 m
#define ROTA_MAX           1400    // rota do lider: 14 km a 10 m
#define TRILHA_PONTOS_PKT  10      // pontos por pacote (68 B cheio)
#define VEL_PROJETO_KMH    30      // velocidade em que a rota ainda converge

// Ack de catch-up dos seguidores vale por este tempo (5 quadros).
#define ROTA_ACK_TTL_MS    (5UL * TDMA_FRAME_MS)
// Holdover do tdma_core (core/tdma.h explica o 4).
#define TDMA_HOLDOVER_QUADROS 4
// Um seguidor cujo ULTIMO ponto recebido esta mais que isto atras do meu topo
// nao esta me ouvindo: nao dita o bloco de catch-up (frotaAckMin). 50 pontos =
// 500 m = ~1 min a 30 km/h - bem mais que a folga normal (10 pontos por bloco,
// blocos recentes em quadros alternados durante o catch-up, perdas de 50 %).
#define ROTA_OUVE_PONTOS   50

// ------------------------------------------------------------- frota (TTLs)
// Tudo em SEGUNDOS de silencio, com a conta ao lado. Entrega medida no campo de
// 09-28 foi 46-62 %; p(k quadros seguidos perdidos) = 0,5^k.
//
//   EXTRAPOLA  ate 12 s (2 quadros): o marcador anda pelo rumo e velocidade do
//              ultimo pacote. Alem disso e chute: 30 km/h x 12 s = 100 m, o que
//              ainda cabe numa curva; 250 m nao.
//   SINAL VELHO 30 s (5 quadros, p = 3 % a 50 % de perda, 0,03 % a 80 %): o
//              marcador congela e fica vazado, a faixa mostra "SEM SINAL" e o
//              trecho vira aviso de perda de sinal.
//   SOME       90 s (15 quadros): sai da lista e da contagem. Carro zumbi nao;
//              carro que parou num sombra de morro por 1 min, sim.
#define FROTA_EXTRAPOLA_MAX_MS  12000UL
#define FROTA_SINAL_VELHO_MS    30000UL
#define FROTA_SOME_MS           90000UL

// Roster (nome/cor/papel): nas falas 1, 3 e 5 (intercalado com posicao, para
// nunca haver 2 quadros seguidos sem beacon - I5 da auditoria) e depois a cada
// ROSTER_CADA falas.
#define ROSTER_CADA        8
#define ROSTER_NOME_N      16

// ------------------------------------------------------------- orcamento de laco
#define ORC_RESERVA_US     8000UL     // nunca entregue a ninguem antes da janela
#define ORC_MINIMO_US      2000UL
#define ORC_CARTAO_US      300000UL   // flush no cartao so com isto de folga

// ------------------------------------------------------------- diario
#define REG_BUF_N          (64 * 1024)
#define REG_FLUSH_MS       10000UL
#define REG_FLUSH_BYTES    (8 * 1024)
#define REG_LINHA_MAX      256

// ------------------------------------------------------------- boot / vigias
#define WDT_TIMEOUT_S      10         // task watchdog do laco principal
#define BOOT_VINGOU_MS     120000UL   // 2 min no ar encerram a serie de reinicios

// ------------------------------------------------------------- GPS
#define GPS_BAUD           9600
#define GPS_FIX_VALIDO_MS  3000UL     // fix envelhece: 3 frases de 1 Hz perdidas
#define GPS_PAR_PPS_MAX_US 400000ULL  // idade maxima da borda para casar com o NMEA (C1)
#define GPS_LACO_PARADO_MS 300UL      // laco parado mais que isto: descarta o backlog NMEA

// ------------------------------------------------------------- mapa
#define MAPA_NZOOM         5
#define FORA_DA_TRILHA_M   150.0f

// ============================================================ static asserts
static_assert(TDMA_FRAME_S > 0 && (18 % TDMA_FRAME_S) == 0, "TDMA_FRAME_S tem de dividir 18 (grade GPS x UTC)");
static_assert((86400 % TDMA_FRAME_S) == 0, "TDMA_FRAME_S tem de dividir 86400 (meia-noite)");
static_assert(TDMA_SLOT_US == 500000UL, "o slot foi dimensionado para 500 ms; refaca as contas de air-time");
static_assert(FROTA_MAX_CARROS <= TDMA_SLOTS, "mais carros que slots");
static_assert(RUIDO_SLOT_INI < TDMA_SLOTS, "sem slot vazio para medir ruido");
static_assert(FROTA_EXTRAPOLA_MAX_MS < FROTA_SINAL_VELHO_MS && FROTA_SINAL_VELHO_MS < FROTA_SOME_MS, "TTLs da frota fora de ordem");
static_assert(FROTA_SINAL_VELHO_MS >= 3 * TDMA_FRAME_MS, "sinal velho antes de 3 quadros piscaria a cada roster");
static_assert(PROTO_VERSAO >= 2 && PROTO_VERSAO <= 255, "versao do protocolo");
static_assert(RF_PWR_PADRAO >= RF_PWR_MIN && RF_PWR_PADRAO <= RF_PWR_MAX, "potencia padrao fora da faixa");
// A rota converge em catch-up ate VEL_PROJETO_KMH (ver ROTA_PASSO_M).
static_assert(TRILHA_PONTOS_PKT * ROTA_PASSO_M * 3.6 >= 2.0 * VEL_PROJETO_KMH * TDMA_FRAME_S,
              "com este passo/quadro o catch-up nao converge a VEL_PROJETO_KMH");
static_assert(ROSTER_CADA >= 2, "roster a cada quadro deixaria a rede sem beacon");
