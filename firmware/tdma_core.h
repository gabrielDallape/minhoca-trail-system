/*
 * ============================================================================
 *  TDMA CORE - fatiamento do tempo em frames/slots para a rede "siga o lider".
 *
 *  Problema que resolve: hoje o seguidor so fala QUANDO OUVE o lider
 *  (hub-and-spoke). Com 3+ carros isso colide. Aqui o tempo e dividido em
 *  slots e cada no fala SO no seu -> zero colisao, sem lider reencaminhando.
 *
 *  A FONTE DE TEMPO fica atras de uma interface, de proposito:
 *    TDMA_SYNC_BEACON  fase 2 - ancora no beacon de outro no (sem GPS). Quem
 *                      serve de beacon e ELEITO pelo relogio, nao fixado no
 *                      slot 0 - ver tdmaEleicao(), e o porque dela.
 *    TDMA_SYNC_PPS     fase 3 - ancora no pulso 1Hz do GPS (relogio comum real)
 *  Trocar de uma para a outra NAO muda o resto do codigo: e so chamar
 *  tdmaOnBeacon() ou tdmaOnPps(). Toda a decisao de "e o meu slot?" e a mesma.
 *
 *  O frame e medido em SEGUNDOS INTEIROS. Isso nao e estetica: o PPS marca a
 *  borda do segundo UTC, entao um frame de N segundos cai sempre no mesmo lugar
 *  do relogio de todos os nos. Frame fracionario nao ancora em PPS.
 *
 *  Independente de radio e de hardware (nao inclui RadioLib nem trilha_core.h)
 *  -> da para testar a matematica sem placa nenhuma.
 * ============================================================================
 */
#pragma once
#include <Arduino.h>

// -------------------------------------------------- relogio monotonico (us)
// millis()/micros() estouram (micros em ~71min). esp_timer_get_time() e int64.
//
// TDMA_TEST_CLOCK: ponto de costura p/ teste. Definindo esta macro antes do
// include, o tempo passa a vir de uma variavel que o teste controla -> da para
// verificar as janelas de slot de forma determinista, sem esperar em tempo real
// e sem radio nenhum (ver firmware/tdma_selftest).
#ifdef TDMA_TEST_CLOCK
  extern uint64_t tdmaTestNowUs;
  inline uint64_t tdmaNowUs(){ return tdmaTestNowUs; }
#elif defined(ESP_PLATFORM) || defined(ESP32)
  #include "esp_timer.h"
  inline uint64_t tdmaNowUs(){ return (uint64_t)esp_timer_get_time(); }
#else
  inline uint64_t tdmaNowUs(){ return (uint64_t)micros(); }
#endif

enum TdmaSync : uint8_t { TDMA_SYNC_NONE = 0, TDMA_SYNC_BEACON = 1, TDMA_SYNC_PPS = 2 };

// Sem ancora ha mais de 1,5s consideramos HOLDOVER: o relogio interno continua
// contando, mas alargamos a guarda porque a confianca no alinhamento caiu.
//
// CUIDADO: o drift NAO e desprezivel. Uma versao anterior deste comentario dizia
// "~2ms/dia", numero que estava errado por ~3 ordens de grandeza (2ms/dia sao
// 0,023 ppm, classe OCXO de laboratorio). A Espressif NAO especifica o ppm do
// cristal - e escolha do fabricante da placa, e a especificacao do ESP32 pede
// +-10 ppm.
//
//   guarda_disponivel_us / (ppm * 1e-6)  =  110000 / 1e-5  =  1,1e10 us
//
// CORRECAO 2026-09-08: este comentario dizia que 1,1e10 us eram "3,05 min". Sao
// 11.000 s = **3,05 HORAS** - um erro de 60x que inverteu a conclusao. Com a guarda
// real de hoje (TDMA_GUARD_US 20000) o holdover seguro e 20000/1e-5 = 2000 s, ou
// seja **~33 minutos**, nao segundos.
//
// Isso bate com o RFC 7554 (TSCH), que com 10 ppm e guarda de 1 ms ressincroniza a
// cada 100 s - e a nossa guarda de 20 ms e 20x a dele.
//
// Portanto TDMA_HOLDOVER_GUARD cobre DEZENAS DE MINUTOS. O holdover nao e o gargalo
// da rede; o air-time e. Ainda assim, meca o ppm de cada placa contando
// esp_timer_get_time() entre N bordas de PPS e grave na NVS - com o numero medido da
// para usar guarda adaptativa (guarda = base + ppm * tempo_desde_ancora), que e o que
// o TSCH faz:
//   ppm = (dt_medido - N*1e6) / (N*1e6) * 1e6      (N=300 resolve <1 ppm)
static const uint32_t TDMA_ANCHOR_TTL_US  = 1500000UL;
static const uint32_t TDMA_HOLDOVER_GUARD = 3;          // multiplicador da guarda

// ------------------------------------------------------- eleicao da ancora
// POR QUE ISTO EXISTE (medido em campo e na bancada, 2026-08-28): so o no 0 se
// auto-ancorava. Com tres aparelhos nos slots 0, 1 e 2, bastava o do slot 0
// estar desligado, longe ou com o GPS ruim para que os outros dois NUNCA
// transmitissem - tdmaShouldTx() exige ancora e nao havia quem a criasse. Lido
// no serial das duas telas: sinc=- e tx=0, mapa vazio, nenhuma mensagem de erro.
// A rede nao ficava fraca: ficava MUDA, que e muito pior de diagnosticar.
//
// E o PPS nao socorre o arranque: no u-blox o pulso so comeca DEPOIS do fix, que
// e justamente o que ainda nao existe quando o carro liga embaixo de arvore.
//
// A regra: quem passa TDMA_ELECT_BASE_US + slot*frame sem ouvir ninguem ancora
// em si mesmo. A espera CRESCE com o slot, entao o menor slot presente vence e
// os outros o escutam antes de o proprio relogio estourar. Nao ha mensagem de
// eleicao nem estado compartilhado - so tempo de espera desigual, que e o que
// torna isto seguro numa rede onde metade dos nos nao se ouve.
static const uint32_t TDMA_ELECT_BASE_US = 3000000UL;

// Quantos pulsos SEGUIDOS de 1 Hz antes de acreditar no PPS. Um PPS que pisca e
// PIOR que nenhum: enquanto ele existe o no vai para a grade UTC e anuncia isso,
// a rede inteira o segue, e quando ele some todos voltam - a cada poucos
// segundos. Medido na bancada em 2026-08-28, com o GPS dentro de casa: as duas
// telas alternando "cedi o lugar" / "ASSUMI a ancora" sem parar, com 'perdi'
// (janela do proprio slot perdida) subindo junto. Dez segundos de pulso limpo
// custam dez segundos no arranque e removem a oscilacao inteira.
static const uint16_t TDMA_PPS_FIRME = 10;

// ---------------------------------------------------- grade de tempo (GPS x UTC)
// O rotulo do segundo que entra em tdmaOnPps() pode vir de DUAS grades:
//   - UTC  : o que o NMEA reporta (e o que o ATGM336H alinha por hardware)
//   - GPS  : o que UBX-TIM-TP reporta, e o DEFAULT de fabrica do u-blox no CFG-TP5
// As duas diferem por um numero INTEIRO de segundos (18 em 2026), entao a BORDA
// do pulso e a mesma nas duas - o que muda e so o rotulo.
//
// Isso importa porque tdmaUsInFrame() faz (anchorSec % frameSecs). Se um no
// rotular em UTC e outro em GPS, os dois calculam frames deslocados de
// (18 % frameSecs) segundos -> colisao total e SILENCIOSA, so em rede mista.
//
//   frameSecs 1,2,3,6,9,18 -> 18%f == 0 -> imune
//   frameSecs 4            -> desloca 2s
//   frameSecs 5            -> desloca 3s   <- e 4 e 5 sao justamente os frames
//                                             que 25-50 nos exigem em SF7.
//
// REGRA DO PROJETO: anchorSec e SEMPRE UTC. Em no u-blox, mande CFG-TP5 com
// gridUtcGps=0. O flag gridSensitive abaixo existe para o sketch avisar quando a
// configuracao escolhida depende dessa regra ser respeitada.
static const uint32_t TDMA_GRID_OFFSET_SEC = 18;   // GPS - UTC, IERS Bulletin C 72

inline bool tdmaFrameSecsGridSafe(uint32_t frameSecs){
  return frameSecs && (TDMA_GRID_OFFSET_SEC % frameSecs) == 0;
}

struct Tdma {
  uint8_t  nodeId;      // 0..nSlots-1, unico por carro. Menor slot = prioridade
                        // na eleicao da ancora, nao ancora garantida.
  uint8_t  nSlots;
  uint32_t frameSecs;   // frame em segundos INTEIROS (casa com o PPS)
  uint32_t frameUs;
  uint32_t slotUs;      // frameUs / nSlots
  uint32_t guardUs;     // absorve jitter de ISR/SPI/driver

  TdmaSync sync;
  uint64_t anchorUs;    // instante local da ultima ancora (beacon ou borda PPS)
  uint32_t anchorSec;   // (PPS) segundo UTC daquela borda
  bool     haveAnchor;
  bool     holdover;

  // Contador MONOTONICO de frames, usado no modo beacon.
  // Sem ele haveria bug: o beacon re-ancora a cada frame, entao um frameIdx
  // derivado so de (agora - ancora)/frameUs voltaria a ZERO em cada beacon; como
  // txFrameDone tambem comeca em 0, a guarda de "1 TX por frame" concluiria que
  // o no ja falou e ele PARARIA de transmitir para sempre depois do 1o frame.
  // No modo PPS nao e necessario (o indice vem do UTC, que ja e absoluto).
  uint32_t frameBase;

  // Air-time MEDIDO do pacote (e22_ping / getTimeOnAir). O rxDone do radio vem
  // no FIM do pacote, entao sem descontar isto a ancora do beacon fica atrasada
  // de um air-time inteiro - que e da ordem da folga do slot. Zero = nao informado
  // (a ancora nao compensa nada, comportamento antigo).
  uint32_t airtimeUs;

  // true no no que ANCORA a rede (eleito, ver tdmaEleicao). Quem ancora nao tem
  // quem o sincronize, entao nao pode ser marcado em holdover por falta de ancora.
  bool isAnchor;

  // A minha fonte esta em PPS? Uma fonte com PPS esta na grade UTC absoluta e
  // vale mais que qualquer beacon, INCLUSIVE de slot menor: sem isto a rede se
  // parte em duas grades no instante em que UM carro pega fix - observado ao
  // vivo na bancada em 2026-08-28, com as duas telas em "fonte=EU".
  bool anchorSrcPps;

  // De QUEM veio a minha ancora. 0xFF = de ninguem ainda. Existe porque a fonte
  // de tempo deixou de ser sempre o slot 0: para decidir se um beacon novo e
  // melhor que o atual e preciso saber qual e o atual. Quando eu mesmo sou a
  // ancora, anchorSrc == nodeId.
  uint8_t anchorSrc;

  // Continuidade do PPS: quantos pulsos seguidos com ~1 s entre eles, e quando
  // foi o ultimo. Contados a parte do anchorUs de proposito - assim a contagem
  // sobrevive ao no estar ancorado em beacon enquanto o pulso ainda se prova.
  uint16_t ppsRun;
  uint64_t lastPpsUs;

  // Instante em que este no entrou no ar. So a eleicao usa: e o marco a partir
  // do qual se conta a espera antes de ancorar em si mesmo.
  uint64_t bornUs;

  // true quando (18 % frameSecs) != 0, ou seja: com este frame, misturar nos que
  // rotulam anchorSec em UTC com nos que rotulam em GPS desalinha a rede inteira
  // sem dar erro. Nao impede nada - a config e valida SE todos usarem UTC. Serve
  // para o sketch avisar no boot. Ver o bloco da grade de tempo la em cima.
  bool gridSensitive;

  uint32_t txFrameDone; // ultimo frame em que ja transmiti -> 1 TX por frame
  bool     haveTxFrame;

  // diagnostico
  uint32_t anchorCount, txCount, missedTx;
};

// airtimeUs deve ser o valor MEDIDO na fase 1 (e22_ping), nao a estimativa.
inline void tdmaInit(Tdma& t, uint8_t nodeId, uint8_t nSlots, uint32_t frameSecs, uint32_t guardUs){
  t.nodeId = nodeId;
  t.nSlots = nSlots ? nSlots : 1;
  t.frameSecs = frameSecs ? frameSecs : 1;
  t.frameUs = t.frameSecs * 1000000UL;
  t.slotUs = t.frameUs / t.nSlots;
  t.guardUs = guardUs;
  t.sync = TDMA_SYNC_NONE;
  t.anchorUs = 0; t.anchorSec = 0;
  t.haveAnchor = false; t.holdover = false;
  t.frameBase = 0;
  t.airtimeUs = 0;
  // NAO nasce mais ancora por ser o slot 0: quem ancora e decidido pelo relogio,
  // em tdmaEleicao(). Nascer ancora aqui era o proprio bug - um no 0 ausente
  // deixava a rede sem referencia e ninguem assumia o lugar dele.
  t.isAnchor = false;
  t.anchorSrc = 0xFF;
  t.anchorSrcPps = false;
  t.ppsRun = 0; t.lastPpsUs = 0;
  t.bornUs = tdmaNowUs();
  t.gridSensitive = !tdmaFrameSecsGridSafe(t.frameSecs);
  t.txFrameDone = 0; t.haveTxFrame = false;
  t.anchorCount = 0; t.txCount = 0; t.missedTx = 0;
}

// Informa o air-time MEDIDO (nao o estimado) para a ancora do beacon poder
// descontar o tempo que o pacote levou no ar. Chamar no setup, depois de
// getTimeOnAir()/e22_ping.
inline void tdmaSetAirtime(Tdma& t, uint32_t airtimeUs){ t.airtimeUs = airtimeUs; }

// Cabe? slot tem de segurar o pacote no ar + a guarda, com folga.
// Retorna a folga em us (negativa = configuracao impossivel).
inline int32_t tdmaHeadroomUs(const Tdma& t, uint32_t airtimeUs){
  return (int32_t)t.slotUs - (int32_t)airtimeUs - (int32_t)t.guardUs;
}
inline bool tdmaFits(const Tdma& t, uint32_t airtimeUs){ return tdmaHeadroomUs(t, airtimeUs) > 0; }

// Maximo de nos que caberia num frame, dado o air-time medido.
inline uint32_t tdmaMaxNodes(const Tdma& t, uint32_t airtimeUs){
  uint32_t per = airtimeUs + t.guardUs;
  return per ? (t.frameUs / per) : 0;
}

// ---------------------------------------------------------------- ancoragem
// QUEM EU ACEITO COMO FONTE DE TEMPO. A regra e uma so, e vale para todos:
// a minha fonte e o MENOR slot que eu ouco, e nunca um slot maior que o meu.
//
//   - PPS ganha de tudo: e relogio absoluto, nao referencia emprestada.
//   - Sem ancora, qualquer fonte serve. Alinhado de forma imperfeita e
//     incomparavelmente melhor que mudo - foi o mudo que perdeu o dia de campo.
//   - Em holdover a minha fonte sumiu; aceito qualquer uma para voltar a rede.
//   - Slot MAIOR que o meu eu recuso: e ele quem deve me seguir. Sem esta linha
//     dois nos ancoram um no outro em circulo, nenhum deles e ancora, e os dois
//     entram em holdover eterno com a guarda triplicada.
inline bool tdmaBeaconAceita(const Tdma& t, uint8_t srcSlot, bool srcPps){
  if (srcSlot == t.nodeId) return false;                        // eco meu: impossivel, mas nao custa
  if (t.sync == TDMA_SYNC_PPS && !t.holdover) return false;     // PPS manda
  if (!t.haveAnchor) return true;
  if (t.holdover)    return true;
  // PPS ANTES DE SLOT. Uma fonte em PPS esta na grade UTC absoluta; a de beacon
  // esta numa grade arbitraria, herdada de quem ligou primeiro. Ela ganha mesmo
  // com slot MAIOR que o meu - foi exatamente isto que faltou quando uma tela
  // pegou fix e as duas viraram ancoras de grades diferentes, sem colidir por
  // sorte e sem nada no log dizendo que a rede tinha se partido em duas.
  if (srcPps) {
    // Entre varias fontes em PPS fico com o menor slot. Todas estao na MESMA
    // grade, entao trocar nao mudaria a fase - mas cada troca de fonte solta a
    // guarda de "ja falei neste frame", e ficar alternando entre duas custaria
    // um TX a mais por quadro. A troca tem de ter motivo.
    if (!t.anchorSrcPps) return true;
    return srcSlot <= t.anchorSrc;
  }
  // Nao rebaixo de UTC para grade arbitraria enquanto a minha fonte UTC viver.
  if (t.anchorSrcPps && !t.holdover) return false;
  if (srcSlot == t.anchorSrc) return true;                      // refresco da minha fonte
  // SO DE SLOT MENOR, e mesmo em holdover. Aceitar qualquer um quando a fonte
  // some parece generoso e cria o circulo: dois nos ficam ancorando um no outro,
  // nenhum e ancora, e a fase dos dois deriva junta. Quem fica sem fonte deve
  // ASSUMIR (tdmaEleicao), nao pendurar-se em quem tem slot maior.
  return srcSlot < t.nodeId && srcSlot < t.anchorSrc;
}

// FASE 2: chamado quando chega o pacote de um no que serve de referencia.
// rxUs = instante local do RX; srcSlot = o slot de quem falou.
inline void tdmaOnBeaconFrom(Tdma& t, uint64_t rxUs, uint8_t srcSlot, bool srcPps){
  // O rxDone do radio vem no FIM do pacote: sem recuar o air-time a ancora fica
  // atrasada de 51,5ms (16B em SF7/BW125), que e quase a folga inteira de um slot
  // de 125ms - e com 8 nos isso virava colisao. Recua SO o air-time: airtimeUs=0
  // (o default) deixa esta correcao inerte, entao quem nao chama tdmaSetAirtime
  // tem exatamente o comportamento antigo.
  //
  // Fica de fora o instante do TX dentro do slot de quem falou (>= guarda/2), que
  // o receptor nao tem como saber. Sao ~7,5ms contra os 59ms de folga - ordem de
  // grandeza menor que o erro que se acabou de remover.
  uint64_t edge = rxUs;
  if (t.airtimeUs && edge > t.airtimeUs) edge -= t.airtimeUs;

  // E recua TAMBEM o offset do slot de quem falou. Enquanto so o slot 0 ancorava
  // este termo era zero e nao existia; com a eleicao, a fonte pode ser o slot 3,
  // e sem isto a "borda do frame" ficaria adiantada de 3 slots. Todo mundo
  // andaria para tras a cada re-ancora, sem erro nenhum aparecendo no log.
  uint64_t off = (uint64_t)srcSlot * t.slotUs;
  edge = (edge > off) ? (edge - off) : 0;

  bool trocouFonte = t.haveAnchor && (t.anchorSrc != srcSlot || t.anchorSrcPps != srcPps);

  if (t.haveAnchor && t.sync == TDMA_SYNC_BEACON && !trocouFonte && edge >= t.anchorUs) {
    // Quantos frames se passaram desde a ancora anterior.
    //
    // ARREDONDA, com minimo 1. Truncar (a versao anterior) tinha um bug fatal:
    // num no com cristal mais LENTO que o da ancora o intervalo entre beacons
    // medido no relogio local fica abaixo de frameUs (1s a -40ppm = 999,96ms), a
    // divisao inteira dava 0, o frameBase congelava e a guarda de "1 TX por
    // frame" travava o no PARA SEMPRE - exatamente o que este campo existe para
    // evitar. Como dependia do SINAL do erro relativo dos cristais, com 2 nos
    // passava e com 3+ metade da rede emudecia. Medido em bancada (60s):
    //   0/-30/-20/-10 ppm -> tx = 60/2/2/2      (truncando)
    //   0/-30/-20/-10 ppm -> tx = 60/60/60/60   (arredondando)
    uint64_t d = edge - t.anchorUs;
    uint32_t n = (uint32_t)((d + t.frameUs / 2) / t.frameUs);
    t.frameBase += n ? n : 1;
  } else if (trocouFonte) {
    // FONTE NOVA. A borda pode saltar PARA TRAS (o srcSlot novo e menor que o
    // antigo), e ai a contagem por diferenca acima nao vale - ela pressupoe
    // tempo andando para a frente. Avanca um frame na marra e SOLTA a guarda de
    // "ja falei neste frame": sem soltar, o no pode ficar um frame inteiro sem
    // poder transmitir logo no instante em que acabou de reentrar na rede.
    t.frameBase++;
    t.haveTxFrame = false;
  }
  t.sync = TDMA_SYNC_BEACON;
  t.anchorUs = edge; t.anchorSec = 0;
  t.anchorSrc = srcSlot;
  t.anchorSrcPps = srcPps;
  t.isAnchor = false;              // tenho quem me sincronize: nao sou mais a ancora
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}

// O beacon classico da fase 2 vinha sempre do slot 0. Mantido para os testes e
// para qualquer chamador que ainda pense assim.
inline void tdmaOnBeacon(Tdma& t, uint64_t rxUs){ tdmaOnBeaconFrom(t, rxUs, 0, false); }

// Ancora em SI MESMO, a frio: nunca ouviu ninguem, entao a fase do frame passa a
// ser definida por este no. Usado so pela eleicao.
inline void tdmaAutoAncora(Tdma& t){
  t.sync = TDMA_SYNC_BEACON;
  t.anchorUs = tdmaNowUs();
  t.anchorSec = 0;
  t.anchorSrc = t.nodeId;
  t.anchorSrcPps = false;          // ancorei em mim sem PPS: grade arbitraria
  t.isAnchor = true;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}

// Assume a referencia SEM MEXER NA FASE. Diferente do tdmaAutoAncora: este no ja
// estava na rede e herdou dela um alinhamento bom; a fonte e que sumiu. Trocar
// anchorUs por "agora" aqui jogaria fora exatamente a informacao que se quer
// preservar, e deslocaria este no em relacao aos que continuam alinhados.
inline void tdmaPromoveAncora(Tdma& t){
  t.isAnchor = true;
  t.anchorSrc = t.nodeId;
  // A FASE herdada continua valendo - e por isso que esta funcao existe. Mas a
  // ALEGACAO de estar na grade UTC cai aqui, e isso nao e detalhe: sem PPS
  // proprio eu passo a correr livre, sem ninguem me corrigir, e nem consigo
  // anunciar UTC no pacote (o flag sai de t.sync, que aqui e BEACON).
  //
  // Mantendo a alegacao, o no ficava TRANCADO: continuava dizendo a si mesmo
  // "eu estou em UTC" e, por isso, recusava a fonte de PPS de verdade quando ela
  // tinha slot maior que o dele. Visto ao vivo na bancada em 2026-08-28 - a tela
  // do slot 1 nunca cedia para a do slot 2, que era a unica com fix.
  t.anchorSrcPps = false;
  t.holdover = false;
}

// A ELEICAO. Chamar no loop, DEPOIS do tdmaTick (que e quem atualiza o holdover
// em que o caso 2 se apoia). Devolve true no instante em que este no assume.
inline bool tdmaEleicao(Tdma& t){
  // 1) nunca ouvi ninguem. Espero o meu tempo - que cresce com o meu slot, para
  //    o menor slot presente assumir primeiro - e ancoro em mim.
  if (!t.haveAnchor) {
    uint64_t espera = (uint64_t)TDMA_ELECT_BASE_US + (uint64_t)t.nodeId * t.frameUs;
    uint64_t agora  = tdmaNowUs();
    if (agora >= t.bornUs && (agora - t.bornUs) >= espera) { tdmaAutoAncora(t); return true; }
    return false;
  }
  // 2) eu tinha fonte e ela calou (holdover no modo beacon). Assumo a referencia
  //    mantendo a fase herdada. Sem este caso, o no do menor slot que perdeu a
  //    ancora ficaria em holdover para sempre - e como ele RECUSA beacons de
  //    slot maior, ninguem o traria de volta.
  if (t.sync == TDMA_SYNC_BEACON && t.holdover && !t.isAnchor) {
    tdmaPromoveAncora(t);
    return true;
  }
  // 3) estou seguindo alguem de slot MAIOR que o meu, numa grade arbitraria.
  //    No arranque isso e certo (seguir e melhor que ficar mudo), mas nao pode
  //    durar: com dois nos assim cada um vira fonte do outro, ninguem e ancora
  //    de verdade e a fase dos dois deriva junta. Quem tem o slot menor assume,
  //    MANTENDO a fase - que a esta altura ja e a fase da rede, nao uma nova.
  //    Nao vale para fonte em PPS: a grade UTC e boa venha do slot que vier.
  if (!t.isAnchor && !t.anchorSrcPps && t.sync == TDMA_SYNC_BEACON &&
      t.anchorSrc != 0xFF && t.anchorSrc > t.nodeId) {
    tdmaPromoveAncora(t);
    return true;
  }
  return false;
}


// FASE 3: chamado da flag levantada pela ISR do PPS.
//   ppsUs  = esp_timer_get_time() gravado DENTRO da ISR (nada mais na ISR!)
//   utcSec = segundo UTC daquela borda. O NMEA diz QUAL segundo e; o PPS da a
//            borda. Cuidado: a sentenca NMEA chega DEPOIS do pulso, entao o
//            segundo a casar com a borda e (ss do ultimo NMEA) + 1.
//
// OBRIGATORIO: utcSec e UTC, nunca a semana/TOW de GPS. Se voce trocar o parser
// por UBX-TIM-TP (que reporta na grade GPS), SUBTRAIA TDMA_GRID_OFFSET_SEC antes
// de chamar aqui. Misturar as duas grades na mesma rede desalinha os frames em
// silencio quando frameSecs nao divide 18 - ver o bloco da grade la em cima.
//
// No P4 o ppsUs nao precisa vir de ISR: GPIO -> ETM -> GPTIMER_ETM_TASK_CAPTURE
// carimba a borda em hardware (12,5 ns) e voce le com gptimer_get_captured_count().
// A interface aqui nao muda; so melhora a procedencia do numero.
inline void tdmaOnPps(Tdma& t, uint64_t ppsUs, uint32_t utcSec){
  // O pulso tem de se PROVAR antes de mexer na ancora. Ate TDMA_PPS_FIRME
  // pulsos limpos e seguidos, ele so e contado - o no continua na grade do
  // grupo. Sem isto um GPS marginal joga a rede inteira de um lado para o outro.
  uint64_t d = (ppsUs > t.lastPpsUs) ? (ppsUs - t.lastPpsUs) : 0;
  if (t.lastPpsUs && d > 800000ULL && d < 1200000ULL) {
    if (t.ppsRun < 0xFFFF) t.ppsRun++;
  } else {
    t.ppsRun = 1;                       // primeira borda, ou uma falha no meio
  }
  t.lastPpsUs = ppsUs;
  if (t.ppsRun < TDMA_PPS_FIRME) return;

  t.sync = TDMA_SYNC_PPS;
  t.anchorUs = ppsUs; t.anchorSec = utcSec;
  // Quem tem PPS e referencia por direito proprio: o relogio dele e absoluto, nao
  // emprestado. Marcar isso deixa a eleicao e o tdmaBeaconAceita coerentes - ele
  // nao aceita beacon de ninguem enquanto o pulso vier. Se o PPS calar, o
  // holdover ainda acende (o atalho do tdmaTick e so para o modo beacon) e ele
  // volta a aceitar a rede como fonte.
  t.anchorSrc = t.nodeId;
  t.anchorSrcPps = true;
  t.isAnchor = true;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}

inline uint32_t tdmaGuardNow(const Tdma& t){
  return t.holdover ? t.guardUs * TDMA_HOLDOVER_GUARD : t.guardUs;
}

// Atualiza holdover. Chamar no loop do radio antes de decidir o TX.
inline void tdmaTick(Tdma& t){
  if (!t.haveAnchor) return;
  // Quem ANCORA a rede nao tem quem o sincronize: ele ancorou em si mesmo no
  // setup e nunca re-ancora. Marcar holdover nele (o que acontecia depois de
  // 1,5s) triplicava a guarda do no 0 para sempre, encolhendo a janela util dele
  // de 110ms para 80ms, e acendia o alarme de HOLDOVER em regime normal.
  if (t.isAnchor && t.sync == TDMA_SYNC_BEACON) { t.holdover = false; return; }
  uint64_t now = tdmaNowUs();
  // O TTL acompanha o FRAME, com o valor antigo como piso. No modo beacon a
  // ancora e o pacote de POSICAO do no 0, e de oito em oito frames ele manda
  // roster no lugar - o maior silencio NORMAL entre ancoras e 2 frames. Com TTL
  // fixo de 1,5s e frame de 1s, a rede "entrava em holdover" em regime normal a
  // cada 8 s: guarda triplicada e alarme aceso sem nada errado. E o holdover nao
  // precisa ser nervoso: 2,5 s de drift a 10 ppm sao 25 us, nada contra uma
  // guarda de 20 ms - ele existe para silencios de verdade, medidos em segundos.
  uint64_t ttl = (uint64_t)t.frameUs * 2 + 500000ULL;
  if (ttl < TDMA_ANCHOR_TTL_US) ttl = TDMA_ANCHOR_TTL_US;
  t.holdover = (now - t.anchorUs) > ttl;
}

// ------------------------------------------------------- posicao no frame
// us decorridos desde o inicio do frame corrente (0 .. frameUs-1).
// Retorna false se ainda nao ha ancora (nao transmitir nesse estado).
inline bool tdmaUsInFrame(const Tdma& t, uint32_t& usInFrame, uint32_t& frameIdx){
  if (!t.haveAnchor) return false;
  uint64_t now = tdmaNowUs();
  uint64_t since = now - t.anchorUs;

  if (t.sync == TDMA_SYNC_PPS) {
    // A borda do PPS pode cair no MEIO de um frame: o offset dentro do frame e
    // dado pelo segundo UTC, nao pela borda.
    uint32_t secInFrame = t.anchorSec % t.frameSecs;
    uint64_t off = (uint64_t)secInFrame * 1000000ULL + since;
    usInFrame = (uint32_t)(off % t.frameUs);
    frameIdx  = (uint32_t)((t.anchorSec / t.frameSecs) + (off / t.frameUs));
  } else {
    usInFrame = (uint32_t)(since % t.frameUs);
    // frameBase mantem o indice monotonico atraves das re-ancoras do beacon;
    // o termo (since / frameUs) cobre os frames em que o beacon NAO chegou.
    frameIdx  = t.frameBase + (uint32_t)(since / t.frameUs);
  }
  return true;
}

inline uint32_t tdmaSlotStartUs(const Tdma& t, uint8_t slot){ return (uint32_t)slot * t.slotUs; }

// Qual slot esta ativo agora (util p/ conferir se o pacote veio no slot certo).
inline uint8_t tdmaSlotAt(const Tdma& t, uint32_t usInFrame){
  uint8_t s = (uint8_t)(usInFrame / t.slotUs);
  return s < t.nSlots ? s : (uint8_t)(t.nSlots - 1);
}

// ------------------------------------------------------------ decisao de TX
// true UMA vez por frame, quando estamos dentro da janela util do meu slot:
//   [inicio + guarda/2 , inicio + slotUs - guarda/2 - airtime]
// A meia-guarda de cada lado deixa o pacote centrado no slot: sobra margem
// tanto p/ atraso de entrada quanto p/ o tempo no ar nao vazar pro vizinho.
//
// O air-time entra no limite SUPERIOR porque a janela limita o INICIO do TX e o
// pacote inteiro tem de caber no slot. Sem o desconto, um loop atrasado (ex.:
// repintando o mapa por 100 ms) autorizava um TX no fim da janela cujos ultimos
// ~48 ms (16 B em SF7) invadiam o slot do vizinho - colisao sistematica que so
// aparece quando a tela esta ocupada, o pior tipo de bug de campo. Com
// airtimeUs = 0 (nao informado) o comportamento antigo se mantem.
inline bool tdmaShouldTx(Tdma& t){
  uint32_t usInFrame, frameIdx;
  if (!tdmaUsInFrame(t, usInFrame, frameIdx)) return false;
  if (t.haveTxFrame && t.txFrameDone == frameIdx) return false;   // ja falei neste frame

  // ESCUTA SILENCIOSA - o desempate de duas ancoras surdas.
  //
  // Dois nos que assumem a ancora quase ao mesmo tempo ficam com as grades
  // deslocadas por um valor qualquer. Se esse deslocamento calhar de ser
  // ~1 slot, cada um transmite EXATAMENTE quando o outro transmite: os dois
  // ficam surdos um para o outro e a rede permanece partida PARA SEMPRE, sem
  // erro nenhum aparecer. O tdmaBeaconAceita nao salva - ele so age quando o
  // beacon do outro CHEGA, e nesse arranjo ele nunca chega.
  //
  // Medido na bancada em 2026-08-29: slot 0 e slot 1 ambos com "fonte=EU", cada
  // um recebendo so 1 pacote por quadro (o terceiro no) em vez de 2.
  //
  // Solucao: quem e ancora SEM ser o menor slot cala a boca um quadro a cada 16
  // e escuta o quadro inteiro. Se existir uma ancora de slot menor, ela vai ser
  // ouvida nesse quadro e o tdmaBeaconAceita faz o resto. Custa 6% das falas
  // enquanto durar - e dura poucos segundos, porque e assim que termina. O no 0
  // nunca cede (nao ha slot menor), entao nunca precisa se calar.
  if (t.isAnchor && t.nodeId != 0 && (frameIdx % 16) == 15) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true;   // cedido de proposito, nao e falha
    return false;
  }

  uint32_t g = tdmaGuardNow(t);
  uint32_t half = g / 2;
  uint32_t start = tdmaSlotStartUs(t, t.nodeId);
  if (t.slotUs <= g) return false;                                // slot menor que a guarda: config invalida
  if (t.airtimeUs && t.airtimeUs + g >= t.slotUs) return false;   // pacote nao cabe: mesmo criterio do headroom
  uint32_t lo = start + half;
  uint32_t hi = start + t.slotUs - half - t.airtimeUs;

  if (usInFrame >= lo && usInFrame <= hi) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true; t.txCount++;
    return true;
  }
  // perdi a janela do meu slot neste frame (loop travado?) -> contabiliza
  if (usInFrame > hi && !(t.haveTxFrame && t.txFrameDone == frameIdx)) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true; t.missedTx++;
  }
  return false;
}

// Quanto falta (us) para o inicio da minha janela. Serve p/ vTaskDelayUntil,
// que e como o radio deve esperar: acorda na hora, sem polling.
inline uint32_t tdmaUsUntilMySlot(const Tdma& t){
  uint32_t usInFrame, frameIdx;
  if (!tdmaUsInFrame(t, usInFrame, frameIdx)) return t.slotUs;
  uint32_t lo = tdmaSlotStartUs(t, t.nodeId) + tdmaGuardNow(t) / 2;
  if (usInFrame <= lo) return lo - usInFrame;
  return (t.frameUs - usInFrame) + lo;   // proximo frame
}

// ====================================================================
//  PACOTE DE POSICAO - 16 bytes (secao 7 do PLANO_IMPLEMENTACAO_MESH.md)
//  lat/lon em int32 = grau*1e7 -> ~1,1cm de resolucao. Little-endian.
//  Na fase 2 o pacote do no 0 e TAMBEM o beacon de sincronia: um formato so.
// ====================================================================
#define TDMA_PKT_N 16
#define TDMA_FL_FIX    0x01
#define TDMA_FL_ALERT  0x02
#define TDMA_FL_LEADER 0x04
// "eu estou na grade UTC" - quem ouve usa isto para preferir esta fonte a
// qualquer beacon de grade arbitraria, mesmo de slot menor. Ver tdmaBeaconAceita.
#define TDMA_FL_PPS    0x08

struct TdmaPkt {
  uint32_t room;      // 24 bits
  uint8_t  slot;      // slot TDMA = identidade
  uint8_t  seq;       // sequencia rolante (detecta perda/reordem)
  uint8_t  flags;
  double   lat, lon;
  float    heading;   // graus
  uint8_t  speed;     // km/h
};

inline void tdmaPutLE32(uint8_t* p, int32_t v){
  p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}
inline int32_t tdmaGetLE32(const uint8_t* p){
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

inline void tdmaPack(uint8_t* p, const TdmaPkt& k){
  p[0] = k.room & 0xFF; p[1] = (k.room >> 8) & 0xFF; p[2] = (k.room >> 16) & 0xFF;
  p[3] = k.slot;
  p[4] = k.seq;
  p[5] = k.flags;
  tdmaPutLE32(p + 6,  (int32_t)(k.lat * 1e7));
  tdmaPutLE32(p + 10, (int32_t)(k.lon * 1e7));
  float h = k.heading; while (h < 0) h += 360.0f; while (h >= 360.0f) h -= 360.0f;
  p[14] = (uint8_t)(h / 2.0f);    // graus/2 cabe em 1 byte (resolucao de 2 graus)
  p[15] = k.speed;
}

inline bool tdmaUnpack(const uint8_t* p, uint8_t len, TdmaPkt& k){
  if (len < TDMA_PKT_N) return false;
  k.room = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
  k.slot = p[3];
  k.seq  = p[4];
  k.flags = p[5];
  k.lat = tdmaGetLE32(p + 6)  / 1e7;
  k.lon = tdmaGetLE32(p + 10) / 1e7;
  k.heading = p[14] * 2.0f;
  k.speed = p[15];
  return true;
}

inline bool tdmaPktFix(const TdmaPkt& k){ return k.flags & TDMA_FL_FIX; }
inline bool tdmaPktAlert(const TdmaPkt& k){ return k.flags & TDMA_FL_ALERT; }
inline bool tdmaPktLeader(const TdmaPkt& k){ return k.flags & TDMA_FL_LEADER; }
