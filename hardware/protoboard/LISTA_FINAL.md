# Lista final — um MTS montado "perfeito" (por carro)

2026-10-13. Esta lista substitui a seção 8 do `README.md`. Ela já incorpora:
- a revisão adversária (`estudos/E_revisao_adversaria.md`), que corrigiu seis pontos do projeto;
- a pesquisa de compra (`estudos/D_compras_brasil.md`), com os links e preços item a item.

## A ideia em uma frase

**Rádio dentro de uma caixa de alumínio colada atrás da tela, antena e GPS no teto por cabo, filtro
em cada fio que entra na tela, e alimentação do carro por uma entrada só, com conversores de 60 V.**

### Por que esse desenho cobre todas as causas de domingo

| Causa possível | Força da hipótese | O que no desenho resolve |
|---|---|---|
| **RF da antena entrando na tela.** A 10–20 cm, ~11–22 V/m; o chicote de 15 cm é ≈ λ/2 em 915 MHz | **a mais forte**: explica os 10 de 10 reinícios com TX no ar e "bancada ok, carro não" | antena no teto; rádio na caixa de alumínio; filtros RC **do lado da tela**; ferrite no chicote e no USB; chicote curto |
| **Retorno de terra pelo chicote** | **fraca a 10 dBm** (4–12 mV, faltam 40× para dar efeito) [conta] | ponto estrela; fios de potência separados; resistores em série |
| **Ruído −87/−89 dBm entrando na antena** | **medida** | antena no teto, a metros da tela |
| **Mau contato (PH sem trava)** | **medido** (FaustoA, 10-06) | conectores com trava; o lado da tela soldado ou colado |
| **Pico do alternador ou da partida** | risco conhecido | entrada protegida e conversores de 60 V |

Um ponto continua **aberto**: 8 dos 18 saltos de relógio aconteceram **sem TX no ar**. Pode ser
um defeito do próprio chip P4 (rev 1.3). O teste T0 da seção 4 separa isso: tela sem rádio, 2 h
no carro.

---

## 1. Compra por carro

Preços aproximados, em R$.

### 1.1. Entrada de 12 V

| # | Peça | Especificação exata | Onde e como buscar | Qtd | R$ |
|---|---|---|---|---|---|
| 1 | Derivador de fusível | "add-a-circuit"/"fuse tap" **no padrão do seu carro** (mini, ATO ou micro2), num circuito **ACC** (que desliga com a chave) | ML / Ali "fuse tap mini" | 1 | 15 |
| 2 | Fusível de lâmina | **2 A** + 1 reserva | autopeças | 2 | 5 |
| 3 | TVS | **1.5KE33CA** (bidirecional, 1500 W) | Soldafria / Multcomercial | 2 | 3 |
| 4 | Diodo de polaridade | **SB560** (Schottky 60 V, 5 A). **1N5822 não serve** | ML | 2 | 4 |
| 5 | Indutor de entrada | toroide **22 µH, ≥ 3 A** | Ali "toroidal inductor 22uH 3A" | 1 | 5 |
| 6 | Capacitores de entrada | eletrolítico **comum** 100 µF / **63 V**, 105 °C (não polímero: o ESR amortece o LC) + cerâmicos 1 µF e 100 nF / 100 V | loja de componentes | 1+1+1 | 5 |
| 7 | Conector do 12 V | **Superseal 1.5 de 2 vias** (com trava e vedação). Borne de parafuso afrouxa na trilha | Ali "superseal 2 pin" | 1 par | 10 |

### 1.2. Conversores (os dois de 60 V)

| # | Peça | Especificação exata | Onde e como buscar | Qtd | R$ |
|---|---|---|---|---|---|
| 8 | Conversor do **rádio** | módulo **TPS54560** (entrada 4,5–60 V), ajustado em **5,05 V** | Ali "TPS54560 step down module" | 1 | 20 |
| 9 | Conversor da **tela** | **outro TPS54560**, ajustado em **5,10 V** | idem | 1 | 20 |
| 10 | Reserva | um TPS54560 a mais (para os dois carros) | idem | 0,5 | 10 |
| 11 | Ponta USB-C para a tela | cabo USB-C macho de **ponta solta** ("pigtail" USB-C de 2 fios, **AWG 20–22**), ≤ 40 cm, ou um USB-C 5 A cortado | ML "cabo usb c ponta solta 5A" | 1 | 20 |

**Por que não usar o conversor selado de "8–40 V":** o TVS deixa passar até ~45 V no pico, e ele
morre. Os dois conversores precisam aguentar 60 V.

**Ao receber:**
- **Confira os capacitores do módulo:** têm de ser ≥ 63 V. Teste com 30 V na bancada antes de
  ir para o carro.
- **Trave o trimpot depois de ajustar**, com esmalte ou epóxi. A vibração mexe nele.

### 1.3. Placa do rádio (na caixa de alumínio)

| # | Peça | Especificação exata | Onde e como buscar | Qtd | R$ |
|---|---|---|---|---|---|
| 12 | Base | placa **cobreada virgem** (fenolite ou fibra) recortada no tamanho da caixa: o cobre é o plano de terra | MakerHero / Baú | 1 | 15 |
| 13 | Filtro do 5 V | ferrite **BLM31PG601SN1L** (600 Ω, **1,5 A**): serve até `p14`. Acima disso, indutor blindado de **2,2 µH / 2 A** | LCSC C16902 | 2 | 2 |
| 14 | Eletrolítico nos pads | **Panasonic EEU-FR1C221** (220 µF / 16 V / 105 °C) **ou o seu 470 µF**, se for eletrolítico comum. A **≤ 15 mm** dos pads 9/10 | LCSC C264061 | 1 | 2 |
| 15 | Cerâmicos nos pads | **10 µF X7R 25 V** (Murata GRM31CR71E106KA12L) + **100 nF X7R 50 V**, a **≤ 5 mm** | LCSC / Multcomercial | 2+2 | 3 |
| 16 | Pull-downs | **10 kΩ** no TXEN e no RXEN, junto do E22 (os dois, não só o TXEN) | qualquer | 2 | 1 |
| 17 | Saída da antena | **RG316 soldado no pad 21** (malha no 22) até um **SMA fêmea de painel** na caixa. Alternativa: pigtail IPEX → SMA fêmea **RG178, 15 cm**. **Cuidado: muito pigtail "SMA" é RP-SMA**; o certo tem **furo** no centro e rosca por fora | Ali "IPEX to SMA female RG178" | 1 | 19 |
| 18 | Caixa | **alumínio extrudado** ~110×80×45, com prensa-cabo. A caixa vai ao GND num ponto só | Ali "aluminum enclosure 110x80" | 1 | 20 |
| 19 | Fixação do E22 | **RTV neutro** (de cura neutra, nunca acético) ou epóxi. Fita dupla-face solta a 70 °C | ferragem / ML | 1 tubo | 20 |
| 20 | Verniz | verniz acrílico para placa (pó e umidade da trilha) | ML "verniz acrilico placa" | 1 | 30 |

### 1.4. Fios e conectores entre tela e rádio

| # | Peça | Especificação exata | Onde e como buscar | Qtd | R$ |
|---|---|---|---|---|---|
| 21 | Chicote de sinal | **Molex Micro-Fit 3.0, 2×6 (12 vias)**, já crimpado, AWG 20, trava positiva. **O mais curto possível**: a caixa do rádio fica colada atrás da tela | Ali "micro-fit 3.0 12 pin cable 30cm" | 1 par | 28 |
| 22 | Potência do rádio | Micro-Fit 3.0 de 2 vias, com fio de silicone **AWG 20** trançado (+ e −), ≤ 30 cm | idem + ML "fio silicone 20 awg" | 1 par + 1 m de cada cor | 30 |
| 23 | Fio de 12 V | silicone **AWG 18** | ML | 2 m | 25 |
| 24 | Ferrite clamp | **NiZn para UHF** (material 61 ou 43, ex. Fair-Rite 0461164281), um no chicote e um no cabo USB-C, **junto da tela** | Mouser / DigiKey | 2 | 25 |
| 25 | Lado da tela | os cabos PH dos headers P1/P3 **soldados ou colados** no conector (o PH sem trava já soltou uma vez), ≤ 5 cm até a placa-filtro | — | — | — |

### 1.5. Placa-filtro, colada no conector da tela

**Esta é a correção mais importante da revisão:** o filtro tem de ficar **do lado da tela**. Do
lado do rádio, a RF captada pelo chicote entra na tela depois de passar por ele.

Montada numa placa perfurada ilhada pequena. Os capacitores são **SMD 0805 NP0/C0G**: o de perna
vira indutor em 915 MHz.

| Sinal | GPIO | Conector | Peça na placa-filtro |
|---|---|---|---|
| SCK, MOSI | 30, 29 | P3-4, P3-5 | **330 Ω** em série |
| NSS | 28 | P3-6 | **1 kΩ** em série (limita a corrente que alimenta o chip "por trás") |
| MISO | 31 | P3-3 | **330 Ω** em série |
| NRST | 51 | P1-2 | **1 kΩ** em série |
| TXEN, RXEN | 52, 5 | P1-1, P3-7 | **1 kΩ** em série (os 10 kΩ ficam na placa do rádio) |
| BUSY | 49 | P1-4 | **1 kΩ** + **100 pF NP0 ao GND**. **REGRA: RC ≤ 150 ns.** Nunca 1 nF nem 10 kΩ: a RadioLib lê o BUSY 1 µs depois do comando |
| DIO1 | 50 | P1-3 | **1 kΩ** + **100 pF NP0 ao GND** |
| GPS PPS | 2 | P3-10 | **1 kΩ** + **100 pF NP0 ao GND**. Hoje está sem filtro e sem pull, e uma rajada de interrupções nele dá exatamente o INT_WDT de domingo |
| GPS TXD → P4 | 4 | P3-8 | **1 kΩ** + **100 pF NP0** |
| P4 → GPS RXD | 3 | P3-9 | **1 kΩ** |
| 3V3 do GPS | — | P3-12 | **100 nF + 100 pF** ao GND, no conector |
| GND | — | P1-8, P1-11, P3-11 | os três ligados |

| # | Peça | Qtd | R$ |
|---|---|---|---|
| 26 | Resistores 1/4 W: **330 Ω** ×3, **1 kΩ** ×9 | 12 | 3 |
| 27 | Capacitores **100 pF 0805 NP0** ×6 e **100 nF 0805** ×2 | 8 | 3 |
| 28 | Placa perfurada ilhada pequena | 1 | 5 |
| 29 | *(opcional, recomendado)* **74LVC1G17** (Schmitt) no PPS e no DIO1, com adaptador SOT-23 | 2 | 10 |

### 1.6. Antena e GPS

| # | Peça | Especificação exata | Onde e como buscar | Qtd | R$ |
|---|---|---|---|---|---|
| 30 | Antena LoRa | **915 MHz, magnética, 3–5 dBi, SMA macho**. Compre **duas diferentes** e escolha no NanoVNA (SWR < 2 em 915, com a antena no teto): **Usinainfo AN5-915** (5 dBi, 3 m) e **2J Antennas 2J3915M-915** (3 dBi, VSWR 1,5, DigiKey). Evite 7 dBi ou mais e qualquer uma "38 dBi" | Usinainfo / DigiKey | 1 (+1 para teste) | 30 |
| 31 | Segurança da antena | cordinha ou presilha no rack/porta (a base magnética voa na trilha) | — | 1 | 5 |
| 32 | *(fase 2)* GPS no teto | módulo GPS com **u.FL** + **antena GPS ativa magnética**, a **≥ 30–50 cm** da antena LoRa. O NEO-7M mini com antena cerâmica dentro do carro funciona, mas é o próximo gargalo | ML "antena gps ativa magnetica sma" | 1 | 40 |

### 1.7. Tela

| # | Peça | Qtd | R$ |
|---|---|---|---|
| 33 | Suporte com ventosa ou parafusado (a tela P4 7B já tem caixa metálica) | 1 | 70 |

**Total por carro: ~R$ 480–520. Dois carros: ~R$ 1.000.** O pedido de passivos na LCSC (itens
13–15, para os dois carros mais reservas) custa ~US$ 5 de peças e US$ 10–20 de frete.

---

## 2. Bancada (compra uma vez)

| Instrumento | Para quê | R$ |
|---|---|---|
| **Carga SMA 50 Ω, 2 W** | testes T1 e T3 | 9 |
| **Atenuador SMA 30 dB, 2 W** | rádios na mesma mesa sem saturar | 15–40 |
| **NanoVNA-H4** | escolher a antena (SWR) e medir o cabo | 210 |
| **Medidor USB-C FNB58** (ou FNB48S) | consumo e tensão da tela | 80–170 |
| **Multímetro UNI-T UT139C** (tem MIN/MAX). O Aneng AN8008 **não tem** | tensões; o MIN/MAX não pega queda de ms | 200 |
| Termômetro IR GM320 | temperatura do E22, do conversor e da **tela ao sol** | 60 |
| *(desejável)* osciloscópio ≥ 20 MHz | ripple e quedas rápidas | — |

---

## 3. Ligação

```
CARRO
 fusível ACC ─ derivador ─ F 2A ─ Superseal ─┬─ TVS 1.5KE33CA ─ GND★
                                             └─ SB560 ─ L 22µH ─┬─ 100µF/63V ∥ 1µF ∥ 100nF ─ GND★
                                                                │
                        ┌───────────────────────────────────────┴────────────────────┐
                 TPS54560 (5,10 V) ─ USB-C ponta solta ─[ferrite]─ TELA P4    TPS54560 (5,05 V)
                                                                                     │ Micro-Fit 2 vias, AWG 20 trançado
                                                                                     ▼
 TELA P4 ─ P1/P3 (≤5 cm, soldado) ─ PLACA-FILTRO ─[ferrite]─ Micro-Fit 12 vias ─ CAIXA DE ALUMÍNIO
                                    (R série + 100 pF)        (curto)            ├ ferrite BLM31 ─ 220–470µF ∥ 10µF ∥ 100nF ─ E22 9/10
                                                                                 ├ 10k TXEN, 10k RXEN ao GND
                                                                                 ├ GND do E22 no plano de cobre
                                                                                 └ RG316 pad 21 ─ SMA painel ─ cabo ≤ 3 m ─ ANTENA NO TETO

★ UM ponto de terra só (na placa de entrada). Tela e rádio ligam juntos pelo ACC.
  O cabo USB-C da tela corre AMARRADO ao chicote: os dois fazem o mesmo caminho, sem abrir laço.
```

---

## 4. Testes, nesta ordem

Os critérios de aprovação estão no `README.md`, seção 7.

| # | Teste | Por quê |
|---|---|---|
| **T0** | **Tela sem rádio** (`MTS_TEM_RADIO 0`) por 2 h no carro, contando `relogioVoltou` | separa o defeito do chip P4 do resto. Se o relógio pular sem rádio nenhum, nada de montagem resolve sozinho |
| **T1** | **Varredura de ruído 902–928 MHz** com o próprio SX1262, com a tela ligada e desligada e o backlight em 0, 50 e 100 % | mostra se o ruído é uma espúria (o harmônico do DSI de 900 Mbps cai a 15 MHz do canal; mudar o clock ou o canal resolve) ou ruído largo (distância e blindagem resolvem) |
| **T2** | **Controle positivo:** reproduzir a falha na bancada com a geometria do carro (antena a 10–20 cm da tela, 14 dBm) | sem isso, "zero falhas" não prova nada: a bancada a 10 dBm já dava zero |
| **T3** | A mesma coisa com **atenuador + carga** pelo mesmo tempo | falha some = é RF (confirma o desenho); continua = é alimentação ou terra |
| **T4** | Montagem nova completa: 1 h, duas telas, 14 dBm | 0 reinício, 0 relógio voltando, entrega ≥ 97 % |
| **T5** | Carro parado, motor ligado, 15 min | ruído ≤ −110 dBm e 0 reinício |
| **T6** | Estrada, 100 m a 2 km (procedimento no README) | o alcance real |

T0 e T1 precisam de firmware: a chave do rádio já existe; a varredura de ruído eu escrevo.

---

## 5. O que muda no repo depois disto

- **`README.md`:** a hipótese principal da seção 1.2 passa a ser RF. O retorno de terra fica como
  secundária.
- **`hardware/placa_radio/`** (PCB): ganha a placa-filtro do lado da tela, o pull-down do RXEN, o
  filtro de ferrite e a caixa.
- **Firmware:**
  - pôr as linhas do rádio em entrada quando ele não responde;
  - descartar a amostra 0xFF do ruído;
  - separar "chip mudo" de "voltou de fábrica";
  - contar interrupções por segundo no PPS e no DIO1 na caixa-preta;
  - a varredura de ruído do T1;
  - o teste do clock do DSI (`EK7_LANE_MBPS` 1000 × 900).
