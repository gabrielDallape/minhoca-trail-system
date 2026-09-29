// TESTES DA ELEICAO DE ANCORA - o que faltava quando a rede quebrou em campo.
//
//   node web/bancada-trilha/eleicao_test.js
//
// POR QUE EXISTE: toda a logica de eleicao, prioridade do PPS, histerese e
// escuta silenciosa nasceu em 2026-08-28/29 DEPOIS de um teste de campo falho, e
// foi escrita sem teste nenhum. Os quatro defeitos seguintes so apareceram
// quando tres telas de verdade foram ligadas juntas - cada um deles esta aqui
// embaixo como caso de regressao, com o nome do sintoma que produziu:
//
//   [R1] so o no 0 ancorava        -> rede inteira muda (sinc=- e tx=0)
//   [R2] beacon do slot 0 apenas   -> quem nao ouve o slot 0 nunca fala
//   [R3] roster primeiro           -> rede se parte em duas ancoras no arranque
//   [R4] duas ancoras deslocadas   -> surdas uma para a outra PARA SEMPRE
//   [R5] PPS piscando              -> a rede inteira persegue e volta
//   [R6] promocao mantinha UTC     -> nunca mais aceitava a fonte de PPS real
//
// O port abaixo e uma traducao LINHA A LINHA do firmware/tdma_core.h. A ultima
// secao le o .h de verdade e confere que port e firmware nao divergiram - se
// alguem "consertar" um destes comportamentos no C sem avisar, o teste acusa.
const fs = require("fs");
const path = require("path");

// =====================================================================
//  PORT do tdma_core.h  (traducao direta; nomes iguais aos do C)
// =====================================================================
const SYNC_NONE = 0, SYNC_BEACON = 1, SYNC_PPS = 2;
const ANCHOR_TTL_US = 1500000;
const HOLDOVER_GUARD = 3;
const ELECT_BASE_US = 3000000;
const PPS_FIRME = 10;
const SEM_FONTE = 0xFF;

let NOW = 0;                              // o relogio de teste (TDMA_TEST_CLOCK)

function tdmaInit(nodeId, nSlots, frameSecs, guardUs) {
  const t = {};
  t.nodeId = nodeId;
  t.nSlots = nSlots || 1;
  t.frameSecs = frameSecs || 1;
  t.frameUs = t.frameSecs * 1000000;
  t.slotUs = Math.floor(t.frameUs / t.nSlots);
  t.guardUs = guardUs;
  t.sync = SYNC_NONE;
  t.anchorUs = 0; t.anchorSec = 0;
  t.haveAnchor = false; t.holdover = false;
  t.frameBase = 0;
  t.airtimeUs = 0;
  t.isAnchor = false;                     // NAO nasce ancora por ser o slot 0
  t.anchorSrc = SEM_FONTE;
  t.anchorSrcPps = false;
  t.ppsRun = 0; t.lastPpsUs = 0;
  t.bornUs = NOW;
  t.gridSensitive = !tdmaFrameSecsGridSafe(t.frameSecs);
  t.txFrameDone = 0; t.haveTxFrame = false;
  t.anchorCount = 0; t.txCount = 0; t.missedTx = 0;
  return t;
}
const tdmaFrameSecsGridSafe = f => !!f && (18 % f) === 0;
const tdmaSetAirtime = (t, us) => { t.airtimeUs = us; };
const tdmaHeadroomUs = (t, air) => t.slotUs - air - t.guardUs;
const tdmaSlotStartUs = (t, slot) => slot * t.slotUs;
const tdmaGuardNow = t => t.holdover ? t.guardUs * HOLDOVER_GUARD : t.guardUs;

function tdmaBeaconAceita(t, srcSlot, srcPps) {
  if (srcSlot === t.nodeId) return false;
  if (t.sync === SYNC_PPS && !t.holdover) return false;
  if (!t.haveAnchor) return true;
  if (srcPps) {
    if (!t.anchorSrcPps) return true;
    return srcSlot <= t.anchorSrc;
  }
  if (t.anchorSrcPps && !t.holdover) return false;
  if (srcSlot === t.anchorSrc) return true;
  return srcSlot < t.nodeId && srcSlot < t.anchorSrc;
}

function tdmaOnBeaconFrom(t, rxUs, srcSlot, srcPps) {
  let edge = rxUs;
  if (t.airtimeUs && edge > t.airtimeUs) edge -= t.airtimeUs;
  const off = srcSlot * t.slotUs;
  edge = edge > off ? edge - off : 0;

  const trocouFonte = t.haveAnchor && (t.anchorSrc !== srcSlot || t.anchorSrcPps !== srcPps);
  if (t.haveAnchor && t.sync === SYNC_BEACON && !trocouFonte && edge >= t.anchorUs) {
    const d = edge - t.anchorUs;
    const n = Math.floor((d + t.frameUs / 2) / t.frameUs);
    t.frameBase += n ? n : 1;
  } else if (trocouFonte) {
    t.frameBase++;
    t.haveTxFrame = false;
  }
  t.sync = SYNC_BEACON;
  t.anchorUs = edge; t.anchorSec = 0;
  t.anchorSrc = srcSlot;
  t.anchorSrcPps = srcPps;
  t.isAnchor = false;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}
const tdmaOnBeacon = (t, rxUs) => tdmaOnBeaconFrom(t, rxUs, 0, false);

function tdmaAutoAncora(t) {
  t.sync = SYNC_BEACON;
  t.anchorUs = NOW; t.anchorSec = 0;
  t.anchorSrc = t.nodeId;
  t.anchorSrcPps = false;
  t.isAnchor = true;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}
function tdmaPromoveAncora(t) {
  t.isAnchor = true;
  t.anchorSrc = t.nodeId;
  t.anchorSrcPps = false;      // larga a alegacao de UTC: sem PPS eu corro livre
  t.holdover = false;
}

function tdmaEleicao(t) {
  if (!t.haveAnchor) {
    const espera = ELECT_BASE_US + t.nodeId * t.frameUs;
    if (NOW >= t.bornUs && (NOW - t.bornUs) >= espera) { tdmaAutoAncora(t); return true; }
    return false;
  }
  if (t.sync === SYNC_BEACON && t.holdover && !t.isAnchor) { tdmaPromoveAncora(t); return true; }
  if (!t.isAnchor && !t.anchorSrcPps && t.sync === SYNC_BEACON &&
      t.anchorSrc !== SEM_FONTE && t.anchorSrc > t.nodeId) { tdmaPromoveAncora(t); return true; }
  return false;
}

function tdmaOnPps(t, ppsUs, utcSec) {
  const d = ppsUs > t.lastPpsUs ? ppsUs - t.lastPpsUs : 0;
  if (t.lastPpsUs && d > 800000 && d < 1200000) { if (t.ppsRun < 0xFFFF) t.ppsRun++; }
  else t.ppsRun = 1;
  t.lastPpsUs = ppsUs;
  if (t.ppsRun < PPS_FIRME) return;
  t.sync = SYNC_PPS;
  t.anchorUs = ppsUs; t.anchorSec = utcSec;
  t.anchorSrc = t.nodeId; t.anchorSrcPps = true;
  t.isAnchor = true;
  t.haveAnchor = true; t.holdover = false; t.anchorCount++;
}

function tdmaTick(t) {
  if (!t.haveAnchor) return;
  if (t.isAnchor && t.sync === SYNC_BEACON) { t.holdover = false; return; }
  let ttl = t.frameUs * 2 + 500000;
  if (ttl < ANCHOR_TTL_US) ttl = ANCHOR_TTL_US;
  t.holdover = (NOW - t.anchorUs) > ttl;
}

function tdmaUsInFrame(t) {
  if (!t.haveAnchor) return null;
  const since = NOW - t.anchorUs;
  if (t.sync === SYNC_PPS) {
    const secInFrame = t.anchorSec % t.frameSecs;
    const off = secInFrame * 1000000 + since;
    return { usInFrame: off % t.frameUs,
             frameIdx: Math.floor(t.anchorSec / t.frameSecs) + Math.floor(off / t.frameUs) };
  }
  return { usInFrame: since % t.frameUs,
           frameIdx: t.frameBase + Math.floor(since / t.frameUs) };
}

function tdmaShouldTx(t) {
  const p = tdmaUsInFrame(t);
  if (!p) return false;
  const { usInFrame, frameIdx } = p;
  if (t.haveTxFrame && t.txFrameDone === frameIdx) return false;

  // escuta silenciosa: quem e ancora sem ser o menor slot cala 1 quadro em 16
  if (t.isAnchor && t.nodeId !== 0 && (frameIdx % 16) === 15) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true;
    return false;
  }
  const g = tdmaGuardNow(t), half = Math.floor(g / 2);
  const start = tdmaSlotStartUs(t, t.nodeId);
  if (t.slotUs <= g) return false;
  if (t.airtimeUs && t.airtimeUs + g >= t.slotUs) return false;
  const lo = start + half, hi = start + t.slotUs - half - t.airtimeUs;
  if (usInFrame >= lo && usInFrame <= hi) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true; t.txCount++;
    return true;
  }
  if (usInFrame > hi && !(t.haveTxFrame && t.txFrameDone === frameIdx)) {
    t.txFrameDone = frameIdx; t.haveTxFrame = true; t.missedTx++;
  }
  return false;
}

// =====================================================================
//  arnes de teste
// =====================================================================
let pass = 0, fail = 0;
const falhas = [];
const ok = (c, n) => { if (c) pass++; else { fail++; falhas.push(n); }
  console.log((c ? "  PASS " : "  FAIL ") + n); };
const eq = (a, b, n) => { const c = a === b; if (c) pass++; else { fail++; falhas.push(`${n} (${a} != ${b})`); }
  console.log((c ? "  PASS " : "  FAIL ") + n + (c ? "" : `   (${a} != ${b})`)); };
const sec = t => console.log("\n" + t);

// Roda uma rede de N nos por T segundos, entregando os pacotes de quem transmite
// a quem estiver ouvindo. surdos: pares [a,b] que NAO se ouvem.
function roda(nos, segundos, opts = {}) {
  const passo = opts.passo || 5000;              // 5 ms
  const surdos = opts.surdos || [];
  const naoOuve = (a, b) => surdos.some(p => (p[0] === a && p[1] === b) || (p[0] === b && p[1] === a));
  const fim = NOW + segundos * 1000000;
  const recebidos = nos.map(() => 0);
  while (NOW < fim) {
    // 1) cada no decide se transmite AGORA
    const noAr = [];
    for (let i = 0; i < nos.length; i++) {
      const t = nos[i];
      tdmaTick(t);
      tdmaEleicao(t);
      if (tdmaShouldTx(t)) noAr.push(i);
    }
    // 2) entrega. Duas transmissoes simultaneas colidem: ninguem recebe nada.
    if (noAr.length === 1) {
      const i = noAr[0], src = nos[i];
      const srcPps = src.sync === SYNC_PPS && !src.holdover;
      for (let j = 0; j < nos.length; j++) {
        if (j === i || naoOuve(i, j)) continue;
        recebidos[j]++;
        // o rxDone chega no fim do pacote: soma o air-time, como no radio real
        const rxUs = NOW + (src.airtimeUs || 0);
        if (tdmaBeaconAceita(nos[j], src.nodeId, srcPps))
          tdmaOnBeaconFrom(nos[j], rxUs, src.nodeId, srcPps);
      }
    }
    NOW += passo;
  }
  return recebidos;
}
const fontes = nos => nos.map(t => t.isAnchor ? "EU" : (t.haveAnchor ? "s" + t.anchorSrc : "-"));
const ancoras = nos => nos.filter(t => t.isAnchor).length;

// =====================================================================
sec("[1] estado inicial - ninguem nasce ancora (R1)");
{
  for (let s = 0; s < 8; s++) {
    const t = tdmaInit(s, 8, 1, 20000);
    ok(!t.isAnchor, `slot ${s} nao nasce ancora`);
  }
  const t = tdmaInit(0, 8, 1, 20000);
  eq(t.anchorSrc, SEM_FONTE, "anchorSrc nasce sem fonte");
  ok(!t.haveAnchor, "nao nasce com ancora");
  ok(!t.anchorSrcPps, "nao nasce alegando grade UTC");
  eq(t.ppsRun, 0, "contador de PPS nasce zerado");
  ok(!tdmaShouldTx(t), "sem ancora NAO transmite");
}

sec("[2] eleicao: a espera cresce com o slot");
{
  for (const s of [0, 1, 2, 5, 7]) {
    NOW = 0;
    const t = tdmaInit(s, 8, 1, 20000);
    const esperado = ELECT_BASE_US + s * 1000000;
    NOW = esperado - 1;  tdmaEleicao(t);
    ok(!t.isAnchor, `slot ${s} ainda nao assumiu 1 us antes (${esperado} us)`);
    NOW = esperado;      tdmaEleicao(t);
    ok(t.isAnchor, `slot ${s} assume exatamente em ${esperado / 1e6} s`);
  }
}

sec("[3] um no sozinho NUNCA fica mudo (R1 - o bug do campo)");
{
  for (const s of [0, 1, 2, 7]) {
    NOW = 0;
    const t = tdmaInit(s, 8, 1, 20000);
    tdmaSetAirtime(t, 58000);
    roda([t], 15);
    ok(t.txCount > 0, `slot ${s} sozinho acaba transmitindo (tx=${t.txCount})`);
    ok(t.isAnchor, `slot ${s} sozinho vira a propria ancora`);
  }
}

sec("[4] o MENOR slot presente vence a eleicao");
{
  const casos = [[0, 1, 2], [1, 2, 3], [2, 5], [3, 7], [1, 7]];
  for (const slots of casos) {
    NOW = 0;
    const nos = slots.map(s => { const t = tdmaInit(s, 8, 1, 20000); tdmaSetAirtime(t, 58000); return t; });
    roda(nos, 40);
    eq(ancoras(nos), 1, `slots [${slots}] convergem para UMA ancora`);
    ok(nos[0].isAnchor, `slots [${slots}]: quem ancora e o menor (${slots[0]})`);
    ok(nos.slice(1).every(t => t.anchorSrc === slots[0]),
       `slots [${slots}]: os demais seguem o slot ${slots[0]}`);
    ok(nos.every(t => t.txCount > 0), `slots [${slots}]: todos transmitem`);
  }
}

sec("[5] politica de aceitacao de beacon");
{
  const novo = (slot, cfg = {}) => {
    const t = tdmaInit(slot, 8, 1, 20000);
    Object.assign(t, cfg);
    return t;
  };
  ok(!tdmaBeaconAceita(novo(3), 3, false), "recusa o proprio slot (eco)");
  ok(tdmaBeaconAceita(novo(3), 0, false), "sem ancora aceita qualquer um");
  ok(tdmaBeaconAceita(novo(3), 7, false), "sem ancora aceita ate slot maior");

  const comFonte = s => novo(3, { haveAnchor: true, sync: SYNC_BEACON, anchorSrc: s });
  ok(tdmaBeaconAceita(comFonte(2), 2, false), "refresco da propria fonte e aceito");
  ok(tdmaBeaconAceita(comFonte(2), 1, false), "fonte melhor (slot menor) e aceita");
  ok(!tdmaBeaconAceita(comFonte(1), 2, false), "fonte pior (slot maior que a atual) e recusada");
  ok(!tdmaBeaconAceita(comFonte(1), 5, false), "slot maior que o MEU e recusado (R4: evita o circulo)");
  ok(!tdmaBeaconAceita(comFonte(1), 4, false), "slot 4 > meu 3: recusado mesmo sendo menor que nada");

  const euPps = novo(3, { haveAnchor: true, sync: SYNC_PPS, anchorSrcPps: true, isAnchor: true });
  ok(!tdmaBeaconAceita(euPps, 0, false), "quem tem PPS nao aceita beacon comum");
  ok(!tdmaBeaconAceita(euPps, 0, true), "quem tem PPS nao aceita nem outro PPS");
  euPps.holdover = true;
  ok(tdmaBeaconAceita(euPps, 0, true), "em holdover, PPS de fora volta a ser aceito");
}

sec("[6] PPS ganha de slot, sempre (R6)");
{
  const t = tdmaInit(1, 8, 1, 20000);
  t.haveAnchor = true; t.sync = SYNC_BEACON; t.isAnchor = true;
  t.anchorSrc = 1; t.anchorSrcPps = false;
  ok(tdmaBeaconAceita(t, 2, true), "ancora do slot 1 CEDE para a grade UTC do slot 2");
  ok(!tdmaBeaconAceita(t, 2, false), "mas nao cede para um beacon comum do slot 2");
  ok(tdmaBeaconAceita(t, 7, true), "cede ate para o slot 7, se ele estiver em UTC");

  const emUtc = tdmaInit(1, 8, 1, 20000);
  emUtc.haveAnchor = true; emUtc.sync = SYNC_BEACON;
  emUtc.anchorSrc = 2; emUtc.anchorSrcPps = true;
  ok(!tdmaBeaconAceita(emUtc, 0, false), "nao rebaixa de UTC para grade arbitraria");
  ok(tdmaBeaconAceita(emUtc, 0, true), "entre fontes UTC, prefere o slot menor");
  ok(!tdmaBeaconAceita(emUtc, 5, true), "entre fontes UTC, ignora slot maior que a atual");
  emUtc.holdover = true;
  ok(tdmaBeaconAceita(emUtc, 0, false), "com a fonte UTC calada, aceita beacon comum menor");
}

sec("[7] promocao preserva a FASE mas larga a alegacao de UTC (R6)");
{
  NOW = 0;
  const t = tdmaInit(1, 8, 1, 20000);
  tdmaOnBeaconFrom(t, 500000, 2, true);
  const faseAntes = t.anchorUs;
  ok(t.anchorSrcPps, "seguindo uma fonte UTC, alega UTC");
  ok(!t.isAnchor, "e nao e ancora");
  tdmaPromoveAncora(t);
  eq(t.anchorUs, faseAntes, "a promocao NAO mexe na fase herdada");
  ok(t.isAnchor, "vira ancora");
  ok(!t.anchorSrcPps, "mas larga a alegacao de UTC (senao trava a recaptura)");
  ok(tdmaBeaconAceita(t, 2, true), "e por isso volta a aceitar a fonte de PPS real");
}

sec("[8] histerese do PPS - um pulso que pisca nao vale (R5)");
{
  NOW = 0;
  const t = tdmaInit(2, 8, 1, 20000);
  for (let i = 1; i < PPS_FIRME; i++) {
    tdmaOnPps(t, i * 1000000, 100 + i);
    ok(t.sync !== SYNC_PPS, `pulso ${i} de ${PPS_FIRME}: ainda nao ancora`);
  }
  tdmaOnPps(t, PPS_FIRME * 1000000, 100 + PPS_FIRME);
  eq(t.sync, SYNC_PPS, `no pulso ${PPS_FIRME} o PPS passa a valer`);
  ok(t.anchorSrcPps, "e ai sim ele alega a grade UTC");

  // um buraco no meio zera a contagem
  const u = tdmaInit(2, 8, 1, 20000);
  for (let i = 1; i <= 8; i++) tdmaOnPps(u, i * 1000000, i);
  eq(u.ppsRun, 8, "oito pulsos limpos contam oito");
  tdmaOnPps(u, 8e6 + 3000000, 12);          // 3 s depois: falhou
  eq(u.ppsRun, 1, "um intervalo errado zera a contagem");
  ok(u.sync !== SYNC_PPS, "e o PPS volta a nao valer");
}

sec("[9] escuta silenciosa quebra o impasse de duas ancoras surdas (R4)");
{
  const t = tdmaInit(1, 8, 1, 20000);
  t.isAnchor = true; t.haveAnchor = true; t.sync = SYNC_BEACON;
  let calados = 0, falados = 0;
  for (let f = 0; f < 64; f++) {
    // 180 ms cai DENTRO da janela util do slot 1 (135..240 ms). O erro obvio
    // aqui e usar um numero "no meio do quadro" como 70 ms - aquilo e o slot 0.
    NOW = f * 1000000 + 180000;
    t.anchorUs = 0; t.frameBase = 0;
    t.haveTxFrame = false;
    if (tdmaShouldTx(t)) falados++; else calados++;
  }
  eq(calados, 4, "cala 1 quadro a cada 16 (4 em 64)");
  eq(falados, 60, "e fala nos outros 60");

  const zero = tdmaInit(0, 8, 1, 20000);
  zero.isAnchor = true; zero.haveAnchor = true; zero.sync = SYNC_BEACON;
  let calouZero = 0;
  for (let f = 0; f < 64; f++) {
    NOW = f * 1000000 + 10000;
    zero.anchorUs = 0; zero.frameBase = 0; zero.haveTxFrame = false;
    if (!tdmaShouldTx(zero)) calouZero++;
  }
  eq(calouZero, 0, "o slot 0 NUNCA cala (nao ha slot menor para ouvir)");

  const seguidor = tdmaInit(3, 8, 1, 20000);
  seguidor.haveAnchor = true; seguidor.sync = SYNC_BEACON; seguidor.anchorSrc = 0;
  let calouSeg = 0;
  for (let f = 0; f < 64; f++) {
    NOW = f * 1000000 + 3 * 125000 + 10000;
    seguidor.anchorUs = 0; seguidor.frameBase = 0; seguidor.haveTxFrame = false;
    if (!tdmaShouldTx(seguidor)) calouSeg++;
  }
  eq(calouSeg, 0, "quem NAO e ancora nunca cala");
}

sec("[10] o impasse real: duas ancoras se juntam sozinhas (R4)");
{
  NOW = 0;
  const a = tdmaInit(0, 8, 1, 20000), b = tdmaInit(1, 8, 1, 20000);
  tdmaSetAirtime(a, 58000); tdmaSetAirtime(b, 58000);
  // forca o pior caso: as duas ja ancoras, grades deslocadas de exatamente 1 slot
  tdmaAutoAncora(a);
  b.bornUs = NOW; tdmaAutoAncora(b); b.anchorUs = NOW - 125000;
  eq(ancoras([a, b]), 2, "partida: duas ancoras, grades deslocadas de 1 slot");
  roda([a, b], 60);
  eq(ancoras([a, b]), 1, "60 s depois sobrou UMA ancora");
  ok(a.isAnchor, "e e o slot 0 que ficou");
  eq(b.anchorSrc, 0, "o slot 1 passou a seguir o slot 0");
}

sec("[11] tres nos, o cenario da bancada");
{
  NOW = 0;
  const nos = [0, 1, 2].map(s => { const t = tdmaInit(s, 8, 1, 20000); tdmaSetAirtime(t, 58000); return t; });
  const rx = roda(nos, 60);
  eq(ancoras(nos), 1, "uma ancora so");
  ok(nos[0].isAnchor, "o slot 0 e a ancora");
  eq(nos[1].anchorSrc, 0, "slot 1 segue o slot 0");
  eq(nos[2].anchorSrc, 0, "slot 2 segue o slot 0");
  ok(nos.every(t => t.txCount > 40), "os tres transmitem sem parar");
  ok(rx.every(r => r > 40), "e os tres recebem dos outros dois");
  const razao = rx[1] / nos[1].txCount;
  ok(razao > 1.5, `rx/tx do slot 1 fica perto de 2 (${razao.toFixed(2)})`);
}

sec("[12] o slot 0 ausente NAO derruba a rede (R1/R2 - o bug do campo)");
{
  NOW = 0;
  const nos = [1, 2].map(s => { const t = tdmaInit(s, 8, 1, 20000); tdmaSetAirtime(t, 58000); return t; });
  roda(nos, 40);
  ok(nos.every(t => t.txCount > 0), "slots 1 e 2, sem nenhum slot 0, FALAM");
  eq(ancoras(nos), 1, "e formam uma rede com uma ancora so");
  ok(nos[0].isAnchor, "o slot 1 assume o lugar do slot 0 ausente");

  NOW = 0;
  const altos = [5, 6, 7].map(s => { const t = tdmaInit(s, 8, 1, 20000); tdmaSetAirtime(t, 58000); return t; });
  roda(altos, 60);
  ok(altos.every(t => t.txCount > 0), "ate slots 5,6,7 sozinhos formam rede");
  eq(ancoras(altos), 1, "com uma ancora so");
}

sec("[13] a ancora some no meio da trilha");
{
  NOW = 0;
  const nos = [0, 1, 2].map(s => { const t = tdmaInit(s, 8, 1, 20000); tdmaSetAirtime(t, 58000); return t; });
  roda(nos, 30);
  ok(nos[0].isAnchor, "arranque: slot 0 e a ancora");
  const txAntes = [nos[1].txCount, nos[2].txCount];
  // o slot 0 sai do ar (carro desligou / ficou para tras)
  roda([nos[1], nos[2]], 40);
  ok(nos[1].txCount > txAntes[0], "o slot 1 continua falando sem a ancora");
  ok(nos[2].txCount > txAntes[1], "o slot 2 tambem");
  eq(ancoras([nos[1], nos[2]]), 1, "os dois que sobraram tem uma ancora");
  ok(nos[1].isAnchor, "e quem assumiu foi o menor slot dos que restaram");
}

sec("[14] nao seguir slot maior por muito tempo (R4)");
{
  NOW = 0;
  const t = tdmaInit(1, 8, 1, 20000);
  tdmaOnBeaconFrom(t, 500000, 5, false);     // ancorou num slot MAIOR
  eq(t.anchorSrc, 5, "no arranque aceita mesmo um slot maior (melhor que mudo)");
  ok(!t.isAnchor, "e nao e ancora ainda");
  ok(tdmaEleicao(t), "mas a eleicao corrige isso no ciclo seguinte");
  ok(t.isAnchor, "o slot 1 assume, por ser menor que a fonte");
  eq(t.anchorSrc, 1, "e passa a ser a propria fonte");

  const u = tdmaInit(4, 8, 1, 20000);
  tdmaOnBeaconFrom(u, 500000, 2, false);
  ok(!tdmaEleicao(u), "quem segue um slot MENOR nao assume nada");
  ok(!u.isAnchor, "e continua seguidor");
}

sec("[15] recuperacao apos silencio (holdover)");
{
  NOW = 0;
  const t = tdmaInit(2, 8, 1, 20000);
  tdmaOnBeaconFrom(t, 100000, 1, false);
  ok(!t.isAnchor, "seguindo o slot 1");
  NOW = 100000 + 2 * 1000000 + 600000;       // passou do TTL
  tdmaTick(t);
  ok(t.holdover, "sem beacon, entra em holdover");
  eq(tdmaGuardNow(t), 60000, "e a guarda triplica");
  ok(tdmaEleicao(t), "a eleicao promove quem ficou sem fonte");
  ok(t.isAnchor, "ele assume a referencia");
  ok(!t.holdover, "e sai do holdover");
  tdmaTick(t);
  ok(!t.holdover, "ancora em modo beacon nao volta para holdover");
}

sec("[16] geometria e limites que nao podem regredir");
{
  const t = tdmaInit(0, 8, 1, 20000);
  eq(t.slotUs, 125000, "8 slots em 1 s -> 125 ms");
  eq(tdmaHeadroomUs(t, 58000), 47000, "folga de 47 ms com o air-time medido");
  tdmaSetAirtime(t, 58000);
  tdmaAutoAncora(t);
  NOW = t.anchorUs; eq(tdmaShouldTx(t), false, "nao transmite no instante 0 (dentro da guarda)");
  t.haveTxFrame = false;
  NOW = t.anchorUs + 10000; ok(tdmaShouldTx(t), "transmite depois de meia guarda");
  t.haveTxFrame = false;
  NOW = t.anchorUs + 120000; ok(!tdmaShouldTx(t), "nao transmite no fim do slot (o pacote vazaria)");

  const apertado = tdmaInit(0, 8, 1, 20000);
  tdmaSetAirtime(apertado, 110000);
  tdmaAutoAncora(apertado);
  NOW = apertado.anchorUs + 20000;
  ok(!tdmaShouldTx(apertado), "air-time que nao cabe no slot bloqueia o TX");

  ok(tdmaFrameSecsGridSafe(1), "frame de 1 s e imune a grade GPS/UTC");
  ok(tdmaFrameSecsGridSafe(3), "frame de 3 s tambem");
  ok(!tdmaFrameSecsGridSafe(4), "frame de 4 s e sensivel");
  ok(!tdmaFrameSecsGridSafe(5), "frame de 5 s tambem");
}

sec("[17] o beacon recua o offset do slot da fonte");
{
  NOW = 0;
  const t = tdmaInit(3, 8, 1, 20000);
  tdmaSetAirtime(t, 58000);
  // o slot 2 falou no meio do slot dele: 2*125ms + 60ms, e o rx chega no fim
  const inicioTx = 2 * 125000 + 60000;
  tdmaOnBeaconFrom(t, inicioTx + 58000, 2, false);
  eq(t.anchorUs, 60000, "a borda do frame sai do rx menos air-time menos 2 slots");

  const u = tdmaInit(3, 8, 1, 20000);
  tdmaSetAirtime(u, 58000);
  tdmaOnBeaconFrom(u, 58000 + 10000, 0, false);
  eq(u.anchorUs, 10000, "com fonte no slot 0 o recuo e so o air-time (compatibilidade)");
}

sec("[18] o firmware de verdade nao divergiu deste port");
{
  const h = fs.readFileSync(path.join(__dirname, "..", "..", "firmware", "tdma_core.h"), "utf8");
  const num = (re, nome, esperado) => {
    const m = h.match(re);
    eq(m ? Number(m[1]) : NaN, esperado, `${nome} = ${esperado} no tdma_core.h`);
  };
  num(/TDMA_ELECT_BASE_US\s*=\s*(\d+)/, "TDMA_ELECT_BASE_US", ELECT_BASE_US);
  num(/TDMA_PPS_FIRME\s*=\s*(\d+)/, "TDMA_PPS_FIRME", PPS_FIRME);
  num(/TDMA_ANCHOR_TTL_US\s*=\s*(\d+)/, "TDMA_ANCHOR_TTL_US", ANCHOR_TTL_US);
  num(/TDMA_HOLDOVER_GUARD\s*=\s*(\d+)/, "TDMA_HOLDOVER_GUARD", HOLDOVER_GUARD);

  ok(/inline bool tdmaEleicao/.test(h), "tdmaEleicao existe no firmware");
  ok(/inline bool tdmaBeaconAceita/.test(h), "tdmaBeaconAceita existe");
  ok(/inline void tdmaOnBeaconFrom/.test(h), "tdmaOnBeaconFrom existe");
  ok(/inline void tdmaPromoveAncora/.test(h), "tdmaPromoveAncora existe");
  ok(/t\.isAnchor = false;/.test(h), "R1: ninguem nasce ancora no tdmaInit");
  ok(!/t\.isAnchor = \(nodeId == 0\)/.test(h), "R1: a versao antiga (isAnchor = nodeId==0) sumiu");
  ok(/anchorSrcPps = false;/.test(h), "R6: a promocao larga a alegacao de UTC");
  ok(/frameIdx % 16\) == 15/.test(h), "R4: a escuta silenciosa esta no firmware");
  ok(/srcSlot < t\.nodeId && srcSlot < t\.anchorSrc/.test(h), "R4: so aceita fonte de slot menor");
  ok(/if \(t\.ppsRun < TDMA_PPS_FIRME\) return;/.test(h), "R5: o PPS so ancora depois de firme");
  ok(/uint64_t off = \(uint64_t\)srcSlot \* t\.slotUs;/.test(h), "o beacon recua o offset do slot");

  const r = fs.readFileSync(path.join(__dirname, "..", "..", "firmware", "mts_p4", "radio.h"), "utf8");
  ok(/falas > 0\) && \(\(falas <= 3\)/.test(r), "R3: a primeira fala e posicao, nao roster");
  ok(!/falas < 3\) \|\| \(falas % 8/.test(r), "R3: a ordem antiga (3 rosters primeiro) sumiu");
  ok(/tdmaBeaconAceita\(g_tdma, k\.slot, srcPps\)/.test(r), "o radio usa a politica de aceitacao");
  ok(/tdmaEleicao\(g_tdma\)/.test(r), "o radio roda a eleicao no laco");
  ok(/getIrqFlags\(\) & RADIOLIB_SX126X_IRQ_TX_DONE/.test(r), "o vigia de TX pergunta ao chip antes de acusar");
}

// =====================================================================
console.log(`\n${pass} PASS, ${fail} FAIL`);
if (fail) { console.log("\nfalhas:"); falhas.forEach(f => console.log("  - " + f)); }
process.exit(fail ? 1 : 0);
