#pragma once
// ============================================================================
//  CAIXA-PRETA - o que o aparelho estava fazendo quando o watchdog o matou
// ============================================================================
//
// POR QUE EXISTE (campo de 2026-09-28)
//
// As duas telas reiniciaram por watchdog (esp_reset_reason = 7, "outro watchdog")
// logo no comeco do campo. O que se sabia depois: nada. O anel do diario vive na
// RAM e e gravado a cada 10 s, entao os ultimos segundos antes do reset - que sao
// os unicos que interessam - foram os que se perderam. E na tela FaustoA o arquivo
// anterior ao reset mostra millis() VOLTANDO de 38639 para 6098 ms dentro do mesmo
// segundo UTC, com tx continuando 18 -> 19: o relogio andou para tras ~33 s com o
// programa vivo. Nao ha como explicar isso sem saber a etapa em que o laco estava.
//
// A caixa-preta e um struct em RAM que o reset NAO apaga (RTC_NOINIT_ATTR: LP RAM,
// fora do .bss que o boot zera). Cada ponto do firmware que pode demorar - entrar
// no startTransmit, esperar o BUSY, ler o cartao, pintar o mapa, gravar o diario -
// carimba a etapa e a hora ANTES de comecar. Se o aparelho morrer la dentro, o boot
// seguinte le a etapa, imprime no serial e grava no diario. Custa uma escrita de
// byte e uma de uint32 por etapa - nada.
//
// TAMBEM E O CONTADOR DE REINICIOS VIOLENTOS SEGUIDOS, que substitui o "resgate de
// boot" antigo (mts_campo.ino) sem gravar nada na NVS - ver radio.h, RADIO_MODO_SEGURO.
//
// Integridade: magic + checksum. Um brownout pode corromper a LP RAM; um struct
// que nao fecha a conta e tratado como vazio, nunca como verdade.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"

enum CaixaEtapa : uint8_t {
  CP_NADA = 0,
  CP_LACO,            // volta normal do laco (nada especial em curso)
  CP_TX_BUSY,         // radioBusyLivre: esperando o BUSY baixar antes do TX
  CP_TX_START,        // dentro do startTransmit (SPI + espera de BUSY sem timeout)
  CP_TX_VOO,          // pacote no ar, esperando TX_DONE
  CP_TX_VIGIA,        // vigia de TX preso: begin() completo do SX1262
  CP_RX_LE,           // readData do pacote recebido
  CP_RX_RSSI,         // leitura do ruido de fundo (GetRssiInst)
  CP_MAPA_PINTA,      // mapaDesenha (inclui leitura do cartao em cache frio)
  CP_MAPA_PUSH,       // pushSprite do quadro
  CP_CARTAO_FLUSH,    // registroTick gravando no cartao
  CP_CARTAO_FECHA,    // registroFecha
  CP_GPS,             // parser NMEA
  CP_WIFI,            // qualquer coisa do esp_hosted
  CP_NVS,             // escrita em Preferences (flash)
  CP_TELA_UI,         // desenho de tela fora do mapa (fillScreen etc.)
  CP_N
};

static const char* const CP_NOMES[CP_N] = {
  "nada", "laco", "tx busy", "startTransmit", "tx em voo", "vigia tx (begin)",
  "readData", "rssi inst", "pinta mapa", "push mapa", "flush cartao",
  "fecha cartao", "gps", "wifi", "nvs", "tela ui"
};

struct CaixaPreta {
  uint32_t magic;
  uint32_t resetsViolentos;   // seguidos, desde o ultimo boot que durou (ver caixaBootOk)
  uint8_t  etapa;             // CaixaEtapa em curso quando o aparelho morreu
  uint8_t  etapaAnt;          // a anterior a ela
  uint8_t  txTipo;            // 'P' posicao/trilha, 'R' roster, 0 = nenhum em voo
  int8_t   pwr;               // potencia programada no chip no momento
  uint32_t etapaMs;           // millis() em que a etapa comecou
  uint32_t ultimoMs;          // millis() da ultima volta do laco
  uint64_t ultimoUs;          // esp_timer da ultima volta (o relogio que voltou atras)
  uint32_t txDesdeMs;         // quando o TX em voo comecou
  uint32_t txCount, rxCount, presos, flushes;
  uint32_t utc;               // segundo do dia do ultimo NMEA
  uint32_t check;
};

#ifdef RTC_NOINIT_ATTR
static RTC_NOINIT_ATTR CaixaPreta g_caixa;
#else
static __NOINIT_ATTR CaixaPreta g_caixa;
#endif

#define CAIXA_MAGIC 0xC41A5A01u

inline uint32_t caixaSoma(const CaixaPreta& c) {
  const uint8_t* p = (const uint8_t*)&c;
  uint32_t s = 0x9E3779B9u;
  for (size_t i = 0; i < offsetof(CaixaPreta, check); i++) s = (s ^ p[i]) * 16777619u;
  return s;
}
inline void caixaFecha() { g_caixa.check = caixaSoma(g_caixa); }
inline bool caixaValida() { return g_caixa.magic == CAIXA_MAGIC && g_caixa.check == caixaSoma(g_caixa); }

// Carimba a etapa. Barato de proposito: chamado dezenas de vezes por segundo.
// NAO fecha o checksum a cada carimbo (seria uma soma de 60 bytes por chamada);
// quem fecha e caixaVolta(), uma vez por volta do laco. Um reset entre o carimbo
// e o fecho deixa a caixa "invalida" - e o boot entao mostra a ULTIMA volta
// fechada, que e o que se quer saber de qualquer jeito.
inline void caixaEtapa(CaixaEtapa e) {
  g_caixa.etapaAnt = g_caixa.etapa;
  g_caixa.etapa    = (uint8_t)e;
  g_caixa.etapaMs  = (uint32_t)(esp_timer_get_time() / 1000);
}

// Uma vez por volta do laco (no servico): atualiza os contadores e fecha a conta.
inline void caixaVolta(uint32_t txCount, uint32_t rxCount, uint32_t presos,
                       uint32_t flushes, uint32_t utc, int8_t pwr, uint8_t txTipo,
                       uint32_t txDesdeMs) {
  g_caixa.ultimoUs  = (uint64_t)esp_timer_get_time();
  g_caixa.ultimoMs  = (uint32_t)(g_caixa.ultimoUs / 1000);
  g_caixa.txCount = txCount; g_caixa.rxCount = rxCount; g_caixa.presos = presos;
  g_caixa.flushes = flushes; g_caixa.utc = utc; g_caixa.pwr = pwr;
  g_caixa.txTipo = txTipo; g_caixa.txDesdeMs = txDesdeMs;
  if (g_caixa.etapa == CP_NADA) g_caixa.etapa = CP_LACO;
  caixaFecha();
}

inline bool caixaMotivoViolento(esp_reset_reason_t m) {
  return m == ESP_RST_PANIC || m == ESP_RST_INT_WDT || m == ESP_RST_TASK_WDT ||
         m == ESP_RST_WDT   || m == ESP_RST_BROWNOUT || m == ESP_RST_PWR_GLITCH ||
         m == ESP_RST_CPU_LOCKUP;
}

// Le a caixa do boot anterior e prepara a deste. Devolve true se havia uma caixa
// valida de um reset violento (e ai 'anterior' e a copia para relatar).
// 'violentosSeguidos' sai atualizado: POWERON e reinicio por software zeram; um
// reset violento soma 1.
inline bool caixaBoot(esp_reset_reason_t motivo, CaixaPreta& anterior, uint32_t& violentosSeguidos) {
  bool tinha = caixaValida();
  bool violento = caixaMotivoViolento(motivo);
  if (tinha) anterior = g_caixa;
  uint32_t n = tinha ? g_caixa.resetsViolentos : 0;
  n = violento ? n + 1 : 0;

  memset(&g_caixa, 0, sizeof(g_caixa));
  g_caixa.magic = CAIXA_MAGIC;
  g_caixa.resetsViolentos = n;
  g_caixa.pwr = 127;              // desconhecida ate o radio subir
  caixaFecha();
  violentosSeguidos = n;
  return tinha && violento;
}

// Chamar quando o boot "vingou" (ex.: 2 min no ar sem morrer): a serie de
// reinicios seguidos acaba aqui. Sem isto tres travadas espalhadas por um dia
// contariam como tres seguidas.
inline void caixaBootOk() { g_caixa.resetsViolentos = 0; caixaFecha(); }

inline const char* caixaNomeEtapa(uint8_t e) { return e < CP_N ? CP_NOMES[e] : "?"; }
