# Cruza os CSVs de campo de dois nos (registro.h do mts_campo) pela hora UTC.
#
# POR QUE INTERPOLA: no v100 o log ficava mudo dentro da tela da trilha (corrigido
# no v101 - servico.h), entao as linhas E saiam esparsas e em instantes diferentes
# em cada no. Os contadores sao acumulados, entao interpolar linearmente entre
# duas amostras da o valor do outro no no instante desta. Continua valendo para
# o v101 (as 21 primeiras colunas de E sao as mesmas); para as linhas novas
# (R/T/N, perda contada por sequencia) use analisa_v101.py.
#
#   python tools\campo\cruza_logs.py dados_campo\cartao1\230217_249.csv dados_campo\cartao2\230911_11.csv
import csv, math, sys

COL = dict(ms=1, utc=2, lat=3, lon=4, tx=8, perdi=9, preso=10, rx=11, ruim=12, confl=13, rssi=14, pwr=15, fix=16)

def le(caminho):
    nome, linhas = "?", []
    for r in csv.reader(open(caminho, encoding="utf-8", errors="replace")):
        if not r or r[0].startswith("#"): continue
        if r[0] == "!" and len(r) > 3 and r[2] == "boot": nome = r[3]
        if r[0] != "E" or len(r) < 17 or r[COL["fix"]] != "1": continue
        linhas.append({k: float(r[i]) for k, i in COL.items()})
    return nome, linhas

def interp(ls, utc, campo):
    for a, b in zip(ls, ls[1:]):
        if a["utc"] <= utc <= b["utc"]:
            if b["utc"] == a["utc"]: return a[campo]
            f = (utc - a["utc"]) / (b["utc"] - a["utc"])
            return a[campo] + f * (b[campo] - a[campo])
    return None

def dist(la1, lo1, la2, lo2):
    dy = (la2 - la1) * 111320.0
    dx = (lo2 - lo1) * 111320.0 * math.cos(math.radians(la1))
    return math.hypot(dx, dy)

def hora(utc):
    s = int(utc) - 3 * 3600          # Brasilia
    return "%02d:%02d:%02d" % (s // 3600 % 24, s // 60 % 60, s % 60)

na, A = le(sys.argv[1])
nb, B = le(sys.argv[2])
print(f"A = {na}: {len(A)} amostras com fix   B = {nb}: {len(B)} amostras com fix\n")
print(f"{'hora':>8} {'dist':>6} | {'A->B':>9} {'rssi B':>6} | {'B->A':>9} {'rssi A':>6} | ruim A/B")

prev = None
tot = dict(ab_env=0, ab_rec=0, ba_env=0, ba_rec=0)
faixas = {}
for b in B:
    u = b["utc"]
    vals = {c: interp(A, u, c) for c in ("lat", "lon", "tx", "rx", "ruim", "rssi")}
    if vals["lat"] is None: continue
    cur = dict(u=u, d=dist(vals["lat"], vals["lon"], b["lat"], b["lon"]),
               atx=vals["tx"], arx=vals["rx"], aruim=vals["ruim"],
               btx=b["tx"], brx=b["rx"], bruim=b["ruim"], brssi=b["rssi"], arssi=vals["rssi"])
    if prev and cur["u"] - prev["u"] >= 5:
        ab_env = cur["atx"] - prev["atx"]; ab_rec = cur["brx"] - prev["brx"]
        ba_env = cur["btx"] - prev["btx"]; ba_rec = cur["arx"] - prev["arx"]
        dm = (cur["d"] + prev["d"]) / 2
        pct = lambda r, e: f"{100*r/e:3.0f}%" if e > 0.5 else "  - "
        print(f"{hora(u):>8} {dm:6.0f} | {ab_rec:3.0f}/{ab_env:3.0f} {pct(ab_rec,ab_env)} {cur['brssi']:6.0f} | "
              f"{ba_rec:3.0f}/{ba_env:3.0f} {pct(ba_rec,ba_env)} {cur['arssi']:6.0f} | "
              f"{cur['aruim']-prev['aruim']:.0f}/{cur['bruim']-prev['bruim']:.0f}")
        tot["ab_env"] += ab_env; tot["ab_rec"] += ab_rec; tot["ba_env"] += ba_env; tot["ba_rec"] += ba_rec
        fx = int(dm // 250) * 250
        f = faixas.setdefault(fx, [0, 0, 0, 0])
        f[0] += ab_env + ba_env; f[1] += ab_rec + ba_rec
    if not prev or cur["u"] - prev["u"] >= 5: prev = cur

print(f"\nTOTAL {na} -> {nb}: {tot['ab_rec']:.0f}/{tot['ab_env']:.0f} = {100*tot['ab_rec']/max(tot['ab_env'],1):.0f}%")
print(f"TOTAL {nb} -> {na}: {tot['ba_rec']:.0f}/{tot['ba_env']:.0f} = {100*tot['ba_rec']/max(tot['ba_env'],1):.0f}%")
print("\nentrega por faixa de distancia (os dois sentidos somados):")
for fx in sorted(faixas):
    e, r = faixas[fx][0], faixas[fx][1]
    print(f"  {fx:5d}-{fx+250:<5d} m: {r:4.0f}/{e:4.0f} = {100*r/max(e,1):3.0f}%")
