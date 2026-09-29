#pragma once
// ============================================================================
//  REGISTRO - o diario de bordo no cartao
// ============================================================================
//
// POR QUE ESTE ARQUIVO EXISTE
//
// O teste de campo de 2026-09-10 voltou com "a conexao ficou bem instavel" e
// zero dado. Nao por descuido: o firmware daquele dia estava com MTS_TEM_SD 0 e
// MTS_TEM_WIFI 0 (para isolar a medicao de radio na bancada), entao nao havia
// onde gravar, e os contadores em RAM morreram quando as telas foram religadas
// no PC. A unica coisa que sobreviveu foi a potencia na NVS: as duas voltaram em
// 10 dBm tendo saido em 14 - prova de que o recuo automatico disparou no mato.
//
// "Instavel" tem QUATRO causas possiveis, e cada uma deixa um contador diferente:
//
//   perdi    -> perdeu a propria janela de TX      -> grade de tempo / laco lento
//   conflito -> dois nos no mesmo slot             -> as grades se separaram
//   ruim     -> CRC ruim                           -> link marginal, e alcance
//   preso    -> TX travado                         -> alimentacao ou radio
//
// Sem log, as quatro sao a mesma frase. Com log, sao quatro numeros.
//
// ---------------------------------------------------------------------------
// A REGRA DURA: ESTE ARQUIVO NAO PODE CAPAR O RADIO
//
// Um fwrite+fsync que caia num garbage collection interno do microSD bloqueia
// por centenas de ms. Isso estoura o vigia de TX preso (radio.h, RADIO_VIGIA_MS),
// conta 'preso' e refaz o begin() do chip no meio de um TX que estava bom. (Ate a
// v100 tres presos seguidos ainda derrubavam 4 dB na NVS; isso acabou, mas o
// vigia disparando a toa continua sendo o instrumento estragando a medida.)
//
// O precedente esta escrito no radio.h:468 - "das tres telas na bancada, so a
// que estava com WiFi ativo acusava 'preso', e a acusacao vinha na linha
// seguinte ao 'eco: subiu -> HTTP 200'". O HTTPS ja fez exatamente isso. Um
// logger ingenuo faria de novo, e o instrumento destruiria a medida.
//
// Dai as tres regras, nesta ordem de importancia:
//
//   1. registroLinha() NUNCA toca no cartao e NUNCA bloqueia. Só escreve na RAM.
//   2. O cartao so e tocado por registroTick(), e so quando ha ORC_CARTAO_US
//      (300 ms) de espaco morto ate a proxima janela de TX.
//   3. Se o buffer encher, perde-se o dado MAIS ANTIGO e conta-se o descarte.
//      Perder linha de log e aceitavel; perder pacote de posicao nao e.
//
// E o proprio custo da escrita e cronometrado e vai para o log. Se o cartao
// segurar 400 ms num flush, isso vira uma linha em vez de virar misterio.

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hardware.h"
#include "orcamento.h"
#include "caixa.h"

#if MTS_TEM_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

// Anel na PSRAM. 64 KB a ~400 B/s de log da mais de 2 minutos de folga entre
// flushes - muito mais do que o intervalo de 10 s que usamos. O tamanho existe
// para atravessar um bloqueio longo do cartao sem perder nada, nao para acumular.
#define REG_BUF_N        (64 * 1024)
#define REG_FLUSH_MS     10000UL      // periodo maximo entre flushes
#define REG_FLUSH_BYTES  (8 * 1024)   // ou quando juntar isto, o que vier antes
#define REG_LINHA_MAX    256

struct Registro {
  char*    buf;
  uint32_t cap;
  uint32_t escreve;        // proxima posicao de escrita
  uint32_t le;             // proxima posicao a gravar no cartao
  uint32_t pendentes;      // bytes esperando flush

  bool     ativo;          // arquivo aberto e utilizavel
  char     caminho[48];

  uint32_t linhas;         // linhas aceitas
  uint32_t descartadas;    // linhas perdidas por buffer cheio
  uint32_t flushes;
  uint32_t bytesGravados;
  uint32_t piorFlushUs;    // o numero que denuncia o garbage collection
  uint32_t ultFlushMs;
  uint32_t adiados;        // flushes que nao couberam no orcamento
#if MTS_TEM_SD
  File     f;
#endif
};

static Registro g_reg;

// ------------------------------------------------------------------ anel
//
// Descarte pelo mais ANTIGO, nao pelo mais novo: numa investigacao de campo o
// que acabou de acontecer vale mais que o que aconteceu ha dois minutos. E o
// descarte e contado, porque um log com buraco silencioso e pior que nenhum log.
inline void regDescartaMaisAntigo(Registro& r, uint32_t precisa) {
  while (r.cap - r.pendentes < precisa && r.pendentes > 0) {
    // joga fora uma linha inteira, ate o '\n' - meia linha no CSV quebra o parser
    uint32_t n = 0;
    while (n < r.pendentes && r.buf[(r.le + n) % r.cap] != '\n') n++;
    if (n < r.pendentes) n++;                 // engole o proprio '\n'
    r.le = (r.le + n) % r.cap;
    r.pendentes -= n;
    r.descartadas++;
  }
}

// A unica funcao que o resto do firmware chama para registrar qualquer coisa.
// Custa um vsnprintf e um memcpy. Nao toca no cartao, nao bloqueia, nao falha.
inline void registroLinha(const char* fmt, ...) {
  Registro& r = g_reg;
  if (!r.buf) return;

  char linha[REG_LINHA_MAX];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(linha, sizeof(linha), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(linha)) n = sizeof(linha) - 1;
  if (linha[n - 1] != '\n') { linha[n++] = '\n'; }

  if (r.cap - r.pendentes < (uint32_t)n) regDescartaMaisAntigo(r, (uint32_t)n);
  if (r.cap - r.pendentes < (uint32_t)n) return;    // linha maior que o anel: impossivel, mas nao trava

  for (int i = 0; i < n; i++) {
    r.buf[r.escreve] = linha[i];
    r.escreve = (r.escreve + 1) % r.cap;
  }
  r.pendentes += n;
  r.linhas++;
}

// ------------------------------------------------------------------ inicio
inline bool registroInicia(uint32_t utcSecDia, uint32_t bootId) {
  Registro& r = g_reg;
  memset(&r, 0, sizeof(r));
  r.cap = REG_BUF_N;

  // PSRAM de proposito: 64 KB de RAM interna sao caros num aparelho que ja tem
  // framebuffer, caches de mapa e pilha de rede. O anel nao e critico em latencia
  // (nao e lido de ISR), entao a PSRAM serve.
  r.buf = (char*)ps_malloc(r.cap);
  if (!r.buf) r.buf = (char*)malloc(r.cap);       // sem PSRAM, tenta a interna
  if (!r.buf) { r.cap = 0; return false; }

#if MTS_TEM_SD
  // O nome usa a hora UTC quando ha GPS; sem fix, cai para um contador de boot.
  // O mapa NAO e afetado por isto: ele mora em particoes brutas 0xDA achadas por
  // magic na MBR (sd_setor.h:79), fora de qualquer filesystem. O log vive na
  // particao FAT, que e disjunta - por isso da para gravar sem por o mapa em
  // risco quando o carro e desligado na chave.
  SD_MMC.mkdir("/log");
  if (utcSecDia)
    snprintf(r.caminho, sizeof(r.caminho), "/log/%02u%02u%02u_%lu.csv",
             (unsigned)(utcSecDia / 3600), (unsigned)((utcSecDia / 60) % 60),
             (unsigned)(utcSecDia % 60), (unsigned long)bootId);
  else
    snprintf(r.caminho, sizeof(r.caminho), "/log/boot%lu.csv", (unsigned long)bootId);

  r.f = SD_MMC.open(r.caminho, FILE_WRITE);
  r.ativo = (bool)r.f;
  if (!r.ativo) return false;
#else
  snprintf(r.caminho, sizeof(r.caminho), "(sem cartao)");
#endif

  // Cabecalho auto-explicativo: quem abrir o CSV daqui a tres meses precisa
  // entender sem abrir o firmware. O da v100 prometia R/T/J/Q que nao existiam;
  // este descreve so o que sai, coluna por coluna.
  registroLinha("# MTS campo v%d - uma linha por evento, primeira coluna = tipo; "
                "ms desde o boot; utc = segundo do dia (0 = sem hora do GPS)", MTS_VERSAO);
  registroLinha("# E,ms,utc,lat,lon,slot,sync(0 nada 1 beacon 2 pps),ancora,tx,perdi,preso,rx,ruim,"
                "conflito,rssiUlt,pwr,fix,sats,holdover,lacoPiorUs,lacoLimiteUs,"
                "seqPerdidos,ruidoUlt,ruidoMed,txAirUltUs,txAirMaxUs,rotaN,rotaPrefixo,"
                "catchups,relogioVoltou,pwrConfig,seguro,flushes,flushPiorUs,logDescartadas");
  registroLinha("# R,ms,utc,slotOrigem,seq,bytes,rssi,snr,freqErrHz,crcOk,perdidosDesdeUltimo,"
                "tipo(pos/roster/crc/sala/tamanho)[,flags,pontosTrilha,pontosNovosNaRota]");
  registroLinha("# T,ms,utc,seq,tipo(P/R),bytes,airMedidoUs,usNoQuadroAoIniciar,catchup,pontos");
  registroLinha("# N,ms,utc,rssiInstantaneo(ruido de fundo, sem pacote),slotVazioOndeMediu");
  registroLinha("# G,ms,evento,sats,vista,ppsRun,ppsCnt,sync    !,ms,evento,...    "
                "C,ms,caixa-preta do boot anterior (so apos reset violento)");
  return true;
}

// ------------------------------------------------------------------ flush
//
// So aqui o cartao e tocado. Chamado do laco principal a cada volta; ele mesmo
// decide se e hora. Devolve true se gravou.
inline bool registroTick(Orcamento& orc, const Tdma& t, bool txEmVoo, uint32_t agoraMs) {
  Registro& r = g_reg;
  if (!r.ativo || r.pendentes == 0) return false;

  bool porTempo = (agoraMs - r.ultFlushMs) >= REG_FLUSH_MS;
  bool porVolume = r.pendentes >= REG_FLUSH_BYTES;
  if (!porTempo && !porVolume) return false;

  // A GUARDA QUE FAZ ESTE ARQUIVO SER SEGURO. Sem ela, este logger seria o
  // proximo HTTPS do eco: mediria a instabilidade que ele mesmo causou.
  if (!orcamentoCabe(orc, t, txEmVoo, ORC_CARTAO_US)) { r.adiados++; return false; }

#if MTS_TEM_SD
  uint32_t t0 = micros();
  caixaEtapa(CP_CARTAO_FLUSH);     // se o cartao segurar ate o watchdog, o boot seguinte diz

  // Grava em ate duas fatias por causa da volta do anel. Uma chamada grande e
  // melhor que muitas pequenas: o custo do microSD e por comando, nao por byte.
  uint32_t n = r.pendentes;
  uint32_t ate = r.cap - r.le;
  uint32_t n1 = (n < ate) ? n : ate;
  size_t w = r.f.write((const uint8_t*)(r.buf + r.le), n1);
  if (n1 < n) w += r.f.write((const uint8_t*)r.buf, n - n1);
  r.f.flush();
  caixaEtapa(CP_LACO);

  uint32_t dt = micros() - t0;
  if (dt > r.piorFlushUs) r.piorFlushUs = dt;

  r.le = (r.le + w) % r.cap;
  r.pendentes -= w;
  r.bytesGravados += w;
  r.flushes++;
  r.ultFlushMs = agoraMs;

  // O custo do proprio flush entra no log. Um GC de cartao aparece aqui como um
  // numero grande e explica sozinho qualquer 'preso' que tenha vindo junto.
  if (dt > 50000UL)
    registroLinha("!,%lu,flush lento,%lu us,%lu bytes", (unsigned long)agoraMs,
                  (unsigned long)dt, (unsigned long)w);
  return true;
#else
  r.le = r.escreve; r.pendentes = 0; r.ultFlushMs = agoraMs;
  return false;
#endif
}

// Fecha de verdade. Chamado antes de qualquer reinicio deliberado - um arquivo
// FAT sem fechar perde o tamanho na tabela e o cartao volta com 0 byte.
inline void registroFecha() {
#if MTS_TEM_SD
  Registro& r = g_reg;
  if (!r.ativo) return;
  caixaEtapa(CP_CARTAO_FECHA);
  if (r.pendentes) {
    uint32_t ate = r.cap - r.le;
    uint32_t n1 = (r.pendentes < ate) ? r.pendentes : ate;
    r.f.write((const uint8_t*)(r.buf + r.le), n1);
    if (n1 < r.pendentes) r.f.write((const uint8_t*)r.buf, r.pendentes - n1);
    r.pendentes = 0;
  }
  r.f.close();
  r.ativo = false;
#endif
}

inline const char* registroCaminho() { return g_reg.caminho; }
inline bool         registroAtivo()   { return g_reg.ativo; }
