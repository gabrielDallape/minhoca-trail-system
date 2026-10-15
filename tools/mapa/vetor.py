#!/usr/bin/env python3
"""
MTS - conversor de OpenStreetMap para o pacote vetorial do aparelho.

POR QUE VETOR, E NAO O TILE RASTER DE tools/tiles/
-------------------------------------------------
Medido neste repo: o Brasil em tiles RGB565 na resolucao que a tela usa (2,4 m/px)
daria 55 milhoes de tiles, 7 TB. O cartao tem 29 GB. Raster guarda todos os pixels,
inclusive os vazios, e o Brasil e quase todo vazio.

A mesma malha em vetor da 207 milhoes de pontos, ~840 MB - 2,9% do cartao. E o
firmware ja e um desenhista de vetor: mapa.h desenha o trajeto com trecho(), e uma
estrada e so mais uma polilinha.

MEDIDO no extrato do Centro-Oeste (191,6 MB de .pbf, 1,6 milhao de km2):
    mata e uso do solo ....  5,07 M pontos
    trilha/estrada de terra  4,44 M
    hidrografia ...........  5,71 M
    rua de cidade .........  2,32 M
    asfalto ...............  1,10 M
    POI ................... 83.739  (13.609 porteiras, 1.607 vaus, 53.881 nascentes)
    densidade .............  5,06 pontos/km2

Dessa densidade sai o orcamento de quadro: a 2 m/px a tela cobre 3,7 km2 (~19
pontos) e a 12 m/px cobre 133 km2 (~670 pontos). O mapa.h ja desenha 1.200 pontos
de rastro por quadro, entao no campo cabe. Na cidade nao cabe - por isso existem
os NIVEIS DE DETALHE abaixo, que jogam fora classe e vertice conforme o zoom.

FORMATO
-------
Grade de setores = a mesma do slippy map do tile_pack.h, para as duas metades do
projeto usarem uma unica matematica de Mercator.

    setor 0             cabecalho + tabela de niveis
    setor indexSector   indice: por slot {setor u32, bytes u32, crc32 u32}
    setor dataSector..  blocos, cada um alinhado em setor de 512 B

Dentro do bloco, cada ponto e um par u16 NORMALIZADO no setor (0..65535). Num
setor de z11 (~19,5 km) isso da 0,3 m de resolucao - mais fino que o GPS - e
custa 4 bytes por ponto, fixos. A escolha e deliberadamente burra: varint
comprimiria mais, mas o decode entra no caminho do quadro, e 2 shifts e uma
multiplicacao por ponto e o que cabe no orcamento de 73 ms de pushSprite.

Se mudar o formato, mude tambem em firmware/vec_pack.h.
"""
import argparse
import collections
import math
import os
import struct
import sys
import zlib

SETOR = 512
MAGIC = b"MTSVECT1"
VERSAO = 1

# ---------------------------------------------------------------- classes
# O numero e gravado no arquivo: NUNCA renumerar, so acrescentar no fim.
CL = {
    'estrada':    1,   # asfalto: federal, estadual, terciaria
    'trilha':     2,   # track, path, unclassified - o que se anda em off road
    'rua':        3,   # residential, service
    'pedestre':   4,   # footway, cycleway, steps
    'rio':        5,
    'corrego':    6,
    'lago':       7,   # area
    'mata':       8,   # area
    'uso':        9,   # area: lavoura, pasto
    'protegida': 10,   # area: parque, terra indigena
    'ferrovia':  11,
    'pista':     12,   # pista de pouso - em trilha, e referencia e pouso de resgate
    'limite':    13,
    'poi':       14,
}
AREAS = {CL['lago'], CL['mata'], CL['uso'], CL['protegida']}

# POI: so o que salva a viagem. O OSM tem milhares de valores; carregar todos
# encheria o cartao de padaria e ponto de onibus.
POI_CL = {
    'combustivel': 1, 'agua': 2, 'hospital': 3, 'farmacia': 4, 'comida': 5,
    'pousada': 6, 'camping': 7, 'mecanico': 8, 'borracharia': 9, 'mercado': 10,
    'policia': 11, 'abrigo': 12, 'mirante': 13, 'pico': 14, 'cachoeira': 15,
    'nascente': 16, 'caverna': 17, 'porteira': 18, 'cancela': 19,
    'mata_burro': 20, 'vau': 21, 'bloqueio': 22, 'torre': 23, 'poco': 24,
}

TAG_POI = {
    'amenity': {'fuel': 'combustivel', 'drinking_water': 'agua', 'hospital': 'hospital',
                'clinic': 'hospital', 'pharmacy': 'farmacia', 'restaurant': 'comida',
                'cafe': 'comida', 'police': 'policia', 'shelter': 'abrigo'},
    'tourism': {'camp_site': 'camping', 'alpine_hut': 'abrigo', 'wilderness_hut': 'abrigo',
                'viewpoint': 'mirante', 'hotel': 'pousada', 'guest_house': 'pousada'},
    'shop': {'car_repair': 'mecanico', 'tyres': 'borracharia',
             'convenience': 'mercado', 'supermarket': 'mercado'},
    'natural': {'peak': 'pico', 'volcano': 'pico', 'spring': 'nascente',
                'cave_entrance': 'caverna'},
    'waterway': {'waterfall': 'cachoeira'},
    'barrier': {'gate': 'porteira', 'lift_gate': 'cancela', 'cattle_grid': 'mata_burro',
                'block': 'bloqueio', 'bollard': 'bloqueio'},
    'ford': {'yes': 'vau', 'stepping_stones': 'vau'},
    'man_made': {'water_well': 'poco'},
}

HW = {
    'motorway': 'estrada', 'trunk': 'estrada', 'primary': 'estrada',
    'secondary': 'estrada', 'tertiary': 'estrada', 'motorway_link': 'estrada',
    'trunk_link': 'estrada', 'primary_link': 'estrada', 'secondary_link': 'estrada',
    'tertiary_link': 'estrada',
    'track': 'trilha', 'path': 'trilha', 'bridleway': 'trilha', 'unclassified': 'trilha',
    'residential': 'rua', 'service': 'rua', 'living_street': 'rua',
    'footway': 'pedestre', 'cycleway': 'pedestre', 'steps': 'pedestre',
    'pedestrian': 'pedestre',
}


def classe_da_via(t):
    hw = t.get('highway')
    if hw is not None:
        return CL.get(HW.get(hw, ''), None)
    wl = t.get('waterway')
    if wl in ('river', 'canal'):
        return CL['rio']
    if wl in ('stream', 'ditch', 'drain'):
        return CL['corrego']
    if t.get('natural') == 'water' or t.get('landuse') in ('reservoir', 'basin'):
        return CL['lago']
    if t.get('natural') in ('wood', 'scrub', 'grassland', 'heath', 'wetland'):
        return CL['mata']
    if t.get('landuse') in ('forest', 'farmland', 'meadow', 'orchard', 'vineyard'):
        return CL['uso']
    if t.get('boundary') in ('protected_area', 'national_park', 'aboriginal_lands'):
        return CL['protegida']
    if t.get('railway') in ('rail', 'narrow_gauge', 'light_rail'):
        return CL['ferrovia']
    if t.get('aeroway') in ('runway', 'aerodrome'):
        return CL['pista']
    return None


# ------------------------------------------------------- niveis de detalhe
# (nome, zoom da grade, classes que entram, tolerancia de simplificacao em metros)
#
# O ZOOM DA GRADE SAI DA TELA, NAO DO GOSTO. A tela tem 1280 px e o firmware usa
# ZOOMS[] = {0,4  0,9  2,0  5,0  12,0} m/px, o que cobre:
#       0,4 -> 0,5 km    0,9 -> 1,2 km    2,0 -> 2,6 km
#       5,0 -> 6,4 km   12,0 -> 15,4 km
# O setor tem de ficar na ordem do dobro da cobertura: menor que isso e ler muitos
# setores (cada um custa 12,4 ms de busca, MEDIDO); maior e ler dado que nao vai
# aparecer.
#
# A primeira versao usava z11 (19,6 km) para o detalhe e o resultado foi MEDIDO:
# setor medio 17,1 KB / 10 ms, mas o pior - Brasilia inteira num quadrado so -
# 1470 KB / 837 ms. Uma travada visivel ao entrar na cidade. Dividir a grade
# resolve sem jogar dado fora: e a mesma informacao em pedacos do tamanho certo.
#
# A tolerancia tambem sai da tela: simplificar com ~1 px da escala em que o nivel
# vai ser usado nao muda nada que o olho veja, e corta a maior parte dos vertices
# de rua urbana.
NIVEIS = [
    # detalhe: 0,4 a 2 m/px (cobre ate 2,6 km). Setor de 4,9 km. Tudo.
    dict(nome='detalhe', zoom=13, tol=1.0,
         classes=set(CL.values())),
    # medio: 5 m/px (cobre 6,4 km). Setor de 19,6 km. Sai calcada e corrego.
    dict(nome='medio', zoom=11, tol=8.0,
         classes={CL['estrada'], CL['trilha'], CL['rua'], CL['rio'], CL['lago'],
                  CL['mata'], CL['protegida'], CL['ferrovia'], CL['pista'],
                  CL['limite'], CL['poi']}),
    # geral: 12 m/px (cobre 15,4 km). Setor de 39 km. So o esqueleto.
    dict(nome='geral', zoom=10, tol=24.0,
         classes={CL['estrada'], CL['trilha'], CL['rio'], CL['lago'],
                  CL['protegida'], CL['limite'], CL['poi']}),
]


# ------------------------------------------------------------- Mercator
# Identico ao tilePackDegToTile do firmware. Se divergir, o mapa desenha deslocado
# e nada acusa - por isso a copia e literal, e nao "equivalente".
def deg2num(lat, lon, z):
    n = 1 << z
    lat = max(-85.05112878, min(85.05112878, lat))
    fx = (lon + 180.0) / 360.0 * n
    s = math.sin(math.radians(lat))
    fy = (1.0 - math.log((1.0 + s) / (1.0 - s)) / (2.0 * math.pi)) / 2.0 * n
    return fx, fy


def metros_por_grau_lon(lat):
    return 111320.0 * math.cos(math.radians(lat))


# ------------------------------------------------------ simplificacao
def douglas_peucker(pts, tol2):
    """Reduz vertices sem mover a linha mais que a tolerancia. Iterativo de
    proposito: uma via de rio no Amazonas tem dezenas de milhares de pontos e a
    versao recursiva estoura a pilha do Python."""
    if len(pts) < 3:
        return pts
    manter = [False] * len(pts)
    manter[0] = manter[-1] = True
    pilha = [(0, len(pts) - 1)]
    while pilha:
        i, j = pilha.pop()
        if j <= i + 1:
            continue
        ax, ay = pts[i]
        bx, by = pts[j]
        dx, dy = bx - ax, by - ay
        den = dx * dx + dy * dy
        pior, pd = -1, tol2
        for k in range(i + 1, j):
            px, py = pts[k]
            if den == 0:
                d = (px - ax) ** 2 + (py - ay) ** 2
            else:
                t = ((px - ax) * dx + (py - ay) * dy) / den
                t = 0.0 if t < 0 else (1.0 if t > 1 else t)
                ex, ey = ax + t * dx - px, ay + t * dy - py
                d = ex * ex + ey * ey
            if d > pd:
                pior, pd = k, d
        if pior > 0:
            manter[pior] = True
            pilha.append((i, pior))
            pilha.append((pior, j))
    return [p for p, m in zip(pts, manter) if m]


# ------------------------------------------------ recorte pela grade
def recorta(pts, z):
    """Parte a polilinha nos limites dos setores, interpolando o cruzamento.

    Sem isto, uma rodovia de 400 km viraria uma feicao gigante presa a um setor
    so, e o aparelho teria de ler o Brasil inteiro para desenhar 20 km dela.
    Interpolar o cruzamento (em vez de simplesmente cortar) evita o buraco visivel
    na borda de cada setor."""
    saida = collections.defaultdict(list)
    if len(pts) < 2:
        if pts:
            saida[(int(pts[0][0]), int(pts[0][1]))].append(list(pts))
        return saida

    atual = (int(pts[0][0]), int(pts[0][1]))
    corrente = [pts[0]]
    for i in range(1, len(pts)):
        x0, y0 = pts[i - 1]
        x1, y1 = pts[i]
        cel = (int(x1), int(y1))
        if cel == atual:
            corrente.append((x1, y1))
            continue
        # anda o segmento em passos pequenos e quebra a cada troca de celula.
        # E aproximado por passo, nao por geometria exata: o erro maximo e o
        # tamanho do passo (1/64 do setor, ~300 m em z11) e some no recorte de
        # tela, mas custa muito menos que um Liang-Barsky por celula.
        n = max(abs(int(x1) - int(x0)), abs(int(y1) - int(y0))) * 4 + 1
        n = min(n, 512)
        for s in range(1, n + 1):
            t = s / n
            px, py = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            c = (int(px), int(py))
            if c != atual:
                corrente.append((px, py))
                if len(corrente) >= 2:
                    saida[atual].append(corrente)
                atual = c
                corrente = [(px, py)]
        corrente.append((x1, y1))
    if len(corrente) >= 2:
        saida[atual].append(corrente)
    return saida


def q(v):
    """Normaliza a coordenada dentro do setor para u16."""
    v = int(v * 65536.0)
    return 0 if v < 0 else (65535 if v > 65535 else v)


# ============================================================ passagem 1
# Le o .pbf e derrama as feicoes ja recortadas em BALDES no disco.
#
# Por que balde, e nao um dicionario na memoria: o Brasil da ~207 milhoes de
# pontos. Segurar isso em objetos Python custaria dezenas de GB. Derramando em
# 64 arquivos e processando um por vez, o pico fica em ~1/64 do total.
N_BALDES = 64
REC = struct.Struct('<BBHHH')     # nivel, classe, cx, cy, nPontos


class Derrama:
    def __init__(self, tmp):
        os.makedirs(tmp, exist_ok=True)
        self.tmp = tmp
        self.f = [open(os.path.join(tmp, f'b{i:02d}.bin'), 'wb', buffering=1 << 20)
                  for i in range(N_BALDES)]
        self.pontos = collections.Counter()
        self.feicoes = collections.Counter()

    @staticmethod
    def balde(cx, cy):
        return ((cx * 2654435761) ^ (cy * 40503)) % N_BALDES

    def linha(self, nivel, classe, cx, cy, pts):
        n = len(pts)
        if n < 2 or n > 65535:
            return
        b = self.f[self.balde(cx, cy)]
        b.write(REC.pack(nivel, classe, cx, cy, n))
        b.write(struct.pack(f'<{2*n}H', *[c for p in pts for c in p]))
        self.pontos[nivel] += n
        self.feicoes[nivel] += 1

    def poi(self, nivel, cx, cy, x, y, sub, nome):
        nome = nome.encode('utf-8')[:31]
        b = self.f[self.balde(cx, cy)]
        b.write(REC.pack(nivel, CL['poi'], cx, cy, 1))
        b.write(struct.pack('<HHBB', x, y, sub, len(nome)))
        b.write(nome)
        self.pontos[nivel] += 1
        self.feicoes[nivel] += 1

    def fecha(self):
        for f in self.f:
            f.close()


import osmium  # noqa: E402  (depois das constantes, para o erro de import ser claro)


class Leitor(osmium.SimpleHandler):
    def __init__(self, d, verbose):
        super().__init__()
        self.d = d
        self.verbose = verbose
        self.nw = 0
        self.npoi = 0
        self.relacoes_ignoradas = 0

    def _emite(self, classe, coords, fechada):
        """coords em (lat, lon). Emite em todos os niveis que aceitam a classe."""
        latm = coords[len(coords) // 2][0]
        for ni, nv in enumerate(NIVEIS):
            if classe not in nv['classes']:
                continue
            z = nv['zoom']
            pts = [deg2num(la, lo, z) for la, lo in coords]
            # tolerancia de metros -> unidades de setor, na latitude da feicao
            larg = 156543.03392 * 256.0 * math.cos(math.radians(latm)) / (1 << z)
            tol = nv['tol'] / max(larg, 1.0)
            if len(pts) > 2:
                pts = douglas_peucker(pts, tol * tol)
            if fechada and len(pts) > 2 and pts[0] != pts[-1]:
                pts.append(pts[0])
            for (cx, cy), pedacos in recorta(pts, z).items():
                if cx < 0 or cy < 0 or cx > 65535 or cy > 65535:
                    continue
                for pe in pedacos:
                    self.d.linha(ni, classe, cx, cy,
                                 [(q(px - cx), q(py - cy)) for px, py in pe])

    def way(self, w):
        c = classe_da_via(w.tags)
        if c is None:
            return
        try:
            coords = [(n.lat, n.lon) for n in w.nodes if n.location.valid()]
        except osmium.InvalidLocationError:
            return
        if len(coords) < 2:
            return
        self._emite(c, coords, c in AREAS)
        self.nw += 1
        if self.verbose and self.nw % 200000 == 0:
            print(f"    {self.nw:,} vias, {sum(self.d.pontos.values()):,} pontos",
                  flush=True)

    def node(self, n):
        for chave, mapa in TAG_POI.items():
            v = n.tags.get(chave)
            if v is None or v not in mapa:
                continue
            sub = POI_CL[mapa[v]]
            nome = n.tags.get('name') or ''
            for ni, nv in enumerate(NIVEIS):
                if CL['poi'] not in nv['classes']:
                    continue
                # no nivel geral so entra o que se procura de longe
                if ni == 2 and mapa[v] not in ('combustivel', 'hospital', 'camping'):
                    continue
                fx, fy = deg2num(n.location.lat, n.location.lon, nv['zoom'])
                cx, cy = int(fx), int(fy)
                self.d.poi(ni, cx, cy, q(fx - cx), q(fy - cy), sub, nome)
            self.npoi += 1
            return

    def relation(self, r):
        # Multipoligono (lago e area de mata grandes) precisa montar aneis a
        # partir dos membros. Fica de fora desta versao DE PROPOSITO, e o
        # contador existe para a falta ser visivel em vez de silenciosa: no
        # Centro-Oeste sao 1.378 lagos e 3.624 matas contra 31 mil e 68 mil
        # vindos de way, ou seja ~5% das areas.
        if classe_da_via(r.tags) is not None:
            self.relacoes_ignoradas += 1


# ============================================================ passagem 2
# Le os baldes, junta o que caiu no mesmo setor e serializa o bloco.
def monta_bloco(camadas):
    """camadas: {classe: [payload de feicao, ...]}. Devolve os bytes do bloco."""
    out = [struct.pack('<H', len(camadas))]
    for classe in sorted(camadas):
        itens = camadas[classe]
        out.append(struct.pack('<BH', classe, len(itens)))
        out.extend(itens)
    return b''.join(out)


def le_balde(caminho):
    """Gera (nivel, classe, cx, cy, payload_da_feicao)."""
    with open(caminho, 'rb') as f:
        dados = f.read()
    i, n = 0, len(dados)
    while i < n:
        nivel, classe, cx, cy, npt = REC.unpack_from(dados, i)
        i += REC.size
        if classe == CL['poi']:
            x, y, sub, ln = struct.unpack_from('<HHBB', dados, i)
            i += 6
            nome = dados[i:i + ln]
            i += ln
            yield nivel, classe, cx, cy, struct.pack('<HHBB', x, y, sub, ln) + nome
        else:
            b = dados[i:i + 4 * npt]
            i += 4 * npt
            yield nivel, classe, cx, cy, struct.pack('<H', npt) + b


def converte(pbf, saida, tmp, verbose):
    d = Derrama(tmp)
    print(f"lendo {pbf} ...", flush=True)
    lt = Leitor(d, verbose)
    # locations=True faz a osmium resolver a coordenada de cada no das vias.
    # flex_mem usa RAM (rapido); se faltar memoria, troque por
    # 'sparse_file_array,<arquivo>' - fica ~3x mais lento mas nao estoura.
    lt.apply_file(pbf, locations=True, idx='flex_mem')
    d.fecha()
    print(f"  {lt.nw:,} vias, {lt.npoi:,} POIs, "
          f"{lt.relacoes_ignoradas:,} relacoes de area ignoradas")
    for ni, nv in enumerate(NIVEIS):
        print(f"  nivel {nv['nome']:<8} z{nv['zoom']:<3} "
              f"{d.feicoes[ni]:>10,} feicoes  {d.pontos[ni]:>12,} pontos")

    # --- serializa balde por balde, gravando os blocos num arquivo temporario
    dat = os.path.join(tmp, 'dados.bin')
    mapa = {}                       # (nivel, cx, cy) -> (setorRelativo, bytes, crc)
    setor_rel = 0
    with open(dat, 'wb') as fd:
        for b in range(N_BALDES):
            cam = collections.defaultdict(lambda: collections.defaultdict(list))
            p = os.path.join(tmp, f'b{b:02d}.bin')
            for nivel, classe, cx, cy, payload in le_balde(p):
                cam[(nivel, cx, cy)][classe].append(payload)
            for chave in sorted(cam):
                bloco = monta_bloco(cam[chave])
                crc = zlib.crc32(bloco) & 0xFFFFFFFF
                mapa[chave] = (setor_rel, len(bloco), crc)
                pad = (-len(bloco)) % SETOR
                fd.write(bloco + b'\0' * pad)
                setor_rel += (len(bloco) + pad) // SETOR
            os.remove(p)
            if verbose:
                print(f"    balde {b+1}/{N_BALDES}: {len(mapa):,} setores montados",
                      flush=True)

    # --- grade de cada nivel
    niveis = []
    total_slots = 0
    for ni, nv in enumerate(NIVEIS):
        cel = [(cx, cy) for (n, cx, cy) in mapa if n == ni]
        if not cel:
            niveis.append(dict(zoom=nv['zoom'], xmin=0, ymin=0, w=0, h=0, off=0))
            continue
        xs = [c[0] for c in cel]
        ys = [c[1] for c in cel]
        g = dict(zoom=nv['zoom'], xmin=min(xs), ymin=min(ys),
                 w=max(xs) - min(xs) + 1, h=max(ys) - min(ys) + 1, off=total_slots)
        total_slots += g['w'] * g['h']
        niveis.append(g)
        print(f"  grade {nv['nome']:<8} {g['w']}x{g['h']} = {g['w']*g['h']:,} slots, "
              f"{len(cel):,} com dado ({100.0*len(cel)/(g['w']*g['h']):.1f}% cheio)")

    IDX = 12                                    # setor u32, bytes u32, crc u32
    setores_indice = (total_slots * IDX + SETOR - 1) // SETOR
    indice_setor = 1
    dados_setor = indice_setor + setores_indice

    # --- escreve o arquivo final
    print(f"gravando {saida} ...", flush=True)
    with open(saida, 'wb') as f:
        # O MAIOR BLOCO VAI NO CABECALHO. Sem isso o firmware tem de adivinhar o
        # tamanho do buffer, e adivinhar da errado: a primeira versao usou 448 KB
        # (estimado convertendo o tempo de leitura em bytes) enquanto o maior
        # bloco de verdade tem 615 KB. Os 5 setores acima do buffer - as maiores
        # cidades - simplesmente NAO ERAM DESENHADOS, e o unico sinal disso era
        # um contador interno que ninguem via.
        maior = max((ln for _, ln, _ in mapa.values()), default=0)

        h = bytearray(SETOR)
        h[0:8] = MAGIC
        struct.pack_into('<HBB', h, 8, VERSAO, len(NIVEIS), 0)
        struct.pack_into('<III', h, 12, indice_setor, dados_setor, total_slots)
        struct.pack_into('<I', h, 24, setor_rel)             # setores de dado
        struct.pack_into('<I', h, 28, maior)                 # maior bloco, em bytes
        for i, g in enumerate(niveis):
            # tolMetros ocupa os bytes 2-3, que eram reservados. E o que o
            # firmware usa para ESCOLHER o nivel: um nivel simplificado com
            # tolerancia T so serve enquanto T couber em ~1,5 pixel. Sem esse
            # campo o leitor tinha de adivinhar pela largura do setor, e adivinhou
            # errado - pulava o nivel de detalhe justamente a 2 m/px.
            tol = int(round(NIVEIS[i]['tol'])) if i < len(NIVEIS) else 0
            struct.pack_into('<BBHIIHHI', h, 32 + i * 20,
                             g['zoom'], 0, min(tol, 65535),
                             g['xmin'], g['ymin'], g['w'], g['h'], g['off'])
        f.write(h)

        idx = bytearray(setores_indice * SETOR)
        for (ni, cx, cy), (sr, ln, crc) in mapa.items():
            g = niveis[ni]
            slot = g['off'] + (cy - g['ymin']) * g['w'] + (cx - g['xmin'])
            struct.pack_into('<III', idx, slot * IDX, dados_setor + sr, ln, crc)
        f.write(idx)

        with open(dat, 'rb') as fd:
            while True:
                c = fd.read(1 << 22)
                if not c:
                    break
                f.write(c)
    os.remove(dat)

    tam = os.path.getsize(saida)
    print(f"\nOK: {tam/1e6:.1f} MB  ({len(mapa):,} setores com dado, "
          f"indice {setores_indice*SETOR/1e6:.1f} MB)")
    return saida


# ================================================================== info
def info(caminho):
    with open(caminho, 'rb') as f:
        h = f.read(SETOR)
        if h[0:8] != MAGIC:
            sys.exit("nao e um pacote MTSVECT")
        versao, nn, _ = struct.unpack_from('<HBB', h, 8)
        isec, dsec, slots = struct.unpack_from('<III', h, 12)
        nsd, = struct.unpack_from('<I', h, 24)
        print(f"versao {versao}, {nn} niveis, {slots:,} slots")
        print(f"indice no setor {isec}, dados no setor {dsec}, "
              f"{nsd:,} setores de dado ({nsd*SETOR/1e6:.1f} MB)")

        niveis = []
        for i in range(nn):
            z, _, _, _, xmin, ymin, w, hh, off = struct.unpack_from('<BBBBIIHHI', h, 32 + i*20)
            niveis.append((z, xmin, ymin, w, hh, off))

        f.seek(isec * SETOR)
        idx = f.read((slots * 12 + SETOR - 1) // SETOR * SETOR)

        # O numero que decide a arquitetura nao e a media: e a CAUDA. Um mapa com
        # media de 10 ms e pior caso de 800 ms trava ao entrar na cidade, e a
        # media nao denuncia isso. Por isso p50/p95/p99/pior, e por nivel.
        #
        # 12,4 ms e o custo MEDIDO de buscar + ler no cartao desta placa; entra
        # uma vez por setor, independente do tamanho.
        BUSCA_MS = 12.4
        print(f"\n{'nivel':<9} {'km/setor':>9} {'cheios':>9} "
              f"{'p50':>8} {'p95':>8} {'p99':>8} {'pior':>9}   (ms de leitura)")
        print("-" * 74)
        for i, (z, xmin, ymin, w, hh, off) in enumerate(niveis):
            tam = []
            for s in range(off, off + w * hh):
                _, ln, _ = struct.unpack_from('<III', idx, s * 12)
                if ln:
                    tam.append(ln)
            if not tam:
                continue
            tam.sort()
            def ms(b):
                return BUSCA_MS + b / 1.8e6 * 1000.0
            p = lambda k: tam[min(len(tam) - 1, int(len(tam) * k))]
            larg = 156543.03392 * 256.0 / (1 << z) / 1000.0
            print(f"{i} z{z:<6} {larg:>9.1f} {len(tam):>9,} "
                  f"{ms(p(.50)):>7.0f} {ms(p(.95)):>7.0f} {ms(p(.99)):>7.0f} "
                  f"{ms(tam[-1]):>8.0f}")
        print("\np50 = metade dos setores le mais rapido que isso; pior = o unico "
              "setor mais pesado do pacote.")


# ============================================================ tolerancia
def grava_tol(caminho):
    """Recalcula os campos DERIVADOS do cabecalho de um pacote ja gerado:
    a tolerancia de cada nivel e o tamanho do maior bloco.

    Existe porque os dois campos nasceram depois de um pacote de 107 minutos
    ficar pronto. Sao metadado puro, moram no setor 0 e nao encostam em um byte
    de geometria - regerar o pacote inteiro por causa deles seria desperdicio.
    Confere o zoom antes de escrever: se a tabela de NIVEIS mudou, o pacote nao
    e este."""
    with open(caminho, 'r+b') as f:
        h = bytearray(f.read(SETOR))
        if h[0:8] != MAGIC:
            sys.exit("nao e um pacote MTSVECT")
        nn = h[10]
        if nn != len(NIVEIS):
            sys.exit(f"o pacote tem {nn} niveis e a tabela tem {len(NIVEIS)}")
        for i, nv in enumerate(NIVEIS):
            z = h[32 + i * 20]
            if z != nv['zoom']:
                sys.exit(f"nivel {i}: pacote em z{z}, tabela em z{nv['zoom']} - "
                         f"nao e o mesmo pacote")
            tol = min(int(round(nv['tol'])), 65535)
            struct.pack_into('<H', h, 32 + i * 20 + 2, tol)
            print(f"  nivel {i} (z{z}, {nv['nome']}): tolerancia {tol} m")

        # maior bloco: varre o indice inteiro (e barato, e o indice ja esta em
        # disco sequencial)
        isec, _, nslots = struct.unpack_from('<III', h, 12)
        f.seek(isec * SETOR)
        idx = f.read(((nslots * 12 + SETOR - 1) // SETOR) * SETOR)
        maior = 0
        for s in range(nslots):
            ln = struct.unpack_from('<I', idx, s * 12 + 4)[0]
            if ln > maior:
                maior = ln
        struct.pack_into('<I', h, 28, maior)
        print(f"  maior bloco: {maior/1024:.0f} KB "
              f"(e o que o firmware vai alocar por slot de cache)")

        f.seek(0)
        f.write(h)
    print("gravado.")


# ================================================================== main
def main():
    ap = argparse.ArgumentParser(description="OSM -> pacote vetorial do MTS")
    sub = ap.add_subparsers(dest='cmd', required=True)

    c = sub.add_parser('pack', help="converte um .osm.pbf")
    c.add_argument('--pbf', required=True)
    c.add_argument('--out', required=True)
    c.add_argument('--tmp', default='_tmp_vetor')
    c.add_argument('-v', '--verbose', action='store_true')

    c = sub.add_parser('info', help="descreve um pacote pronto")
    c.add_argument('--pack', required=True)

    c = sub.add_parser('tol', help="grava a tolerancia nos niveis de um pacote ja pronto")
    c.add_argument('--pack', required=True)

    a = ap.parse_args()
    if a.cmd == 'pack':
        converte(a.pbf, a.out, a.tmp, a.verbose)
    elif a.cmd == 'tol':
        grava_tol(a.pack)
    else:
        info(a.pack)


if __name__ == '__main__':
    main()
