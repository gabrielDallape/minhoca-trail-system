#pragma once
// ============================================================================
//  REDE - o no no ar: TDMA + protocolo + frota + rota, sobre o hal/radio.h
// ============================================================================
//
// Aqui se decide QUANDO falar (tdma_core), O QUE falar (posicao com cauda de
// trilha, ou roster), o que fazer com o que chegou (frota, rota, ancora de
// tempo) e o que escrever no diario (linhas R/T/N/V). O chip e problema do
// hal/radio.h; o carro e problema do core/frota.h.
//
// AS REGRAS QUE VIERAM DO CAMPO e que este arquivo carrega:
//
//   - A POTENCIA NUNCA MUDA SOZINHA (usuario, 2026-09-28 e 2026-09-29). Nao
//     existe recuo automatico, resgate de boot nem teto de "modo seguro". Apos
//     RADIO_SEGURO_APOS reinicios violentos seguidos (hal/caixa.h) o que acontece
//     e um ALARME: faixa vermelha na tela e linha no diario, com a potencia
//     intacta. Ate 2026-09-29 esse caso baixava para 2 dBm em RAM - e somado a
//     um boot-loop por radio mudo, terminava pior que os 6 dBm do campo.
//   - Um pacote de VERSAO diferente e ignorado, contado e AVISADO na tela.
//   - A primeira fala e sempre POSICAO (roster nao ancora ninguem); os rosters
//     de arranque sao INTERCALADOS (falas 1, 3, 5) para nunca haver dois quadros
//     seguidos sem beacon - com 6 s de quadro, dois quadros mudos passavam do
//     TTL de holdover de entao (12,5 s) e a rede se partia no arranque (I5).
//     Hoje o TTL e de 4 quadros (core/tdma.h), mas a regra continua.
//   - O vigia de TX preso pergunta ao chip antes de acusar, espera mais que o
//     maior pacote (RADIO_VIGIA_MS), e refaz o begin() completo.
//   - O mapa e o cartao perguntam ao orcamento antes de gastar tempo; a
//     pergunta e respondida aqui (redePodeGastar).

#include <stdint.h>
#include <string.h>
#include "../core/params.h"
#include "../core/airtime.h"
#include "../core/proto.h"
#include "../core/tdma.h"
#include "../core/frota.h"
#include "../core/rota.h"
#include "../core/trajeto.h"
#include "../core/orcamento.h"
#include "../core/vizinhos.h"
#include "../hal/pinos.h"
#include "../hal/relogio.h"
#include "../hal/caixa.h"
#include "estado.h"
#include "registro.h"
#if MTS_TEM_RADIO
#include "../hal/radio.h"
#endif
#if MTS_TEM_GPS
#include "../hal/gps.h"
#endif

// ------------------------------------------------------------ dimensionamento
// O MAIOR pacote que este firmware transmite: posicao + cauda de trilha cheia. E
// ele que dimensiona a janela de TX no tdma_core (um menor so sobra folga).
#define REDE_PKT_MAX      (TRILHA_PKT_N(TRILHA_PONTOS_PKT))
#define REDE_AIRTIME_US   airtimeUs(REDE_PKT_MAX)
// O vigia de TX preso tem de esperar mais que o maior pacote (417 ms) - 400 ms
// fixos contariam 'preso' em toda transmissao longa e refariam o begin() no meio.
#define RADIO_VIGIA_MS    (REDE_AIRTIME_US / 1000UL + 200UL)
// Janela que sobra para o laco decidir transmitir dentro do slot.
#define REDE_JANELA_US    (TDMA_SLOT_US - TDMA_GUARD_US - REDE_AIRTIME_US)

static_assert(REDE_PKT_MAX <= 255, "pacote maior que o FIFO do SX1262");
static_assert(PROTO_ROSTER_N <= REDE_PKT_MAX, "roster maior que o pacote maximo");
static_assert(REDE_AIRTIME_US + TDMA_GUARD_US + 40000UL <= TDMA_SLOT_US,
              "o maior pacote nao cabe no slot com 40 ms de janela para o laco: reduza TRILHA_PONTOS_PKT");
static_assert(REDE_JANELA_US >= 60000UL, "menos de 60 ms de janela para o laco decidir o TX");
static_assert(RADIO_VIGIA_MS < TDMA_SLOT_US / 1000UL + 200UL, "vigia de TX mais lento que um slot inteiro");

struct Rede {
  Tdma     tdma;
  uint16_t sala;
  bool     ok;                 // radio subiu
  // TX em curso
  bool     txEmVoo;
  uint32_t txDesdeMs;
  uint64_t txIniUs;
  uint8_t  txLen;
  char     txTipo;             // 'P' posicao/trilha, 'R' roster, 0 = nenhum
  uint32_t txUsInFrame;
  bool     txCatchup;
  uint8_t  txPontos;
  uint8_t  seq;
  uint32_t falas;              // minhas falas desde a ultima troca de slot/sala
  uint32_t quadroTx, catchups;
  // contadores
  uint32_t txPreso;
  uint32_t rxOk, rxRuim, rxForaDaSala, rxTamanho;
  float    rxRssi, rxSnr;
  bool     seqTem[FROTA_MAX_CARROS];
  uint8_t  seqUlt[FROTA_MAX_CARROS];
  uint32_t seqPerd[FROTA_MAX_CARROS], rxPorSlot[FROTA_MAX_CARROS];
  uint32_t seqPerdTotal;
  uint32_t txAirUltUs, txAirMaxUs, txAirMinUs;
  // ruido de fundo
  float    ruidoUlt, ruidoSoma, ruidoMin, ruidoMax;
  uint32_t ruidoN, ruidoFrame;
  uint8_t  ruidoSlotProx;
  // potencia
  int8_t   pwrConfig;          // a que o USUARIO escolheu (NVS)
  bool     seguro;             // alarme de reinicios seguidos (so aviso, NAO mexe na potencia)
  // bancada
  bool     rssiVerboso;        // 'm' no serial: uma linha por pacote recebido
  uint32_t relogioVoltou;
};
static Rede g_rede;
// O orcamento de laco (core/orcamento.h): quem responde "cabe custoUs agora?".
static Orcamento g_orc;
// Os grupos que estao no ar e nao sao o meu (core/vizinhos.h): a tela ENTRAR lista.
static Vizinhos g_viz;

inline int8_t redePotencia()       { return MTS_TEM_RADIO ? g_rhw.pwr : g_rede.pwrConfig; }
inline int8_t redePotenciaConfig() { return g_rede.pwrConfig; }
inline bool   redeEmModoSeguro()   { return g_rede.seguro; }
inline bool   redeTxEmVoo()        { return g_rede.txEmVoo; }
inline bool   redeOk()             { return g_rede.ok; }
inline uint32_t redeUtc()
{
#if MTS_TEM_GPS
  return gpsUtc();
#else
  return 0;
#endif
}

#if MTS_TEM_RADIO
// ------------------------------------------------------------------ inicio
inline void redeZeraSeq()
{
  for (int s = 0; s < FROTA_MAX_CARROS; s++) { g_rede.seqTem[s] = false; }
}

inline bool redeInicia(uint8_t meuSlot, const char* codigoSala, int8_t pwrConfig, bool seguro)
{
  Rede& r = g_rede;
  memset(&r, 0, sizeof(r));
  r.pwrConfig = pwrConfig;
  r.seguro = seguro;
  r.txAirMinUs = 0xFFFFFFFFu;
  r.ruidoFrame = 0xFFFFFFFFu;
  r.ruidoMax = -200; r.ruidoSlotProx = RUIDO_SLOT_INI;
  r.sala = protoSalaDoCodigo(codigoSala);
  vizinhosInicia(g_viz, protoSalaHash("SEMSALA"));
  const int8_t pwr = pwrConfig;

  tdmaInit(r.tdma, meuSlot, TDMA_SLOTS, TDMA_FRAME_S, TDMA_GUARD_US);
  tdmaSetAirtime(r.tdma, REDE_AIRTIME_US);
  r.ok = radioHwInicia(pwr);
  if (!r.ok) return false;

  int32_t folga = tdmaHeadroomUs(r.tdma, REDE_AIRTIME_US);
  Serial.printf("rede: slot %u de %d | quadro %d s | slot %lu us | folga %ld us | proto v%d\n",
                meuSlot, TDMA_SLOTS, TDMA_FRAME_S, (unsigned long)r.tdma.slotUs, (long)folga, PROTO_VERSAO);
  Serial.printf("rede: air-time SF%d/BW%.0f: 16 B = %lu us | roster %d B = %lu us | trilha %d B = %lu us (vigia %lu ms)\n",
                RF_SF, RF_BW_KHZ, (unsigned long)airtimeUs(16), (int)PROTO_ROSTER_N,
                (unsigned long)airtimeUs(PROTO_ROSTER_N), (int)REDE_PKT_MAX,
                (unsigned long)REDE_AIRTIME_US, (unsigned long)RADIO_VIGIA_MS);
  Serial.printf("rede: no ar em %.1f MHz, sala 0x%04X, potencia %d dBm (~%d na antena)%s\n",
                RF_FREQ_MHZ, r.sala, pwr, radioHwPotenciaAntena(pwr),
                seguro ? "  *** REINICIOS SEGUIDOS: conferir o 5V do modulo ***" : "");
  if (pwrConfig < RF_PWR_PADRAO)
    Serial.printf("rede: *** ATENCAO: %d dBm e MENOS que o padrao (%d). Veio da NVS. 'p%d' no serial volta. ***\n",
                  pwrConfig, RF_PWR_PADRAO, RF_PWR_PADRAO);
  return true;
}

// O grupo so existe depois que o usuario cria ou entra num; ate la o radio esta
// no ar numa sala neutra. Trocar a sala e trocar o filtro.
inline void redeTrocaSala(const char* cod)
{
  uint16_t nova = protoSalaDoCodigo(cod);
  if (nova == g_rede.sala) return;
  g_rede.sala = nova;
  g_rede.rxOk = g_rede.rxRuim = g_rede.rxForaDaSala = 0;
  redeZeraSeq();
  g_rede.falas = 0;             // M18: anuncia o roster de novo logo
  Serial.printf("rede: sala agora e 0x%04X (codigo %s)\n", nova, cod);
  registroLinha("!,%lu,sala,0x%04X", (unsigned long)millis(), nova);
}

// Troca o slot EM EXECUCAO. O tdmaInit zera a ancora de proposito: com o slot
// novo, a posicao no quadro precisa ser reaprendida.
inline void redeTrocaSlot(uint8_t novo)
{
  if (novo >= FROTA_MAX_CARROS) return;
  g_frota.c[0].slot = novo;
  if (!g_rede.ok) return;
  tdmaInit(g_rede.tdma, novo, TDMA_SLOTS, TDMA_FRAME_S, TDMA_GUARD_US);
  tdmaSetAirtime(g_rede.tdma, REDE_AIRTIME_US);
  g_rede.falas = 0;
  Serial.printf("rede: slot agora e %u (aplicado sem reiniciar)\n", novo);
  registroLinha("!,%lu,slot,%u", (unsigned long)millis(), (unsigned)novo);
}

// ------------------------------------------------------------------ potencia
// Espera um TX em voo terminar ANTES de mexer no chip (I10): trocar com TX em
// voo deu ESP_RST_PANIC. Devolve false se nao aplicou.
inline bool redeAplicaPotencia(int8_t pwr)
{
  if (!g_rede.ok) return false;
  if (g_rede.txEmVoo) {
    uint32_t t0 = millis();
    while ((millis() - t0) < RADIO_VIGIA_MS) {
      uint64_t us;
      if (radioHwIrqPega(us) || radioHwTxDoneNoChip()) { g_rede.txEmVoo = false; radioHwFimTx(); break; }
      delay(2);
    }
    if (g_rede.txEmVoo) { Serial.println("rede: TX nao completou - potencia NAO trocada (mande de novo)"); return false; }
  }
  radioHwAplicaPotencia(pwr);
  Serial.printf("rede: potencia agora e %d dBm (~%d na antena)\n", pwr, radioHwPotenciaAntena(pwr));
  return true;
}

// COMANDO EXPLICITO do usuario ('p' no serial, tela de diagnostico). O UNICO
// caminho que muda a potencia configurada. Quem grava na NVS e o chamador (app),
// porque e ele quem tem o Cfg. Se o TX em voo nao deixou aplicar AGORA, o valor
// pedido fica valendo e o redeAtualiza aplica assim que o radio estiver livre -
// antes, a NVS ficava com o novo e o chip com o velho, sem ninguem perceber.
inline void redeTrocaPotencia(int8_t nova)
{
  if (nova < RF_PWR_MIN) nova = RF_PWR_MIN;
  if (nova > RF_PWR_MAX) nova = RF_PWR_MAX;
  g_rede.pwrConfig = nova;
  registroLinha("!,%lu,potencia por comando,%d dBm", (unsigned long)millis(), (int)nova);
  redeAplicaPotencia(nova);
}

// ------------------------------------------------------------------ TX
inline void redeTransmite(const uint8_t* buf, int n, char tipo)
{
  Rede& r = g_rede;
  if (!radioHwBusyLivre()) {
    // Marca TX em voo para o vigia reconfigurar pelo caminho que ja existe.
    r.txEmVoo = true; r.txDesdeMs = millis(); r.txTipo = tipo; r.txLen = (uint8_t)n;
    Serial.printf("rede: BUSY preso alto - NAO transmiti (%c); o vigia reconfigura\n", tipo);
    registroLinha("!,%lu,busy preso,%c", (unsigned long)millis(), tipo);
    return;
  }
  uint32_t usInFrame = 0, frameIdx = 0;
  tdmaUsInFrame(r.tdma, usInFrame, frameIdx);
  r.txEmVoo = true; r.txDesdeMs = millis();
  r.txTipo = tipo; r.txLen = (uint8_t)n; r.txUsInFrame = usInFrame;
  r.txIniUs = relogioUs();
  int st = radioHwTransmite(buf, n);
  if (st != RADIOLIB_ERR_NONE) {
    r.txEmVoo = false;
    Serial.printf("rede: startTransmit(%c) -> %d\n", tipo, st);
    registroLinha("!,%lu,startTransmit falhou,%c,%d", (unsigned long)millis(), tipo, st);
  }
}

inline void redeTransmiteRoster()
{
  static uint8_t buf[REDE_PKT_MAX];
  const Carro& eu = g_frota.c[0];
  int n = protoPackRoster(buf, g_rede.sala, eu.slot, g_rede.seq++, eu.cor, eu.lider, eu.nome);
  g_rede.txCatchup = false; g_rede.txPontos = 0;
  redeTransmite(buf, n, 'R');
}

// POSICAO + CAUDA DE TRILHA. Todo no manda o ack; so o LIDER manda pontos - o
// rastro de um seguidor nao e caminho para ninguem.
inline void redeTransmitePosicao()
{
  static uint8_t buf[REDE_PKT_MAX];
  Rede& r = g_rede;
  const Carro& eu = g_frota.c[0];
  ProtoPos k;
  k.sala = r.sala; k.slot = eu.slot; k.seq = r.seq++;
  k.flags = FL_TRILHA;
  if (g_eu.fix) k.flags |= FL_FIX;
  // Anuncia que o MEU relogio esta na grade UTC: quem me ouve prefere esta fonte
  // a qualquer beacon de grade arbitraria (impede a rede de se partir em duas).
  if (r.tdma.sync == TDMA_SYNC_PPS && !r.tdma.holdover) k.flags |= FL_PPS;
  if (eu.alerta) k.flags |= FL_ALERTA;
  if (eu.lider)  k.flags |= FL_LIDER;
  k.lat = g_eu.lat; k.lon = g_eu.lon; k.rumo = g_eu.rumo; k.vel = g_eu.vel;
  protoPackPos(buf, k);

  uint16_t base = 0; int nPts = 0; bool catchup = false;
  if (eu.lider && g_traj.n > 0) {
    uint16_t ackAteMin = ROTA_SEQ_NENHUMA, ackDeMax = ROTA_SEQ_NENHUMA;
    // so conta quem me ouve (o topo dele perto do meu head) - ver frotaAckMin
    bool temAck = frotaAckMin(g_frota, g_traj.seqUlt, ackAteMin, ackDeMax, millis());
    catchup = trilhaEscolheBloco(g_traj.seqUlt, g_traj.n, ackAteMin, ackDeMax, temAck, r.quadroTx,
                                 TRILHA_PONTOS_PKT, base, nPts);
  }
  // O LIDER nao tem ack a dar: no lugar do ackDe vai o seq do ponto MAIS ANTIGO
  // que ele guarda - o piso de quem segue (core/rota.h, rotaDefinePiso).
  uint16_t ackAte = rotaAck16(g_rotaC), ackDe = rotaAckDe16(g_rotaC);
  if (eu.lider) {
    ackAte = ROTA_SEQ_NENHUMA;
    ackDe = g_traj.n > 0 ? (uint16_t)(g_traj.seqUlt - (uint16_t)(g_traj.n - 1)) : ROTA_SEQ_NENHUMA;
  } else {
    // O seguidor nao manda pontos, entao o 'base' vai livre: leva o TOPO, o ultimo
    // ponto do lider que eu tenho. E assim que o lider sabe que eu o ouco.
    base = rotaTopo16(g_rotaC);
  }
  r.quadroTx++;
  int n = trilhaCodifica(buf, g_traj.epoca, base, ackAte, ackDe, nPts,
    [&](int i, double& la, double& lo) {
      if (!trajetoPontoSeq(g_traj, (uint16_t)(base + i), la, lo)) { la = g_eu.lat; lo = g_eu.lon; }
    });
  r.txCatchup = catchup; r.txPontos = buf[23];
  if (catchup) r.catchups++;
  redeTransmite(buf, n, 'P');
}

// ------------------------------------------------------------------ RX
inline void redeRecebe(uint64_t rxUs)
{
  static uint8_t buf[REDE_PKT_MAX];
  Rede& r = g_rede;
  const uint32_t agora = millis();
  int n; float rssi, snr, ferr;
  int st = radioHwLeRx(buf, REDE_PKT_MAX, n, rssi, snr, ferr);
  if (st == -1) {
    r.rxRuim++; r.rxTamanho++;
    registroLinha("R,%lu,%lu,-1,-1,%d,%.0f,%.1f,0,0,0,tamanho", (unsigned long)agora, (unsigned long)redeUtc(), n, rssi, snr);
    return;
  }
  if (st != RADIOLIB_ERR_NONE) {
    r.rxRuim++;                      // CRC ruim: qualidade do enlace
    registroLinha("R,%lu,%lu,-1,-1,%d,%.0f,%.1f,%.0f,0,0,crc %d", (unsigned long)agora, (unsigned long)redeUtc(), n, rssi, snr, ferr, st);
    radioHwRx();
    return;
  }
  // VERSAO DO PROTOCOLO antes de qualquer outra coisa. Um aparelho com firmware
  // antigo (ou mais novo) no ar e contado, guardado e avisado na tela.
  if (!protoVersaoOk(buf, n)) {
    uint8_t v = protoVersao(buf, n);
    frotaVersaoAlheia(g_frota, v, n >= 4 ? buf[3] : 0xFF, agora);
    registroLinha("V,%lu,%lu,%u,%u,%d,%.0f", (unsigned long)agora, (unsigned long)redeUtc(),
                  (unsigned)v, (unsigned)(n >= 4 ? buf[3] : 255), n, rssi);
    if (g_frota.versaoErrada == 1 || (g_frota.versaoErrada % 50) == 0)
      Serial.printf("rede: *** pacote de protocolo v%u no ar (eu falo v%d) - aparelho com firmware incompativel ***\n",
                    (unsigned)v, PROTO_VERSAO);
    radioHwRx();
    return;
  }
  uint16_t sala = protoSala(buf);
  uint8_t slot = buf[3], seq = buf[4];
  if (sala != r.sala) {
    r.rxForaDaSala++;
    // Antes de descartar: DE QUEM era. E o que a tela ENTRAR lista para entrar com
    // um toque (core/vizinhos.h - o teste de 2026-09-30 ficou em grupos trocados).
    {
      char nomeViz[ROSTER_NOME_N + 1]; const char* pn = nullptr;
      if (protoEhRoster(buf, n)) { protoRosterNome(buf, nomeViz, sizeof(nomeViz)); pn = nomeViz; }
      vizinhosOuve(g_viz, sala, slot, n >= 6 && (buf[5] & FL_LIDER), pn, (int16_t)rssi, agora);
    }
    registroLinha("R,%lu,%lu,%u,%u,%d,%.0f,%.1f,%.0f,1,0,sala 0x%04X", (unsigned long)agora, (unsigned long)redeUtc(),
                  (unsigned)slot, (unsigned)seq, n, rssi, snr, ferr, sala);
    radioHwRx();
    return;
  }
  r.rxOk++; r.rxRssi = rssi; r.rxSnr = snr;
  // PERDA POR SEQUENCIA deste remetente (roster e posicao usam o mesmo contador).
  uint32_t perdidos = 0;
  if (slot < FROTA_MAX_CARROS) {
    if (r.seqTem[slot]) perdidos = (uint8_t)(seq - r.seqUlt[slot] - 1);
    r.seqTem[slot] = true; r.seqUlt[slot] = seq;
    r.seqPerd[slot] += perdidos; r.rxPorSlot[slot]++;
    r.seqPerdTotal += perdidos;
  }
  if (r.rssiVerboso) Serial.printf("MEDIDA %u %.0f %.1f\n", (unsigned)slot, rssi, snr);

  if (protoEhRoster(buf, n)) {
    // Roster NAO ancora o tempo: e mais longo, o rxDone cai mais tarde, e usa-lo
    // como beacon deslocaria a ancora ~6 ms a cada oito quadros (erro periodico).
    char nome[ROSTER_NOME_N + 1];
    protoRosterNome(buf, nome, sizeof(nome));
    bool nomeMudou;
    int idx = frotaRecebeRoster(g_frota, slot, protoRosterCor(buf), (buf[5] & FL_LIDER) != 0, nome, agora, nomeMudou);
    if (idx == -2) Serial.printf("*** OUTRO aparelho esta no MEU slot (%u)! conflitos=%lu\n", (unsigned)slot, (unsigned long)g_frota.conflitos);
    else if (idx > 0 && nomeMudou) Serial.printf("frota: slot %u se apresentou: %s%s\n", slot, nome, (buf[5] & FL_LIDER) ? " (lider)" : "");
    registroLinha("R,%lu,%lu,%u,%u,%d,%.0f,%.1f,%.0f,1,%lu,roster", (unsigned long)agora, (unsigned long)redeUtc(),
                  (unsigned)slot, (unsigned)seq, n, rssi, snr, ferr, (unsigned long)perdidos);
    radioHwRx();
    return;
  }

  ProtoPos k;
  if (!protoUnpackPos(buf, n, k)) { radioHwRx(); return; }

  // QUALQUER pacote de posicao serve de beacon: quem vira a referencia e decidido
  // pelo tdmaBeaconAceita (menor slot ouvido, nunca maior que o proprio, PPS
  // ganha de tudo). O pacote tem tamanho variavel (16 a 68 B) e o rxDone vem no
  // FIM dele; o tdma_core desconta um air-time FIXO (o do maior pacote), entao o
  // carimbo e adiantado pela diferenca para a borda calculada ser a do INICIO.
  bool srcPps = (k.flags & FL_PPS) != 0;
  if (tdmaBeaconAceita(r.tdma, k.slot, srcPps)) {
    uint8_t antes = r.tdma.anchorSrc; bool eraAncora = r.tdma.isAnchor;
    uint64_t rxAjust = rxUs + (REDE_AIRTIME_US - airtimeUs((uint32_t)n));
    tdmaOnBeaconFrom(r.tdma, rxAjust, k.slot, srcPps);
    if (antes != k.slot || eraAncora) {
      Serial.printf("rede: fonte de tempo agora e o slot %u%s%s\n", k.slot,
                    srcPps ? " (grade UTC, tem PPS)" : "", eraAncora ? " - eu era a ancora e cedi" : "");
      registroLinha("!,%lu,fonte de tempo,slot %u,%s", (unsigned long)agora, (unsigned)k.slot, srcPps ? "UTC" : "grupo");
    }
  }

  bool alertaMudou;
  int idx = frotaRecebePos(g_frota, k, agora, alertaMudou);
  if (idx == -2) {
    static uint32_t tAviso = 0;
    if (agora - tAviso > 5000) {
      tAviso = agora;
      Serial.printf("*** OUTRO aparelho esta no MEU slot (%u)! Troque um deles na configuracao (conflitos: %lu)\n",
                    (unsigned)k.slot, (unsigned long)g_frota.conflitos);
    }
  } else if (idx > 0) {
    if (alertaMudou) Serial.printf("frota: %s %s ALERTA\n", g_frota.c[idx].nome, g_frota.c[idx].alerta ? "PEDIU" : "cancelou o");
  }
  int novos = 0;
  if (idx > 0 && (k.flags & FL_TRILHA)) {
    uint8_t epoca; uint16_t base, ackAte, ackDe;
    const bool lider = (k.flags & FL_LIDER) != 0;
    uint32_t antes = g_rotaC.inseridos;
    int np = trilhaDecodifica(buf, n, epoca, base, ackAte, ackDe,
      [&](uint16_t seq16, double lat, double lon) {
        // So o LIDER escreve na rota: o rastro dos outros nao e caminho a seguir.
        if (lider) rotaInsere(g_rotaC, rotaSeqVirtual(g_rotaC, epoca, seq16), lat, lon);
      });
    if (np >= 0) {
      // do lider, o 'ackDe' e o ponto mais antigo que ele guarda: vira o piso.
      // E so ele: um ack de lider na frota confundiria quem virar lider depois.
      if (lider) rotaDefinePiso(g_rotaC, epoca, ackDe);
      else       frotaAck(g_frota, idx, ackAte, ackDe, base, agora);   // base = o topo dele
    }
    novos = (int)(g_rotaC.inseridos - antes);
  }
  registroLinha("R,%lu,%lu,%u,%u,%d,%.0f,%.1f,%.0f,1,%lu,pos,%d,%u,%d", (unsigned long)agora, (unsigned long)redeUtc(),
                (unsigned)slot, (unsigned)seq, n, rssi, snr, ferr, (unsigned long)perdidos,
                (int)k.flags, (unsigned)((k.flags & FL_TRILHA) ? buf[23] : 0), novos);
  radioHwRx();
}

// ------------------------------------------------------------------ ruido
// Uma amostra por quadro num slot que NUNCA tem carro (RUIDO_SLOT_INI..), lida
// no meio do slot, longe das guardas, so com o radio em RX e sem TX em voo. Se
// a entrega e de 50 % a -97 dBm num radio que ouve ate -129, ou o piso de ruido
// subiu (step-down, tela, lataria) ou o receptor esta surdo - e este e o numero
// que separa as duas hipoteses.
inline void redeMedeRuido()
{
  Rede& r = g_rede;
  if (r.txEmVoo || !r.tdma.haveAnchor) return;
  uint32_t usInFrame, frameIdx;
  if (!tdmaUsInFrame(r.tdma, usInFrame, frameIdx)) return;
  if (frameIdx == r.ruidoFrame) return;
  uint8_t s = r.ruidoSlotProx;
  uint32_t ini = tdmaSlotStartUs(r.tdma, s);
  uint32_t g = tdmaGuardNow(r.tdma);
  if (usInFrame < ini + g * 2 || usInFrame > ini + r.tdma.slotUs - g * 2) return;
  r.ruidoFrame = frameIdx;
  r.ruidoSlotProx = (uint8_t)(s + 1 < TDMA_SLOTS ? s + 1 : RUIDO_SLOT_INI);
  float v = radioHwRssiInst();
  r.ruidoUlt = v; r.ruidoN++; r.ruidoSoma += v;
  if (r.ruidoN == 1 || v < r.ruidoMin) r.ruidoMin = v;
  if (v > r.ruidoMax) r.ruidoMax = v;
  registroLinha("N,%lu,%lu,%.0f,%d", (unsigned long)millis(), (unsigned long)redeUtc(), v, (int)s);
}

// ------------------------------------------------------------------ ciclo
inline void redeRelogioVoltou(uint64_t salto)
{
  Rede& r = g_rede;
  r.relogioVoltou++;
  Serial.printf("rede: *** RELOGIO VOLTOU %llu us - deslocando ancoras ***\n", (unsigned long long)salto);
  registroLinha("!,%lu,relogio voltou,%llu us", (unsigned long)millis(), (unsigned long long)salto);
  r.tdma.anchorUs  = (r.tdma.anchorUs  > salto) ? r.tdma.anchorUs  - salto : 0;
  r.tdma.lastPpsUs = (r.tdma.lastPpsUs > salto) ? r.tdma.lastPpsUs - salto : 0;
  r.tdma.bornUs    = (r.tdma.bornUs    > salto) ? r.tdma.bornUs    - salto : 0;
  r.txDesdeMs = millis();
  r.txIniUs = relogioUs();
}

inline void redeAtualiza()
{
  Rede& r = g_rede;
  if (!r.ok) {
    // O RADIO NAO SUBIU NO BOOT: tenta de novo, para sempre, sem travar nada.
    // Medido nas duas telas em 2026-09-29: no boot a frio o chip nao respondia, e
    // o firmware desistia de vez - o begin() antigo insistia ~10 s (e as vezes
    // pegava o E22 acordando no meio); o teste rapido desiste em 50 ms. O diario
    // diz quanto tempo depois do boot ele apareceu: e o numero que separa "o E22
    // demora a acordar" de "o E22 nao tem 5 V".
    static uint32_t tUlt = 0;
    const uint32_t agora = millis();
    if (agora - tUlt >= RADIO_RETENTA_MS) {
      tUlt = agora;
      if (radioHwTentaDeNovo(r.pwrConfig)) {
        r.ok = true;
        Serial.printf("rede: radio SUBIU %lu ms depois do boot (no boot ele nao respondia)\n", (unsigned long)agora);
        registroLinha("!,%lu,radio subiu tarde,%lu ms", (unsigned long)agora, (unsigned long)agora);
      }
    }
    return;
  }
  const uint32_t agora = millis();

  // 1) ancorar o tempo: o PPS so vale pareado com o segundo do NMEA (e recente).
#if MTS_TEM_GPS
  { uint64_t ppsUs; uint32_t sec; if (gpsPegaPar(ppsUs, sec)) tdmaOnPps(r.tdma, ppsUs, sec); }
#endif

  // 2) o vigia de TX preso. ANTES DE ACUSAR, PERGUNTE AO CHIP: se o laco ficou
  //    parado mais que o vigia, um TX que COMPLETOU chega aqui parecendo travado.
  //    E se o chip diz que terminou mas a ISR nao correu, assume-se o evento.
  bool venceu = r.txEmVoo && (agora - r.txDesdeMs > RADIO_VIGIA_MS) && !g_loraIrq;
  if (venceu && radioHwTxDoneNoChip()) { radioHwIrqForca(relogioUs()); venceu = false; }
  if (venceu) {
    r.txEmVoo = false;
    r.txPreso++;
    uint16_t err = radioHwErrosChip();
    char en[128];
    Serial.printf("rede: TX preso (%lu) - erros do chip = %s\n", (unsigned long)r.txPreso, radioHwErroNome(err, en, sizeof(en)));
    registroLinha("!,%lu,tx preso,%lu,%c,%u B,err 0x%04X,pwr %d,%lu ms", (unsigned long)agora,
                  (unsigned long)r.txPreso, r.txTipo ? r.txTipo : '?', (unsigned)r.txLen, err,
                  (int)g_rhw.pwr, (unsigned long)(agora - r.txDesdeMs));
    if (radioHwRebegin(g_rhw.pwr))
      Serial.println("rede: radio RECONFIGURADO do zero. Se isto se repete, o 5V do modulo esta afundando no TX.");
    else
      Serial.println("rede: TX preso e begin() falhou - tentando de novo no proximo ciclo");
    // NAO HA RECUO DE POTENCIA AQUI (params.h / README): quem decide e quem le o numero.
  }

  // 2b) potencia pedida que nao pode ser aplicada na hora (TX em voo): aplica
  //     agora. E o valor do USUARIO, nunca um recuo.
  if (!r.txEmVoo && g_rhw.pwr != r.pwrConfig) redeAplicaPotencia(r.pwrConfig);

  // 3) o que o radio tem a dizer
  uint64_t irqUs;
  if (radioHwIrqPega(irqUs)) {
    uint32_t flags = radioHwIrqFlags();
    if (flags & RADIOLIB_SX126X_IRQ_TX_DONE) {
      r.txEmVoo = false;
      radioHwFimTx();
      // LINHA T: o air-time MEDIDO (startTransmit -> TX_DONE), o numero de que o
      // slot inteiro depende. Se vier acima de airtimeUs(len), AIRTIME_EXTRA_US esta curto.
      uint32_t air = (irqUs > r.txIniUs) ? (uint32_t)(irqUs - r.txIniUs) : 0;
      r.txAirUltUs = air;
      if (air > r.txAirMaxUs) r.txAirMaxUs = air;
      if (air && air < r.txAirMinUs) r.txAirMinUs = air;
      registroLinha("T,%lu,%lu,%u,%c,%u,%lu,%lu,%d,%u", (unsigned long)agora, (unsigned long)redeUtc(),
                    (unsigned)(uint8_t)(r.seq - 1), r.txTipo, (unsigned)r.txLen, (unsigned long)air,
                    (unsigned long)r.txUsInFrame, r.txCatchup ? 1 : 0, (unsigned)r.txPontos);
      r.txTipo = 0;
    } else if (flags & RADIOLIB_SX126X_IRQ_RX_DONE) {
      redeRecebe(irqUs);
    }
    // Borda sem flag: residuo do proprio startReceive() ao rearmar. NAO rearmar
    // aqui - rearmar gera a piscada seguinte (900 eventos medidos com o radio parado).
  }

  // 4) e minha vez de falar?
  tdmaTick(r.tdma);
  // A eleicao vem DEPOIS do tick (e o tick quem atualiza o holdover em que ela se
  // apoia). Sem ela um no que nao ouve ninguem nunca ancora e nunca transmite.
  if (tdmaEleicao(r.tdma)) {
    Serial.printf("rede: ASSUMI a ancora da rede (slot %u)\n", r.tdma.nodeId);
    registroLinha("!,%lu,assumi a ancora", (unsigned long)agora);
  }
  if (!r.txEmVoo && tdmaShouldTx(r.tdma)) {
    // Primeira fala = POSICAO (ancora quem me ouve). Rosters INTERCALADOS nas
    // falas 1, 3 e 5 (o nome aparece em ate 12 s, nunca 2 quadros sem beacon),
    // depois a cada ROSTER_CADA.
    const uint32_t f = r.falas++;
    bool souRoster = (f == 1 || f == 3 || f == 5) || (f > 5 && (f % ROSTER_CADA) == 0);
    if (souRoster) redeTransmiteRoster();
    else           redeTransmitePosicao();
  }

  // 5) ruido de fundo, num slot que nunca tem carro
  if (!r.txEmVoo) redeMedeRuido();
}

// "Da para me deixar parado por 'us' agora?" - C4 da auditoria: nao basta
// perguntar se ha TX em voo (91 % das vezes nao ha); e preciso saber quanto
// falta para a janela.
inline bool redePodeGastar(uint32_t us)
{
  if (!g_rede.ok) return true;
  if (g_rede.txEmVoo) return false;
  if (!g_rede.tdma.haveAnchor) return true;
  // DENTRO da minha janela e ainda sem ter falado neste quadro: nao. O
  // tdmaUsUntilMySlot so conta ate o INICIO da janela - aberta ela, devolve "~6 s,
  // o proximo quadro" e o mapa ganhava 250 ms que comiam os 63 ms de janela.
  {
    const Tdma& t = g_rede.tdma;
    uint32_t usInFrame, frameIdx;
    if (tdmaUsInFrame(t, usInFrame, frameIdx) && !(t.haveTxFrame && t.txFrameDone == frameIdx)) {
      const uint32_t g = tdmaGuardNow(t), ini = tdmaSlotStartUs(t, t.nodeId);
      const uint32_t lo = ini + g / 2, hi = ini + t.slotUs - g / 2 - t.airtimeUs;
      if (usInFrame >= lo && usInFrame <= hi) return false;
    }
  }
  return tdmaUsUntilMySlot(g_rede.tdma) > us + ORC_RESERVA_US;
}

#else   // !MTS_TEM_RADIO ---------------------------------------------------
inline bool redeInicia(uint8_t, const char*, int8_t pwrConfig, bool seguro) { memset(&g_rede, 0, sizeof(g_rede)); g_rede.pwrConfig = pwrConfig; g_rede.seguro = seguro; vizinhosInicia(g_viz, protoSalaHash("SEMSALA")); return false; }
inline void redeTrocaSala(const char*) {}
inline void redeTrocaSlot(uint8_t novo) { if (novo < FROTA_MAX_CARROS) g_frota.c[0].slot = novo; }
inline void redeTrocaPotencia(int8_t nova) { g_rede.pwrConfig = nova; }
inline void redeAtualiza() {}
inline bool redePodeGastar(uint32_t) { return true; }
inline void redeRelogioVoltou(uint64_t) { g_rede.relogioVoltou++; }
#endif
