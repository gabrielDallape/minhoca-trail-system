#pragma once
// ============================================================================
//  ROTA CORE - o trajeto do lider com NUMERO DE PONTO, e o catch-up que o
//  preenche depois de um vao de radio. Sem hardware: so stdint/math.
// ============================================================================
//
// O PROBLEMA (regressao relatada no campo de 2026-09-28): "quando caia e
// reconectava, ele nao marcava o caminho guardado, que nem fazia antes. Agora so
// marca onde detectou o outro carro." O grupo_ws (LoRaMESH) mandava POSICAO +
// TRAJETO: os ultimos 24 pontos em todo pacote, e quando o seguidor acusava estar
// atrasado (ACK), o lider mandava o bloco a partir do que o seguidor JA tinha.
// O mts_campo so mandava a posicao atual (16 B), entao cada pacote perdido era
// um pedaco de caminho perdido para sempre - e o mapa ligava os dois lados do
// vao com uma reta, ou nao ligava.
//
// O QUE ESTE ARQUIVO FAZ
//
//   1. Cada ponto do trajeto tem um NUMERO (seq, 16 bits, rolante). O lider numera
//      os seus ao gravar (mundo.h); o seguidor guarda a rota ORDENADA por numero e
//      insere no lugar certo o que chegar fora de ordem (rotaInsere). Um salto de
//      numero entre dois pontos vizinhos e um VAO CONHECIDO: o mapa desenha
//      tracejado ali, mesmo que a distancia seja pequena, e nunca uma reta que
//      finge caminho. Quando o preenchimento chega, o vao some sozinho.
//
//   2. O seguidor diz no proprio pacote ate onde a rota dele e CONTINUA desde o
//      comeco (rotaAck). O lider compara com o numero do ultimo ponto dele: se
//      algum seguidor esta mais de TRILHA_PONTOS_PKT pontos atras, alterna os
//      quadros - um com os pontos mais recentes (o fluxo normal), outro com o bloco
//      a partir do ack (o preenchimento). Assim o caminho a frente nunca para de
//      chegar, e o buraco atras fecha a 11 pontos por quadro.
//
//   3. EPOCA: o lider que reinicia recomeca a numerar do zero. Sem tratar isso, os
//      pontos novos entrariam ANTES dos velhos na rota ordenada. Um byte de epoca
//      (sorteado no boot do lider) viaja no pacote; quando muda, a rota do
//      seguidor passa a por os numeros novos DEPOIS de tudo que ja tinha
//      (rotaSeqVirtual). O caminho antigo continua no mapa, sem mistura.
//
// FORMATO DO PACOTE "TRILHA" (radio.h monta e desmonta; aqui so a logica):
//   [0..15]  posicao normal (tdma_core.h), flags com TDMA_FL_TRILHA (0x40)
//   [16]     epoca do remetente
//   [17..18] seq do PRIMEIRO ponto do bloco (LE)
//   [19..20] ackAte: ate que seq do LIDER a minha rota e CONTINUA (LE); 0xFFFF = nada
//   [21..22] ackDe:  desde que seq do LIDER eu tenho alguma coisa (LE); 0xFFFF = nada
//   [23]     n = pontos no bloco (0..TRILHA_PONTOS_PKT)
//   [24..31] primeiro ponto ABSOLUTO (int32 lat, int32 lon, grau x 1e7)
//   depois   (n-1) x (int16 dlat, int16 dlon) em 1e-5 grau (~1,1 m), cada um
//            relativo ao ponto ANTERIOR do bloco
//
// POR QUE DOIS ACKS: "ate onde e continuo" fecha o buraco do MEIO (perdi sinal
// e voltei); "desde onde tenho" e o que diz ao lider que um seguidor que entrou
// TARDE nao tem o historico anterior - com um ack so, ele parecia em dia e nunca
// recebia o caminho ja percorrido (achado pelo espelho em Node antes da placa).
// O buraco do meio tem prioridade: e o caminho logo a frente do seguidor.
//
// Por que o primeiro ponto e absoluto e os outros sao delta: um bloco de catch-up
// pode comecar 500 m atras do carro (int16 x 1e-5 so alcanca +-327 m), mas entre
// dois pontos vizinhos do trajeto ha ~5 m. Se um delta nao couber (GPS ficou mudo
// por um trecho longo), o bloco e cortado ali e o resto vai no quadro seguinte.
//
// A CONTA DE BYTES / AIR-TIME esta no hardware.h (TDMA_SLOTS): 24 + 8 + 9 x 4 =
// 68 bytes = 417 ms em SF9, cabendo num slot de 500 ms com 63 ms de janela.

#include <stdint.h>
#include <string.h>
#include <math.h>

#define ROTA_MAX          1400        // ~7 km com passo de 5 m
#define ROTA_PASSO_M      5.0
#define ROTA_SEQ_NENHUMA  0xFFFFu     // ack "nao tenho nada da sua epoca"
#define TRILHA_CAB_N      24          // bytes antes do primeiro ponto
#define TRILHA_ABS_N      8           // primeiro ponto, absoluto
#define TRILHA_DELTA_N    4           // os seguintes
#define TRILHA_ESC_DELTA  1e5         // 1e-5 grau por unidade de delta
#define TRILHA_PKT_N(n)   ((n) == 0 ? TRILHA_CAB_N : TRILHA_CAB_N + TRILHA_ABS_N + ((n) - 1) * TRILHA_DELTA_N)

struct RotaPonto { double lat, lon; uint32_t vseq; };

struct Rota {
  RotaPonto p[ROTA_MAX];
  float     km[ROTA_MAX];      // metros acumulados desde o primeiro ponto
  int       n;
  bool      temEpoca;
  uint8_t   epoca;             // epoca do lider como vista por ultimo
  uint32_t  base;              // vseq do seq16 = 0 desta epoca (referencia do ack)
  uint32_t  ultVseq;           // ultimo par (vseq, seq16) visto: o proximo seq16 e
  uint16_t  ultSeq16;          // interpretado como deslocamento int16 a partir dele
  int       prefixo;           // indice do fim do trecho continuo desde p[0] (-1 = vazio)
  uint32_t  inseridos, repetidos, fora;   // diagnostico
};

inline float rotaD2(double la1, double lo1, double la2, double lo2)
{
  const double k = 111320.0;
  double dy = (la2 - la1) * k;
  double dx = (lo2 - lo1) * k * cos(la1 * M_PI / 180.0);
  return (float)(dx * dx + dy * dy);
}

inline void rotaLimpaCore(Rota& r)
{
  r.n = 0; r.temEpoca = false; r.epoca = 0; r.base = 0; r.ultVseq = 0; r.ultSeq16 = 0; r.prefixo = -1;
  r.inseridos = r.repetidos = r.fora = 0;
}

// Recalcula o prefixo continuo. O(n), mas so roda quando algo entrou ANTES do fim
// do prefixo - no fluxo normal (pontos chegando em ordem) e O(1).
inline void rotaRefazPrefixo(Rota& r, int desde)
{
  if (r.n == 0) { r.prefixo = -1; return; }
  int i = desde < 0 ? 0 : desde;
  if (i > r.prefixo + 1) i = r.prefixo + 1;
  if (i < 0) i = 0;
  while (i + 1 < r.n && r.p[i + 1].vseq == r.p[i].vseq + 1) i++;
  r.prefixo = i;
}

inline void rotaRefazKm(Rota& r, int desde)
{
  for (int i = desde < 1 ? 0 : desde; i < r.n; i++) {
    if (i == 0) { r.km[0] = 0; continue; }
    r.km[i] = r.km[i - 1] + sqrtf(rotaD2(r.p[i - 1].lat, r.p[i - 1].lon, r.p[i].lat, r.p[i].lon));
  }
}

// Traduz o seq de 16 bits do pacote para o seq virtual monotonico da rota. A
// EPOCA e do remetente: quando muda, tudo dele que chegar daqui em diante vai
// para DEPOIS do que ja existe.
// O seq16 ROLA (65535 -> 0) e um bloco pode apontar ate ROTA_MAX pontos para tras
// (catch-up) ou para a frente: a traducao e por deslocamento int16 a partir do
// ultimo par visto, nunca por soma direta - a soma direta quebrava a ordem na
// rolagem (achado pelo espelho em Node).
inline uint32_t rotaSeqVirtual(Rota& r, uint8_t epoca, uint16_t seq16)
{
  if (!r.temEpoca || r.epoca != epoca) {
    uint32_t topo = r.n ? r.p[r.n - 1].vseq + 1 : 0;
    // base tal que o seq 0 desta epoca caia acima de tudo que ja existe
    r.base = topo;
    r.ultVseq = topo + seq16; r.ultSeq16 = seq16;
    r.epoca = epoca; r.temEpoca = true;
    return r.ultVseq;
  }
  int32_t d = (int16_t)(uint16_t)(seq16 - r.ultSeq16);
  int64_t v = (int64_t)r.ultVseq + d;
  if (v < (int64_t)r.base) v = r.base;          // nunca abaixo do seq 0 da epoca
  r.ultVseq = (uint32_t)v; r.ultSeq16 = seq16;
  return r.ultVseq;
}

// Insere um ponto no lugar certo. Devolve true se entrou (false = repetido ou
// rota cheia e ponto mais antigo que tudo).
inline bool rotaInsere(Rota& r, uint32_t vseq, double lat, double lon)
{
  // caso comum: e o mais novo de todos
  int pos = r.n;
  if (r.n && vseq <= r.p[r.n - 1].vseq) {
    // busca binaria pelo primeiro indice com vseq >= pedido
    int lo = 0, hi = r.n - 1;
    while (lo < hi) { int m = (lo + hi) / 2; if (r.p[m].vseq < vseq) lo = m + 1; else hi = m; }
    pos = lo;
    if (r.p[pos].vseq == vseq) { r.repetidos++; return false; }
  }
  if (r.n >= ROTA_MAX) {
    // Cheia: descarta o COMECO, como antes. A rota longe demais atras nao ajuda
    // ninguem, e parar de gravar congelaria o caminho do lider justamente quando
    // ele esta mais longe. Um ponto mais antigo que tudo simplesmente nao entra.
    if (pos == 0) { r.fora++; return false; }
    memmove(r.p, r.p + 1, (ROTA_MAX - 1) * sizeof(RotaPonto));
    r.n--; pos--;
    r.prefixo = -1;            // o comeco mudou: o prefixo sera refeito abaixo
  }
  if (pos < r.n) {
    memmove(r.p + pos + 1, r.p + pos, (r.n - pos) * sizeof(RotaPonto));
  }
  r.p[pos] = { lat, lon, vseq };
  r.n++;
  r.inseridos++;
  rotaRefazKm(r, pos);
  if (r.prefixo < 0 || pos <= r.prefixo + 1) rotaRefazPrefixo(r, pos - 1);
  return true;
}

// ackAte: seq16 (na epoca atual do lider) ate onde a rota e continua desde o
// primeiro ponto que tenho. 0xFFFF quando nao ha nada da epoca atual - o lider
// entao manda desde o mais antigo que ainda guarda.
inline uint16_t rotaAck16(const Rota& r)
{
  if (r.prefixo < 0 || !r.temEpoca) return ROTA_SEQ_NENHUMA;
  uint32_t v = r.p[r.prefixo].vseq;
  if (v < r.base) return ROTA_SEQ_NENHUMA;      // o prefixo ainda e da epoca velha
  uint16_t s = (uint16_t)(v - r.base);
  return s == ROTA_SEQ_NENHUMA ? 0xFFFE : s;
}

// ackDe: seq16 do PRIMEIRO ponto que tenho da epoca atual. O lider compara com o
// mais antigo que ele guarda e, se eu nao tenho o comeco, manda o bloco logo
// abaixo do meu primeiro (o historico entra de tras para a frente).
inline uint16_t rotaAckDe16(const Rota& r)
{
  if (!r.temEpoca) return ROTA_SEQ_NENHUMA;
  for (int i = 0; i < r.n; i++)
    if (r.p[i].vseq >= r.base) { uint16_t s = (uint16_t)(r.p[i].vseq - r.base); return s == ROTA_SEQ_NENHUMA ? 0xFFFE : s; }
  return ROTA_SEQ_NENHUMA;
}

// Ha um vao CONHECIDO entre o ponto i-1 e o ponto i? (numero saltou)
inline bool rotaVaoAntes(const Rota& r, int i)
{
  return i > 0 && i < r.n && (r.p[i].vseq - r.p[i - 1].vseq) > 1;
}

// ------------------------------------------------------------------ lado do LIDER
//
// Escolhe o bloco a mandar neste quadro. Entradas:
//   head      = seq16 do ultimo ponto que eu gravei; trajN = quantos guardo
//   ackAteMin = o menor ackAte dos seguidores (ROTA_SEQ_NENHUMA se algum nao tem
//               nada, ou se ninguem informou)
//   ackDeMax  = o maior ackDe dos seguidores que tem alguma coisa
//   temAck    = pelo menos um seguidor informou ack
//   quadro    = contador de quadros, para alternar recente / preenchimento
// Saidas: base (seq16 do primeiro ponto) e n. Devolve true se e um bloco de
// preenchimento (catch-up), so para o log.
//
// Prioridade: buraco do MEIO (ackAte muito atras do head) antes do historico
// (ackDe acima do meu mais antigo). O do meio e o caminho logo a frente de quem
// perdeu sinal; o historico e conforto.
inline bool trilhaEscolheBloco(uint16_t head, int trajN, uint16_t ackAteMin, uint16_t ackDeMax,
                               bool temAck, uint32_t quadro, int maxPontos, uint16_t& base, int& n)
{
  if (trajN <= 0) { base = 0; n = 0; return false; }
  uint16_t oldest = (uint16_t)(head - (uint16_t)(trajN - 1));
  bool atras = false;
  uint16_t inicioCatch = oldest;
  int nCatch = maxPontos;
  if (temAck) {
    if (ackAteMin == ROTA_SEQ_NENHUMA) {
      atras = true;                                   // alguem nao tem nada: desde o comeco
    } else {
      int16_t d = (int16_t)(ackAteMin - head);
      if (d < -maxPontos) {                           // buraco no meio, maior que um bloco
        atras = true;
        inicioCatch = (uint16_t)(ackAteMin + 1);
        if ((int16_t)(inicioCatch - oldest) < 0) inicioCatch = oldest;   // perdeu alem do meu buffer
      } else if (d <= 0 && ackDeMax != ROTA_SEQ_NENHUMA && (int16_t)(ackDeMax - oldest) > 0) {
        // em dia na frente, mas falta historico atras: o bloco que termina logo
        // antes do primeiro ponto que ele tem
        atras = true;
        uint16_t fim = (uint16_t)(ackDeMax - 1);
        int disp = (int)(uint16_t)(fim - oldest) + 1;
        nCatch = disp < maxPontos ? disp : maxPontos;
        inicioCatch = (uint16_t)(fim - (uint16_t)(nCatch - 1));
      }
      // d > 0 = ack de uma epoca minha anterior (eu reiniciei): ignora
    }
  }
  bool catchup = atras && (quadro & 1);
  if (catchup) {
    base = inicioCatch;
    int disp = (int)(uint16_t)(head - base) + 1;
    n = disp < nCatch ? disp : nCatch;
  } else {
    n = trajN < maxPontos ? trajN : maxPontos;
    base = (uint16_t)(head - (uint16_t)(n - 1));
  }
  return catchup;
}

// ------------------------------------------------------------ codificacao
inline void trilhaPutLE32(uint8_t* p, int32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }
inline int32_t trilhaGetLE32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); }
inline void trilhaPutLE16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
inline uint16_t trilhaGetLE16(const uint8_t* p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

// Monta a cauda a partir de p[16]. 'ponto(i, lat, lon)' devolve o i-esimo ponto
// do bloco (0 = base). Devolve o tamanho TOTAL do pacote. Corta o bloco no
// primeiro delta que nao couber em int16.
template <typename FN>
inline int trilhaCodifica(uint8_t* pkt, uint8_t epoca, uint16_t base, uint16_t ackAte,
                          uint16_t ackDe, int n, FN ponto)
{
  pkt[16] = epoca;
  trilhaPutLE16(pkt + 17, base);
  trilhaPutLE16(pkt + 19, ackAte);
  trilhaPutLE16(pkt + 21, ackDe);
  int o = TRILHA_CAB_N;
  int usados = 0;
  double la0 = 0, lo0 = 0;
  for (int i = 0; i < n; i++) {
    double la, lo;
    ponto(i, la, lo);
    if (i == 0) {
      trilhaPutLE32(pkt + o, (int32_t)llround(la * 1e7)); o += 4;
      trilhaPutLE32(pkt + o, (int32_t)llround(lo * 1e7)); o += 4;
    } else {
      long dla = lround((la - la0) * TRILHA_ESC_DELTA);
      long dlo = lround((lo - lo0) * TRILHA_ESC_DELTA);
      if (dla > 32767 || dla < -32768 || dlo > 32767 || dlo < -32768) break;
      trilhaPutLE16(pkt + o, (uint16_t)(int16_t)dla); o += 2;
      trilhaPutLE16(pkt + o, (uint16_t)(int16_t)dlo); o += 2;
      // o receptor reconstroi somando deltas QUANTIZADOS, entao a referencia
      // do proximo delta tem de ser o valor quantizado, nao o real - senao o
      // erro de arredondamento acumula ao longo do bloco
      la = la0 + dla / TRILHA_ESC_DELTA;
      lo = lo0 + dlo / TRILHA_ESC_DELTA;
    }
    la0 = la; lo0 = lo;
    usados++;
  }
  pkt[23] = (uint8_t)usados;
  return o;
}

// Desmonta. Chama 'entrega(seq16, lat, lon)' para cada ponto. Devolve n, ou -1 se
// o pacote e curto demais para o que anuncia.
template <typename FN>
inline int trilhaDecodifica(const uint8_t* pkt, int len, uint8_t& epoca, uint16_t& base,
                            uint16_t& ackAte, uint16_t& ackDe, FN entrega)
{
  if (len < TRILHA_CAB_N) return -1;
  epoca  = pkt[16];
  base   = trilhaGetLE16(pkt + 17);
  ackAte = trilhaGetLE16(pkt + 19);
  ackDe  = trilhaGetLE16(pkt + 21);
  int n = pkt[23];
  if (n == 0) return 0;
  if (len < TRILHA_PKT_N(n)) return -1;
  int o = TRILHA_CAB_N;
  double la = trilhaGetLE32(pkt + o) / 1e7; o += 4;
  double lo = trilhaGetLE32(pkt + o) / 1e7; o += 4;
  entrega(base, la, lo);
  for (int i = 1; i < n; i++) {
    int16_t dla = (int16_t)trilhaGetLE16(pkt + o); o += 2;
    int16_t dlo = (int16_t)trilhaGetLE16(pkt + o); o += 2;
    la += dla / TRILHA_ESC_DELTA;
    lo += dlo / TRILHA_ESC_DELTA;
    entrega((uint16_t)(base + i), la, lo);
  }
  return n;
}
