# Placa do rádio — MTS E22 v3

Placa de face simples, **40 × 70 mm**, **sem furo e sem conector**. O lado da fibra fica
**colado na traseira da tela** de 7"; tudo é soldado por cima, no cobre:

- o **E22-900M30S**, com a etiqueta para cima;
- os **fios dos cabos do P1 e do P3 da tela**, cortados, cada um no pad com o número do pino
  gravado ao lado (só os fios usados: 5 do P1 e 10 do P3);
- os **fios do GPS**, no **mesmo pad** do fio do P3 que vai para o mesmo pino da tela;
- a **alimentação** do step-down (+5V e GND), em pads maiores;
- a **antena**, soldada no pad 21 do E22 (o IPEX fica vazio);
- **C1** 220 µF + **C2** 10 µF + **C3** 100 nF junto dos pads 9/10 do E22;
- **R1** e **R2**, 10 kΩ, segurando o TXEN e o RXEN em 0 enquanto a tela liga.

Sem conector, os pads ficam na ordem dos pinos do E22: **nenhuma trilha cruza outra e não há
jumper**. O porquê de cada peça está em **`ESTUDO_PECAS.md`**.

Tudo sai de um script só, `gera_placa.py`, que roda o DRC antes de gerar os arquivos:

```
python gera_placa.py
```

O DRC exige folga ≥ 0,4 mm entre redes, toda rede inteira e cada pino na rede certa.
Ele também confere:
- que o número gravado em cada pad é o pino certo da tela (`pinos.h` / `MONTAGEM.md`);
- que nenhum nome encosta numa trilha;
- que nenhuma peça encosta em outra.

A última execução mediu **folga mínima de 0,50 mm** e deu **DRC OK**.

## Arquivos (`saida/`; o que vai para a laser também está em `final/`)

| Arquivo | Para quê |
|---|---|
| `laser_queimar.png` / `.dxf` / `.svg` | **o que a laser queima** (preto = tira a tinta, o ácido come o cobre). 46 × 76 mm, PNG a 1200 dpi |
| **`guia_1_montagem.png`** | **a placa montada**: cada peça e cada fio no lugar |
| **`guia_2_ligacoes.png`** | **o que liga em quê**: pino da tela → pad, GPS, step-down, antena |
| `guia_3_vao_e22.png` | os vãos debaixo do E22, ampliados |
| `cobre.png` / `.svg` | o cobre que fica, só para conferir |
| `conferencia_redes.png` | cada rede numa cor |
| `regua_100mm.png` | régua de 100 mm: grave antes e meça para conferir a escala da laser |

## Fabricação

1. **Confira a escala:** grave a `regua_100mm` num retalho e meça. Ela tem que dar 100 mm.
2. Pinte a placa crua. Ela precisa ter pelo menos **46 × 76 mm**: o arquivo tem 3 mm de
   sobra de cada lado.
3. Na laser: **cobre para cima, sem espelhar**. No DXF, ponha a camada preta em **Fill**
   (preenchimento) e **desligue o retângulo vermelho**. Senão a laser queima tudo.
4. Corroa, tire a tinta e confira com lupa se nenhuma trilha ficou emendada.
5. Corte **por fora** da linha de corte gravada e lixe até ela. As trilhas da borda (TXEN à
   direita, NRST/MISO à esquerda) ficam a ~0,6 mm da borda: um corte para dentro rompe o TXEN.

Os nomes já saem gravados no cobre: a letra é queimada e aparece na cor da fibra.

## Montagem (tudo do lado do cobre)

1. **Olhe o fundo do E22.** A placa tem cobre de terra debaixo dele, como o manual da Ebyte
   pede. Se o fundo for todo verniz (verde ou preto, sem ponto dourado), siga. Se tiver ponto
   de metal exposto, pinte a área debaixo dele com a tinta da laser e deixe secar.
   - Debaixo do módulo, cada pad de sinal tem **1,2 mm** de vão até o terra: um E22 soldado um
     pouco torto não encosta (`guia_3_vao_e22.png`).
2. **E22:** etiqueta para cima, **pino 1 no canto marcado "1"** (embaixo, à direita).
   - Solde um pad de canto, confira o alinhamento e só então solde os outros.
   - Os pads foram alongados 0,5 mm para fora para o ferro alcançar o meio-furo.
3. **C1, C2, C3, R1, R2.**
   - **C1** é eletrolítico SMD, com a **faixa preta (−) para a esquerda**, ao lado do "−"
     gravado.
   - Os pads de terra têm um anel queimado com quatro pontes (alívio térmico): é o que deixa
     o pad visível e fácil de soldar.
4. **Fios da tela:** corte o cabo PH de 12 vias deixando o plugue do lado da tela.
   - Cada fio vai no pad com o **mesmo número** gravado (`guia_2_ligacoes.png`). Os que não
     são usados: corte e isole.
   - **Antes de soldar, confira cada fio com o multímetro**, da ponta cortada até o pino com o
     GPIO certo no conector da tela (o GPIO está impresso na placa da tela). Na tela, a ordem
     física impressa é o inverso da numeração dos pinos (`docs/MONTAGEM.md`).
5. **GPS:** cada fio do GPS solda no mesmo pad do fio do P3:
   - TXD → "8 TXD", RXD → "9 RXD", PPS → "10 PPS";
   - GND → "11 GND", VCC → "12 3V3".
6. **Alimentação:** + do step-down no pad "+5V", − no "GND", com fio AWG 22.
7. **Antena:** vivo do cabo no **pad 21** do E22, malha no **pad 22**.
   - As trilhas do SPI passam a menos de 1 mm à esquerda do pad 21: desencape só ~2 mm.
   - Não use o IPEX e o pad 21 ao mesmo tempo.
8. **Cola quente** em todos os fios, perto da placa: a solda não aguenta puxão.

## Lista de material

| Ref | Peça | Qtd |
|---|---|---|
| U1 | E22-900M30S | 1 |
| C1 | 220 µF / **16 V** eletrolítico **SMD**, lata Ø 6,3 mm, 105 °C (série Panasonic FK ou equivalente). Serve um de furo com as pernas dobradas e soldadas por cima. **Não troque por polímero** | 1 |
| C2 | 10 µF cerâmico 1206 **X7R, 16 ou 25 V** (Murata GRM31CR71C106KAC7L). Não use de 6,3 V | 1 |
| C3 | 100 nF cerâmico 1206 X7R, 25 V ou mais | 1 |
| R1, R2 | 10 kΩ 1206 | 2 |
| — | antena 915 MHz com cabo coaxial (soldado nos pads 21/22) | 1 |
| — | 2 cabos PH2.0 de 12 vias (um para o P1, outro para o P3), cortados | 2 |
| — | fio AWG 22 para a alimentação, cola quente | — |

## Antes de energizar (multímetro)

1. **+5V × GND**, sem nada ligado: não pode dar curto. O valor sobe devagar enquanto o C1
   carrega.
2. **"12 3V3" × GND:** sem curto.
3. Continuidade de cada pad de fio até o pad certo do E22, conferindo pela
   `conferencia_redes.png`.
4. **"1 TXEN" e "7 RXEN" até o GND: ~10 kΩ** (são o R1 e o R2).
5. **Step-down em 5,0–5,1 V**, medido antes de ligar na placa. O E22 queima acima de 5,5 V.
6. **Ligue a antena antes de dar 5 V.**
7. Primeiro boot a **10 dBm** (14 dBm resetou a FaustoA).
