"""Cruza o diario de quem TRANSMITE (linhas T) com o de quem RECEBE (linhas R).

    python tools/campo/cruza_mts.py <diario_tx.csv> <slot_tx> <diario_rx.csv>

Casa cada transmissao pelo segundo UTC (coluna utc das duas telas, que vem do
GPS) e pelo seq de 8 bits. Para cada uma diz se chegou, e se chegou: RSSI, CRC,
e se foi descartada por estar em OUTRO GRUPO. Depois separa as perdas pelo que
pode explica-las: o instante no quadro em que o TX comecou e o que o receptor
estava fazendo naquele segundo (o proprio TX, linha T).
"""
import sys, collections, statistics

def le(caminho):
    T, R, E = [], [], []
    for l in open(caminho, encoding='utf-8', errors='replace'):
        c = l.rstrip('\n').split(',')
        if c[0] == 'T' and len(c) >= 10:
            T.append(dict(ms=int(c[1]), utc=int(c[2]), seq=int(c[3]), tipo=c[4], bytes=int(c[5]), air=int(c[6]), usq=int(c[7])))
        elif c[0] == 'R' and len(c) >= 12:
            try:
                R.append(dict(ms=int(c[1]), utc=int(c[2]), slot=int(c[3]), seq=int(c[4]), rssi=float(c[6]), snr=float(c[7]), tipo=c[11].split(' ')[0]))
            except ValueError:
                pass
        elif c[0] == 'E' and len(c) > 16:
            E.append(dict(ms=int(c[1]), utc=int(c[2]), sync=c[6], fix=c[16]))
    return T, R, E

tx_arq, slot_tx, rx_arq = sys.argv[1], int(sys.argv[2]), sys.argv[3]
T, _, Etx = le(tx_arq)
Ttx_rx, R, Erx = le(rx_arq)
R = [r for r in R if r['slot'] == slot_tx]
recebido = {(r['utc'], r['seq']): r for r in R}
# Tolerancia de 3 s: o rotulo UTC de cada tela e o ultimo NMEA que ELA leu, e
# entre duas telas ele difere em ate 2 s (medido em 2026-09-30: seq 36 saiu em
# 86285 e chegou registrado em 86287). O seq de 8 bits so repete a cada 256
# quadros (~25 min), entao 3 s nao casa pacote errado.
def achou(t):
    for du in (0, 1, -1, 2, -2, 3, -3):
        r = recebido.get((t['utc'] + du, t['seq']))
        if r:
            return r
    return None

utc_rx = [e['utc'] for e in Erx if e['utc'] > 0]
if not utc_rx:
    sys.exit('o receptor nao tem UTC (sem GPS): nao da para casar')
# So os segundos em que o receptor estava LIGADO (linha E daquele segundo, com
# folga de 1 s). Um intervalo min..max quebra quando o diario cruza a meia-noite
# UTC: ele pega o dia inteiro e conta como perdido o que nem tinha quem ouvisse.
ligado = set()
for u in utc_rx:
    ligado.update((u - 1, u, u + 1))
T = [t for t in T if t['utc'] > 0 and t['utc'] in ligado]
print(f"transmissoes do slot {slot_tx} com o receptor ligado: {len(T)}")
ini, fim = min(utc_rx), max(utc_rx)
cont = collections.Counter()
rssi = []
perdas_usq = []
ok_usq = []
meu_tx = {t['utc'] for t in Ttx_rx}
perdas_com_meu_tx = 0
for t in T:
    r = achou(t)
    if not r:
        cont['PERDIDO'] += 1
        perdas_usq.append(t['usq'])
        if t['utc'] in meu_tx:
            perdas_com_meu_tx += 1
    elif r['tipo'] == 'sala':
        cont['chegou, OUTRO GRUPO (descartado)'] += 1
    else:
        cont[f"chegou ({r['tipo']})"] += 1
        rssi.append(r['rssi']); ok_usq.append(t['usq'])
for k, v in cont.most_common():
    print(f"  {k:36s} {v:5d}  ({100*v/len(T):.0f} %)")
if rssi:
    print(f"  RSSI dos que chegaram: mediana {statistics.median(rssi):.0f} dBm (min {min(rssi):.0f}, max {max(rssi):.0f})")
if perdas_usq:
    print(f"  instante no quadro dos PERDIDOS: mediana {statistics.median(perdas_usq)/1000:.0f} ms (min {min(perdas_usq)/1000:.0f}, max {max(perdas_usq)/1000:.0f})")
if ok_usq:
    print(f"  instante no quadro dos que CHEGARAM: mediana {statistics.median(ok_usq)/1000:.0f} ms (min {min(ok_usq)/1000:.0f}, max {max(ok_usq)/1000:.0f})")
print(f"  perdidos num segundo em que o receptor tambem transmitiu: {perdas_com_meu_tx} de {cont['PERDIDO']}")
# perdas em sequencia: rajadas indicam sombra/distancia, perdas soltas indicam colisao/timing
seqs, run = [], 0
for t in T:
    if achou(t) is None:
        run += 1
    elif run:
        seqs.append(run); run = 0
if run: seqs.append(run)
if seqs:
    print(f"  rajadas de perda: {len(seqs)} | maior {max(seqs)} seguidas | soltas (1) {seqs.count(1)}")
# o sync de quem recebe ao longo do tempo
syncs = collections.Counter(e['sync'] for e in Erx)
print(f"  sync do receptor (0 nada, 1 beacon, 2 PPS) em segundos: {dict(syncs)}")
syncs = collections.Counter(e['sync'] for e in Etx if ini <= e['utc'] <= fim)
print(f"  sync do transmissor no mesmo periodo: {dict(syncs)}")
