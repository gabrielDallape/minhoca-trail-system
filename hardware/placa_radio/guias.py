"""GUIAS DE MONTAGEM da placa do radio v3 - chamado pelo gera_placa.py.

Tudo e desenhado da MESMA geometria que vai para a laser: o guia nao tem como
discordar da placa.

  guia_1_montagem.png   a placa montada: E22, SMD, cada fio chegando no seu pad
  guia_2_ligacoes.png   o que liga em que: pino da tela -> pad, GPS, step-down, antena
  guia_3_vao_e22.png    os vaos debaixo do E22, ampliados
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch, PathPatch, Rectangle
from matplotlib.path import Path
from shapely.geometry.polygon import orient

FR4 = "#e9dfb4"
FR4_BORDA = "#8f8250"
COBRE = "#c47a35"
TINTA = "#1d1d1d"
SOLDA = "#c9cdd2"
SOLDA_BORDA = "#8a9099"
BLINDAGEM = "#aeb4bb"
COR = {"P1": "#1f77b4", "P3": "#2ca02c", "step-down": "#d62728", "GPS": "#9467bd", "ANT": "#d62728",
       "E22": "#555555", "SMD": "#8c564b"}
# a cor do fio no desenho (nao e a cor real do cabo: e so para seguir o fio com o olho)
COR_FIO = {"GND": "#222222", "5V": "#d62728", "3V3": "#ff7f0e"}

# GPIO da tela atras de cada pino (docs/MONTAGEM.md, conferido no esquematico)
P1_GPIO = {1: "IO52", 2: "IO51", 3: "IO50", 4: "IO49", 5: "IO48", 6: "IO47", 7: "IO46", 8: "GND",
           9: "LDO_VO4", 10: "3V3", 11: "GND", 12: "BAT"}
P3_GPIO = {1: "IO36", 2: "IO34", 3: "IO31", 4: "IO30", 5: "IO29", 6: "IO28", 7: "IO5", 8: "IO4",
           9: "IO3", 10: "IO2", 11: "GND", 12: "3V3"}


def _poligonos(g):
    if g.is_empty:
        return []
    if g.geom_type == "Polygon":
        return [g]
    return [p for p in getattr(g, "geoms", []) if p.geom_type == "Polygon" and not p.is_empty]


def _pinta(ax, geom, cor, alpha=1.0, z=2, borda=None, lw=0.0):
    """Poligonos com buraco (miolo de letra) do jeito certo."""
    for p in _poligonos(geom):
        p = orient(p, 1.0)
        vs, cs = [], []
        for anel in [p.exterior] + list(p.interiors):
            pts = list(anel.coords)
            vs += pts
            cs += [Path.MOVETO] + [Path.LINETO] * (len(pts) - 2) + [Path.CLOSEPOLY]
        ax.add_patch(PathPatch(Path(vs, cs), fc=cor, ec=borda or "none", lw=lw, alpha=alpha, zorder=z))


def _chamada(ax, texto, xy, xyt, cor, ha="left", fs=10.5):
    ax.annotate(texto, xy=xy, xytext=xyt, ha=ha, va="center", fontsize=fs, color=TINTA, zorder=40,
                bbox=dict(boxstyle="round,pad=0.45", fc="white", ec=cor, lw=1.6),
                arrowprops=dict(arrowstyle="-|>", color=cor, lw=1.5, shrinkA=0, shrinkB=3))


def _placa(ax, g):
    ax.add_patch(Rectangle((0, 0), g["LARG"], g["ALT"], fc=FR4, ec=FR4_BORDA, lw=2, zorder=1))
    _pinta(ax, g["cobre_final"], COBRE, z=2)


def _regua(ax, g):
    ax.plot([0, 10], [g["ALT"] + 2.2, g["ALT"] + 2.2], color=TINTA, lw=2)
    ax.text(5, g["ALT"] + 3.0, "10 mm", ha="center", va="top", fontsize=8, color=TINTA)


def _peca(g, ref):
    return next(c for r, c, _ in g["pecas"] if r == ref)


# ---------------------------------------------------------------- 1: montagem
def _e22(ax, g):
    pads, ub = g["E22_PAD"], g["modulo_e22"].bounds
    for p, (x, y) in pads.items():
        if g["NET_E22"][p] is None and p != 21:
            continue                    # DIO2 (pad 8): nao se solda
        x0, x1 = (ub[2], x + 1.5) if p <= 11 else (x - 1.5, ub[0])
        ax.add_patch(Rectangle((x0, y - 0.6), x1 - x0, 1.2, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=4))
    ax.add_patch(Rectangle((ub[0], ub[1]), ub[2] - ub[0], ub[3] - ub[1], fc="#1f6f4a", ec="#0f3b27", lw=1.2, zorder=5))
    ax.add_patch(Rectangle((ub[0] + 1.6, ub[1] + 1.4), ub[2] - ub[0] - 3.2, 28.5, fc=BLINDAGEM, ec="#6b7178",
                           lw=1.0, zorder=6))
    m = (ub[0] + ub[2]) / 2
    ax.text(m, ub[1] + 9.0, "EBYTE", ha="center", va="center", fontsize=12, weight="bold", color="#30353b", zorder=7)
    ax.text(m, ub[1] + 13.0, "E22-900M30S", ha="center", va="center", fontsize=11, weight="bold", color="#30353b", zorder=7)
    ax.text(m, ub[1] + 17.0, "etiqueta para CIMA", ha="center", va="center", fontsize=8, color="#30353b", zorder=7)
    # o IPEX fica no canto do pino 22 (manual E22-M, desenho mecanico) - VAZIO: a antena e soldada
    ix, iy = ub[0] + 3.2, ub[3] - 3.0
    ax.add_patch(Circle((ix, iy), 1.1, fc="#d8c070", ec="#7a6a2a", lw=0.8, zorder=7))
    ax.add_patch(Circle((ix, iy), 0.4, fc="#7a6a2a", zorder=8))
    ax.text(ix + 1.6, iy, "IPEX vazio", ha="left", va="center", fontsize=7, color="#e8f2ec", zorder=8)
    ax.add_patch(Circle((ub[2] - 1.2, pads[1][1]), 0.55, fc="white", zorder=8))
    ax.text(ub[2] - 2.4, pads[1][1], "1", ha="center", va="center", fontsize=9, weight="bold", color="white", zorder=8)


def _smd(ax, g):
    for r in ("C2", "R1"):
        b = _peca(g, r).bounds
        w, h = b[2] - b[0], b[3] - b[1]
        cxm, cym = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        L, W = (3.2, 1.6) if w > h else (1.6, 3.2)
        corpo = "#c8a26a" if r.startswith("C") else "#1e1e1e"
        ax.add_patch(Rectangle((cxm - L / 2, cym - W / 2), L, W, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=6))
        if w > h:
            ax.add_patch(Rectangle((cxm - L / 2 + 0.55, cym - W / 2), L - 1.1, W, fc=corpo, zorder=7))
        else:
            ax.add_patch(Rectangle((cxm - L / 2, cym - W / 2 + 0.55), L, W - 1.1, fc=corpo, zorder=7))
        if r.startswith("R"):
            ax.text(cxm, cym, "103", ha="center", va="center", fontsize=4.5, color="white", zorder=8,
                    rotation=0 if w > h else 90)
    # C1: eletrolitico SMD visto de cima - base quadrada, lata redonda, faixa preta no -
    b = _peca(g, "C1").bounds
    cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
    ax.add_patch(Rectangle((b[0], b[1]), b[2] - b[0], b[3] - b[1], fc="#2b2b2b", ec="#111111", lw=0.8, zorder=8))
    ax.add_patch(Circle((cx, cy), 3.0, fc="#c7ccd2", ec="#7d838a", lw=0.8, zorder=9))
    ax.add_patch(PathPatch(Path([(cx - 3.0, cy - 3.2), (cx - 1.4, cy - 3.2), (cx - 1.4, cy + 3.2), (cx - 3.0, cy + 3.2)]),
                           fc="#1d1d1d", ec="none", zorder=10, clip_path=Circle((cx, cy), 3.0, transform=ax.transData)))
    ax.text(cx + 0.6, cy, "220µF\n16V", ha="center", va="center", fontsize=6.5, color="#30353b", zorder=11)


def _fios(ax, g):
    """Cada fio chega de fora da placa e para em cima do seu pad."""
    ALT = g["ALT"]
    for net, (x, y), rot, origem, w, h in g["fios"]:
        cor = COR_FIO.get(net, COR[origem] if origem in COR else "#555555")
        if net == "TXEN":
            pts = [(x + 0.2, y), (x + 0.2, -6.0)]
        elif y < ALT / 2:
            pts = [(x, y + 0.6), (x, -6.0)]
        else:
            pts = [(x, y - 0.6), (x, ALT + 6.0)]
        lw = 4.2 if origem == "step-down" else 3.0
        ax.plot(*zip(*pts), color=cor, lw=lw, solid_capstyle="round", zorder=12, alpha=0.75)
        ax.add_patch(Circle(pts[0], 0.75, fc=SOLDA, ec=SOLDA_BORDA, lw=0.6, zorder=13))
    # o GPS: segundo fio nos pads do pino 8, 9, 10, 11 e 12 do P3
    for net in ("GPS_TX", "GPS_RX", "PPS", "GND", "3V3"):
        x = g["P3X"][net]
        ax.plot([x + 0.5, x + 0.5], [g["YBOT"] - 0.2, ALT + 6.0], color=COR["GPS"], lw=2.2, ls=(0, (2, 1)),
                zorder=11)


def _antena(ax, g):
    pads = g["E22_PAD"]
    x21, y21 = pads[21]
    x22, y22 = pads[22]
    xp = x21 - 1.1
    ax.plot([xp, xp - 2.0], [y21, y21 + 1.2], color="#d9a441", lw=1.6, zorder=14)
    ax.plot([xp, xp - 2.0], [y22, y21 + 1.6], color="#9a9a9a", lw=3.0, zorder=14)
    ax.add_patch(Circle((xp, y21), 0.55, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=15))
    ax.add_patch(Circle((xp, y22), 0.65, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=15))
    cabo = [(xp - 2.0, y21 + 1.4), (-1.0, y21 + 1.4), (-5.0, y21 + 3.5)]
    ax.plot(*zip(*cabo), color="#151515", lw=4.5, solid_capstyle="round", zorder=14)
    ax.add_patch(Circle((xp - 2.4, y21 + 1.4), 0.9, fc="#ededed", ec="#888888", lw=0.6, zorder=15))
    return (-5.0, y21 + 3.5)


def _guia_montagem(g):
    LARG, ALT = g["LARG"], g["ALT"]
    fig, ax = plt.subplots(figsize=(15, 13))
    ax.set_xlim(-46, LARG + 46)
    ax.set_ylim(ALT + 9, -15)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.text(LARG / 2, -14.5,"A PLACA MONTADA  (tudo deste lado; o outro lado cola na tela)", ha="center",
            va="top", fontsize=16, weight="bold", color=TINTA)
    ax.text(LARG / 2, -11.8, "Sem furo e sem conector: cada fio do cabo cortado é soldado no pad com o nome dele. "
                             "Os nomes estão gravados no cobre.", ha="center", va="top", fontsize=10, color="#555555")
    _placa(ax, g)
    _e22(ax, g)
    _smd(ax, g)
    _fios(ax, g)
    ant = _antena(ax, g)
    # os nomes gravados, por cima dos fios: na placa o fio cobre a letra so depois de soldado
    for r, h, x, y, a, axx, ayy in g["rotulos"]:
        b = g["poe"](r, h, x, y, a, axx, ayy).bounds
        ax.text((b[0] + b[2]) / 2, (b[1] + b[3]) / 2, r, ha="center", va="center", fontsize=7 if a else 7.5,
                rotation=90 if a else 0, weight="bold", color="#5a4a1e", zorder=20,
                bbox=dict(boxstyle="round,pad=0.12", fc=FR4, ec="none", alpha=0.95))

    pads = g["E22_PAD"]
    _chamada(ax, "Fios do P1 da tela (cabo cortado)\n2 NRST · 4 BUSY · 3 DIO1 · 8 GND\n"
                 "o número é o pino do P1", (5.4, -3.0), (-45, -3.5), COR["P1"])
    _chamada(ax, "Pino 1 do P1 = TXEN\nsolda no pad do canto", (38.7, -3.0), (LARG + 5, -3.5), COR["P1"])
    _chamada(ax, "Alimentação do step-down\nAJUSTE PARA 5,0–5,1 V\nfio grosso (AWG 22)",
             (24.0, -3.0), (LARG + 5, 3.5), COR["step-down"])
    _chamada(ax, "C1 220 µF 16 V (SMD)\nfaixa preta (−) para a ESQUERDA", (31.0, 8.0), (LARG + 5, 12.5), COR["SMD"])
    _chamada(ax, "C2 10 µF (1206), colado nos\npinos 10 e 11 do E22", (g["X_C2"] + 0.8, 18.1), (LARG + 5, 20.0), COR["SMD"])
    _chamada(ax, "R1 10 kΩ (1206): segura o\nTXEN em 0 enquanto a tela liga", (g["X_R1"] + 0.8, pads[7][1] + 4.1),
             (LARG + 5, 29.0), COR["SMD"])
    _chamada(ax, "PINO 1 do E22 (gravado “1”)\nconfira o 1 impresso no módulo\n"
                 "solde um canto, alinhe, depois o resto", (g["modulo_e22"].bounds[2] - 1.2, pads[1][1]),
             (LARG + 5, 44.5), COR["E22"])
    _chamada(ax, "Antena: vivo no pad 21,\nmalha no pad 22 (IPEX vazio).\nLIGUE A ANTENA ANTES DO 5 V",
             ant, (-45, 40.0), COR["ANT"])
    _chamada(ax, "Fios do P3 da tela (cabo cortado)\n3 MISO · 5 MOSI · 4 SCK · 6 NSS\n11 GND · 7 RXEN",
             (5.6, ALT + 3.0), (-45, ALT + 1.0), COR["P3"])
    _chamada(ax, "GPS: o fio do GPS solda NO MESMO PAD\ndo fio do P3 (tracejado roxo):\n"
                 "TXD→8 TXD · RXD→9 RXD · PPS→10 PPS\nGND→11 GND · VCC→12 3V3",
             (25.0, ALT + 3.0), (LARG + 5, ALT + 1.0), COR["GPS"])
    _chamada(ax, "Prenda os fios com cola quente\nperto da placa: a solda\nnão aguenta puxão", (12.0, 61.0),
             (-45, 55.0), "#777777")
    fig.savefig(os.path.join(g["SAIDA"], "guia_1_montagem.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------- 2: ligacoes
def _tabela(ax, x, y, titulo, linhas, cor, larg=30, alt=2.3):
    """linhas: [(numero, nome, extra, usado)]. Devolve {numero: (x_esq, x_dir, y)}."""
    ax.add_patch(FancyBboxPatch((x, y - 3.4), larg, alt * len(linhas) + 3.8, boxstyle="round,pad=0.3",
                                fc="white", ec=cor, lw=2, zorder=2))
    ax.text(x + larg / 2, y - 1.7, titulo, ha="center", va="center", fontsize=11, weight="bold", color=cor, zorder=3)
    pos = {}
    for i, (n, nome, extra, usado) in enumerate(linhas):
        yy = y + 0.6 + i * alt
        c = TINTA if usado else "#b0b0b0"
        ax.text(x + 1.0, yy, str(n), ha="left", va="center", fontsize=9, color=c, zorder=3, family="monospace")
        ax.text(x + 3.8, yy, nome, ha="left", va="center", fontsize=9.5, color=c, zorder=3,
                weight="bold" if usado else "normal")
        if extra:
            ax.text(x + larg - 0.8, yy, extra, ha="right", va="center", fontsize=8, color="#777777", zorder=3)
        pos[n] = (x, x + larg, yy)
    return pos


def _guia_ligacoes(g):
    fig, ax = plt.subplots(figsize=(17, 12))
    ax.set_xlim(0, 170)
    ax.set_ylim(114, 0)
    ax.axis("off")
    ax.text(85, 2, "O QUE LIGA EM QUÊ", ha="center", va="top", fontsize=18, weight="bold", color=TINTA)
    ax.text(85, 6.5, "Corte o cabo PH de 12 vias deixando o plugue do lado da tela. Cada fio usado vai no pad "
                     "da placa com o MESMO NÚMERO gravado. Cinza = fio que não é usado: corte e isole.",
            ha="center", va="top", fontsize=10.5, color="#555555")

    pads_p1 = {k: (n, rot) for n, _, rot, o, _, _ in g["fios"] if o == "P1"
               for k in [int(rot.split()[0])]}
    pads_p3 = {k: (n, rot) for n, _, rot, o, _, _ in g["fios"] if o == "P3"
               for k in [int(rot.split()[0])]}

    ax.add_patch(FancyBboxPatch((2, 13), 40, 92, boxstyle="round,pad=0.5", fc="#f2f2f2", ec="#888888", lw=1.5))
    ax.text(22, 16, 'TELA 7" — os plugues ficam', ha="center", va="center", fontsize=11, weight="bold")
    l1 = [(k, P1_GPIO[k], "", k in pads_p1) for k in range(1, 13)]
    l3 = [(k, P3_GPIO[k], "", k in pads_p3) for k in range(1, 13)]
    p1 = _tabela(ax, 7, 25, "P1  (pino · sinal da tela)", l1, COR["P1"], larg=30)
    p3 = _tabela(ax, 7, 66, "P3  (pino · sinal da tela)", l3, COR["P3"], larg=30)

    ax.add_patch(FancyBboxPatch((72, 13), 40, 92, boxstyle="round,pad=0.5", fc="#f7f1d9", ec=FR4_BORDA, lw=1.5))
    ax.text(92, 16, "PLACA DO RÁDIO — pad gravado", ha="center", va="center", fontsize=11, weight="bold")
    j1 = _tabela(ax, 77, 25, "pads do P1", [(k, pads_p1[k][1] if k in pads_p1 else "— corte", "", k in pads_p1)
                                            for k in range(1, 13)], COR["P1"], larg=30)
    j3 = _tabela(ax, 77, 66, "pads do P3", [(k, pads_p3[k][1] if k in pads_p3 else "— corte", "", k in pads_p3)
                                            for k in range(1, 13)], COR["P3"], larg=30)
    for k in range(1, 13):
        for a, b, usado in ((p1[k], j1[k], k in pads_p1), (p3[k], j3[k], k in pads_p3)):
            ax.plot([a[1] + 0.3, b[0] - 0.3], [a[2], b[2]], color=(TINTA if usado else "#cccccc"),
                    lw=1.6 if usado else 1, ls="-" if usado else (0, (3, 3)), zorder=1)
    ax.text(57, 22.5, "fio do cabo cortado", ha="center", fontsize=9.5, color=COR["P1"], weight="bold")
    ax.text(57, 63.5, "fio do cabo cortado", ha="center", fontsize=9.5, color=COR["P3"], weight="bold")

    # GPS, step-down e antena, a direita
    gps = [("TXD", "8 TXD"), ("RXD", "9 RXD"), ("PPS", "10 PPS"), ("GND", "11 GND"), ("VCC", "12 3V3")]
    gp = _tabela(ax, 128, 66, "MÓDULO GPS", [(i + 1, n, "", True) for i, (n, _) in enumerate(gps)], COR["GPS"], larg=34)
    for i, (n, pad) in enumerate(gps):
        k = int(pad.split()[0])
        a, b = j3[k], gp[i + 1]
        ax.plot([a[1] + 0.3, b[0] - 0.3], [a[2], b[2]], color=COR["GPS"], lw=1.8)
    ax.text(145, 84, "o fio do GPS solda no MESMO pad\ndo fio do P3 (dois fios no pad)", ha="center", va="top",
            fontsize=9, color=COR["GPS"], weight="bold")
    ax.add_patch(FancyBboxPatch((126, 25), 38, 14, boxstyle="round,pad=0.4", fc="white", ec=COR["step-down"], lw=2))
    ax.text(145, 32, "STEP-DOWN  (ajuste para 5,0–5,1 V)\nsaída +  →  pad “+5V”\nsaída −  →  pad “GND”\n"
                     "fio AWG 22 ou mais grosso", ha="center", va="center", fontsize=9.5)
    ax.add_patch(FancyBboxPatch((126, 44), 38, 14, boxstyle="round,pad=0.4", fc="#fff3f3", ec=COR["ANT"], lw=2))
    ax.text(145, 51, "ANTENA 915 MHz (cabo coaxial)\nvivo → pad 21 do E22\nmalha → pad 22 do E22\n"
                     "LIGUE ANTES DO 5 V", ha="center", va="center", fontsize=9.5, color="#a01010", weight="bold")
    ax.text(85, 110.5, "Antes de soldar: confira cada fio com o multímetro em continuidade, da ponta cortada até o "
                       "pino com o GPIO certo no conector da tela (o número do GPIO está impresso na placa da tela).",
            ha="center", va="center", fontsize=9.5, color="#a01010")
    fig.savefig(os.path.join(g["SAIDA"], "guia_2_ligacoes.png"), dpi=140, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------- 3: o vao debaixo do E22
def _guia_vao(g):
    m = g["modulo_e22"].bounds
    fig, axs = plt.subplots(1, 2, figsize=(14, 13))
    for ax, (x0, x1, esq, tit) in zip(axs, [(0, 20, True, "lado ESQUERDO do E22 (pads 12 a 22)"),
                                            (20, 40, False, "lado DIREITO do E22 (pads 1 a 11)")]):
        y0, y1 = m[1] - 1.5, m[3] + 1.5
        ax.add_patch(Rectangle((0, 0), 40, 70, fc=FR4, zorder=1))
        _pinta(ax, g["cobre_final"], COBRE, z=2)
        ax.add_patch(Rectangle((m[0], m[1]), m[2] - m[0], m[3] - m[1], fill=False, ec="#333", lw=2, ls="--", zorder=5))
        for p, (x, y) in g["E22_PAD"].items():
            if (p > 11) != esq:
                continue
            n = g["NET_E22"][p] or "livre"
            ax.text(x + 3.0 if esq else x - 3.0, y, f"{p} {n}", ha="left" if esq else "right", va="center",
                    fontsize=10, weight="bold", color="#1a5c1a" if n == "GND" else "#8b1a1a", zorder=6,
                    bbox=dict(fc="white", ec="none", alpha=0.85, pad=1))
        ax.text(m[0] + 6 if esq else m[2] - 6, m[1] + 0.9, "borda do E22 (tracejado)", ha="center", fontsize=9,
                color="#333", zorder=6)
        ax.set_xlim(x0, x1); ax.set_ylim(y1, y0); ax.set_aspect("equal"); ax.axis("off")
        ax.set_title(tit, fontsize=14, weight="bold")
    fig.suptitle("Debaixo do E22: cobre = terra, bege = sem cobre (a laser queimou)\n"
                 "Vermelho = pad de sinal: 1,2 mm de vão até o terra.   Verde = pad de terra: encosta no terra de propósito.",
                 fontsize=12)
    plt.tight_layout()
    fig.savefig(os.path.join(g["SAIDA"], "guia_3_vao_e22.png"), dpi=120)
    plt.close(fig)


def gera(g):
    _guia_montagem(g)
    _guia_ligacoes(g)
    _guia_vao(g)
