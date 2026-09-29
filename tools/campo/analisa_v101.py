# Resume um CSV do mts_campo v101 (registro.h) pelas linhas novas: R, T, N, C.
#
#   python tools\campo\analisa_v101.py dados_campo\cartaoX\arquivo.csv
#
# O que separa as hipoteses do campo de 2026-09-28 (entrega de ~50% a -97 dBm):
#   - N: ruido de fundo (RSSI sem pacote). Piso acima de ~-110 dBm em SF9 = o
#     receptor esta ouvindo ruido do proprio carro (step-down, tela, lataria).
#   - R: RSSI/SNR dos pacotes BONS contra os de CRC RUIM. CRC ruim com RSSI alto
#     e SNR baixo = ruido/colisao; CRC ruim com RSSI baixo = borda do alcance.
#   - R: perda por SEQUENCIA por remetente, contada, nao estimada.
#   - T: air-time medido x calculado. Se o medido passa do calculado, a janela
#     do slot esta mentindo e o pacote invade o vizinho.
#   - E: 'perdi' (janelas perdidas) e lacoPiorUs por minuto.
import csv, sys, statistics as st
from collections import defaultdict

def f(x):
    try: return float(x)
    except: return None

def mediana(v): return st.median(v) if v else float('nan')

def main(caminho):
    R, T, N, E, C, EV = [], [], [], [], [], []
    for r in csv.reader(open(caminho, encoding='utf-8', errors='replace')):
        if not r or r[0].startswith('#'): continue
        t = r[0]
        if t == 'R': R.append(r)
        elif t == 'T': T.append(r)
        elif t == 'N': N.append(r)
        elif t == 'E': E.append(r)
        elif t == 'C': C.append(r)
        elif t == '!': EV.append(r)
    print(f'{caminho}: E={len(E)} R={len(R)} T={len(T)} N={len(N)} eventos={len(EV)} caixa={len(C)}')
    for r in EV[:20]: print('  !', ','.join(r[1:]))
    for r in C: print('  CAIXA-PRETA:', ','.join(r[1:]))

    if E:
        dur = (f(E[-1][1]) - f(E[0][1])) / 1000
        print(f'\nE: {len(E)} linhas em {dur:.0f} s ({len(E)/max(dur,1):.2f}/s - esperado ~1/s)')
        if len(E[-1]) > 21:
            r = E[-1]
            print(f'   fim: tx={r[8]} perdi={r[9]} preso={r[10]} rx={r[11]} ruim={r[12]} seqPerd={r[21]} '
                  f'ruidoMed={r[23]} airUlt={r[24]} airMax={r[25]} rotaN={r[26]} prefixo={r[27]} '
                  f'catchups={r[28]} relogioVoltou={r[29]} pwrCfg={r[30]} seguro={r[31]}')
        lacos = [f(r[19]) for r in E if f(r[19]) is not None]
        if lacos: print(f'   laco pior por segundo: mediana {mediana(lacos)/1000:.0f} ms, max {max(lacos)/1000:.0f} ms')

    if N:
        v = [f(r[3]) for r in N if f(r[3]) is not None]
        print(f'\nN (ruido de fundo, {len(v)} amostras): mediana {mediana(v):.0f} dBm, '
              f'p10 {sorted(v)[len(v)//10]:.0f}, p90 {sorted(v)[len(v)*9//10]:.0f}, min {min(v):.0f}, max {max(v):.0f}')
        print('   referencia: um SX1262 quieto em SF9/BW125 fica em torno de -110 a -115 dBm;')
        print('   mais alto que isso = ruido entrando pelo receptor (step-down, tela, lataria).')

    if R:
        bons = [r for r in R if r[9] == '1']
        ruins = [r for r in R if r[9] == '0']
        def stats(lst, nome):
            rs = [f(r[6]) for r in lst if f(r[6]) is not None]
            sn = [f(r[7]) for r in lst if f(r[7]) is not None]
            fe = [f(r[8]) for r in lst if len(r) > 8 and f(r[8]) is not None]
            if not rs: return
            print(f'   {nome}: {len(lst)} | rssi mediana {mediana(rs):.0f} (min {min(rs):.0f} max {max(rs):.0f}) '
                  f'| snr mediana {mediana(sn):.1f} (min {min(sn):.1f}) | ferr mediana {mediana(fe) if fe else 0:.0f} Hz')
        print(f'\nR: {len(R)} recepcoes, {len(bons)} boas, {len(ruins)} com CRC ruim ({100*len(ruins)/len(R):.0f}%)')
        stats(bons, 'CRC ok  ')
        stats(ruins, 'CRC ruim')
        porSlot = defaultdict(lambda: [0, 0])
        for r in bons:
            if r[3] in ('-1', ''): continue
            porSlot[r[3]][0] += 1
            porSlot[r[3]][1] += int(f(r[10]) or 0)
        for s, (n, perd) in sorted(porSlot.items()):
            print(f'   do slot {s}: {n} recebidos, {perd} perdidos por sequencia -> entrega {100*n/max(n+perd,1):.0f}%')
        cu = [r for r in bons if len(r) > 14 and r[11] == 'pos' and r[13] not in ('0', '')]
        if cu: print(f'   pacotes com cauda de trilha: {len(cu)}, pontos novos na rota: {sum(int(r[14]) for r in cu)}')

    if T:
        air = defaultdict(list)
        for r in T: air[(r[4], r[5])].append(f(r[6]))
        print(f'\nT: {len(T)} transmissoes')
        for (tipo, n), v in sorted(air.items()):
            v = [x for x in v if x]
            if v: print(f'   {tipo} {n} B: {len(v)}x | air medido mediana {mediana(v)/1000:.1f} ms, min {min(v)/1000:.1f}, max {max(v)/1000:.1f}')
        us = [f(r[7]) for r in T if f(r[7]) is not None]
        if us: print(f'   inicio do TX dentro do quadro: mediana {mediana(us)/1000:.0f} ms, max {max(us)/1000:.0f} ms')
        cc = sum(1 for r in T if len(r) > 8 and r[8] == '1')
        print(f'   quadros de catch-up: {cc}')

if __name__ == '__main__':
    for c in sys.argv[1:]: main(c); print()
