#!/usr/bin/env python3
"""Resumo forense dos diarios SD do firmware/mts (v200..v202).

    python -I tools/campo/resumo_diario.py PASTA [PASTA ...] [--sem-dist]

Cada PASTA e o cartao de UMA tela (bootN.csv / HHMMSS_N.csv). Para cada arquivo:
  - boot: nome, slot, pwr, motivo do reset (esp_reset_reason) e rom, seguro, fw
  - linha C (caixa-preta do boot anterior): etapa, tx em voo, ha quantos ms
  - eventos '!': tx preso (com err do chip), chip voltou de fabrica, relogio
    voltou (com o salto em multiplos de 2^26 ticks = 4,194 s), busy preso,
    startTransmit falhou, radio subiu tarde, sem DIO1, AVISO (a faixa da tela)
  - T (TX completos), R (pacotes: bons, CRC ruim, outra sala), N (ruido)
  - N: percentis; separa -128 (= byte 0xFF: chip sem RX/sem 5 V), 0 (leitura SPI
    falhou) e > -60 (mediu o TX de alguem) do ruido de verdade
  - E: ultimo valor de preso/relogioVoltou/rx/ruim
Com duas ou mais pastas: RSSI/SNR dos pacotes recebidos por DISTANCIA entre as
telas (posicao do remetente tirada do diario DELE no mesmo segundo UTC, +-3 s) e
entrega por distancia (T do remetente casado com R do receptor pelo seq).
So biblioteca padrao.
"""
import sys, os, glob, re, math, collections, statistics

MOTIVO = {1: 'POWERON', 3: 'SW', 4: 'PANIC', 5: 'INT_WDT', 6: 'TASK_WDT', 7: 'WDT', 8: 'DEEPSLEEP',
          9: 'BROWNOUT', 10: 'SDIO', 11: 'USB', 12: 'JTAG', 13: 'EFUSE', 14: 'PWR_GLITCH', 15: 'CPU_LOCKUP'}
VIOLENTOS = {4, 5, 6, 7, 9, 14, 15}
BINS = [(0, 100), (100, 250), (250, 400), (400, 650), (650, 1000), (1000, 99999)]


def pct(v, p):
    if not v:
        return float('nan')
    s = sorted(v)
    k = (len(s) - 1) * p / 100.0
    i = int(k)
    return s[i] if i + 1 >= len(s) else s[i] + (s[i + 1] - s[i]) * (k - i)


def dist_m(a, b):
    la1, lo1 = map(math.radians, a); la2, lo2 = map(math.radians, b)
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * 6371000 * math.asin(math.sqrt(h))


def chave_boot(caminho):
    m = re.search(r'(\d+)\.csv$', os.path.basename(caminho))
    return int(m.group(1)) if m else 0


class Arq:
    pass


def le(caminho):
    a = Arq()
    a.path = caminho; a.nome = os.path.basename(caminho); a.fw = None; a.formato = '?'
    a.boot = {}; a.C = []; a.ev = collections.Counter(); a.evl = []
    a.T = []; a.R = []; a.N = []; a.E = []; a.ecol = {}; a.aviso = []
    a.saltos = []; a.presos = []
    for l in open(caminho, encoding='utf-8', errors='replace'):
        l = l.rstrip('\r\n')
        if not l:
            continue
        if l.startswith('#'):
            m = re.match(r'# MTS fw v(\d+)', l)
            if m:
                a.fw = int(m.group(1)); a.formato = 'mts'
            elif l.startswith('# MTS campo'):
                a.formato = 'campo_v100'
            if l.startswith('# E,'):
                nomes = [x.split('(')[0] for x in l[2:].split(',')]
                a.ecol = {n: i for i, n in enumerate(nomes)}
            continue
        c = l.split(',')
        t = c[0]
        try:
            if t == 'E':
                a.E.append(c)
            elif t == 'N' and len(c) >= 4:
                a.N.append((int(c[1]), int(c[2] or 0), float(c[3]), c[4] if len(c) > 4 else ''))
            elif t == 'T' and len(c) >= 7:
                a.T.append({'ms': int(c[1]), 'utc': int(c[2] or 0), 'seq': int(c[3]), 'tipo': c[4],
                            'air': int(c[6] or 0)})
            elif t == 'R' and len(c) >= 12:
                a.R.append({'ms': int(c[1]), 'utc': int(c[2] or 0), 'slot': c[3], 'seq': c[4],
                            'rssi': float(c[6]), 'snr': float(c[7]), 'crc': c[9], 'tipo': c[11].split(' ')[0]})
            elif t == 'C':
                a.C.append(','.join(c[2:]))
            elif t == '!':
                ms = int(c[1]) if c[1].isdigit() else 0
                ev = c[2] if len(c) > 2 else ''
                resto = ','.join(c[3:])
                a.ev[ev] += 1
                if ev == 'boot':
                    a.boot = {'nome': c[3]}
                    for x in c[4:]:
                        p = x.strip().split(' ', 1)
                        if len(p) == 2:
                            a.boot[p[0]] = p[1]
                elif ev == 'relogio voltou':
                    us = int(re.sub(r'\D', '', resto) or 0)
                    a.saltos.append((ms, us))
                elif ev == 'tx preso':
                    a.presos.append((ms, resto))
                elif ev == 'AVISO':
                    a.aviso.append(resto)
                if ev not in ('tela', 'alerta meu', 'sala', 'fonte de tempo', 'assumi a ancora', 'slot', 'boot'):
                    a.evl.append((ms, ev, resto))
        except (ValueError, IndexError):
            a.ev['_linha_ilegivel'] += 1
    return a


def ecol(a, linha, nome, default=None):
    i = a.ecol.get(nome)
    if i is None or i >= len(linha):
        return default
    return linha[i]


def resumo_arquivo(a, out):
    b = a.boot
    mot = int(b.get('motivo', -1)) if b.get('motivo', '-1').lstrip('-').isdigit() else -1
    dur = (int(a.E[-1][1]) / 1000.0) if a.E else 0
    utc0 = next((int(x[2]) for x in a.E if x[2] not in ('', '0')), 0)
    hh = f"{utc0 // 3600:02d}:{utc0 % 3600 // 60:02d}Z" if utc0 else 'sem UTC'
    out.append(f"\n--- {a.nome} [{a.formato} fw {a.fw}] {b.get('nome', '?')} slot {b.get('slot', '?')} "
               f"pwr {b.get('pwr', '?')} | motivo {mot} {MOTIVO.get(mot, '')}{' (VIOLENTO)' if mot in VIOLENTOS else ''} "
               f"rom {b.get('rom', '?')} seguro {b.get('seguro', '?')} | {dur / 60:.1f} min ligado, inicio {hh}")
    for c in a.C:
        out.append(f"    CAIXA-PRETA do boot anterior: {c}")
    for x in a.aviso:
        out.append(f"    AVISO NA TELA: {x}")
    nT = len(a.T)
    nP = len(a.presos)
    errs = collections.Counter(re.search(r'err (0x[0-9A-Fa-f]+)', r).group(1) if re.search(r'err (0x[0-9A-Fa-f]+)', r) else '?'
                               for _, r in a.presos)
    out.append(f"    TX completos (T) {nT} | TX presos {nP}{' ' + dict(errs).__repr__() if nP else ''} | "
               f"voltou de fabrica {a.ev.get('chip voltou de fabrica', 0)} | busy preso {a.ev.get('busy preso', 0)} | "
               f"startTransmit falhou {a.ev.get('startTransmit falhou', 0)} | radio subiu tarde {a.ev.get('radio subiu tarde', 0)} | "
               f"sem DIO1 {a.ev.get('radio sem DIO1 - modo consulta', 0)}")
    if a.saltos:
        s = ', '.join(f"{ms / 1000:.0f}s:{us / 1e6:.2f}s(={us / 4194304:.3f}x2^26)" for ms, us in a.saltos)
        out.append(f"    RELOGIO VOLTOU {len(a.saltos)}x: {s}")
    if a.T:
        air = [t['air'] for t in a.T if t['air'] > 0]
        if air:
            out.append(f"    air-time medido us: min {min(air)} mediana {int(statistics.median(air))} max {max(air)}")
    if a.R:
        bons = [r for r in a.R if r['crc'] == '1' and r['tipo'] in ('pos', 'roster')]
        ruins = [r for r in a.R if r['crc'] != '1' or r['tipo'].startswith('crc')]
        fora = [r for r in a.R if r['tipo'] == 'sala']
        linha = f"    RX: {len(bons)} bons, {len(ruins)} CRC/tamanho ruim, {len(fora)} de OUTRA sala"
        if bons:
            rs = [r['rssi'] for r in bons]; sn = [r['snr'] for r in bons]
            linha += (f" | RSSI p10/p50/p90 {pct(rs, 10):.0f}/{pct(rs, 50):.0f}/{pct(rs, 90):.0f} "
                      f"SNR p10/p50 {pct(sn, 10):.1f}/{pct(sn, 50):.1f} | mais fraco decodificado {min(rs):.0f} dBm")
            # piso estimado pelos pacotes: com SNR<0 o RSSI do pacote ~ ruido
            neg = [r['rssi'] - r['snr'] for r in bons if r['snr'] > 0 and r['snr'] < 8]
            if neg:
                linha += f" | piso pelos pacotes (rssi-snr, 0<snr<8) mediana {statistics.median(neg):.0f}"
        out.append(linha)
    if a.N:
        v = [n[2] for n in a.N]
        m128 = sum(1 for x in v if x <= -127.5)
        zero = sum(1 for x in v if x >= -0.5)
        alto = sum(1 for x in v if -60 < x < -0.5)
        real = [x for x in v if -127.5 < x <= -60]
        linha = f"    RUIDO N: {len(v)} amostras | -128(0xFF, chip sem RX) {m128} | 0 (SPI falhou) {zero} | >-60 (TX de alguem) {alto}"
        if real:
            linha += (f" | resto n={len(real)} p5/p25/p50/p75/p95 = {pct(real, 5):.0f}/{pct(real, 25):.0f}/"
                      f"{pct(real, 50):.0f}/{pct(real, 75):.0f}/{pct(real, 95):.0f} dBm")
        out.append(linha)
    if a.E:
        e = a.E[-1]
        out.append(f"    E final: tx {ecol(a, e, 'tx')} perdi {ecol(a, e, 'perdi')} preso {ecol(a, e, 'preso')} rx {ecol(a, e, 'rx')} "
                   f"ruim {ecol(a, e, 'ruim')} relogioVoltou {ecol(a, e, 'relogioVoltou')} seguro {ecol(a, e, 'seguro')} "
                   f"pwr {ecol(a, e, 'pwr')}/{ecol(a, e, 'pwrConfig')} sdErros {ecol(a, e, 'sdErros')} carrosAtivos {ecol(a, e, 'carrosAtivos')}")
    outros = [x for x in a.evl if x[1] not in ('tx preso', 'relogio voltou', 'AVISO', 'radio subiu tarde', 'flush lento')]
    if outros:
        cnt = collections.Counter(x[1] for x in outros)
        out.append(f"    outros eventos: {dict(cnt)}")


def resumo_pasta(pasta, out):
    arqs = sorted(glob.glob(os.path.join(pasta, '*.csv')), key=chave_boot)
    lidos = [le(p) for p in arqs if os.path.getsize(p) > 0]
    vazios = [os.path.basename(p) for p in arqs if os.path.getsize(p) == 0]
    out.append(f"\n================ {pasta}  ({len(lidos)} diarios, {len(vazios)} vazios{': ' + ' '.join(vazios) if vazios else ''})")
    motivos = collections.Counter(); viol = 0
    for a in lidos:
        resumo_arquivo(a, out)
        m = a.boot.get('motivo')
        if m and m.lstrip('-').isdigit():
            motivos[int(m)] += 1; viol += int(m) in VIOLENTOS
    out.append(f"\n  TOTAL {pasta}: boots por motivo {{{', '.join(f'{k} {MOTIVO.get(k, chr(63))}: {v}' for k, v in sorted(motivos.items()))}}} "
               f"| violentos {viol} | caixas-pretas {sum(len(a.C) for a in lidos)} | presos {sum(len(a.presos) for a in lidos)} | "
               f"voltou de fabrica {sum(a.ev.get('chip voltou de fabrica', 0) for a in lidos)} | relogio voltou {sum(len(a.saltos) for a in lidos)} | "
               f"avisos {sum(len(a.aviso) for a in lidos)}")
    # ruido por versao e por classe de boot
    porfw = collections.defaultdict(list)
    for a in lidos:
        if a.N:
            classe = 'com TX ok' if a.T else ('sem TX (presos)' if a.presos else 'sem TX')
            porfw[(a.fw, classe)] += [n[2] for n in a.N if -127.5 < n[2] <= -60]
    for (fw, classe), v in sorted(porfw.items(), key=lambda x: (x[0][0] or 0, x[0][1])):
        if v:
            out.append(f"  ruido fw {fw} boots {classe}: n={len(v)} p5/p50/p95 {pct(v, 5):.0f}/{pct(v, 50):.0f}/{pct(v, 95):.0f} dBm")
    return lidos


def por_distancia(pastas_lidas, out):
    # trilha de posicao por tela: (nome) -> {utc: (lat, lon)}; slot por (nome, arquivo)
    pos = collections.defaultdict(dict)
    for lidos in pastas_lidas:
        for a in lidos:
            nome = a.boot.get('nome')
            if not nome or not a.ecol:
                continue
            ifix = a.ecol.get('fix')
            for e in a.E:
                try:
                    u = int(e[2]); fix = e[ifix] == '1' if ifix is not None else True
                    if u and fix:
                        pos[nome][u] = (float(e[3]), float(e[4]))
                except (ValueError, IndexError):
                    pass

    def acha(nome, u):
        d = pos.get(nome, {})
        for k in (0, 1, -1, 2, -2, 3, -3):
            if u + k in d:
                return d[u + k]
        return None

    # quem fala em que slot: o slot do boot
    slot_nome = {}
    for lidos in pastas_lidas:
        for a in lidos:
            if a.boot.get('nome') and a.boot.get('slot') is not None:
                slot_nome.setdefault(a.boot['slot'], set()).add(a.boot['nome'])
    out.append("\n================ RSSI/SNR por distancia (pacotes bons, mesma sala; posicao do remetente pelo diario dele)")
    tab = collections.defaultdict(list)
    for lidos in pastas_lidas:
        for a in lidos:
            rx = a.boot.get('nome')
            for r in a.R:
                if r['crc'] != '1' or r['tipo'] not in ('pos', 'roster') or not r['utc']:
                    continue
                tx = [n for n in slot_nome.get(r['slot'], ()) if n != rx]
                if not tx:
                    continue
                p1 = acha(rx, r['utc']); p2 = acha(tx[0], r['utc'])
                if not p1 or not p2:
                    continue
                d = dist_m(p1, p2)
                for lo, hi in BINS:
                    if lo <= d < hi:
                        tab[(tx[0], rx, a.nome, lo, hi)].append((r['rssi'], r['snr']))
    for k in sorted(tab):
        v = tab[k]
        rs = [x[0] for x in v]; sn = [x[1] for x in v]
        out.append(f"  {k[0]:>14} -> {k[1]:<14} {k[2]:<16} {k[3]:>4}-{k[4]:<5} m: n={len(v):4d} RSSI med {statistics.median(rs):6.1f} "
                   f"(min {min(rs):.0f}) SNR med {statistics.median(sn):5.1f} (min {min(sn):.1f})")
    # entrega: T do remetente vs R do receptor (seq, +-3 s)
    out.append("\n================ ENTREGA por distancia (T do remetente casado com R bom do receptor pelo seq, +-3 s)")
    rxidx = collections.defaultdict(set)
    rxjan = collections.defaultdict(list)   # receptor -> lista de (ini, fim) de utc em que o diario dele estava ligado
    for lidos in pastas_lidas:
        for a in lidos:
            rx = a.boot.get('nome')
            us = [int(e[2]) for e in a.E if e[2] not in ('', '0')]
            if us:
                rxjan[rx].append((min(us), max(us)))
            for r in a.R:
                if r['crc'] == '1' and r['tipo'] in ('pos', 'roster') and r['utc']:
                    rxidx[(rx, r['slot'])].add((r['seq'], r['utc']))
    ent = collections.defaultdict(lambda: [0, 0])
    for lidos in pastas_lidas:
        for a in lidos:
            tx = a.boot.get('nome'); slot = a.boot.get('slot')
            for t in a.T:
                if not t['utc']:
                    continue
                for rx in pos:
                    if rx == tx or not any(i <= t['utc'] <= f for i, f in rxjan.get(rx, [])):
                        continue
                    p1 = acha(tx, t['utc']); p2 = acha(rx, t['utc'])
                    if not p1 or not p2:
                        continue
                    d = dist_m(p1, p2)
                    ok = any(int(s) == t['seq'] and abs(u - t['utc']) <= 3 for s, u in rxidx.get((rx, slot), ())
                             if s.isdigit())
                    for lo, hi in BINS:
                        if lo <= d < hi:
                            ent[(tx, rx, lo, hi)][0] += 1; ent[(tx, rx, lo, hi)][1] += ok
    for k in sorted(ent):
        n, ok = ent[k]
        out.append(f"  {k[0]:>14} -> {k[1]:<14} {k[2]:>4}-{k[3]:<5} m: {ok}/{n} = {100.0 * ok / n:5.1f} %"
                   "  (inclui periodos em grupos diferentes: pacote de outra sala nao conta)")


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    if not args:
        print(__doc__); sys.exit(1)
    out = []
    lidas = [resumo_pasta(p, out) for p in args]
    if len(lidas) >= 2 and '--sem-dist' not in sys.argv:
        por_distancia(lidas, out)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
