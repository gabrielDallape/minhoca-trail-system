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
#define MTS_VERSAO       201

// Versao do PROTOCOLO no ar (byte 0 de todo pacote). Diferente = ignorado E
// avisado na tela. Foi a falta disto que pos telas em v7 e v8 no mesmo campo
// sem que ninguem soubesse por que nao se viam.
//   1 = mts_campo v101 (byte 0 era o primeiro byte do hash da sala, sem versao)
//   2 = mts v200: [versao][sala16][slot][seq][flags][lat][lon][rumo][vel]
//   3 = mts v201: os MESMOS bytes, outro quadro (2 s x 4 slots, era 6 s x 12).
//       Os bytes iguais sao justamente o perigo: um v200 no ar seria aceito e
//       colidiria EM SILENCIO (o slot k dele cai no slot k mod 4 daqui uma vez a
//       cada tres quadros). Com a versao nova ele aparece como incompativel.
#define PROTO_VERSAO     3

// Esquema da configuracao na NVS (namespace "grupo"). Ao mudar campos, suba e
// escreva a migracao em core/cfg.h.
#define CFG_ESQUEMA      3

// ------------------------------------------------------------- TDMA
//
// QUADRO DE 2 s COM 4 SLOTS DE 500 ms (usuario, 2026-10-08: "ate quatro carros,
// e que funcione perfeitamente").
//
// HISTORICO: o v200 tinha 6 s x 12 slots para caber 8 carros. No carro de 09-30
// o lider chegava ao seguidor a cada 6 s no melhor caso, 27 % dos intervalos
// passaram de 12 s e o icone ficou congelado/vazado 35 % da trilha - "o lider
// fica muito lento". Com 4 carros o quadro volta aos 2 s do mts_campo:
//
//   - 2 divide 18 e 86400: imune ao problema de grade GPS x UTC do tdma_core.h
//     e a virada da meia-noite (anchorSec % frameSecs nao salta). NAO use 4 nem 5.
//   - 500 ms de slot continua sendo o que o maior pacote (68 B em SF9 = 417 ms)
//     exige com guarda de 20 ms e ~63 ms de janela para o laco decidir transmitir.
//   - 4 slots = 4 carros, SEM slot fixo vazio: o ruido de fundo e medido num slot
//     que ninguem ocupa no momento (app/rede.h, redeMedeRuido).
//
// O que isto custa: o canal fica ~50 % ocupado com 4 carros (era ~18 %) e cada
// aparelho transmite 3x mais vezes por hora. Dois grupos no MESMO canal passam
// a colidir muito mais - o produto e um grupo por trilha.
#define TDMA_SLOTS         4
#define TDMA_FRAME_S       2      // 1, 2, 3, 6, 9 ou 18 (divisores de 18)
#define TDMA_GUARD_US      20000UL
#define TDMA_SLOT_US       ((uint32_t)TDMA_FRAME_S * 1000000UL / TDMA_SLOTS)
#define TDMA_FRAME_MS      ((uint32_t)TDMA_FRAME_S * 1000UL)

// Um carro por slot: 0..FROTA_MAX_CARROS-1.
#define FROTA_MAX_CARROS   4

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
// A cada tanto, fora de TX, pergunta ao chip se ele ainda esta configurado
// (radioHwConfigOk): um E22 que reiniciou sozinho volta de fabrica e fica surdo.
#define RADIO_CONFERE_MS   5000UL
// DIO1 que nao avisa: depois de tantos TX SEGUIDOS terminados sem interrupcao (o
// vigia teve de perguntar ao chip), a rede passa a CONSULTAR os eventos do chip
// pelo SPI a cada volta (app/rede.h, modo consulta).
#define RADIO_SEM_DIO1_TX  3

// ------------------------------------------------------------- trajeto / rota
// PASSO DE 10 m (era 5). A conta que manda: o pacote leva TRILHA_PONTOS_PKT
// pontos por quadro e, durante o catch-up, metade dos quadros vai para o
// preenchimento. Para a rota CONVERGIR o lider nao pode produzir mais pontos
// novos do que a metade da capacidade:
//     v / passo  <=  TRILHA_PONTOS_PKT / (2 x TDMA_FRAME_S)
//     quadro de 2 s: 10 / 4 = 2,5 pt/s  ->  converge ate 90 km/h a 10 m
//     (no quadro de 6 s eram 0,83 pt/s: o limite era justo os 30 km/h)
// Com passo de 5 m a 30 km/h o catch-up NUNCA fechava com quadro de 6 s: cada
// quadro de preenchimento abria um buraco novo do mesmo tamanho. Com 2 s o
// passo de 5 m convergiria ate 45 km/h, mas fica em 10 m nesta versao: voltar a
// 5 m dobra rota e rastro na memoria e muda o desenho - mudanca a parte.
#define ROTA_PASSO_M       10.0
#define TRAJETO_MAX        1200    // meu rastro: 12 km a 10 m
#define ROTA_MAX           1400    // rota do lider: 14 km a 10 m
#define TRILHA_PONTOS_PKT  10      // pontos por pacote (68 B cheio)
#define VEL_PROJETO_KMH    60      // velocidade em que a rota ainda converge (com folga)

// Ack de catch-up dos seguidores vale por este tempo. Era 5 quadros = 30 s; com
// quadro de 2 s, 5 quadros sao 10 s e quatro perdas seguidas (6 % a 50 % de
// entrega) derrubariam o ack de um seguidor que ainda ouve. 8 quadros = 16 s.
#define ROTA_ACK_TTL_MS    (8UL * TDMA_FRAME_MS)
// Holdover do tdma_core (core/tdma.h). O que importa e o tempo em SEGUNDOS, nao
// em quadros: a deriva do cristal so pesa depois de ~30 min (tdma_core.h), e o
// que derruba a rede e o holdover acabar num buraco de entrega. 4 quadros de 6 s
// eram 24,5 s; com quadro de 2 s seriam 8,5 s e a 50 % de perda o holdover
// cairia ~127 vezes por hora (cada queda e uma reeleicao). 12 quadros = 24,5 s,
// o mesmo de antes.
#define TDMA_HOLDOVER_QUADROS 12
// Um seguidor cujo ULTIMO ponto recebido esta mais que isto atras do meu topo
// nao esta me ouvindo: nao dita o bloco de catch-up (frotaAckMin). 50 pontos =
// 500 m = ~1 min a 30 km/h - bem mais que a folga normal (10 pontos por bloco,
// blocos recentes em quadros alternados durante o catch-up, perdas de 50 %).
#define ROTA_OUVE_PONTOS   50

// ------------------------------------------------------------- frota (TTLs)
// Tudo em SEGUNDOS de silencio, com a conta ao lado. Entrega medida no carro de
// 09-30: 52-97 % conforme a distancia; p(k quadros seguidos perdidos) = (1-e)^k.
// Com quadro de 2 s ha 1800 quadros por hora: o limiar em quadros precisa ser
// maior que no quadro de 6 s para o alarme falso continuar raro.
//
//   EXTRAPOLA  ate 8 s (4 quadros; congela em 6 % dos silencios a 50 % de
//              entrega, 0,8 % a 70 %): o marcador anda pelo rumo e velocidade do
//              ultimo pacote. Alem disso e chute: 30 km/h x 8 s = 67 m.
//   SINAL VELHO 20 s (10 quadros, p = 0,1 % a 50 % de perda): o marcador congela
//              e fica vazado, a faixa mostra "SEM SINAL" e o trecho vira aviso de
//              perda de sinal. Com 30 s de antes eram 15 quadros de 2 s - ficava
//              "vivo" tempo demais com o carro ja longe.
//   SOME       90 s (45 quadros): sai da lista e da contagem. Carro zumbi nao;
//              carro que parou num sombra de morro por 1 min, sim.
#define FROTA_EXTRAPOLA_MAX_MS  8000UL
#define FROTA_SINAL_VELHO_MS    20000UL
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
// Repintura periodica da tela da trilha (app/telas/tela_trilha.h). Um quadro de
// mapa custa 47-73 ms quente; a 2 por segundo sao ~15 % do laco, e a pintura so
// sai quando o orcamento deixa (core/orcamento.h, que desde a v201 tambem guarda
// a janela de TX aberta). A 30 km/h, 500 ms = 4 m por passo do marcador.
#define TELA_REPINTA_ANDANDO_MS  500UL
#define TELA_REPINTA_MS          1000UL
#define FORA_DA_TRILHA_M   150.0f

// ============================================================ static asserts
static_assert(TDMA_FRAME_S > 0 && (18 % TDMA_FRAME_S) == 0, "TDMA_FRAME_S tem de dividir 18 (grade GPS x UTC)");
static_assert((86400 % TDMA_FRAME_S) == 0, "TDMA_FRAME_S tem de dividir 86400 (meia-noite)");
static_assert(TDMA_SLOT_US == 500000UL, "o slot foi dimensionado para 500 ms; refaca as contas de air-time");
static_assert(FROTA_MAX_CARROS <= TDMA_SLOTS, "mais carros que slots");
static_assert(FROTA_EXTRAPOLA_MAX_MS >= 2 * TDMA_FRAME_MS, "extrapolacao menor que o buraco normal do roster (2 quadros)");
static_assert(FROTA_EXTRAPOLA_MAX_MS < FROTA_SINAL_VELHO_MS && FROTA_SINAL_VELHO_MS < FROTA_SOME_MS, "TTLs da frota fora de ordem");
static_assert(FROTA_SINAL_VELHO_MS >= 3 * TDMA_FRAME_MS, "sinal velho antes de 3 quadros piscaria a cada roster");
static_assert(PROTO_VERSAO >= 2 && PROTO_VERSAO <= 255, "versao do protocolo");
static_assert(RF_PWR_PADRAO >= RF_PWR_MIN && RF_PWR_PADRAO <= RF_PWR_MAX, "potencia padrao fora da faixa");
// A rota converge em catch-up ate VEL_PROJETO_KMH (ver ROTA_PASSO_M).
static_assert(TRILHA_PONTOS_PKT * ROTA_PASSO_M * 3.6 >= 2.0 * VEL_PROJETO_KMH * TDMA_FRAME_S,
              "com este passo/quadro o catch-up nao converge a VEL_PROJETO_KMH");
static_assert(ROSTER_CADA >= 2, "roster a cada quadro deixaria a rede sem beacon");
