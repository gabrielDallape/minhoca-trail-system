#pragma once
// ============================================================================
//  FROTA - os carros do grupo: quem esta onde, ha quanto tempo, e o que pediu
// ============================================================================
//
// E a fronteira entre o que a tela desenha e de onde o dado vem. A identidade de
// um carro e o SLOT, nao a ordem de chegada: se um carro reiniciar, volta no
// mesmo slot e reocupa a mesma linha em vez de virar um fantasma. O indice 0 e
// sempre EU.
//
// TEMPO ENTRA COMO PARAMETRO (agoraMs) - nada aqui le relogio, radio ou GPS.
// Assim a matematica dos TTLs e da extrapolacao roda no PC (mts_core_test.js).
//
// TRES ESTADOS DE SINAL por carro, com os tempos em params.h (FROTA_*):
//   VIVO      - pacote recente; posicao extrapolada por rumo x velocidade ate
//               FROTA_EXTRAPOLA_MAX_MS, para o mapa nao pular a cada 6 s
//   VELHO     - sem pacote ha mais que FROTA_SINAL_VELHO_MS: marcador congela e
//               fica vazado; a faixa diz SEM SINAL; se e o lider, a tela avisa
//   SUMIU     - sem pacote ha mais que FROTA_SOME_MS: sai da lista e da contagem

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "params.h"
#include "proto.h"
#include "geo.h"
#include "rota.h"
#include "cfg.h"          // CFG_N_CORES (so compilava pela ordem de quem incluia)

enum FrotaSinal : uint8_t { SINAL_NUNCA = 0, SINAL_VIVO, SINAL_VELHO, SINAL_SUMIU };

struct Carro {
  bool     ativo;
  uint8_t  slot;
  bool     lider;
  bool     alerta;
  uint8_t  cor;
  bool     temNome;
  char     nome[ROSTER_NOME_N + 1];
  // ultima posicao RECEBIDA (ou a minha, no indice 0)
  bool     fix;
  double   lat, lon;
  float    rumo;               // graus, 0 = norte
  uint8_t  vel;                // km/h
  bool     temPos;             // ja recebi ao menos uma posicao
  uint32_t posMs;              // quando a ultima posicao chegou
  uint32_t ultimoMs;           // ultimo sinal de vida (posicao); roster nao conta
  uint8_t  versaoAlheia;       // 0 = ok; senao a versao de protocolo que este slot anunciou
  // catch-up (core/rota.h): ate onde este carro diz ter a MINHA trilha
  bool     temAck;
  uint16_t ackAte, ackDe;
  uint32_t ackMs;
  uint16_t ackTopo;            // o ultimo ponto MEU que ele tem (0xFFFF = nenhum)
};

struct Frota {
  Carro    c[FROTA_MAX_CARROS];
  uint32_t conflitos;          // pacotes da minha sala com o MEU slot (outro aparelho igual a mim)
  uint32_t versaoErrada;       // pacotes de versao de protocolo diferente
  uint8_t  versaoErradaUlt;    // a ultima versao alheia ouvida (0 = nenhuma)
  uint32_t versaoErradaMs;     // quando
};

inline void frotaLimpaCarro(Carro& k) { memset(&k, 0, sizeof(k)); }

inline void frotaInicia(Frota& f)
{
  memset(&f, 0, sizeof(f));
}

// QUEM EU SOU, desde o boot - o roster sai certo desde o primeiro anuncio.
inline void frotaIdentidade(Frota& f, const char* nome, uint8_t cor, uint8_t slot)
{
  Carro& eu = f.c[0];
  eu.ativo = true; eu.slot = slot; eu.cor = cor;
  strncpy(eu.nome, nome, sizeof(eu.nome) - 1); eu.nome[sizeof(eu.nome) - 1] = 0;
  eu.temNome = true;
}

inline int frotaAcha(const Frota& f, uint8_t slot)
{
  for (int i = 1; i < FROTA_MAX_CARROS; i++)
    if (f.c[i].ativo && f.c[i].slot == slot) return i;
  return -1;
}

inline int frotaVaga(Frota& f, uint8_t slot, uint32_t agoraMs)
{
  for (int i = 1; i < FROTA_MAX_CARROS; i++) {
    if (f.c[i].ativo) continue;
    frotaLimpaCarro(f.c[i]);
    f.c[i].ativo = true;
    f.c[i].slot = slot;
    // COR PELO SLOT ate o roster chegar, para a cor ser a mesma em todas as
    // telas do grupo. Rotulo provisorio = o slot, o unico identificador que ja se sabe.
    f.c[i].cor = (uint8_t)(slot % CFG_N_CORES);
    snprintf(f.c[i].nome, sizeof(f.c[i].nome), "CARRO %u", (unsigned)slot);
    f.c[i].temNome = false;
    f.c[i].ultimoMs = agoraMs;
    return i;
  }
  return -1;
}

// Chegou pacote de posicao da MINHA sala. Devolve o indice, -1 sem vaga, -2 conflito
// (outro aparelho com o meu slot), -3 slot invalido.
inline int frotaRecebePos(Frota& f, const ProtoPos& k, uint32_t agoraMs, bool& alertaMudou)
{
  alertaMudou = false;
  if (k.slot >= FROTA_MAX_CARROS) return -3;
  if (k.slot == f.c[0].slot) { f.conflitos++; return -2; }
  int i = frotaAcha(f, k.slot);
  if (i < 0) i = frotaVaga(f, k.slot, agoraMs);
  if (i < 0) return -1;
  Carro& c = f.c[i];
  c.lat = k.lat; c.lon = k.lon;
  c.fix = (k.flags & FL_FIX) != 0;
  c.rumo = k.rumo; c.vel = k.vel;
  c.temPos = true; c.posMs = agoraMs;
  bool al = (k.flags & FL_ALERTA) != 0;
  alertaMudou = (c.alerta != al);
  c.alerta = al;
  c.lider = (k.flags & FL_LIDER) != 0;
  c.ultimoMs = agoraMs;
  c.versaoAlheia = 0;
  return i;
}

// Roster NAO carimba ultimoMs: presenca e provada por POSICAO. Aceita roster de
// quem ainda nao mandou posicao (a sala de espera vive disso).
inline int frotaRecebeRoster(Frota& f, uint8_t slot, uint8_t cor, bool lider, const char* nome,
                             uint32_t agoraMs, bool& nomeMudou)
{
  nomeMudou = false;
  if (slot >= FROTA_MAX_CARROS) return -3;
  if (slot == f.c[0].slot) { f.conflitos++; return -2; }
  int i = frotaAcha(f, slot);
  if (i < 0) i = frotaVaga(f, slot, agoraMs);
  if (i < 0) return -1;
  Carro& c = f.c[i];
  nomeMudou = !c.temNome || strncmp(c.nome, nome, sizeof(c.nome) - 1) != 0;
  strncpy(c.nome, nome, sizeof(c.nome) - 1); c.nome[sizeof(c.nome) - 1] = 0;
  c.temNome = true;
  c.lider = lider;
  // A COR VEM DO DONO: cada um escolhe a sua, e e por ela que os outros o
  // chamam no radio ("segue o azul").
  if (cor < CFG_N_CORES) c.cor = cor;
  return i;
}

// Ouvi um pacote de OUTRA versao de protocolo. So conta e guarda para a tela
// avisar; nao sabemos nem a sala (o formato antigo nao tem versao).
inline void frotaVersaoAlheia(Frota& f, uint8_t versao, uint8_t slotDeclarado, uint32_t agoraMs)
{
  f.versaoErrada++;
  f.versaoErradaUlt = versao;
  f.versaoErradaMs = agoraMs;
  if (slotDeclarado < FROTA_MAX_CARROS) {
    int i = frotaAcha(f, slotDeclarado);
    if (i > 0) f.c[i].versaoAlheia = versao;
  }
}

// O ack de trilha de um carro (para o lider escolher o bloco).
inline void frotaAck(Frota& f, int i, uint16_t ackAte, uint16_t ackDe, uint16_t topo, uint32_t agoraMs)
{
  if (i <= 0 || i >= FROTA_MAX_CARROS) return;
  Carro& c = f.c[i];
  c.temAck = true; c.ackAte = ackAte; c.ackDe = ackDe; c.ackTopo = topo; c.ackMs = agoraMs;
}

// Os acks vivos combinados: MENOR ackAte (quem esta mais atras manda) e MAIOR
// ackDe (o historico mais perto do presente primeiro). false se ninguem informou.
//
// SO CONTA QUEM ME OUVE: o topo do carro (o ultimo ponto meu que ele tem) tem de
// estar a ate ROTA_OUVE_PONTOS do meu 'head'. Um seguidor surdo tem o ack parado
// para sempre, e pela regra do "menor ack manda" ele decidia o bloco de todo
// mundo: o lider repetia para ele o mesmo bloco inutil e o vao dos outros nunca
// fechava (revisao de 2026-09-29). Quando volta a ouvir, o primeiro bloco recente
// ja poe o topo dele no presente e ele volta a contar - sem relogio nenhum.
inline bool frotaAckMin(const Frota& f, uint16_t head, uint16_t& ackAteMin, uint16_t& ackDeMax, uint32_t agoraMs)
{
  bool tem = false, temDe = false;
  uint16_t m = 0, de = 0;
  for (int i = 1; i < FROTA_MAX_CARROS; i++) {
    const Carro& c = f.c[i];
    if (!c.ativo || !c.temAck || (agoraMs - c.ackMs) > ROTA_ACK_TTL_MS) continue;
    if (c.ackTopo == ROTA_SEQ_NENHUMA || (int16_t)(c.ackTopo - head) < -ROTA_OUVE_PONTOS) continue;
    if (!tem) { m = c.ackAte; tem = true; }
    else if (c.ackAte == ROTA_SEQ_NENHUMA || m == ROTA_SEQ_NENHUMA) m = ROTA_SEQ_NENHUMA;
    else if ((int16_t)(c.ackAte - m) < 0) m = c.ackAte;
    if (c.ackDe != ROTA_SEQ_NENHUMA) {
      if (!temDe) { de = c.ackDe; temDe = true; }
      else if ((int16_t)(c.ackDe - de) > 0) de = c.ackDe;
    }
  }
  ackAteMin = m;
  ackDeMax  = temDe ? de : ROTA_SEQ_NENHUMA;
  return tem;
}

// ---------------------------------------------------------------- tempo
inline FrotaSinal frotaSinal(const Carro& c, uint32_t agoraMs)
{
  if (!c.ativo || !c.temPos) return SINAL_NUNCA;
  uint32_t q = agoraMs - c.posMs;
  if (q > FROTA_SOME_MS) return SINAL_SUMIU;
  if (q > FROTA_SINAL_VELHO_MS) return SINAL_VELHO;
  return SINAL_VIVO;
}

inline uint32_t frotaSilencioMs(const Carro& c, uint32_t agoraMs)
{
  return c.temPos ? (agoraMs - c.posMs) : 0xFFFFFFFFu;
}

// Envelhece a lista. DOIS ESTAGIOS de proposito: perder o sinal e reversivel
// (sombra de morro) e nao pode ser nervoso; mas ALGUEM tem de zerar o ativo,
// senao quem desligou fica na lista e na contagem para sempre - o carro zumbi
// que o grupo_ws ja teve e ja corrigiu. Devolve quantos sairam.
inline int frotaEnvelhece(Frota& f, uint32_t agoraMs)
{
  int sairam = 0;
  for (int i = 1; i < FROTA_MAX_CARROS; i++) {
    Carro& c = f.c[i];
    if (!c.ativo) continue;
    uint32_t quieto = agoraMs - c.ultimoMs;
    if (quieto > FROTA_SOME_MS) { frotaLimpaCarro(c); sairam++; }
  }
  return sairam;
}

// A POSICAO PARA DESENHAR: a recebida, avancada pelo rumo e pela velocidade do
// pacote, ate FROTA_EXTRAPOLA_MAX_MS. Depois disso congela (mostrar 250 m de
// chute como certeza e pior que mostrar o ultimo ponto conhecido). Devolve false
// se nao ha posicao com fix.
inline bool frotaEstimativa(const Carro& c, uint32_t agoraMs, double& lat, double& lon, bool& extrapolada)
{
  extrapolada = false;
  if (!c.ativo || !c.temPos || !c.fix) return false;
  lat = c.lat; lon = c.lon;
  uint32_t dt = agoraMs - c.posMs;
  if (dt > FROTA_EXTRAPOLA_MAX_MS) dt = FROTA_EXTRAPOLA_MAX_MS;
  if (c.vel == 0 || dt == 0) return true;
  double metros = (double)c.vel / 3.6 * (double)dt / 1000.0;
  geoAvanca(c.lat, c.lon, c.rumo, metros, lat, lon);
  extrapolada = true;
  return true;
}

inline int frotaQuantos(const Frota& f)
{
  int n = 0;
  for (int i = 0; i < FROTA_MAX_CARROS; i++) if (f.c[i].ativo) n++;
  return n;
}

inline int frotaLider(const Frota& f)
{
  for (int i = 1; i < FROTA_MAX_CARROS; i++) if (f.c[i].ativo && f.c[i].lider) return i;
  return -1;
}

// Esquece todos os OUTROS. Chamar sempre que a sala muda: a sala de espera de um
// grupo recem-criado nasceria com os carros do grupo anterior dentro.
inline void frotaLimpaOutros(Frota& f)
{
  for (int i = 1; i < FROTA_MAX_CARROS; i++) frotaLimpaCarro(f.c[i]);
  f.conflitos = 0;
}
