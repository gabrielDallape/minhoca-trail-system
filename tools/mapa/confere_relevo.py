#!/usr/bin/env python3
"""
Confere o pacote de relevo CONTRA o .hgt de origem, amostra por amostra.

POR QUE ESTE TESTE EXISTE
-------------------------
Comparar a altitude do pacote com "a altitude que eu sei que o lugar tem" mistura
dois erros diferentes:

  1. o erro do MODELO - o SRTM de 90 m corta o topo de pico afiado, e isso e
     inevitavel e nao e defeito do nosso codigo;
  2. o erro do NOSSO CODIGO - deslocamento de linha, bloco trocado, norte
     invertido, byte na ordem errada.

O segundo e o unico que da para consertar, e ele se disfarca de primeiro: um
deslocamento de meio bloco erra muito em ladeira e quase nada em terreno plano -
exatamente o padrao de "so os picos erram". Lendo o .hgt cru e comparando com o
pacote, o erro do modelo sai da conta e sobra so o nosso.

Um unico byte de diferenca aqui e falha. Nao ha tolerancia a aplicar: o pacote e
uma reorganizacao do mesmo dado, nao uma reamostragem.
"""
import argparse
import os
import random
import struct
import sys
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from relevo import (MAGIC, SETOR, IDX, N, POR_GRAU, VOID,  # noqa: E402
                    lista_tiles, tapa_buracos)


def abre(caminho):
    f = open(caminho, 'rb')
    h = f.read(SETOR)
    if h[0:8] != MAGIC:
        sys.exit("nao e um pacote MTSDEM")
    _, n, porgrau = struct.unpack_from('<HHB', h, 8)
    isec, dsec, nslots = struct.unpack_from('<III', h, 16)
    lin0, col0, w, hh = struct.unpack_from('<iiHH', h, 28)
    return f, dict(n=n, porgrau=porgrau, isec=isec, nslots=nslots,
                   lin0=lin0, col0=col0, w=w, h=hh)


def le_bloco(f, c, lin, col):
    """lin/col ABSOLUTOS na grade global de 0,1 grau."""
    li, ci = lin - c['lin0'], col - c['col0']
    if not (0 <= li < c['h'] and 0 <= ci < c['w']):
        return None
    slot = li * c['w'] + ci
    f.seek(c['isec'] * SETOR + slot * IDX)
    sec, crc = struct.unpack('<II', f.read(IDX))
    if sec == 0:
        return None
    f.seek(sec * SETOR)
    return np.frombuffer(f.read(N * N * 2), dtype='<i2').reshape(N, N), crc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', required=True)
    ap.add_argument('--dem', required=True, help="a mesma pasta de zips usada no pack")
    ap.add_argument('--tiles', type=int, default=6, help="quantos tiles sortear")
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()

    random.seed(a.seed)
    f, c = abre(a.pack)
    print(f"pacote: bloco {c['n']}x{c['n']}, {c['porgrau']}/grau, "
          f"grade {c['w']}x{c['h']}")

    tiles = lista_tiles(a.dem)
    escolhidos = random.sample(sorted(tiles), min(a.tiles, len(tiles)))

    total = iguais = ausentes = 0
    piores = []
    for lat, lon in escolhidos:
        zp, membro = tiles[(lat, lon)]
        with zipfile.ZipFile(zp) as z:
            cru = z.read(membro)
        # reproduz EXATAMENTE o que o pack faz com o tile
        orig = np.frombuffer(cru, dtype='>i2').reshape(1201, 1201).astype('<i2')
        tapa_buracos(orig)

        for br in range(POR_GRAU):
            for bc in range(POR_GRAU):
                r0 = (POR_GRAU - 1 - br) * (N - 1)
                c0 = bc * (N - 1)
                esperado = orig[r0:r0 + N, c0:c0 + N]
                lin = (lat * POR_GRAU + br) + 900
                col = (lon * POR_GRAU + bc) + 1800
                got = le_bloco(f, c, lin, col)
                total += 1
                if got is None:
                    if esperado.any():
                        piores.append((lat, lon, br, bc, "AUSENTE mas tem terreno"))
                    else:
                        ausentes += 1       # mar, corretamente pulado
                    continue
                bloco, _ = got
                if np.array_equal(bloco, esperado):
                    iguais += 1
                else:
                    d = np.abs(bloco.astype(np.int32) - esperado.astype(np.int32))
                    piores.append((lat, lon, br, bc,
                                   f"difere: max {d.max()} m, {int((d>0).sum())} amostras"))
        print(f"  {lat:+03d},{lon:+04d} conferido", flush=True)

    print(f"\n{total} blocos: {iguais} identicos, {ausentes} de mar (corretos), "
          f"{len(piores)} com problema")
    for p in piores[:20]:
        print("   ", p)
    if piores:
        sys.exit(1)

    # amostra pontual: o caminho que o firmware vai usar de verdade
    print("\nleitura pontual (o caminho do firmware):")
    for _ in range(5):
        lat, lon = random.choice(escolhidos)
        la = lat + random.random()
        lo = lon + random.random()
        lin = int((la + 90) * POR_GRAU)
        col = int((lo + 180) * POR_GRAU)
        got = le_bloco(f, c, lin, col)
        if got is None:
            continue
        bloco, _ = got
        fr = ((la + 90) * POR_GRAU) % 1.0
        fc = ((lo + 180) * POR_GRAU) % 1.0
        r = int(round((1.0 - fr) * (N - 1)))
        cc = int(round(fc * (N - 1)))
        # o mesmo ponto direto do .hgt
        zp, membro = tiles[(lat, lon)]
        with zipfile.ZipFile(zp) as z:
            orig = np.frombuffer(z.read(membro), dtype='>i2').reshape(1201, 1201)
        rr = int(round((lat + 1 - la) * 1200))
        cc2 = int(round((lo - lon) * 1200))
        direto = int(orig[rr, cc2])
        do_pack = int(bloco[r, cc])
        sinal = "OK " if direto == do_pack or abs(direto - do_pack) <= 1 else "ERRO"
        print(f"  {sinal} {la:+.4f},{lo:+.4f}  pacote {do_pack:>5} m   "
              f"hgt cru {direto:>5} m")
    f.close()
    print("\ntudo conferido.")


if __name__ == '__main__':
    main()
