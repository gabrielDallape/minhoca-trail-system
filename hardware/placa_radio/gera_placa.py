"""PLACA DO RADIO (MTS) v3 - E22-900M30S, face unica, TUDO DO LADO DO COBRE.

    python hardware/placa_radio/gera_placa.py

A placa NAO tem furo: o lado da fibra fica colado na traseira da tela, e tudo
(o E22, os SMD e os fios) e soldado por cima, no cobre. Os cabos da tela (P1 e
P3) chegam CORTADOS e cada fio e soldado num pad - sem conector. Isso deixa os
pads na ordem dos pinos do E22: nenhuma trilha cruza outra e nao ha jumper.

Gera, em hardware/placa_radio/saida/:
  laser_queimar.png/.svg/.dxf  O QUE A LASER TIRA DA TINTA (tudo que NAO e cobre)
  cobre.png/.svg               o cobre que fica (positivo, para conferir)
  guia_1_montagem.png          a placa montada: cada peca e cada fio no lugar
  guia_2_ligacoes.png          o que liga em que: pino da tela -> pad da placa
  guia_3_vao_e22.png           os vaos debaixo do E22, ampliados
  conferencia_redes.png        cada rede numa cor
  regua_100mm.png              teste de escala: grave e meca 100 mm

COORDENADAS: mm, origem no canto superior esquerdo, x para a direita, y para
baixo, VISTO PELO LADO DO COBRE - que e o lado de cima da placa e o que a laser
enxerga. Os arquivos da laser NAO sao espelhados.

Fontes das medidas:
  E22-900M30S: footprint oficial da LCSC/JLCPCB (peca C411294), baixado com
    JLC2KiCadLib em 2026-10-01: 38,5 x 24 mm, 22 pads 2,5 x 1,5 mm, passo 2,54 mm.
  Pinagem e GPIOs: firmware/mts/hal/pinos.h; P1/P3 da tela: docs/MONTAGEM.md.
  As pecas em volta: ESTUDO_PECAS.md.
"""
import math
import os
import sys
from functools import reduce

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
TR_POT = 1.6                    # 5 V
FOLGA_POUR = 0.6                # do plano de terra a qualquer outra coisa
FOLGA_MIN = 0.4                 # o DRC reprova abaixo disto
MARGEM_BORDA = 0.5              # o cobre para antes da borda
# Debaixo do E22 o vao entre pad de sinal e terra e maior: o meio-furo assenta em
# cima do pad, e um modulo soldado um pouco torto encostaria no terra com 0,6 mm.
VAO_E22 = 1.2

# ------------------------------------------------------------------ o que existe
copper = []      # (net, geometria)
pecas = []       # (ref, contorno, rotulo) - para os guias
fios = []        # (net, (x, y), rotulo, origem) - cada pad de fio
rotulos = []     # (texto, altura, x, y, angulo, ancora_x, ancora_y) - vazados no terra
alivio = []      # pads de terra com alivio termico (anel queimado com 4 pontes)


def pad_rect(net, cx, cy, w, h):
    g = box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    copper.append((net, g))
    # Pad de terra dentro do plano: sem o anel ele SOME na placa (nao se ve onde
    # soldar) e o plano puxa o calor do ferro. Os do E22 nao passam por aqui.
    if net == "GND":
        alivio.append(g)


def trilha(net, pts, w=TR_SINAL):
    copper.append((net, LineString(pts).buffer(w / 2, cap_style=1, join_style=1, resolution=12)))


# ------------------------------------------------------------------ E22-900M30S
# Centro do modulo; offsets dos pads do footprint oficial (y para baixo).
# Vista de CIMA (etiqueta para fora): pinos 1-11 na direita (o 1 embaixo), 12-22 na
# esquerda (o 12 em cima) - o mesmo desenho do docs/MONTAGEM.md, passo 2.
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
    x = E22X + (12 + 0.25 if p <= 11 else -12 - 0.25)
    y = E22Y + OFF[p]
    E22_PAD[p] = (x, y)
    # SEM alivio termico: o terra do modulo e o terra do RF, quanto mais cobre melhor
    copper.append((NET_E22[p] or f"NC_E22_{p}", box(x - 1.5, y - 0.75, x + 1.5, y + 0.75)))
modulo_e22 = box(E22X - 12, E22Y - 17.84, E22X + 12, E22Y + 20.66)
pecas.append(("U1", modulo_e22, "E22-900M30S"))


def pe(p):          # borda EXTERNA do pad p do E22 (onde a trilha chega)
    x, y = E22_PAD[p]
    return (x + 1.5 if p <= 11 else x - 1.5, y)


def ye(p):
    return E22_PAD[p][1]


# ------------------------------------------------------------------ pads dos fios
# Cada fio do cabo cortado e soldado num pad alongado no sentido em que o fio chega.
# O rotulo leva o NUMERO DO PINO DA TELA: e por ele que se conta o fio no cabo.
FIO_W, FIO_H = 1.8, 3.2         # sinal
POT_W, POT_H = 2.4, 3.6         # alimentacao (fio mais grosso)


def pad_fio(net, x, y, rot, origem, w=FIO_W, h=FIO_H):
    pad_rect(net, x, y, w, h)
    fios.append((net, (x, y), rot, origem, w, h))


# --- em cima, a esquerda: os fios do P1 (NRST, BUSY, DIO1 e um terra)
YTOP = 8.0
P1_PADS = [("NRST", 2.2, "2 NRST"), ("BUSY", 5.4, "4 BUSY"), ("DIO1", 8.6, "3 DIO1"), ("GND", 11.8, "8 GND")]
for n, x, r in P1_PADS:
    pad_fio(n, x, YTOP, r, "P1")
# --- em cima, ao centro: a alimentacao vinda do step-down
X_GND_POT, X_5V = 22.0, 26.0
pad_fio("GND", X_GND_POT, YTOP, "GND", "step-down", POT_W, POT_H)
pad_fio("5V", X_5V, YTOP, "+5V", "step-down", POT_W, POT_H)
# --- no canto de cima a direita: o TXEN (tambem do P1), que desce pela borda
TXEN_PAD = (38.5, 3.4)
pad_fio("TXEN", *TXEN_PAD, "1 TXEN", "P1")

# --- embaixo: os fios do P3. Os do GPS soldam NO MESMO PAD do fio do P3 que vai
#     para o mesmo pino da tela (TXD do GPS com o pino 8, e assim por diante).
YBOT = 60.0
P3_PADS = [("MISO", "3 MISO"), ("MOSI", "5 MOSI"), ("SCK", "4 SCK"), ("NSS", "6 NSS"), ("GND", "11 GND"),
           ("GPS_TX", "8 TXD"), ("GPS_RX", "9 RXD"), ("PPS", "10 PPS"), ("3V3", "12 3V3")]
P3X = {}
for i, (n, r) in enumerate(P3_PADS):
    P3X[n] = 2.0 + 3.6 * i
    pad_fio(n, P3X[n], YBOT, r, "P3")
X_RXEN = 37.0
pad_fio("RXEN", X_RXEN, YBOT, "7 RXEN", "P3")

# ------------------------------------------------------------------ trilhas
# --- P1 -> lado esquerdo do E22. O canal mais de fora vai ao pino mais de baixo:
#     NRST (15) por fora, DIO1 (13) por dentro. Nada se cruza.
CAN1 = {"NRST": (1.1, 10.3, 15), "BUSY": (2.3, 11.4, 14), "DIO1": (3.5, 12.5, 13)}
for n, x0, _ in P1_PADS[:3]:
    xc, yv, p = CAN1[n]
    trilha(n, [(x0, YTOP), (x0, yv), (xc, yv), (xc, ye(p)), pe(p)])

# --- P3 -> lado esquerdo do E22, subindo. O canal de fora vai ao pino mais de cima:
#     MISO (16) por fora, NSS (19) por dentro. Estes canais e os do P1 dividem a
#     mesma faixa, mas em alturas diferentes (P1 acima do pino 15, P3 abaixo do 16).
CAN3 = {"MISO": (1.1, 56.9, 16), "MOSI": (2.3, 55.7, 17), "SCK": (3.5, 54.5, 18), "NSS": (4.7, 53.3, 19)}
for n, (xc, yv, p) in CAN3.items():
    trilha(n, [(P3X[n], YBOT), (P3X[n], yv), (xc, yv), (xc, ye(p)), pe(p)])

# --- alimentacao: pad +5V -> por cima -> desce pela direita -> ramais dos pads 10 e 9
X5V = 36.9
trilha("5V", [(X_5V, YTOP), (X5V, YTOP), (X5V, ye(9))], TR_POT)
trilha("5V", [pe(10), (X5V, ye(10))], TR_POT)
trilha("5V", [pe(9), (X5V, ye(9))], TR_POT)

# --- TXEN: do canto, desce colado na borda (por fora do 5 V) e entra no pad 7
X_TXEN = 39.1
trilha("TXEN", [TXEN_PAD, (X_TXEN, TXEN_PAD[1]), (X_TXEN, ye(7)), pe(7)])
# R1 (10 k, 1206, em pe): pull-down do TXEN para a faixa de terra da borda
pad_rect("TXEN", 38.6, ye(7) + 2.3, 1.4, 1.2)
trilha("TXEN", [(38.6, ye(7)), (38.6, ye(7) + 2.3)])
pad_rect("GND", 38.6, ye(7) + 5.3, 1.4, 1.2)
pecas.append(("R1", box(37.8, ye(7) + 1.6, 39.4, ye(7) + 6.0), "R1 10k"))

# --- RXEN: do pad de baixo, sobe reto ate o pad 6
trilha("RXEN", [(X_RXEN, YBOT), (X_RXEN, ye(6)), pe(6)])
# R2 (10 k, 1206, deitado): pull-down do RXEN, da trilha para o terra junto do modulo
pad_rect("RXEN", 37.4, 38.3, 1.2, 1.6)
pad_rect("GND", 34.4, 38.3, 1.2, 1.6)
pecas.append(("R2", box(33.7, 37.4, 38.1, 39.2), "R2 10k"))

# ------------------------------------------------------------------ capacitores
# C1: eletrolitico SMD 220 uF / 16 V, lata de 6,3 mm (ESTUDO_PECAS.md). Deitado na
# horizontal: o + na descida do 5 V, o - no terra. Pads largos: serve tambem um de
# furo com as pernas dobradas e soldadas por cima.
C1Y = 10.3
C1_MAIS, C1_MENOS = (36.4, C1Y), (30.8, C1Y)
pad_rect("5V", *C1_MAIS, 3.4, 1.8)
pad_rect("GND", *C1_MENOS, 3.4, 1.8)
pecas.append(("C1", box(30.3, C1Y - 3.3, 36.9, C1Y + 3.3), "C1 220uF"))
# C2 (10 uF) e C3 (100 nF), 1206, a menos de 10 mm dos pads 9 e 10 do E22
pad_rect("GND", 33.9, 14.7, 1.2, 1.6)
pad_rect("5V", X5V, 14.7, 1.2, 1.6)
pecas.append(("C2", box(33.2, 13.8, 37.6, 15.6), "C2 10uF"))
pad_rect("GND", 34.8, 16.6, 1.6, 1.2)
pad_rect("5V", 34.8, 19.6, 1.6, 1.2)
pecas.append(("C3", box(33.9, 15.8, 35.7, 20.4), "C3 100nF"))
trilha("GND", [pe(11), (34.8, ye(11)), (34.8, 16.6)])

# ------------------------------------------------------------------ nomes (vazados no terra)
# Cada nome e GRAVADO no plano de terra: a laser queima a letra, o acido come o cobre
# dela e a letra aparece na cor da fibra. Letra vazada aguenta tamanho menor que
# letra de cobre (que o acido come por baixo). A letra fica a DIST_ROT do pad: mais
# perto que isso ela se funde com o vao queimado em volta dele ("5 MOSI" virava "5 MOS").
DIST_ROT = 1.15
for net, (x, y), rot, origem, w, h in fios:
    if origem == "P1" and net == "TXEN":
        rotulos.append((rot, 1.1, x - w / 2 - DIST_ROT, y, 0, "dir", "c"))        # deitado, a esquerda
    elif y < ALT / 2:
        rotulos.append((rot, 0.95, x, y - h / 2 - DIST_ROT, -90, "c", "base"))    # em pe, acima do pad
    else:
        rotulos.append((rot, 1.1, x, y + h / 2 + DIST_ROT, -90, "c", "topo"))     # em pe, abaixo do pad
rotulos += [("P1", 1.6, 15.6, YTOP, 0, "c", "c"),
            ("P3", 1.6, 24.0, 55.6, 0, "c", "c"),
            ("MTS v3", 1.2, 16.6, 2.4, 0, "c", "c"),
            ("1", 1.3, 35.3, ye(1), 0, "c", "c"),                                 # o pino 1 do E22
            ("-", 1.6, 27.6, C1Y + 1.4, 0, "c", "c")]                                    # o - do C1


def texto(t, h, negrito=True):
    """Texto como poligono (mm, y para baixo, origem na linha de base)."""
    tp = TextPath((0, 0), t, size=h, prop=FontProperties(family="DejaVu Sans", weight="bold" if negrito else "normal"))
    polys = [Polygon([(px, -py) for px, py in poly]).buffer(0) for poly in tp.to_polygons() if len(poly) >= 3]
    # XOR de todos os contornos: o miolo do P, do e, do 0 sai vazado.
    return reduce(lambda a, b: a.symmetric_difference(b), polys, Polygon())


def poe(t, h, x, y, ang=0, ax="c", ay="c"):
    """Texto girado (ang -90 = em pe) e ancorado pela caixa."""
    g = affinity.rotate(texto(t, h), ang, origin=(0, 0))
    x0, y0, x1, y1 = g.bounds
    dx = {"c": x - (x0 + x1) / 2, "esq": x - x0, "dir": x - x1}[ax]
    dy = {"c": y - (y0 + y1) / 2, "topo": y - y0, "base": y - y1}[ay]
    return affinity.translate(g, dx, dy)


# o traco da letra engorda 0,06 mm de cada lado: risco de laser fino demais o acido
# nao abre
letras = [(r, poe(r, h, x, y, a, ax, ay).buffer(0.06, join_style=2)) for r, h, x, y, a, ax, ay in rotulos]

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
# O plano PASSA debaixo do E22, de proposito: o manual Ebyte (E22-M V1.5, 4.1) pede
# "all copper-covered and well grounded" ali. Ja foi tirado uma vez (2026-10-01) por
# medo de curto com o fundo do modulo - o risco so existe se o fundo tiver metal
# exposto: confira antes de soldar (ESTUDO_PECAS.md, 3.1).
pads_sinal_e22 = unary_union([box(x - 1.5, y - 0.75, x + 1.5, y + 0.75)
                              for p, (x, y) in E22_PAD.items() if NET_E22[p] != "GND"])
pour = pour.difference(pads_sinal_e22.buffer(VAO_E22, join_style=2).intersection(modulo_e22))
# alivio termico: anel de 0,45 mm queimado em volta do pad de terra, com 4 pontes de
# 0,8 mm (uma por lado) que o mantem no plano
for pg in alivio:
    c = pg.centroid
    pontes = unary_union([box(c.x - 0.4, c.y - 10, c.x + 0.4, c.y + 10), box(c.x - 10, c.y - 0.4, c.x + 10, c.y + 0.4)])
    pour = pour.difference(pg.buffer(0.45, join_style=2).difference(pg).difference(pontes))
gnd_total = unary_union([pour, por_net["GND"]])


def pedacos(g):
    return list(g.geoms) if isinstance(g, MultiPolygon) else [g]


# so fica a parte do plano que encosta num pad de terra (ilha solta e lixo)
pads_gnd = por_net["GND"]
partes = pedacos(gnd_total)
gnd_ok = unary_union([p for p in partes if p.intersects(pads_gnd)])
ilhas = [p for p in partes if not p.intersects(pads_gnd)]
# os nomes: a letra sai do terra; o miolo das letras (o do R, do 0) fica como ilha
# de cobre solta, que nao e terra - vira a "rede" TEXTO
vazado = unary_union([g for _, g in letras])
gnd_rot = gnd_ok.difference(vazado)
gnd_partes = pedacos(gnd_rot)
por_net["GND"] = unary_union([p for p in gnd_partes if p.intersects(pads_gnd.difference(vazado))])
miolos = [p for p in gnd_partes if not p.intersects(pads_gnd.difference(vazado))]
if miolos:
    por_net["TEXTO"] = unary_union(miolos)
cobre_final = unary_union(list(por_net.values())).intersection(area_util)

# ------------------------------------------------------------------ DRC
falhas = []
nets = [n for n in por_net if n != "TEXTO"]
for i, a in enumerate(nets):
    for b in nets[i + 1:]:
        d = por_net[a].distance(por_net[b])
        if d < FOLGA_MIN:
            falhas.append(f"FOLGA {a} x {b}: {d:.2f} mm (minimo {FOLGA_MIN})")

# cada rede tem de ser UM pedaco (nao ha jumper nesta versao)
for n in nets:
    if n.startswith("NC_"):
        continue
    ps = pedacos(por_net[n])
    if len(ps) != 1:
        onde = "; ".join("(%.1f,%.1f)-(%.1f,%.1f)" % p.bounds for p in ps)
        falhas.append(f"REDE {n} PARTIDA em {len(ps)} pedacos: {onde}")

# cada pad do E22 e cada pad de fio pertence a rede esperada
esperado = [(n, E22_PAD[p], f"E22 pad {p}") for p, n in NET_E22.items() if n]
esperado += [(n, xy, f"fio {rot}") for n, xy, rot, _, _, _ in fios]
for n, xy, desc in esperado:
    if not por_net[n].buffer(0.01).contains(Point(xy)):
        falhas.append(f"LIGACAO {desc} nao esta na rede {n}")

# a pinagem tem de bater com o firmware e com a tela (docs/MONTAGEM.md)
PINO_TELA = {"TXEN": ("P1", 1), "NRST": ("P1", 2), "DIO1": ("P1", 3), "BUSY": ("P1", 4),
             "MISO": ("P3", 3), "SCK": ("P3", 4), "MOSI": ("P3", 5), "NSS": ("P3", 6), "RXEN": ("P3", 7),
             "GPS_TX": ("P3", 8), "GPS_RX": ("P3", 9), "PPS": ("P3", 10), "3V3": ("P3", 12)}
for n, xy, rot, origem, _, _ in fios:
    if n in PINO_TELA:
        c, k = PINO_TELA[n]
        if origem != c or not rot.startswith(f"{k} "):
            falhas.append(f"ROTULO {rot}: o fio {n} e o pino {k} do {c}")

# um nome gravado nao pode encostar em trilha nem pad (a letra sairia cortada)
for r, g in letras:
    d = g.distance(outras)
    if d < FOLGA_POUR + 0.3:
        falhas.append(f"NOME '{r}' a {d:.2f} mm de uma trilha ou pad (se funde com o vao)")
    d = g.distance(unary_union(alivio))
    if d < 0.45 + 0.3:
        falhas.append(f"NOME '{r}' a {d:.2f} mm de um pad de terra")
    if not area_util.buffer(-0.2).contains(g):
        falhas.append(f"NOME '{r}' sai da placa")
    if g.intersects(modulo_e22):
        falhas.append(f"NOME '{r}' fica debaixo do E22")

# nenhuma peca encosta em outra nem em pad de fio
for i, (ra, ca, _) in enumerate(pecas):
    for rb, cb, _ in pecas[i + 1:]:
        if ra != "U1" and rb != "U1" and ca.intersects(cb):
            falhas.append(f"MECANICA: {ra} encosta no {rb}")
for ra, ca, _ in pecas:
    for n, (x, y), rot, _, w, h in fios:
        if ca.intersects(box(x - w / 2, y - h / 2, x + w / 2, y + h / 2)):
            falhas.append(f"MECANICA: {ra} em cima do pad do fio {rot}")

# ------------------------------------------------------------------ saidas
DPI = 1200
PX = DPI / 25.4


def png(geom, nome, larg, alt):
    """geom em PRETO sobre branco, na escala exata (1200 dpi)."""
    W, H = int(round(larg * PX)), int(round(alt * PX))
    img = Image.new("1", (W, H), 1)
    # cada poligono vira uma MASCARA propria (contorno cheio, buracos vazios) e so
    # entao e carimbado de preto: desenhar os buracos direto na imagem apagava o
    # que outro poligono ja tinha pintado ali dentro
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


def svg(geom, nome, larg, alt):
    paths = []
    for p in pedacos(geom):
        if p.is_empty:
            continue
        aneis = [p.exterior] + list(p.interiors)
        dd = " ".join("M " + " L ".join(f"{x:.4f},{y:.4f}" for x, y in r.coords) + " Z" for r in aneis)
        paths.append(f'<path d="{dd}" fill="black" fill-rule="evenodd"/>')
    with open(os.path.join(SAIDA, nome), "w") as f:
        f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{larg}mm" height="{alt}mm" viewBox="0 0 {larg} {alt}">\n')
        f.write(f'<rect x="0" y="0" width="{larg}" height="{alt}" fill="none" stroke="red" stroke-width="0.05"/>\n')
        f.write("\n".join(paths) + "\n</svg>\n")


def dxf(geom, nome, larg, alt):
    doc = ezdxf.new("R2010", setup=True)
    doc.units = ezdxf.units.MM
    msp = doc.modelspace()
    for p in pedacos(geom):
        if p.is_empty:
            continue
        for r in [p.exterior] + list(p.interiors):
            # DXF tem y para CIMA: espelha o y para a peca sair igual ao PNG
            msp.add_lwpolyline([(x, alt - y) for x, y in r.coords], close=True, dxfattribs={"layer": "QUEIMAR"})
    msp.add_lwpolyline([(0, 0), (larg, 0), (larg, alt), (0, alt)], close=True, dxfattribs={"layer": "CONTORNO", "color": 1})
    doc.saveas(os.path.join(SAIDA, nome))


queimar = placa.difference(cobre_final)          # a laser tira a tinta daqui (o acido come)
# A placa de cobre crua e MAIOR que 40 x 70: os arquivos da laser tem 3 mm de sobra
# em volta e a LINHA DE CORTE gravada (0,3 mm sem cobre, colada por fora da borda).
SOBRA = 3.0
corte = box(-0.3, -0.3, LARG + 0.3, ALT + 0.3).difference(placa)
laser = affinity.translate(unary_union([queimar, corte]), SOBRA, SOBRA)
LW, LH = LARG + 2 * SOBRA, ALT + 2 * SOBRA
png(laser, "laser_queimar.png", LW, LH)
svg(laser, "laser_queimar.svg", LW, LH)
dxf(laser, "laser_queimar.dxf", LW, LH)
png(cobre_final, "cobre.png", LARG, ALT)
svg(cobre_final, "cobre.svg", LARG, ALT)

# regua de escala: 100 mm de traco + marcas de 10 em 10
reg = unary_union([box(0, 0, 100, 0.6)] + [box(i * 10, 0, i * 10 + 0.3, 3 if i % 5 == 0 else 1.8) for i in range(11)])
W, H = int(round(104 * PX)), int(round(6 * PX))
img = Image.new("1", (W, H), 1); d = ImageDraw.Draw(img)
for p in pedacos(reg):
    d.polygon([((x + 2) * PX, (y + 1.5) * PX) for x, y in p.exterior.coords], fill=0)
img.save(os.path.join(SAIDA, "regua_100mm.png"), dpi=(DPI, DPI))

# ------------------------------------------------------------------ conferencia
CORES = {"GND": "#7a7a7a", "5V": "#d62728", "3V3": "#ff7f0e", "TEXTO": "#bbbbbb"}
fig, ax = plt.subplots(figsize=(4.5, 8))
for n, g in por_net.items():
    cor = CORES.get(n, "#1f77b4" if not n.startswith("NC") else "#bbbbbb")
    for p in pedacos(g):
        if p.is_empty:
            continue
        xs, ys = zip(*p.exterior.coords)
        ax.fill(xs, ys, color=cor, alpha=0.55 if n == "GND" else 0.9, lw=0)
        for r in p.interiors:
            xs, ys = zip(*r.coords); ax.fill(xs, ys, color="white", lw=0)
for ref, g, rot in pecas:
    xs, ys = zip(*g.exterior.coords)
    ax.plot(xs, ys, color="#2ca02c", lw=0.8)
for n, (x, y), rot, _, _, _ in fios:
    ax.text(x, y, n, ha="center", va="center", fontsize=3.2, rotation=90, color="white")
ax.add_patch(plt.Rectangle((0, 0), LARG, ALT, fill=False, color="black", lw=1))
ax.set_xlim(-1, LARG + 1); ax.set_ylim(ALT + 1, -1); ax.set_aspect("equal")
ax.set_title("conferencia: cada cor e uma rede; cinza = terra", fontsize=8); ax.tick_params(labelsize=5)
plt.tight_layout(); plt.savefig(os.path.join(SAIDA, "conferencia_redes.png"), dpi=260); plt.close()

# os arquivos da v2 (furos, gravacao do outro lado, conectores) nao valem mais
for velho in ("furos.csv", "laser_serigrafia.png", "laser_serigrafia.svg", "laser_serigrafia.dxf",
              "guia_1_conectores.png", "guia_2_lado_do_cobre.png", "guia_3_ligacoes.png",
              "guia_4_placa_montada.png", "zoom_vao_e22.png", "preview_laser_serigrafia_dxf.png", "montagem.png"):
    if os.path.exists(os.path.join(SAIDA, velho)):
        os.remove(os.path.join(SAIDA, velho))

import guias
guias.gera(globals())

# ------------------------------------------------------------------ relatorio
print(f"placa {LARG} x {ALT} mm | 0 furos | 0 jumpers | {len(fios)} pads de fio | "
      f"ilhas de terra descartadas: {len(ilhas)}")
print(f"menor folga entre redes: {min(por_net[a].distance(por_net[b]) for i, a in enumerate(nets) for b in nets[i+1:]):.2f} mm")
if falhas:
    print("\nFALHAS:")
    for f in falhas:
        print("  " + f)
    sys.exit(1)
print("DRC OK: folgas >= %.1f mm, todas as redes inteiras, todos os pinos ligados, nomes conferidos" % FOLGA_MIN)
