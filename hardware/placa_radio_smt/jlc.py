"""Converte o que o kicad-cli exporta para os formatos que a JLCPCB pede.

    BOM: Comment, Designator, Footprint, JLCPCB Part #
    CPL: Designator, Mid X, Mid Y, Layer, Rotation
O E22 (U1) e o jumper de cobre (JP1) estao marcados DNP e nao entram em nenhum dos dois.
"""
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gera_pcb import PECAS, DNP  # noqa: E402

S = os.path.join(os.path.dirname(os.path.abspath(__file__)), "saida")
F = os.path.join(S, "fabrica")

linhas = list(csv.DictReader(open(os.path.join(S, "pos_kicad.csv"), encoding="utf-8")))
with open(os.path.join(F, "CPL_placa_radio_v4.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.writer(fp)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for r in linhas:
        if r["Ref"] in DNP:
            continue
        w.writerow([r["Ref"], r["PosX"] + "mm", r["PosY"] + "mm",
                    "Top" if r["Side"] == "top" else "Bottom", "%g" % (float(r["Rot"]) % 360)])

grupos = {}
for ref, (lib, fpn, x, y, rot, val, lcsc, mapa) in PECAS.items():
    if ref in DNP:
        continue
    grupos.setdefault((val, fpn, lcsc), []).append(ref)
with open(os.path.join(F, "BOM_placa_radio_v4.csv"), "w", newline="", encoding="utf-8") as fp:
    w = csv.writer(fp)
    w.writerow(["Comment", "Designator", "Footprint", "JLCPCB Part #"])
    for (val, fpn, lcsc), refs in sorted(grupos.items(), key=lambda g: g[1][0]):
        w.writerow([val, ",".join(sorted(refs)), fpn, lcsc])
print("CPL:", len([r for r in linhas if r["Ref"] not in DNP]), "pecas; BOM:", len(grupos), "linhas")
