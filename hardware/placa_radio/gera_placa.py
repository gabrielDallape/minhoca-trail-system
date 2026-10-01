"""PLACA DO RADIO (MTS) - E22-900M30S + conectores da tela 7B + GPS, face unica.

    python hardware/placa_radio/gera_placa.py

Gera, em hardware/placa_radio/saida/:
  laser_queimar.png/.svg/.dxf  O QUE A LASER TIRA DA TINTA (tudo que NAO e cobre)
  cobre.png/.svg               o cobre que fica (positivo, para conferir)
  furos.csv / furos.png        cada furo: posicao, diametro, o que e
  montagem.png                 o lado dos COMPONENTES (espelhado), com os jumpers
  regua_100mm.png              teste de escala: imprima/grave e meca 100 mm

COORDENADAS: mm, origem no canto superior esquerdo, x para a direita, y para
baixo, VISTO PELO LADO DO COBRE - que e o lado que a laser enxerga (a placa vai
na laser com o cobre para cima). Por isso os arquivos da laser NAO sao espelhados.
O E22 e SMD e fica DESTE lado; os conectores sao de furo e ficam do outro.

Fontes das medidas:
  E22-900M30S: footprint oficial da LCSC/JLCPCB (peca C411294), baixado com
    JLC2KiCadLib em 2026-10-01: 38,5 x 24 mm, 22 pads 2,5 x 1,5 mm, passo 2,54 mm.
  Tela 7B: P1 e P3 = JST-PH 2,0 mm, 12 vias (docs/MONTAGEM.md, esquematico oficial).
  Pinagem e GPIOs: firmware/mts/hal/pinos.h.
"""
import csv
import math
import os
import sys

import ezdxf
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from matplotlib.textpath import TextPath
from PIL import Image, ImageDraw
from shapely import affinity
from shapely.geometry import LineString, MultiPolygon, Point, Polygon, box
from shapely.ops import unary_union

AQUI = os.path.dirname(os.path.abspath(__file__))
SAIDA = os.path.join(AQUI, "saida")
os.makedirs(SAIDA, exist_ok=True)

# ------------------------------------------------------------------ regras
LARG, ALT = 40.0, 70.0          # a placa (espaco atras da tela: 40 x 70)
TR_SINAL = 0.5                  # largura de trilha de sinal
TR_POT = 1.6                    # 5 V e GND de potencia
FOLGA_POUR = 0.6                # do plano de terra a qualquer outra coisa
FOLGA_MIN = 0.4                 # o DRC reprova abaixo disto
MARGEM_BORDA = 0.5              # o cobre para antes da borda
GUIA_BROCA = 0.6                # o furinho no centro de cada pad (guia da broca)

# ------------------------------------------------------------------ o que existe
copper = []      # (net, geometria)
furos = []       # (x, y, diametro, descricao)
pecas = []       # para o desenho de montagem: (ref, contorno no lado do cobre, rotulo)
jumpers = []     # (netA_pad_xy, netB_pad_xy, net, descricao)


def pad_rect(net, cx, cy, w, h):
    copper.append((net, box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)))


def pad_tht(net, cx, cy, diam, furo, desc, oval=None):
    """Pad de furo. oval=(w,h) para pad alongado (o PH2.0 tem passo de 2 mm)."""
    if oval:
        w, h = oval
        r = min(w, h) / 2
        g = LineString([(cx - (w / 2 - r), cy - (h / 2 - r)), (cx + (w / 2 - r), cy + (h / 2 - r))]).buffer(r)
    else:
        g = Point(cx, cy).buffer(diam / 2, resolution=24)
    copper.append((net, g))
    furos.append((cx, cy, furo, desc))


def trilha(net, pts, w=TR_SINAL):
    copper.append((net, LineString(pts).buffer(w / 2, cap_style=1, join_style=1, resolution=12)))


# ------------------------------------------------------------------ E22-900M30S
# Centro do modulo; offsets dos pads do footprint oficial (y para baixo).
E22X, E22Y = 20.0, 32.0
OFF = {1: 15.23, 2: 12.69, 3: 10.15, 4: 2.55, 5: 0.01, 6: -2.53, 7: -5.07, 8: -7.61,
       9: -10.15, 10: -12.69, 11: -15.23,
       12: -15.23, 13: -12.69, 14: -10.15, 15: -7.61, 16: -5.07, 17: -2.53, 18: 0.01,
       19: 2.55, 20: 10.15, 21: 12.69, 22: 15.23}
NET_E22 = {1: "GND", 2: "GND", 3: "GND", 4: "GND", 5: "GND", 6: "RXEN", 7: "TXEN", 8: None,
           9: "5V", 10: "5V", 11: "GND", 12: "GND", 13: "DIO1", 14: "BUSY", 15: "NRST",
           16: "MISO", 17: "MOSI", 18: "SCK", 19: "NSS", 20: "GND", 21: None, 22: "GND"}
# Pad do footprint: 2,5 x 1,5 centrado na borda do modulo. Aqui ele cresce 0,5 mm
# PARA FORA (3,0 x 1,5): sobra estanho para o ferro alcancar o meio-furo.
E22_PAD = {}
for p in range(1, 23):
    direita = p <= 11
    x = E22X + (12 + 0.25 if direita else -12 - 0.25)
    y = E22Y + OFF[p]
    E22_PAD[p] = (x, y)
    pad_rect(NET_E22[p] or f"NC_E22_{p}", x, y, 3.0, 1.5)
pecas.append(("U1", box(E22X - 12, E22Y - 17.84, E22X + 12, E22Y + 20.66), "E22-900M30S\n(lado do cobre)"))


def pe(p):          # borda EXTERNA do pad p do E22 (onde a trilha chega)
    x, y = E22_PAD[p]
    return (x + 1.5 if p <= 11 else x - 1.5, y)


# ------------------------------------------------------------------ J2 = P1 da tela
# JST-PH 2,0 mm 12 vias. Pino k do J2 = pino k do P1 (cabo "mesmo sentido").
P1 = {1: "TXEN", 2: "NRST", 3: "DIO1", 4: "BUSY", 5: None, 6: None, 7: None, 8: "GND",
      9: None, 10: None, 11: "GND", 12: None}
J2Y = 5.0
J2X = {k: 4.0 + (k - 1) * 2.0 for k in range(1, 13)}
for k in range(1, 13):
    pad_tht(P1[k] or f"NC_J2_{k}", J2X[k], J2Y, None, 0.8, f"J2 pino {k} ({P1[k] or 'nao usado'})", oval=(1.4, 2.4))
pecas.append(("J2", box(J2X[1] - 2.0, J2Y - 2.3, J2X[12] + 2.0, J2Y + 2.3), "J2 -> P1 da tela"))

# ------------------------------------------------------------------ J1 = P3 da tela
P3 = {1: None, 2: None, 3: "MISO", 4: "SCK", 5: "MOSI", 6: "NSS", 7: "RXEN", 8: "GPS_TX",
      9: "GPS_RX", 10: "PPS", 11: "GND", 12: "3V3"}
J1Y = 58.0
J1X = {k: 4.0 + (k - 1) * 2.0 for k in range(1, 13)}
for k in range(1, 13):
    pad_tht(P3[k] or f"NC_J1_{k}", J1X[k], J1Y, None, 0.8, f"J1 pino {k} ({P3[k] or 'nao usado'})", oval=(1.4, 2.4))
pecas.append(("J1", box(J1X[1] - 2.0, J1Y - 2.3, J1X[12] + 2.0, J1Y + 2.3), "J1 -> P3 da tela"))

# ------------------------------------------------------------------ J4 = GPS (2,54 mm, 5 vias)
J4Y = 65.5
GPS = ["GPS_TX", "GPS_RX", "PPS", "GND", "3V3"]          # da esquerda para a direita (lado do cobre)
J4X = [15.92 + i * 2.54 for i in range(5)]
for i, n in enumerate(GPS):
    pad_tht(n, J4X[i], J4Y, 1.8, 0.9, f"J4 GPS ({n})")
pecas.append(("J4", box(J4X[0] - 1.3, J4Y - 1.3, J4X[4] + 1.3, J4Y + 1.3), "J4 GPS"))

# ------------------------------------------------------------------ alimentacao (canto de cima, direita)
# J3: JST-XH 2,54 mm 2 vias (tem trava: nao inverte). O TERRA fica do lado de DENTRO
# e o +5V do lado da BORDA: assim a trilha de 5 V desce pela borda e o terra do
# capacitor encosta no plano (com o 5 V por dentro, o canto ficava uma ilha).
# C1: eletrolitico 220 uF / 10 V, radial 6,3 mm, passo 2,5 mm.
J3 = [(31.0, 3.0, "GND"), (33.54, 3.0, "5V")]
for x, y, n in J3:
    pad_tht(n, x, y, 1.9, 1.0, f"J3 entrada 5,3 V ({'+' if n == '5V' else '-'})")
pecas.append(("J3", box(28.5, 0.6, 36.0, 5.4), "J3 5,3V"))
X5V = 38.1                                       # a descida do 5 V, colada na borda
C1 = [(35.6, 8.5, "GND"), (X5V, 8.5, "5V")]
for x, y, n in C1:
    pad_tht(n, x, y, 1.9, 0.9, f"C1 220uF ({'+' if n == '5V' else '-'})")
pecas.append(("C1", Point(36.85, 8.5).buffer(3.15), "C1"))
# C2 (10 uF) e C3 (100 nF), SMD 1206, a menos de 10 mm dos pads 9 e 10 do E22.
# C3 em pe, entre o pad 11 (terra) e o ramal do pad 10 (5 V).
pad_rect("GND", 35.6, 16.3, 1.6, 1.2); pad_rect("5V", 35.6, 19.3, 1.6, 1.2)
pecas.append(("C3", box(34.7, 15.5, 36.5, 20.1), "C3"))
trilha("GND", [pe(11), (35.6, 16.77), (35.6, 16.3)])
# C2 deitado acima do modulo: terra no plano, 5 V na descida
pad_rect("GND", 35.0, 12.8, 1.2, 1.6); pad_rect("5V", 38.0, 12.8, 1.2, 1.6)
pecas.append(("C2", box(34.3, 11.9, 38.7, 13.7), "C2"))

# ------------------------------------------------------------------ trilhas
# --- alimentacao: J3+ -> pela borda -> C1+ -> C2 -> ramais para os pads 10 e 9
trilha("5V", [(33.54, 3.0), (X5V, 3.0), (X5V, E22_PAD[9][1])], TR_POT)
trilha("5V", [pe(10), (X5V, E22_PAD[10][1])], TR_POT)
trilha("5V", [pe(9), (X5V, E22_PAD[9][1])], TR_POT)

# --- P1 (J2, em cima): NRST, BUSY e DIO1 descem pelo canal da esquerda
CAN_NRST, CAN_BUSY, CAN_DIO1 = 1.0, 2.9, 4.8
trilha("NRST", [(J2X[2], J2Y), (J2X[2], 7.6), (CAN_NRST, 7.6), (CAN_NRST, E22_PAD[15][1]), pe(15)])
trilha("DIO1", [(J2X[3], J2Y), (J2X[3], 9.2), (CAN_DIO1, 9.2), (CAN_DIO1, E22_PAD[13][1]), pe(13)])
# BUSY cruza o DIO1: JUMPER (A2 perto do J2 -> B2 no canal)
A2, B2 = (10.0, 9.4), (CAN_BUSY, 12.4)
trilha("BUSY", [(J2X[4], J2Y), A2])
pad_tht("BUSY", *A2, 1.8, 0.8, "JUMPER JP2 lado A (BUSY)")
pad_tht("BUSY", *B2, 1.8, 0.8, "JUMPER JP2 lado B (BUSY)")
trilha("BUSY", [B2, (CAN_BUSY, E22_PAD[14][1]), pe(14)])
jumpers.append((A2, B2, "BUSY", "JP2: BUSY por cima do DIO1"))
# TXEN cruza tudo ate o lado direito: JUMPER longo (A1 acima do J2 -> B1 na faixa da direita)
A1, B1 = (J2X[1], 1.6), (38.4, E22_PAD[7][1])
trilha("TXEN", [(J2X[1], J2Y), A1])
pad_tht("TXEN", *A1, 1.8, 0.8, "JUMPER JP1 lado A (TXEN)")
pad_tht("TXEN", *B1, 1.8, 0.8, "JUMPER JP1 lado B (TXEN)")
trilha("TXEN", [B1, pe(7)])
jumpers.append((A1, B1, "TXEN", "JP1: TXEN atravessando a placa"))
# R1 (10 k, 1206, em pe): pull-down do TXEN, do B1 para a faixa de terra da borda
pad_rect("TXEN", 38.6, E22_PAD[7][1] + 2.3, 1.4, 1.2)
trilha("TXEN", [B1, (38.6, E22_PAD[7][1] + 2.3)])
pad_rect("GND", 38.6, E22_PAD[7][1] + 5.3, 1.4, 1.2)
pecas.append(("R1", box(37.8, E22_PAD[7][1] + 1.6, 39.4, E22_PAD[7][1] + 6.0), "R1"))

# --- P3 (J1, embaixo): SPI sobe pelo canal da esquerda
CAN_MISO, CAN_MOSI, CAN_SCK, CAN_NSS = 0.9, 2.3, 3.7, 5.1
trilha("MISO", [(J1X[3], J1Y), (J1X[3], 55.9), (CAN_MISO, 55.9), (CAN_MISO, E22_PAD[16][1]), pe(16)])
trilha("MOSI", [(J1X[5], J1Y), (J1X[5], 54.5), (CAN_MOSI, 54.5), (CAN_MOSI, E22_PAD[17][1]), pe(17)])
trilha("NSS", [(J1X[6], J1Y), (J1X[6], 53.2), (CAN_NSS, 53.2), (CAN_NSS, E22_PAD[19][1]), pe(19)])
# SCK cruza o MOSI: JUMPER (A3 logo acima do J1 -> B3 no canal)
A3, B3 = (J1X[4], 56.0), (CAN_SCK, 49.6)
trilha("SCK", [(J1X[4], J1Y), A3])
pad_tht("SCK", *A3, 1.6, 0.8, "JUMPER JP3 lado A (SCK)")
pad_tht("SCK", *B3, 1.4, 0.7, "JUMPER JP3 lado B (SCK)")
trilha("SCK", [B3, (CAN_SCK, E22_PAD[18][1]), pe(18)])
jumpers.append((A3, B3, "SCK", "JP3: SCK por cima do MOSI"))
# RXEN: por baixo do modulo, sobe pela faixa da direita ate o pad 6
trilha("RXEN", [(J1X[7], J1Y), (J1X[7], 55.4), (37.0, 55.4), (37.0, E22_PAD[6][1]), pe(6)])
# R2 (10 k, 1206, deitado): pull-down do RXEN, da trilha para o terra entre ela e o modulo
pad_rect("RXEN", 37.4, 38.3, 1.2, 1.6)
pad_rect("GND", 34.4, 38.3, 1.2, 1.6)
pecas.append(("R2", box(33.7, 37.4, 38.1, 39.2), "R2"))
# --- JP4: o TERRA de baixo (P3, GPS, borda direita) e o de cima (modulo, alimentacao,
#     P1) ficam separados pela trilha do RXEN, que passa por baixo do modulo. Um fio
#     curto do lado dos componentes os junta - sem ele o terra so se fecharia pela tela.
A4, B4 = (24.5, 53.8), (30.5, 60.6)
pad_tht("GND", *A4, 1.6, 0.8, "JUMPER JP4 lado A (TERRA)")
pad_tht("GND", *B4, 1.6, 0.8, "JUMPER JP4 lado B (TERRA)")
jumpers.append((A4, B4, "GND", "JP4: junta os dois terras"))

# --- GPS: J1 pinos 8..12 descem para o J4
for k, i in ((8, 0), (9, 1), (10, 2), (11, 3), (12, 4)):
    n = P3[k]
    trilha(n, [(J1X[k], J1Y), (J1X[k], 61.6), (J4X[i], 63.2), (J4X[i], J4Y)])

# ------------------------------------------------------------------ texto no cobre
TEXTOS = [("MTS E22 v1", 14.0, 69.2, 1.3), ("GPS", 28.0, 66.2, 1.0),
          ("P3", 1.0, 61.8, 1.2), ("P1", 28.6, 10.6, 1.2), ("-", 30.6, 6.9, 1.0), ("+5V", 32.6, 6.9, 0.9)]
textos = []
for t, x, y, h in TEXTOS:
    tp = TextPath((0, 0), t, size=h, prop=FontProperties(family="DejaVu Sans", weight="bold"))
    polys = []
    for poly in tp.to_polygons():
        if len(poly) >= 3:
            polys.append(Polygon([(px, -py) for px, py in poly]))
    g = unary_union([p for p in polys if p.is_valid])
    # letras com miolo (o, e, a): o maior anel e o contorno, os de dentro furam
    g = unary_union(polys[0:1]).symmetric_difference(unary_union(polys[1:])) if len(polys) > 1 else g
    g = affinity.translate(g, x, y)
    textos.append(g)
    copper.append(("TEXTO", g))

# ------------------------------------------------------------------ montagem do cobre
placa = box(0, 0, LARG, ALT)
area_util = placa.buffer(-MARGEM_BORDA, join_style=2)

por_net = {}
for n, g in copper:
    por_net.setdefault(n, []).append(g)
por_net = {n: unary_union(gs) for n, gs in por_net.items()}

# o PLANO DE TERRA: tudo que sobra, longe das outras redes
outras = unary_union([g for n, g in por_net.items() if n != "GND"])
pour = area_util.difference(outras.buffer(FOLGA_POUR, join_style=1))
gnd_total = unary_union([pour, por_net.get("GND")])
# so fica a parte do plano que encosta num pad de terra (ilha solta e lixo)
pads_gnd = por_net["GND"]
partes = list(gnd_total.geoms) if isinstance(gnd_total, MultiPolygon) else [gnd_total]
gnd_ok = unary_union([p for p in partes if p.intersects(pads_gnd)])
ilhas = [p for p in partes if not p.intersects(pads_gnd)]
por_net["GND"] = gnd_ok

# furos: o guia da broca e cobre REMOVIDO no centro de cada pad
guias = unary_union([Point(x, y).buffer(GUIA_BROCA / 2, resolution=16) for x, y, d, _ in furos])
cobre_final = unary_union(list(por_net.values())).difference(guias).intersection(area_util)

# ------------------------------------------------------------------ DRC
falhas = []
nets = [n for n in por_net if n != "TEXTO"]
for i, a in enumerate(nets):
    for b in nets[i + 1:]:
        d = por_net[a].distance(por_net[b])
        if d < FOLGA_MIN:
            falhas.append(f"FOLGA {a} x {b}: {d:.2f} mm (minimo {FOLGA_MIN})")
for n in nets:
    if n == "TEXTO":
        continue
    d = por_net[n].distance(por_net["TEXTO"]) if "TEXTO" in por_net else 99
    if d < FOLGA_MIN:
        falhas.append(f"FOLGA {n} x TEXTO: {d:.2f} mm")

# conectividade: cada rede tem de ser UM pedaco (contando os jumpers como fio)
def pedacos(g):
    return list(g.geoms) if isinstance(g, MultiPolygon) else [g]

for n in nets:
    if n.startswith("NC_"):
        continue
    ps = pedacos(por_net[n])
    grupos = [[p] for p in ps]
    for a, b, jn, _ in jumpers:
        if jn != n:
            continue
        ia = next((k for k, gr in enumerate(grupos) if any(p.contains(Point(a)) for p in gr)), None)
        ib = next((k for k, gr in enumerate(grupos) if any(p.contains(Point(b)) for p in gr)), None)
        if ia is not None and ib is not None and ia != ib:
            grupos[ia] += grupos[ib]; grupos.pop(ib)
    if len(grupos) != 1:
        onde = "; ".join("x %.1f-%.1f y %.1f-%.1f" % unary_union(gr).bounds[0::2] + unary_union(gr).bounds[1::2]
                         if False else "(%.1f,%.1f)-(%.1f,%.1f)" % unary_union(gr).bounds for gr in grupos)
        falhas.append(f"REDE {n} PARTIDA em {len(grupos)} pedacos: {onde}")

# cada pino de conector/pad do E22 pertence a rede esperada e esta ligado a ela
esperado = []
for p, n in NET_E22.items():
    if n:
        esperado.append((n, E22_PAD[p], f"E22 pad {p}"))
for k, n in P1.items():
    if n:
        esperado.append((n, (J2X[k], J2Y), f"J2 pino {k}"))
for k, n in P3.items():
    if n:
        esperado.append((n, (J1X[k], J1Y), f"J1 pino {k}"))
for i, n in enumerate(GPS):
    esperado.append((n, (J4X[i], J4Y), f"J4 {n}"))
for n, xy, desc in esperado:
    if not por_net[n].buffer(0.01).contains(Point(xy)):
        falhas.append(f"LIGACAO {desc} nao esta na rede {n}")

# ------------------------------------------------------------------ saidas
DPI = 1200
PX = DPI / 25.4


def png(geom, nome, preto_e="geom"):
    """geom em PRETO sobre branco (preto_e='geom'), na escala exata (1200 dpi)."""
    W, H = int(round(LARG * PX)), int(round(ALT * PX))
    img = Image.new("1", (W, H), 1)
    # cada poligono vira uma MASCARA propria (contorno cheio, buracos vazios) e so
    # entao e carimbado de preto: desenhar os buracos direto na imagem apagava o
    # que outro poligono ja tinha pintado ali dentro (os guias da broca sumiam)
    for p in pedacos(geom):
        if p.is_empty:
            continue
        m = Image.new("1", (W, H), 0)
        dm = ImageDraw.Draw(m)
        dm.polygon([(x * PX, y * PX) for x, y in p.exterior.coords], fill=1)
        for r in p.interiors:
            dm.polygon([(x * PX, y * PX) for x, y in r.coords], fill=0)
        img.paste(0, mask=m)
    img.save(os.path.join(SAIDA, nome), dpi=(DPI, DPI))


def svg(geom, nome):
    paths = []
    for p in pedacos(geom):
        if p.is_empty:
            continue
        aneis = [p.exterior] + list(p.interiors)
        dd = " ".join("M " + " L ".join(f"{x:.4f},{y:.4f}" for x, y in r.coords) + " Z" for r in aneis)
        paths.append(f'<path d="{dd}" fill="black" fill-rule="evenodd"/>')
    with open(os.path.join(SAIDA, nome), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{LARG}mm" height="{ALT}mm" viewBox="0 0 {LARG} {ALT}">\n')
        f.write(f'<rect x="0" y="0" width="{LARG}" height="{ALT}" fill="none" stroke="red" stroke-width="0.05"/>\n')
        f.write("\n".join(paths) + "\n</svg>\n")


def dxf(geom, nome):
    doc = ezdxf.new("R2010", setup=True)
    doc.units = ezdxf.units.MM
    msp = doc.modelspace()
    for p in pedacos(geom):
        if p.is_empty:
            continue
        for r in [p.exterior] + list(p.interiors):
            # DXF tem y para CIMA: espelha o y para a peca sair igual ao PNG
            msp.add_lwpolyline([(x, ALT - y) for x, y in r.coords], close=True, dxfattribs={"layer": "QUEIMAR"})
    msp.add_lwpolyline([(0, 0), (LARG, 0), (LARG, ALT), (0, ALT)], close=True, dxfattribs={"layer": "CONTORNO", "color": 1})
    doc.saveas(os.path.join(SAIDA, nome))


queimar = placa.difference(cobre_final)          # a laser tira a tinta daqui (o acido come)
# A placa de cobre crua e MAIOR que 40 x 70: os arquivos da laser tem 3 mm de sobra
# em volta e a LINHA DE CORTE gravada (0,3 mm sem cobre, colada por fora da borda).
# Depois de corroer, corte em cima da linha.
SOBRA = 3.0
corte = box(-0.3, -0.3, LARG + 0.3, ALT + 0.3).difference(placa)
laser = affinity.translate(unary_union([queimar, corte]), SOBRA, SOBRA)
LARG0, ALT0 = LARG, ALT
LARG, ALT = LARG0 + 2 * SOBRA, ALT0 + 2 * SOBRA
png(laser, "laser_queimar.png")
svg(laser, "laser_queimar.svg")
dxf(laser, "laser_queimar.dxf")
LARG, ALT = LARG0, ALT0
png(cobre_final, "cobre.png")
svg(cobre_final, "cobre.svg")

with open(os.path.join(SAIDA, "furos.csv"), "w", newline="") as f:
    w = csv.writer(f, delimiter=";")
    w.writerow(["x_mm_lado_cobre", "y_mm", "broca_mm", "o_que_e"])
    for x, y, dmm, desc in sorted(furos, key=lambda t: (t[1], t[0])):
        w.writerow([f"{x:.2f}", f"{y:.2f}", f"{dmm:.1f}", desc])

# regua de escala: 100 mm de traco + marcas de 10 em 10
reg = unary_union([box(0, 0, 100, 0.6)] + [box(i * 10, 0, i * 10 + 0.3, 3 if i % 5 == 0 else 1.8) for i in range(11)])
W, H = int(round(104 * PX)), int(round(6 * PX))
img = Image.new("1", (W, H), 1); d = ImageDraw.Draw(img)
for p in pedacos(reg):
    d.polygon([((x + 2) * PX, (y + 1.5) * PX) for x, y in p.exterior.coords], fill=0)
img.save(os.path.join(SAIDA, "regua_100mm.png"), dpi=(DPI, DPI))

# ------------------------------------------------------------------ desenho de conferencia e montagem
CORES = {"GND": "#7a7a7a", "5V": "#d62728", "3V3": "#ff7f0e", "TEXTO": "#444444"}


def desenha(ax, espelha, titulo):
    def tx(x):
        return LARG - x if espelha else x
    for n, g in por_net.items():
        cor = CORES.get(n, "#1f77b4" if not n.startswith("NC") else "#bbbbbb")
        for p in pedacos(g):
            if p.is_empty:
                continue
            xs, ys = zip(*p.exterior.coords)
            ax.fill([tx(x) for x in xs], ys, color=cor, alpha=0.55 if n == "GND" else 0.9, lw=0)
            for r in p.interiors:
                xs, ys = zip(*r.coords); ax.fill([tx(x) for x in xs], ys, color="white", lw=0)
    for x, y, dmm, desc in furos:
        ax.add_patch(plt.Circle((tx(x), y), dmm / 2, color="black"))
    for ref, g, rot in pecas:
        xs, ys = zip(*g.exterior.coords)
        ax.plot([tx(x) for x in xs], ys, color="green" if espelha else "#2ca02c", lw=0.8, ls="--" if ref == "U1" and espelha else "-")
        c = g.centroid
        ax.text(tx(c.x), c.y, rot, ha="center", va="center", fontsize=5, color="darkgreen")
    if espelha:
        for a, b, n, desc in jumpers:
            ax.plot([tx(a[0]), tx(b[0])], [a[1], b[1]], color="#9467bd", lw=2.2)
            ax.text((tx(a[0]) + tx(b[0])) / 2, (a[1] + b[1]) / 2, desc.split(":")[0], color="#9467bd", fontsize=6, weight="bold")
    ax.add_patch(plt.Rectangle((0, 0), LARG, ALT, fill=False, color="black", lw=1))
    ax.set_xlim(-1, LARG + 1); ax.set_ylim(ALT + 1, -1); ax.set_aspect("equal"); ax.set_title(titulo, fontsize=8)
    ax.tick_params(labelsize=5)


fig, axs = plt.subplots(1, 2, figsize=(9, 8.5))
desenha(axs[0], False, "LADO DO COBRE (o que a laser ve) - E22 fica deste lado")
desenha(axs[1], True, "LADO DOS COMPONENTES (espelhado) - conectores, C1 e os 3 jumpers")
plt.tight_layout(); plt.savefig(os.path.join(SAIDA, "montagem.png"), dpi=220); plt.close()

fig, ax = plt.subplots(figsize=(4.5, 8))
desenha(ax, False, "conferencia: cada cor e uma rede; cinza = terra")
for n, xy, desc in esperado:
    pass
plt.tight_layout(); plt.savefig(os.path.join(SAIDA, "conferencia_redes.png"), dpi=260); plt.close()

# ------------------------------------------------------------------ relatorio
print(f"placa {LARG} x {ALT} mm | {len(furos)} furos | {len(jumpers)} jumpers | ilhas de terra descartadas: {len(ilhas)}")
print(f"menor folga entre redes: {min(por_net[a].distance(por_net[b]) for i, a in enumerate(nets) for b in nets[i+1:]):.2f} mm")
if falhas:
    print("\nFALHAS:")
    for f in falhas:
        print("  " + f)
    sys.exit(1)
print("DRC OK: folgas >= %.1f mm, todas as redes inteiras, todos os pinos ligados" % FOLGA_MIN)
