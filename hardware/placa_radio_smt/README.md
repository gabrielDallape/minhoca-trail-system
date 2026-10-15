# Placa do rádio v4: versão de fábrica (JLCPCB, SMT)

A mesma função da `placa_radio` v3 (o E22 atrás da tela de 7"), mas para **encomendar pronta**:
2 camadas, conectores de verdade e o step-down dentro da placa. **40 × 70 mm**, o mesmo espaço
da v3.

**O E22 não vem montado** (DNP): o footprint está na placa e você solda o módulo à mão. É a
peça mais cara, e os módulos já estão aqui.

## O que muda em relação à v3

| | v3 (laser) | v4 (fábrica) |
|---|---|---|
| Ligação com a tela | cabo PH cortado, fio soldado no pad | **J1/J2: JST-PH 12 vias**, cabo PH pronto |
| Alimentação | step-down solto, 5 V num pad | **J4: 12 V do carro** → fusível, diodo, TVS, buck AP63205 |
| GPS | fio soldado no pad do P3 | **J3: JST-PH 5 vias**, na ordem do NEO-7M (VCC GND TXD RXD PPS) |
| Antena | soldada no pad 21 | **no IPEX do próprio E22** (pad 21 fica livre) |
| Terra debaixo do E22 | face única | top inteiro em terra + 130 vias de costura, como a Ebyte pede |

## O circuito

**Entrada (J4, 12 V):** F1 (PTC 0,75 A) → D1 (SS34, protege contra inversão) → D2 (SMAJ18A).
- O TVS grampeia surtos do carro em ~29 V, abaixo dos 35 V absolutos do AP63205.
- O carro carregando (14,4 V) fica abaixo dos 18 V em que o TVS começa a conduzir.

**Buck (U2, AP63205WU, 5 V fixo, 2 A):** os valores são os da Tabela 2 do datasheet.
- L1 4,7 µH blindado, C4 10 µF/50 V na entrada, C7/C8 2 × 22 µF na saída, C6 100 nF no BST.
- O EN vai direto no VIN, como o datasheet permite.

**Do buck ao E22:**
- FB1 é uma ferrite de 600 Ω.
- Depois dela vem o banco do `ESTUDO_PECAS.md`, junto dos pinos 9/10: C1 220 µF eletrolítico **comum** (o ESR é o que amortece o surto), C2 10 µF e C3 100 nF.
- A nota AN2023005 da Ebyte sugere um LDO depois do DC-DC. De 5 V para 5 V não sobra folga para LDO, então a ferrite com o banco faz esse papel de filtro.

**R1 10 kΩ** segura o TXEN em 0 enquanto a tela liga.

**JP1 (jumper de cobre, já fechado)** liga o GND do P3, que é o terra do GPS, ao terra da placa.
- O `docs/MONTAGEM.md` mandava separar os dois terras no chicote. Lá o retorno de 600 mA do E22 voltava pela tela.
- Nesta placa o retorno vai direto para o 12 V e não passa pela tela. Por isso o jumper fica fechado.
- Se o PPS se mostrar sujo, corte o JP1 com estilete e compare antes e depois: é o teste que separa as duas hipóteses.

A lista completa de ligações, **lida da placa roteada**, está em `saida/fabrica/ligacoes.txt`.
Ela foi conferida pino a pino com o `docs/MONTAGEM.md`.

## Arquivos para a JLCPCB (`saida/fabrica/`)

| Arquivo | Onde vai |
|---|---|
| `GERBER_placa_radio_v4.zip` | "Add gerber file" |
| `BOM_placa_radio_v4.csv` | PCB Assembly → BOM |
| `CPL_placa_radio_v4.csv` | PCB Assembly → CPL (Pick & Place) |
| `ligacoes.txt` | conferência: cada rede e os pinos dela |
| `vista_cima.png`, `vista_baixo.png` | render da placa |

### Como pedir

1. **PCB:**
   - 2 camadas, 1,6 mm, 5 unidades, HASL sem chumbo;
   - cor de máscara à escolha;
   - "Remove Order Number": **Specify a location**. O lugar já está marcado no verso.
2. **PCB Assembly:**
   - **Economic**, Top side;
   - quantidade: **2 ou 5**. O mínimo da JLC é 2, e o custo fixo é o mesmo.
3. Suba o BOM e o CPL. O U1 (E22) e o JP1 não aparecem: é de propósito.
4. **Na pré-visualização, confira a rotação** destas peças:
   - U2 (pino 1 = bolinha);
   - D1, D2 (faixa = catodo, do lado de cima);
   - LED1;
   - C1 (+ para cima);
   - os 4 conectores.

   A JLC às vezes usa outro "zero" para o mesmo encapsulamento. Se alguma peça aparecer girada, corrija ali mesmo. O engenheiro da JLC também confere as peças polarizadas antes de montar.

### Quanto custa (estimativa de 2026-10-08, 5 placas, todas montadas)

Preços de peça e estoque lidos na API da JLC nesse dia. A taxa de montagem vem da página de
preços deles. **O imposto é a parte mais incerta.**

| Item | US$ |
|---|---|
| PCB 2 camadas 40 × 70, 5 un. | ~2–5 |
| Setup 8 + estêncil 1,5 | 9,5 |
| 8 peças "extended" × 3 (a JLC vem cobrando 1,50 em parte dos casos) | 12–24 |
| Peças: ~3,2 por placa × 5 | ~16 |
| Frete para o Brasil | ~15–25 |
| **Subtotal** | **~55–80** |

Com imposto de importação e ICMS, conte **~R$ 550–850 pelas 5**, ou **~R$ 110–170 por placa**.
Os E22 entram à parte.

As 8 peças "extended" são as que pagam taxa de setup: os 3 tipos de conector, U2, L1, C1, D2,
F1 e FB1. O resto é "basic" e não paga.

## Montagem depois que chegar

1. **Antes de soldar o E22:** ligue 12 V no J4 e meça **5,0 V** no pad do C1 (+). O LED verde acende.
2. Desligue. Solde o E22 com a **etiqueta para cima** e o **pino 1 no canto marcado** (embaixo, à direita).
   - Solde primeiro um pad de canto, confira o alinhamento e só então solde o resto.
3. **Antena no IPEX do módulo antes de energizar.**
4. **Cabo PH 12 vias:**
   - confira com o multímetro que o pino 1 da placa chega no pino 1 da tela (P1 → IO52);
   - existem cabos "mesmo sentido" e "invertidos", e a tela imprime os pinos na ordem inversa da numeração;
   - a pinagem inteira está gravada no verso da placa.
5. Primeiro boot a **10 dBm**.

## Como regenerar

```powershell
.\hardware\placa_radio_smt\gera.ps1
```

O fluxo roda no Docker (KiCad 9.0.9 e Freerouting 1.9; imagem em `docker/`):
1. `gera_pcb.py montar` posiciona as peças e liga as redes;
2. o Freerouting roteia;
3. `gera_pcb.py finalizar` enche o terra e costura;
4. o DRC roda;
5. Gerber, BOM, CPL e renders são exportados.

**Última execução: 0 violações, 0 pads desconectados.** `saida/placa.kicad_pcb` abre no KiCad
para ajuste à mão.

O footprint do E22 (`lib/`) é o oficial da LCSC (C411294), o mesmo que a v3 usou.

## O que não foi verificado

- **Nada foi fabricado ainda.** O DRC prova que a placa está desenhada direito; ele não prova que o circuito funciona.
- O buck segue o datasheet ao pé da letra, mas o layout dele foi espremido numa faixa de 9 mm. O laço quente (C4 → U2 → GND) ficou com ~3 mm. Se aparecer ruído no RF, é o primeiro suspeito.
- A rotação das peças no CPL é a do KiCad. Ver o passo 4 de "Como pedir".
- A altura: C1 tem 7,7 mm e os conectores verticais ~6 mm mais o cabo. Confira o vão atrás da tela.
