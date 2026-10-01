"""GUIAS DE MONTAGEM da placa do radio - chamado pelo gera_placa.py.

Tudo e desenhado da MESMA geometria que vai para a laser (cobre, furos, carcacas,
serigrafia): o guia nao tem como discordar da placa.

  guia_1_conectores.png      o lado SEM cobre: conectores, C1, jumpers, os nomes gravados
  guia_2_lado_do_cobre.png   o lado do cobre: E22, C2, C3, R1, R2 e o fio do JP3
  guia_3_ligacoes.png        o que liga em que: tela, GPS, step-down e antena
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch, PathPatch, Rectangle
from matplotlib.path import Path
from shapely.geometry import Point, box
from shapely.geometry.polygon import orient

FR4 = "#e9dfb4"
FR4_BORDA = "#8f8250"
GRAVADO = "#3b3122"
COBRE = "#c47a35"
FURO = "#2d2a24"
JST = "#f4f0e3"
JST_BORDA = "#9e977d"
OURO = "#d4a72c"
FIO = "#d62728"
TINTA = "#1d1d1d"
DESTAQUE = {"J2": "#1f77b4", "J1": "#2ca02c", "J4": "#9467bd", "J3": "#d62728", "C1": "#1f3c88",
            "JP": "#d62728", "E22": "#555555", "SMD": "#8c564b"}


def _poligonos(g):
    if g.is_empty:
        return []
    if g.geom_type == "Polygon":
        return [g]
    return [p for p in getattr(g, "geoms", []) if p.geom_type == "Polygon" and not p.is_empty]


def _pinta(ax, geom, cor, alpha=1.0, z=2, borda=None, lw=0.0):
    """Poligonos com buraco (miolo de letra, furo de pad) do jeito certo."""
    for p in _poligonos(geom):
        p = orient(p, 1.0)
        vs, cs = [], []
        for anel in [p.exterior] + list(p.interiors):
            pts = list(anel.coords)
            vs += pts
            cs += [Path.MOVETO] + [Path.LINETO] * (len(pts) - 2) + [Path.CLOSEPOLY]
        ax.add_patch(PathPatch(Path(vs, cs), fc=cor, ec=borda or "none", lw=lw, alpha=alpha, zorder=z))


def _chamada(ax, texto, xy, xyt, cor, ha="left", fs=11):
    ax.annotate(texto, xy=xy, xytext=xyt, ha=ha, va="center", fontsize=fs, color=TINTA, zorder=20,
                bbox=dict(boxstyle="round,pad=0.45", fc="white", ec=cor, lw=1.6),
                arrowprops=dict(arrowstyle="-|>", color=cor, lw=1.6, shrinkA=0, shrinkB=3,
                                connectionstyle="arc3,rad=0"))


def _placa(ax, g):
    ax.add_patch(Rectangle((0, 0), g["LARG"], g["ALT"], fc=FR4, ec=FR4_BORDA, lw=2, zorder=1))


def _eixos(ax, g, x0, x1, y0, y1, titulo, sub):
    ax.set_xlim(x0, x1)
    ax.set_ylim(y1, y0)
    ax.set_aspect("equal")
    ax.axis("off")
    ax.text((x0 + x1) / 2, y0 + 1.5, titulo, ha="center", va="top", fontsize=17, weight="bold", color=TINTA)
    ax.text((x0 + x1) / 2, y0 + 5.2, sub, ha="center", va="top", fontsize=11, color="#555555")
    # regua de 10 mm: da a escala real do desenho
    ax.plot([0, 10], [g["ALT"] + 2.2, g["ALT"] + 2.2], color=TINTA, lw=2)
    ax.text(5, g["ALT"] + 3.0, "10 mm", ha="center", va="top", fontsize=8, color=TINTA)


# ---------------------------------------------------------------- 1: lado dos conectores
def _guia_conectores(g):
    LARG, ALT, cx = g["LARG"], g["ALT"], g["cx"]
    cc = g["corpos_c"]
    fig, ax = plt.subplots(figsize=(15, 11))
    _eixos(ax, g, -44, 84, -9, 76, "LADO DOS CONECTORES  (o lado sem cobre)",
           "Placa em tamanho proporcional. O que está escrito na placa é gravado na laser "
           "(arquivo laser_serigrafia).")
    _placa(ax, g)
    # o E22 esta do OUTRO lado: so a sombra, para orientar
    u1 = g["espelha"](next(c for r, c, _ in g["pecas"] if r == "U1"))
    xs, ys = u1.exterior.xy
    ax.plot(xs, ys, color="#9b9070", lw=1, ls=(0, (4, 3)), zorder=2)
    ax.text(u1.centroid.x, u1.bounds[3] - 1.2, "(o E22 está do outro lado)", ha="center",
            fontsize=8, color="#8a7f5c", zorder=2)
    _pinta(ax, g["serigrafia"], GRAVADO, z=3)
    for x, y, d in g["furos_c"]:
        ax.add_patch(Circle((x, y), d / 2, fc=FURO, zorder=4))

    # carcacas por cima (como a peca fica de verdade)
    pinos = {"J2": [(cx(x), g["J2Y"]) for x in g["J2X"].values()],
             "J1": [(cx(x), g["J1Y"]) for x in g["J1X"].values()],
             "J3": [(cx(x), y) for x, y, _ in g["J3"]],
             "J4": [(cx(x), g["J4Y"]) for x in g["J4X"]]}
    for r in ("J1", "J2", "J3"):
        _pinta(ax, cc[r], JST, alpha=0.96, z=5, borda=JST_BORDA, lw=1.2)
        for x, y in pinos[r]:
            ax.add_patch(Rectangle((x - 0.32, y - 0.32), 0.64, 0.64, fc=OURO, zorder=6))
    _pinta(ax, cc["J4"], "#262626", z=5)
    for x, y in pinos["J4"]:
        ax.add_patch(Rectangle((x - 0.32, y - 0.32), 0.64, 0.64, fc=OURO, zorder=6))
    for r in ("J1", "J2"):
        b = cc[r].bounds
        ax.text((b[0] + b[2]) / 2, (b[1] + b[3]) / 2 - 1.15, r, ha="center", va="center",
                fontsize=8, color="#7d7764", zorder=6)
    # C1: eletrolitico visto de cima, faixa clara do lado do -
    c1 = cc["C1"]
    _pinta(ax, c1, "#2a4f9e", z=5, borda="#16295a", lw=1.2)
    faixa = c1.intersection(box(cx(g["C1"][0][0]) - 0.9, 0, LARG, ALT))
    _pinta(ax, faixa, "#c9d4f0", z=6)
    ax.text(faixa.centroid.x, faixa.centroid.y, "−\n−", ha="center", va="center", fontsize=9,
            color="#16295a", weight="bold", zorder=7, linespacing=0.9)
    ax.text(c1.centroid.x - 0.8, c1.centroid.y, "220µF", ha="center", va="center", fontsize=7,
            color="white", rotation=90, zorder=7)

    # jumpers deste lado (fio)
    for a, b, n, desc in g["jumpers"]:
        if "COBRE" in desc:
            continue
        ax.plot([cx(a[0]), cx(b[0])], [a[1], b[1]], color=FIO, lw=5, solid_capstyle="round", zorder=8)

    # chamadas
    bJ2, bJ1, bJ3, bJ4 = cc["J2"].bounds, cc["J1"].bounds, cc["J3"].bounds, cc["J4"].bounds
    _chamada(ax, "J2  →  P1 da tela\ncabo PH 2,0 mm de 12 vias\no ▶ marca o pino 1: pino 1 com pino 1",
             (bJ2[2] - 3, bJ2[1]), (48, 4), DESTAQUE["J2"])
    _chamada(ax, "J1  →  P3 da tela\ncabo PH 2,0 mm de 12 vias\no ▶ marca o pino 1: pino 1 com pino 1",
             (bJ1[2] - 3, bJ1[1]), (48, 54), DESTAQUE["J1"])
    _chamada(ax, "GPS: 5 fios, cada um no nome igual\nTXD→TXD  RXD→RXD  PPS→PPS\nGND→GND  VCC→VCC\n"
                 "(a ordem NÃO é a do GPS: guie-se pelo nome)",
             (bJ4[0], g["J4Y"]), (-42, 66), DESTAQUE["J4"])
    _chamada(ax, "5,3 V do step-down\n+5V à esquerda, GND à direita\n(conector XH: só entra de um jeito)",
             (bJ3[0] + 1.5, bJ3[1] + 1.0), (-42, 1.5), DESTAQUE["J3"])
    _chamada(ax, "C1  220 µF\nperna COMPRIDA (+) no furo do +\nfaixa clara (−) para o lado de dentro\n"
                 "se faltar altura, deite o capacitor",
             (c1.bounds[0], c1.centroid.y), (-42, 16), DESTAQUE["C1"])
    for a, b, n, desc in g["jumpers"]:
        if "COBRE" in desc:
            continue
        nome = desc.split(":")[0]
        m = ((cx(a[0]) + cx(b[0])) / 2, (a[1] + b[1]) / 2)
        if nome == "JP2":
            _chamada(ax, "JP2: fio deste lado\n(BUSY por cima do DIO1)", m, (48, 20), DESTAQUE["JP"])
        else:
            _chamada(ax, f"{nome}: fio curto deste lado\n(junta os dois terras;\nsem ele a placa não funciona)",
                     m, (-42, 44), DESTAQUE["JP"])
    _chamada(ax, "Os nomes de cada fio estão gravados\nem pé ao lado de cada pino:\n"
                 "vire a cabeça para a esquerda para ler",
             (cx(g["J1X"][3]), bJ1[3] + 2.5), (48, 70), "#777777")
    fig.savefig(os.path.join(g["SAIDA"], "guia_1_conectores.png"), dpi=170, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------- 2: lado do cobre
def _guia_cobre(g):
    LARG, ALT = g["LARG"], g["ALT"]
    fig, ax = plt.subplots(figsize=(15, 11))
    _eixos(ax, g, -44, 84, -9, 76, "LADO DO COBRE  (o lado que vai na laser e no ácido)",
           "Aqui ficam o E22, os quatro SMD (C2, C3, R1, R2), o fio do JP3 e a antena. Os pinos dos conectores "
           "também são soldados deste lado.")
    _placa(ax, g)
    _pinta(ax, g["cobre_final"], COBRE, z=2)
    for x, y, d, _ in g["furos"]:
        ax.add_patch(Circle((x, y), d / 2, fc=FURO, zorder=3))

    # E22: o modulo por cima (transparente, para ver os pads)
    u1 = next(c for r, c, _ in g["pecas"] if r == "U1")
    _pinta(ax, u1, "#b8bec8", alpha=0.72, z=4, borda="#4d5562", lw=1.8)
    ax.text(u1.centroid.x, u1.centroid.y - 3, "E22-900M30S", ha="center", va="center", fontsize=15,
            weight="bold", color="#2f3540", zorder=5)
    ax.text(u1.centroid.x, u1.centroid.y + 1.5, "virado para CIMA\n(etiqueta para fora)", ha="center",
            va="center", fontsize=10, color="#2f3540", zorder=5)
    pads = g["E22_PAD"]
    for p in (1, 11, 12, 22):
        x, y = pads[p]
        dx = -2.6 if p <= 11 else 2.6
        ax.text(x + dx, y, str(p), ha="center", va="center", fontsize=9, weight="bold", color="#2f3540", zorder=6)
    x1, y1 = pads[1]
    ax.add_patch(Circle((x1 - 3.6, y1), 0.7, fc="#d62728", zorder=6))
    _chamada(ax, "PINO 1 do E22 aqui\n(confira o 1 impresso no módulo)\nsolde primeiro um canto, alinhe, depois o resto",
             (x1 - 3.6, y1), (48, 47), DESTAQUE["E22"])

    # SMD 1206: o corpo entre os dois pads
    valores = {"C2": ("10 µF", "#d8b27c"), "C3": ("100 nF", "#d8b27c"), "R1": ("10 kΩ", "#222222"),
               "R2": ("10 kΩ", "#222222")}
    pos = {}
    for r, c, _ in g["pecas"]:
        if r not in valores:
            continue
        b = c.bounds
        w, h = b[2] - b[0], b[3] - b[1]
        cxm, cym = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        L, W = (3.2, 1.6) if w > h else (1.6, 3.2)
        ax.add_patch(Rectangle((cxm - L / 2, cym - W / 2), L, W, fc=valores[r][1], ec="#444444", lw=0.8, zorder=6))
        pos[r] = (cxm, cym)
    _chamada(ax, "C2 = 10 µF (1206)", pos["C2"], (48, 9), DESTAQUE["SMD"])
    _chamada(ax, "C3 = 100 nF (1206)", pos["C3"], (48, 16), DESTAQUE["SMD"])
    _chamada(ax, "R1 = 10 kΩ (1206)\nsegura o TXEN em 0 no boot", pos["R1"], (48, 27), DESTAQUE["SMD"])
    _chamada(ax, "R2 = 10 kΩ (1206)\nsegura o RXEN em 0 no boot", pos["R2"], (48, 37), DESTAQUE["SMD"])

    # JP3: fio deitado do lado do cobre
    for a, b, n, desc in g["jumpers"]:
        if "COBRE" not in desc:
            continue
        ax.plot([a[0], b[0]], [a[1], b[1]], color=FIO, lw=5, solid_capstyle="round", zorder=7)
        m = ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
        _chamada(ax, "JP3: fio encapado DESTE lado,\nsoldado deitado nos dois pads\n(esses dois pads não têm furo)",
                 m, (-42, 50), DESTAQUE["JP"])
    _chamada(ax, "Pinos de J1, J2, J3, J4 e C1:\nas peças entram pelo outro lado\ne são soldadas aqui",
             (g["J1X"][3], g["J1Y"]), (-42, 64), "#777777")
    _chamada(ax, "TXEN: a trilha contorna a placa\npela borda (não tem mais jumper)",
             (g["X_TXEN"], 12), (48, 1.5), "#777777")
    _chamada(ax, "Antena SOLDADA: vivo no pad 21,\nmalha no pad 22 (o IPEX fica vazio).\nLIGUE A ANTENA ANTES DO 5 V",
             (g["E22_PAD"][21][0] - 1.1, g["E22_PAD"][21][1]), (-42, 30), "#d62728")
    fig.savefig(os.path.join(g["SAIDA"], "guia_2_lado_do_cobre.png"), dpi=170, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------- 3: ligacoes
GPIO_P1 = {1: ("TXEN", "IO52"), 2: ("NRST", "IO51"), 3: ("DIO1", "IO50"), 4: ("BUSY", "IO49"),
           8: ("GND", ""), 11: ("GND", "")}
GPIO_P3 = {3: ("MISO", "IO31"), 4: ("SCK", "IO30"), 5: ("MOSI", "IO29"), 6: ("NSS", "IO28"),
           7: ("RXEN", "IO5"), 8: ("TXD do GPS", "IO4"), 9: ("RXD do GPS", "IO3"), 10: ("PPS", "IO2"),
           11: ("GND", ""), 12: ("3V3", "")}


def _tabela(ax, x, y, titulo, linhas, cor, larg=19, alt=2.2):
    """linhas: [(numero, nome, extra, usado)]. Devolve {numero: (x_esq, x_dir, y)}."""
    ax.add_patch(FancyBboxPatch((x, y - 3.2), larg, alt * len(linhas) + 3.6, boxstyle="round,pad=0.3",
                                fc="white", ec=cor, lw=2, zorder=2))
    ax.text(x + larg / 2, y - 1.6, titulo, ha="center", va="center", fontsize=11, weight="bold", color=cor, zorder=3)
    pos = {}
    for i, (n, nome, extra, usado) in enumerate(linhas):
        yy = y + 0.6 + i * alt
        c = TINTA if usado else "#b0b0b0"
        ax.text(x + 1.0, yy, str(n), ha="left", va="center", fontsize=9, color=c, zorder=3, family="monospace")
        ax.text(x + 3.6, yy, nome, ha="left", va="center", fontsize=9.5, color=c, zorder=3,
                weight="bold" if usado else "normal")
        if extra:
            ax.text(x + larg - 0.8, yy, extra, ha="right", va="center", fontsize=8, color="#777777", zorder=3)
        pos[n] = (x, x + larg, yy)
    return pos


def _guia_ligacoes(g):
    fig, ax = plt.subplots(figsize=(17, 11))
    ax.set_xlim(0, 170)
    ax.set_ylim(108, 0)
    ax.axis("off")
    ax.text(85, 2, "O QUE LIGA EM QUÊ", ha="center", va="top", fontsize=18, weight="bold", color=TINTA)
    ax.text(85, 6.5, "Os cabos da tela são retos: pino 1 com pino 1, pino 2 com pino 2... "
                     "Cinza = pino que não é usado (o fio pode ficar no cabo).",
            ha="center", va="top", fontsize=11, color="#555555")

    # tela
    ax.add_patch(FancyBboxPatch((2, 12), 30, 92, boxstyle="round,pad=0.5", fc="#f2f2f2", ec="#888888", lw=1.5))
    ax.text(17, 15, 'TELA 7" (traseira)', ha="center", va="center", fontsize=12, weight="bold")
    l1 = [(k, *(GPIO_P1.get(k, ("não usado", ""))), k in GPIO_P1) for k in range(1, 13)]
    l3 = [(k, *(GPIO_P3.get(k, ("não usado", ""))), k in GPIO_P3) for k in range(1, 13)]
    p1 = _tabela(ax, 6, 23, "P1", l1, DESTAQUE["J2"], larg=22)
    p3 = _tabela(ax, 6, 64, "P3", l3, DESTAQUE["J1"], larg=22)

    # placa
    ax.add_patch(FancyBboxPatch((60, 12), 46, 92, boxstyle="round,pad=0.5", fc="#f7f1d9", ec=FR4_BORDA, lw=1.5))
    ax.text(83, 15, "PLACA DO RÁDIO", ha="center", va="center", fontsize=12, weight="bold")
    n1 = {k: (n, "", u) for k, n, _, u in l1}
    j2 = _tabela(ax, 63, 23, "J2", [(k, *n1[k]) for k in range(1, 13)], DESTAQUE["J2"], larg=17)
    nj1 = {k: g["NOMES_J1"].get(k, "não usado") for k in range(1, 13)}
    j1 = _tabela(ax, 63, 64, "J1", [(k, nj1[k], "", k in g["NOMES_J1"]) for k in range(1, 13)], DESTAQUE["J1"], larg=17)
    for k in range(1, 13):
        for a, b, usado in ((p1[k], j2[k], k in GPIO_P1), (p3[k], j1[k], k in GPIO_P3)):
            ax.plot([a[1] + 0.3, b[0] - 0.3], [a[2], b[2]], color=(TINTA if usado else "#cccccc"),
                    lw=1.6 if usado else 1, ls="-" if usado else (0, (3, 3)), zorder=1)
    ax.text(45, 20, "cabo PH 2,0 mm, 12 vias", ha="center", fontsize=10, color=DESTAQUE["J2"], weight="bold")
    ax.text(45, 61, "cabo PH 2,0 mm, 12 vias", ha="center", fontsize=10, color=DESTAQUE["J1"], weight="bold")

    # E22 e J3 dentro da placa
    ax.add_patch(FancyBboxPatch((84, 30), 19, 18, boxstyle="round,pad=0.3", fc="#d9dde3", ec="#4d5562", lw=1.5))
    ax.text(93.5, 36, "E22-900M30S", ha="center", va="center", fontsize=10, weight="bold")
    ax.text(93.5, 41.5, "SPI + TXEN/RXEN\nDIO1, BUSY, NRST", ha="center", va="center", fontsize=8, color="#444444")
    j3 = _tabela(ax, 84, 56, "J3  5,3 V", [("+", "+5V", "", True), ("−", "GND", "", True)], DESTAQUE["J3"], larg=19)

    # J4 e o GPS
    j4l = [(i + 1, n, "", True) for i, n in enumerate(g["NOMES_J4"])]
    j4 = _tabela(ax, 84, 82, "J4  GPS", j4l, DESTAQUE["J4"], larg=19)
    gps_ordem = ["VCC", "GND", "TXD", "RXD", "PPS"]          # o header do NEO-7M mini
    gp = _tabela(ax, 128, 82, "MÓDULO GPS", [(i + 1, n, "", True) for i, n in enumerate(gps_ordem)],
                 DESTAQUE["J4"], larg=19)
    for i, n in enumerate(g["NOMES_J4"]):
        a = j4[i + 1]
        b = gp[gps_ordem.index(n) + 1]
        ax.plot([a[1] + 0.3, b[0] - 0.3], [a[2], b[2]], color=DESTAQUE["J4"], lw=1.8)
    ax.text(117.5, 79, "fio por fio, pelo NOME\n(as ordens são diferentes)", ha="center", fontsize=9,
            color=DESTAQUE["J4"], weight="bold")
    ax.text(83, 99, "J1 pinos 8 a 12 já estão ligados ao J4 pela placa", ha="center", fontsize=8.5, color="#555555")

    # step-down e antena
    ax.add_patch(FancyBboxPatch((128, 52), 32, 10, boxstyle="round,pad=0.4", fc="white", ec=DESTAQUE["J3"], lw=2))
    ax.text(144, 57, "12 V do carro → STEP-DOWN\nsaída 5,3 V", ha="center", va="center", fontsize=10)
    for k, s in (("+", "+"), ("−", "−")):
        a = j3[k]
        ax.plot([a[1] + 0.3, 127.5], [a[2], 55 if k == "+" else 59], color=DESTAQUE["J3"], lw=1.8)
    ax.add_patch(FancyBboxPatch((124, 28), 42, 14, boxstyle="round,pad=0.4", fc="#fff3f3", ec="#d62728", lw=2))
    ax.text(145, 35, "ANTENA 915 MHz\ncabo soldado: vivo no pad 21,\nmalha no pad 22\nLIGUE ANTES DO 5 V",
            ha="center", va="center", fontsize=9.5, color="#a01010", weight="bold")
    ax.plot([103.3, 123.5], [39, 35], color="#d62728", lw=1.8)
    fig.savefig(os.path.join(g["SAIDA"], "guia_3_ligacoes.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------- 4: a placa montada
SOLDA = "#c9cdd2"
SOLDA_BORDA = "#8a9099"
BLINDAGEM = "#aeb4bb"


def _ref(ax, x, y, t, cor=TINTA, fs=8, **kw):
    ax.text(x, y, t, ha="center", va="center", fontsize=fs, weight="bold", color=cor, zorder=30,
            bbox=dict(boxstyle="round,pad=0.18", fc="white", ec="none", alpha=0.85), **kw)


def _montada_conectores(ax, g):
    LARG, ALT, cx, cc = g["LARG"], g["ALT"], g["cx"], g["corpos_c"]
    _placa(ax, g)
    _pinta(ax, g["serigrafia"], GRAVADO, z=3)
    for x, y, d in g["furos_c"]:
        ax.add_patch(Circle((x, y), d / 2, fc=FURO, zorder=4))
    # PH 2,0 vertical visto de cima: corpo, a boca onde o cabo entra, os pinos dentro
    for r, ys, xs in (("J2", g["J2Y"], g["J2X"]), ("J1", g["J1Y"], g["J1X"])):
        _pinta(ax, cc[r], JST, z=5, borda=JST_BORDA, lw=1.2)
        b = cc[r].bounds
        ax.add_patch(Rectangle((b[0] + 0.5, ys - 1.0), b[2] - b[0] - 1.0, 2.0, fc="#d9d2bb", ec=JST_BORDA,
                               lw=0.6, zorder=6))
        for x in xs.values():
            ax.add_patch(Rectangle((cx(x) - 0.25, ys - 0.25), 0.5, 0.5, fc=OURO, zorder=7))
        _ref(ax, (b[0] + b[2]) / 2, b[1] + 0.9, r, fs=7)
    # XH 2 vias
    _pinta(ax, cc["J3"], JST, z=5, borda=JST_BORDA, lw=1.2)
    b = cc["J3"].bounds
    ax.add_patch(Rectangle((b[0] + 0.6, b[1] + 1.4), b[2] - b[0] - 1.2, b[3] - b[1] - 2.0, fc="#d9d2bb",
                           ec=JST_BORDA, lw=0.6, zorder=6))
    for x, y, _ in g["J3"]:
        ax.add_patch(Rectangle((cx(x) - 0.32, y - 0.32), 0.64, 0.64, fc=OURO, zorder=7))
    _ref(ax, (b[0] + b[2]) / 2, b[1] + 0.8, "J3", fs=7)
    # barra de pinos do GPS
    _pinta(ax, cc["J4"], "#262626", z=5)
    for x in g["J4X"]:
        ax.add_patch(Rectangle((cx(x) - 0.32, g["J4Y"] - 0.32), 0.64, 0.64, fc=OURO, zorder=7))
    # C1 visto de cima: tampa com a cruz de alivio e a faixa do -
    c1 = cc["C1"]
    _pinta(ax, c1, "#2a4f9e", z=5, borda="#16295a", lw=1.2)
    _pinta(ax, c1.intersection(box(cx(g["C1"][0][0]) - 0.9, 0, LARG, ALT)), "#c9d4f0", z=6)
    c = c1.centroid
    ax.plot([c.x - 1.6, c.x + 1.6], [c.y, c.y], color="#9fb0d8", lw=0.8, zorder=7)
    ax.plot([c.x, c.x], [c.y - 1.6, c.y + 1.6], color="#9fb0d8", lw=0.8, zorder=7)
    _ref(ax, c.x, c1.bounds[3] + 0.9, "C1 220µF", fs=7)
    # jumpers: fio encapado vermelho, ponta de solda nos furos
    for a, b2, n, desc in g["jumpers"]:
        if "COBRE" in desc:
            continue
        A, B = (cx(a[0]), a[1]), (cx(b2[0]), b2[1])
        ax.plot([A[0], B[0]], [A[1], B[1]], color="#a31515", lw=6, solid_capstyle="round", zorder=8)
        ax.plot([A[0], B[0]], [A[1], B[1]], color=FIO, lw=4, solid_capstyle="round", zorder=9)


def _montada_cobre(ax, g):
    _placa(ax, g)
    _pinta(ax, g["cobre_final"], COBRE, z=2)
    # pinos dos conectores atravessando e soldados deste lado
    for x, y, d, desc in g["furos"]:
        ax.add_patch(Circle((x, y), 0.75 if d < 0.95 else 0.9, fc=SOLDA, ec=SOLDA_BORDA, lw=0.6, zorder=3))
        ax.add_patch(Circle((x, y), 0.22, fc="#6d737b", zorder=4))
    # E22: blindagem metalica por cima, a parte do pad que fica para fora coberta de solda
    pads = g["E22_PAD"]
    u1 = next(c for r, c, _ in g["pecas"] if r == "U1")
    ub = u1.bounds
    for p, (x, y) in pads.items():
        if g["NET_E22"][p] is None and p != 21:
            continue                    # DIO2 (pad 8): nao se solda
        x0, x1 = (ub[2], x + 1.5) if p <= 11 else (x - 1.5, ub[0])
        ax.add_patch(Rectangle((x0, y - 0.6), x1 - x0, 1.2, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=4))
    ax.add_patch(Rectangle((ub[0], ub[1]), ub[2] - ub[0], ub[3] - ub[1], fc="#1f6f4a", ec="#0f3b27", lw=1.2, zorder=5))
    ax.add_patch(Rectangle((ub[0] + 1.6, ub[1] + 1.4), ub[2] - ub[0] - 3.2, 28.5, fc=BLINDAGEM, ec="#6b7178",
                           lw=1.0, zorder=6))
    m = ub[0] + (ub[2] - ub[0]) / 2
    ax.text(m, ub[1] + 9.0, "EBYTE", ha="center", va="center", fontsize=11, weight="bold", color="#30353b", zorder=7)
    ax.text(m, ub[1] + 13.0, "E22-900M30S", ha="center", va="center", fontsize=10, weight="bold", color="#30353b", zorder=7)
    ax.text(m, ub[1] + 16.5, "SX1262  915 MHz  1 W", ha="center", va="center", fontsize=7, color="#30353b", zorder=7)
    # o IPEX fica no canto do pino 22 (manual E22-M, desenho mecanico) - VAZIO: a antena e soldada
    ix, iy = ub[0] + 3.2, ub[3] - 3.0
    ax.add_patch(Circle((ix, iy), 1.1, fc="#d8c070", ec="#7a6a2a", lw=0.8, zorder=7))
    ax.add_patch(Circle((ix, iy), 0.4, fc="#7a6a2a", zorder=8))
    ax.text(ix + 1.6, iy, "IPEX vazio", ha="left", va="center", fontsize=6.5, color="#e8f2ec", zorder=8)
    x1p, y1p = pads[1]
    ax.add_patch(Circle((ub[2] - 1.2, y1p), 0.55, fc="white", zorder=8))
    ax.text(ub[2] - 2.4, y1p, "1", ha="center", va="center", fontsize=8, weight="bold", color="white", zorder=8)
    # SMD 1206
    for r, c, _ in g["pecas"]:
        if r not in ("C2", "C3", "R1", "R2"):
            continue
        b = c.bounds
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
    # JP3: fio encapado deitado deste lado
    for a, b2, n, desc in g["jumpers"]:
        if "COBRE" not in desc:
            continue
        ax.plot([a[0], b2[0]], [a[1], b2[1]], color="#a31515", lw=6, solid_capstyle="round", zorder=9)
        ax.plot([a[0], b2[0]], [a[1], b2[1]], color=FIO, lw=4, solid_capstyle="round", zorder=10)
        for p in (a, b2):
            ax.add_patch(Circle(p, 0.7, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=11))
    # ANTENA SOLDADA: vivo no pad 21, malha no pad 22; o cabo sai pela borda esquerda
    x21, y21 = pads[21]
    x22, y22 = pads[22]
    xp = x21 - 1.1                                  # ponta do pad, fora do modulo
    ax.plot([xp, xp - 2.0], [y21, y21 + 1.2], color="#d9a441", lw=1.6, zorder=12)          # vivo
    ax.plot([xp, xp - 2.0], [y22, y21 + 1.6], color="#9a9a9a", lw=3.0, zorder=12)          # malha
    ax.add_patch(Circle((xp, y21), 0.55, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=13))
    ax.add_patch(Circle((xp, y22), 0.65, fc=SOLDA, ec=SOLDA_BORDA, lw=0.5, zorder=13))
    cabo = [(xp - 2.0, y21 + 1.4), (-1.0, y21 + 1.4), (-5.0, y21 + 3.5)]
    ax.plot(*zip(*cabo), color="#151515", lw=4.5, solid_capstyle="round", zorder=12)
    ax.add_patch(Circle((xp - 2.4, y21 + 1.4), 0.9, fc="#ededed", ec="#888888", lw=0.6, zorder=13))   # cola quente


def _guia_montada(g):
    LARG, ALT = g["LARG"], g["ALT"]
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(16, 13.5))
    for ax in (a1, a2):
        ax.set_xlim(-9, LARG + 9)
        ax.set_ylim(ALT + 6, -7)
        ax.set_aspect("equal")
        ax.axis("off")
        ax.plot([0, 10], [ALT + 2.2, ALT + 2.2], color=TINTA, lw=2)
        ax.text(5, ALT + 3.0, "10 mm", ha="center", va="top", fontsize=8, color=TINTA)
    a1.text(LARG / 2, -5.5, "LADO DOS CONECTORES", ha="center", fontsize=15, weight="bold", color=TINTA)
    a1.text(LARG / 2, -2.8, "é o lado de fora: os cabos encaixam aqui", ha="center", fontsize=10, color="#555555")
    a2.text(LARG / 2, -5.5, "LADO DO COBRE", ha="center", fontsize=15, weight="bold", color=TINTA)
    a2.text(LARG / 2, -2.8, "a mesma placa virada (esquerda e direita trocam)", ha="center", fontsize=10,
            color="#555555")
    _montada_conectores(a1, g)
    _montada_cobre(a2, g)

    # nomes das pecas fora da placa, com uma linha fina ate ela
    def fora(ax, txt, xy, xyt, ha):
        ax.annotate(txt, xy=xy, xytext=xyt, ha=ha, va="center", fontsize=8.5, color=TINTA, zorder=40,
                    arrowprops=dict(arrowstyle="-", color="#777777", lw=0.8))
    cx, cc = g["cx"], g["corpos_c"]
    fora(a1, "5,3 V", (cc["J3"].bounds[0], g["J3"][0][1]), (-8.5, 1.0), "left")
    fora(a1, "P1 da tela", (cc["J2"].bounds[2], g["J2Y"]), (LARG + 8.5, 3.0), "right")
    fora(a1, "P3 da tela", (cc["J1"].bounds[2], g["J1Y"]), (LARG + 8.5, 56.0), "right")
    fora(a1, "GPS", (cc["J4"].bounds[0], g["J4Y"]), (-8.5, 66.0), "left")
    fora(a1, "JP2", (cx(9.0), 11.0), (LARG + 8.5, 14.0), "right")
    fora(a1, "JP4", (cx(g["jumpers"][-1][0][0]), 55.5), (-8.5, 52.0), "left")
    pads = g["E22_PAD"]
    fora(a2, "antena\n(cabo soldado)", (-5.0, pads[21][1] + 3.5), (-8.5, pads[21][1] + 8.0), "left")
    for r, xyt in (("C2", (LARG + 8.5, 11.5)), ("C3", (LARG + 8.5, 17.5)), ("R1", (LARG + 8.5, 31.5)),
                   ("R2", (LARG + 8.5, 40.5))):
        c = next(c for rr, c, _ in g["pecas"] if rr == r)
        val = {"C2": "C2 10µF", "C3": "C3 100nF", "R1": "R1 10k", "R2": "R2 10k"}[r]
        fora(a2, val, (c.bounds[2], c.centroid.y), xyt, "right")
    j3 = next(j for j in g["jumpers"] if "COBRE" in j[3])
    fora(a2, "JP3", ((j3[0][0] + j3[1][0]) / 2, (j3[0][1] + j3[1][1]) / 2), (-8.5, 58.0), "left")
    plt.tight_layout()
    fig.savefig(os.path.join(g["SAIDA"], "guia_4_placa_montada.png"), dpi=170, bbox_inches="tight")
    plt.close(fig)


def gera(g):
    _guia_conectores(g)
    _guia_cobre(g)
    _guia_ligacoes(g)
    _guia_montada(g)
