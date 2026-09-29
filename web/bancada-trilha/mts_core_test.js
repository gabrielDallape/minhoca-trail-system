// Espelho em Node do core do firmware/mts (o firmware profissional da tela P4) e
// AUDITORIA do proprio fonte: le firmware/mts/core/*.h, hal/radio.h e app/*.h e
// confere que as contas fecham e que as armadilhas do radio continuam no lugar.
//
//   node web\bancada-trilha\mts_core_test.js
//
// POR QUE UM ESPELHO: nao ha g++ de host nesta maquina; o firmware/mts_selftest
// roda as mesmas contas na placa. O que muda em core/*.h muda aqui junto - e se
// nao mudar, a auditoria da secao [0] acusa (ela le os numeros do .h de verdade).
'use strict';
const fs = require('fs');
const path = require('path');
const R = p => fs.readFileSync(path.join(__dirname, '..', '..', p), 'utf8');

let pass = 0, fail = 0;
const ck = (c, nome, info = '') => { if (c) { pass++; console.log('PASS ' + nome); } else { fail++; console.log('FAIL ' + nome + (info ? '   ' + info : '')); } };
const sec = t => console.log('\n-- ' + t);

// ---------------------------------------------------------------- [0] params.h
sec('[0] params.h: as constantes e as contas que dependem delas');
const P = R('firmware/mts/core/params.h');
const def = nome => {
  const m = P.match(new RegExp('#define\\s+' + nome + '\\s+([^\\n/]+)'));
  if (!m) return NaN;
  let v = m[1].trim().replace(/[UL]+$/i, '').replace(/f$/, '');
  if (/^\(/.test(v)) return NaN;         // expressao: calculada abaixo
  if (/^[A-Z_]+$/.test(v)) return def(v);   // alias de outra constante (RUIDO_SLOT_INI = FROTA_MAX_CARROS)
  return parseFloat(v);
};
const TDMA_SLOTS = def('TDMA_SLOTS'), TDMA_FRAME_S = def('TDMA_FRAME_S'), TDMA_GUARD_US = def('TDMA_GUARD_US');
const RF_SF = def('RF_SF'), RF_CR = def('RF_CR'), RF_PRE = def('RF_PRE'), RF_BW_KHZ = def('RF_BW_KHZ');
const FROTA_MAX_CARROS = def('FROTA_MAX_CARROS'), RUIDO_SLOT_INI = def('RUIDO_SLOT_INI');
const ROTA_PASSO_M = def('ROTA_PASSO_M'), TRILHA_PONTOS_PKT = def('TRILHA_PONTOS_PKT'), VEL_PROJETO_KMH = def('VEL_PROJETO_KMH');
const FROTA_EXTRAPOLA_MAX_MS = def('FROTA_EXTRAPOLA_MAX_MS'), FROTA_SINAL_VELHO_MS = def('FROTA_SINAL_VELHO_MS'), FROTA_SOME_MS = def('FROTA_SOME_MS');
const ROSTER_CADA = def('ROSTER_CADA'), PROTO_VERSAO = def('PROTO_VERSAO'), MTS_VERSAO = def('MTS_VERSAO'), CFG_ESQUEMA = def('CFG_ESQUEMA');
const RF_PWR_PADRAO = def('RF_PWR_PADRAO'), RADIO_PWR_SEGURO = def('RADIO_PWR_SEGURO'), RADIO_SEGURO_APOS = def('RADIO_SEGURO_APOS');
ck([TDMA_SLOTS, TDMA_FRAME_S, TDMA_GUARD_US, RF_SF, RF_CR, RF_PRE, RF_BW_KHZ, FROTA_MAX_CARROS, ROTA_PASSO_M, TRILHA_PONTOS_PKT,
    FROTA_EXTRAPOLA_MAX_MS, FROTA_SINAL_VELHO_MS, FROTA_SOME_MS, ROSTER_CADA, PROTO_VERSAO, MTS_VERSAO, CFG_ESQUEMA].every(Number.isFinite),
   'todas as constantes foram lidas do params.h');
ck(TDMA_FRAME_S === 6 && TDMA_SLOTS === 12, `quadro de 6 s com 12 slots (decisao do usuario): ${TDMA_FRAME_S} s x ${TDMA_SLOTS}`);
const SLOT_US = TDMA_FRAME_S * 1e6 / TDMA_SLOTS;
ck(SLOT_US === 500000, 'slot = 500 ms');
ck(18 % TDMA_FRAME_S === 0, 'o quadro divide 18 (grade GPS x UTC)');
ck(86400 % TDMA_FRAME_S === 0, 'o quadro divide 86400 (meia-noite)');
ck(FROTA_MAX_CARROS <= TDMA_SLOTS && RUIDO_SLOT_INI === FROTA_MAX_CARROS && RUIDO_SLOT_INI < TDMA_SLOTS,
   `slots ${RUIDO_SLOT_INI}..${TDMA_SLOTS - 1} sempre vazios para o ruido`);
ck(MTS_VERSAO > 101 && PROTO_VERSAO === 2 && CFG_ESQUEMA === 2, `fw v${MTS_VERSAO} > 101, proto v${PROTO_VERSAO}, cfg esquema ${CFG_ESQUEMA}`);
ck(RADIO_PWR_SEGURO <= RF_PWR_PADRAO && RADIO_SEGURO_APOS >= 2, 'modo seguro: teto abaixo do padrao, so apos >= 2 reinicios');

// air-time: espelho de core/airtime.h (formula do datasheet + 6,5 ms medidos)
const AIRTIME_EXTRA_US = 6500;
const simboloUs = () => Math.floor((2 ** RF_SF) * 1e6 / Math.floor(RF_BW_KHZ * 1000));
const ldro = () => simboloUs() >= 16380;
const sfEf = () => RF_SF - (ldro() ? 2 : 0);
const simbolosCarga = b => { const num = 8 * b - 4 * RF_SF + 28 + 16; return 8 + (num <= 0 ? 0 : Math.floor((num + 4 * sfEf() - 1) / (4 * sfEf()))) * RF_CR; };
const airtimeUs = b => Math.floor((4 * RF_PRE + 17) * simboloUs() / 4) + simbolosCarga(b) * simboloUs() + AIRTIME_EXTRA_US;
const PKT_MAX = 24 + 8 + (TRILHA_PONTOS_PKT - 1) * 4;
ck(PKT_MAX === 68, `pacote maximo = ${PKT_MAX} B`);
ck(airtimeUs(16) > 160000 && airtimeUs(16) < 180000, `air-time 16 B = ${airtimeUs(16)} us (~171 ms)`);
ck(airtimeUs(68) > 400000 && airtimeUs(68) < 430000, `air-time 68 B = ${airtimeUs(68)} us (~417 ms)`);
ck(airtimeUs(23) < 250000, `air-time do roster (23 B) = ${airtimeUs(23)} us`);
const janela = SLOT_US - TDMA_GUARD_US - airtimeUs(PKT_MAX);
ck(janela >= 60000, `janela para o laco decidir o TX = ${janela} us (>= 60 ms)`);
ck(airtimeUs(PKT_MAX) + TDMA_GUARD_US + 40000 <= SLOT_US, 'o maior pacote cabe no slot com 40 ms de folga (static_assert do rede.h)');
const VIGIA_MS = Math.floor(airtimeUs(PKT_MAX) / 1000) + 200;
ck(VIGIA_MS > airtimeUs(PKT_MAX) / 1000 && VIGIA_MS < SLOT_US / 1000 + 200, `vigia de TX preso = ${VIGIA_MS} ms (> maior pacote, < slot)`);
// convergencia do catch-up (o motivo do passo de 10 m)
const ptsPorS = (VEL_PROJETO_KMH / 3.6) / ROTA_PASSO_M, capPorS = TRILHA_PONTOS_PKT / (2 * TDMA_FRAME_S);
ck(ptsPorS <= capPorS + 1e-9, `catch-up converge a ${VEL_PROJETO_KMH} km/h: ${ptsPorS.toFixed(3)} pt/s <= ${capPorS.toFixed(3)} pt/s (passo ${ROTA_PASSO_M} m)`);
ck((VEL_PROJETO_KMH / 3.6) / 5.0 > capPorS, 'com passo de 5 m o catch-up NAO convergiria (por isso 10 m)');
// TTLs
ck(FROTA_EXTRAPOLA_MAX_MS === 2 * TDMA_FRAME_S * 1000, `extrapola no maximo 2 quadros (${FROTA_EXTRAPOLA_MAX_MS} ms)`);
ck(FROTA_SINAL_VELHO_MS >= 3 * TDMA_FRAME_S * 1000 && FROTA_SINAL_VELHO_MS < FROTA_SOME_MS, `sinal velho aos ${FROTA_SINAL_VELHO_MS / 1000} s, some aos ${FROTA_SOME_MS / 1000} s`);
const pPerda = 0.5, kVelho = FROTA_SINAL_VELHO_MS / (TDMA_FRAME_S * 1000);
ck(Math.pow(pPerda, kVelho) < 0.05, `a 50 % de perda, falso "sem sinal" em ${(100 * Math.pow(pPerda, kVelho)).toFixed(1)} % dos casos (< 5 %)`);
// holdover x roster intercalado
const holdoverTtlUs = 2 * TDMA_FRAME_S * 1e6 + 500000;
const souRoster = f => (f === 1 || f === 3 || f === 5) || (f > 5 && f % ROSTER_CADA === 0);
let maxSilencio = 0, corrida = 0;
for (let f = 0; f < 200; f++) { if (souRoster(f)) { corrida++; if (corrida > maxSilencio) maxSilencio = corrida; } else corrida = 0; }
ck(!souRoster(0), 'a primeira fala e POSICAO (roster nao ancora ninguem)');
ck(maxSilencio === 1, `nunca dois rosters seguidos: maior silencio de beacon = ${maxSilencio} quadro`);
ck(maxSilencio * TDMA_FRAME_S * 1e6 < holdoverTtlUs, `${maxSilencio} quadro sem beacon (${maxSilencio * TDMA_FRAME_S} s) < TTL de holdover (${holdoverTtlUs / 1e6} s)`);
ck(fs.existsSync(path.join(__dirname, '..', '..', 'firmware', 'mts', 'app', 'rede.h')) && /f === 1 \|\| f === 3 \|\| f === 5/.test(R('firmware/mts/app/rede.h').replace(/==/g, '===')),
   'rede.h usa a mesma regra de roster intercalado deste espelho');
// eleicao: espera do maior slot sem GPS
ck(3 + (FROTA_MAX_CARROS - 1) * TDMA_FRAME_S <= 60, `slot ${FROTA_MAX_CARROS - 1} assume a ancora em ${3 + (FROTA_MAX_CARROS - 1) * TDMA_FRAME_S} s sem ouvir ninguem (<= 60 s)`);

// ---------------------------------------------------------------- [1] proto.h
sec('[1] proto.h: versao, sala de 16 bits, pack/unpack');
const u16 = v => v & 0xFFFF;
const salaHash = cod => { let h = 2166136261 >>> 0; for (const c of cod) { h = (h ^ c.charCodeAt(0)) >>> 0; h = Math.imul(h, 16777619) >>> 0; } return ((h ^ (h >>> 16)) & 0xFFFF); };
const FL = { FIX: 1, ALERTA: 2, LIDER: 4, PPS: 8, TRILHA: 0x40, ROSTER: 0x80 };
const putLE32 = (p, o, v) => { v |= 0; p[o] = v & 255; p[o + 1] = (v >> 8) & 255; p[o + 2] = (v >> 16) & 255; p[o + 3] = (v >>> 24) & 255; };
const getLE32 = (p, o) => (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | (p[o + 3] << 24)) | 0;
function packPos(p, k) {
  p[0] = PROTO_VERSAO; p[1] = k.sala & 255; p[2] = (k.sala >> 8) & 255; p[3] = k.slot; p[4] = k.seq; p[5] = k.flags & ~FL.ROSTER;
  putLE32(p, 6, Math.trunc(k.lat * 1e7)); putLE32(p, 10, Math.trunc(k.lon * 1e7));
  let h = k.rumo; while (h < 0) h += 360; while (h >= 360) h -= 360; p[14] = Math.floor(h / 2); p[15] = k.vel; return 16;
}
function unpackPos(p, len) {
  if (len < 16 || p[0] !== PROTO_VERSAO) return null;
  if (p[5] & FL.ROSTER) return null;
  const k = { sala: p[1] | (p[2] << 8), slot: p[3], seq: p[4], flags: p[5], lat: getLE32(p, 6) / 1e7, lon: getLE32(p, 10) / 1e7, rumo: p[14] * 2, vel: p[15] };
  if (k.slot >= FROTA_MAX_CARROS) return null;
  return k;
}
{
  const p = new Uint8Array(70);
  const k = { sala: salaHash('12345'), slot: 3, seq: 200, flags: FL.FIX | FL.LIDER | FL.TRILHA, lat: -23.123456, lon: -46.654321, rumo: 271, vel: 37 };
  packPos(p, k);
  const d = unpackPos(p, 16);
  ck(d && d.sala === k.sala && d.slot === 3 && d.seq === 200 && d.flags === k.flags, 'cabecalho atravessa o pack/unpack');
  ck(d && Math.abs(d.lat - k.lat) < 2e-7 && Math.abs(d.lon - k.lon) < 2e-7, 'lat/lon em 1e-7 grau');
  ck(d && d.rumo === 270 && d.vel === 37, 'rumo em graus/2 e velocidade em km/h');
  ck(p[0] === PROTO_VERSAO, 'byte 0 = versao do protocolo');
  // um pacote v1 (mts_campo v101): byte 0 era o hash da sala - qualquer valor
  let rejeitados = 0;
  for (let b0 = 0; b0 < 256; b0++) { p[0] = b0; if (!unpackPos(p, 16)) rejeitados++; }
  ck(rejeitados === 255, 'todo byte 0 diferente da versao e rejeitado (255 de 256)');
  p[0] = PROTO_VERSAO; p[3] = FROTA_MAX_CARROS;
  ck(!unpackPos(p, 16), `slot >= ${FROTA_MAX_CARROS} e rejeitado`);
  p[3] = 1; ck(!unpackPos(p, 15), 'pacote curto e rejeitado');
  p[5] |= FL.ROSTER; ck(!unpackPos(p, 23), 'roster nao passa por unpackPos');
  ck(salaHash('12345') !== salaHash('12346') && salaHash('12345') <= 0xFFFF, 'hash da sala tem 16 bits e distingue codigos vizinhos');
  // colisao entre 1000 codigos aleatorios de 5 digitos
  const vistos = new Set(); let col = 0;
  for (let i = 0; i < 1000; i++) { const c = String(10000 + Math.floor(Math.random() * 90000)); const h = salaHash(c); if (vistos.has(h)) col++; vistos.add(h); }
  ck(col < 20, `poucas colisoes de hash em 1000 codigos (${col})`);
}

// ---------------------------------------------------------------- [2] geo.h
sec('[2] geo.h: rumo e extrapolacao');
const R_TERRA = 6371000, MPG = 111320;
const haversine = (la1, lo1, la2, lo2) => { const r = Math.PI / 180, dla = (la2 - la1) * r, dlo = (lo2 - lo1) * r; const a = Math.sin(dla / 2) ** 2 + Math.cos(la1 * r) * Math.cos(la2 * r) * Math.sin(dlo / 2) ** 2; return 2 * R_TERRA * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a)); };
const rumo = (la1, lo1, la2, lo2) => { const r = Math.PI / 180, dlo = (lo2 - lo1) * r; const y = Math.sin(dlo) * Math.cos(la2 * r), x = Math.cos(la1 * r) * Math.sin(la2 * r) - Math.sin(la1 * r) * Math.cos(la2 * r) * Math.cos(dlo); let b = Math.atan2(y, x) / r; if (b < 0) b += 360; return b; };
const avanca = (lat, lon, rg, m) => { const r = rg * Math.PI / 180; return { lat: lat + m * Math.cos(r) / MPG, lon: lon + m * Math.sin(r) / (MPG * Math.cos(lat * Math.PI / 180)) }; };
{
  const a = avanca(-23.5, -46.6, 90, 100);
  ck(Math.abs(haversine(-23.5, -46.6, a.lat, a.lon) - 100) < 0.5, 'avancar 100 m para leste = 100 m de haversine');
  ck(Math.abs(rumo(-23.5, -46.6, a.lat, a.lon) - 90) < 0.5, 'e o rumo de volta e 90');
  const n = avanca(-23.5, -46.6, 0, 250);
  ck(Math.abs(n.lat - (-23.5 + 250 / MPG)) < 1e-9 && Math.abs(rumo(-23.5, -46.6, n.lat, n.lon)) < 0.01, 'norte: so a latitude muda');
}

// ---------------------------------------------------------------- [3] frota.h
sec('[3] frota.h: TTLs e extrapolacao com o relogio injetado');
const SINAL = { NUNCA: 0, VIVO: 1, VELHO: 2, SUMIU: 3 };
function frotaNova() { return { c: Array.from({ length: FROTA_MAX_CARROS }, () => ({ ativo: false })), conflitos: 0, versaoErrada: 0 }; }
function frotaIdent(f, nome, cor, slot) { f.c[0] = { ativo: true, slot, cor, nome, temNome: true, lider: false, alerta: false }; }
function frotaAcha(f, slot) { for (let i = 1; i < FROTA_MAX_CARROS; i++) if (f.c[i].ativo && f.c[i].slot === slot) return i; return -1; }
function frotaVaga(f, slot, ms) { for (let i = 1; i < FROTA_MAX_CARROS; i++) if (!f.c[i].ativo) { f.c[i] = { ativo: true, slot, cor: slot % 8, nome: 'CARRO ' + slot, temNome: false, ultimoMs: ms, temPos: false, alerta: false, lider: false }; return i; } return -1; }
function frotaRecebePos(f, k, ms) {
  if (k.slot >= FROTA_MAX_CARROS) return -3;
  if (k.slot === f.c[0].slot) { f.conflitos++; return -2; }
  let i = frotaAcha(f, k.slot); if (i < 0) i = frotaVaga(f, k.slot, ms); if (i < 0) return -1;
  const c = f.c[i]; c.lat = k.lat; c.lon = k.lon; c.fix = !!(k.flags & FL.FIX); c.rumo = k.rumo; c.vel = k.vel; c.temPos = true; c.posMs = ms;
  c.alerta = !!(k.flags & FL.ALERTA); c.lider = !!(k.flags & FL.LIDER); c.ultimoMs = ms; return i;
}
function frotaRecebeRoster(f, slot, cor, lider, nome, ms) {
  if (slot === f.c[0].slot) { f.conflitos++; return -2; }
  let i = frotaAcha(f, slot); if (i < 0) i = frotaVaga(f, slot, ms); if (i < 0) return -1;
  const c = f.c[i]; c.nome = nome; c.temNome = true; c.lider = lider; if (cor < 8) c.cor = cor; return i;   // NAO carimba ultimoMs
}
function frotaSinal(c, ms) { if (!c.ativo || !c.temPos) return SINAL.NUNCA; const q = ms - c.posMs; if (q > FROTA_SOME_MS) return SINAL.SUMIU; if (q > FROTA_SINAL_VELHO_MS) return SINAL.VELHO; return SINAL.VIVO; }
function frotaEnvelhece(f, ms) { let s = 0; for (let i = 1; i < FROTA_MAX_CARROS; i++) { const c = f.c[i]; if (!c.ativo) continue; if (ms - c.ultimoMs > FROTA_SOME_MS) { f.c[i] = { ativo: false }; s++; } } return s; }
function frotaEstimativa(c, ms) {
  if (!c.ativo || !c.temPos || !c.fix) return null;
  let dt = ms - c.posMs; if (dt > FROTA_EXTRAPOLA_MAX_MS) dt = FROTA_EXTRAPOLA_MAX_MS;
  if (c.vel === 0 || dt === 0) return { lat: c.lat, lon: c.lon, ext: false };
  const m = c.vel / 3.6 * dt / 1000; const a = avanca(c.lat, c.lon, c.rumo, m); return { lat: a.lat, lon: a.lon, ext: true };
}
{
  const f = frotaNova(); frotaIdent(f, 'eu', 0, 0);
  const k = { sala: 1, slot: 2, seq: 1, flags: FL.FIX | FL.LIDER, lat: -23.5, lon: -46.6, rumo: 90, vel: 36 };
  const i = frotaRecebePos(f, k, 1000);
  ck(i === 1 && f.c[1].slot === 2 && f.c[1].nome === 'CARRO 2' && f.c[1].lider, 'primeiro pacote cria o carro com rotulo provisorio e papel');
  ck(frotaRecebePos(f, { ...k, slot: 0 }, 1000) === -2 && f.conflitos === 1, 'pacote com o MEU slot = conflito');
  const e6 = frotaEstimativa(f.c[1], 7000);
  ck(e6.ext && Math.abs(haversine(k.lat, k.lon, e6.lat, e6.lon) - 60) < 0.5, '6 s depois, a 36 km/h: 60 m a frente pelo rumo');
  const e30 = frotaEstimativa(f.c[1], 31000);
  ck(Math.abs(haversine(k.lat, k.lon, e30.lat, e30.lon) - 120) < 0.5, `30 s depois: congela em 120 m (teto de ${FROTA_EXTRAPOLA_MAX_MS / 1000} s)`);
  ck(frotaSinal(f.c[1], 1000 + FROTA_SINAL_VELHO_MS) === SINAL.VIVO && frotaSinal(f.c[1], 1001 + FROTA_SINAL_VELHO_MS) === SINAL.VELHO, `VELHO logo apos ${FROTA_SINAL_VELHO_MS / 1000} s`);
  ck(frotaSinal(f.c[1], 1001 + FROTA_SOME_MS) === SINAL.SUMIU, `SUMIU logo apos ${FROTA_SOME_MS / 1000} s`);
  frotaRecebeRoster(f, 2, 5, true, 'Gino', 1000 + FROTA_SOME_MS);
  ck(f.c[1].nome === 'Gino' && f.c[1].cor === 5 && f.c[1].ultimoMs === 1000, 'roster da nome e cor mas NAO conta como sinal de vida');
  ck(frotaEnvelhece(f, 1000 + FROTA_SOME_MS) === 0 && frotaEnvelhece(f, 1001 + FROTA_SOME_MS) === 1 && !f.c[1].ativo, 'envelhece: sai da lista so depois de FROTA_SOME_MS');
  // roster de quem nunca mandou posicao: entra na sala de espera, sinal NUNCA
  frotaRecebeRoster(f, 4, 1, false, 'Fausto', 5000);
  ck(frotaAcha(f, 4) > 0 && frotaSinal(f.c[frotaAcha(f, 4)], 5000) === SINAL.NUNCA, 'roster sem posicao aparece na lista com sinal NUNCA');
  // lotacao: 7 outros cabem, o 8o nao
  const g = frotaNova(); frotaIdent(g, 'eu', 0, 0); let ok = 0;
  for (let s = 1; s < 8; s++) if (frotaRecebePos(g, { ...k, slot: s }, 1) > 0) ok++;
  ck(ok === 7 && frotaRecebePos(g, { ...k, slot: 8 }, 1) === -3, '7 outros carros cabem; slot 8 e invalido');
}

// ---------------------------------------------------------------- [4] cfg.h
sec('[4] cfg.h: migracao de esquema');
function cfgMigra(c, esquemaLido, pwrvLido) {
  let mudou = false, e = esquemaLido;
  if (e === 0 && pwrvLido >= 101) e = 1;
  if (e < 1) { c.pwr = RF_PWR_PADRAO; e = 1; mudou = true; }
  if (e < 2) { e = 2; mudou = true; }
  return { mudou, e };
}
{
  let c = { pwr: 6 }; let r = cfgMigra(c, 0, 0);
  ck(r.mudou && r.e === 2 && c.pwr === RF_PWR_PADRAO, 'esquema 0 sem pwrv (mts_p4 / v100): pwr volta ao padrao UMA vez');
  c = { pwr: 6 }; r = cfgMigra(c, 0, 101);
  ck(r.mudou && r.e === 2 && c.pwr === 6, 'v101 (pwrv=101): o pwr e do usuario, so o esquema sobe');
  c = { pwr: 14 }; r = cfgMigra(c, 2, 101);
  ck(!r.mudou && c.pwr === 14, 'esquema atual: nada muda');
}

// ---------------------------------------------------------------- [5] diario.h x servico.h
sec('[5] diario.h: o cabecalho da linha E bate com o que o servico escreve');
{
  const D = R('firmware/mts/core/diario.h');
  const cabE = D.match(/DIARIO_CAB_E "([^"]+)"/)[1];
  const cols = cabE.replace(/^# /, '').split(',').length;
  const decl = parseInt(D.match(/DIARIO_E_COLUNAS (\d+)/)[1]);
  ck(cols === decl, `cabecalho E tem ${cols} colunas = DIARIO_E_COLUNAS (${decl})`);
  const S = R('firmware/mts/app/servico.h');
  const fmt = S.match(/registroLinha\("E,([^;]+?)",/s);
  const fmtStr = fmt ? fmt[1].replace(/"\s*\n\s*"/g, '') : '';
  const nEsp = (fmtStr.match(/%[-0-9.]*[a-zA-Z]/g) || []).length;
  ck(nEsp === cols - 1, `a linha E do servico tem ${nEsp} %-campos = ${cols - 1} valores`);
  for (const t of ['DIARIO_CAB_R', 'DIARIO_CAB_T', 'DIARIO_CAB_N', 'DIARIO_CAB_G']) ck(new RegExp(t + ' "# ').test(D), t + ' existe');
}

// ---------------------------------------------------------------- [6] auditoria do radio
sec('[6] hal/radio.h e app/rede.h: as armadilhas continuam no lugar');
{
  const H = R('firmware/mts/hal/radio.h');
  const N = R('firmware/mts/app/rede.h');
  const iPos = H.indexOf('radioHwPosBegin');
  const bloco = H.slice(iPos, H.indexOf('inline bool radioHwBegin'));
  ck(/setDio2AsRfSwitch\(false\)/.test(bloco), 'setDio2AsRfSwitch(false) depois do begin');
  ck(/setCurrentLimit\(RF_OCP_MA\)/.test(bloco), 'setCurrentLimit(140) depois do begin');
  ck(bloco.indexOf('setOutputPower(pwr, false)') > 0 && bloco.indexOf('setPaRampTime') > bloco.indexOf('setOutputPower'), 'setOutputPower(p, false) e a rampa DEPOIS dele');
  ck(/clearDeviceErrors\(\)/.test(bloco), 'clearDeviceErrors no boot');
  ck(/spiConfig\.timeout = 100/.test(bloco), 'timeout de SPI de 100 ms');
  ck(!/setOutputPower\([^)]*\)\s*;/.test(H.replace(/setOutputPower\([^,]+,\s*false\)/g, '')), 'NENHUM setOutputPower sem o false (paOptTable desligada)');
  ck(!/XTAL\s*=\s*true/.test(H.replace(/\/\/.*$/gm, '')), 'sem radio.XTAL = true no codigo (so no comentario)');
  ck(/radioHwBusyLivre/.test(N) && N.indexOf('radioHwBusyLivre()') < N.indexOf('radioHwTransmite(buf, n)'), 'guarda de BUSY antes de todo startTransmit');
  ck(/radioHwTxDoneNoChip\(\)/.test(N), 'o vigia pergunta ao chip antes de acusar TX preso');
  // so o caminho COM radio: o ramo #else (sem radio) tem stubs que tambem escrevem pwrConfig
  const codigo = N.split('#else   // !MTS_TEM_RADIO')[0].replace(/\/\/.*$/gm, '');
  ck(!/RECUO_AUTO|recuoAuto|RADIO_RECUO/.test(codigo), 'sem recuo automatico no codigo');
  const atribPwr = (codigo.match(/pwrConfig\s*=/g) || []).length;
  ck(atribPwr === 2, `pwrConfig so e escrito 2x no codigo (init e comando explicito): ${atribPwr}`);
  ck(!/nvsGrava|putChar|Preferences/.test(codigo), 'rede.h nunca grava na NVS (a potencia gravada e do app, por comando)');
  ck(/protoVersaoOk\(buf, n\)/.test(N) && /frotaVersaoAlheia/.test(N), 'pacote de versao alheia e contado e nao entra na frota');
  ck(/tdmaBeaconAceita\(r\.tdma, k\.slot, srcPps\)/.test(N) && /tdmaEleicao\(r\.tdma\)/.test(N), 'politica de aceitacao e eleicao do tdma_core em uso');
  ck(/tdmaUsUntilMySlot\(g_rede\.tdma\) > us \+ ORC_RESERVA_US/.test(N), 'redePodeGastar usa tdmaUsUntilMySlot (C4 da auditoria)');
  const G = R('firmware/mts/hal/gps.h');
  ck(/idade < GPS_PAR_PPS_MAX_US/.test(G) && /backlogDescartado\+\+/.test(G), 'C1: o par PPS/NMEA confere idade e descarta backlog');
  const T = R('firmware/mts/core/tdma.h');
  ck(/#include "\.\.\/tdma_core\.h"/.test(T), 'core/tdma.h inclui o tdma_core.h compartilhado (sem copia)');
  const W = R('firmware/mts/hal/pinos.h');
  ck(/#define MTS_TEM_WIFI\s+0/.test(W), 'WiFi desligado por padrao');
  ck(!/#defines+MTS_PLACA/.test(W) && !fs.existsSync(path.join(__dirname, '..', '..', 'firmware', 'mts', 'hal', 'LGFX_P4_LCD5.h')), 'um alvo so: sem MTS_PLACA nem LGFX_P4_LCD5.h');
  // core nao inclui Arduino
  let arduinoNoCore = [];
  for (const f of fs.readdirSync(path.join(__dirname, '..', '..', 'firmware', 'mts', 'core'))) {
    const s = R('firmware/mts/core/' + f);
    if (/#include\s+<Arduino\.h>|#include\s+"esp_|#include\s+<esp_/.test(s)) arduinoNoCore.push(f);
  }
  ck(arduinoNoCore.length === 0, 'nenhum core/*.h inclui Arduino/ESP diretamente' + (arduinoNoCore.length ? ' (' + arduinoNoCore.join(',') + ')' : ''));
}

console.log(`\n${pass} PASS, ${fail} FAIL`);
process.exit(fail ? 1 : 0);
