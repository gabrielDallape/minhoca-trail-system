#!/usr/bin/env python3
"""
Grava o cartao do MTS: tabela de particao + mapa vetorial + relevo.

COMO FICA O CARTAO
------------------
    p1  FAT32   1 GB    trajetos gravados - e a unica parte que o PC enxerga
    p2  0xDA    vetor   mapa (vias, agua, mata, POI)
    p3  0xDA    relevo  elevacao de 90 m
    (o que sobrar fica livre, para mapa novo sem reparticionar)

O mapa NAO e arquivo. Particao bruta, tipo 0xDA ("dado sem filesystem"), pelo
motivo que esta no tools/tiles/README.md: o aparelho perde energia sem aviso
quando se desliga a chave do carro, e sem FAT nao existe tabela de alocacao nem
diretorio para corromper. O pior caso passa a ser um setor errado, que o CRC32 do
indice torna detectavel - o firmware pinta o pedaco de cinza em vez de rabiscar a
tela.

A FAT32 fica separada e pequena de proposito: e a parte que ESCREVE, e portanto a
unica que pode se corromper. Perder o registro de um passeio e chato; perder o
mapa no meio da trilha e outra coisa.

SEGURANCA: so mexe em disco USB, entre 4 e 128 GB, que nao seja o disco de
sistema nem de boot, e pede o numero do disco explicitamente. Antes de escrever
um byte, mostra o que vai destruir.

    python grava_cartao.py --disco 1 --vec brasil.vec --dem brasil.dem
    python grava_cartao.py --disco 1 --conferir        (so le e valida)
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

SETOR = 512
MB = 1024 * 1024

TIPO_FAT32 = 0x0C          # FAT32 LBA
TIPO_BRUTO = 0xDA          # "non-FS data" - nao e montavel, e e essa a intencao
FAT32_MB = 1024            # 1 GB para os trajetos
ALINHA = 1 * MB // SETOR    # 2048 setores: alinhamento de bloco de apagamento


def ps(cmd):
    r = subprocess.run(['powershell', '-NoProfile', '-Command', cmd],
                       capture_output=True, text=True)
    return r.stdout.strip()


def descreve(n):
    j = ps(f"Get-Disk -Number {n} | Select-Object Number,FriendlyName,BusType,"
           f"Size,IsSystem,IsBoot,SerialNumber | ConvertTo-Json -Compress")
    import json
    return json.loads(j) if j else None


def confere_seguranca(n, d):
    if d is None:
        sys.exit(f"disco {n} nao existe")
    if d.get('BusType') != 'USB':
        sys.exit(f"TRAVA: o disco {n} nao e USB (e {d.get('BusType')})")
    if d.get('IsSystem') or d.get('IsBoot'):
        sys.exit(f"TRAVA: o disco {n} e de sistema ou de boot")
    gb = d['Size'] / (1024 ** 3)
    if not (4 <= gb <= 128):
        sys.exit(f"TRAVA: o disco {n} tem {gb:.1f} GB, fora da faixa de cartao")


def monta_mbr(particoes, total_setores):
    """particoes: [(tipo, inicio, tamanho)] em setores."""
    mbr = bytearray(512)
    for i, (tipo, ini, n) in enumerate(particoes[:4]):
        e = 0x1BE + i * 16
        mbr[e + 0] = 0x00                      # nao inicializavel
        # CHS nao importa em cartao moderno; 0xFE FF FF e o "use LBA" convencional
        mbr[e + 1:e + 4] = b'\xFE\xFF\xFF'
        mbr[e + 4] = tipo
        mbr[e + 5:e + 8] = b'\xFE\xFF\xFF'
        struct.pack_into('<II', mbr, e + 8, ini, n)
    mbr[510] = 0x55
    mbr[511] = 0xAA
    return bytes(mbr)


def grava(disco, vec, dem, so_conferir):
    d = descreve(disco)
    confere_seguranca(disco, d)
    total = d['Size'] // SETOR

    print(f"disco {disco}: {d['FriendlyName']} | {d['Size']/(1024**3):.2f} GB | "
          f"serial {d.get('SerialNumber')}")

    layout = []
    ini = ALINHA
    layout.append(('FAT32 (trajetos)', TIPO_FAT32, ini, FAT32_MB * MB // SETOR))
    ini += FAT32_MB * MB // SETOR

    arquivos = []
    for nome, cam, magic in (('vetor', vec, b'MTSVECT1'), ('relevo', dem, b'MTSDEM01')):
        if not cam:
            continue
        if not os.path.exists(cam):
            sys.exit(f"nao achei {cam}")
        with open(cam, 'rb') as f:
            if f.read(8) != magic:
                sys.exit(f"{cam} nao comeca com {magic.decode()}")
        n = (os.path.getsize(cam) + SETOR - 1) // SETOR
        n = ((n + ALINHA - 1) // ALINHA) * ALINHA          # alinha em 1 MB
        layout.append((nome, TIPO_BRUTO, ini, n))
        arquivos.append((cam, ini, os.path.getsize(cam)))
        ini += n

    print("\nlayout:")
    for nome, tipo, s, n in layout:
        print(f"  {nome:<18} tipo 0x{tipo:02X}  setor {s:>10,}  {n*SETOR/(1024**3):>6.2f} GB")
    print(f"  {'livre':<18}                            "
          f"{(total-ini)*SETOR/(1024**3):>6.2f} GB")
    if ini > total:
        sys.exit("nao cabe no cartao")

    if so_conferir:
        confere(disco, layout)
        return

    print(f"\nISTO APAGA TUDO no disco {disco}.")
    # limpa: sem volumes montados o Windows libera a escrita crua no disco
    print("limpando particoes...")
    ps(f"Clear-Disk -Number {disco} -RemoveData -RemoveOEM -Confirm:$false")

    escrito = 0
    with abre_disco(disco, 'rb+') as dev:
        dev.seek(0)
        dev.write(monta_mbr([(t, s, n) for _, t, s, n in layout], total))
        print("tabela de particao gravada")

        for cam, setor, tam in arquivos:
            print(f"gravando {os.path.basename(cam)} ({tam/(1024**3):.2f} GB) "
                  f"no setor {setor:,}...", flush=True)
            dev.seek(setor * SETOR)
            with open(cam, 'rb') as f:
                feito = 0
                while True:
                    # 4 MB por vez: menos syscall, e ainda multiplo de setor
                    b = f.read(4 * MB)
                    if not b:
                        break
                    if len(b) % SETOR:
                        b += b'\0' * (SETOR - len(b) % SETOR)
                    dev.write(b)
                    feito += len(b)
                    escrito += len(b)
                    if feito % (256 * MB) < 4 * MB:
                        print(f"    {feito/(1024**3):.2f} / {tam/(1024**3):.2f} GB",
                              flush=True)
    print(f"\ngravados {escrito/(1024**3):.2f} GB")

    ps(f"Update-Disk -Number {disco}")
    print("formatando a particao de trajetos...")
    ps(f"$p = Get-Partition -DiskNumber {disco} | Where-Object {{ $_.Size -lt 2GB }} | "
       f"Select-Object -First 1; if ($p) {{ "
       f"if (-not $p.DriveLetter) {{ $p | Add-PartitionAccessPath -AssignDriveLetter }}; "
       f"$p = Get-Partition -DiskNumber {disco} -PartitionNumber $p.PartitionNumber; "
       f"Format-Volume -DriveLetter $p.DriveLetter -FileSystem FAT32 "
       f"-NewFileSystemLabel MTS-LOG -Confirm:$false }}")

    confere(disco, layout)


def abre_disco(disco, modo):
    """Abre o disco cru dizendo o que fazer quando nao da - abrir
    \\\\.\\PhysicalDriveN exige administrador, e traceback de Python nao ajuda
    ninguem a resolver isso."""
    caminho = f"\\\\.\\PhysicalDrive{disco}"
    try:
        return open(caminho, modo, buffering=0)
    except PermissionError:
        sys.exit("\nPRECISA DE ADMINISTRADOR para ler ou gravar o disco cru.\n"
                 "  Abra um PowerShell como administrador e rode o mesmo comando,\n"
                 "  ou peca ao Claude - ele dispara o UAC para voce.")
    except FileNotFoundError:
        sys.exit(f"\ndisco {disco} sumiu. O cartao ainda esta no leitor?")
    except OSError as e:
        sys.exit(f"\nnao consegui abrir o disco {disco}: {e}")


def le_alinhado(dev, setor, nbytes):
    """Le nbytes a partir de um setor, respeitando o alinhamento do disco cru.

    O Windows exige que a leitura de \\\\.\\PhysicalDriveN seja multipla do setor
    TAMBEM NO TAMANHO, nao so no deslocamento. Pedir os 17.431 bytes exatos de um
    setor de mapa devolve OSError 22 - que nao diz nada sobre alinhamento e faz
    parecer que o cartao esta com defeito. Arredonda para cima e corta depois."""
    dev.seek(setor * SETOR)
    n = ((nbytes + SETOR - 1) // SETOR) * SETOR
    return dev.read(n)[:nbytes]


def confere(disco, layout):
    """Le de volta do cartao e valida - e a unica prova de que gravou certo."""
    print("\nconferindo o que ficou no cartao:")
    with abre_disco(disco, 'rb') as dev:
        dev.seek(0)
        mbr = dev.read(SETOR)
        assert mbr[510] == 0x55 and mbr[511] == 0xAA, "MBR sem assinatura"
        for i in range(4):
            e = 0x1BE + i * 16
            tipo = mbr[e + 4]
            s, n = struct.unpack_from('<II', mbr, e + 8)
            if not s:
                continue
            print(f"  p{i+1}: tipo 0x{tipo:02X}  setor {s:>10,}  "
                  f"{n*SETOR/(1024**3):>6.2f} GB", end='')
            if tipo == TIPO_BRUTO:
                dev.seek(s * SETOR)
                cab = dev.read(SETOR)
                magic = cab[0:8]
                print(f"  magic {magic.decode('ascii', 'replace')}", end='')
                if magic == b'MTSVECT1':
                    isec, dsec, nslots = struct.unpack_from('<III', cab, 12)
                    dev.seek((s + isec) * SETOR)
                    idx = dev.read(((nslots * 12 + SETOR - 1) // SETOR) * SETOR)
                    ruins = achados = 0
                    passo = max(1, nslots // 60)
                    for k in range(0, nslots, passo):
                        sec, ln, crc = struct.unpack_from('<III', idx, k * 12)
                        if not sec or not ln:
                            continue
                        achados += 1
                        b = le_alinhado(dev, s + sec, ln)
                        if (zlib.crc32(b) & 0xFFFFFFFF) != crc:
                            ruins += 1
                    print(f"  CRC: {achados-ruins}/{achados} OK", end='')
                elif magic == b'MTSDEM01':
                    n_, = struct.unpack_from('<H', cab, 10)
                    isec, dsec, nslots = struct.unpack_from('<III', cab, 16)
                    dev.seek((s + isec) * SETOR)
                    idx = dev.read(((nslots * 8 + SETOR - 1) // SETOR) * SETOR)
                    ruins = achados = 0
                    passo = max(1, nslots // 60)
                    for k in range(0, nslots, passo):
                        sec, crc = struct.unpack_from('<II', idx, k * 8)
                        if not sec:
                            continue
                        achados += 1
                        b = le_alinhado(dev, s + sec, n_ * n_ * 2)
                        if (zlib.crc32(b) & 0xFFFFFFFF) != crc:
                            ruins += 1
                    print(f"  CRC: {achados-ruins}/{achados} OK", end='')
                else:
                    print("  SEM MAGIC - particao vazia?", end='')
            print()
    print("\npronto.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--disco', type=int, required=True)
    ap.add_argument('--vec')
    ap.add_argument('--dem')
    ap.add_argument('--conferir', action='store_true')
    ap.add_argument('--so-cabecalho', action='store_true',
                    help="regrava apenas o setor 0 de cada particao bruta")
    a = ap.parse_args()
    if a.so_cabecalho:
        so_cabecalho(a.disco, a.vec, a.dem)
    else:
        grava(a.disco, a.vec, a.dem, a.conferir)


def so_cabecalho(disco, vec, dem):
    """Regrava so o setor 0 de cada particao bruta.

    Os campos derivados do cabecalho (tolerancia, maior bloco) mudam sem que um
    byte de geometria mude. Reescrever 4,75 GB por causa de 512 bytes seria meia
    hora de cartao a toa - e desgaste de flash sem motivo."""
    d = descreve(disco)
    confere_seguranca(disco, d)
    with abre_disco(disco, 'rb+') as dev:
        dev.seek(0)
        mbr = dev.read(SETOR)
        for i in range(4):
            e = 0x1BE + i * 16
            if mbr[e + 4] != TIPO_BRUTO:
                continue
            s = struct.unpack_from('<I', mbr, e + 8)[0]
            dev.seek(s * SETOR)
            magic = dev.read(SETOR)[0:8]
            fonte = {b'MTSVECT1': vec, b'MTSDEM01': dem}.get(magic)
            if not fonte:
                print(f"  p{i+1}: magic {magic!r} sem arquivo correspondente, pulando")
                continue
            with open(fonte, 'rb') as f:
                novo = f.read(SETOR)
            if novo[0:8] != magic:
                sys.exit(f"{fonte} nao bate com a particao p{i+1}")
            dev.seek(s * SETOR)
            dev.write(novo)
            print(f"  p{i+1} ({magic.decode()}): cabecalho regravado no setor {s:,}")
    print("pronto.")


if __name__ == '__main__':
    main()
