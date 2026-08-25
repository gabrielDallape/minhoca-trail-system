# Baixa o modelo de elevacao (90 m) que cobre o Brasil, do viewfinderpanoramas.
#
# POR QUE ESTA FONTE, e nao a "oficial" da NASA: os .hgt aqui sao SRTM com os
# buracos ja preenchidos e correcoes de terceiros, vem em zip sem cadastro, e sao
# int16 big-endian cru de 1201x1201 - da para ler com struct, sem GDAL. As outras
# duas fontes testadas: a AWS skadi e 1 arcsec (30 m) e custaria 13 GB de download
# para um dado que a gente vai reamostrar para 90 m; a Copernicus e COG e exigiria
# GDAL so para abrir.
#
# Convencao do nome do bloco:
#   ao SUL do equador ... "S" + letra + zona   -> SF23
#   ao NORTE do equador ....... letra + zona   -> A18      (SEM prefixo!)
#   letra = faixa de 4 graus de latitude   (A = 0-4, B = 4-8, ... I = 32-36)
#   zona  = fuso de 6 graus de longitude   (zona n cobre -180+6(n-1) .. -180+6n)
# Cada bloco traz 24 tiles de 1 grau.
#
# A assimetria do prefixo nao e detalhe: gerar "NA18" faz o servidor devolver 404,
# o script anota "oceano puro" e o Brasil perde Roraima, Amapa e o norte do
# Amazonas e do Para - sem erro nenhum na tela. Foi o que aconteceu na primeira
# passada. Por isso o confere() abaixo existe.
import os, re, sys, urllib.request, concurrent.futures

DESTINO = sys.argv[1]
INDICE = 'http://viewfinderpanoramas.org/Coverage%20map%20viewfinderpanoramas_org3.htm'
UA = {'User-Agent': 'mts-tiles/0.1'}

# Brasil, com folga: da Ponta do Seixas (-34,79) ao Acre (-73,99),
# do monte Caburai (+5,27) ao arroio Chui (-33,75).
LAT_MIN, LAT_MAX = -34.0, 5.5
LON_MIN, LON_MAX = -74.5, -34.0

def zona(lon):
    return int((lon + 180) // 6) + 1

def blocos_desejados():
    r = []
    zonas = range(zona(LON_MIN), zona(LON_MAX) + 1)
    for z in zonas:
        # faixas ao sul
        for i in range(9):                       # A..I  -> 0..36 graus sul
            if -(4 * (i + 1)) < LAT_MAX and -(4 * i) > LAT_MIN:
                r.append(f"S{chr(65+i)}{z:02d}")
        # faixas ao norte: sem prefixo de hemisferio
        for i in range(2):                       # A..B  -> 0..8 graus norte
            if (4 * i) < LAT_MAX:
                r.append(f"{chr(65+i)}{z:02d}")
    return sorted(set(r))


# Pontos extremos do Brasil, um por sentido, mais o norte do Amazonas. Se o bloco
# que contem qualquer um deles nao entrar na lista, o mapa nasce com um buraco do
# tamanho de um estado e ninguem percebe ate olhar a tela em campo.
CONFERE = [
    ("monte Caburai (RR, ponto mais ao norte)",   5.27, -60.19),
    ("arroio Chui (RS, mais ao sul)",           -33.75, -53.37),
    ("Ponta do Seixas (PB, mais a leste)",       -7.15, -34.79),
    ("serra do Contamana (AC, mais a oeste)",    -7.54, -73.99),
    ("Macapa (AP, em cima do equador)",           0.03, -51.07),
    ("Manaus (AM)",                              -3.10, -60.02),
    ("Boa Vista (RR)",                            2.82, -60.67),
]


def bloco_de(lat, lon):
    z = zona(lon)
    i = int(abs(lat) // 4)
    return (f"S{chr(65+i)}{z:02d}" if lat < 0 else f"{chr(65+i)}{z:02d}")

# NAO use a pagina de cobertura para decidir o que existe. Ela esta incompleta:
# o bloco A21 (Roraima e norte do Para) NAO aparece nela e mesmo assim baixa,
# 33 MB. Confiar no indice abria um buraco de 6 x 4 graus no mapa sem avisar.
# Perguntar ao servidor custa 88 requisicoes de 1 byte e nao erra.
def existe(b):
    req = urllib.request.Request(f'http://viewfinderpanoramas.org/dem3/{b}.zip',
                                 headers={**UA, 'Range': 'bytes=0-0'})
    try:
        urllib.request.urlopen(req, timeout=30).read()
        return True
    except Exception:
        return False


print("perguntando ao servidor quais blocos existem de verdade...")
_cand = blocos_desejados()
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
    existem = {b for b, e in zip(_cand, ex.map(existe, _cand)) if e}
print(f"  {len(existem)} de {len(_cand)} candidatos respondem")

quero = blocos_desejados()
alvo = [b for b in quero if b in existem]
faltam = [b for b in quero if b not in existem]
print(f"  {len(quero)} blocos cobrem o Brasil; {len(alvo)} existem, "
      f"{len(faltam)} nao (oceano puro): {' '.join(faltam)}")

print("\nconferindo os extremos do pais:")
ruim = 0
for nome, la, lo in CONFERE:
    b = bloco_de(la, lo)
    ok = b in alvo
    if not ok:
        ruim += 1
    print(f"  {'OK ' if ok else 'FALTA'}  {b:<6} {nome}")
if ruim:
    sys.exit(f"\nPAROU: {ruim} ponto(s) do Brasil ficariam sem relevo.")

os.makedirs(DESTINO, exist_ok=True)

def baixa(b):
    d = os.path.join(DESTINO, b + '.zip')
    if os.path.exists(d) and os.path.getsize(d) > 1000:
        return b, os.path.getsize(d), 'ja tinha'
    req = urllib.request.Request(f'http://viewfinderpanoramas.org/dem3/{b}.zip', headers=UA)
    with urllib.request.urlopen(req, timeout=300) as r, open(d, 'wb') as f:
        while True:
            c = r.read(1 << 20)
            if not c:
                break
            f.write(c)
    return b, os.path.getsize(d), 'ok'

total = 0
# 4 de cada vez: o servidor e pequeno e nao vale a pena martelar
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
    for i, (b, n, s) in enumerate(ex.map(baixa, alvo), 1):
        total += n
        print(f"  [{i:>3}/{len(alvo)}] {b}  {n/1e6:>6.1f} MB  {s}   "
              f"(acumulado {total/1e9:.2f} GB)", flush=True)

print(f"\nfim: {len(alvo)} blocos, {total/1e9:.2f} GB em {DESTINO}")
