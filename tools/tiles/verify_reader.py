#!/usr/bin/env python3
r"""
Teste de conformidade entre o GRAVADOR (tiles.py, Python) e o LEITOR
(firmware/tile_pack.h, C++).

Existe porque as duas pontas implementam o MESMO formato em linguagens
diferentes: se divergirem num offset ou no CRC, todo tile valido seria rejeitado
no aparelho e o sintoma na tela (mapa cinza) nao diria onde esta o erro.

Aqui os algoritmos do lado C sao reimplementados EXATAMENTE como estao no
tile_pack.h (mesma tabela de nibble, mesmos offsets manuais, mesma formula de
Mercator) e conferidos contra o Python. Roda no PC, sem placa.

  python verify_reader.py --img trilha.img
  python verify_reader.py                 # so os testes que nao precisam de .img
"""

import argparse
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tiles  # noqa: E402

PASS = FAIL = 0


def check(ok, what, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  PASS  {what}" + (f"  {detail}" if detail else ""))
    else:
        FAIL += 1
        print(f"  FAIL  {what}  {detail}   <-------")


# --------------------------------------------------------------------- CRC32
# Copia fiel de TP_CRC_NIB / tilePackCrc32() do firmware/tile_pack.h
TP_CRC_NIB = [
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
    0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
    0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
]


def c_crc32(data, crc=0):
    """tilePackCrc32() do lado C, bit a bit."""
    crc = (~crc) & 0xFFFFFFFF
    for b in data:
        crc ^= b
        crc = (crc >> 4) ^ TP_CRC_NIB[crc & 0x0F]
        crc = (crc >> 4) ^ TP_CRC_NIB[crc & 0x0F]
        crc &= 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF


def test_crc():
    print("\n[1] CRC32 do leitor C == zlib.crc32 do gravador Python")
    # vetor canonico: "123456789" -> 0xCBF43926
    check(c_crc32(b"123456789") == 0xCBF43926, "vetor canonico CBF43926",
          f"got={c_crc32(b'123456789'):08x}")
    casos = [
        b"", b"\x00", b"\xff", b"a", b"TRILHAMP",
        bytes(range(256)),
        b"\x00" * 512,
        b"\xff" * 512,
        bytes([(i * 37 + 11) & 0xFF for i in range(131072)]),   # tamanho de 1 tile
    ]
    todos = True
    for d in casos:
        if c_crc32(d) != (zlib.crc32(d) & 0xFFFFFFFF):
            todos = False
            print(f"    divergiu em len={len(d)}: C={c_crc32(d):08x} zlib={zlib.crc32(d)&0xFFFFFFFF:08x}")
    check(todos, f"{len(casos)} payloads batem com zlib (inclusive 128KB)")

    # CRC incremental (o C aceita crc de entrada p/ encadear)
    a, b = b"parte1", b"parte2"
    check(c_crc32(b, c_crc32(a)) == (zlib.crc32(b, zlib.crc32(a)) & 0xFFFFFFFF),
          "CRC incremental encadeia igual ao zlib")


# ------------------------------------------------------------------ Mercator
def c_deg_to_tile(lat, lon, z):
    """tilePackDegToTile() do lado C: usa log((1+s)/(1-s)), nao asinh(tan)."""
    n = float(1 << z)
    lat = max(min(lat, 85.05112878), -85.05112878)
    fx = (lon + 180.0) / 360.0 * n
    s = math.sin(lat * 0.017453292519943295)
    fy = (1.0 - math.log((1.0 + s) / (1.0 - s)) / (2.0 * math.pi)) / 2.0 * n
    fx = max(0.0, min(fx, n - 1))
    fy = max(0.0, min(fy, n - 1))
    return int(fx), int(fy)


def test_mercator():
    print("\n[2] Mercator: formula do C == formula do Python")
    # As duas formas sao algebricamente iguais:
    #   asinh(tan f) == artanh(sin f) == 0.5*ln((1+sin f)/(1-sin f))
    # mas isso precisa valer NUMERICAMENTE, senao o tile escolhido difere na borda.
    pontos = [
        (-23.5505, -46.6333, "Sao Paulo"),
        (-23.0,    -46.0,    "sudeste"),
        (-30.0328, -51.2302, "Porto Alegre"),
        (-3.1190,  -60.0217, "Manaus"),
        (0.0,       0.0,     "equador/Greenwich"),
        (85.0,      179.9,   "extremo norte/leste"),
        (-85.0,    -179.9,   "extremo sul/oeste"),
        (-22.9068, -43.1729, "Rio"),
    ]
    todos = True
    for lat, lon, nome in pontos:
        for z in (10, 13, 15, 16, 18):
            py = tiles.deg2tile(lat, lon, z)
            c = c_deg_to_tile(lat, lon, z)
            if py != c:
                todos = False
                print(f"    divergiu {nome} z{z}: py={py} c={c}")
    check(todos, f"{len(pontos)} pontos x 5 zooms concordam")

    # ida e volta: o canto do tile tem de cair no proprio tile
    ok = True
    for z in (13, 15, 16):
        for (lat, lon, _n) in pontos[:5]:
            x, y = tiles.deg2tile(lat, lon, z)
            clat, clon = tiles.tile2deg(x, y, z)
            # o canto NO cai exatamente na borda; empurra 1e-9 p/ dentro
            x2, y2 = tiles.deg2tile(clat - 1e-9, clon + 1e-9, z)
            if (x2, y2) != (x, y):
                ok = False
                print(f"    ida/volta falhou z{z} em {lat},{lon}: {(x,y)} -> {(x2,y2)}")
    check(ok, "ida e volta tile->graus->tile e estavel")

    # latitude do Brasil e NEGATIVA: conferir que o y cresce para o sul
    x1, y1 = tiles.deg2tile(-23.50, -46.65, 15)
    x2, y2 = tiles.deg2tile(-23.60, -46.65, 15)
    check(y2 > y1, "y cresce para o sul (hemisferio sul)", f"y(-23.50)={y1} y(-23.60)={y2}")


# ------------------------------------------------------- parsing do cabecalho
HDR_SIZE_C = 36
LVL_SIZE_C = 20


def c_parse_header(buf):
    """tilePackOpen() do lado C: offsets manuais, little-endian."""
    if buf[0:8] != b"TRILHAMP":
        return None
    g16 = lambda o: buf[o] | (buf[o + 1] << 8)
    g32 = lambda o: buf[o] | (buf[o + 1] << 8) | (buf[o + 2] << 16) | (buf[o + 3] << 24)
    h = dict(version=g16(8), tile_px=g16(10), pixfmt=buf[12], n_levels=buf[13],
             index_sector=g32(16), data_sector=g32(20),
             tile_sectors=g32(24), n_tiles=g32(28), levels=[])
    for i in range(h["n_levels"]):
        o = HDR_SIZE_C + i * LVL_SIZE_C
        h["levels"].append(dict(zoom=buf[o], xmin=g32(o + 4), ymin=g32(o + 8),
                                w=g16(o + 12), h=g16(o + 14), off=g32(o + 16)))
    return h


def test_struct_sizes():
    print("\n[3] tamanhos de struct batem entre as duas pontas")
    check(struct.calcsize(tiles.HDR_FMT) == HDR_SIZE_C, "cabecalho",
          f"py={struct.calcsize(tiles.HDR_FMT)} c={HDR_SIZE_C}")
    check(struct.calcsize(tiles.LVL_FMT) == LVL_SIZE_C, "registro de nivel",
          f"py={struct.calcsize(tiles.LVL_FMT)} c={LVL_SIZE_C}")
    check(struct.calcsize(tiles.IDX_FMT) == 8, "registro de indice")
    check(tiles.TILE_SECTORS == tiles.TILE_BYTES // 512 == 256, "256 setores por tile")
    check(HDR_SIZE_C + 16 * LVL_SIZE_C <= 512, "cabecalho + 16 niveis cabem no setor 0",
          f"{HDR_SIZE_C + 16*LVL_SIZE_C} bytes")


def test_image(path):
    print(f"\n[4] leitura de {os.path.basename(path)} pelo parser do C")
    with open(path, "rb") as f:
        sector0 = f.read(512)
        py = tiles.read_header(f)

    c = c_parse_header(sector0)
    check(c is not None, "magic TRILHAMP reconhecido")
    if c is None:
        return

    for k in ("version", "tile_px", "pixfmt", "index_sector", "data_sector", "tile_sectors", "n_tiles"):
        check(c[k] == py[k], f"campo {k}", f"c={c[k]} py={py[k]}")
    check(len(c["levels"]) == len(py["levels"]), "quantidade de niveis")
    for i, (lc, lp) in enumerate(zip(c["levels"], py["levels"])):
        same = all(lc[k] == lp[k] for k in ("zoom", "xmin", "ymin", "w", "h", "off"))
        check(same, f"nivel {i} (z{lp['zoom']})", f"c={lc} py={lp}")

    # tilePackSlot() do lado C, e o CRC de cada tile conferido com a tabela nibble
    print("\n[5] slot + CRC de cada tile, do jeito que o firmware faz")
    with open(path, "rb") as f:
        bad_slot = bad_crc = absent = ok_cnt = 0
        for m in py["levels"]:
            for dy in range(m["h"]):
                for dx in range(m["w"]):
                    x, y = m["xmin"] + dx, m["ymin"] + dy
                    # slot pelo calculo do C
                    slot_c = m["off"] + dy * m["w"] + dx
                    slot_py = tiles.find_slot(py, m["zoom"], x, y)
                    if slot_c != slot_py:
                        bad_slot += 1
                        continue
                    sec, crc = tiles.read_index(f, py, slot_c)
                    if sec == 0:
                        absent += 1
                        continue
                    f.seek(sec * 512)
                    raw = f.read(tiles.TILE_BYTES)
                    if c_crc32(raw) != crc:
                        bad_crc += 1
                    else:
                        ok_cnt += 1
        check(bad_slot == 0, "calculo de slot identico nas duas pontas",
              f"divergencias={bad_slot}")
        check(bad_crc == 0, f"CRC do leitor C confere em {ok_cnt} tiles",
              f"falhas={bad_crc} ausentes={absent}")

    # tile fora da grade tem de dar -1 (ABSENT), nao lixo
    print("\n[6] limites da grade")
    m = py["levels"][0]
    check(tiles.find_slot(py, m["zoom"], m["xmin"] - 1, m["ymin"]) is None, "x abaixo do minimo -> ausente")
    check(tiles.find_slot(py, m["zoom"], m["xmin"] + m["w"], m["ymin"]) is None, "x acima do maximo -> ausente")
    check(tiles.find_slot(py, m["zoom"], m["xmin"], m["ymin"] + m["h"]) is None, "y acima do maximo -> ausente")
    check(tiles.find_slot(py, 99, m["xmin"], m["ymin"]) is None, "zoom inexistente -> ausente")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--img", help="opcional: um .img gerado por tiles.py pack")
    args = ap.parse_args()

    print("=" * 60)
    print(" CONFORMIDADE gravador (Python) <-> leitor (C, tile_pack.h)")
    print("=" * 60)

    test_crc()
    test_mercator()
    test_struct_sizes()
    if args.img:
        test_image(args.img)
    else:
        print("\n(sem --img: pulei os testes que precisam de uma imagem)")

    print("\n" + "=" * 60)
    print(f" RESULTADO: {PASS} PASS, {FAIL} FAIL")
    print(" TUDO OK" if FAIL == 0 else " HA FALHAS")
    print("=" * 60)
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
