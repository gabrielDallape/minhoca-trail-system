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
           "Aqui ficam o E22, os quatro SMD (C2, C3, R1, R2) e o fio do JP3. Os pinos dos conectores "
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
    _chamada(ax, "Antena: conector IPEX do E22\n→ pigtail → SMA.\nLIGUE A ANTENA ANTES DO 5 V",
             (u1.centroid.x - 6, u1.centroid.y - 3), (-42, 14), "#d62728")
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
    ax.text(145, 35, "ANTENA 915 MHz\n→ cabo SMA → pigtail → IPEX do E22\nLIGUE ANTES DO 5 V",
            ha="center", va="center", fontsize=9.5, color="#a01010", weight="bold")
    ax.plot([103.3, 123.5], [39, 35], color="#d62728", lw=1.8)
    fig.savefig(os.path.join(g["SAIDA"], "guia_3_ligacoes.png"), dpi=150, bbox_inches="tight")
    plt.close(fig)


def gera(g):
    _guia_conectores(g)
    _guia_cobre(g)
    _guia_ligacoes(g)
