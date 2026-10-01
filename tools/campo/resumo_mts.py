"""Resumo por boot dos diarios do firmware/mts (linhas E/R/T/!).

    python tools/campo/resumo_mts.py dados_campo/gino_2026-09-30 [boot17 boot18 ...]

Para cada arquivo: quanto tempo ficou ligado, se o radio subiu (e quando), quantas
transmissoes completaram (linha T) contra TX presos, o que foi recebido de cada
slot (posicao/roster/CRC ruim) com RSSI/SNR, e os eventos '!' que importam.
"""
import sys, os, glob, collections, statistics

def resumo(caminho):
    T, presos, eventos = [], 0, []
    R = collections.defaultdict(lambda: collections.Counter())
    rssi = collections.defaultdict(list)
    snr = collections.defaultdict(list)
    E = []
    for l in open(caminho, encoding='utf-8', errors='replace'):
        c = l.rstrip('\n').split(',')
        if not c or c[0].startswith('#'):
            continue
        t = c[0]
        if t == 'T':
            T.append(c)
        elif t == 'R' and len(c) >= 12:
            slot = c[3]
            tipo = c[11].split(' ')[0]
            R[slot][tipo] += 1
            try:
                if tipo in ('pos', 'roster'):
                    rssi[slot].append(float(c[6])); snr[slot].append(float(c[7]))
            except ValueError:
                pass
        elif t == 'E':
            E.append(c)
        elif t == '!':
            ev = ','.join(c[2:])
            if 'tx preso' in ev:
                presos += 1
            if any(k in ev for k in ('boot,', 'radio', 'fonte de tempo', 'assumi', 'sala', 'busy', 'startTransmit',
                                     'REINICIOS', 'potencia', 'slot', 'relogio')):
                eventos.append((int(c[1]) if c[1].isdigit() else 0, ev))
    dur = int(E[-1][1]) / 1000 if E else 0
    print(f"\n=== {os.path.basename(caminho)}  ({dur/60:.1f} min ligado, {len(E)} linhas E)")
    if E:
        e = E[-1]
        nomes = 'E,ms,utc,lat,lon,slot,sync,ancora,tx,perdi,preso,rx,ruim,conflito,rssiUlt,pwr,fix,sats'.split(',')
        d = dict(zip(nomes, e))
        fixes = sum(1 for x in E if len(x) > 16 and x[16] == '1')
        print(f"  fim: slot {d.get('slot')} sync {d.get('sync')} ancora {d.get('ancora')} | tx {d.get('tx')} perdi {d.get('perdi')} "
              f"preso {d.get('preso')} | rx {d.get('rx')} ruim {d.get('ruim')} | pwr {d.get('pwr')} | GPS com fix em {fixes}/{len(E)} s")
    print(f"  TX completados (linha T): {len(T)}  | TX presos: {presos}")
    if T:
        air = [int(x[6]) for x in T if x[6].isdigit() and int(x[6]) > 0]
        if air:
            print(f"    air-time medido: min {min(air)} med {int(statistics.median(air))} max {max(air)} us")
    if not R:
        print("  RECEBIDO: nada")
    for slot in sorted(R):
        rs, sn = rssi[slot], snr[slot]
        extra = f" | RSSI med {statistics.median(rs):.0f} (min {min(rs):.0f} max {max(rs):.0f}) SNR med {statistics.median(sn):.1f}" if rs else ""
        print(f"  RECEBIDO do slot {slot}: {dict(R[slot])}{extra}")
    for ms, ev in eventos[:14]:
        print(f"    {ms/1000:7.1f} s  {ev[:120]}")
    if len(eventos) > 14:
        print(f"    ... mais {len(eventos) - 14} eventos")

if __name__ == '__main__':
    pasta = sys.argv[1]
    nomes = sys.argv[2:]
    arqs = [os.path.join(pasta, n if n.endswith('.csv') else n + '.csv') for n in nomes] if nomes else sorted(glob.glob(os.path.join(pasta, '*.csv')))
    for a in arqs:
        if os.path.getsize(a) > 0:
            resumo(a)
