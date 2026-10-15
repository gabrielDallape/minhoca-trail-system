"""PLACA DO RADIO (MTS) v4 - versao de FABRICA (JLCPCB, montagem SMT).

Roda DENTRO do container do KiCad (ver gera.ps1). Tres etapas:

    python3 gera_pcb.py montar    -> saida/placa.kicad_pcb + saida/placa.dsn (para o Freerouting)
    (freerouting roteia placa.dsn -> placa.ses)
    python3 gera_pcb.py finalizar -> importa o .ses, enche o terra, costura, salva

Diferencas para a v3 (laser, face simples):
  - 2 camadas, terra dos dois lados; o top debaixo do E22 e todo terra, como o
    manual da Ebyte pede ("top layer under the module all copper-covered").
  - conectores JST-PH de verdade: cabo PH pronto ate o P1/P3 da tela, sem cortar fio.
  - entrada de 12 V do carro com fusivel, diodo de polaridade, TVS e buck proprio
    (AP63205, 5 V fixo). O step-down solto do chicote deixa de existir.
  - o E22 NAO e montado pela fabrica (DNP): o dono ja tem os modulos e solda a mao.

COORDENADAS: mm, origem no canto superior esquerdo, y para baixo, vista de cima.
"""
import os
import sys

import pcbnew

AQUI = os.path.dirname(os.path.abspath(__file__))
SAIDA = os.path.join(AQUI, "saida")
os.makedirs(SAIDA, exist_ok=True)
PCB = os.path.join(SAIDA, "placa.kicad_pcb")
DSN = os.path.join(SAIDA, "placa.dsn")
SES = os.path.join(SAIDA, "placa.ses")
LIBS = "/usr/share/kicad/footprints/"

LARG, ALT = 40.0, 70.0          # o mesmo espaco atras da tela que a v3 usava
# E22 encostado a 3,75 mm da borda esquerda: o canal da esquerda leva os 7 sinais do
# lado dos pinos 13-19, e a faixa da direita (9,3 mm) leva o buck e o banco do E22.
E22X, E22Y = 17.0, 48.64


def mm(v):
    return pcbnew.FromMM(v)


def pt(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


# ------------------------------------------------------------------ pecas
# ref: (biblioteca, footprint, x, y, rotacao, valor, LCSC, {pad: rede})
# LCSC conferido na API da JLC em 2026-10-08 (estoque e tipo basic/extended).
# Rotacao do KiCad (anti-horaria). Com 90 o pad 1 de uma peca de 2 pads vai para BAIXO.
PECAS = {
    # --- E22: footprint oficial da LCSC (C411294). DNP: soldado a mao.
    "U1": ("lib", "E22-900M30S", E22X, E22Y, 0, "E22-900M30S", "C411294", {
        1: "GND", 2: "GND", 3: "GND", 4: "GND", 5: "GND", 6: "RXEN", 7: "TXEN",
        9: "+5V", 10: "+5V", 11: "GND", 12: "GND", 13: "DIO1", 14: "BUSY",
        15: "NRST", 16: "MISO", 17: "MOSI", 18: "SCK", 19: "NSS", 20: "GND",
        22: "GND"}),   # 8 = NC; 21 = ANT (fica sem rede: a antena vai no IPEX do modulo)

    # --- conectores da tela (P1 e P3 da 7B, esquematico oficial em docs/MONTAGEM.md)
    # GPIO 46/47/48 (P1 5-7) ficam sem ligacao de proposito: VDD_IO_5, ver MONTAGEM.md.
    "J1": ("Connector_JST", "JST_PH_B12B-PH-SM4-TB_1x12-1MP_P2.00mm_Vertical", 20.0, 5.2, 0,
           "P1 tela", "C545752", {1: "TXEN", 2: "NRST", 3: "DIO1", 4: "BUSY", 8: "GND",
                                  11: "GND", "MP": "GND"}),
    "J2": ("Connector_JST", "JST_PH_B12B-PH-SM4-TB_1x12-1MP_P2.00mm_Vertical", 20.0, 14.4, 0,
           "P3 tela", "C545752", {3: "MISO", 4: "SCK", 5: "MOSI", 6: "NSS", 7: "RXEN",
                                  8: "GPS_TX", 9: "GPS_RX", 10: "PPS", 11: "GND_P3",
                                  12: "3V3", "MP": "GND"}),
    # GPS NEO-7M mini, na ordem do header dele: VCC GND TXD RXD PPS
    "J3": ("Connector_JST", "JST_PH_B5B-PH-SM4-TB_1x05-1MP_P2.00mm_Vertical", 9.0, 23.4, 0,
           "GPS", "C265085", {1: "3V3", 2: "GND_P3", 3: "GPS_TX", 4: "GPS_RX", 5: "PPS",
                              "MP": "GND"}),
    # 12 V do carro
    "J4": ("Connector_JST", "JST_PH_B2B-PH-SM4-TB_1x02-1MP_P2.00mm_Vertical", 35.0, 23.4, 0,
           "12V", "C160352", {1: "VIN", 2: "GND", "MP": "GND"}),

    # --- protecao da entrada
    "F1": ("Fuse", "Fuse_1210_3225Metric", 27.9, 23.0, 90, "PTC 0,75 A 24 V", "C209728",
           {1: "VIN_F", 2: "VIN"}),
    "D1": ("Diode_SMD", "D_SMA", 23.9, 23.0, 90, "SS34", "C8678",
           {1: "VIN_P", 2: "VIN_F"}),          # pad 1 = catodo
    "D2": ("Diode_SMD", "D_SMA", 19.6, 23.0, 90, "SMAJ18A", "C173513",
           {1: "VIN_P", 2: "GND"}),            # TVS: catodo no +

    # --- buck 12 V -> 5 V (AP63205: 5 V fixo; valores da Tabela 2 do datasheet)
    "U2": ("Package_TO_SOT_SMD", "TSOT-23-6", 32.9, 56.0, 0, "AP63205WU-7", "C2071056",
           {1: "5V_BUCK", 2: "VIN_P", 3: "VIN_P", 4: "GND", 5: "SW", 6: "BST"}),
    "L1": ("Inductor_SMD", "L_Changjiang_FNR4030S", 37.4, 56.0, 0, "4,7 uH", "C167874",
           {1: "SW", 2: "5V_BUCK"}),
    "C4": ("Capacitor_SMD", "C_1206_3216Metric", 33.05, 59.5, 0, "10 uF 50 V", "C13585",
           {1: "VIN_P", 2: "GND"}),
    "C5": ("Capacitor_SMD", "C_0603_1608Metric", 33.05, 61.7, 0, "100 nF 50 V", "C14663",
           {1: "VIN_P", 2: "GND"}),
    "C6": ("Capacitor_SMD", "C_0603_1608Metric", 34.0, 52.9, 0, "100 nF 50 V", "C14663",
           {1: "SW", 2: "BST"}),
    "C7": ("Capacitor_SMD", "C_1206_3216Metric", 37.65, 60.6, 0, "22 uF 25 V", "C12891",
           {1: "5V_BUCK", 2: "GND"}),
    "C8": ("Capacitor_SMD", "C_1206_3216Metric", 37.65, 62.9, 0, "22 uF 25 V", "C12891",
           {1: "5V_BUCK", 2: "GND"}),
    # AN2023005 da Ebyte: o chaveamento do DC-DC suja o RF. No lugar do LDO que a nota
    # sugere (5 V -> 5 V nao tem folga), uma ferrite + o banco de capacitores do E22.
    "FB1": ("Inductor_SMD", "L_0805_2012Metric", 32.6, 49.0, 90, "600R 2 A", "C21519",
            {1: "5V_BUCK", 2: "+5V"}),
    "LED1": ("LED_SMD", "LED_0805_2012Metric", 37.0, 66.3, 0, "verde", "C2297",
             {1: "GND", 2: "LED_A"}),          # pad 1 = catodo
    "R2": ("Resistor_SMD", "R_0603_1608Metric", 37.0, 68.3, 0, "1k", "C21190",
           {1: "5V_BUCK", 2: "LED_A"}),

    # --- banco do E22 (ESTUDO_PECAS.md): 220 uF eletrolitico + 10 uF + 100 nF nos pads 9/10.
    # O eletrolitico e COMUM (ESR alto) de proposito: e o ESR que amortece o surto.
    "C1": ("Capacitor_SMD", "CP_Elec_6.3x7.7", 35.6, 41.6, 270, "220 uF 16 V", "C970695",
           {1: "+5V", 2: "GND"}),
    "C2": ("Capacitor_SMD", "C_0805_2012Metric", 32.5, 34.75, 90, "10 uF 25 V", "C15850",
           {1: "+5V", 2: "GND"}),
    "C3": ("Capacitor_SMD", "C_0603_1608Metric", 34.4, 34.75, 90, "100 nF 50 V", "C14663",
           {1: "+5V", 2: "GND"}),
    # TXEN em 0 enquanto a tela liga: sem ele o PA pode acordar transmitindo.
    "R1": ("Resistor_SMD", "R_0603_1608Metric", 2.6, 10.2, 90, "10k", "C25804",
           {1: "TXEN", 2: "GND"}),
    # O terra do P3 (o do GPS) liga no terra da placa por este jumper fechado. Ver README.
    "JP1": ("Jumper", "SolderJumper-2_P1.3mm_Bridged_RoundedPad1.0x1.5mm", 12.0, 29.0, 0,
            "GND P3", "", {1: "GND_P3", 2: "GND"}),
}
DNP = {"U1", "JP1"}       # o JP1 e so cobre

LARGURA = {"VIN": 0.8, "VIN_F": 0.8, "VIN_P": 0.8, "SW": 0.8, "5V_BUCK": 0.8,
           "+5V": 1.0, "GND": 0.6, "3V3": 0.4}
TR_SINAL = 0.25


def carregar(lib, nome):
    if lib == "lib":
        return pcbnew.FootprintLoad(os.path.join(AQUI, "lib"), nome)
    return pcbnew.FootprintLoad(LIBS + lib + ".pretty", nome)


def campo(f, nome, valor):
    fld = pcbnew.PCB_FIELD(f, f.GetNextFieldId() if hasattr(f, "GetNextFieldId") else f.GetFieldCount(), nome)
    fld.SetText(valor)
    fld.SetVisible(False)
    fld.SetLayer(pcbnew.F_Fab)
    f.AddField(fld)


def contorno(board):
    cantos = [(0, 0), (LARG, 0), (LARG, ALT), (0, ALT)]
    for i in range(4):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pt(*cantos[i]))
        s.SetEnd(pt(*cantos[(i + 1) % 4]))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(mm(0.1))
        board.Add(s)


def zona(board, layer, rede, poly, prio=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(board.FindNet(rede))
    z.SetAssignedPriority(prio)
    z.SetLocalClearance(mm(0.3))
    z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetThermalReliefGap(mm(0.3))
    z.SetThermalReliefSpokeWidth(mm(0.5))
    o = z.Outline()
    o.NewOutline()
    for x, y in poly:
        o.Append(mm(x), mm(y))
    board.Add(z)
    return z


def keepout_trilhas(board, poly, layers):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(False)
    z.SetDoNotAllowPads(False)
    if hasattr(z, "SetDoNotAllowCopperPour"):
        z.SetDoNotAllowCopperPour(False)
    z.SetDoNotAllowFootprints(False)
    ls = pcbnew.LSET()
    for l in layers:
        ls.AddLayer(l)
    z.SetLayerSet(ls)
    o = z.Outline()
    o.NewOutline()
    for x, y in poly:
        o.Append(mm(x), mm(y))
    board.Add(z)


def regras(board):
    ds = board.GetDesignSettings()
    # JLCPCB 2 camadas padrao: 0,127/0,127 e via 0,3/0,45. Folga de sobra aqui.
    ds.m_TrackMinWidth = mm(0.2)
    ds.m_MinClearance = mm(0.2)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleToHoleMin = mm(0.5)
    ds.SetBoardThickness(mm(1.6))
    # Vias tampadas pela mascara: debaixo do E22 elas encostam no fundo do modulo.
    for attr in ("m_TentViasFront", "m_TentViasBack"):
        if hasattr(ds, attr):
            setattr(ds, attr, True)
    nc = ds.m_NetSettings.GetDefaultNetclass()
    nc.SetClearance(mm(0.2))
    nc.SetTrackWidth(mm(TR_SINAL))
    nc.SetViaDiameter(mm(0.6))
    nc.SetViaDrill(mm(0.3))
    for nome, w in [("POT", 0.8), ("5V", 1.0)]:
        c = pcbnew.NETCLASS(nome)
        c.SetClearance(mm(0.25))
        c.SetTrackWidth(mm(w))
        c.SetViaDiameter(mm(0.8))
        c.SetViaDrill(mm(0.4))
        ds.m_NetSettings.SetNetclass(nome, c)
    for rede, w in LARGURA.items():
        if rede in ("GND", "3V3"):
            continue
        ds.m_NetSettings.SetNetclassPatternAssignment(rede, "5V" if w >= 1.0 else "POT")


def texto(board, s, x, y, h=0.8, rot=0, baixo=False, esq=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pt(x, y))
    t.SetLayer(pcbnew.B_SilkS if baixo else pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(mm(h), mm(h)))
    t.SetTextThickness(mm(h * 0.15))
    t.SetTextAngleDegrees(rot)
    if baixo:
        t.SetMirrored(True)
    if esq:
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    board.Add(t)


# Face de baixo: a pinagem inteira, para conferir o cabo com o multimetro sem abrir
# documento nenhum. A ordem fisica impressa na tela e o INVERSO da numeracao.
VERSO = [
    "MTS RADIO E22 v4  -  12 V",
    "",
    "J1 -> P1 da tela (cabo PH 12 vias)",
    " 1 TXEN  IO52   2 NRST IO51",
    " 3 DIO1  IO50   4 BUSY IO49",
    " 8/11 GND   5-7 NAO USAR (46-48)",
    "J2 -> P3 da tela (cabo PH 12 vias)",
    " 3 MISO IO31  4 SCK  IO30",
    " 5 MOSI IO29  6 NSS  IO28",
    " 7 RXEN IO5   8 GPS-TX IO4",
    " 9 GPS-RX IO3 10 PPS IO2",
    " 11 GND  12 3V3",
    "J3 GPS: 3V3 GND TX RX PPS",
    "J4: +12V (pino 1)  GND",
    "",
    "CONFIRA: pino 1 da placa =",
    "pino 1 da tela (multimetro)",
    "",
    "U1 E22 soldado a mao",
    "antena no IPEX do modulo",
    "1o boot a 10 dBm",
]


def serigrafia(board):
    texto(board, "P1", 2.6, 4.6, 1.2, 90)
    texto(board, "P3", 2.6, 15.0, 1.2, 90)
    texto(board, "GPS", 5.0, 28.7, 0.9)
    texto(board, "+", 34.0, 28.6, 1.2)
    texto(board, "-", 36.0, 28.6, 1.2)
    texto(board, "12V", 38.3, 28.6, 0.8)
    texto(board, "GND P3", 16.6, 29.0, 0.8)
    texto(board, "E22 - SOLDAR A MAO", E22X, E22Y - 4, 1.0)
    texto(board, "etiqueta para cima", E22X, E22Y - 2.2, 0.8)
    texto(board, "pino 1 ->", E22X + 5.0, E22Y + 15.23, 0.8)
    texto(board, "antena: IPEX do modulo", E22X, E22Y + 4, 0.8)
    texto(board, "MTS v4", E22X, E22Y + 8, 1.2)
    # A JLC grava o numero do pedido onde achar este texto (senao, onde quiser).
    texto(board, "JLCJLCJLCJLC", 20.0, 66.5, 1.0, baixo=True)
    y = 2.0
    for linha in VERSO:
        if linha:
            texto(board, linha, 38.5, y, 0.8, baixo=True, esq=True)
        y += 1.25


def montar():
    board = pcbnew.CreateEmptyBoard()
    board.SetCopperLayerCount(2)
    regras(board)
    redes = sorted({r for p in PECAS.values() for r in p[7].values()})
    for r in redes:
        board.Add(pcbnew.NETINFO_ITEM(board, r))
    contorno(board)

    for ref, (lib, nome, x, y, rot, val, lcsc, mapa) in PECAS.items():
        f = carregar(lib, nome)
        f.SetReference(ref)
        f.SetValue(val)
        f.SetPosition(pt(x, y))
        f.SetOrientationDegrees(rot)
        if lcsc:
            campo(f, "LCSC", lcsc)
        if ref in DNP:
            f.SetDNP(True)
            f.SetExcludedFromBOM(True)
            f.SetExcludedFromPosFiles(True)
        # Referencia na serigrafia so onde ajuda a montar/depurar; o resto vai para
        # a camada Fab (a JLC nao precisa dela, e em 40 x 70 as letras se atropelam).
        r = f.Reference()
        r.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        r.SetTextThickness(mm(0.12))
        if ref in ("J1", "J2"):
            r.SetPosition(pt(37.4, y))
        if ref not in ("J1", "J2", "J4", "LED1"):
            r.SetLayer(pcbnew.F_Fab)
        board.Add(f)
        for pad in f.Pads():
            n = pad.GetNumber()
            chave = int(n) if n.isdigit() else n
            if chave in mapa:
                pad.SetNet(board.FindNet(mapa[chave]))

    # Terra nos dois lados, a placa inteira
    m = 0.3
    quadro = [(m, m), (LARG - m, m), (LARG - m, ALT - m), (m, ALT - m)]
    zona(board, pcbnew.F_Cu, "GND", quadro)
    zona(board, pcbnew.B_Cu, "GND", quadro)
    # Debaixo do E22, no top: so terra (sem trilha). A Ebyte pede cobre aterrado ali.
    ex, ey = E22X, E22Y
    keepout_trilhas(board, [(ex - 10.6, ey - 17.6), (ex + 10.6, ey - 17.6),
                            (ex + 10.6, ey + 20.4), (ex - 10.6, ey + 20.4)], [pcbnew.F_Cu])

    serigrafia(board)
    pcbnew.SaveBoard(PCB, board)
    ok = pcbnew.ExportSpecctraDSN(board, DSN)
    print("montado:", PCB, "dsn:", ok)
    for f in board.GetFootprints():
        for pad in f.Pads():
            if pad.GetNetname():
                p = pad.GetPosition()
                print("  %-4s %-3s %-8s %6.2f %6.2f" % (f.GetReference(), pad.GetNumber(),
                                                    pad.GetNetname(), p.x / 1e6, p.y / 1e6))


def finalizar():
    board = pcbnew.LoadBoard(PCB)
    if os.path.exists(SES):
        print("ses:", pcbnew.ImportSpecctraSES(board, SES))
    terra_nos_pads(board)
    # costura de terra: vias numa grade, so onde nao incomodam nada
    costurar(board)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    pcbnew.SaveBoard(PCB, board)
    print("finalizado:", PCB)


def via(board, x, y, rede="GND"):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pt(x, y))
    v.SetWidth(mm(0.6))
    v.SetDrill(mm(0.3))
    v.SetNet(board.FindNet(rede))
    board.Add(v)


def trilha(board, a, b, rede, w, layer=pcbnew.F_Cu):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(pt(*a))
    t.SetEnd(pt(*b))
    t.SetWidth(mm(w))
    t.SetLayer(layer)
    t.SetNet(board.FindNet(rede))
    board.Add(t)


def terra_nos_pads(board):
    """Uma via de terra colada em cada pad de GND (fora o E22, que ja esta no plano).

    Sem isso o plano de cima vira ilha entre as trilhas e o pad fica "pendurado"
    num fio de cobre so (o DRC acusou o MP do J1 e o anodo do D2)."""
    n = 0
    for f in board.GetFootprints():
        if f.GetReference() == "U1":
            continue
        for pad in f.Pads():
            if pad.GetNetname() != "GND":
                continue
            c = pad.GetPosition()
            cx, cy = c.x / 1e6, c.y / 1e6
            sx, sy = pad.GetBoundingBox().GetWidth() / 2e6, pad.GetBoundingBox().GetHeight() / 2e6
            for dx, dy in [(0, sy + 0.55), (0, -sy - 0.55), (sx + 0.55, 0), (-sx - 0.55, 0),
                           (sx + 0.5, sy + 0.5), (-sx - 0.5, sy + 0.5),
                           (sx + 0.5, -sy - 0.5), (-sx - 0.5, -sy - 0.5)]:
                x, y = cx + dx, cy + dy
                if livre(board, x, y, 0.3, ignorar_pad=pad, ignorar_patio=True):
                    via(board, x, y)
                    trilha(board, (cx, cy), (x, y), "GND", 0.5)
                    n += 1
                    break
    print("vias de terra nos pads:", n)


def costurar(board):
    passo = 3.0
    n = 0
    y = 1.5
    while y < ALT - 1:
        x = 1.5
        while x < LARG - 1:
            if livre(board, x, y, 0.45):
                via(board, x, y)
                n += 1
            x += passo
        y += passo
    print("vias de costura:", n)


def _dist_seg(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    l2 = vx * vx + vy * vy
    t = 0 if l2 == 0 else max(0, min(1, ((px - ax) * vx + (py - ay) * vy) / l2))
    qx, qy = ax + t * vx, ay + t * vy
    return ((px - qx) ** 2 + (py - qy) ** 2) ** 0.5


def livre(board, x, y, folga, ignorar_pad=None, ignorar_patio=False):
    """Cabe uma via de 0,6 mm em (x, y) com `folga` de qualquer cobre de outra rede?

    Os pads entram pela caixa (conservador). O patio das pecas tambem barra a via
    de costura (nao quero furo debaixo de conector), menos o do E22: debaixo do
    modulo a via aterrada e bem-vinda, tampada pela mascara."""
    R = 0.3 + folga
    if x < 0.3 + R or y < 0.3 + R or x > LARG - 0.3 - R or y > ALT - 0.3 - R:
        return False
    for f in board.GetFootprints():
        if not ignorar_patio and f.GetReference() != "U1":
            cy = f.GetCourtyard(pcbnew.F_CrtYd)
            if cy.OutlineCount():
                bb = cy.BBox()
                if (bb.GetLeft() / 1e6 - 0.3 < x < bb.GetRight() / 1e6 + 0.3 and
                        bb.GetTop() / 1e6 - 0.3 < y < bb.GetBottom() / 1e6 + 0.3):
                    return False
        for pad in f.Pads():
            # (os objetos do SWIG sao novos a cada chamada: `is` nao serve, compara posicao)
            if ignorar_pad is not None and pad.GetPosition() == ignorar_pad.GetPosition():
                continue
            b = pad.GetBoundingBox()
            if (b.GetLeft() / 1e6 - R < x < b.GetRight() / 1e6 + R and
                    b.GetTop() / 1e6 - R < y < b.GetBottom() / 1e6 + R):
                return False
    for t in board.GetTracks():
        if t.GetNetname() == "GND" and t.GetClass() != "PCB_VIA":
            continue
        if t.GetClass() == "PCB_VIA":
            p = t.GetPosition()
            if ((p.x / 1e6 - x) ** 2 + (p.y / 1e6 - y) ** 2) ** 0.5 < t.GetWidth(pcbnew.F_Cu) / 2e6 + R:
                return False
        else:
            a, b = t.GetStart(), t.GetEnd()
            if _dist_seg(x, y, a.x / 1e6, a.y / 1e6, b.x / 1e6, b.y / 1e6) < t.GetWidth() / 2e6 + R:
                return False
    return True


if __name__ == "__main__":
    {"montar": montar, "finalizar": finalizar}[sys.argv[1]]()
