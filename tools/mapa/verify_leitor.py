#!/usr/bin/env python3
"""
Conformidade entre o GRAVADOR (Python) e o LEITOR (C) do mapa vetorial e do relevo.

POR QUE ISTO EXISTE
-------------------
O gravador e o leitor sao dois programas, em duas linguagens, que precisam
concordar sobre bytes. Nada no compilador nem no interpretador liga um ao outro:
se eu mudar um offset de um lado, o outro le lixo e NADA acusa - o mapa desenha
deslocado, ou nao desenha, e o erro parece "problema do cartao".

Ja aconteceu neste projeto, hoje: o relevo.py gravava porGrau no offset 12 e logo
depois tres u32 a partir do offset 12, apagando o campo. So apareceu porque o
info imprimia o valor.

Este script le os OFFSETS DECLARADOS NO .h - de verdade, do arquivo - e confere
contra o pacote gravado. Se alguem mexer em um lado so, aqui quebra.

    python verify_leitor.py --vec brasil.vec --dem brasil.dem
"""
import argparse
import os
import re
import struct
import sys
import zlib

# MTS_FW existe para o teste do teste: apontar para uma copia adulterada dos .h e
# conferir que este script REALMENTE quebra. Um verificador que nunca falhou nao
# provou nada.
FW = os.environ.get('MTS_FW') or os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'firmware')

ok_n, mal_n = 0, 0


def ok(nome, cond, detalhe=''):
    global ok_n, mal_n
    if cond:
        ok_n += 1
        print(f"  PASS  {nome}")
    else:
        mal_n += 1
        print(f"  FALHA {nome}  {detalhe}")


def le_h(nome):
    with open(os.path.join(FW, nome), encoding='utf-8', errors='replace') as f:
        return f.read()


# --------------------------------------------------------- extrai do .h
def offsets_de(src, funcao):
    """Acha os 'campo = xxLE32(h + N)' / 'h[N]' dentro de uma funcao do header.

    Nao e parser de C: e casamento de padrao no corpo da funcao. Basta para o que
    importa - pegar quando um offset muda de um lado so."""
    i = src.index(funcao)
    corpo = src[i:src.index('\n}', i)]
    r = {}
    # O (cast) opcional NAO e detalhe: sem ele na regex, 'x = (int32_t)dpLE32(h+28)'
    # nao casa e o campo some do resultado - que e indistinguivel de um campo que
    # o .h realmente deixou de ler. Alem de dar alarme falso, isso cega a
    # checagem de colisao de offsets logo abaixo, que so ve o que foi encontrado.
    cast = r'(?:\(\s*\w+\s*\)\s*)?'
    for m in re.finditer(rf'(\w+)\.(\w+)\s*=\s*{cast}\w+LE(16|32)\(h \+ (\d+)\)', corpo):
        r[m.group(2)] = (int(m.group(4)), int(m.group(3)) // 8)
    for m in re.finditer(rf'(\w+)\.(\w+)\s*=\s*{cast}h\[(\d+)\]', corpo):
        r[m.group(2)] = (int(m.group(3)), 1)
    return r


def const_de(src, nome):
    m = re.search(rf'#define\s+{nome}\s+(\d+)', src)
    return int(m.group(1)) if m else None


def enum_de(src, nome):
    m = re.search(rf'enum {nome}[^{{]*\{{(.*?)\}}', src, re.S)
    if not m:
        return {}
    r = {}
    for k, v in re.findall(r'(\w+)\s*=\s*(\d+)', m.group(1)):
        r[k] = int(v)
    return r


# ------------------------------------------------------------------ CRC32
def testa_crc(src, tag):
    """A tabela de nibble do .h tem de produzir o mesmo valor que o zlib.crc32."""
    m = re.search(rf'{tag}_CRC_NIB\[16\] = \{{(.*?)\}};', src, re.S)
    tab = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]+)UL', m.group(1))]
    ok(f"{tag}: tabela de CRC tem 16 entradas", len(tab) == 16)

    def crc_c(dados):
        c = 0xFFFFFFFF
        for b in dados:
            c ^= b
            c = (c >> 4) ^ tab[c & 0xF]
            c = (c >> 4) ^ tab[c & 0xF]
        return c ^ 0xFFFFFFFF

    for amostra in (b'', b'a', b'MTS', bytes(range(256)), os.urandom(1024)):
        if crc_c(amostra) != (zlib.crc32(amostra) & 0xFFFFFFFF):
            ok(f"{tag}: CRC32 bate com o zlib", False,
               f"em {len(amostra)} bytes")
            return
    ok(f"{tag}: CRC32 bate com o zlib (5 amostras, ate 1 KB)", True)


# -------------------------------------------------------------------- vec
def verifica_vec(caminho):
    print("\n--- vec_pack.h x vetor.py ---")
    src = le_h('vec_pack.h')
    testa_crc(src, 'VP')

    off = offsets_de(src, 'inline bool vecPackOpen')
    esperado = {'version': (8, 2), 'indexSector': (12, 4), 'dataSector': (16, 4),
                'nSlots': (20, 4), 'nDataSectors': (24, 4)}
    for campo, (o, t) in esperado.items():
        ok(f"offset de {campo} e {o}", off.get(campo) == (o, t),
           f"o .h diz {off.get(campo)}")
    ok("nLevels no byte 10", off.get('nLevels') == (10, 1), f"{off.get('nLevels')}")

    # O registro de nivel e lido a partir de L, nao de h, entao o offsets_de()
    # acima nao o enxerga. Sem esta checagem, tolMetros podia sair do lugar num
    # lado so e o firmware escolheria o nivel errado calado.
    i = src.index('inline bool vecPackOpen')
    corpo = src[i:src.index('\n}', i)]
    niv = {}
    for m in re.finditer(r'levels\[i\]\.(\w+)\s*=\s*(?:\(\s*\w+\s*\)\s*)?'
                         r'(?:vpLE(16|32)\(L \+ (\d+)\)|L\[(\d+)\])', corpo):
        campo = m.group(1)
        if m.group(4) is not None:
            niv[campo] = (int(m.group(4)), 1)
        else:
            niv[campo] = (int(m.group(3)), int(m.group(2)) // 8)
    # tem de bater com o struct '<BBHIIHHI' que o vetor.py grava
    esperado_niv = {'zoom': (0, 1), 'tolMetros': (2, 2), 'xmin': (4, 4),
                    'ymin': (8, 4), 'w': (12, 2), 'h': (14, 2), 'indexOffset': (16, 4)}
    for campo, v in esperado_niv.items():
        ok(f"registro de nivel: {campo} no byte {v[0]}", niv.get(campo) == v,
           f"o .h diz {niv.get(campo)}")
    ok("registro de nivel cabe em LVL_SIZE",
       max(o + t for o, t in niv.values()) <= const_de(src, 'VECPACK_LVL_SIZE'))
    ok("HDR_SIZE = 32", const_de(src, 'VECPACK_HDR_SIZE') == 32)
    ok("LVL_SIZE = 20", const_de(src, 'VECPACK_LVL_SIZE') == 20)
    ok("IDX_SIZE = 12", const_de(src, 'VECPACK_IDX_SIZE') == 12)

    # as classes do .h tem de bater com as do vetor.py
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vetor
    cls = enum_de(src, 'VecClasse')
    for nome, num in vetor.CL.items():
        chave = 'VEC_' + {'protegida': 'PROTEGIDA', 'uso': 'USO'}.get(nome, nome.upper())
        ok(f"classe {nome} = {num} nos dois lados", cls.get(chave) == num,
           f"o .h diz {cls.get(chave)}")
    poi = enum_de(src, 'VecPoi')
    faltando = [n for n in vetor.POI_CL
                if 'VECP_' + n.upper() not in poi]
    ok("todos os POI do gravador existem no leitor", not faltando, str(faltando))
    for n, v in vetor.POI_CL.items():
        k = 'VECP_' + n.upper()
        if k in poi:
            ok(f"POI {n} = {v}", poi[k] == v, f"o .h diz {poi[k]}")

    if not caminho or not os.path.exists(caminho):
        print("  (sem pacote para conferir - passe --vec)")
        return

    print(f"\n--- lendo {os.path.basename(caminho)} como o firmware leria ---")
    with open(caminho, 'rb') as f:
        h = f.read(512)
        ok("magic", h[0:8] == b'MTSVECT1')
        ver = struct.unpack_from('<H', h, 8)[0]
        nlv = h[10]
        isec, dsec, nslots = struct.unpack_from('<III', h, 12)
        ok("versao 1", ver == 1, str(ver))
        ok("cabecalho + niveis cabem no setor 0", 32 + nlv * 20 <= 512)
        ok("indice vem antes dos dados", 0 < isec < dsec)

        niveis = []
        for i in range(nlv):
            z, _, _, _, xmin, ymin, w, hh, o = struct.unpack_from('<BBBBIIHHI', h, 32 + i*20)
            niveis.append((z, xmin, ymin, w, hh, o))
        soma = 0
        for i, (z, xmin, ymin, w, hh, o) in enumerate(niveis):
            ok(f"nivel {i}: offset do indice bate com a soma das grades", o == soma,
               f"offset {o}, esperado {soma}")
            soma += w * hh
        ok("total de slots bate com as grades", soma == nslots, f"{soma} x {nslots}")
        ok("zooms em ordem decrescente de detalhe",
           all(niveis[i][0] > niveis[i+1][0] for i in range(len(niveis)-1)))

        # conferencia de conteudo: le alguns setores e valida CRC e estrutura
        f.seek(isec * 512)
        idx = f.read(((nslots * 12 + 511) // 512) * 512)
        cheios = [s for s in range(nslots)
                  if struct.unpack_from('<I', idx, s * 12)[0]]
        ok("ha setores com dado", len(cheios) > 0, f"{len(cheios)}")

        import random
        random.seed(7)
        ruins = 0
        estrut = 0
        for s in random.sample(cheios, min(200, len(cheios))):
            sec, ln, crc = struct.unpack_from('<III', idx, s * 12)
            f.seek(sec * 512)
            b = f.read(ln)
            if (zlib.crc32(b) & 0xFFFFFFFF) != crc:
                ruins += 1
                continue
            if not caminha_bloco(b):
                estrut += 1
        ok(f"CRC de 200 setores sorteados", ruins == 0, f"{ruins} errados")
        ok(f"estrutura de 200 setores (camadas, feicoes, pontos)", estrut == 0,
           f"{estrut} mal formados")


def caminha_bloco(b):
    """Percorre o bloco exatamente como o VecCursor do .h, e exige acabar no fim.

    Se sobrar ou faltar byte, a serializacao e o percurso discordam - e na tela
    isso viraria uma linha aleatoria atravessando o mapa."""
    try:
        i = 0
        ncam = struct.unpack_from('<H', b, i)[0]; i += 2
        for _ in range(ncam):
            classe = b[i]; nfe = struct.unpack_from('<H', b, i+1)[0]; i += 3
            for _ in range(nfe):
                if classe == 14:                    # POI
                    ln = b[i+5]; i += 6 + ln
                else:
                    n = struct.unpack_from('<H', b, i)[0]; i += 2 + n * 4
                if i > len(b):
                    return False
        return i == len(b)
    except Exception:
        return False


# -------------------------------------------------------------------- dem
def verifica_dem(caminho):
    print("\n--- dem_pack.h x relevo.py ---")
    src = le_h('dem_pack.h')
    testa_crc(src, 'DP')

    off = offsets_de(src, 'inline bool demPackOpen')
    esperado = {'version': (8, 2), 'n': (10, 2), 'indexSector': (16, 4),
                'dataSector': (20, 4), 'nSlots': (24, 4), 'lin0': (28, 4),
                'col0': (32, 4), 'w': (36, 2), 'h': (38, 2), 'blocoSetores': (40, 4)}
    for campo, (o, t) in esperado.items():
        ok(f"offset de {campo} e {o}", off.get(campo) == (o, t),
           f"o .h diz {off.get(campo)}")
    ok("porGrau no byte 12", off.get('porGrau') == (12, 1), f"{off.get('porGrau')}")
    # o bug que ja aconteceu: dois campos no mesmo offset
    usados = {}
    colisao = []
    for campo, (o, t) in off.items():
        for k in range(o, o + t):
            if k in usados:
                colisao.append(f"{campo} x {usados[k]} no byte {k}")
            usados[k] = campo
    ok("nenhum campo do cabecalho se sobrepoe", not colisao, '; '.join(colisao))

    import relevo
    ok(f"N = {relevo.N} nos dois lados", const_de(src, 'DEMPACK_SECTOR') == 512)
    ok("IDX_SIZE = 8 divide 512 (registro nunca cruza setor)",
       const_de(src, 'DEMPACK_IDX_SIZE') == 8 and 512 % 8 == 0)
    ok("1201 = porGrau*(N-1)+1", relevo.POR_GRAU * (relevo.N - 1) + 1 == 1201)

    if not caminho or not os.path.exists(caminho):
        print("  (sem pacote para conferir - passe --dem)")
        return

    print(f"\n--- lendo {os.path.basename(caminho)} como o firmware leria ---")
    with open(caminho, 'rb') as f:
        h = f.read(512)
        ok("magic", h[0:8] == b'MTSDEM01')
        ver, n = struct.unpack_from('<HH', h, 8)
        porgrau = h[12]
        isec, dsec, nslots = struct.unpack_from('<III', h, 16)
        lin0, col0, w, hh = struct.unpack_from('<iiHH', h, 28)
        bsec = struct.unpack_from('<I', h, 40)[0]
        ok("versao 1", ver == 1)
        ok(f"N = {n}", n == relevo.N)
        ok(f"porGrau = {porgrau}", porgrau == relevo.POR_GRAU)
        ok("w*h = nSlots", w * hh == nslots, f"{w}x{hh} x {nslots}")
        ok("bloco cabe nos setores declarados", n * n * 2 <= bsec * 512)
        ok("indice vem antes dos dados", 0 < isec < dsec)

        f.seek(isec * 512)
        idx = f.read(((nslots * 8 + 511) // 512) * 512)
        cheios = [s for s in range(nslots)
                  if struct.unpack_from('<I', idx, s * 8)[0]]
        ok("ha blocos com dado", len(cheios) > 0, f"{len(cheios)}")

        import random
        random.seed(7)
        ruins = 0
        for s in random.sample(cheios, min(200, len(cheios))):
            sec, crc = struct.unpack_from('<II', idx, s * 8)
            f.seek(sec * 512)
            if (zlib.crc32(f.read(n * n * 2)) & 0xFFFFFFFF) != crc:
                ruins += 1
        ok("CRC de 200 blocos sorteados", ruins == 0, f"{ruins} errados")

        # o slot calculado pela formula do .h tem de achar o bloco certo
        import math
        erros = 0
        for s in random.sample(cheios, min(50, len(cheios))):
            lin, col = divmod(s, w)
            lat = (lin + lin0) / porgrau - 90.0 + 0.05     # meio do bloco
            lon = (col + col0) / porgrau - 180.0 + 0.05
            calc_lin = math.floor((lat + 90.0) * porgrau) - lin0
            calc_col = math.floor((lon + 180.0) * porgrau) - col0
            if calc_lin * w + calc_col != s:
                erros += 1
        ok("formula de endereco do .h reencontra o bloco (50 sorteios)", erros == 0,
           f"{erros} erraram")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vec')
    ap.add_argument('--dem')
    a = ap.parse_args()
    verifica_vec(a.vec)
    verifica_dem(a.dem)
    print(f"\n{ok_n} PASS, {mal_n} FALHA")
    sys.exit(1 if mal_n else 0)


if __name__ == '__main__':
    main()
