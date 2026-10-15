#!/usr/bin/env python3
"""
MTS - conversor do modelo de elevacao para o pacote de relevo do aparelho.

POR QUE GUARDAR O MODELO CRU, E NAO CURVAS DE NIVEL PRONTAS
-----------------------------------------------------------
Curva de nivel e a cara de um GPS topografico, mas guardar a curva em vetor sai
PIOR que guardar a elevacao: a curva de 20 em 20 m no Brasil daria mais pontos que
toda a malha viaria do pais, e ainda assim so serviria para desenhar linha. Com o
modelo cru o aparelho faz tres coisas com o mesmo dado:

    curva de nivel .... interpola o contorno na hora, no intervalo que couber no zoom
    sombreado ......... da o relevo "3D" que faz o vale aparecer sem ler numero
    altitude ........... quantos metros voce esta, e quanto falta subir

FONTE: viewfinderpanoramas dem3 - SRTM de 3 arcsec (90 m) com os buracos ja
preenchidos. Ver tools/mapa/baixa_relevo.py. Cada arquivo .hgt e int16 BIG-ENDIAN,
1201x1201, cobrindo 1 grau, linha 0 no NORTE.

O CORTE EM BLOCOS
-----------------
1201 = 10 x 120 + 1. Isso nao e coincidencia util a toa: o grau divide EXATAMENTE
em 10x10 blocos de 121x121 amostras, e blocos vizinhos compartilham a linha da
borda. Essa borda compartilhada e o que permite calcular o sombreado (que precisa
do vizinho de cada amostra) sem ler o bloco do lado.

    bloco = 0,1 grau = ~11,1 km = 121x121x2 = 29.282 B -> 58 setores (29.696 B)

Leitura MEDIDA nesta placa: 12,4 ms de busca + 16,5 ms de dado = ~29 ms por bloco.
A tela mais aberta (12 m/px, 15,4 km) pega 2 blocos. Cabe, e o cache em PSRAM faz
isso acontecer so ao cruzar borda.

Indexacao: como o bloco e 0,1 grau exato, o endereco e aritmetica pura -
    linha  = floor((lat +  90) * 10)
    coluna = floor((lon + 180) * 10)
sem tabela de conversao e sem Mercator. O relevo nao precisa da projecao do mapa
porque e amostrado em graus, nao em pixels.

Se mudar o formato, mude tambem em firmware/dem_pack.h.
"""
import argparse
import os
import re
import struct
import sys
import zipfile
import zlib

import numpy as np

SETOR = 512
MAGIC = b"MTSDEM01"
VERSAO = 1

N = 121                      # amostras por lado do bloco
POR_GRAU = 10                # blocos por grau, em cada eixo
BLOCO_BYTES = N * N * 2
BLOCO_SETORES = (BLOCO_BYTES + SETOR - 1) // SETOR
BLOCO_PAD = BLOCO_SETORES * SETOR

VOID = -32768                # marca de buraco do SRTM
IDX = 8                      # setor u32 + crc32 u32

NOME = re.compile(r'([NS])(\d{2})([EW])(\d{3})\.hgt$', re.IGNORECASE)


def coords_do_nome(caminho):
    """'F23/S21W047.hgt' -> (-21, -47), o canto SUDOESTE do grau."""
    m = NOME.search(caminho)
    if not m:
        return None
    ns, la, ew, lo = m.groups()
    lat = int(la) * (1 if ns.upper() == 'N' else -1)
    lon = int(lo) * (1 if ew.upper() == 'E' else -1)
    return lat, lon


def lista_tiles(pasta):
    """{(lat, lon): (zip, membro)} de todos os .hgt nos zips da pasta."""
    r = {}
    for nome in sorted(os.listdir(pasta)):
        if not nome.lower().endswith('.zip'):
            continue
        p = os.path.join(pasta, nome)
        try:
            z = zipfile.ZipFile(p)
        except zipfile.BadZipFile:
            print(f"  aviso: {nome} nao abre, pulando")
            continue
        for m in z.namelist():
            c = coords_do_nome(m)
            if c and z.getinfo(m).file_size == 1201 * 1201 * 2:
                # blocos se sobrepoem entre zips vizinhos; o primeiro ganha
                r.setdefault(c, (p, m))
        z.close()
    return r


def tapa_buracos(a):
    """Substitui VOID pelo vizinho valido mais proximo na linha, com numpy.

    Buraco vira precipicio no sombreado: uma amostra de -32768 no meio da mata
    desenha um risco preto de varios pixels. O dem3 ja vem quase todo preenchido,
    mas 'quase' nao serve - por isso o contador de quantos foram tapados.

    NUMPY NAO E LUXO AQUI. A versao em Python puro varria 1,44 milhao de amostras
    por tile vezes 1.632 tiles: 2,4 bilhoes de iteracoes, horas de processamento
    para uma conta que o numpy faz em milissegundos."""
    vale = a != VOID
    if vale.all():
        return 0
    n = int((~vale).sum())
    lin, col = a.shape
    ci = np.arange(col)
    # preenche para a frente: cada buraco pega o ultimo valor valido da esquerda
    ida = np.where(vale, ci, 0)
    np.maximum.accumulate(ida, axis=1, out=ida)
    frente = np.take_along_axis(a, ida, axis=1)
    # e para tras, para os buracos que comecam na borda esquerda
    volta_i = np.where(vale, ci, col - 1)
    volta_i = np.minimum.accumulate(volta_i[:, ::-1], axis=1)[:, ::-1]
    tras = np.take_along_axis(a, volta_i, axis=1)
    saida = np.where(vale, a, np.where(frente != VOID, frente, tras))
    # linha inteira sem dado (acontece em tile 100% oceano): vira nivel do mar
    saida = np.where(saida == VOID, 0, saida)
    a[:, :] = saida
    return n


def converte(pasta, saida, verbose):
    print(f"varrendo {pasta} ...", flush=True)
    tiles = lista_tiles(pasta)
    if not tiles:
        sys.exit("nenhum .hgt encontrado")
    lats = [c[0] for c in tiles]
    lons = [c[1] for c in tiles]
    print(f"  {len(tiles)} tiles de 1 grau, "
          f"lat {min(lats)}..{max(lats)+1}, lon {min(lons)}..{max(lons)+1}")

    # grade global de blocos de 0,1 grau
    lin0 = (min(lats) + 90) * POR_GRAU
    col0 = (min(lons) + 180) * POR_GRAU
    h = (max(lats) + 1 - min(lats)) * POR_GRAU
    w = (max(lons) + 1 - min(lons)) * POR_GRAU
    nslots = w * h
    setores_indice = (nslots * IDX + SETOR - 1) // SETOR
    indice_setor = 1
    dados_setor = indice_setor + setores_indice
    print(f"  grade {w}x{h} = {nslots:,} blocos possiveis; "
          f"indice {setores_indice*SETOR/1e6:.1f} MB")

    indice = bytearray(setores_indice * SETOR)
    prox = dados_setor
    gravados = oceano = buracos = 0

    with open(saida, 'wb') as f:
        f.seek(dados_setor * SETOR)
        for i, (lat, lon) in enumerate(sorted(tiles), 1):
            zp, membro = tiles[(lat, lon)]
            with zipfile.ZipFile(zp) as z:
                cru = z.read(membro)
            # o .hgt e big-endian; a copia com astype ja deixa em little-endian,
            # que e a ordem nativa do ESP32 - o aparelho nao gasta um ciclo
            # trocando byte no caminho do quadro.
            a = np.frombuffer(cru, dtype='>i2').reshape(1201, 1201).astype('<i2')
            buracos += tapa_buracos(a)

            for br in range(POR_GRAU):
                for bc in range(POR_GRAU):
                    # linha 0 do .hgt e o NORTE; a linha da grade cresce para o
                    # NORTE. Inverter aqui e o erro classico deste formato: o
                    # mapa fica com o relevo espelhado e ninguem nota de imediato.
                    r0 = (POR_GRAU - 1 - br) * (N - 1)
                    c0 = bc * (N - 1)
                    sub = a[r0:r0 + N, c0:c0 + N]
                    if not sub.any():
                        oceano += 1          # bloco todo no nivel do mar: e mar
                        continue
                    bloco = bytearray(BLOCO_PAD)
                    bloco[0:BLOCO_BYTES] = sub.tobytes()
                    slot = ((lat * POR_GRAU + br) + 900 - lin0) * w + \
                           ((lon * POR_GRAU + bc) + 1800 - col0)
                    crc = zlib.crc32(bytes(bloco[:BLOCO_BYTES])) & 0xFFFFFFFF
                    struct.pack_into('<II', indice, slot * IDX, prox, crc)
                    f.write(bloco)
                    prox += BLOCO_SETORES
                    gravados += 1
            if verbose or i % 100 == 0:
                print(f"  [{i:>4}/{len(tiles)}] {lat:+03d},{lon:+04d}  "
                      f"{gravados:,} blocos ({gravados*BLOCO_PAD/1e9:.2f} GB), "
                      f"{oceano:,} de mar pulados", flush=True)

        # Layout do cabecalho. Os offsets sao EXPLICITOS e nao se tocam: a
        # primeira versao gravava porGrau no 12 e logo depois tres u32 a partir do
        # 12, apagando o campo. O info lia "1 bloco por grau" e uma origem
        # absurda, e nada mais acusava.
        #   8 versao u16 | 10 N u16 | 12 porGrau u8 | 13..15 reservado
        #  16 indiceSetor u32 | 20 dadosSetor u32 | 24 nslots u32
        #  28 lin0 i32 | 32 col0 i32 | 36 w u16 | 38 h u16 | 40 blocoSetores u32
        cab = bytearray(SETOR)
        cab[0:8] = MAGIC
        struct.pack_into('<HHB', cab, 8, VERSAO, N, POR_GRAU)
        struct.pack_into('<III', cab, 16, indice_setor, dados_setor, nslots)
        struct.pack_into('<iiHH', cab, 28, lin0, col0, w, h)
        struct.pack_into('<I', cab, 40, BLOCO_SETORES)
        f.seek(0)
        f.write(cab)
        f.write(indice)

    tam = os.path.getsize(saida)
    print(f"\nOK: {tam/1e9:.2f} GB  ({gravados:,} blocos, {oceano:,} de mar pulados, "
          f"{buracos:,} amostras sem dado tapadas)")
    print(f"leitura de 1 bloco a 1,8 MB/s: {12.4 + BLOCO_PAD/1.8e6*1000:.0f} ms")


def info(caminho):
    with open(caminho, 'rb') as f:
        h = f.read(SETOR)
        if h[0:8] != MAGIC:
            sys.exit("nao e um pacote MTSDEM")
        versao, n, porgrau = struct.unpack_from('<HHB', h, 8)
        isec, dsec, nslots = struct.unpack_from('<III', h, 16)
        lin0, col0, w, hh = struct.unpack_from('<iiHH', h, 28)
        bsec, = struct.unpack_from('<I', h, 40)
        print(f"versao {versao}: bloco {n}x{n}, {porgrau} por grau, "
              f"{bsec} setores ({bsec*SETOR/1024:.1f} KB) por bloco")
        print(f"grade {w}x{hh} = {nslots:,} blocos, "
              f"origem lat {lin0/porgrau-90:+.1f} lon {col0/porgrau-180:+.1f}")
        f.seek(isec * SETOR)
        idx = f.read((nslots * IDX + SETOR - 1) // SETOR * SETOR)
        cheios = sum(1 for s in range(nslots)
                     if struct.unpack_from('<I', idx, s * IDX)[0])
        print(f"{cheios:,} blocos com dado ({100.0*cheios/nslots:.1f}% da grade), "
              f"{cheios*bsec*SETOR/1e9:.2f} GB de dado")


def amostra(caminho, lat, lon):
    """Le a altitude de um ponto - serve para conferir contra valor conhecido."""
    with open(caminho, 'rb') as f:
        h = f.read(SETOR)
        _, n, porgrau = struct.unpack_from('<HHB', h, 8)
        isec, _, nslots = struct.unpack_from('<III', h, 16)
        lin0, col0, w, hh = struct.unpack_from('<iiHH', h, 28)
        lin = int((lat + 90) * porgrau) - lin0
        col = int((lon + 180) * porgrau) - col0
        if not (0 <= lin < hh and 0 <= col < w):
            return None
        slot = lin * w + col
        f.seek(isec * SETOR + slot * IDX)
        sec, _ = struct.unpack('<II', f.read(IDX))
        if sec == 0:
            return None
        # posicao dentro do bloco: o bloco cobre 1/porgrau de grau e tem n-1 passos
        fr = ((lat + 90) * porgrau) % 1.0
        fc = ((lon + 180) * porgrau) % 1.0
        r = int(round((1.0 - fr) * (n - 1)))     # linha 0 = norte do bloco
        c = int(round(fc * (n - 1)))
        f.seek(sec * SETOR + (r * n + c) * 2)
        return struct.unpack('<h', f.read(2))[0]


def main():
    ap = argparse.ArgumentParser(description="DEM -> pacote de relevo do MTS")
    s = ap.add_subparsers(dest='cmd', required=True)
    c = s.add_parser('pack')
    c.add_argument('--dem', required=True, help="pasta com os zips do dem3")
    c.add_argument('--out', required=True)
    c.add_argument('-v', '--verbose', action='store_true')
    c = s.add_parser('info')
    c.add_argument('--pack', required=True)
    c = s.add_parser('alt', help="altitude de um ponto (conferencia)")
    c.add_argument('--pack', required=True)
    c.add_argument('--lat', type=float, required=True)
    c.add_argument('--lon', type=float, required=True)

    a = ap.parse_args()
    if a.cmd == 'pack':
        converte(a.dem, a.out, a.verbose)
    elif a.cmd == 'info':
        info(a.pack)
    else:
        v = amostra(a.pack, a.lat, a.lon)
        print(f"{a.lat},{a.lon} -> {v} m" if v is not None else "sem dado")


if __name__ == '__main__':
    main()
