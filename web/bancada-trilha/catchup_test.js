// Espelho em Node do rota_core.h (firmware/mts_campo) e o mesmo roteiro do
// firmware/catchup_selftest - roda no PC, sem placa:
//
//   node web\bancada-trilha\catchup_test.js
//
// POR QUE UM ESPELHO: nao ha g++ de host nesta maquina e o selftest de placa so
// roda com hardware. O codigo abaixo segue o C++ linha a linha (mesmos nomes,
// mesma aritmetica de 16 bits) para um bug de logica aparecer aqui antes de ir
// para a trilha. Se o rota_core.h mudar, este arquivo muda junto.
'use strict';

const TRILHA_PONTOS_PKT = 10;
const ROTA_MAX = 1400;
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
function rotaNova() { return { p: [], km: [], n: 0, temEpoca: false, epoca: 0, base: 0, ultVseq: 0, ultSeq16: 0, prefixo: -1, inseridos: 0, repetidos: 0, fora: 0 }; }
function rotaRefazPrefixo(r, desde) {
  if (r.n === 0) { r.prefixo = -1; return; }
  let i = desde < 0 ? 0 : desde;
  if (i > r.prefixo + 1) i = r.prefixo + 1;
  if (i < 0) i = 0;
  while (i + 1 < r.n && r.p[i + 1].vseq === r.p[i].vseq + 1) i++;
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
    r.p.shift(); r.km.shift(); r.n--; pos--; r.prefixo = -1;
  }
  r.p.splice(pos, 0, { lat, lon, vseq }); r.km.splice(pos, 0, 0);
  r.n++; r.inseridos++;
  rotaRefazKm(r, pos);
  if (r.prefixo < 0 || pos <= r.prefixo + 1) rotaRefazPrefixo(r, pos - 1);
  return true;
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

function trilhaEscolheBloco(head, trajN, ackAteMin, ackDeMax, temAck, quadro, maxPontos) {
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
  const catchup = atras && ((quadro & 1) === 1);
  let base, n;
  if (catchup) {
    base = inicioCatch;
    const disp = u16(head - base) + 1;
    n = disp < nCatch ? disp : nCatch;
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

function lider(epoca) {
  const L = { lat: [], lon: [], head: 0xFFFF, n: 0, epoca };
  L.anda = () => { const i = L.n++; L.lat[i] = -23.15 + i * 0.00004; L.lon[i] = -46.55 + i * 0.00003; L.head = u16(L.head + 1); };
  L.ponto = seq => { const atras = u16(L.head - seq); if (atras >= L.n) return { la: 0, lo: 0 }; return { la: L.lat[L.n - 1 - atras], lo: L.lon[L.n - 1 - atras] }; };
  return L;
}
function quadro(L, R, st, q, entrega) {
  const pkt = new Uint8Array(80);
  const { base, n, catchup } = trilhaEscolheBloco(L.head, L.n, st.ack, st.ackDe, st.temAck, q, TRILHA_PONTOS_PKT);
  const len = trilhaCodifica(pkt, L.epoca, base, 0, 0, n, i => L.ponto(u16(base + i)));
  if (len !== TRILHA_PKT_N(pkt[23])) throw new Error('tamanho codificado inconsistente');
  if (!entrega) return { novos: 0, catchup };
  let novos = 0;
  const d = trilhaDecodifica(pkt, len, (s, la, lo) => { if (rotaInsere(R, rotaSeqVirtual(R, d0.epoca, s), la, lo)) novos++; });
  // (o decodificador chama 'entrega' antes de devolver; a epoca e lida do pacote)
  st.ack = rotaAck16(R); st.ackDe = rotaAckDe16(R); st.temAck = true;
  return { novos, catchup };
}
// a epoca do pacote e conhecida antes da entrega: leitura direta do byte 16
let d0 = { epoca: 0 };
const quadroEp = (L, R, st, q, entrega) => { d0.epoca = L.epoca; return quadro(L, R, st, q, entrega); };
const continua = R => { for (let i = 1; i < R.n; i++) if (R.p[i].vseq !== R.p[i - 1].vseq + 1) return false; return true; };

console.log('=== catchup_test (espelho do rota_core.h) ===');
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

console.log(`\n${pass} PASS, ${fail} FAIL`);
process.exit(fail ? 1 : 0);
