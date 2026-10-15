# Pesquisa B — alimentação, terra e montagem do E22 + tela P4 no carro

2026-10-13. Escopo: as 6 perguntas do pedido, a protoboard "do jeito perfeito" e a PCB que vem
depois. Não repito o que já está em `ESTUDO_PECAS.md`, `PLANO_7B.md`, `ESTUDO_E22.md` e
`placa_radio_smt/README.md`. Aqui aprofundo e corrijo o que está errado.

Procedência de cada número:
- **[M]**: datasheet, norma ou manual lido (nesta pesquisa ou citado no repo com a fonte);
- **[R]**: medido por nós, registrado no repo (CHANGELOG ou memória);
- **[C]**: conta feita aqui a partir de [M];
- **[E]**: estimativa ou hipótese, a confirmar com a medição da seção 5.

---

## 0. Antes de tudo: o aviso "CONFERIR O 5V DO RADIO" não prova que foi o 5 V do rádio

`firmware/mts/app/boot.h:96` acende o aviso depois de `RADIO_SEGURO_APOS = 3` reinícios
"violentos" seguidos. A classificação `caixaMotivoViolento()` (`hal/caixa.h:128`) entra com
**`ESP_RST_BROWNOUT` e `ESP_RST_PWR_GLITCH`**, além de PANIC e WDT. Só um POWERON zera o
contador. [R, código lido]

O brownout é **da própria tela**: o 3,3 V do ESP32-P4 afundou. Ele não vem do E22. Logo, um
**adaptador de isqueiro com mau contato** já dá o aviso, mesmo com o rádio perfeito. Um corte
curto no 5 V do USB derruba o 3,3 V sem zerar a RAM RTC, e o contador sobe a cada solavanco.
Na estrada de terra, mola do isqueiro mais vibração é exatamente isso [E].

**Ação de custo zero:** ler o motivo de cada boot no cartão SD (`appRelataCaixa` imprime o
motivo e a etapa da caixa-preta).

| O que aparece | O que significa |
|---|---|
| `BROWNOUT` / `PWR_GLITCH` | alimentação **da tela** (isqueiro, cabo USB) |
| `TASK_WDT` com a etapa `CP_TX_*` | **rádio** (TX preso; aí sim pode ser o 5 V do E22) |
| `PANIC` | software, ou RF entrando na tela (ver 4.6) |

O texto do aviso deveria dizer "conferir alimentação". Fica como sugestão: não mexi no
firmware.

---

## 1. Topologia de alimentação e terra

### 1.1. O que temos hoje (pela descrição)

```
bateria 12V ─┬─ isqueiro ─ adaptador USB ─ cabo USB ─ TELA ─┐
             │                                              │ GND pelos fios PH (P1/P3)
             └─ step-down 12→5 ──── +5V / GND ───── E22 ────┘
                    (GND da saída = GND da entrada: buck não isolado)
```

São **duas** ligações independentes ao terra do carro: o isqueiro e a entrada do step-down.
Mais a terceira, entre elas, pelos fios GND do cabo PH. Isso fecha um **laço de terra**.
Qualquer diferença de potencial entre o terra do isqueiro e o terra do step-down empurra
corrente pelos fios GND de 28 AWG do PH e pela placa da tela:
- o motor de partida puxando 100–200 A pela cordoalha;
- o alternador;
- o ventilador.

**Conta [C]:** cordoalha motor–chassi de ~2 mΩ × 150 A na partida = **0,3 V** entre dois
pontos de "terra". O laço tem 0,2–0,5 Ω:
- cabo USB de 28 AWG, 1 m: 213 mΩ;
- PH: ~16 mΩ;
- fiação do carro.

Resultado: **0,6–1,5 A circulando pelos fios de sinal** enquanto o motor de partida gira. E
centenas de mV de "terra" diferente entre a tela e o rádio [E na amplitude, M no mecanismo].

### 1.2. Topologia recomendada: uma entrada, um ponto estrela, laço do PA local

```
BATERIA/ACC 12V
   │ (derivador de fusível num circuito ACC, ou direto da bateria com relé ACC)
   ├─[F1 fusível 2 A lâmina]─[D1 TVS 33 V bidirecional]─[D2 Schottky 60 V/5 A]─[L1 10–22 µH + C 100 µF/50 V]─┐
   │                                                                                                         │ 12 V protegido
   │                         ┌──────────────────────────────┬──────────────────────────────────────────────────┘
   │                         │                              │
   │                   [BUCK A 5 V/3 A]               [BUCK B 5,05 V/≥2 A]
   │                   (tela)                          (rádio)
   │                     │   │                            │    │
   │              USB-C ≤50 cm   GND                 +5V (AWG 20, ≤30 cm, trançado)  GND
   │                 │           │                        │    │
   │               TELA          │                        │    │
   │                 │           │                  [FB/2,2 µH]│
   │                 │           │                        │    │
   │                 │           │              220 µF + 10 µF + 100 nF  ← <5 mm dos pads 9/10/11
   │                 │           │                        │    │
   │                 └── PH P1/P3 (sinais + GND de REFERÊNCIA) ── E22 ─┘
   │                             │                             │
   └── GND do carro ──────────── ★ ESTRELA ────────────────────┘
        (um fio só, AWG 18)     (um borne/ponto de solda na placa de entrada:
                                 GND da entrada, GND do buck A, GND do buck B)
```

Regras que essa topologia garante:

1. **Um único fio de terra até o carro** (o do 12 V). Some o laço do item 1.1: a tela e o
   rádio já não têm caminhos separados para o chassi.
2. **O laço do pulso do PA é curto e fechado localmente.** O caminho é
   BUCK B → +5V → banco → E22 → GND → BUCK B. O banco fornece o di/dt e o buck o resto. O GND
   do PH vira só **referência de sinal**.
3. **Um interruptor (ACC) liga os dois juntos.** Some o caso "tela ligada, rádio sem 5 V" que
   alimenta o SX1262 pelos pinos (memória de 09-30: o chip "sobe" 3–6 s depois, com
   XOSC_NAO_PARTIU).
4. **Uma fonte só para os dois? Não.** O degrau de 0,3–1 A do PA voltaria pelo mesmo nó do
   boost do backlight e do MP1658 da tela. E o caminho natural (USB-C → ORing de AO3401 → Q8
   → Core_5V) é o mesmo que o `PLANO_7B` 3.1 já condenou. Dois bucks baratos custam menos que
   o problema.

### 1.3. Quanto do retorno do PA passa pelo PH nessa topologia [C]

Resistência de fio (AWG, cobre, 20 °C) [M, tabela AWG]:

| AWG | mΩ/m |
|---|---|
| 20 | 33,3 |
| 22 | 53,0 |
| 24 | 84,2 |
| 26 | 133,9 |
| 28 | 212,9 |

Contato JST-PH: 10 mΩ novo, 20 mΩ máx. depois de envelhecido; 2 A só com AWG 24 [M, JST PH].

- **Caminho direto** (GND do E22 → buck B): 30 cm de AWG 20 = 10 mΩ, mais as soldas: **~12 mΩ**.
- **Caminho alternativo:** E22 → GND do PH → tela → GND do USB-C → buck A → estrela.
  - Os 3 fios GND do PH (P1 pinos 8 e 11, P3 pino 11) em paralelo: cada um tem 15 cm de
    AWG 28 (32 mΩ) mais um contato PH (10–20 mΩ). Juntos dão **~16 mΩ**.
  - Placa da tela: ~5 mΩ [E].
  - USB-C de 50 cm (AWG 24 a 28): 42–106 mΩ, mais os contatos (~10 mΩ).
  - Total: **~75–140 mΩ**.
- **Divisão de corrente:** 12/(12+75…140) = **8–14 %**.
  - A 1 A de pico (30 dBm), **80–140 mA no PH**: 1–2 mV de deslocamento de terra. Irrelevante.
  - A 24 dBm (~0,35 A [E]): menos de 1 mV.

**O mesmo cálculo no caso ruim**, com o GND do E22 voltando só pelo PH (GND do step-down frouxo
ou ligado no chassi em outro ponto):
- **1 A em 16 mΩ = 16 mV** se os 3 fios estiverem bons;
- **52 mV** se for um fio só (AWG 28 com o contato envelhecido).

### 1.4. O que isso faz com os limiares lógicos [M + C]

| Lado | VIL máx | VIH mín | Margem |
|---|---|---|---|
| ESP32-P4 (VDD 3,3 V) | 0,25·VDD = 0,83 V | 0,75·VDD = 2,48 V | ~0,4 V (VOL do SX1262 ~0,4 V) |
| SX1262 (VDD ~3,3 V, atrás do regulador do módulo) | 0,3·VDD ≈ 1,0 V | 0,7·VDD ≈ 2,3 V | ~1 V para cada lado |

- **Conclusão 1:** queda **resistiva** (DC) no GND do PH, mesmo no caso ruim (52 mV), não
  derruba lógica. Ela não explica os travamentos.
- **Conclusão 2 — o que derruba é o di/dt.** Indutância de 15 cm de AWG 28 isolado [C]:

  L ≈ 0,2·l·(ln(2l/r) − 0,75) µH = 0,2·0,15·(ln(0,30/0,00016) − 0,75) ≈ **0,20 µH**
  (a regra de bolso de ~1 nH/mm dá 0,15 µH).

  - **A rampa do PA (800 µs) não produz bounce:** 0,2 µH × 1 A / 800 µs = 0,25 mV. A
    configuração `RADIOLIB_SX126X_PA_RAMP_800U` (`radio.h:124`) é exatamente o que protege
    aqui. Com a rampa de 10 µs: ~20 mV.
  - **A corrente de chaveamento de um buck é outra história:** bordas de ~10–20 ns, centenas
    de mA. Se ela passar pelo GND do PH: 0,2 µH × 0,3 A / 20 ns = **3 V de pico**.
    - Isso basta para uma borda falsa no **DIO1** (o firmware lê como TxDone/RxDone).
    - Basta também para **clock duplo no SCK** (comando SPI corrompido = rádio em estado
      estranho, "TX preso").

  **Regra prática:** nenhum buck pode ter o laço de entrada ou de saída fechando pelo cabo de
  sinal. Os dois bucks ficam juntos da estrela, cada um com o capacitor de entrada no próprio
  módulo.
- **Conclusão 3 — conector frouxo é o pior caso.** Se o GND do PH abre e o +5V do E22
  continua, os sinais viram o único retorno do módulo: corrente pelos diodos de proteção do
  ESP32-P4 e do SX1262. Isso explica o "reinício em loop no TX com conector frouxo" sem
  precisar de ground bounce. Os resistores em série da seção 4.4 limitam esse caso.

### 1.5. ESP32-P4

O P4 é alimentado pelo buck MP1658 da tela (3,3 V), a partir do 5 V do USB [M, esquemático, via
`PLANO_7B`]. Ele não enxerga a queda do 5 V do E22. Só enxerga a do próprio 5 V e os sinais.

Os riscos ao P4 são três:
- **(a)** brownout do 5 V **da tela** (isqueiro, cabo fino; seção 6);
- **(b)** corrente injetada pelos GPIO quando os dois lados não estão ligados juntos;
- **(c)** RF a 915 MHz entrando pelos fios (4.6).

---

## 2. Ambiente automotivo e escolha do conversor

### 2.1. O que a norma exige (sistema de 12 V) [M]

| Evento (ISO 16750-2) | Valor |
|---|---|
| Faixa contínua de alimentação | 6–16 V (classe usada pela TI no TIC10024-Q1) |
| Partida, nível I | cai a **8 V**, segura 9,5 V, volta em 1 s |
| Partida, nível II | cai a **4,5 V**, 6,5 V, 10 s |
| Partida, nível IV | cai a **6 V**, 6,5 V, 10 s |
| Partida com bateria auxiliar (*jump start*) | **24 V por 60 s** |
| *Load dump* Test B (alternador com supressão central, o normal hoje) | **35 V**, td 40–400 ms, 5 pulsos |
| *Load dump* Test A (sem supressão) | 79–101 V |
| Polaridade invertida | −14 V (60 s) |
| AC sobreposta (ondulação do alternador) | 1–4 Vpp, 50 Hz–25 kHz |

Pulsos rápidos da ISO 7637-2, ordem de grandeza:
- pulso 1: −75 a −150 V, 2 ms, Ri 10 Ω;
- pulso 2a: +37 a +112 V, 50 µs, 2 Ω;
- pulsos 3a/3b: ±150 V em centenas de ns, 50 Ω.

### 2.2. Os módulos baratos contra essa tabela

| Módulo | Vin máx | Topologia, fsw | 24 V por 60 s | Load dump B (35 V) | Partida a 6 V | Veredito |
|---|---|---|---|---|---|---|
| **Mini560** (JW5068A) | 20 V (vendedores) [M] | síncrono, 500 kHz | **morre** | morre | ok | **não** |
| **MP1584** | 28 V [M] | 1,5 MHz, 3 A | passa raspando | **morre** | ok | **não** |
| **LM2596** (comum) | 40 V (45 abs) [M] | assíncrono, 150 kHz | ok | ok | **não** (precisa de ~Vout+1,5 V = 6,5 V) | aceitável com ressalva |
| **XL4015** | 36 V (40 abs) [M] | assíncrono, 180 kHz, 5 A | ok | raspando, sem margem para o TVS | ~ok | aceitável |
| **LM2596HV** | 60 V [M] | 150 kHz | ok | ok, com folga para o TVS | não | **bom para o rádio** |
| **TPS54560** (módulo 5–60 V) | 60 V [M] | 100 kHz–2,5 MHz | ok | ok | ok | **o melhor dos disponíveis** (já está no `PLANO_7B`) |
| Conversor selado "12/24 V → 5 V 3 A" | geralmente 8–35 ou 8–40 V | — | ok | ok, se for de 40 V | depende | **bom para a tela** (procurar "entrada 8–40 V") |
| Carregador veicular USB-C PD de marca (Anker, Baseus, Ugreen) | entrada 12/24 V | — | ok | provável | ok | ok para a tela. **O ponto fraco é a tomada de isqueiro**, não o carregador |

### 2.3. Proteção de entrada: correção do `PLANO_7B` e do `placa_radio_smt`

**O TVS não segura *load dump*.** A energia do Test B (35 V, Ri ≥ 0,5 Ω, até 400 ms) num TVS que
conduz a ~29 V é de **dezenas de joules** [C]. Um SMCJ aguenta ~1,5 J (1500 W × 1 ms) e um 5KP
~5 J. **O TVS precisa ficar FORA da faixa do *load dump* B e só cortar os pulsos rápidos.** A
sobretensão lenta, quem aguenta é o conversor.

- `PLANO_7B` 3.2 propõe o **5KP26A** (VBR 28,9 V): ele **conduz no Test B** e no *jump start*
  de 24 V perto do limite. **Trocar por um TVS de 33 V** (VR 33, VBR 36,7, Vc ~53 V) com
  conversor de 60 V.
- `placa_radio_smt` usa SMAJ18A (VBR 20 V) com o AP63205 (Vin máx 32 V) e um PTC de 0,75 A:
  - o **SMAJ18A conduz** em qualquer *jump start* (24 V) e em qualquer *load dump*;
  - com 400 W, ele morre antes do PTC abrir.

  Na v4, considere:
  - **SMAJ/SMBJ30A** (VBR 33,3 V, ainda abaixo dos 35 V do Test B) mais um conversor de
    ≥40 V;
  - ou aceitar que o TVS é fusível de sacrifício e documentar.

  É uma escolha de projeto. Registro porque o README da v4 diz "grampeia surtos do carro em
  ~29 V", e isso só vale para pulsos rápidos.

Sequência recomendada para a protoboard:
- **F1:** fusível de lâmina **2 A** (os dois bucks puxam ~1,2 A de 12 V no pior caso [C]:
  (5 V×1,2 A + 5 V×1 A)/12 V/0,85).
- **D1:** TVS **bidirecional** de 33 V: 1.5KE33CA (furo, 1500 W) ou SMCJ33CA. O bidirecional
  não conduz com bateria invertida (−14 V), então o fusível não queima à toa.
- **D2:** **Schottky SB560/SR560** (60 V, 5 A, ~0,5 V de queda). Bloqueia polaridade invertida
  e aguenta os −53 V do grampo do D1. O 1N5822 (40 V) **não serve** aqui. P-FET (IRF9540N) é a
  alternativa sem queda, mas pede um zener de 12–15 V no gate.
- **L1 + C:** toróide de 10–22 µH / 3 A + eletrolítico 100 µF/50 V 105 °C + 1 µF e 100 nF
  cerâmicos 50 V.
  - Isola a ondulação do alternador (1–4 Vpp) e devolve menos ruído do buck ao carro.
  - Ressonância [C]: 1/(2π√(10 µH·100 µF)) ≈ **5 kHz**. Amortecida pelo ESR do eletrolítico
    (~0,1–0,3 Ω contra Z0 = √(L/C) = 0,32 Ω).

### 2.4. Ripple de saída e o que ele faz no rádio

- **Dentro do E22** [M, desmontagem S5NC/CDEBYTE_Modules]:
  - o **TXEN liga um buck TI TLV62568** (2,5–5,5 V, ~1,5 MHz), que gera VCCB/VREF2 do PA;
  - o **RXEN liga um LDO** que alimenta o LNA.

  O pulso de entrada do TLV62568 (1,5 MHz, bordas rápidas) é o que o **10 µF X7R colado nos
  pads** atende. O módulo só tem ~4 cerâmicos pequenos por dentro.
- **Ripple no VCC aparece como AM no PA:** espúrios em f ± fsw [C, modelo de modulação]. Com
  m = ΔVpico/V:
  - 50 mVpp em 5 V → m = 0,005 → espúrios a **~−52 dBc** (a 24 dBm, −28 dBm em ±150 kHz com o
    LM2596; dentro ou colado no canal vizinho);
  - 10 mVpp → ~−66 dBc.

  Meta: **≤ 20 mVpp nos pads**. É o que justifica o filtro FB/2,2 µH + banco (2.5).
- **RX:** o LNA está atrás de um LDO, então o ripple de baixa frequência pesa pouco. Pesa a
  **emissão de alta frequência** do buck (harmônicos das bordas de ns chegando a 915 MHz) e a
  **antena perto do buck**.
  - O firmware já mede ruído de fundo (linha N).
  - **Teste:** ruído de fundo com o buck B contra uma bateria de 4×AA/LiPo + diodo, ou uma
    fonte linear.
  - Diferença > 2 dB = buck mal posicionado ou sem blindagem.

### 2.5. Filtro entre o buck B e o E22 (substitui o "LDO depois do buck")

De 5 V para 5 V não há folga para LDO; de 5,9 V com TPS7A4501, há (v5).

Para a protoboard:
- uma **ferrite de 600 Ω@100 MHz, 2 A** (BLM31PG601 ou conta de ferrite no fio);
- **ou** um indutor de **2,2 µH / ≥2 A** blindado;
- antes do banco 220 µF + 10 µF + 100 nF.

Contas [C]:
- **Filtro:** 2,2 µH com 220 µF dá fc = 7,2 kHz. **~−70 dB a 1,5 MHz e ~−52 dB a 150 kHz**
  (2ª ordem, ideal; na prática 30–40 dB por causa do ESR e da ESL).
- **Amortecimento:** Z0 = √(2,2 µH/220 µF) = 0,1 Ω, e o ESR do eletrolítico comum (~0,13 Ω)
  amortece. **Mais um motivo para não usar polímero** (confirma o `ESTUDO_PECAS`).
- **Queda DC no indutor:** DCR ~30–50 mΩ × 0,4 A = 12–20 mV. Aceitável.

---

## 3. Desacoplamento do E22

### 3.1. Por que a distância importa (indutância por mm)

A regra de bolso é ~**1 nH por mm** de fio ou trilha [M, regra clássica de *layout*;
coerente com o 0,2 µH/15 cm calculado acima]. O cerâmico existe para atender bordas de
**10–50 ns** (o TLV62568 interno, a rampa do SX1262, harmônicos):

| Distância do cerâmico aos pads (ida e volta) | ΔV com 1 A em 20 ns [C] |
|---|---|
| 5 mm | 5 nH × 1 A / 20 ns = **0,25 V** |
| 20 mm | 1 V |
| 50 mm (num cabo) | **2,5 V**: o capacitor é inútil |

**Regra:** 10 µF X7R (+100 nF) com terminais de **≤ 5 mm** entre os pads 9/10 (VCC) e 11
(GND). O eletrolítico tolera ≤ 10–15 mm, porque atende eventos de dezenas de µs.

### 3.2. O pacote de 200–420 ms (SF9) não é coberto por capacitor nenhum [C]

- **Para o capacitor segurar o pacote inteiro:** ΔV = I·t/C = 0,6 A × 0,4 s / 470 µF =
  **510 V**. Ou seja, o capacitor não segura nada no pacote; quem segura é o buck e o fio.
- **O capacitor cobre só o atraso de resposta do buck:**
  - τ ≈ 20 µs (MP1584/TPS54560 em alta frequência) a ~100 µs (LM2596, 150 kHz);
  - com a rampa de 800 µs, o déficit é ≈ (1 A/800 µs)·τ = 0,125 A durante ~τ;
  - ΔV ≈ 0,125 A × 100 µs / 220 µF ≈ **57 mV** (pior caso, 1 A, LM2596).
- **Com 470 µF:** 27 mV. **O 470 µF não é errado, é só pouco ganho.** Pode ser o C1 se já estiver
  na mão, **desde que esteja nos pads** (≤ 15 mm). Se ele estiver na saída do step-down, longe
  do módulo, não faz nada contra o fio. Também **não é polímero/low-ESR**, por causa do
  amortecimento do engate (ver `ESTUDO_PECAS` 2).
- **Queda durante o pacote = I × R_cabo + regulação do buck:**
  - 30 cm de par AWG 20 (20 mΩ ida e volta) × 1 A = 20 mV;
  - regulação de carga de um LM2596: ~1 % = 50 mV;
  - somado à queda do capacitor, a **~1 A: VCC mínimo ≈ 5,05 − 0,02 − 0,05 − 0,06 ≈ 4,92 V**;
  - **a 24 dBm (~0,35 A [E]): ≈ 5,0 V.**

  Folga sobre o "≥ 5 V para 30 dBm" do manual. A 24 dBm o PA está em *back-off* de 6 dB e
  não precisa dos 5 V cheios [E].
- **Corrente a 24 dBm:** o manual só dá 500–620 mA a 30 dBm [M]. Num PA perto de classe AB, a
  corrente escala com a amplitude: −6 dB → ~metade, mais ~50 mA fixos. Fica em **~0,3–0,4 A a
  24 dBm** e **~0,2–0,25 A a 20 dBm** [E]. **Medir** (5.2).
- **Ressonância cabo × banco** [C]: 0,5 µH (par de 50 cm) com 220 µF dá f = 15 kHz, Z0 = 48 mΩ.
  O ESR de 0,13 Ω do eletrolítico deixa o laço **sobreamortecido**. Sem eletrolítico (só
  cerâmico), ressoa: é o pico de 7–9,5 V ao plugar do `ESTUDO_PECAS`.

---

## 4. Protoboard: como montar "do jeito perfeito"

### 4.1. O que **não** pode ir pela protoboard de molas

| Parâmetro da protoboard comum | Valor | Consequência |
|---|---|---|
| Resistência por contato de mola | 10–50 mΩ novo, >100 mΩ gasto ou oxidado [M, literatura de *breadboard*; varia por fabricante] | caminho buck → trilho → jumper → módulo = 4–6 contatos + 2 jumpers Dupont de AWG 26–28 ≈ **0,15–0,4 Ω**. A 1 A, **0,15–0,4 V de queda**, e **intermitente** com qualquer toque |
| Corrente | ~1 A por contato, na prática | no limite |
| Capacitância entre fileiras vizinhas | ~2–25 pF | irrelevante a 2 MHz |
| Indutância dos trilhos longos | dezenas a centenas de nH | anula qualquer desacoplamento que não esteja soldado no módulo |

**Conclusão:**
- **+5V, GND e o banco do E22 nunca passam pela protoboard.** Vão soldados, curtos.
- **12 V e os bucks também não**: borne de parafuso ou solda.
- **A protoboard de molas não é o "jeito perfeito".** Ela reproduz exatamente o defeito que
  estamos caçando (contato intermitente no caminho de potência).

### 4.2. Recomendação de base

| Opção | Veredito |
|---|---|
| Protoboard de molas | **não**, para potência; tolerável só para os sinais |
| Placa perfurada ilhada (ilhas isoladas, 2,54 mm, fibra) | **boa**. Faça um **barramento de terra de fio de cobre nu AWG 18–16** correndo pela placa, e solde todo GND nele, em estrela |
| **Fenolite ou fibra cobreada virgem como plano de terra ("Manhattan")** | **a melhor**, para RF e potência. A face de cobre inteira é o GND. Os pontos que não são terra viram ilhas de placa colada com cianoacrilato ("Manhattan pads") ou ilhas riscadas com estilete. O GND do E22 (pads 1–5, 11, 12, 20, 22) solda **direto no plano**: é o que o manual da Ebyte pede ("cobre debaixo do módulo") |
| Placa de protótipo com ilhas em tiras (stripboard) | evitar: as tiras são antenas e indutâncias compridas |

**Montagem física do E22 na base:**
- Fixe o E22 com fita dupla-face fina ou cola, etiqueta para cima.
- Ligue cada pad castelado com **fio rígido curto (≤ 5 mm)**, ou com uma barra de pinos
  macho soldada no meio-furo.
- Banco nos pads 9/10 ↔ 11 com os terminais cortados no mínimo.

### 4.3. Fios de sinal (SPI a 2 MHz)

- **Frequência:** o firmware usa **2 MHz no SPI** (`radio.h:65`, `SPISettings(2000000, …,
  SPI_MODE0)`) [R]. O período é de 500 ns. O cabo PH de 15 cm tem ~0,75 ns de atraso: não é
  linha de transmissão a essa frequência. **O problema é diafonia e laço, não reflexão.**
- **Comprimento:** **≤ 20 cm**, o PH atual serve.
  - No cabo, deixe pelo menos um GND entre o SCK e os sinais lentos.
  - No PH de 12 vias isso já existe parcialmente: P3 pino 11 GND, P1 pino 8 GND.
  - **Use os três GND do cabo**, não um só.
- **Não passe o par de 5 V do E22 colado ao cabo PH em paralelo comprido:** cruze em 90°.

### 4.4. Resistores em série, *pulls* e sequência de energização

Na protoboard, eles custam 9 resistores de 1/4 W. **Recomendo pôr agora**, apesar de o
`ESTUDO_PECAS` 3.3 ter deixado para depois. A razão é medida: o rádio sem 5 V com a tela ligada
"sobe" pelos pinos (memória 09-30), e já houve um travamento só resolvido cortando a energia.

| Linha | Peça | Onde | Por quê [C] |
|---|---|---|---|
| SCK, MOSI, NSS | **330 Ω** em série | lado da tela (fonte) ou no meio | limita a injeção a (3,3 − 0,6)/330 ≈ **8 mA** com o rádio apagado. RC = 330 Ω × ~25 pF ≈ 8 ns ≪ 500 ns: o SPI a 2 MHz nem percebe |
| MISO | **330 Ω** | lado do E22 | mesma razão, sentido inverso (tela desligada) |
| NRST | **1 kΩ** | — | injeção ≤ 3 mA; o SX1262 tem pull-up interno ~50 kΩ [M] |
| TXEN | **1 kΩ** em série + **10 kΩ pull-down no lado do E22** | — | nível alto = 3,3×10/11 = **3,0 V**, acima do limiar de EN do TLV62568 (≤ 1,2 V) [M]. Pull-down obrigatório (ESTUDO_PECAS 6) |
| RXEN | **1 kΩ** + **10 kΩ pull-down** | — | o `ESTUDO_PECAS` tirou o R2. Na protoboard, **devolva**: o pino 5 do P4 fica flutuando do reset até o firmware [M, datasheet P4 tab. 2-1], e o RXEN solto liga o LDO do LNA e o lado RX do switch |
| BUSY, DIO1 | **1 kΩ** + **100 pF ao GND no lado da tela** | — | RC = 100 ns. Corta 915 MHz (RF captada pelo cabo, 4.6) e glitches de ns. Não atrasa nada que importe: o firmware espera o BUSY em µs–ms, e o DIO1 é IRQ de fim de pacote |

**Sequência de energização:**
- O correto é **os dois juntos** (seção 1.2, mesmo ACC).
- Na bancada, com fontes separadas:
  - liga **primeiro o 5 V do E22**, depois a tela;
  - desliga **primeiro a tela**.
- **Antena (ou carga de 50 Ω) sempre antes.**
- O firmware deveria pôr NSS/NRST/TXEN/RXEN/SPI como **entrada** enquanto o rádio não responde
  (já proposto no `ESTUDO_PECAS` 3.3; não implementado). Com os resistores acima, isso vira
  boa prática, não emergência.

### 4.5. O que já está documentado e eu não mudaria

- 10 kΩ no TXEN;
- nada de pull-up no NRST para 5 V (o SX1262 aguenta 3,9 V);
- nada de TVS de 5 V;
- nada de polímero.

### 4.6. Uma hipótese que os docs não cobrem: **RF de 24 dBm entrando na tela** [E, mecanismo M]

O problema da bancada: a 14 dBm (24 na antena), **uma tela deu PANIC e a outra aguentou**. O
mesmo firmware e o mesmo nível de potência, com resultado diferente, apontam para **geometria**
(posição da antena), além da alimentação.

Campo perto da antena [C, campo distante, ordem de grandeza]:
E = √(30·P·G)/d, com P = 0,25 W e G = 1,6 (2 dBi):

| Distância | Campo |
|---|---|
| 30 cm | 11,5 V/m |
| 10 cm | 35 V/m |

Ensaios de imunidade típicos usam 3–10 V/m (IEC 61000-4-3).

E o cabo PH de 15 cm tem ≈ **λ/2 a 915 MHz** (λ = 32,8 cm): é uma antena ressonante entrando
direto nos GPIO. A RF retificada nos diodos de proteção dos pinos vira deslocamento de DC e
bordas falsas:
- no **DIO1**, uma IRQ fantasma;
- no **SCK**, um comando corrompido;
- no 3,3 V, um brownout.

**Teste decisivo (barato):** repetir os 14 dBm com a saída do E22 num **atenuador de 30 dB /
2–5 W + carga de 50 Ω** (ou antena a 1–2 m, por cabo coaxial).

| Resultado | Conclusão |
|---|---|
| O PANIC some | é **acoplamento de RF**. O remédio é o RC de 100 pF no BUSY/DIO1, uma ferrite *clamp* no cabo PH e a antena longe da tela (no carro: teto, fora) |
| O PANIC fica | é **alimentação ou firmware**: seção 5 |

**"E22 travou até desligar a energia":**
- Se reiniciar a tela (que pulsa o NRST e refaz o `begin()`) não destrava, o SX1262 ficou num
  estado que só o POR limpa. Os candidatos [E]:
  - *latch-up* por injeção nos pinos durante um afundamento do 5 V do módulo (o VDD interno
    cai abaixo dos 3,3 V que a tela continua impondo nos pinos);
  - ou o regulador interno em proteção.
- **Na próxima vez, antes de desligar, meça:**
  - a corrente do 5 V do E22 em repouso: normal é dezenas de mA. **Centenas de mA com o
    módulo esquentando = *latch-up***;
  - a tensão nos pads 9/10;
  - o nível do BUSY.
- Os resistores em série da 4.4 são a proteção contra *latch-up* por injeção.

---

## 5. Medições de bancada para provar que está "perfeito"

### 5.1. Instrumentos (do mais barato ao melhor)

| Instrumento | Mede | Observação |
|---|---|---|
| **Multímetro com MIN/MAX** (UNI-T UT61E+, UT89X, Aneng com "MAX/MIN") | **VCC mínimo nos pads durante o pacote** | Funciona porque o pacote SF9 dura 200–420 ms: o *sag* resistivo dura o pacote inteiro, e o MIN/MAX (resposta de ~100–250 ms) pega. O pico de µs da rampa ele **não** pega |
| **Osciloscópio de entrada** (FNIRSI DSO152/DSO-TC3 ~200 kHz–1 MHz; Hantek 6022BE USB 20 MHz; FNIRSI 1013D/1014D) | queda de VCC durante o pacote, a forma da rampa, o **ripple do buck** (para ripple de 150 kHz–1,5 MHz precisa de ≥ 20 MHz) | Ponta em ×1, **mola de terra curta** (não o jacaré de 15 cm), direto nos pads 9 e 11 |
| **Módulo INA226 com shunt trocado para R010 (10 mΩ)** | tensão **e** corrente do 5 V do E22, ~1 kHz, registrável | Ligado ao I2C da tela (header I2C PH 4 vias, o mesmo barramento do toque; endereço 0x40–0x4F não colide com o GT911). Dá ao firmware um "**VCC mínimo por TX**". Exige firmware novo. Antes disso, rode num Arduino/ESP32 qualquer |
| **Medidor USB-C em linha** (FNIRSI FNB48/C1, KWS-1902C) | tensão e corrente **da tela** no conector | É o instrumento que responde "a tela está tomando brownout?" |
| **Atenuador 30 dB / ≥2 W + carga de 50 Ω SMA** | separa RF de alimentação (4.6) | Também protege o vizinho na bancada |
| **Fonte de bancada ajustável com limite de corrente** | simula partida (12 → 6 V → 12 V) e *jump start* (24 V) | — |
| Termômetro IR ou termopar do multímetro | temperatura da blindagem do E22 e do buck | — |

### 5.2. Critérios de aceitação (protoboard e depois a PCB)

Os limites são **[C/E]**: derivam do manual (5,0–5,5 V) e da conta da seção 3.2.

| # | Teste | Critério |
|---|---|---|
| 1 | Tensão nos pads 9/10 em repouso, rádio em RX | **5,00–5,15 V** |
| 2 | VCC mínimo nos pads durante o TX a 14 dBm (MIN/MAX, 10 min) | **≥ 4,90 V** |
| 3 | Idem a **18 dBm** (28 dBm na antena), **na carga de 50 Ω**, 10 min. É o teste de margem de 4 dB que o `ESTUDO_PECAS` 6 pede | **≥ 4,80 V**, `preso=0`, zero reinício |
| 4 | Pico máximo ao plugar o 5 V a quente (osciloscópio, *single shot*) | **≤ 5,4 V** (abs 5,5 V) |
| 5 | Ripple nos pads, banda de 20 MHz | **≤ 20 mVpp** em RX; ≤ 50 mVpp em TX |
| 6 | Corrente do E22 (INA226 ou multímetro na escala de 10 A, ~10 mΩ) | repouso RX ~**10–20 mA**; TX a 14 dBm ~**0,3–0,4 A** [E]. **Anotar o valor medido**: ele substitui a minha estimativa. RX acima de 50 mA ou crescendo = suspeita de *latch-up* |
| 7 | Δtensão DC entre o GND da tela (carcaça do USB-C) e o GND do E22 (pad 11), durante o TX | **≤ 10 mV** (> 20 mV = retorno passando pelo PH) |
| 8 | Ruído de fundo (linha N) com o buck B contra bateria/linear | **diferença ≤ 2 dB** |
| 9 | Temperatura da blindagem do E22, 30 min no ciclo TDMA real (1 slot em 4, pacote de ~0,3 s, ~15 % de *duty*) a 24 dBm | **≤ 20 °C acima do ambiente**. No carro, o ambiente chega a 60–70 °C ao sol, e o limite do módulo é 85 °C [M] |
| 10 | Partida simulada: fonte de bancada 12 → 6 V por 1 s → 14 V | sem PANIC. Se a tela reiniciar, o motivo tem de ser **POWERON ou BROWNOUT**, e o rádio tem de voltar sozinho (sem desligar a energia) |
| 11 | 24 V por 60 s na entrada | nada esquenta além do normal, o TVS frio |
| 12 | 1 h no ciclo real a 14 dBm, duas telas | `preso=0`, 0 reinício violento, entrega ≥ 97 % (a referência de 10-08 é 97,5–99,9 %) |

**Como separar "modelo errado" de "montagem errada":**
- **Se o teste 2 passa e o 3 falha:** a queda está no fio ou no buck. Meça o VCC **na saída
  do buck** e **nos pads** ao mesmo tempo:
  - diferença grande = fio ou contato;
  - as duas caem juntas = buck fraco.
- **Se os testes 2 e 3 passam e ainda há TX preso:** não é alimentação. Vá para o 4.6 (RF) e
  para o `getDeviceErrors()` do vigia.

---

## 6. A tela P4 no carro

- **Entrada:** USB-C, 5 V. Os dois USB-C passam por ORing de AO3401 e pelo FET do botão
  (Q8, DMP2066LSN) até o Core_5V [M, esquemático, via `PLANO_7B`]. A placa não negocia PD:
  pede 5 V (Rd), e qualquer fonte PD entrega 5 V até 3 A.
- **Consumo:** o repo diz **0,8–1,2 A com o backlight no máximo** (`PLANO_7B` 3.1, sem a
  medição registrada). A Waveshare não publica [M, página da 7B lida: só "Type-C para
  alimentação"]. **Meça com o FNB48** (5.1): é a primeira coisa a fazer.
- **Dimensionamento:** fonte de **5 V / 3 A** (≥ 2× o consumo).
- **O cabo é o elo fraco** [C], com 1,2 A e 1 m de cabo:

  | Fio do VBUS/GND | Queda ida e volta | Tensão na placa |
  |---|---|---|
  | 28 AWG (cabo comum) | 0,51 V | **4,5 V** |
  | 24 AWG | 0,20 V | 4,8 V |
  | 20 AWG (cabo "5 A") | 0,08 V | 4,9 V |

  **Use cabo USB-C "5 A/100 W" de 30–50 cm.** Na protoboard, o buck A com saída USB-A +
  cabo A→C curto e grosso também serve.
- **Isqueiro:** a mola mais a vibração geram microcortes, e o soquete é desligado na partida
  em muitos carros. Cada microcorte vira um `BROWNOUT` (seção 0).
  - **Preferível:** cabear do **derivador de fusível** (circuito ACC) para a placa de entrada
    da seção 1.2.
  - Se usar o isqueiro, use um adaptador de **plugue com trava** (tomada 12 V "com rosca") e
    nunca o mesmo adaptador para o rádio.
- **O 5 V da tela pode alimentar o E22? Não**, por nenhum caminho:
  - o **Core_5V (CAN/RS485)** não serve: `CLAUDE.md` / `PLANO_7B` 3.1;
  - os pinos **BAT do P1** e **J4** são entrada de célula 1S;
  - o **VBUS do USB-C antes do Q8** não está exposto;
  - somar 0,3–1 A de pulsos aos 0,8–1,2 A da tela no mesmo cabo USB-C dobra a queda
    resistiva e põe o pulso do PA no nó do MP1658 e do boost do backlight.
- **Correção de documento:** `docs/MONTAGEM.md` (Passo 3, tabela "RÁDIO — alimentação") ainda
  manda ligar o VCC do E22 no **CAN ou RS485 pino 1**. Isso contradiz o `CLAUDE.md` e o
  `PLANO_7B`. O mesmo arquivo (Passo 6) diz que `setCurrentLimit` "tem que vir depois de
  `setOutputPower()`", e o `CLAUDE.md` corrigiu para "depois do `begin()`; a ordem é
  indiferente". Não alterei o arquivo.

---

## 7. Lista de material da protoboard (por nó)

| Item | Especificação | Alternativas (Brasil: Mercado Livre, AliExpress, lojas de componentes) |
|---|---|---|
| Base | fenolite ou fibra **cobreada virgem** 10×15 cm (plano de terra, Manhattan) | placa perfurada **ilhada** de fibra 2,54 mm + barramento de cobre nu AWG 16–18 |
| Derivador de fusível | "fuse tap" / "add-a-circuit" no padrão do carro (mini, ATO ou micro2), num circuito **ACC** | tomada 12 V com rosca, só para teste |
| F1 | fusível de lâmina **2 A** + porta-fusível em linha | — |
| D1 | TVS **1.5KE33CA** (furo, bidirecional, 1500 W) | SMCJ33CA (SMD), P6KE33CA (600 W) |
| D2 | Schottky **SB560 / SR560** (60 V, 5 A) | P-FET IRF9540N + zener 12 V + 100 kΩ no gate |
| L1 | indutor toroidal **10–22 µH, ≥ 3 A** | indutor de buck reaproveitado (o do LM2596 é 33 µH / 3 A) |
| Entrada | eletrolítico **100 µF / 50 V, 105 °C** + cerâmicos 1 µF e 100 nF 50 V | — |
| Buck A (tela) | **conversor 12/24 V → 5 V 3 A, entrada 8–40 V** (selado), saída USB-A ou fio | carregador veicular USB-C PD de marca (Anker/Baseus/Ugreen), cabeado |
| Cabo da tela | USB-C **5 A/100 W, 30–50 cm** (ou A→C curto e grosso) | — |
| Buck B (rádio) | **TPS54560** (módulo 5–60 V) ajustado para **5,05 V** | **LM2596HV** (60 V); XL4015 só se o TVS for ≤ 36 V de grampo (não é o caso do 33CA) |
| Filtro do E22 | ferrite **600 Ω@100 MHz / 2 A** (BLM31PG601SN1) **ou** indutor 2,2 µH / 2 A blindado | conta de ferrite no fio |
| C1 (nos pads) | eletrolítico **220 µF / 16 V, 105 °C, comum** (EEU-FR1C221, Nichicon HE, Rubycon ZLH) | o **470 µF** que já está montado, se for comum (não polímero) e for movido para **≤ 15 mm** dos pads |
| C2, C3 | **10 µF X7R 25 V** (GRM31CR71E106KA12L) + **100 nF X7R 50 V**, terminais ≤ 5 mm | 2× 4,7 µF X7R |
| R pull-down | **10 kΩ** ×2 (TXEN, RXEN) | — |
| R série | **330 Ω** ×4 (SCK, MOSI, NSS, MISO); **1 kΩ** ×5 (NRST, TXEN, RXEN, BUSY, DIO1) | — |
| C de RF | **100 pF** C0G ×2 (BUSY, DIO1 ao GND, no lado da tela) | — |
| Fio de 5 V do rádio | silicone **AWG 20**, vermelho e preto, **trançado**, ≤ 30 cm | AWG 22 com ≤ 20 cm |
| Fio de 12 V | AWG 18 | — |
| Conectores de potência | **XT30** (5 V) e borne de parafuso KF301 ou Deutsch DT (12 V) | solda direta |
| Cabo de sinal | o PH 12 vias atual, com os **três GND** ligados | — |
| Atenuador | SMA **30 dB / 2–5 W** + carga de 50 Ω | antena a 1–2 m por coaxial |
| Instrumentos | multímetro com MIN/MAX; FNB48; INA226 com shunt R010; termômetro IR; osciloscópio ≥ 20 MHz desejável | ver 5.1 |
| Ferrite *clamp* | *snap-on* para o cabo PH (escolher uma especificada até 1 GHz) | — |

---

## 8. Esquema textual da ligação (protoboard)

```
[CARRO]
 fusível ACC ──(derivador)── F1 2A ──┬── D1 1.5KE33CA ──┐
                                     │                   GND★
                                     └── D2 SB560 (anodo→catodo) ── L1 10–22µH ──┬── C 100µF/50V ∥ 1µF ∥ 100nF ──GND★
                                                                                 │
                                                       ┌─────────────────────────┴───────────────┐
                                                   BUCK A (8–40V→5V 3A)                 BUCK B TPS54560/LM2596HV (5,05V)
                                                    OUT+   OUT−                           OUT+          OUT−
                                                     │      └──── GND★                     │             └──── GND★
                                      USB-C 5A, ≤50 cm                          FB 600Ω (ou 2,2µH)
                                                     │                                     │ AWG20 trançado ≤30 cm (com o GND)
                                                  TELA P4                       ┌──────────┴──────────┐
                                                     │                          │ C1 220µF ∥ C2 10µF ∥ C3 100nF   (≤5 mm)
                                                     │                        E22 pads 9/10 (VCC)   pad 11 + 12/20/22 (GND) ─ plano ─ GND (par trançado) ─ GND★
                                                     │
          P3/P1 (PH 15 cm) ──────────────────────────┘
            SCK  ─330Ω─────────────────────── E22 18
            MOSI ─330Ω─────────────────────── E22 17
            NSS  ─330Ω─────────────────────── E22 19
            MISO ─────────────────────330Ω─── E22 16
            NRST ─1kΩ──────────────────────── E22 15
            TXEN ─1kΩ───────────┬──────────── E22 7     10kΩ → GND (lado do E22)
            RXEN ─1kΩ───────────┬──────────── E22 6     10kΩ → GND (lado do E22)
            BUSY ─[100pF→GND]─1kΩ──────────── E22 14
            DIO1 ─[100pF→GND]─1kΩ──────────── E22 13
            GND ×3 (P1-8, P1-11, P3-11) ───── plano de terra da placa do E22  (referência, não retorno de potência)
            GPS: 3V3 (P3-12), GND (P3-11), TXD/RXD/PPS (P3-8/9/10) — como hoje

★ = UM ponto físico: borne ou ilha de solda única na placa de entrada. O GND do carro chega
    só ali, por um fio AWG 18.
```

---

## 9. O que é medido, o que é datasheet e o que é estimativa (resumo)

- **Medido [R]:**
  - SPI a 2 MHz e rampa de 800 µs (código);
  - estável a 14 dBm e trava a 18 dBm sem capacitor (CHANGELOG 09-08);
  - zero TX preso a 10 dBm no carro (09-30);
  - brownout e watchdog contam como "violento" (código);
  - chip "sobe" pelos pinos sem 5 V (09-30).
- **Datasheet ou norma [M]:**
  - tabelas ISO 16750-2 (TI SCPS268A);
  - JST PH 10/20 mΩ, 2 A com AWG 24;
  - E22: 2,5–5,5 V, ≥ 5 V para potência plena, 500–620 mA a 30 dBm;
  - TLV62568 e LDO do LNA dentro do E22 (desmontagem S5NC);
  - Vin máx dos conversores;
  - resistência AWG.
- **Calculado [C]:** quedas, divisão de corrente, L do fio, ΔV dos capacitores, espúrios de AM,
  campo de RF, energia do *load dump* no TVS.
- **Estimado [E]:**
  - corrente do E22 a 24 dBm (0,3–0,4 A);
  - consumo da tela (0,8–1,2 A, número do repo sem medição registrada);
  - tempo de resposta dos bucks;
  - resistência de contato de protoboard (varia por marca);
  - hipóteses de *latch-up* e de RF entrando na tela.

  Os testes 6, 8 e 10 da seção 5.2 e o teste do atenuador (4.6) transformam cada uma em
  medição.

## Fontes

- ISO 16750-2 (parâmetros reproduzidos em TI TIC10024-Q1, SCPS268A, seção ISO 16750-2):
  https://www.ti.com/document-viewer/lit/html/SCPS268A/GUID-246A814D-33B3-4CB3-89E7-CCD6A372DCF4
- *Load dump* Test A/B (35 V suprimido; 79–101 V sem supressão): Intertek ISO 16750-2; ADI,
  *Analog Dialogue* vol. 54 n.º 1 (*protecting and powering automotive electronics*).
- JST PH, especificação (contato 10 mΩ inicial / 20 mΩ após ensaio; 2 A com AWG 24).
- Desmontagem do E22-900M30S (TLV62568 no TXEN, LDO S2RF no RXEN, LNA BFU520):
  https://github.com/S5NC/CDEBYTE_Modules
- Mini560 = JW5068A, entrada de 5 V/7–20 V conforme o vendedor (gogo.co.nz, domoticx).
- Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (página oficial, alimentação só "Type-C"):
  https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-7B
- Do repo (fontes primárias já citadas lá): Ebyte E22-M V1.2/V1.5 e AN2023005; Semtech
  SX1261/2; ESP32-P4 datasheet v0.7; esquemático oficial da 7B; TI TLV62568; datasheets de
  MP1584, LM2596/HV, XL4015 e TPS54560 (via `PLANO_7B`).
