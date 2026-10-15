#!/usr/bin/env python3
"""
Mede o contraste das cores do mapa, nos dois temas, direto do ui.h.

POR QUE MEDIR E NAO OLHAR
-------------------------
Contraste e a unica coisa da interface que NAO da para julgar no olho: o monitor
do PC e claro, tem brilho alto e esta a 60 cm; a tela do carro esta ao sol, suja,
e voce olha de relance com a mao no volante. O que "da para ver" na bancada some
na trilha.

Ja aconteceu neste projeto: o tema DIA tinha o contorno do traçado em BRANCO
(#FFFFFF) sobre o fundo bege #E3E1D2 - 1,32:1, praticamente invisivel. E o
contorno e o traço MAIS GROSSO (11 px contra 5 do nucleo), ou seja, dois tercos
do traçado nao apareciam. Ninguem tinha notado olhando.

CRITERIO: WCAG 2.1 pede 3:1 para elemento grafico nao-textual. Aqui e piso, nao
meta - a tela e usada ao sol.

    python contraste.py
"""
import os
import re
import sys

UI = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                  '..', '..', 'firmware', 'mts_p4', 'ui.h')

MINIMO = 3.0


def rgb565(v):
    r = (v >> 11) & 0x1F
    g = (v >> 5) & 0x3F
    b = v & 0x1F
    return ((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31)


def lum(c):
    def f(x):
        x /= 255.0
        return x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4
    r, g, b = c
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def contraste(a, b):
    la, lb = lum(a), lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def le_temas():
    src = open(UI, encoding='utf-8', errors='replace').read()
    i = src.index('inline void aplicaTema')
    corpo = src[i:src.index('\n}\n', i)]
    # o corpo tem dois blocos: DIA antes do 'else', NOITE depois
    j = corpo.index('} else {')
    blocos = {'DIA': corpo[:j], 'NOITE': corpo[j:]}
    fixos = {m.group(1): int(m.group(2), 16)
             for m in re.finditer(r'#define\s+(C_\w+)\s+0x([0-9A-Fa-f]{4})', src)}
    out = {}
    for nome, b in blocos.items():
        c = dict(fixos)
        # atribuicao por literal
        for m in re.finditer(r'(C_\w+)\s*=\s*0x([0-9A-Fa-f]{4})', b):
            c[m.group(1)] = int(m.group(2), 16)
        # atribuicao por OUTRA cor (C_ROTA_C = C_CASING). Vale a pena aceitar:
        # escrever o nome em vez do numero e o que registra a INTENCAO - "o mesmo
        # preto dos marcadores" - e impede as duas se separarem numa edicao futura.
        for _ in range(4):                     # resolve cadeias curtas
            mudou = False
            for m in re.finditer(r'(C_\w+)\s*=\s*(C_\w+)\s*;', b):
                alvo, fonte = m.group(1), m.group(2)
                if fonte in c and c.get(alvo) != c[fonte]:
                    c[alvo] = c[fonte]
                    mudou = True
            if not mudou:
                break
        out[nome] = c
    return out


# Cores de traço unico: contrastam com o fundo, e ponto.
SOZINHAS = [
    ('C_VAO',  'ponte tracejada onde o sinal caiu'),
    ('C_RED',  'alerta'),
    ('C_INK',  'texto principal'),
    ('C_INK2', 'texto secundario'),
    ('C_INK3', 'texto terciario'),
    ('C_SUN',  'estrada asfaltada / destaque'),
    ('C_TAN',  'nome do grupo'),
    ('C_OK',   'area protegida / combustivel'),
    ('C_WARN', 'porteira, vau, mata-burro'),
]

# Feicoes de DOIS TRAÇOS (contorno grosso + nucleo fino). A regra aqui NAO e
# "cada cor contra o fundo" - foi assim que a primeira versao deste script
# reprovou o tema noite inteiro por engano.
#
# Os dois temas funcionam por mecanismos OPOSTOS, e os dois estao certos:
#   DIA .... o ESCURO carrega o contraste (contorno preto sobre bege claro)
#   NOITE .. o CLARO carrega (nucleo brilhante sobre fundo quase preto)
# Entao a regra e:
#   a) pelo menos UM dos dois traços tem 3:1 contra o fundo - a feicao se separa
#      do chao;
#   b) o nucleo tem 3:1 contra o proprio contorno - da para ver que sao dois, e a
#      cor do nucleo (que e a IDENTIDADE: roxo = falta andar, azul = ja andei)
#      nao se perde dentro do contorno.
DUPLAS = [
    ('C_ROTA',   'C_ROTA_C',   'trajeto que falta andar'),
    ('C_RASTRO', 'C_RASTRO_C', 'trajeto ja percorrido'),
]


def main():
    temas = le_temas()
    src = open(UI, encoding='utf-8', errors='replace').read()
    m = re.search(r'CORES_MAPA\[\]\s*=\s*\{(.*?)\};', src, re.S)
    carros = [int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{4})', m.group(1))]

    ruins = 0
    hexa = lambda v: '#%02X%02X%02X' % rgb565(v)

    for tema, c in temas.items():
        bg = rgb565(c['C_BG'])
        print(f"\n=== {tema} (fundo {hexa(c['C_BG'])}) ===")
        print(f"{'cor':<12} {'valor':<9} {'x fundo':>9}   para que")
        print("-" * 70)
        for nome, para in SOZINHAS:
            if nome not in c:
                continue
            r = contraste(rgb565(c[nome]), bg)
            if r < MINIMO:
                ruins += 1
            print(f"{nome:<12} {hexa(c[nome]):<9} {r:>6.2f}:1 "
                  f"{'ok ' if r >= MINIMO else 'RUIM'}  {para}")

        print(f"\n{'feicao':<12} {'nucleo':<9} {'contorno':<9} "
              f"{'x fundo':>9} {'nuc x cont':>11}   veredito")
        print("-" * 70)
        for nuc, cont, para in DUPLAS:
            cn, cc = rgb565(c[nuc]), rgb565(c[cont])
            rn, rc = contraste(cn, bg), contraste(cc, bg)
            rr = contraste(cn, cc)
            separa = max(rn, rc) >= MINIMO          # (a)
            distingue = rr >= MINIMO                # (b)
            bom = separa and distingue
            if not bom:
                ruins += 1
            porque = ('' if bom else
                      (' <- nenhum dos dois se separa do fundo' if not separa
                       else ' <- nucleo some dentro do contorno'))
            print(f"{para[:12]:<12} {hexa(c[nuc]):<9} {hexa(c[cont]):<9} "
                  f"{max(rn, rc):>6.2f}:1 {rr:>10.2f}:1   "
                  f"{'ok' if bom else 'RUIM'}{porque}")

        # CARROS: nao se mede contra o fundo. Cada carro e um disco preenchido com
        # ANEL de C_CASING em volta - foi assim que a paleta Tol foi escolhida. O
        # que precisa contrastar e (anel x fundo) e (cor do carro x anel).
        anel = rgb565(c['C_CASING'])
        r_anel = contraste(anel, bg)
        pior = min(contraste(rgb565(v), anel) for v in carros)
        # se o anel nao se separa do fundo, quem tem de se separar e a cor cheia
        pior_fundo = min(contraste(rgb565(v), bg) for v in carros)
        bom = (max(r_anel, pior_fundo) >= MINIMO) and pior >= MINIMO
        if not bom:
            ruins += 1
        print(f"\ncarros: anel {hexa(c['C_CASING'])} x fundo {r_anel:.2f}:1 | "
              f"pior das 8 cores x anel {pior:.2f}:1 | x fundo {pior_fundo:.2f}:1"
              f"   {'ok' if bom else 'RUIM'}")

    print(f"\n{ruins} problema(s) de contraste")
    sys.exit(1 if ruins else 0)


if __name__ == '__main__':
    main()
