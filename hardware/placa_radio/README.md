# Placa do rádio — MTS E22 v2

Placa de face simples, **40 × 70 mm**, que vai colada atrás da tela de 7" (o espaço livre
lá é de 40 × 70; o máximo aceito era 45 × 70). Ela junta:

- o **E22-900M30S** (SMD, soldado do lado do cobre);
- **J2 → P1 da tela** e **J1 → P3 da tela**: JST-PH 2,0 mm, 12 vias, com a mesma
  numeração dos conectores da tela;
- **J4 → GPS**, por cabo: 2,54 mm, 5 vias;
- **J3 → entrada de 5,3 V** do step-down que já está no carro: JST-XH 2,54 mm, 2 vias;
- o desacoplamento do E22: C1 220 µF + C2 10 µF + C3 100 nF, encostados nos pads 9/10;
- pull-downs de 10 kΩ em **TXEN** e **RXEN**. Com a tela em reset ou no boot, os pinos
  dela ficam soltos, e TXEN e RXEN altos ao mesmo tempo danificam o módulo.

Tudo sai de um script só, `gera_placa.py`, que roda o DRC antes de gerar os arquivos:

```
python gera_placa.py
```

O DRC exige folga ≥ 0,4 mm entre redes, toda rede inteira (contando os jumpers) e cada
pino na rede certa. Ele também confere a **mecânica**: as carcaças reais dos conectores
(medidas do footprint oficial do KiCad) não podem encostar umas nas outras nem cobrir furo
de jumper, e nenhum nome gravado pode ficar debaixo de carcaça ou em cima de furo. A última execução mediu **folga mínima de 0,45 mm** e deu **DRC OK**.

## Arquivos (`saida/`)

| Arquivo | Para quê |
|---|---|
| `laser_queimar.png` / `.dxf` / `.svg` | **o que a laser queima** (preto = tira a tinta, o ácido come o cobre). PNG a 1200 dpi em escala 1:1 |
| `cobre.png` / `.svg` | o cobre que fica, só para conferir |
| `laser_serigrafia.png` / `.dxf` / `.svg` | **os nomes de cada fio**, gravados no lado SEM cobre (ver abaixo). Já vem espelhado, 40 × 70 exato |
| **`guia_1_conectores.png`** | **montagem do lado dos conectores**: onde vai cada peça, o pino 1, os jumpers JP2 e JP4 |
| **`guia_2_lado_do_cobre.png`** | **montagem do lado do cobre**: o E22 (e o pino 1 dele), C2, C3, R1, R2 e o fio do JP3 |
| **`guia_3_ligacoes.png`** | **o que liga em quê**: cada pino do P1/P3 da tela, o GPS fio por fio, o step-down, a antena |
| `conferencia_redes.png` | cada rede numa cor, para conferir contra o esquema |
| `furos.csv` | posição (vista pelo lado do cobre), broca e descrição dos 37 furos |
| `regua_100mm.png` | régua de 100 mm: grave antes e meça para conferir a escala da laser |

## Fabricação

1. **Confira a escala:** grave a `regua_100mm.png` num retalho e meça. Ela tem que dar
   100 mm. Se der diferente, o PNG está sendo importado com outro dpi (use 1200) ou use o DXF.
2. Pinte a placa crua. Ela precisa ter pelo menos **46 × 76 mm**: os arquivos já trazem
   3 mm de sobra de cada lado.
3. Na laser, coloque a placa com o **cobre para cima** e use o arquivo **sem espelhar**.
   O desenho já está visto pelo lado do cobre.
4. Corroa, tire a tinta e confira com lupa se nenhuma trilha ficou emendada, principalmente
   nas fileiras do J1/J2 (passo de 2 mm).
5. Corte a placa **em cima da linha de corte** gravada em volta (0,3 mm sem cobre).
6. Fure seguindo o `furos.csv`. Cada pad tem um furinho de 0,6 mm sem cobre no centro,
   que serve de guia para a broca:
   - **0,8 mm**: J1, J2 e os jumpers JP2 e JP4. O JP3 não tem furo;
   - **0,9 mm**: J4 (GPS) e C1;
   - **1,0 mm**: J3.

## Gravar os nomes (lado sem cobre)

Depois de furar e cortar em 40 × 70, vire a placa e grave a `laser_serigrafia` no lado
sem cobre. É ali que os cabos encaixam, e cada pino fica com o nome do fio do lado.
No cobre os nomes não cabem: o passo do conector é 2 mm, e o ácido come letra de cobre
menor que ~1,2 mm.

- O arquivo já está espelhado e tem o tamanho exato da placa. Encoste a placa no canto
  da laser e confira com a função de enquadrar (frame) antes de gravar.
- Dá para gravar direto na fibra, que escurece. Para mais contraste, pinte esse lado: a
  laser tira a tinta nas letras e a tinta fica como fundo.
- O ▶ ao lado do J1 e do J2 marca o **pino 1**.
- Os nomes estão em pé: vire a cabeça para a esquerda para ler.

## Montagem

Siga os guias: `guia_2_lado_do_cobre.png` primeiro, depois `guia_1_conectores.png`.

**Lado do cobre:** o **E22**, virado com a etiqueta para fora e o **pino 1 no canto
marcado** no guia. Os pads foram alongados 0,5 mm para fora para o ferro alcançar o
meio-furo do módulo. Solde primeiro um pad de canto, alinhe o módulo e depois solde os
outros. O **C2**, o **C3**, o **R1** e o **R2** (1206) também ficam deste lado.

**Lado dos componentes (sem cobre):** J1, J2, J3, J4 e C1, soldados pelo lado do cobre.

**3 jumpers** de fio fino encapado:

| Jumper | Rede | Lado | Observação |
|---|---|---|---|
| JP2 | BUSY | conectores | passa por cima do DIO1 |
| JP3 | SCK | **cobre** | soldado **deitado** nos dois pads, que não têm furo. Um furo cairia debaixo da carcaça do J1 |
| JP4 | GND | conectores | fio curto: junta os dois lados do plano de terra. **Sem ele a placa não funciona** |

O TXEN não tem mais jumper: a trilha contorna a placa pela borda. Na versão anterior o
jumper passava por cima do J2 e o cabo do P1 não encaixaria.

O C1 (eletrolítico de 6,3 mm) é a peça mais alta. Se faltar altura atrás da tela, deite
o capacitor antes de soldar.

## Lista de material

| Ref | Peça | Qtd |
|---|---|---|
| U1 | E22-900M30S | 1 |
| J1, J2 | JST-PH 2,0 mm, 12 vias, THT, vertical | 2 |
| J3 | JST-XH 2,54 mm, 2 vias, THT | 1 |
| J4 | barra de pinos 2,54 mm, 5 vias (ou XH 5 vias) | 1 |
| C1 | 220 µF / 10 V eletrolítico, Ø 6,3 mm, passo 2,5 mm | 1 |
| C2 | 10 µF cerâmico 1206 | 1 |
| C3 | 100 nF cerâmico 1206 | 1 |
| R1, R2 | 10 kΩ 1206 | 2 |
| — | pigtail IPEX → SMA + antena 915 MHz | 1 |
| — | 2 cabos PH2.0 12 vias "mesmo sentido", cabo para o GPS, fio para os jumpers | — |

## Cabos

- **J2 ↔ P1** e **J1 ↔ P3**: cabo PH2.0 de 12 vias, **pino 1 com pino 1**. Antes de
  ligar, confira com o multímetro em continuidade. Um cabo "invertido" leva o 3,3 V da tela
  (P3 pino 12) ao pino 1 da placa e troca todos os sinais de lugar.
- **GPS (J4)**: cada fio vai no **nome igual** gravado na placa (TXD, RXD, PPS, GND,
  VCC: os mesmos nomes impressos no GPS). A ordem da placa **não** é a do GPS, então um
  cabo de 5 vias reto não serve: ligue fio por fio. O TXD do GPS entra no IO4 da tela
  pelo J1 pino 8, e o PPS vai no pino 10.
- **J3**: + e − estão gravados no cobre. O XH tem trava e não entra invertido.

## Antes de energizar (multímetro)

1. **5V × GND** sem nada ligado: não pode dar curto. O valor sobe devagar enquanto o C1
   carrega.
2. **3V3 × GND** (J1 pino 12): sem curto.
3. Continuidade de cada pino do J1/J2 até o pad certo do E22, conferindo pela
   `conferencia_redes.png`. Nos pinos 1/2 do J1, 5–7/9/10/12 do J2: nada ligado.
4. TXEN (J2 pino 1) e RXEN (J1 pino 7) até o GND: **~10 kΩ**, que são os pull-downs.
5. **Ligue a antena antes de dar 5 V.**
6. Primeiro boot a **10 dBm** (14 dBm resetou a FaustoA).
