"""PLACA DO RADIO (MTS) - E22-900M30S + conectores da tela 7B + GPS, face unica.

    python hardware/placa_radio/gera_placa.py

Gera, em hardware/placa_radio/saida/:
  laser_queimar.png/.svg/.dxf  O QUE A LASER TIRA DA TINTA (tudo que NAO e cobre)
  cobre.png/.svg               o cobre que fica (positivo, para conferir)
  furos.csv / furos.png        cada furo: posicao, diametro, o que e
  laser_serigrafia.png/.svg/.dxf  os NOMES gravados no lado dos componentes (ja espelhado)
  guia_1_conectores.png        montagem do lado dos conectores
  guia_2_lado_do_cobre.png     montagem do lado do cobre (E22, SMD, JP3)
  guia_3_ligacoes.png          o que liga em que: tela, GPS, step-down, antena
  conferencia_redes.png        cada rede numa cor
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
from functools import reduce
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
    """Pad de furo. oval=(w,h) para pad alongado (o PH2.0 tem passo de 2 mm).
    furo=None: pad SEM furo, so para soldar um fio deitado do lado do cobre."""
    if oval:
        w, h = oval
        r = min(w, h) / 2
        g = LineString([(cx - (w / 2 - r), cy - (h / 2 - r)), (cx + (w / 2 - r), cy + (h / 2 - r))]).buffer(r)
    else:
        g = Point(cx, cy).buffer(diam / 2, resolution=24)
    copper.append((net, g))
    if furo:
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


# ------------------------------------------------------------------ carcacas (lado dos componentes)
# Medidas da camada F.Fab dos footprints oficiais do KiCad (Connector_JST.pretty):
#   PH vertical (B12B-PH-K): pino 1 em (0,0), pinos para +x, corpo de y -1,7 a +2,8
#   XH vertical (B2B-XH-A):  corpo de x -2,45 a +4,95, y -2,35 a +3,4
# Visto do lado dos componentes, o pino 1 do J1/J2 fica na DIREITA e os pinos andam
# para a esquerda: e o footprint girado 180 graus, entao o lado GROSSO (2,8 mm) da
# carcaca fica para CIMA (y menor). Foi essa conta que mostrou o furo do JP3 debaixo
# do plastico do J1 e o JP1 passando por cima do J2 - nenhum dos dois cabia.
corpos = {}      # ref -> contorno da carcaca, em coordenadas do LADO DO COBRE


def corpo_ph(ref, x1, y):
    """J1/J2: x1 = x do pino 1 no lado do cobre (pinos para +x no cobre)."""
    corpos[ref] = box(x1 - 1.95, y - 2.8, x1 + 22 + 1.95, y + 1.7)


# ------------------------------------------------------------------ J2 = P1 da tela
# JST-PH 2,0 mm 12 vias. Pino k do J2 = pino k do P1 (cabo "mesmo sentido").
P1 = {1: "TXEN", 2: "NRST", 3: "DIO1", 4: "BUSY", 5: None, 6: None, 7: None, 8: "GND",
      9: None, 10: None, 11: "GND", 12: None}
J2Y = 5.0
J2X = {k: 4.0 + (k - 1) * 2.0 for k in range(1, 13)}
for k in range(1, 13):
    pad_tht(P1[k] or f"NC_J2_{k}", J2X[k], J2Y, None, 0.8, f"J2 pino {k} ({P1[k] or 'nao usado'})", oval=(1.4, 2.4))
corpo_ph("J2", J2X[1], J2Y)
pecas.append(("J2", corpos["J2"], "J2 -> P1 da tela"))

# ------------------------------------------------------------------ J1 = P3 da tela
P3 = {1: None, 2: None, 3: "MISO", 4: "SCK", 5: "MOSI", 6: "NSS", 7: "RXEN", 8: "GPS_TX",
      9: "GPS_RX", 10: "PPS", 11: "GND", 12: "3V3"}
J1Y = 58.0
J1X = {k: 4.0 + (k - 1) * 2.0 for k in range(1, 13)}
for k in range(1, 13):
    pad_tht(P3[k] or f"NC_J1_{k}", J1X[k], J1Y, None, 0.8, f"J1 pino {k} ({P3[k] or 'nao usado'})", oval=(1.4, 2.4))
corpo_ph("J1", J1X[1], J1Y)
pecas.append(("J1", corpos["J1"], "J1 -> P3 da tela"))

# ------------------------------------------------------------------ J4 = GPS (2,54 mm, 5 vias)
J4Y = 65.5
GPS = ["GPS_TX", "GPS_RX", "PPS", "GND", "3V3"]          # da esquerda para a direita (lado do cobre)
J4X = [15.92 + i * 2.54 for i in range(5)]
for i, n in enumerate(GPS):
    pad_tht(n, J4X[i], J4Y, 1.8, 0.9, f"J4 GPS ({n})")
corpos["J4"] = box(J4X[0] - 1.27, J4Y - 1.27, J4X[4] + 1.27, J4Y + 1.27)
pecas.append(("J4", corpos["J4"], "J4 GPS"))

# ------------------------------------------------------------------ alimentacao (canto de cima, direita)
# J3: JST-XH 2,54 mm 2 vias (tem trava: nao inverte). O TERRA fica do lado de DENTRO
# e o +5V do lado da BORDA: assim a trilha de 5 V desce pela borda e o terra do
# capacitor encosta no plano (com o 5 V por dentro, o canto ficava uma ilha).
# C1: eletrolitico 220 uF / 10 V, radial 6,3 mm, passo 2,5 mm.
# O lado GROSSO da carcaca do XH (3,4 mm) fica para a borda de cima: virado para
# baixo ele encostava no C1.
J3Y = 3.4
J3 = [(31.0, J3Y, "GND"), (33.54, J3Y, "5V")]
for x, y, n in J3:
    pad_tht(n, x, y, 1.9, 1.0, f"J3 entrada 5,3 V ({'+' if n == '5V' else '-'})")
corpos["J3"] = box(31.0 - 2.45, J3Y - 3.4, 33.54 + 2.45, J3Y + 2.35)
pecas.append(("J3", corpos["J3"], "J3 5,3V"))
# A descida do 5 V NAO fica colada na borda: a faixa da borda e do TXEN (que antes
# atravessava a placa num jumper por cima do J2).
X5V = 37.6
C1 = [(33.1, 9.6, "GND"), (35.6, 9.6, "5V")]
for x, y, n in C1:
    pad_tht(n, x, y, 1.9, 0.9, f"C1 220uF ({'+' if n == '5V' else '-'})")
corpos["C1"] = Point(34.35, 9.6).buffer(3.15)
pecas.append(("C1", corpos["C1"], "C1"))
# C2 (10 uF) e C3 (100 nF), SMD 1206, a menos de 10 mm dos pads 9 e 10 do E22.
# C3 em pe, entre o pad 11 (terra) e o ramal do pad 10 (5 V).
pad_rect("GND", 35.4, 16.3, 1.6, 1.2); pad_rect("5V", 35.4, 19.3, 1.6, 1.2)
pecas.append(("C3", box(34.5, 15.5, 36.3, 20.1), "C3"))
trilha("GND", [pe(11), (35.4, 16.77), (35.4, 16.3)])
# C2 deitado acima do modulo: terra no plano, 5 V na descida
pad_rect("GND", 34.6, 12.8, 1.2, 1.6); pad_rect("5V", X5V, 12.8, 1.2, 1.6)
pecas.append(("C2", box(33.9, 11.9, 38.3, 13.7), "C2"))

# ------------------------------------------------------------------ trilhas
# --- alimentacao: J3+ -> pela borda -> C1+ -> C2 -> ramais para os pads 10 e 9
trilha("5V", [(33.54, J3Y), (X5V, J3Y), (X5V, E22_PAD[9][1])], TR_POT)
trilha("5V", [C1[1][:2], (X5V, C1[1][1])], TR_POT)
trilha("5V", [pe(10), (X5V, E22_PAD[10][1])], TR_POT)
trilha("5V", [pe(9), (X5V, E22_PAD[9][1])], TR_POT)

# --- P1 (J2, em cima): NRST, BUSY e DIO1 descem pelo canal da esquerda
CAN_NRST, CAN_BUSY, CAN_DIO1 = 1.0, 2.9, 4.8
trilha("NRST", [(J2X[2], J2Y), (J2X[2], 7.6), (CAN_NRST, 7.6), (CAN_NRST, E22_PAD[15][1]), pe(15)])
trilha("DIO1", [(J2X[3], J2Y), (J2X[3], 9.2), (CAN_DIO1, 9.2), (CAN_DIO1, E22_PAD[13][1]), pe(13)])
# BUSY cruza o DIO1: JUMPER (A2 perto do J2 -> B2 no canal)
A2, B2 = (10.0, 12.0), (CAN_BUSY, 12.4)
trilha("BUSY", [(J2X[4], J2Y), A2])
pad_tht("BUSY", *A2, 1.8, 0.8, "JUMPER JP2 lado A (BUSY)")
pad_tht("BUSY", *B2, 1.8, 0.8, "JUMPER JP2 lado B (BUSY)")
trilha("BUSY", [B2, (CAN_BUSY, E22_PAD[14][1]), pe(14)])
jumpers.append((A2, B2, "BUSY", "JP2: BUSY por cima do DIO1"))
# TXEN contorna a placa: sobe do pino 1, corre por CIMA do J2 e do J3, desce pela
# borda direita (por fora da descida do 5 V) e entra no pad 7. Antes era um jumper
# de 42 mm que passava por cima da carcaca do J2 - e o cabo do P1 nao encaixava.
X_TXEN, Y_TXEN = 39.1, 1.4
trilha("TXEN", [(J2X[1], J2Y), (J2X[1], Y_TXEN), (X_TXEN, Y_TXEN), (X_TXEN, E22_PAD[7][1]), pe(7)])
# R1 (10 k, 1206, em pe): pull-down do TXEN, para a faixa de terra da borda
pad_rect("TXEN", 38.6, E22_PAD[7][1] + 2.3, 1.4, 1.2)
trilha("TXEN", [(38.6, E22_PAD[7][1]), (38.6, E22_PAD[7][1] + 2.3)])
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
# O A3 fica 2 mm do pino, DEBAIXO do plastico do J1 (que avanca 2,8 mm). Nao ha
# outro lugar: acima esta o MOSI. Entao o JP3 e um fio DO LADO DO COBRE, soldado
# deitado nos dois pads (sem furo).
pad_tht("SCK", *A3, 1.6, None, "JUMPER JP3 lado A (SCK)")
pad_tht("SCK", *B3, 1.4, None, "JUMPER JP3 lado B (SCK)")
trilha("SCK", [B3, (CAN_SCK, E22_PAD[18][1]), pe(18)])
jumpers.append((A3, B3, "SCK", "JP3: SCK por cima do MOSI e do NSS (LADO DO COBRE)"))
# RXEN: por baixo do modulo, sobe pela faixa da direita ate o pad 6
trilha("RXEN", [(J1X[7], J1Y), (J1X[7], 55.4), (37.0, 55.4), (37.0, E22_PAD[6][1]), pe(6)])
# R2 (10 k, 1206, deitado): pull-down do RXEN, da trilha para o terra entre ela e o modulo
pad_rect("RXEN", 37.4, 38.3, 1.2, 1.6)
pad_rect("GND", 34.4, 38.3, 1.2, 1.6)
pecas.append(("R2", box(33.7, 37.4, 38.1, 39.2), "R2"))
# --- JP4: o TERRA de baixo (P3, GPS, borda direita) e o de cima (modulo, alimentacao,
#     P1) ficam separados pela trilha do RXEN, que passa por baixo do modulo. Um fio
#     curto do lado dos componentes os junta - sem ele o terra so se fecharia pela tela.
# a direita do modulo, 3,4 mm por cima do RXEN: o antigo (24,5 -> 30,5) passava
# por cima da ponta da carcaca do J1
A4, B4 = (33.0, 53.8), (33.0, 57.2)
pad_tht("GND", *A4, 1.6, 0.8, "JUMPER JP4 lado A (TERRA)")
pad_tht("GND", *B4, 1.6, 0.8, "JUMPER JP4 lado B (TERRA)")
jumpers.append((A4, B4, "GND", "JP4: junta os dois terras"))

# --- GPS: J1 pinos 8..12 descem para o J4
for k, i in ((8, 0), (9, 1), (10, 2), (11, 3), (12, 4)):
    n = P3[k]
    trilha(n, [(J1X[k], J1Y), (J1X[k], 61.6), (J4X[i], 63.2), (J4X[i], J4Y)])

# ------------------------------------------------------------------ texto no cobre
def texto(t, h, negrito=True):
    """Texto como poligono (mm, y para baixo, origem na linha de base)."""
    tp = TextPath((0, 0), t, size=h, prop=FontProperties(family="DejaVu Sans", weight="bold" if negrito else "normal"))
    polys = [Polygon([(px, -py) for px, py in poly]).buffer(0) for poly in tp.to_polygons() if len(poly) >= 3]
    # XOR de todos os contornos: o miolo do P, do e, do 0 sai vazado. (Antes era o
    # primeiro contorno XOR a uniao dos outros, e a uniao TAPAVA o miolo de toda
    # letra que nao fosse a primeira - o P do "GPS" saia cheio.)
    return reduce(lambda a, b: a.symmetric_difference(b), polys, Polygon())


TEXTOS = [("MTS E22 v2", 14.0, 69.2, 1.3), ("GPS", 28.0, 66.2, 1.0),
          ("P3", 1.0, 61.8, 1.2), ("P1", 28.6, 10.6, 1.2), ("-", 30.6, 6.9, 1.0), ("+5V", 32.6, 6.9, 0.9)]
textos = []
for t, x, y, h in TEXTOS:
    g = affinity.translate(texto(t, h), x, y)
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

# ------------------------------------------------------------------ serigrafia (lado dos componentes)
# Os NOMES de cada fio, gravados na laser no lado SEM cobre - que e onde os cabos
# encaixam. No lado do cobre nao cabem: o passo do PH e 2 mm, e letra de cobre
# menor que ~1,2 mm o acido come. Coordenadas aqui ja sao as do LADO DOS
# COMPONENTES (x espelhado): e assim que a placa entra na laser para esta gravacao.
def cx(x):
    return LARG - x


def espelha(g):
    return affinity.scale(g, xfact=-1, yfact=1, origin=(LARG / 2, 0))


def poe(t, h, x, y, ang=0, ax="c", ay="c", negrito=True):
    """Texto girado (ang: -90 = le de baixo para cima) e ancorado pela caixa."""
    g = affinity.rotate(texto(t, h, negrito), ang, origin=(0, 0))
    x0, y0, x1, y1 = g.bounds
    dx = {"c": x - (x0 + x1) / 2, "esq": x - x0, "dir": x - x1}[ax]
    dy = {"c": y - (y0 + y1) / 2, "topo": y - y0, "base": y - y1}[ay]
    return affinity.translate(g, dx, dy)


def vertical(t, x, ytopo, comp_max, h=1.3):
    """Rotulo em pe embaixo de um pino; a letra diminui ate caber em comp_max."""
    while h >= 0.85:
        g = poe(t, h, x, ytopo, -90, "c", "topo")
        if g.bounds[3] - g.bounds[1] <= comp_max:
            return g
        h -= 0.05
    falhas.append(f"SERIGRAFIA: '{t}' nao cabe em {comp_max:.1f} mm")
    return g


corpos_c = {r: espelha(g) for r, g in corpos.items()}
furos_c = [(cx(x), y, d) for x, y, d, _ in furos]
seri = []          # (geometria, conferir?, descricao)
contornos = []     # carcacas e marcas: nao entram na conferencia de folga


def ri(g, desc):
    seri.append((g, True, desc))


# carcacas desenhadas (para encaixar a peca no lugar certo) e o pino 1 do J1/J2
for r in ("J1", "J2", "J3", "J4"):
    contornos.append(corpos_c[r].exterior.buffer(0.1).intersection(placa.buffer(-0.3)))
contornos.append(corpos_c["C1"].exterior.buffer(0.1))
for y in (J2Y, J1Y):
    xb = corpos_c["J2"].bounds[2] + 0.25          # a direita da carcaca
    ri(Polygon([(xb, y), (xb + 1.1, y - 0.65), (xb + 1.1, y + 0.65)]), "pino 1")

# titulos
yJ2c = corpos_c["J2"].bounds[1]
ri(poe("J2 \u2192 P1 DA TELA", 1.1, corpos_c["J2"].centroid.x, yJ2c / 2 + 0.25), "titulo J2")
ri(poe("J1 \u2192 P3 DA TELA", 1.1, corpos_c["J1"].centroid.x, corpos_c["J1"].bounds[1] - 0.35, ay="base"), "titulo J1")
ri(poe("GPS", 2.0, corpos_c["J4"].bounds[0] / 2 + 0.3, J4Y), "titulo GPS")

# nomes dos pinos, em pe, embaixo de cada pino usado
NOMES_J1 = {3: "MISO", 4: "SCK", 5: "MOSI", 6: "NSS", 7: "RXEN", 8: "TXD", 9: "RXD", 10: "PPS", 11: "GND", 12: "3V3"}
NOMES_J4 = ["TXD", "RXD", "PPS", "GND", "VCC"]      # o nome impresso no proprio GPS
ytopo2 = corpos_c["J2"].bounds[3] + 0.35
for k, n in P1.items():
    if n:
        ri(vertical(n, cx(J2X[k]), ytopo2, 3.9), f"J2 pino {k}")
ytopo1 = corpos_c["J1"].bounds[3] + 0.35
for k, n in NOMES_J1.items():
    x = cx(J1X[k])
    dentro_j4 = corpos_c["J4"].bounds[0] - 0.5 < x < corpos_c["J4"].bounds[2] + 0.5
    lim = (corpos_c["J4"].bounds[1] - 0.3 if dentro_j4 else ALT - 0.6) - ytopo1
    ri(vertical(n, x, ytopo1, min(lim, 4.2)), f"J1 pino {k}")
ytopo4 = corpos_c["J4"].bounds[3] + 0.3
for i, n in enumerate(NOMES_J4):
    ri(vertical(n, cx(J4X[i]), ytopo4, ALT - 0.6 - ytopo4), f"J4 {n}")

# alimentacao: + a esquerda do J3 (o pino do 5 V e o da esquerda, visto deste lado)
ri(poe("+5V", 1.3, corpos_c["J3"].bounds[0] / 2 + 0.2, J3Y, -90), "J3 +")
ri(poe("GND", 1.0, cx(31.0) + 0.7, corpos_c["J3"].bounds[3] + 0.85), "J3 GND")
# C1: o + do eletrolitico, por fora do corpo, ao lado do furo do +
ri(poe("+", 1.8, corpos_c["C1"].bounds[0] / 2 + 0.2, C1[1][1]), "C1 +")

# os jumpers deste lado: um traco ligando os furos e o nome
for a, b, n, desc in jumpers:
    if "COBRE" in desc:
        continue
    A, B = (cx(a[0]), a[1]), (cx(b[0]), b[1])
    ln = LineString([A, B])
    L = ln.length
    contornos.append(LineString([ln.interpolate(0.95).coords[0], ln.interpolate(L - 0.95).coords[0]]).buffer(0.12))
    nome = desc.split(":")[0]
    m = ln.interpolate(0.5, normalized=True)
    if abs(B[0] - A[0]) > abs(B[1] - A[1]):          # deitado: nome embaixo
        ri(poe(nome, 1.0, m.x, m.y + 1.1, ay="topo"), nome)
    else:                                            # em pe: nome a direita
        ri(poe(nome, 1.0, m.x + 1.0, m.y, ax="esq"), nome)

# o meio da placa (o E22 esta do outro lado): identificacao e o aviso que queima radio
ri(poe("MTS", 5.0, LARG / 2, 23.0), "MTS")
ri(poe("RADIO E22 - v2", 1.5, LARG / 2, 27.5), "subtitulo")
ri(poe("LIGUE A ANTENA", 1.8, LARG / 2, 36.0), "aviso 1")
ri(poe("ANTES DO 5 V", 1.8, LARG / 2, 38.8), "aviso 2")

# conferencia: dentro da placa, longe dos furos, fora das carcacas
dentro = placa.buffer(-0.5)
furo_geo = unary_union([Point(x, y).buffer(d / 2 + 0.3) for x, y, d in furos_c])
for g, conf, desc in seri:
    if not conf:
        continue
    if not dentro.contains(g):
        falhas.append(f"SERIGRAFIA {desc}: sai da placa")
    if g.intersects(furo_geo):
        falhas.append(f"SERIGRAFIA {desc}: em cima de um furo")
    for r, c in corpos_c.items():
        if g.intersects(c):
            falhas.append(f"SERIGRAFIA {desc}: debaixo da carcaca do {r}")
serigrafia = unary_union([g for g, _, _ in seri] + contornos)

# mecanica: nenhuma carcaca encosta em outra nem em furo de jumper deste lado
refs = list(corpos_c)
for i, a in enumerate(refs):
    for b in refs[i + 1:]:
        d = corpos_c[a].distance(corpos_c[b])
        if d < 0.2:
            falhas.append(f"MECANICA: carcaca do {a} a {d:.2f} mm do {b}")
for a, b, n, desc in jumpers:
    if "COBRE" in desc:
        continue
    for xy in (a, b):
        P = Point(cx(xy[0]), xy[1]).buffer(0.4)
        for r, c in corpos_c.items():
            if P.distance(c) < 0.3:
                falhas.append(f"MECANICA: furo do {desc.split(':')[0]} debaixo/encostado na carcaca do {r}")

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
# a serigrafia vai na laser DEPOIS de cortar a placa em 40 x 70: o arquivo tem o
# tamanho exato da placa, para alinhar pelo canto
png(serigrafia, "laser_serigrafia.png")
svg(serigrafia, "laser_serigrafia.svg")
dxf(serigrafia, "laser_serigrafia.dxf")

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


import guias
guias.gera(globals())
if os.path.exists(os.path.join(SAIDA, "montagem.png")):
    os.remove(os.path.join(SAIDA, "montagem.png"))      # trocado pelos guia_*.png

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
