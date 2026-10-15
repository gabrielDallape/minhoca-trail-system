"""Desenho ilustrativo da montagem da LISTA_FINAL.md (nao e gerber, nao e escala).

    python -I hardware/protoboard/desenha.py   ->  hardware/protoboard/montagem_visual.png

Tres quadros: a visao geral no carro, a placa-filtro (atras da tela) e a placa do
radio (na caixa de aluminio). Pinos conferidos em docs/MONTAGEM.md e hal/pinos.h.
"""
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle, Polygon

AQUI = os.path.dirname(os.path.abspath(__file__))
FR4, COBRE, TINTA = "#e9dfb4", "#d9a066", "#1d1d1d"
VERM, PRETO, AZUL, VERDE, ROXO, LARANJA, CINZA = "#d62728", "#222222", "#1f77b4", "#2ca02c", "#9467bd", "#ff7f0e", "#777777"


def caixa(ax, x, y, w, h, txt, cor="#ffffff", borda=TINTA, fs=9, peso="bold", raio=0.02, tc=TINTA):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle=f"round,pad=0,rounding_size={raio}",
                                fc=cor, ec=borda, lw=1.4, zorder=3))
    ax.text(x + w / 2, y + h / 2, txt, ha="center", va="center", fontsize=fs, weight=peso, zorder=4, color=tc)


def fio(ax, pts, cor, lw=2.2, z=2, ls="-"):
    xs, ys = zip(*pts)
    ax.plot(xs, ys, color=cor, lw=lw, zorder=z, solid_capstyle="round", ls=ls)


def resistor(ax, x, y, val, horiz=True, w=0.07, h=0.022):
    if horiz:
        ax.add_patch(Rectangle((x - w / 2, y - h / 2), w, h, fc="#f3e3c3", ec=TINTA, lw=1, zorder=5))
    else:
        ax.add_patch(Rectangle((x - h / 2, y - w / 2), h, w, fc="#f3e3c3", ec=TINTA, lw=1, zorder=5))
    ax.text(x, y, val, ha="center", va="center", fontsize=6.3, zorder=6, weight="bold")


def cap(ax, x, y, val, cor="#8c564b", s=0.018):
    ax.add_patch(Rectangle((x - s / 2, y - s / 2), s, s, fc=cor, ec=TINTA, lw=0.8, zorder=5))
    ax.text(x + s * 0.9, y, val, ha="left", va="center", fontsize=6, zorder=6)


def gnd(ax, x, y, s=0.012):
    for i, k in enumerate((1.0, 0.65, 0.3)):
        ax.plot([x - s * k, x + s * k], [y - i * s * 0.55, y - i * s * 0.55], color=TINTA, lw=1, zorder=5)


fig = plt.figure(figsize=(17, 12.5), dpi=120)
fig.suptitle("MTS - como fica a montagem (ilustrativo, fora de escala)", fontsize=16, weight="bold", y=0.985)

# ============================================================== 1. VISAO GERAL
ax = fig.add_axes([0.02, 0.58, 0.96, 0.35]); ax.set_xlim(0, 2.6); ax.set_ylim(0, 1); ax.axis("off")
ax.set_title("1. No carro: uma entrada de 12 V, um ponto de terra (*), antena no teto", fontsize=12, weight="bold", loc="left")
# teto
ax.add_patch(Rectangle((1.55, 0.86), 1.0, 0.035, fc="#bbbbbb", ec=CINZA))
ax.text(1.70, 0.82, "TETO DO CARRO (fora)", ha="center", fontsize=8, color=CINZA, weight="bold")
ax.add_patch(Polygon([[2.25, 0.895], [2.27, 0.895], [2.262, 0.99], [2.258, 0.99]], fc=TINTA))
ax.add_patch(Circle((2.26, 0.9), 0.02, fc=CINZA, ec=TINTA))
ax.text(2.30, 0.965, "antena LoRa 915\n3-5 dBi, base magnetica", fontsize=7.5, va="center")
ax.add_patch(Circle((1.80, 0.9), 0.018, fc="#444", ec=TINTA))
ax.text(1.60, 0.965, "(fase 2) antena GPS ativa\n>= 30-50 cm da LoRa", fontsize=7, va="center", color=CINZA)

caixa(ax, 0.02, 0.38, 0.22, 0.22, "FUSIVEL ACC\n(derivador)\n+ fusivel 2 A", "#fff2cc")
caixa(ax, 0.32, 0.28, 0.36, 0.42, "PLACA DE ENTRADA\n\nTVS 1.5KE33CA\nSB560 (polaridade)\ntoroide 22 uH\n100 uF/63 V + 1 uF + 100 nF\n\n* PONTO ESTRELA (GND)", "#fde9d9", fs=8.5)
caixa(ax, 0.78, 0.60, 0.30, 0.17, "TPS54560 (60 V)\n-> 5,10 V  TELA", "#dbe9f6")
caixa(ax, 0.78, 0.21, 0.30, 0.17, "TPS54560 (60 V)\n-> 5,05 V  RADIO", "#f8d7d7")
fio(ax, [(0.24, 0.49), (0.32, 0.49)], VERM, 3); ax.text(0.28, 0.52, "12V\nAWG18", fontsize=6.5, ha="center")
fio(ax, [(0.68, 0.60), (0.73, 0.60), (0.73, 0.685), (0.78, 0.685)], VERM, 2.5)
fio(ax, [(0.68, 0.38), (0.73, 0.38), (0.73, 0.295), (0.78, 0.295)], VERM, 2.5)

# tela
caixa(ax, 1.25, 0.47, 0.42, 0.33, "TELA P4 7\"\n(caixa metalica)", "#e8e8e8", fs=10)
fio(ax, [(1.08, 0.685), (1.25, 0.685)], VERM, 2.5)
ax.add_patch(Rectangle((1.15, 0.665), 0.03, 0.04, fc="#555", ec=TINTA, zorder=5))
ax.text(1.165, 0.73, "ferrite\nclamp", fontsize=6.3, ha="center")
ax.text(1.165, 0.62, "USB-C AWG20", fontsize=6.3, ha="center")
# placa filtro
caixa(ax, 1.30, 0.30, 0.32, 0.12, "PLACA-FILTRO\n(colada no P1/P3)", "#e2f0d9", fs=8)
fio(ax, [(1.40, 0.47), (1.40, 0.42)], AZUL, 3); fio(ax, [(1.52, 0.47), (1.52, 0.42)], VERDE, 3)
ax.text(1.55, 0.445, "P1/P3 <=5 cm", fontsize=6.5)
# caixa do radio
caixa(ax, 1.80, 0.18, 0.40, 0.42, "", "#d9dde2", borda="#555", raio=0.01)
ax.text(2.0, 0.555, "CAIXA DE ALUMINIO", ha="center", fontsize=8, weight="bold", color="#333")
caixa(ax, 1.86, 0.25, 0.28, 0.24, "placa do radio\nE22-900M30S", "#f0c27b", fs=8.5)
fio(ax, [(1.62, 0.36), (1.86, 0.36)], AZUL, 3.5)
ax.add_patch(Rectangle((1.69, 0.34), 0.03, 0.04, fc="#555", ec=TINTA, zorder=5))
ax.text(1.74, 0.40, "Micro-Fit 12 vias\n(trava) + ferrite", fontsize=6.5, ha="center")
fio(ax, [(1.08, 0.295), (1.20, 0.295), (1.20, 0.22), (1.86, 0.22), (1.86, 0.28)], VERM, 2.5)
ax.text(1.45, 0.195, "5 V do radio: Micro-Fit 2 vias, AWG20 TRANCADO <= 30 cm", fontsize=6.8, color=VERM)
# coax
fio(ax, [(2.14, 0.40), (2.24, 0.40), (2.24, 0.895)], "#555", 2.5)
ax.add_patch(Rectangle((2.20, 0.385), 0.025, 0.03, fc="#c9a227", ec=TINTA, zorder=6))
ax.text(2.255, 0.60, "coaxial <= 3 m\n(RG58/LMR-200)", fontsize=7)
ax.text(2.17, 0.36, "SMA", fontsize=6.5)
# terras
fio(ax, [(0.5, 0.28), (0.5, 0.10), (2.0, 0.10), (2.0, 0.18)], PRETO, 1.6, ls="--")
fio(ax, [(0.93, 0.21), (0.93, 0.10)], PRETO, 1.6, ls="--"); fio(ax, [(0.93, 0.60), (0.93, 0.55), (0.72, 0.55), (0.72, 0.36), (0.68, 0.36)], PRETO, 1.2, ls="--")
ax.text(1.0, 0.065, "GND de tudo volta ao * (um ponto so). O cabo USB-C corre AMARRADO junto do chicote (sem laco).", fontsize=7.5)
fio(ax, [(0.1, 0.38), (0.1, 0.1), (0.32, 0.1)], PRETO, 2.5); ax.text(0.02, 0.13, "GND carro\nAWG18", fontsize=6.5)

# ============================================================== 2. PLACA-FILTRO
ax = fig.add_axes([0.02, 0.03, 0.45, 0.54]); ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
ax.set_title("2. PLACA-FILTRO - colada atras da tela, entre P1/P3 e o chicote", fontsize=12, weight="bold", loc="left")
ax.add_patch(FancyBboxPatch((0.17, 0.09), 0.62, 0.84, boxstyle="round,pad=0,rounding_size=0.01", fc="#c8a96a", ec="#7a6232", lw=1.5))
for gx in range(30):
    for gy in range(40):
        ax.add_patch(Circle((0.19 + gx * 0.02, 0.11 + gy * 0.0205), 0.0045, fc="#e8c98a", ec="none", zorder=1))
# barramento GND
fio(ax, [(0.60, 0.08), (0.60, 0.90)], PRETO, 3.5, z=2); ax.text(0.605, 0.915, "GND", fontsize=7, weight="bold")

linhas = [  # (rotulo, conector, cor, valor R, tem C)
    ("SCK  IO30", "P3-4", VERDE, "330", False),
    ("MOSI IO29", "P3-5", VERDE, "330", False),
    ("NSS  IO28", "P3-6", VERDE, "1k", False),
    ("MISO IO31", "P3-3", VERDE, "330", False),
    ("NRST IO51", "P1-2", AZUL, "1k", False),
    ("TXEN IO52", "P1-1", AZUL, "1k", False),
    ("RXEN IO5", "P3-7", VERDE, "1k", False),
    ("BUSY IO49", "P1-4", AZUL, "1k", True),
    ("DIO1 IO50", "P1-3", AZUL, "1k", True),
    ("GPS PPS IO2", "P3-10", ROXO, "1k", True),
    ("GPS TX->IO4", "P3-8", ROXO, "1k", True),
    ("IO3->GPS RX", "P3-9", ROXO, "1k", False),
]
y0, dy = 0.86, 0.062
for i, (rot, con, cor, rv, tc) in enumerate(linhas):
    y = y0 - i * dy
    ax.text(0.005, y, f"{con}", fontsize=7.5, va="center", weight="bold", color=cor)
    ax.text(0.065, y, rot, fontsize=6.6, va="center")
    fio(ax, [(0.15, y), (0.78, y)], cor, 2)
    resistor(ax, 0.33, y, rv)
    if tc:
        fio(ax, [(0.46, y), (0.46, y - 0.02), (0.60, y - 0.02)], CINZA, 1, z=3)
        cap(ax, 0.50, y - 0.02, "100pF", s=0.016)
    ax.text(0.80, y, "-> Micro-Fit", fontsize=6.5, va="center", color=CINZA)
# GND e 3V3 do GPS
y = y0 - len(linhas) * dy
ax.text(0.005, y, "P1-8, P1-11, P3-11", fontsize=7, va="center", weight="bold"); fio(ax, [(0.15, y), (0.60, y)], PRETO, 2.5)
ax.text(0.80, y, "-> GND do chicote", fontsize=6.5, va="center", color=CINZA)
ax.text(0.0, 0.0, "R em serie: 1/4 W de perna.  100pF e 100nF: SMD 0805 NP0 (o de perna vira indutor a 915 MHz).\n"
        "BUSY: RC <= 150 ns (NUNCA 1 nF nem 10k).  3V3 do GPS (P3-12): 100nF + 100pF ao GND no conector.",
        fontsize=7, va="bottom")

# ============================================================== 3. PLACA DO RADIO
ax = fig.add_axes([0.50, 0.03, 0.49, 0.54]); ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.axis("off")
ax.set_title("3. PLACA DO RADIO - cobre inteiro = GND, dentro da caixa de aluminio", fontsize=12, weight="bold", loc="left")
ax.add_patch(FancyBboxPatch((0.03, 0.05), 0.94, 0.88, boxstyle="round,pad=0,rounding_size=0.01", fc=COBRE, ec="#8a5a2b", lw=2))
ax.text(0.5, 0.905, "placa cobreada virgem: o cobre e o plano de terra (GND do E22 solda direto nele)", ha="center", fontsize=7.5, style="italic")
# E22
ex, ey, ew, eh = 0.36, 0.20, 0.26, 0.62
ax.add_patch(Rectangle((ex, ey), ew, eh, fc="#aeb4bb", ec=TINTA, lw=1.5, zorder=4))
ax.text(ex + ew / 2, ey + eh / 2, "E22-900M30S\n(etiqueta p/ cima)\n\nfixar com\nRTV neutro", ha="center", va="center", fontsize=8.5, weight="bold", zorder=5)
esq = [12, 13, 14, 15, 16, 17, 18, 19, None, 20, 21, 22]
dir_ = [11, 10, 9, 8, 7, 6, 5, 4, None, 3, 2, 1]
nome = {1: "GND", 2: "GND", 3: "GND", 4: "GND", 5: "GND", 6: "RXEN", 7: "TXEN", 8: "DIO2", 9: "VCC", 10: "VCC", 11: "GND",
        12: "GND", 13: "DIO1", 14: "BUSY", 15: "NRST", 16: "MISO", 17: "MOSI", 18: "SCK", 19: "NSS", 20: "GND", 21: "ANT", 22: "GND"}
pady = {}
for k, (a, b) in enumerate(zip(esq, dir_)):
    y = ey + eh - 0.04 - k * 0.049
    for p, xp, ha, xt in ((a, ex - 0.012, "right", ex - 0.02), (b, ex + ew + 0.012, "left", ex + ew + 0.02)):
        if p is None:
            continue
        cor = "#555" if nome[p] == "GND" else "#e8e8e8"
        ax.add_patch(Rectangle((xp - 0.01, y - 0.009), 0.02, 0.018, fc=cor, ec=TINTA, lw=0.6, zorder=6))
        pady[p] = (xp, y)
# rotulos e ligacoes - lado esquerdo (sinais vindos do Micro-Fit)
caixa(ax, 0.05, 0.42, 0.09, 0.30, "Micro-\nFit\n12\nvias", "#333", tc="#fff", fs=7.5)
for p in (13, 14, 15, 16, 17, 18, 19):
    x, y = pady[p]
    fio(ax, [(0.14, y), (x - 0.01, y)], AZUL, 1.8, z=5)
    ax.text(0.22, y + 0.012, f"{nome[p]} ({p})", fontsize=6.5, ha="center")
for p in (12, 20, 22):
    x, y = pady[p]; ax.text(x - 0.016, y, f"GND {p}", fontsize=6, ha="right", va="center", color="#333", zorder=7)
# antena: pad 21 -> SMA
x, y = pady[21]
fio(ax, [(x - 0.01, y), (0.06, y)], "#555", 3, z=5)
ax.add_patch(Rectangle((0.03, y - 0.022), 0.035, 0.044, fc="#c9a227", ec=TINTA, zorder=6))
ax.text(0.10, y + 0.025, "RG316: vivo no 21, malha no 22\n-> SMA femea de painel (FURO no centro)", fontsize=6.3)
# lado direito: VCC, TXEN, RXEN
x9, y9 = pady[9]; x10, y10 = pady[10]
fio(ax, [(x9 + 0.01, y9), (x9 + 0.03, y9), (x9 + 0.03, y10), (x10 + 0.01, y10)], VERM, 3, z=5)
ax.text(x9 + 0.035, (y9 + y10) / 2, "", fontsize=6)
# banco colado
cx = 0.70
cap(ax, cx + 0.005, y10 + 0.03, "100nF", cor="#8c564b", s=0.018)
cap(ax, cx + 0.06, y10 + 0.03, "10uF X7R", cor="#8c564b", s=0.022)
ax.add_patch(Circle((cx + 0.19, y10 + 0.01), 0.04, fc="#1f3b73", ec=TINTA, zorder=5))
ax.text(cx + 0.19, y10 + 0.01, "470uF\n(ou 220)", ha="center", va="center", fontsize=6.3, color="white", weight="bold", zorder=6)
ax.text(cx - 0.02, y10 + 0.075, "<= 5 mm dos pads 9/10/11       <= 15 mm", fontsize=6.5, weight="bold", color=VERM)
fio(ax, [(x9 + 0.03, y10), (cx + 0.19, y10)], VERM, 2.5, z=4)
# ferrite e entrada 5V
fy = y9 - 0.07
fio(ax, [(x9 + 0.03, y9), (x9 + 0.03, fy), (0.80, fy)], VERM, 2.5, z=4)
ax.add_patch(Rectangle((0.73, fy - 0.014), 0.05, 0.028, fc="#555", ec=TINTA, zorder=6))
ax.text(0.755, fy + 0.03, "ferrite BLM31", fontsize=6.5, ha="center")
caixa(ax, 0.82, fy - 0.05, 0.12, 0.10, "Micro-Fit\n2 vias\n5V + GND", "#333", tc="#fff", fs=7)
ax.text(0.88, fy - 0.075, "do TPS54560 do radio\n(AWG20 trancado)", fontsize=6.3, ha="center")
# TXEN / RXEN pull-down
for p in (7, 6):
    x, y = pady[p]
    fio(ax, [(x + 0.01, y), (0.80, y)], AZUL, 1.8, z=5)
    resistor(ax, 0.72, y - 0.0, "10k", horiz=False, w=0.045, h=0.018) if False else None
    ax.add_patch(Rectangle((0.695, y - 0.045), 0.018, 0.04, fc="#f3e3c3", ec=TINTA, zorder=6))
    ax.text(0.718, y - 0.025, "10k->GND", fontsize=6.2, va="center")
    fio(ax, [(0.704, y), (0.704, y - 0.005)], AZUL, 1.2, z=5)
    ax.text(x + 0.03, y + 0.013, f"{nome[p]} ({p})", fontsize=6.5)
    fio(ax, [(0.80, y), (0.80, 0.12), (0.14, 0.12), (0.14, 0.42)], AZUL, 1.2, z=3, ls=":")
ax.text(0.5, 0.135, "TXEN/RXEN vem do Micro-Fit (pontilhado)", fontsize=6.3, ha="center", color=AZUL)
x8, y8 = pady[8]; ax.text(x8 + 0.03, y8, "DIO2: nao liga", fontsize=6.3, va="center", color=CINZA)
ax.text(0.5, 0.07, "Pads 1-5, 11, 12, 20, 22 = GND: soldados no cobre. Antena (ou carga 50 ohm) SEMPRE ligada antes do 5 V.",
        ha="center", fontsize=7)

out = os.path.join(AQUI, "montagem_visual.png")
fig.savefig(out, dpi=120)
print(out)
