"""Desenho estilo Tinkercad da montagem MINIMA (plaquinha de antes + antenas externas).

    python -I hardware/protoboard/visual_tinkercad.py  ->  montagem_tinkercad.svg (e .png, se achar o Chrome)

Ilustrativo, fora de escala. Pinos conferidos em docs/MONTAGEM.md (esquematico da 7B)
e firmware/mts/hal/pinos.h. O que e obrigatorio e o que e opcional: LISTA_FINAL.md.
"""
import os
import subprocess

AQUI = os.path.dirname(os.path.abspath(__file__))
W, H = 2170, 1440
S = []
FONTE = "Segoe UI, Arial, sans-serif"
COR = dict(spi="#2fb344", ctl="#2f80ed", gnd="#2b2b2b", v5="#e5383b", v33="#f59f00", gps="#9b51e0", rf="#1e1e1e")


def add(x):
    S.append(x)


def txt(x, y, t, size=14, cor="#1d2733", peso=400, anc="start"):
    t = t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    add(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{FONTE}" font-size="{size}" fill="{cor}" '
        f'font-weight="{peso}" text-anchor="{anc}">{t}</text>')


def etiqueta(x, y, t, cor, size=12, anc="start"):
    """texto com fundo branco, para ficar legivel por cima de fio"""
    w = len(t) * size * 0.56 + 10
    x0 = x - (w if anc == "end" else w / 2 if anc == "middle" else 0)
    add(f'<rect x="{x0:.1f}" y="{y - size:.1f}" width="{w:.1f}" height="{size + 7}" rx="4" fill="#ffffffe6" stroke="{cor}" stroke-width="1"/>')
    txt(x0 + 5, y + 1, t, size, cor, 700)


def fio(pts, cor, w=6, raio=14):
    """fio de Tinkercad: segmentos retos com dobras arredondadas, contorno escuro."""
    d = f"M{pts[0][0]:.1f},{pts[0][1]:.1f}"
    for i in range(1, len(pts) - 1):
        (x0, y0), (x1, y1), (x2, y2) = pts[i - 1], pts[i], pts[i + 1]
        import math
        l1 = math.hypot(x1 - x0, y1 - y0) or 1
        l2 = math.hypot(x2 - x1, y2 - y1) or 1
        r = min(raio, l1 / 2, l2 / 2)
        ax, ay = x1 - (x1 - x0) / l1 * r, y1 - (y1 - y0) / l1 * r
        bx, by = x1 + (x2 - x1) / l2 * r, y1 + (y2 - y1) / l2 * r
        d += f" L{ax:.1f},{ay:.1f} Q{x1:.1f},{y1:.1f} {bx:.1f},{by:.1f}"
    d += f" L{pts[-1][0]:.1f},{pts[-1][1]:.1f}"
    add(f'<path d="{d}" fill="none" stroke="#00000059" stroke-width="{w + 3}" stroke-linecap="round" stroke-linejoin="round"/>')
    add(f'<path d="{d}" fill="none" stroke="{cor}" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/>')
    add(f'<path d="{d}" fill="none" stroke="#ffffff40" stroke-width="{max(1, w / 3):.1f}" stroke-linecap="round" stroke-linejoin="round" transform="translate(-1,-1)"/>')
    for x, y in (pts[0], pts[-1]):
        add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{w * 0.6:.1f}" fill="#c0c4c9" stroke="#444" stroke-width="1"/>')


def badge(x, y, n):
    add(f'<circle cx="{x}" cy="{y}" r="12" fill="#e8590c" stroke="#ffffff" stroke-width="2"/>')
    txt(x, y + 5, str(n), 13, "#ffffff", 900, "middle")


# ================================================================== defs
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">')
add("""<defs>
<linearGradient id="fundo" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f1f5f9"/><stop offset="1" stop-color="#dde5ee"/></linearGradient>
<linearGradient id="metal" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#f1f3f5"/><stop offset=".55" stop-color="#bcc3cb"/><stop offset="1" stop-color="#dfe3e8"/></linearGradient>
<linearGradient id="tela" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#50565f"/><stop offset="1" stop-color="#2b2f35"/></linearGradient>
<linearGradient id="pcbazul" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2367b5"/><stop offset="1" stop-color="#153f72"/></linearGradient>
<linearGradient id="capel" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#16233f"/><stop offset=".45" stop-color="#43629f"/><stop offset="1" stop-color="#101a30"/></linearGradient>
<linearGradient id="ceram" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#c98a3c"/><stop offset=".5" stop-color="#ecb86b"/><stop offset="1" stop-color="#b5762b"/></linearGradient>
<linearGradient id="indut" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5c5c5c"/><stop offset="1" stop-color="#1f1f1f"/></linearGradient>
<linearGradient id="tubo" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#2a2d31"/><stop offset=".4" stop-color="#6c727a"/><stop offset="1" stop-color="#25282c"/></linearGradient>
<linearGradient id="ouro" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f3d572"/><stop offset="1" stop-color="#b8901f"/></linearGradient>
<pattern id="furos" width="20" height="20" patternUnits="userSpaceOnUse"><rect width="20" height="20" fill="#dcbc80"/><circle cx="10" cy="10" r="5" fill="#cf9d50"/><circle cx="10" cy="10" r="2.2" fill="#5b4320"/></pattern>
<filter id="sombra" x="-15%" y="-15%" width="140%" height="140%"><feDropShadow dx="3" dy="6" stdDeviation="6" flood-color="#0b1a2b" flood-opacity=".28"/></filter>
</defs>""")
add(f'<rect width="{W}" height="{H}" fill="url(#fundo)"/>')
txt(40, 54, "MTS — montagem de um carro / UTV", 32, peso=800)
txt(40, 84, "A plaquinha de antes + antena LoRa e antena do GPS LONGE da tela. Ilustrativo, fora de escala. "
    "Pinos pelo esquemático da 7B (docs/MONTAGEM.md).", 16, "#4a5868")

# ================================================================== TELA
tx, ty, tw, th = 40, 120, 560, 800
add(f'<g filter="url(#sombra)"><rect x="{tx}" y="{ty}" width="{tw}" height="{th}" rx="22" fill="url(#tela)"/></g>')
add(f'<rect x="{tx + 16}" y="{ty + 16}" width="{tw - 32}" height="{th - 32}" rx="12" fill="none" stroke="#6f767f" stroke-width="2" stroke-dasharray="7 7"/>')
for k in range(5):
    add(f'<rect x="{tx + 40}" y="{ty + 120 + k * 16}" width="160" height="5" rx="2" fill="#3e434a"/>')
txt(tx + 34, ty + 58, "TELA ESP32-P4 7\"", 24, "#ffffff", 800)
txt(tx + 34, ty + 86, "Waveshare 7B — vista de TRÁS", 15, "#c4cad1")
add(f'<rect x="{tx + 34}" y="{ty + th - 70}" width="64" height="26" rx="12" fill="#a4acb5" stroke="#1d1d1d" stroke-width="2"/>')
add(f'<rect x="{tx + 46}" y="{ty + th - 62}" width="40" height="10" rx="5" fill="#2b2f35"/>')
txt(tx + 110, ty + th - 51, "USB-C — alimentação 5 V", 14, "#e8ebef", 600)

P3 = {1: "IO36", 2: "IO34", 3: "IO31  MISO", 4: "IO30  SCK", 5: "IO29  MOSI", 6: "IO28  NSS", 7: "IO5  RXEN",
      8: "IO4  ← GPS TXD", 9: "IO3  → GPS RXD", 10: "IO2  PPS", 11: "GND", 12: "3V3"}
P1 = {1: "IO52  TXEN", 2: "IO51  NRST", 3: "IO50  DIO1", 4: "IO49  BUSY", 5: "IO48", 6: "IO47", 7: "IO46",
      8: "GND", 9: "LDO_VO4", 10: "3V3", 11: "GND", 12: "BAT"}
USADOS = {"P3": {3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, "P1": {1, 2, 3, 4, 8, 11}}
pino = {}
PASSO = 22


def header(nome, mapa, x, y0):
    add(f'<rect x="{x - 30}" y="{y0 - 20}" width="40" height="{12 * PASSO + 16}" rx="5" fill="#f4f1e8" stroke="#8d8676" stroke-width="2"/>')
    txt(x - 10, y0 - 30, f"{nome} (PH2.0, 12 vias)", 15, "#ffffff", 800, "end")
    for i in range(1, 13):
        y = y0 + (i - 1) * PASSO
        u = i in USADOS[nome]
        add(f'<rect x="{x - 21}" y="{y - 8}" width="20" height="16" rx="3" fill="{"url(#ouro)" if u else "#cfc9b8"}" stroke="#6d6553"/>')
        txt(x - 40, y + 5, str(i), 13, "#ffffff" if u else "#8b919a", 700, "end")
        txt(x - 62, y + 5, mapa[i], 13, "#ffffff" if u else "#7d838c", 600 if u else 300, "end")
        pino[(nome, i)] = (x - 1, y)


HX = tx + tw - 12
header("P3", P3, HX, ty + 180)
header("P1", P1, HX, ty + 180 + 12 * PASSO + 60)
txt(tx + 34, ty + th - 100, "Na placa, a ordem impressa é o INVERSO da numeração:", 13, "#f0c674", 600)
txt(tx + 34, ty + th - 82, "confira o pino pelo GPIO escrito ao lado de cada furo.", 13, "#f0c674", 600)

# ================================================================== PLACA DO RADIO + E22
bx, by, bw, bh = 790, 150, 780, 720
add(f'<g filter="url(#sombra)"><rect x="{bx}" y="{by}" width="{bw}" height="{bh}" rx="12" fill="url(#furos)" stroke="#8a6a35" stroke-width="3"/></g>')
txt(bx + 16, by + 30, "PLACA DO RÁDIO — placa perfurada, tudo SOLDADO", 16, "#3d2c10", 800)
txt(bx + 16, by + 50, "(nada de protoboard de molas no 5 V e no GND)", 13, "#5b4320", 600)
# barramento de GND
GY = by + bh - 46
GX = bx + bw - 34
add(f'<rect x="{bx + 40}" y="{GY - 6}" width="{GX - bx - 34}" height="12" rx="6" fill="#c07a33" stroke="#6b3f12"/>')
add(f'<rect x="{GX - 6}" y="{by + 80}" width="12" height="{GY - by - 74}" rx="6" fill="#c07a33" stroke="#6b3f12"/>')
txt(GX - 20, GY - 16, "barramento de GND — fio de cobre nu", 13, "#3d2c10", 700, "end")

ex, ey, ew, eh = 1040, 250, 250, 380
add(f'<g filter="url(#sombra)"><rect x="{ex}" y="{ey}" width="{ew}" height="{eh}" rx="8" fill="url(#metal)" stroke="#5b636c" stroke-width="2.5"/></g>')
add(f'<rect x="{ex + 80}" y="{ey + 120}" width="{ew - 160}" height="120" rx="5" fill="#ffffff" stroke="#a5adb6"/>')
txt(ex + ew / 2, ey + 158, "EBYTE", 17, "#1e3a5f", 900, "middle")
txt(ex + ew / 2, ey + 182, "E22-", 14, "#1d2733", 700, "middle")
txt(ex + ew / 2, ey + 200, "900M30S", 14, "#1d2733", 700, "middle")
txt(ex + ew / 2, ey + 224, "1 W", 12, "#4a5868", 600, "middle")
txt(ex + ew / 2, ey - 12, "E22-900M30S — etiqueta para cima", 14, "#3d2c10", 800, "middle")
add(f'<circle cx="{ex + ew / 2}" cy="{ey + eh - 24}" r="10" fill="url(#ouro)" stroke="#7a5f0f" stroke-width="2"/>')
txt(ex + ew / 2, ey + eh - 40, "IPX: não usar", 11, "#5b4a12", 600, "middle")

NOME = {1: "GND", 2: "GND", 3: "GND", 4: "GND", 5: "GND", 6: "RXEN", 7: "TXEN", 8: "DIO2", 9: "VCC", 10: "VCC", 11: "GND",
        12: "GND", 13: "DIO1", 14: "BUSY", 15: "NRST", 16: "MISO", 17: "MOSI", 18: "SCK", 19: "NSS", 20: "GND", 21: "ANT", 22: "GND"}
ESQ = [12, 13, 14, 15, 16, 17, 18, 19, None, 20, 21, 22]
DIR = [11, 10, 9, 8, 7, 6, 5, 4, None, 3, 2, 1]
pad = {}
for k, (a, b) in enumerate(zip(ESQ, DIR)):
    y = ey + 26 + k * 29
    for p, x, lado in ((a, ex, -1), (b, ex + ew, 1)):
        if p is None:
            continue
        g = NOME[p] == "GND"
        add(f'<rect x="{x - 11}" y="{y - 8}" width="22" height="16" rx="2" fill="{"#a7aeb6" if g else "url(#ouro)"}" stroke="#555"/>')
        rot = f"{p} {NOME[p]}" if lado < 0 else f"{NOME[p]} {p}"
        txt(x - lado * 16, y + 5, rot, 12, "#5d6670" if g else "#14202e", 500 if g else 800, "start" if lado < 0 else "end")
        pad[p] = (x + lado * 11, y)
txt(ex + 40, ey + 26 + 8 * 29 + 5, "", 10)

# ---------------- sinais tela -> pads da esquerda
sinais = [(("P3", 3), 16, COR["spi"], 700), (("P3", 4), 18, COR["spi"], 716), (("P3", 5), 17, COR["spi"], 732),
          (("P3", 6), 19, COR["spi"], 748),
          (("P1", 2), 15, COR["ctl"], 940), (("P1", 3), 13, COR["ctl"], 960), (("P1", 4), 14, COR["ctl"], 980)]
for hp, pd, cor, lane in sinais:
    (x1, y1), (x2, y2) = pino[hp], pad[pd]
    fio([(x1, y1), (lane, y1), (lane, y2), (x2, y2)], cor, 5)
# TXEN / RXEN: por baixo do modulo ate o lado direito
TX_LANE = {7: (650, ey + eh + 50, ex + ew + 70), 6: (668, ey + eh + 70, ex + ew + 92)}
for hp, pd in ((("P1", 1), 7), (("P3", 7), 6)):
    (x1, y1), (x2, y2) = pino[hp], pad[pd]
    lane, ybaixo, xsobe = TX_LANE[pd]
    fio([(x1, y1), (lane, y1), (lane, ybaixo), (xsobe, ybaixo), (xsobe, y2), (x2, y2)], COR["ctl"], 5)
etiqueta(ex + 20, ey + eh + 40, "TXEN (P1-1) e RXEN (P3-7) passam por baixo do E22", COR["ctl"], 12)

# ---------------- GND
# fios de GND do cabo da tela (P1-8, P1-11, P3-11) -> barramento
for i, hp in enumerate((("P3", 11), ("P1", 8), ("P1", 11))):
    x1, y1 = pino[hp]
    lane = 620 + i * 10
    fio([(x1, y1), (lane, y1), (lane, GY + 0), (bx + 46, GY)], COR["gnd"], 4)
# GND do lado esquerdo do E22 (12, 20, 22): descem ao barramento
for i, p in enumerate((12, 20, 22)):
    x, y = pad[p]
    xs = ex - 40 - i * 0
for p in (20, 22):
    x, y = pad[p]
    fio([(x, y), (ex - 30, y), (ex - 30, GY)], COR["gnd"], 3)
x12, y12 = pad[12]
fio([(x12, y12), (ex - 22, y12), (ex - 22, ey - 40), (GX, ey - 40)], COR["gnd"], 3)
# GND do lado direito (11 e 5..1) -> barramento da direita
for p in (11, 5, 4, 3, 2, 1):
    x, y = pad[p]
    if p in (5, 4):
        fio([(x, y), (ex + ew + 40, y), (ex + ew + 40, y + 0)], COR["gnd"], 3)
    else:
        fio([(x, y), (GX, y)], COR["gnd"], 3)
x5, y5 = pad[5]; x4, y4 = pad[4]; x3, y3 = pad[3]
fio([(ex + ew + 40, y5), (ex + ew + 40, y3)], COR["gnd"], 3)
etiqueta(ex + ew + 120, pad[1][1] + 30, "pads 1–5, 11, 12, 20, 22 = GND", "#2b2b2b", 12)

# ---------------- 5 V, capacitores e pull-downs (lado direito)
x10, y10 = pad[10]; x9, y9 = pad[9]
NO = ex + ew + 28
fio([(x10, y10), (NO, y10), (NO, y9), (x9, y9)], COR["v5"], 5, raio=6)
# ceramico 10 uF entre o VCC e o GND 11, colado nos pads
cx, cy = NO + 22, (pad[11][1] + y10) / 2
add(f'<rect x="{cx - 9}" y="{cy - 15}" width="18" height="30" rx="4" fill="url(#ceram)" stroke="#7a4a12" stroke-width="1.5"/>')
fio([(NO, y10), (cx, y10), (cx, cy + 15)], COR["v5"], 3, raio=4)
add(f'<rect x="{cx - 9}" y="{cy - 15}" width="18" height="30" rx="4" fill="url(#ceram)" stroke="#7a4a12" stroke-width="1.5"/>')
txt(cx, cy + 4, "106", 9, "#3d2508", 800, "middle")
badge(cx + 22, cy - 16, 1)
# eletrolitico 470 uF deitado entre VCC e o barramento
ey9 = y9 + 0
e1, e2 = ex + ew + 160, ex + ew + 250
fio([(NO, y9), (e1, y9)], COR["v5"], 5, raio=4)
add(f'<g filter="url(#sombra)"><rect x="{e1}" y="{y9 - 24}" width="{e2 - e1}" height="48" rx="12" fill="url(#capel)" stroke="#0d1526" stroke-width="2"/></g>')
add(f'<rect x="{e2 - 22}" y="{y9 - 24}" width="12" height="48" fill="#c9d3e6" opacity=".9"/>')
txt(e2 - 16, y9 + 6, "–", 18, "#1b2a4a", 900, "middle")
txt((e1 + e2) / 2 - 8, y9 - 2, "470 µF", 12, "#ffffff", 800, "middle")
txt((e1 + e2) / 2 - 8, y9 + 13, "16 V", 10, "#dce4f3", 600, "middle")
fio([(e2, y9), (GX, y9)], COR["gnd"], 4)
badge(e2 + 16, y9 - 26, 2)


# pull-down 10k
def resistor_h(x1, x2, y, rot, cor_rot):
    add(f'<rect x="{x1}" y="{y - 8}" width="{x2 - x1}" height="16" rx="8" fill="#ecd7aa" stroke="#7a5f2a" stroke-width="1.5"/>')
    for k, c in enumerate(("#8b4513", "#111111", "#ff7f00", "#d4af37")):
        add(f'<rect x="{x1 + 9 + k * 9 + (6 if k == 3 else 0)}" y="{y - 8}" width="5" height="16" fill="{c}"/>')
    pass


for pd, rot in ((7, "10 kΩ TXEN→GND (obrigatório)"), (6, "10 kΩ RXEN→GND (recomendado)")):
    x2, y2 = pad[pd]
    xsobe = TX_LANE[pd][2]
    r1, r2 = xsobe + 40, xsobe + 100
    fio([(xsobe, y2), (r1, y2)], COR["ctl"], 3)
    resistor_h(r1, r2, y2, rot, "#5b3d0b")
    badge(r2 + 14, y2 - 18, 3 if pd == 7 else 4)
    fio([(r2, y2), (GX, y2)], COR["gnd"], 3)
x8, y8 = pad[8]
txt(ex + ew + 20, y8 + 5, "DIO2: não liga", 11, "#8b919a", 600)

# ---------------- antena LoRa: pad 21 -> RG316 -> SMA de painel
x21, y21 = pad[21]
smx, smy = 960, by + bh
fio([(x21, y21), (ex - 60, y21), (ex - 60, y21 + 60), (smx, y21 + 60), (smx, smy - 22)], COR["rf"], 6)
add(f'<rect x="{smx - 20}" y="{smy - 24}" width="40" height="30" rx="4" fill="url(#ouro)" stroke="#7a5f0f" stroke-width="2"/>')
add(f'<rect x="{smx - 13}" y="{smy + 6}" width="26" height="26" rx="3" fill="url(#ouro)" stroke="#7a5f0f"/>')
add(f'<circle cx="{smx}" cy="{smy + 19}" r="5" fill="#7a5f0f"/>')
badge(smx + 34, smy - 30, 5)

NOTAS = ["10 µF cerâmico \"106\", a ≤ 5 mm dos pads 9/10/11",
         "470 µF (o seu) a ≤ 15 mm · faixa (–) no GND",
         "10 kΩ do TXEN ao GND — OBRIGATÓRIO",
         "10 kΩ do RXEN ao GND — recomendado",
         "RG316: vivo no 21, malha no 22 → SMA fêmea (furo)"]
nx, ny = 1808, 1310
add(f'<g filter="url(#sombra)"><rect x="{nx - 18}" y="{ny - 40}" width="{2160 - nx}" height="{len(NOTAS) * 24 + 40}" rx="14" fill="#ffffff" stroke="#e8590c" stroke-width="1.5"/></g>')
txt(nx - 2, ny - 18, "NA PLACA DO RÁDIO", 14, "#e8590c", 800)
for k, t in enumerate(NOTAS):
    badge(nx, ny - 4 + k * 24, k + 1)
    txt(nx + 18, ny + 1 + k * 24, t, 11, "#2a2f36", 600)

# ================================================================== ANTENA LoRa no santo-antonio
MX = 1760
add(f'<rect x="{MX - 15}" y="140" width="30" height="760" rx="15" fill="url(#tubo)"/>')
add(f'<rect x="{MX - 15}" y="140" width="330" height="30" rx="15" fill="url(#tubo)"/>')
txt(MX + 24, 420, "santo-antônio", 14, "#3b3f45", 800)
txt(MX + 24, 438, "do UTV", 14, "#3b3f45", 800)
AX = 1910
add(f'<g filter="url(#sombra)"><rect x="{AX - 10}" y="190" width="20" height="250" rx="10" fill="#1d2023"/></g>')
add(f'<rect x="{AX - 4}" y="200" width="4" height="230" rx="2" fill="#3c4046"/>')
add(f'<rect x="{AX - 15}" y="436" width="30" height="24" rx="4" fill="#5c6168" stroke="#222"/>')
for yy in (250, 330):
    add(f'<rect x="{AX - 32}" y="{yy}" width="64" height="13" rx="5" fill="#a9b0b8" stroke="#333"/>')
    add(f'<rect x="{MX + 10}" y="{yy + 3}" width="{AX - MX - 40}" height="7" fill="#a9b0b8" stroke="#333"/>')
txt(AX + 30, 230, "ANTENA LoRa 915 MHz", 17, "#1d2733", 800)
txt(AX + 30, 252, "dipolo ou colinear,", 14, "#4a5868")
txt(AX + 30, 270, "2–5 dBi, SMA macho", 14, "#4a5868")
txt(AX + 30, 288, "(não precisa de chapa", 14, "#4a5868")
txt(AX + 30, 306, "de metal embaixo)", 14, "#4a5868")
txt(AX + 30, 330, "≥ 1 m da tela,", 15, "#c0392b", 800)
txt(AX + 30, 350, "o mais alto possível", 15, "#c0392b", 800)
# coaxial
fio([(AX, 460), (AX, 905), (smx, 905), (smx, smy + 32)], COR["rf"], 8, raio=24)
etiqueta(1080, 930, "cabo coaxial da antena (≤ 3 m)", "#1e1e1e", 13)

# ================================================================== GPS
gx0, gy0 = 150, 980
add(f'<g filter="url(#sombra)"><rect x="{gx0}" y="{gy0}" width="230" height="140" rx="10" fill="url(#pcbazul)" stroke="#0e2c52" stroke-width="2"/></g>')
add(f'<rect x="{gx0 + 18}" y="{gy0 + 22}" width="74" height="74" rx="5" fill="#cbb57c" stroke="#6b5a2a" stroke-width="2"/>')
add(f'<rect x="{gx0 + 38}" y="{gy0 + 42}" width="34" height="34" rx="3" fill="#dccb95" stroke="#6b5a2a"/>')
add(f'<rect x="{gx0 + 110}" y="{gy0 + 26}" width="70" height="56" rx="3" fill="#c3cad2" stroke="#555"/>')
txt(gx0 + 145, gy0 + 52, "u-blox", 10, "#333", 600, "middle")
txt(gx0 + 145, gy0 + 68, "NEO-7M", 12, "#111", 800, "middle")
add(f'<circle cx="{gx0 + 205}" cy="{gy0 + 112}" r="9" fill="url(#ouro)" stroke="#7a5f0f"/>')
txt(gx0 + 205, gy0 + 134, "IPEX", 10, "#ffffff", 700, "middle")
txt(gx0 + 12, gy0 + 128, "GPS NEO-7M (mini)", 15, "#ffffff", 800)
GPS = [("VCC", ("P3", 12), COR["v33"]), ("GND", ("P3", 11), COR["gnd"]), ("TXD", ("P3", 8), COR["gps"]),
       ("RXD", ("P3", 9), COR["gps"]), ("PPS", ("P3", 10), COR["gps"])]
for k, (nm, hp, cor) in enumerate(GPS):
    x = gx0 + 24 + k * 36
    add(f'<rect x="{x - 6}" y="{gy0 - 0}" width="12" height="14" fill="url(#ouro)" stroke="#6d6553"/>')
    txt(x, gy0 + 34, nm, 11, "#ffffff", 800, "middle")
    x1, y1 = pino[hp]
    lane = 690 + k * 14
    fio([(x, gy0), (x, gy0 - 40 - k * 8), (lane, gy0 - 40 - k * 8), (lane, y1), (x1, y1)], cor, 4)
etiqueta(gx0 + 250, gy0 + 70, "TXD→P3-8 · RXD→P3-9 · PPS→P3-10 · GND→P3-11 · VCC→P3-12 (3V3)", "#6b2fa8", 12)
# antena GPS ativa
pgx, pgy = 600, 1190
fio([(gx0 + 205, gy0 + 121), (gx0 + 205, pgy), (pgx - 90, pgy)], COR["rf"], 5)
add(f'<g filter="url(#sombra)"><ellipse cx="{pgx}" cy="{pgy}" rx="80" ry="30" fill="#1b1b1b"/><ellipse cx="{pgx}" cy="{pgy - 9}" rx="68" ry="21" fill="#3b3b3b"/></g>')
txt(pgx, pgy - 4, "GPS", 14, "#e2e2e2", 900, "middle")
txt(pgx + 100, pgy - 8, "antena GPS ativa (se o seu módulo tiver IPEX)", 14, "#1d2733", 800)
txt(pgx + 100, pgy + 12, "virada para o céu, a 30–50 cm da antena LoRa", 14, "#4a5868")

# ================================================================== ALIMENTACAO
# bateria 12 V
bbx, bby = 830, 1000
add(f'<g filter="url(#sombra)"><rect x="{bbx}" y="{bby}" width="170" height="110" rx="12" fill="#2b2b2b"/></g>')
add(f'<rect x="{bbx + 22}" y="{bby - 14}" width="26" height="16" rx="3" fill="#e5383b"/>')
add(f'<rect x="{bbx + 122}" y="{bby - 14}" width="26" height="16" rx="3" fill="#6b6b6b"/>')
txt(bbx + 35, bby - 20, "+", 18, "#e5383b", 900, "middle"); txt(bbx + 135, bby - 20, "–", 18, "#333", 900, "middle")
txt(bbx + 85, bby + 55, "12 V do UTV", 16, "#ffffff", 800, "middle")
txt(bbx + 85, bby + 78, "fusível 2 A", 13, "#cfcfcf", 600, "middle")


def conversor(x, y, rot, sub):
    add(f'<g filter="url(#sombra)"><rect x="{x}" y="{y}" width="200" height="100" rx="8" fill="url(#pcbazul)" stroke="#0e2c52" stroke-width="2"/></g>')
    add(f'<rect x="{x + 76}" y="{y + 22}" width="54" height="54" rx="7" fill="url(#indut)"/>')
    add(f'<rect x="{x + 146}" y="{y + 20}" width="24" height="24" rx="2" fill="#2d6cdf" stroke="#0b2a66"/>')
    add(f'<circle cx="{x + 158}" cy="{y + 32}" r="6" fill="#d0d6dc"/>')
    add(f'<circle cx="{x + 34}" cy="{y + 50}" r="18" fill="url(#capel)"/>')
    txt(x + 6, y + 18, "IN+", 10, "#ffffff", 700); txt(x + 6, y + 94, "IN–", 10, "#ffffff", 700)
    txt(x + 194, y + 18, "OUT+", 10, "#ffffff", 700, "end"); txt(x + 194, y + 94, "OUT–", 10, "#ffffff", 700, "end")
    txt(x + 100, y + 124, rot, 14, "#1d2733", 800, "middle")
    txt(x + 100, y + 142, sub, 12, "#4a5868", 600, "middle")
    return (x, y + 14), (x, y + 88), (x + 200, y + 14), (x + 200, y + 88)


a_ip, a_in, a_op, a_on = conversor(1060, 1010, "conversor 60 V → TELA", "TPS54560 ajustado em 5,10 V")
b_ip, b_in, b_op, b_on = conversor(1330, 1010, "conversor 60 V → RÁDIO", "TPS54560 ajustado em 5,05 V")
fio([(bbx + 35, bby - 14), (bbx + 35, 975), (1040, 975), (1040, a_ip[1]), a_ip], COR["v5"], 5)
fio([(1040, 975), (1310, 975), (1310, b_ip[1]), b_ip], COR["v5"], 5)
fio([(bbx + 135, bby - 14), (bbx + 135, 990), (1030, 990), (1030, a_in[1]), a_in], COR["gnd"], 5)
fio([(1030, 990), (1300, 990), (1300, b_in[1]), b_in], COR["gnd"], 5)
# conversor A -> USB-C
fio([a_op, (1285, a_op[1]), (1285, 1160), (tx + 66, 1160), (tx + 66, ty + th - 44)], COR["v5"], 5)
etiqueta(tx + 90, 1150, "cabo USB-C curto e grosso (AWG 20–22)", "#c0392b", 12)
# conversor B -> VCC do E22 / GND
fio([b_op, (1700, b_op[1]), (1700, 120), (NO + 6, 120), (NO + 6, y10 - 2)], COR["v5"], 6)
fio([b_on, (1720, b_on[1]), (1720, GY + 60), (GX, GY + 60), (GX, GY)], COR["gnd"], 6)
etiqueta(1230, 112, "5 V do rádio: AWG 20, + e – trançados, ≤ 30 cm", "#c0392b", 12)

# ================================================================== quadro de pecas
lx, ly = 1790, 935
add(f'<g filter="url(#sombra)"><rect x="{lx}" y="{ly}" width="360" height="320" rx="14" fill="#ffffff" stroke="#c5ced8"/></g>')
txt(lx + 18, ly + 32, "PEÇAS (por carro)", 17, "#1d2733", 800)
itens = ["E22-900M30S", "470 µF (o seu) + 10 µF cerâmico", "10 kΩ no TXEN (obrigatório)", "10 kΩ no RXEN (recomendado)",
         "RG316 + SMA fêmea de painel", "antena LoRa 915 dipolo 2–5 dBi", "antena GPS ativa (se tiver IPEX)",
         "2× conversor TPS54560 (60 V)", "fusível 2 A"]
for k, it in enumerate(itens):
    txt(lx + 20, ly + 58 + k * 21, "• " + it, 13, "#2a3542")
leg = [(COR["spi"], "SPI"), (COR["ctl"], "controle"), (COR["gnd"], "GND"), (COR["v5"], "5 V"), (COR["v33"], "3V3"), (COR["gps"], "GPS")]
for k, (c, n) in enumerate(leg):
    xx, yy = lx + 20 + (k % 3) * 92, ly + 262 + (k // 3) * 20 - 12
    add(f'<rect x="{xx}" y="{yy - 9}" width="24" height="8" rx="4" fill="{c}"/>')
    txt(xx + 30, yy - 1, n, 12, "#2a3542", 600)

add("</svg>")
svg = os.path.join(AQUI, "montagem_tinkercad.svg")
open(svg, "w", encoding="utf-8").write("\n".join(S))
print(svg)
for chrome in (r"C:\Program Files\Google\Chrome\Application\chrome.exe",
               r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"):
    if os.path.exists(chrome):
        png = os.path.join(AQUI, "montagem_tinkercad.png")
        subprocess.run([chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size={W},{H}",
                        f"--screenshot={png}", "file:///" + svg.replace("\\", "/")], capture_output=True, timeout=60)
        print(png)
        break
