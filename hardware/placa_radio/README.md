# Placa do rádio — MTS E22 v3

Placa de face simples, **40 × 70 mm**, **sem furo e sem conector**. O lado da fibra fica
**colado na traseira da tela** de 7"; tudo é soldado por cima, no cobre:

- o **E22-900M30S**, com a etiqueta para cima;
- os **fios dos cabos do P1 e do P3 da tela**, cortados, cada um no pad com o número do pino
  gravado ao lado (só os fios usados: 5 do P1 e 10 do P3);
- os **fios do GPS**, no **mesmo pad** do fio do P3 que vai para o mesmo pino da tela;
- a **alimentação** do step-down (+5V e GND), em pads maiores;
- a **antena**, soldada no pad 21 do E22 (o IPEX fica vazio);
- **C1** 220 µF e **C2** 10 µF junto dos pads 9/10 do E22;
- **R1**, 10 kΩ, segurando o TXEN em 0 enquanto a tela liga.

**Só 3 peças além do E22**, depois de dois estudos (`ESTUDO_PECAS.md`). O C3 (100 nF) e o R2
(pull-down do RXEN) saíram, e as que ficaram ganharam pads maiores e espaço para o ferro.

**O manual completo está em `MONTAGEM_placa_radio.pdf`**: lista de peças com o que é cada
uma, fabricação na laser, onde vai cada peça, ordem de solda, ligações e testes.

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
| **`MONTAGEM_placa_radio.pdf`** | **o manual de montagem**, tudo num arquivo só |
| `laser_queimar_bmp.bmp` / `.png` / `.dxf` / `.svg` | **o que a laser queima** (preto = tira a tinta, o ácido come o cobre). 46 × 76 mm, PNG a 1200 dpi |
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
3. **C1, C2, R1** são peças **comuns, de perna**. Sem furo, cada perna é dobrada em "L" com
   o alicate, cortada a ~2 mm da dobra, e o pé é soldado em cima do pad.
   - **R1** (1/4 W) vai deitado ao longo da borda direita, uma perna em cada pad (~10 mm).
   - **C2** (cerâmico "106") vai em pé, colado nos pinos 10 e 11 do E22.
   - **C1** vai em pé, com a **faixa (−) para a esquerda**, ao lado do "−" gravado. Se faltar
     altura atrás da tela, deite a lata e dobre as pernas até os mesmos pads.
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
| C1 | capacitor eletrolítico **comum, de perna**, 220 µF / **16 V**, lata Ø 6,3 mm, 105 °C (ex.: Panasonic EEU-FR1C221). **Não troque por polímero** | 1 |
| C2 | capacitor **cerâmico multicamada de perna** 10 µF, marcado "106", 16 V ou mais. Não use de 6,3 V | 1 |
| R1 | resistor **comum** 10 kΩ, 1/4 W (marrom, preto, laranja) | 1 |
| — | antena 915 MHz com cabo coaxial (soldado nos pads 21/22) | 1 |
| — | 2 cabos PH2.0 de 12 vias (um para o P1, outro para o P3), cortados | 2 |
| — | fio AWG 22 para a alimentação, cola quente | — |

## Antes de energizar (multímetro)

1. **+5V × GND**, sem nada ligado: não pode dar curto. O valor sobe devagar enquanto o C1
   carrega.
2. **"12 3V3" × GND:** sem curto.
3. Continuidade de cada pad de fio até o pad certo do E22, conferindo pela
   `conferencia_redes.png`.
4. **"1 TXEN" até o GND: ~10 kΩ** (é o R1).
5. **Step-down em 5,0–5,1 V**, medido antes de ligar na placa. O E22 queima acima de 5,5 V.
6. **Ligue a antena antes de dar 5 V.**
7. Primeiro boot a **10 dBm** (14 dBm resetou a FaustoA).
