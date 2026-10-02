"""MANUAL DE MONTAGEM em PDF da placa do radio - chamado pelo gera_placa.py.

Junta, num PDF so: o que e a placa, a lista de pecas (o que e cada uma e para que
serve), como fabricar na laser, onde vai cada peca, a ordem de solda, as ligacoes
e os testes antes de ligar. As figuras saem da MESMA geometria da laser.
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle

import guias

A4 = (11.69, 8.27)          # A4 deitado, em polegadas
TINTA = "#1d1d1d"
CINZA = "#555555"
DESTAQUE = "#a01010"


def _salva(pdf, fig):
    pdf.savefig(fig)
    # MTS_PREVIA=<pasta>: tambem salva cada pagina em PNG, para conferir sem leitor de PDF
    pasta = os.environ.get("MTS_PREVIA")
    if pasta:
        fig.savefig(os.path.join(pasta, f"manual_p{pdf.get_pagecount()}.png"), dpi=80)
    plt.close(fig)


def _pagina(pdf, titulo, num):
    fig = plt.figure(figsize=A4)
    fig.text(0.04, 0.955, titulo, fontsize=19, weight="bold", color=TINTA, va="top")
    fig.add_artist(plt.Line2D([0.04, 0.96], [0.905, 0.905], color="#c47a35", lw=2.5))
    fig.text(0.96, 0.025, f"Placa do rádio MTS v3 · página {num}", fontsize=8, color="#999999", ha="right")
    return fig


def _texto(fig, x, y, blocos, larg=0.42, fs=10.5):
    """blocos: [(subtitulo, [linhas])]. Devolve o y em que parou."""
    for sub, linhas in blocos:
        if sub:
            fig.text(x, y, sub, fontsize=fs + 2, weight="bold", color="#8f4f17", va="top")
            y -= 0.045
        for ln in linhas:
            cor = DESTAQUE if ln.startswith("!") else TINTA
            ln = ln.lstrip("!")
            fig.text(x + 0.012, y, ln, fontsize=fs, color=cor, va="top",
                     weight="bold" if cor == DESTAQUE else "normal", wrap=True)
            y -= 0.037 * (1 + ln.count("\n"))
        y -= 0.02
    return y


def _imagem(fig, caminho, ret):
    ax = fig.add_axes(ret)
    ax.imshow(plt.imread(caminho))
    ax.axis("off")
    return ax


# ---------------------------------------------------------------- icones das pecas
def _icone(ax, tipo):
    ax.set_xlim(0, 10); ax.set_ylim(0, 10); ax.set_aspect("equal"); ax.axis("off")
    if tipo == "E22":
        ax.add_patch(Rectangle((2.2, 0.6), 5.6, 8.8, fc="#1f6f4a", ec="#0f3b27", lw=1))
        ax.add_patch(Rectangle((2.7, 2.2), 4.6, 6.6, fc=guias.BLINDAGEM, ec="#6b7178", lw=0.8))
        for i in range(8):
            ax.add_patch(Rectangle((1.6, 1.0 + i * 1.0), 0.8, 0.5, fc=guias.SOLDA, lw=0))
            ax.add_patch(Rectangle((7.6, 1.0 + i * 1.0), 0.8, 0.5, fc=guias.SOLDA, lw=0))
        ax.text(5, 5.5, "E22", ha="center", va="center", fontsize=7, weight="bold", color="#30353b")
    elif tipo == "C1":
        ax.add_patch(Rectangle((1.5, 1.5), 7, 7, fc="#2b2b2b", ec="#111111"))
        ax.add_patch(Circle((5, 5), 3.2, fc="#c7ccd2", ec="#7d838a"))
        ax.add_patch(Rectangle((1.8, 1.8), 1.6, 6.4, fc="#1d1d1d", lw=0))
        ax.text(5.6, 5, "220µF", ha="center", va="center", fontsize=6, color="#30353b")
    elif tipo in ("C2", "R1"):
        corpo = "#c8a26a" if tipo == "C2" else "#1e1e1e"
        ax.add_patch(Rectangle((1.5, 3.4), 7, 3.2, fc=guias.SOLDA, ec=guias.SOLDA_BORDA))
        ax.add_patch(Rectangle((2.8, 3.4), 4.4, 3.2, fc=corpo, lw=0))
        if tipo == "R1":
            ax.text(5, 5, "103", ha="center", va="center", fontsize=7, color="white", weight="bold")
    elif tipo == "ANT":
        ax.plot([5, 5], [1, 7.5], color="#151515", lw=5, solid_capstyle="round")
        ax.add_patch(Rectangle((4.2, 0.4), 1.6, 1.4, fc="#d8c070", ec="#7a6a2a"))
        ax.plot([5, 5], [7.5, 9.6], color="#d9a441", lw=2)
    elif tipo == "CABO":
        ax.add_patch(Rectangle((0.8, 3.6), 3.2, 2.8, fc=guias.FR4, ec="#9e977d", lw=1))
        for i, c in enumerate(["#1f77b4", "#2ca02c", "#d62728", "#ff7f0e", "#9467bd"]):
            ax.plot([4.0, 9.5], [4.0 + i * 0.5, 4.0 + i * 0.5], color=c, lw=1.6)
    elif tipo == "FIO":
        ax.plot([1, 9], [6, 6], color="#d62728", lw=4, solid_capstyle="round")
        ax.plot([1, 9], [4, 4], color="#222222", lw=4, solid_capstyle="round")
    elif tipo == "COLA":
        ax.add_patch(Rectangle((1, 4), 6, 2, fc="#ededed", ec="#999999"))
        ax.add_patch(Rectangle((7, 4.4), 2, 1.2, fc="#d0d0d0", ec="#999999"))


PECAS = [
    ("E22", "U1", "E22-900M30S (Ebyte)", "O rádio LoRa de 1 W. Vai soldado deitado, etiqueta para cima,\ncom o pino 1 no canto marcado “1”.", "1"),
    ("C1", "C1", "Capacitor eletrolítico SMD 220 µF / 16 V,\nlata Ø 6,3 mm, 105 °C (série Panasonic FK ou similar)",
     "Reserva de energia: segura o pico de ~0,6 A da transmissão e o\npico de tensão ao plugar o 5 V. Tem polaridade: faixa preta = −.", "1"),
    ("C2", "C2", "Capacitor cerâmico 10 µF, 1206, X7R, 16 ou 25 V\n(ex.: Murata GRM31CR71C106KAC7L)",
     "Filtra as variações rápidas que o C1 é lento demais para pegar.\nO manual da Ebyte pede um cerâmico junto do módulo. Sem polaridade.", "1"),
    ("R1", "R1", "Resistor 10 kΩ, 1206 (marcado “103”)",
     "Segura o TXEN em 0 enquanto a tela liga. O TXEN liga o amplificador\nde 1 W, e esse pino não pode ficar solto. Sem polaridade.", "1"),
    ("ANT", "—", "Antena 915 MHz com cabo coaxial", "Soldada direto no E22: vivo no pad 21, malha no pad 22.\nO conector IPEX do módulo fica vazio.", "1"),
    ("CABO", "—", "Cabo JST-PH 2,0 mm de 12 vias (2 un.)", "Um para o P1 e outro para o P3 da tela. O plugue fica na tela;\na outra ponta é cortada e cada fio solda num pad da placa.", "2"),
    ("FIO", "—", "Fio AWG 22, vermelho e preto (~30 cm)", "Liga o step-down (5,0–5,1 V) aos pads +5V e GND.", "1"),
    ("COLA", "—", "Cola quente", "Prende os fios perto da placa: a solda não aguenta puxão.", "—"),
]


def _pag_lista(pdf, num):
    fig = _pagina(pdf, "1. Lista de peças: o que é cada uma e para que serve", num)
    y0, alt = 0.86, 0.098
    fig.text(0.13, y0 - 0.005, "Ref", fontsize=9, color=CINZA, weight="bold", va="top")
    fig.text(0.18, y0 - 0.005, "Peça (o que comprar)", fontsize=9, color=CINZA, weight="bold", va="top")
    fig.text(0.50, y0 - 0.005, "Para que serve", fontsize=9, color=CINZA, weight="bold", va="top")
    fig.text(0.93, y0 - 0.005, "Qtd", fontsize=9, color=CINZA, weight="bold", va="top")
    for i, (ic, ref, peca, serve, qtd) in enumerate(PECAS):
        y = y0 - 0.03 - i * alt
        if i % 2 == 0:
            fig.add_artist(Rectangle((0.04, y - alt + 0.012), 0.92, alt, transform=fig.transFigure,
                                     fc="#f6f1e2", ec="none", zorder=0))
        _icone(fig.add_axes([0.05, y - alt + 0.016, 0.07, alt - 0.008]), ic)
        fig.text(0.13, y - alt / 2 + 0.006, ref, fontsize=11, weight="bold", va="center")
        fig.text(0.18, y - alt / 2 + 0.006, peca, fontsize=9, va="center")
        fig.text(0.50, y - alt / 2 + 0.006, serve, fontsize=9, va="center", color="#333333")
        fig.text(0.935, y - alt / 2 + 0.006, qtd, fontsize=10, va="center")
    fig.text(0.04, 0.06, "Saíram nesta versão, depois do estudo (ESTUDO_PECAS.md): o C3 de 100 nF (redundante com o C2 e com\n"
                         "os cerâmicos de dentro do E22) e o R2 do RXEN (o R1 já cobre o caso perigoso).",
             fontsize=8.5, color=CINZA, va="top")
    _salva(pdf, fig)


def _pag_onde(pdf, g, num):
    """A placa com as pecas e um numero em cada uma; a legenda diz o que e."""
    fig = _pagina(pdf, "3. Onde vai cada peça", num)
    ax = fig.add_axes([0.02, 0.05, 0.45, 0.84])
    LARG, ALT = g["LARG"], g["ALT"]
    ax.set_xlim(-7, LARG + 8); ax.set_ylim(ALT + 3, -8); ax.set_aspect("equal"); ax.axis("off")
    guias._placa(ax, g)
    guias._e22(ax, g)
    guias._smd(ax, g)
    guias._antena(ax, g)
    for r, h, x, y, a, axx, ayy in g["rotulos"]:
        b = g["poe"](r, h, x, y, a, axx, ayy).bounds
        ax.text((b[0] + b[2]) / 2, (b[1] + b[3]) / 2, r, ha="center", va="center", fontsize=4.6,
                rotation=90 if a else 0, weight="bold", color="#5a4a1e", zorder=20)
    pads = g["E22_PAD"]
    c1 = guias._peca(g, "C1").centroid
    # 2, 3 e 4 ficam na margem direita, com uma linha ate a peca: em cima dela a tampariam
    for n, alvo, y in (("2", (c1.x + 2.5, c1.y), 9.0), ("3", (g["X_C2"] + 0.8, 18.1), 18.1),
                       ("4", (g["X_R1"] + 0.8, pads[7][1] + 4.1), pads[7][1] + 4.1)):
        ax.plot([alvo[0], LARG + 4.6], [alvo[1], y], color="#c0392b", lw=1.2, zorder=29)
    marcas = [("1", (g["E22X"], g["E22Y"] + 6)), ("2", (LARG + 4.6, 9.0)), ("3", (LARG + 4.6, 18.1)),
              ("4", (LARG + 4.6, pads[7][1] + 4.1)), ("5", (-3.8, pads[21][1] + 4.6)),
              ("6", (6.9, -4.5)), ("7", (24.0, -4.5)), ("8", (38.5, -4.5)), ("9", (14.0, ALT + 1.8))]
    for n, (x, y) in marcas:
        ax.add_patch(Circle((x, y), 1.55, fc="#c0392b", ec="white", lw=1.2, zorder=30))
        ax.text(x, y, n, ha="center", va="center", fontsize=9, weight="bold", color="white", zorder=31)
    leg = [("1", "E22-900M30S", "etiqueta para cima; o pino 1 vai no canto gravado “1”\n(embaixo, à direita). Solde um canto, alinhe, depois o resto."),
           ("2", "C1 220 µF (eletrolítico SMD)", "a faixa preta (−) fica para a ESQUERDA, do lado do “−” gravado."),
           ("3", "C2 10 µF (cerâmico 1206)", "em pé, entre o pino 11 (terra) e o ramal do pino 10 (5 V).\nNão tem lado."),
           ("4", "R1 10 kΩ (resistor 1206)", "em pé, na borda direita, entre o TXEN e o terra.\nNão tem lado."),
           ("5", "Antena", "vivo do cabo no pad 21 do E22, malha no pad 22.\nDesencape só ~2 mm; cola quente perto da solda."),
           ("6", "Fios do P1 da tela", "pads “2 NRST”, “4 BUSY”, “3 DIO1”, “8 GND”:\no número é o pino do P1."),
           ("7", "Alimentação", "+ do step-down no “+5V”, − no “GND” (fio AWG 22).\nStep-down ajustado em 5,0–5,1 V."),
           ("8", "TXEN (pino 1 do P1)", "no pad do canto, “1 TXEN”."),
           ("9", "Fios do P3 da tela + GPS", "“3 MISO” “5 MOSI” “4 SCK” “6 NSS” “11 GND” “7 RXEN” e\n“8 TXD” “9 RXD” “10 PPS” “12 3V3”, que recebem também o fio\ndo GPS com o mesmo nome (VCC do GPS → “12 3V3”).")]
    y = 0.86
    for n, nome, desc in leg:
        fig.add_artist(Circle((0.487, y - 0.013), 0.012, transform=fig.transFigure, fc="#c0392b", ec="none"))
        fig.text(0.487, y - 0.013, n, fontsize=9, weight="bold", color="white", ha="center", va="center")
        fig.text(0.51, y, nome, fontsize=11, weight="bold", va="top")
        fig.text(0.51, y - 0.03, desc, fontsize=9, va="top", color="#333333")
        y -= 0.06 + 0.026 * desc.count("\n") + 0.012
    _salva(pdf, fig)


def gera(g):
    S = g["SAIDA"]
    caminho = os.path.join(S, "MONTAGEM_placa_radio.pdf")
    with PdfPages(caminho) as pdf:
        # capa
        fig = plt.figure(figsize=A4)
        fig.text(0.05, 0.9, "Placa do rádio MTS — v3", fontsize=30, weight="bold", color=TINTA, va="top")
        fig.text(0.05, 0.82, "Manual de fabricação e montagem", fontsize=16, color="#8f4f17", va="top")
        _texto(fig, 0.05, 0.74, [
            ("O que é", ["Placa de 40 × 70 mm, face simples, colada atrás",
                         "da tela de 7\". Junta o rádio E22, a alimentação,",
                         "a antena e os fios da tela e do GPS.",
                         "Sem furo e sem conector: tudo é soldado por cima,",
                         "no cobre; o lado da fibra cola na tela."]),
            ("Neste manual", ["1. Lista de peças", "2. Fabricação na laser", "3. Onde vai cada peça",
                              "4. Ordem de montagem", "5. O que liga em quê", "6. Testes antes de ligar"]),
            ("Arquivo da laser", ["laser_queimar_bmp.bmp: 46 × 76 mm, 1200 dpi", "(ou laser_queimar.dxf)"]),
        ], fs=11)
        _imagem(fig, os.path.join(S, "guia_1_montagem.png"), [0.47, 0.06, 0.52, 0.80])
        _salva(pdf, fig)

        _pag_lista(pdf, 2)

        fig = _pagina(pdf, "2. Fabricação na laser", 3)
        _texto(fig, 0.05, 0.86, [
            ("Antes", ["Grave a regua_100mm_bmp.bmp num retalho e meça: tem que dar 100 mm.",
                       "Se der outra medida, o programa da laser leu o dpi errado: ajuste o",
                       "tamanho na mão (a placa é 46 × 76 mm) com a proporção travada."]),
            ("Gravar", ["1. Pinte a placa crua (mínimo 46 × 76 mm) do lado do cobre.",
                        "2. Laser com o COBRE PARA CIMA, arquivo SEM ESPELHAR.",
                        "   Preto = onde a laser queima a tinta e o ácido come o cobre.",
                        "3. No DXF: camada preta em modo FILL e retângulo vermelho DESLIGADO.",
                        "   No BMP não há nada a ajustar."]),
            ("Depois", ["4. Corroa, tire a tinta e confira com lupa se nenhuma trilha emendou.",
                        "5. Corte POR FORA da moldura gravada e lixe até ela.",
                        "!   Não corte para dentro: a trilha do TXEN corre a 0,6 mm da borda direita.",
                        "6. Os nomes já saem gravados: a letra aparece na cor da fibra."]),
        ], fs=10.5)
        _imagem(fig, os.path.join(S, "preview_laser_queimar_dxf.png"), [0.58, 0.05, 0.38, 0.84])
        _salva(pdf, fig)

        _pag_onde(pdf, g, 4)

        fig = _pagina(pdf, "4. Ordem de montagem", 5)
        _texto(fig, 0.05, 0.86, [
            ("Antes de soldar", ["!Olhe o fundo do E22. Debaixo dele a placa tem cobre de terra (o manual pede).",
                                 "Se o fundo for todo verniz, sem ponto dourado, siga. Se tiver metal exposto,",
                                 "pinte a área com a tinta da laser e deixe secar."]),
            ("Na ordem (do mais baixo para o mais alto)",
             ["☐ 1. E22: pino 1 no canto “1”. Solde UM pad de canto, confira o alinhamento,",
              "        depois os outros. Os pads de sinal têm 1,2 mm de folga até o terra.",
              "☐ 2. R1 (10 kΩ) e C2 (10 µF): estanhe um pad, encoste a peça, solde o outro.",
              "☐ 3. C1 (220 µF): faixa preta para a ESQUERDA.",
              "☐ 4. Fios da tela: corte o cabo PH deixando o plugue na tela. Confira cada fio",
              "        com o multímetro até o pino da tela ANTES de soldar (a ordem impressa",
              "        na tela é o inverso da numeração). Solde no pad com o mesmo número.",
              "        Os fios que não são usados: corte e isole.",
              "☐ 5. GPS: cada fio no pad do P3 com o mesmo nome (VCC → “12 3V3”).",
              "☐ 6. Alimentação: AWG 22, + no “+5V”, − no “GND”.",
              "☐ 7. Antena: vivo no pad 21, malha no 22.",
              "☐ 8. Cola quente em todos os fios, perto da placa.",
              "☐ 9. Fita dupla-face do lado da fibra e cole atrás da tela."]),
            ("Dica", ["Os pads de terra têm um anel queimado com quatro pontes: é para o calor",
                      "não fugir para o plano. Mesmo assim, segure o ferro um pouco mais neles."]),
        ], fs=10.2)
        _salva(pdf, fig)

        fig = _pagina(pdf, "5. O que liga em quê", 6)
        _imagem(fig, os.path.join(S, "guia_2_ligacoes.png"), [0.03, 0.04, 0.94, 0.85])
        _salva(pdf, fig)

        fig = _pagina(pdf, "6. Testes antes de ligar", 7)
        _texto(fig, 0.05, 0.86, [
            ("Com o multímetro, sem nada ligado",
             ["☐ +5V × GND: sem curto (o valor sobe devagar enquanto o C1 carrega).",
              "☐ “12 3V3” × GND: sem curto.",
              "☐ “1 TXEN” × GND: ~10 kΩ (é o R1).",
              "☐ Cada pad de fio até o pino certo do E22 (conferencia_redes.png)."]),
            ("Na hora de ligar",
             ["☐ Step-down medido em 5,0–5,1 V ANTES de ligar na placa.",
              "!☐ ANTENA LIGADA ANTES DO 5 V.",
              "☐ Primeiro boot a 10 dBm.",
              "☐ No serial: “radio: conferindo a fiacao...” sem aviso, e transmissões",
              "    com preso=0."]),
        ], fs=10.5)
        _texto(fig, 0.53, 0.86, [
            ("Se algo der errado (o que o serial diz)",
             ["“chip nao responde ao GetStatus”",
              "   → falta 5 V no E22, ou fio do NSS/MISO solto.",
              "“BUSY e DIO1 TROCADOS no chicote”",
              "   → os fios 3 e 4 do P1 estão invertidos.",
              "“begin() FALHOU (-707)” no boot",
              "   → tensão do TCXO (o firmware já usa 2,2 V).",
              "preso subindo junto com tx",
              "   → o 5 V afunda na transmissão: step-down,",
              "      fio fino ou C1 mal soldado.",
              "Shield do E22 esquentando parado",
              "   → TXEN preso em 1: confira o R1."]),
        ], fs=10)
        _salva(pdf, fig)
    return caminho
