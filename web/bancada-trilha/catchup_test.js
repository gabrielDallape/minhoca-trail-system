// Espelho em Node do firmware/mts/core/rota.h (e do frotaAckMin do core/frota.h)
// - roda no PC, sem placa:
//
//   node web\bancada-trilha\catchup_test.js
//
// POR QUE UM ESPELHO: nao ha g++ de host nesta maquina e o selftest de placa so
// roda com hardware. O codigo abaixo segue o C++ linha a linha (mesmos nomes,
// mesma aritmetica de 16 bits) para um bug de logica aparecer aqui antes de ir
// para a trilha.
//
// ATE 2026-09-29 ESTE ARQUIVO ESPELHAVA O mts_campo/rota_core.h, com constantes
// escritas a mao - e por isso passou verde com dois bugs do firmware/mts: o km[]
// que nao andava com a rota cheia, e o catch-up que travava para sempre num vao
// que o lider ja nao guardava. Agora as constantes sao LIDAS do params.h e as
// linhas que decidem o catch-up sao conferidas no C (secao "fidelidade").
'use strict';

const fs = require('fs'), path = require('path');
const RC = p => fs.readFileSync(path.join(__dirname, '..', '..', p), 'utf8');
const PARAMS = RC('firmware/mts/core/params.h');
const defC = nome => {
  const m = PARAMS.match(new RegExp('#define\\s+' + nome + '\\s+([^\\n/]+)'));
  if (!m) throw new Error('params.h sem ' + nome);
  const v = m[1].trim();
  const q = v.match(/^\((\d+)UL \* TDMA_FRAME_MS\)$/);
  if (q) return parseInt(q[1]) * defC('TDMA_FRAME_S') * 1000;
  return parseFloat(v.replace(/[UL]+$/i, ''));
};
const TRILHA_PONTOS_PKT = defC('TRILHA_PONTOS_PKT');
const ROTA_MAX = defC('ROTA_MAX');
const TRAJETO_MAX = defC('TRAJETO_MAX');
const TDMA_FRAME_MS = defC('TDMA_FRAME_S') * 1000;
const ROTA_ACK_TTL_MS = defC('ROTA_ACK_TTL_MS');
const ROTA_OUVE_PONTOS = defC('ROTA_OUVE_PONTOS');
const ROTA_SEQ_NENHUMA = 0xFFFF;
const TRILHA_CAB_N = 24, TRILHA_ABS_N = 8, TRILHA_DELTA_N = 4, TRILHA_ESC_DELTA = 1e5;
const TRILHA_PKT_N = n => n === 0 ? TRILHA_CAB_N : TRILHA_CAB_N + TRILHA_ABS_N + (n - 1) * TRILHA_DELTA_N;
const u16 = v => v & 0xFFFF;
const i16 = v => { v &= 0xFFFF; return v >= 0x8000 ? v - 0x10000 : v; };
const i32 = v => v | 0;

function rotaD2(la1, lo1, la2, lo2) {
  const k = 111320.0;
  const dy = (la2 - la1) * k, dx = (lo2 - lo1) * k * Math.cos(la1 * Math.PI / 180);
  return dx * dx + dy * dy;
}
function rotaNova() { return { p: [], km: [], n: 0, temEpoca: false, epoca: 0, base: 0, ultVseq: 0, ultSeq16: 0, prefixo: -1, temPiso: false, pisoV: 0, inseridos: 0, repetidos: 0, fora: 0 }; }
function rotaRefazPrefixo(r, desde) {
  if (r.n === 0) { r.prefixo = -1; return; }
  let i = desde < 0 ? 0 : desde;
  if (i > r.prefixo + 1) i = r.prefixo + 1;
  if (i < 0) i = 0;
  while (i + 1 < r.n && (r.p[i + 1].vseq === r.p[i].vseq + 1 ||
                         (r.temPiso && r.p[i + 1].vseq <= r.pisoV))) i++;
  r.prefixo = i;
}
function rotaRefazKm(r, desde) {
  for (let i = desde < 1 ? 0 : desde; i < r.n; i++) {
    if (i === 0) { r.km[0] = 0; continue; }
    r.km[i] = r.km[i - 1] + Math.sqrt(rotaD2(r.p[i - 1].lat, r.p[i - 1].lon, r.p[i].lat, r.p[i].lon));
  }
}
function rotaSeqVirtual(r, epoca, seq16) {
  if (!r.temEpoca || r.epoca !== epoca) {
    const topo = r.n ? r.p[r.n - 1].vseq + 1 : 0;
    r.base = topo;
    r.ultVseq = topo + seq16; r.ultSeq16 = seq16;
    r.epoca = epoca; r.temEpoca = true;
    r.temPiso = false;
    return r.ultVseq;
  }
  const d = i16(seq16 - r.ultSeq16);
  let v = r.ultVseq + d;
  if (v < r.base) v = r.base;
  r.ultVseq = v; r.ultSeq16 = seq16;
  return v;
}
function rotaInsere(r, vseq, lat, lon) {
  let pos = r.n;
  if (r.n && vseq <= r.p[r.n - 1].vseq) {
    let lo = 0, hi = r.n - 1;
    while (lo < hi) { const m = (lo + hi) >> 1; if (r.p[m].vseq < vseq) lo = m + 1; else hi = m; }
    pos = lo;
    if (r.p[pos].vseq === vseq) { r.repetidos++; return false; }
  }
  if (r.n >= ROTA_MAX) {
    if (pos === 0) { r.fora++; return false; }
    r.p.shift(); r.km.shift();
    { const k0 = r.km[0]; for (let j = 0; j < r.km.length; j++) r.km[j] -= k0; }
    r.n--; pos--; r.prefixo = -1;
  }
  r.p.splice(pos, 0, { lat, lon, vseq }); r.km.splice(pos, 0, 0);
  r.n++; r.inseridos++;
  rotaRefazKm(r, pos);
  if (r.prefixo < 0 || pos <= r.prefixo + 1) rotaRefazPrefixo(r, pos - 1);
  return true;
}
function rotaDefinePiso(r, epoca, oldest16) {
  if (!r.temEpoca || r.epoca !== epoca || oldest16 === ROTA_SEQ_NENHUMA) return;
  const d = i16(oldest16 - r.ultSeq16);
  let v = r.ultVseq + d;
  if (v < r.base) v = r.base;
  if (r.temPiso && v <= r.pisoV) return;
  r.temPiso = true; r.pisoV = v;
  if (r.n) rotaRefazPrefixo(r, r.prefixo < 0 ? 0 : r.prefixo);
}
function rotaAck16(r) {
  if (r.prefixo < 0 || !r.temEpoca) return ROTA_SEQ_NENHUMA;
  const v = r.p[r.prefixo].vseq;
  if (v < r.base) return ROTA_SEQ_NENHUMA;
  const s = u16(v - r.base);
  return s === ROTA_SEQ_NENHUMA ? 0xFFFE : s;
}
function rotaAckDe16(r) {
  if (!r.temEpoca) return ROTA_SEQ_NENHUMA;
  for (let i = 0; i < r.n; i++) if (r.p[i].vseq >= r.base) { const s = u16(r.p[i].vseq - r.base); return s === ROTA_SEQ_NENHUMA ? 0xFFFE : s; }
  return ROTA_SEQ_NENHUMA;
}
function rotaVaoAntes(r, i) { return i > 0 && i < r.n && (r.p[i].vseq - r.p[i - 1].vseq) > 1; }

function trilhaEscolheBloco(head, trajN, ackAteMin, ackDeMax, temAck, quadro, maxPontos, topoMin = ROTA_SEQ_NENHUMA, producao = 0) {
  if (trajN <= 0) return { base: 0, n: 0, catchup: false };
  const oldest = u16(head - u16(trajN - 1));
  let atras = false, inicioCatch = oldest, nCatch = maxPontos;
  if (temAck) {
    if (ackAteMin === ROTA_SEQ_NENHUMA) atras = true;
    else {
      const d = i16(ackAteMin - head);
      if (d < -maxPontos) {
        atras = true;
        inicioCatch = u16(ackAteMin + 1);
        if (i16(inicioCatch - oldest) < 0) inicioCatch = oldest;
      } else if (d <= 0 && ackDeMax !== ROTA_SEQ_NENHUMA && i16(ackDeMax - oldest) > 0) {
        atras = true;
        const fim = u16(ackDeMax - 1);
        const disp = u16(fim - oldest) + 1;
        nCatch = disp < maxPontos ? disp : maxPontos;
        inicioCatch = u16(fim - u16(nCatch - 1));
      }
    }
  }
  let frente = -1;
  if (temAck && topoMin !== ROTA_SEQ_NENHUMA) { const d = i16(head - topoMin); frente = d < 0 ? 0 : d; }
  if (producao < 0) producao = 0;
  const catchup = frente < 0 ? (atras && ((quadro & 1) === 1))
    : (atras && (frente + producao <= maxPontos || ((quadro & 3) === 1 && producao < maxPontos && frente + producao <= 2 * maxPontos)));
  let base, n;
  if (catchup) {
    base = inicioCatch;
    const disp = u16(head - base) + 1;
    n = disp < nCatch ? disp : nCatch;
  } else if (frente > 0 && frente <= 2 * maxPontos) {
    base = u16(topoMin + 1);
    if (i16(base - oldest) < 0) base = oldest;
    const disp = u16(head - base) + 1;
    n = disp < maxPontos ? disp : maxPontos;
  } else {
    n = trajN < maxPontos ? trajN : maxPontos;
    base = u16(head - u16(n - 1));
  }
  return { base, n, catchup };
}

function putLE32(p, o, v) { v = i32(v); p[o] = v & 0xFF; p[o + 1] = (v >> 8) & 0xFF; p[o + 2] = (v >> 16) & 0xFF; p[o + 3] = (v >>> 24) & 0xFF; }
function getLE32(p, o) { return (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) | 0; }
function putLE16(p, o, v) { p[o] = v & 0xFF; p[o + 1] = (v >> 8) & 0xFF; }
function getLE16(p, o) { return p[o] | (p[o + 1] << 8); }

function trilhaCodifica(pkt, epoca, base, ackAte, ackDe, n, ponto) {
  pkt[16] = epoca; putLE16(pkt, 17, base); putLE16(pkt, 19, ackAte); putLE16(pkt, 21, ackDe);
  let o = TRILHA_CAB_N, usados = 0, la0 = 0, lo0 = 0;
  for (let i = 0; i < n; i++) {
    let { la, lo } = ponto(i);
    if (i === 0) {
      putLE32(pkt, o, Math.round(la * 1e7)); o += 4;
      putLE32(pkt, o, Math.round(lo * 1e7)); o += 4;
    } else {
      const dla = Math.round((la - la0) * TRILHA_ESC_DELTA), dlo = Math.round((lo - lo0) * TRILHA_ESC_DELTA);
      if (dla > 32767 || dla < -32768 || dlo > 32767 || dlo < -32768) break;
      putLE16(pkt, o, u16(dla)); o += 2; putLE16(pkt, o, u16(dlo)); o += 2;
      la = la0 + dla / TRILHA_ESC_DELTA; lo = lo0 + dlo / TRILHA_ESC_DELTA;
    }
    la0 = la; lo0 = lo; usados++;
  }
  pkt[23] = usados;
  return o;
}
function trilhaDecodifica(pkt, len, entrega) {
  if (len < TRILHA_CAB_N) return { n: -1 };
  const epoca = pkt[16], base = getLE16(pkt, 17), ackAte = getLE16(pkt, 19), ackDe = getLE16(pkt, 21), n = pkt[23];
  if (n === 0) return { n: 0, epoca, base, ackAte, ackDe };
  if (len < TRILHA_PKT_N(n)) return { n: -1 };
  let o = TRILHA_CAB_N;
  let la = getLE32(pkt, o) / 1e7; o += 4;
  let lo = getLE32(pkt, o) / 1e7; o += 4;
  entrega(base, la, lo);
  for (let i = 1; i < n; i++) {
    const dla = i16(getLE16(pkt, o)); o += 2;
    const dlo = i16(getLE16(pkt, o)); o += 2;
    la += dla / TRILHA_ESC_DELTA; lo += dlo / TRILHA_ESC_DELTA;
    entrega(u16(base + i), la, lo);
  }
  return { n, epoca, base, ackAte, ackDe };
}

// ------------------------------------------------------------- o roteiro
let pass = 0, fail = 0;
const check = (c, nome) => { if (c) { pass++; console.log('PASS ' + nome); } else { fail++; console.log('FAIL ' + nome); } };

// O lider guarda so os ultimos TRAJETO_MAX pontos (o anel do core/trajeto.h).
function lider(epoca) {
  const L = { lat: [], lon: [], head: 0xFFFF, n: 0, epoca };
  L.guarda = () => Math.min(L.n, TRAJETO_MAX);
  L.oldest = () => L.n ? u16(L.head - u16(L.guarda() - 1)) : ROTA_SEQ_NENHUMA;
  L.anda = () => { const i = L.n++; L.lat[i] = -23.15 + i * 0.00004; L.lon[i] = -46.55 + i * 0.00003; L.head = u16(L.head + 1); };
  L.ponto = seq => { const atras = u16(L.head - seq); if (atras >= L.guarda()) return { la: 0, lo: 0 }; return { la: L.lat[L.n - 1 - atras], lo: L.lon[L.n - 1 - atras] }; };
  return L;
}
// Como o app/rede.h monta o pacote do LIDER: ackAte vazio e, no ackDe, o ponto
// mais antigo que ele ainda guarda (o piso de quem segue).
function pacoteLider(L, base, n) {
  const pkt = new Uint8Array(80);
  const len = trilhaCodifica(pkt, L.epoca, base, ROTA_SEQ_NENHUMA, L.oldest(), n, i => L.ponto(u16(base + i)));
  if (len !== TRILHA_PKT_N(pkt[23])) throw new Error('tamanho codificado inconsistente');
  return { pkt, len };
}
// Como o app/rede.h recebe um pacote do lider: pontos na rota e o piso.
function recebeDoLider(R, pkt, len) {
  let novos = 0;
  const ep = pkt[16];
  const d = trilhaDecodifica(pkt, len, (s, la, lo) => { if (rotaInsere(R, rotaSeqVirtual(R, ep, s), la, lo)) novos++; });
  if (d.n >= 0) rotaDefinePiso(R, ep, d.ackDe);
  return novos;
}
function quadro(L, R, st, q, entrega) {
  const { base, n, catchup } = trilhaEscolheBloco(L.head, L.guarda(), st.ack, st.ackDe, st.temAck, q, TRILHA_PONTOS_PKT);
  const { pkt, len } = pacoteLider(L, base, n);
  if (!entrega) return { novos: 0, catchup };
  const novos = recebeDoLider(R, pkt, len);
  // (o decodificador chama 'entrega' antes de devolver; a epoca e lida do pacote)
  st.ack = rotaAck16(R); st.ackDe = rotaAckDe16(R); st.temAck = true;
  return { novos, catchup };
}
// a epoca do pacote e conhecida antes da entrega: leitura direta do byte 16
let d0 = { epoca: 0 };
const quadroEp = (L, R, st, q, entrega) => { d0.epoca = L.epoca; return quadro(L, R, st, q, entrega); };
const continua = R => { for (let i = 1; i < R.n; i++) if (R.p[i].vseq !== R.p[i - 1].vseq + 1) return false; return true; };

console.log('=== catchup_test (espelho do firmware/mts/core/rota.h) ===');
check(TRILHA_PKT_N(0) === 24, 'pacote sem pontos = 24 B');
check(TRILHA_PKT_N(TRILHA_PONTOS_PKT) === 68, 'pacote cheio = 68 B');

{ // codec
  const pkt = new Uint8Array(80);
  const la = [-23.1, -23.1001, -23.1002, -23.5, -23.5001], lo = [-46.5, -46.5001, -46.5002, -46.5, -46.5001];
  const len = trilhaCodifica(pkt, 3, 100, 0xBEEF, 0x1234, 5, i => ({ la: la[i], lo: lo[i] }));
  let cnt = 0, ula = 0;
  const d = trilhaDecodifica(pkt, len, (s, a) => { cnt++; ula = a; });
  check(d.n === 3 && cnt === 3, 'delta que nao cabe em int16 corta o bloco (3 de 5)');
  check(d.epoca === 3 && d.base === 100 && d.ackAte === 0xBEEF && d.ackDe === 0x1234, 'epoca/base/acks atravessam o codec');
  check(Math.abs(ula - (-23.1002)) < 2e-5, 'ponto reconstruido por delta dentro de 2e-5 grau');
  check(len === TRILHA_PKT_N(3), 'tamanho codificado bate com TRILHA_PKT_N');
}
{ // sem perda
  const L = lider(7), R = rotaNova(), st = { ack: ROTA_SEQ_NENHUMA, ackDe: ROTA_SEQ_NENHUMA, temAck: false }; let catchups = 0;
  for (let q = 0; q < 40; q++) { for (let k = 0; k < 3; k++) L.anda(); if (quadroEp(L, R, st, q, true).catchup) catchups++; }
  check(continua(R), 'sem perda: rota continua');
  check(R.n === L.n, 'sem perda: rota tem todos os pontos do lider');
  check(catchups === 0, 'sem perda: nenhum quadro de catch-up');
}
{ // vao
  const L = lider(7), R = rotaNova(), st = { ack: ROTA_SEQ_NENHUMA, ackDe: ROTA_SEQ_NENHUMA, temAck: false }; let q = 0;
  for (; q < 10; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroEp(L, R, st, q, true); }
  for (; q < 30; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroEp(L, R, st, q, false); }
  for (let k = 0; k < 3; k++) L.anda(); quadroEp(L, R, st, q++, true);
  let salto = false; for (let i = 1; i < R.n; i++) if (rotaVaoAntes(R, i)) salto = true;
  check(salto, 'apos o vao: a rota registra o salto de numero');
  check(st.ack < L.head - 30, 'apos o vao: o ack aponta para antes do buraco');
  let catchups = 0, quadros = 0;
  while (!continua(R) && quadros < 60) { for (let k = 0; k < 3; k++) L.anda(); if (quadroEp(L, R, st, q++, true).catchup) catchups++; quadros++; }
  check(continua(R), 'catch-up fecha o buraco');
  for (let k = 0; k < 3; k++) L.anda(); quadroEp(L, R, st, q++, true);   // o quadro seguinte (recentes) traz os 3 novos
  check(R.n === L.n && continua(R), 'depois do catch-up a rota tem todos os pontos');
  check(catchups >= 5 && catchups <= 9, `60 pontos perdidos fecham em 5-9 quadros de catch-up (${catchups})`);
  console.log(`     (fechou em ${quadros} quadros, ${catchups} de catch-up)`);
}
{ // epoca
  const L = lider(7), R = rotaNova(), st = { ack: ROTA_SEQ_NENHUMA, ackDe: ROTA_SEQ_NENHUMA, temAck: false };
  for (let q = 0; q < 10; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroEp(L, R, st, q, true); }
  const antes = R.n;
  const L2 = lider(9);
  for (let q = 0; q < 5; q++) { for (let k = 0; k < 3; k++) L2.anda(); quadroEp(L2, R, st, q, true); }
  check(R.n === antes + L2.n, 'epoca nova: os pontos entram sem colidir com os velhos');
  check(R.p[R.n - 1].vseq > R.p[antes - 1].vseq, 'epoca nova: os pontos novos ficam no fim');
  // e o lider novo, vendo um ack de epoca velha, nao deve entrar em catch-up eterno
  let catchups = 0;
  for (let q = 5; q < 25; q++) { for (let k = 0; k < 3; k++) L2.anda(); if (quadroEp(L2, R, st, q, true).catchup) catchups++; }
  check(catchups <= 8, `epoca nova: o catch-up para quando a rota nova fica continua (${catchups})`);
}
{ // tardio
  const L = lider(7), R = rotaNova(), st = { ack: ROTA_SEQ_NENHUMA, ackDe: ROTA_SEQ_NENHUMA, temAck: false };
  for (let k = 0; k < 200; k++) L.anda();
  let q = 0; quadroEp(L, R, st, q++, true);
  check(R.n === TRILHA_PONTOS_PKT, 'tardio: o primeiro pacote traz os 10 mais recentes');
  check(st.ackDe === u16(L.head - 9) && st.ack === L.head, 'tardio: ackDe aponta o primeiro que tem, ackAte o head');
  let quadros = 0; while (R.n < L.n && quadros < 80) { quadroEp(L, R, st, q++, true); quadros++; }
  check(R.n === L.n && continua(R), 'tardio: o historico inteiro chega e fica continuo (de tras para a frente)');
  console.log(`     (200 pontos em ${quadros} quadros)`);
}
{ // ordem
  const R = rotaNova();
  rotaInsere(R, 10, 1, 1); rotaInsere(R, 12, 3, 3); rotaInsere(R, 11, 2, 2); rotaInsere(R, 11, 9, 9);
  check(R.n === 3 && R.p[1].vseq === 11 && R.p[1].lat === 2, 'insercao ordenada e repetido ignorado');
  check(R.prefixo === 2, 'prefixo continuo cobre os tres');
  check(Math.abs(R.km[2] - (R.km[1] + Math.sqrt(rotaD2(2, 2, 3, 3)))) < 1.0, 'km recalculado apos insercao no meio');
}
{ // rolagem do seq de 16 bits: o lider passa por 0xFFFF sem quebrar a ordem
  const L = lider(7), R = rotaNova(), st = { ack: ROTA_SEQ_NENHUMA, ackDe: ROTA_SEQ_NENHUMA, temAck: false };
  L.head = 0xFFF0; // como se ja tivesse andado muito (os pontos guardados sao so os recentes)
  for (let q = 0; q < 20; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroEp(L, R, st, q, true); }
  check(continua(R) && R.n === L.n, 'seq de 16 bits rola por 0xFFFF sem quebrar a rota');
}
{ // perda aleatoria de 50% (o campo de 09-28): a rota fica continua no fim
  let seed = 12345; const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF; return seed / 0x7FFFFFFF; };
  const L = lider(7), R = rotaNova(), st = { ack: ROTA_SEQ_NENHUMA, ackDe: ROTA_SEQ_NENHUMA, temAck: false }; let catchups = 0;
  for (let q = 0; q < 300; q++) { for (let k = 0; k < 3; k++) L.anda(); if (quadroEp(L, R, st, q, rnd() > 0.5).catchup) catchups++; }
  for (let q = 300; q < 340; q++) { for (let k = 0; k < 3; k++) L.anda(); if (quadroEp(L, R, st, q, true).catchup) catchups++; }
  check(continua(R) && R.n === L.n, `50% de perda por 300 quadros: tudo recuperado (${catchups} catch-ups)`);
}

// ================================================================ frota (frotaAckMin)
function carroNovo() { return { ativo: true, temAck: false, ackAte: 0, ackDe: 0, ackTopo: 0, ackMs: 0 }; }
function frotaAck(c, ackAte, ackDe, topo, agoraMs) {
  c.temAck = true; c.ackAte = ackAte; c.ackDe = ackDe; c.ackTopo = topo; c.ackMs = agoraMs;
}
function frotaAckMin(carros, head, agoraMs) {
  let tem = false, temDe = false, m = 0, de = 0, topoMin = ROTA_SEQ_NENHUMA;
  for (const c of carros) {
    if (!c.ativo || !c.temAck || (agoraMs - c.ackMs) > ROTA_ACK_TTL_MS) continue;
    if (c.ackTopo === ROTA_SEQ_NENHUMA || i16(c.ackTopo - head) < -ROTA_OUVE_PONTOS) continue;
    if (topoMin === ROTA_SEQ_NENHUMA || i16(c.ackTopo - topoMin) < 0) topoMin = c.ackTopo;
    if (!tem) { m = c.ackAte; tem = true; }
    else if (c.ackAte === ROTA_SEQ_NENHUMA || m === ROTA_SEQ_NENHUMA) m = ROTA_SEQ_NENHUMA;
    else if (i16(c.ackAte - m) < 0) m = c.ackAte;
    if (c.ackDe !== ROTA_SEQ_NENHUMA) {
      if (!temDe) { de = c.ackDe; temDe = true; }
      else if (i16(c.ackDe - de) > 0) de = c.ackDe;
    }
  }
  return { tem, ackAteMin: m, ackDeMax: temDe ? de : ROTA_SEQ_NENHUMA, topoMin };
}
function rotaTopo16(r) {
  if (!r.temEpoca || r.n === 0) return ROTA_SEQ_NENHUMA;
  const v = r.p[r.n - 1].vseq;
  if (v < r.base) return ROTA_SEQ_NENHUMA;
  const s = u16(v - r.base);
  return s === ROTA_SEQ_NENHUMA ? 0xFFFE : s;
}
// Um quadro da REDE INTEIRA: o lider escolhe o bloco com os acks que ouviu, cada
// seguidor recebe (ou nao) e manda o ack de volta (o lider sempre ouve).
function quadroRede(L, segs, q, ouve) {
  const agora = q * TDMA_FRAME_MS;
  const a = frotaAckMin(segs.map(s => s.carro), L.head, agora);
  const producao = L.headUlt === undefined ? 0 : i16(L.head - L.headUlt);
  L.headUlt = L.head;
  const { base, n, catchup } = trilhaEscolheBloco(L.head, L.guarda(), a.ackAteMin, a.ackDeMax, a.tem, q, TRILHA_PONTOS_PKT, a.topoMin, producao);
  const { pkt, len } = pacoteLider(L, base, n);
  segs.forEach((s, i) => {
    if (ouve(i, q)) recebeDoLider(s.R, pkt, len);
    frotaAck(s.carro, rotaAck16(s.R), rotaAckDe16(s.R), rotaTopo16(s.R), agora);
  });
  return catchup;
}
const emDia = (R, L) => rotaAck16(R) === L.head;
const seguidor = () => ({ R: rotaNova(), carro: carroNovo() });

console.log('\n--- cenarios da revisao de 2026-09-29 (travas do catch-up) ---');
{ // A) o LIDER REINICIA com um vao do seguidor ainda aberto
  const L = lider(7), S = seguidor(); let q = 0;
  const vivo = (i, qq) => !(qq >= 10 && qq < 20);                 // 10 quadros surdos
  for (; q < 25; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q, vivo); }
  // o lider reinicia ANTES de o vao fechar: epoca nova, numeracao do zero
  const L2 = lider(8);
  for (; q < 200; q++) { for (let k = 0; k < 3; k++) L2.anda(); quadroRede(L2, [S], q, (i, qq) => !(qq >= 60 && qq < 70)); }
  check(emDia(S.R, L2), `A) lider reiniciou com vao aberto: o ack alcanca o head do lider novo (ack ${rotaAck16(S.R)}, head ${L2.head})`);
  let vaosNovos = 0; for (let i = 1; i < S.R.n; i++) if (S.R.p[i].vseq >= S.R.base && rotaVaoAntes(S.R, i)) vaosNovos++;
  check(vaosNovos === 0, `A) e o vao que abriu DEPOIS do reinicio (quadros 60-69) fechou (${vaosNovos} vaos na epoca nova)`);
}
{ // B) vao MAIOR que o buffer do lider (o que falta nao existe mais)
  const L = lider(7), S = seguidor(); let q = 0;
  const surdo = Math.ceil((TRAJETO_MAX + 150) / 3);                // mais que TRAJETO_MAX pontos perdidos
  for (; q < 10; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q, () => true); }
  for (; q < 10 + surdo; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q, () => false); }
  const fimVao = q;
  for (; q < fimVao + 400; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q, (i, qq) => !(qq >= fimVao + 250 && qq < fimVao + 260)); }
  check(S.R.temPiso, 'B) o seguidor aprendeu o piso do lider');
  check(emDia(S.R, L), `B) vao alem do buffer: o ack passa por cima do que nao existe mais e alcanca o head`);
  let abertos = 0; for (let i = 1; i < S.R.n; i++) if (rotaVaoAntes(S.R, i) && S.R.p[i].vseq > S.R.pisoV) abertos++;
  check(abertos === 0, `B) todo vao acima do piso fechou, inclusive o que abriu depois (${abertos} abertos)`);
}
{ // C) um seguidor SURDO (nunca ouve o lider) nao trava o catch-up dos outros
  const L = lider(7), bom = seguidor(), surdo = seguidor(); let q = 0;
  const ouve = (i, qq) => i === 0 && !(qq >= 20 && qq < 40);       // o 0 perde 20 quadros; o 1 nunca ouve
  for (; q < 40; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [bom, surdo], q, ouve); }
  let quadros = 0;
  while (!emDia(bom.R, L) && quadros < 200) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [bom, surdo], q++, ouve); quadros++; }
  check(emDia(bom.R, L), `C) com um seguidor surdo na frota, o vao do outro fecha (${quadros} quadros)`);
  check(quadros <= 60, `C) e fecha em tempo util: ${quadros} quadros de 6 s (antes: nunca)`);
  const a = frotaAckMin([bom.carro, surdo.carro], L.head, q * TDMA_FRAME_MS);
  check(a.tem && a.ackAteMin !== ROTA_SEQ_NENHUMA, 'C) o surdo (nao ouve o lider) nao entra na conta do bloco');
}
{ // C2) quem ficou surdo um tempo e volta a ouvir entra na conta DE NOVO, ja no primeiro pacote
  const L = lider(7), S = seguidor(); let q = 0;
  for (; q < 10; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q, () => true); }
  for (; q < 40; q++) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q, () => false); }
  check(!frotaAckMin([S.carro], L.head, q * TDMA_FRAME_MS).tem, 'C2) surdo por 30 quadros: fora da conta');
  for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q++, () => true);
  check(frotaAckMin([S.carro], L.head, q * TDMA_FRAME_MS).tem, 'C2) um pacote ouvido e ele volta para a conta');
  let quadros = 0; while (!emDia(S.R, L) && quadros < 120) { for (let k = 0; k < 3; k++) L.anda(); quadroRede(L, [S], q++, () => true); quadros++; }
  check(emDia(S.R, L), `C2) e o vao de 90 pontos dele fecha (${quadros} quadros)`);
}
{ // D) rota cheia: km[] anda junto com os pontos
  const R = rotaNova();
  for (let i = 0; i < ROTA_MAX + 300; i++) rotaInsere(R, i, -23.15 + i * 0.00009, -46.55);
  let pior = 0, soma = 0;
  for (let i = 1; i < R.n; i++) {
    soma += Math.sqrt(rotaD2(R.p[i - 1].lat, R.p[i - 1].lon, R.p[i].lat, R.p[i].lon));
    pior = Math.max(pior, Math.abs(R.km[i] - soma));
  }
  check(R.n === ROTA_MAX && R.km[0] === 0, 'D) rota cheia: km[0] = 0 no novo primeiro ponto');
  check(pior < 1.0, `D) rota cheia: km[i] = distancia real desde p[0] (pior erro ${pior.toFixed(3)} m)`);
}

// ================================================================ fidelidade ao C
// O espelho so vale se o C disser o mesmo. Estas sao as linhas que decidem.
console.log('\n--- fidelidade: o firmware/mts diz o mesmo que este espelho ---');
{
  const C = RC('firmware/mts/core/rota.h').replace(/\s+/g, ' ');
  const F = RC('firmware/mts/core/frota.h').replace(/\s+/g, ' ');
  const N = RC('firmware/mts/app/rede.h').replace(/\s+/g, ' ');
  check(C.includes('(r.temPiso && r.p[i + 1].vseq <= r.pisoV)'), 'rota.h: o prefixo passa por cima do vao abaixo do piso');
  check(C.includes('r.temPiso = false; // o piso era da epoca velha'), 'rota.h: epoca nova zera o piso');
  check(C.includes('memmove(r.km, r.km + 1, (ROTA_MAX - 1) * sizeof(float));') && C.includes('r.km[j] -= k0;'), 'rota.h: km[] anda e recomeca do novo p[0]');
  check(C.includes('inline void rotaDefinePiso(Rota& r, uint8_t epoca, uint16_t oldest16)'), 'rota.h: rotaDefinePiso existe');
  check(F.includes('if (c.ackTopo == ROTA_SEQ_NENHUMA || (int16_t)(c.ackTopo - head) < -ROTA_OUVE_PONTOS) continue;'), 'frota.h: quem nao ouve o lider sai da conta');
  check(N.includes('base = rotaTopo16(g_rotaC);'), 'rede.h: o seguidor manda o topo no campo base');
  check(N.includes('frotaAck(g_frota, idx, ackAte, ackDe, base, rxMs);'), 'rede.h: o lider guarda o topo de cada seguidor');
  check(N.includes('frotaAckMin(g_frota, g_traj.seqUlt, ackAteMin, ackDeMax, millis(), topoMin)'), 'rede.h: o lider compara o topo com o proprio head');
  check(C.includes('else catchup = atras && (frente + producao <= maxPontos ||'), 'rota.h: preenchimento com folga na frente');
  check(C.includes('((quadro & 3) == 1 && producao < maxPontos && frente + producao <= 2 * maxPontos));'), 'rota.h: ou 1 em 4 com sobra ao longo do tempo');
  check(C.includes('if (frente > 0 && frente <= 2 * maxPontos) {'), 'rota.h: bloco da frente contiguo ao topo');
  check(N.includes('if (lider) rotaDefinePiso(g_rotaC, epoca, ackDe);'), 'rede.h: o seguidor aplica o piso do pacote do lider');
  check(N.includes('ackDe = g_traj.n > 0 ? (uint16_t)(g_traj.seqUlt - (uint16_t)(g_traj.n - 1)) : ROTA_SEQ_NENHUMA;'), 'rede.h: o lider anuncia o ponto mais antigo que guarda');
}

console.log(`\n${pass} PASS, ${fail} FAIL`);
process.exit(fail ? 1 : 0);
