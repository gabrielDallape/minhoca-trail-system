# Bancada do E22 — montagem "do jeito certo" antes da PCB

2026-10-13. Depois do teste de domingo (2026-10-11), com sinal ruim na estrada e a faixa
"N REINICIOS SEGUIDOS - CONFERIR O 5V DO RADIO" nas duas telas.

> **A lista definitiva de peças, o desenho e a ordem dos testes estão em `LISTA_FINAL.md`.** Ela
> corrige este guia em seis pontos:
> - filtros do lado da tela;
> - conversor da tela de 60 V;
> - filtros no GPS;
> - capacitores SMD;
> - conectores com trava;
> - caixa de alumínio.

Este guia junta três estudos, que estão completos, com conta e fonte, em `estudos/`:

| Estudo | Assunto |
|---|---|
| `A_reinicios_e_ruido.md` | forense dos reinícios e do ruído, com os logs de bancada e de 2026-09-30 |
| `B_alimentacao_e_terra.md` | alimentação no carro, terra, desacoplamento, protoboard, critérios de aceitação |
| `C_antena_e_alcance.md` | orçamento de enlace, antena, cabo, teste de alcance |

Procedência dos números: **[M]** medido nos nossos logs, **[D]** datasheet ou norma,
**[E]** estimativa ou conta.

---

## 1. O que os dados dizem

### 1.1. O alcance ruim é ruído, não potência

| | Ruído de fundo (RSSI em slot livre) | Pacote mais fraco que passou |
|---|---|---|
| No carro, 09-30 (gino; FaustoA boot 262) | **−88 a −96 dBm** [M] | −95 a −105 dBm [M] |
| Bancada, 10-08 | −83 a −88 dBm [M] | — |
| O que o SX1262 deveria ver | ~−117 dBm [D/E] | ~−125 dBm (−129 de sensibilidade) [D] |

- O ruído foi conferido por dois caminhos independentes, que concordam em 1–3 dB [M]:
  - o RSSI em slot vazio;
  - RSSI − SNR de cada pacote.
- **O "−128 dBm" que aparecia não é ruído.** É o byte 0xFF que o chip devolve quando está
  sem 5 V. Só aparece em boots com o E22 desligado [M].
- **O receptor perde ~25–30 dB**, equivalentes a um alcance **4–5× menor** [E, expoente medido
  n ≈ 3,7]. Com piso de −88 a conta dá ~420 m, e o medido em 09-30 foi "nada acima de ~400 m".
- **Na FaustoA, boot 261, o ruído caiu junto com o sinal** (−113, e ela ouvia o gino 32 dB mais
  fraco do que ele a ouvia). Isso aponta para uma única fonte: **o ruído entra pela antena** [M + E].
- A fonte provável é o **próprio aparelho**: painel DSI, backlight, P4, step-down. Isso é
  **[E], ainda não provado**, e o teste que prova está na seção 5.

### 1.2. Os reinícios acontecem durante o TX

- As 4 caixas-pretas de pânico/WDT registraram **TX em voo** (24–192 ms) [M].
- O "relógio voltando" é sempre em múltiplos exatos de 2²⁶ ticks do SYSTIMER. É corrupção de
  hardware, não conta errada: **10 de 18** saltos aconteceram com TX no ar, contra 1–3 que
  seriam esperados por acaso [M/E].
- Depende da **unidade** e do **contato**:
  - a FaustoA com o conector P1/P3 frouxo (10-06);
  - a gino a 14 dBm (10-08), com a FaustoA limpa na mesma mesa;
  - 0 eventos a 10 dBm em ~200 unidade·min [M].
- Melhor encaixe, **[E]** (revisado em `estudos/E_revisao_adversaria.md`): **a RF da antena
  entrando na tela**. A 10–20 cm da antena o campo é de ~11–22 V/m, e o chicote de 15 cm é ≈ λ/2
  em 915 MHz.
  - O **retorno de terra pelo chicote** dá só 4–12 mV a 10 dBm, uns 40× abaixo do que seria
    preciso para causar efeito. **Fica como hipótese secundária.**
  - Mau contato é um gatilho medido.
- **E22 "voltando de fábrica" em loop não volta com o reset, só cortando a energia.** É o chip
  mudo, lendo 0x00 pelo SPI. Tem cara de *latch-up* (injeção pelos pinos) [E].

### 1.3. A faixa "CONFERIR O 5V" não prova que foi o 5 V do rádio

`app/boot.h:99` acende depois de **3 reinícios violentos seguidos** (PANIC, WDT, BROWNOUT,
PWR_GLITCH, ...) e só apaga desligando a energia, o que explica o "reiniciava e voltava".

BROWNOUT e PWR_GLITCH são do **3,3 V da própria tela**: um isqueiro com mau contato na estrada
de terra também acende a faixa. **O SD de domingo diz qual foi** (seção 2).

---

## 2. Passo 0 — os cartões de domingo (grátis, faça primeiro)

Copie a pasta `log/` dos dois cartões para `dados_campo/<tela>_2026-10-11/` e rode:

```
python -I tools/campo/resumo_diario.py dados_campo/fausto_2026-10-11 dados_campo/gino_2026-10-11
```

| O script mostra | Significa |
|---|---|
| motivo **BROWNOUT / PWR_GLITCH** | a alimentação **da tela** (isqueiro, cabo USB) |
| **PANIC / INT_WDT com "tx em voo"** na caixa-preta | o rádio derrubando a tela (seção 1.2) |
| **TASK_WDT** na etapa de TX | o rádio travou (BUSY) |
| `tx preso` com err **0x0000** | corte por corrente: 5 V do módulo afundou |
| `tx preso` com err **0x0020** | sem 5 V / TCXO |
| `N` em −128 | E22 sem 5 V naquele boot |
| `N` em −85..−95 | o ruído da seção 1.1 |
| RSSI/SNR e entrega por distância | o alcance real de domingo |

### 2.1. Resultado de domingo (2026-10-11, v202, 10 dBm) [M]

Resumo completo em `dados_campo/resumo_2026-10-11.txt`.

| | FaustoA | gino |
|---|---|---|
| Reinícios violentos | 4 (INT_WDT) | 6 (4 PANIC, 2 WDT) |
| Com TX em voo | **4 de 4** | **6 de 6** |
| BROWNOUT, TX preso | 0, 0 | 0, 0 |
| Relógio voltou (múltiplos de 2²⁶) | 4 | 23 |
| Ruído de fundo (mediana) | −87 dBm | −89 dBm |

- A faixa "CONFERIR O 5V" acendeu nas duas, mas **o 5 V não caiu**. **O TX derruba o P4 já em
  10 dBm** no carro.
- **Entrega por distância** (nos dois sentidos, praticamente igual):

| Distância | Entrega |
|---|---|
| 0–100 m | ~90 % |
| 100–250 m | ~70 % |
| 250–400 m | ~37 % |
| 400–650 m | 11–16 % |
| > 650 m | ≤ 7 % |

  O pacote mais fraco que passou chegou a ~−100 dBm, como o piso de −87/−89 prevê.

---

## 3. A base: protoboard de molas **não**

A protoboard comum soma **0,15–0,4 Ω intermitentes** no caminho do 5 V (4–6 contatos de mola
mais jumpers) [E, estudo B §4.1]. **É exatamente o defeito que estamos caçando.**

| Parte | Onde vai |
|---|---|
| 12 V, conversores, **5 V e GND do E22**, capacitores | **soldado**, fio curto e grosso |
| Sinais (SPI, BUSY, DIO1, NRST, TXEN, RXEN) | podem ir na protoboard ou no chicote PH; melhor soldado também |
| **Base recomendada** | **placa cobreada virgem como plano de terra** ("Manhattan"): o cobre inteiro é o GND, os GND do E22 soldam direto nele (é o que a Ebyte pede) e os outros pontos viram ilhas coladas |
| Segunda opção | placa perfurada **ilhada** com um barramento de fio de cobre nu AWG 16–18 como terra |
| Evitar | placa de tiras (*stripboard*): as tiras viram antenas e indutâncias |

Fixe o E22 com fita dupla-face, etiqueta para cima. Cada pad ligado com fio rígido de **≤ 5 mm**.

---

## 4. Alimentação e terra

### 4.1. A topologia

```
fusível ACC (derivador) ─ F1 2A ─┬─ D1 TVS 1.5KE33CA ─ GND★
                                 └─ D2 SB560 ─ L1 10–22µH ─┬─ 100µF/50V ∥ 1µF ∥ 100nF ─ GND★
                                                           │
                         ┌─────────────────────────────────┴──────────────────┐
                 CONVERSOR A (tela)                                  CONVERSOR B (rádio)
                 entrada 8–40 V → 5 V 3 A                            TPS54560 / LM2596HV (60 V) → 5,05 V
                         │  USB-C "5 A", 30–50 cm                             │ ferrite 600 Ω (ou 2,2 µH)
                      TELA P4                                                 │ par trançado AWG 20, ≤ 30 cm
                         │                                     C1 220–470 µF ∥ C2 10 µF ∥ C3 100 nF  (≤ 5–15 mm dos pads)
                         │                                           E22 pads 9/10 (VCC)  ·  GND no plano ─ GND★
                         └── chicote P1/P3 (sinais + 3 GND como REFERÊNCIA, não retorno)

★ = UM ponto físico só. O GND do carro chega ali por um fio AWG 18.
```

**Por quê:**

- **Uma entrada e um ponto de terra só.** Hoje são dois terras independentes até o carro (o do
  isqueiro e o do step-down), e o laço fecha pelos GND do chicote. Na partida, ~0,3 V entre
  pontos do chassi empurram corrente pelos fios de sinal [E, estudo B §1].
- **Dois conversores, não um.** O pulso do PA não pode cair no nó do backlight e do regulador
  da tela.
- **Conversor de 60 V.** O carro tem partida a 6 V, *jump start* de 24 V e *load dump*
  (ISO 16750-2) [D]. **Mini560 e MP1584 morrem; o LM2596 comum desliga na partida.**
- **O TVS não absorve *load dump*:** são dezenas de joules, contra 1,5–5 J do componente. Quem
  segura é o conversor de 60 V; o TVS pega os picos rápidos [D/E].
- **O 5 V da tela não alimenta o E22 por nenhum caminho.** Nem CAN/RS485 (sem proteção nenhuma),
  nem BAT/J4 (entrada de célula 1S).
- **Isqueiro só para teste, e com plugue de rosca.** A mola mais a vibração geram microcortes,
  e cada um vira um BROWNOUT na tela.

### 4.2. Os capacitores do E22: o lugar importa mais que o valor

- Fio e trilha têm **~1 nH/mm**. O cerâmico de 10 µF + 100 nF tem de ficar a **≤ 5 mm** dos
  pads 9/10 e 11. A 20 mm, o pico de chaveamento já passa de 1 V [E].
- **Seu 470 µF serve**, se for **eletrolítico comum** (não polímero) e ficar a **≤ 15 mm** dos
  pads. Junto do step-down, longe do módulo, ele não faz nada.
- Nenhum capacitor segura o pacote inteiro (200–420 ms): seriam 0,5 F para 0,1 V. Ele cobre só
  o atraso do conversor (~30–60 mV). **O resto é o fio**: AWG 20, curto, trançado com o GND [E].
- **Nunca monte sem o eletrolítico.** O ESR dele amortece o pico de plugar o cabo; só com
  cerâmicos o pico chega a 7–9,5 V e queima o E22 (máx. 5,5 V) [E, simulação do ESTUDO_PECAS].

---

## 5. Os sinais: proteção contra o *latch-up*

Nove resistores e dois capacitores que custam centavos e cortam a injeção de corrente pelos
pinos. Valem quando um lado está ligado e o outro não, e quando o terra pula no TX.

| Sinal | GPIO (P4) | Conector da tela | Pad do E22 | Peça |
|---|---|---|---|---|
| SCK | 30 | P3-4 | 18 | **330 Ω** em série |
| MOSI | 29 | P3-5 | 17 | **330 Ω** em série |
| NSS | 28 | P3-6 | 19 | **330 Ω** em série |
| MISO | 31 | P3-3 | 16 | **330 Ω** em série, do lado do E22 |
| NRST | 51 | P1-2 | 15 | **1 kΩ** em série (sem pull-up: o chip tem ~50 kΩ interno e aguenta 3,9 V) |
| TXEN | 52 | P1-1 | 7 | **1 kΩ** em série + **10 kΩ ao GND do lado do E22** (obrigatório: liga o buck do PA) |
| RXEN | 5 | P3-7 | 6 | **1 kΩ** em série + **10 kΩ ao GND do lado do E22** (o R2 volta: o GPIO5 flutua até o firmware) |
| BUSY | 49 | P1-4 | 14 | **1 kΩ** em série + **100 pF C0G ao GND do lado da tela** |
| DIO1 | 50 | P1-3 | 13 | **1 kΩ** em série + **100 pF C0G ao GND do lado da tela** |
| GND | — | **P1-8, P1-11, P3-11** (os três) | 1–5, 11, 12, 20, 22 | referência, no plano |

- O SPI roda a **2 MHz** (`hal/radio.h`). 330 Ω × ~25 pF ≈ 8 ns, contra 500 ns de período: o
  SPI nem percebe.
- O TXEN com 1 kΩ e 10 kΩ fica em 3,0 V, acima do limiar do EN do PA (≤ 1,2 V) [D].
- O 100 pF no BUSY/DIO1 corta a RF de 915 MHz que o cabo de 15 cm (≈ λ/2) capta.
- **Ordem de ligar na bancada:** antena (ou carga) → 5 V do E22 → tela. Para desligar, a tela
  primeiro. **No carro, as duas juntas, pelo ACC.**
- **GPS:** como hoje (P3-8 TXD, P3-9 RXD, P3-10 PPS, P3-11 GND, P3-12 3V3).
- **Cruze o par do 5 V com o chicote a 90°.** Nunca o deixe correndo em paralelo.

---

## 6. Antena

| Mudança | Ganho [E] | Alcance |
|---|---|---|
| Resolver o ruído (seção 7, teste 1) | ~25 dB | **×4–5** |
| **Antena no centro do teto**, fora do carro | 15–25 dB (lataria + altura) | **×2,5–4,7** |
| 20 → 24 dBm (`p14`) | 4 dB | ×1,3 |
| SF10 | 2,5 dB | **não cabe**: o pacote de 68 B leva 739 ms, mais que o slot de 500 ms |

- A antena no teto também **afasta a fonte de ruído** (seção 1.1) e tira a RF de cima da tela
  (seção 1.2): **resolve três problemas de uma vez**.
- **O que comprar:**
  - **Tipo:** 1/4 ou 5/8 de onda, ou colinear de **no máximo 5 dBi**. De 8–9 dBi não: o lobo fica
    estreito demais para morro e vale.
  - **Base magnética**, com o teto servindo de plano de terra.
  - **Cabo:** **≤ 3 m de RG58, LMR-200 ou CFD-200** (~1 dB). O RG174 perde 0,6–0,85 dB/m; se a
    antena vier com ele, use no máximo ~1,5–2 m.
  - **Conector:** SMA **macho** de verdade. RP-SMA rosqueia, mas não liga o pino central.
- **Do E22 até a caixa:** pigtail **IPEX → SMA de painel, ≤ 15 cm**, colado e amarrado. Ou coaxial
  RG316 soldado no pad 21, com alívio de tensão. **Nunca o IPEX e o pad 21 ao mesmo tempo.**
- **Confira a antena:** SWR < 2 em 915 MHz num NanoVNA, **medido com a antena no teto**. Antena
  "915" de loja às vezes é de 868 MHz.
- **Na bancada:** **no máximo −20 dBm** entrando na outra antena. O LNA na frente do chip satura e
  pode danificar: a 1 m, 24 dBm já chegam a ~+6 dBm no chip [E]. Use `p-9`, rádios a ≥ 3 m ou
  um atenuador de 30 dB. A 1 m, as leituras não servem para comparar antenas.

---

## 7. Roteiro de testes (uma variável por vez)

**Instrumentos:**

| Instrumento | Uso |
|---|---|
| Multímetro com **MIN/MAX** | o pacote SF9 dura ~0,3 s, e o MIN pega a queda |
| Medidor USB-C em linha (FNIRSI FNB48 ou similar) | consumo e tensão da tela |
| **Carga de 50 Ω SMA** e **atenuador de 30 dB / ≥ 2 W** | testes 1 e 4 |
| Termômetro IR | teste 6 |
| Osciloscópio ≥ 20 MHz | desejável: ripple e diferença de terra |

### Teste 1 — de onde vem o ruído (o mais importante)

- **Método:** leia `ruido=` no status serial (comando `d`), mediana de ≥ 30 amostras. Mude
  **uma coisa por vez**.

| Passo | Resultado | Conclusão |
|---|---|---|
| a. **Carga de 50 Ω no lugar da antena** | carga em ~−113 e antena em ~−85 | o ruído é **irradiado**: entra pela antena |
| | carga também em ~−85 | o ruído é **conduzido**: 5 V ou terra |
| b. Antena a 1–2 m da tela, por coaxial | o ruído cai | a fonte é a tela |
| c. Backlight no mínimo; tela parada × mapa repintando | — | isola backlight e painel |
| d. E22 numa bateria ou fonte linear, sem step-down | o ruído cai | é o step-down |
| e. GPS desligado, USB do PC desconectado, ferrite no cabo PH | — | elimina o resto |

- **Meta:** piso ≤ −110 dBm, primeiro na bancada e depois no carro com o motor ligado.

### Teste 2 — tensão

| # | Medida | Critério |
|---|---|---|
| 2a | VCC nos pads 9/10, rádio em RX | 5,00–5,15 V |
| 2b | VCC mínimo (MIN/MAX) a 14 dBm, 10 min | ≥ 4,90 V |
| 2c | Diferença DC entre o GND da tela (carcaça do USB-C) e o pad 11 do E22, durante o TX | ≤ 10 mV (> 20 mV = retorno passando pelo chicote) |
| 2d | Pico ao plugar o 5 V a quente | ≤ 5,4 V |
| 2e | Corrente do E22 em RX | 10–20 mA. **Acima de 50 mA, ou subindo com o módulo quente = latch-up** |

### Teste 3 — partida simulada

- **Método:** com fonte de bancada, 12 → 6 V por 1 s → 14 V.
- **Critério:** **sem PANIC**. Se a tela reiniciar, o motivo tem de ser POWERON ou BROWNOUT, e o
  rádio tem de voltar **sozinho**, sem desligar a energia.

### Teste 4 — margem de potência

- **Método:** 14 dBm, depois **18 dBm na carga de 50 Ω**, 10 min cada.
- **Critério:** `preso=0`, `relogioVoltou=0`, nenhum reinício, VCC ≥ 4,80 V.
- **Interpretação:**
  - passa na carga e falha com antena = é **RF**, e a antena vai para longe da tela;
  - falha nos dois = é **alimentação ou terra**: volte ao teste 2.

### Teste 5 — duas telas, 1 hora

- **Método:** ciclo real a 14 dBm, rádios a ≥ 3 m ou com atenuador.
- **Critério:** `preso=0`, 0 reinício violento, entrega ≥ 97 %.

### Teste 6 — temperatura

- **Método:** 30 min a 24 dBm no ciclo real.
- **Critério:** blindagem do E22 **≤ 20 °C acima do ambiente**. No sol, o carro chega a 60–70 °C,
  e o limite do módulo é 85 °C [D].

### Teste 7 — alcance na estrada

- **Método:**
  - estrada reta de ~2 km;
  - carro A parado com o motor ligado;
  - carro B para 60 s em 100, 200, 300, 500, 700, 1000, 1500 e 2000 m;
  - piso de ruído anotado antes e depois.
- **Uma rodada por mudança:**
  - R1 = como em 09-30 (base);
  - R2 = ruído resolvido;
  - R3 = antena no teto;
  - R4 = `p14`.
- **Como comparar:** pelos **dB ganhos na mesma distância** (ajuste `RSSI = A − 10n·log d`), não
  por "até onde chegou". O cartão já registra RSSI, SNR, posição e piso.
- **Se a entrega cair com SNR ainda acima de −5 dB, o problema não é RF.**

---

## 8. Lista de material (por carro)

| Item | Especificação | Alternativa |
|---|---|---|
| Base | placa **cobreada virgem** 10×15 cm (plano de terra) | perfurada ilhada + barramento de cobre nu AWG 16–18 |
| Entrada 12 V | derivador de fusível (*fuse tap*) num circuito **ACC** | tomada 12 V com rosca (só teste) |
| F1 | fusível de lâmina **2 A** + porta-fusível | — |
| D1 | TVS **1.5KE33CA** (bidirecional) | SMCJ33CA, P6KE33CA |
| D2 | Schottky **SB560 / SR560** (60 V, 5 A) | P-FET + zener (estudo B §2.3). **1N5822 não serve** (40 V) |
| L1 | indutor **10–22 µH, ≥ 3 A** | o de 33 µH de um LM2596 |
| Capacitores de entrada | **100 µF / 50 V** 105 °C + 1 µF + 100 nF / 50 V | — |
| Conversor A (tela) | **12/24 V → 5 V 3 A, entrada 8–40 V**, selado | carregador veicular USB-C de marca, cabeado |
| Cabo da tela | USB-C **5 A**, 30–50 cm | — |
| Conversor B (rádio) | módulo **TPS54560** (5–60 V) ajustado para **5,05 V** | **LM2596HV** (60 V). **Não usar** Mini560, MP1584 nem LM2596 comum |
| Filtro do E22 | ferrite **600 Ω @ 100 MHz, 2 A** (BLM31PG601SN1) | indutor blindado 2,2 µH / 2 A |
| C1 (nos pads) | eletrolítico **comum** 220 µF / 16 V 105 °C (EEU-FR1C221) | o **470 µF** atual, se comum, a ≤ 15 mm |
| C2, C3 | **10 µF X7R 25 V** + **100 nF X7R 50 V**, terminais ≤ 5 mm | 2× 4,7 µF X7R |
| Resistores | **330 Ω** ×4, **1 kΩ** ×5, **10 kΩ** ×2 (1/4 W) | — |
| C de RF | **100 pF C0G** ×2 | — |
| Fio do 5 V do rádio | silicone **AWG 20**, par trançado, ≤ 30 cm | AWG 22 com ≤ 20 cm |
| Fio do 12 V | AWG 18 | — |
| Conectores de potência | **XT30** (5 V), borne de parafuso (12 V) | solda direta |
| Antena | 915 MHz, 1/4 ou 5/8 de onda, ≤ 5 dBi, base magnética, SMA macho | — |
| Coaxial | ≤ 3 m de RG58, LMR-200 ou CFD-200 | RG174 com ≤ 2 m |
| Pigtail | IPEX → SMA de painel, ≤ 15 cm | RG316 soldado no pad 21 |
| Bancada (uma unidade só) | carga 50 Ω SMA, atenuador 30 dB / 2 W, multímetro com MIN/MAX, FNB48, termômetro IR | NanoVNA para a antena |

---

## 9. O que vai para a PCB depois

Tudo o que passar nos testes vira desenho:
- o plano de terra contínuo debaixo do E22;
- o banco a ≤ 5 mm;
- os 9 resistores e 2 capacitores dos sinais;
- os pull-downs de TXEN e RXEN;
- o filtro de ferrite;
- o ponto estrela.

A placa atual (`hardware/placa_radio/`) **não tem os resistores em série, o RXEN pull-down nem o
filtro**, e precisa ser revista depois destes testes.

## 10. Correções feitas e pendentes nos documentos

- `docs/MONTAGEM.md`, Passo 3: mandava alimentar o E22 pelo pino 1 do CAN/RS485. **Corrigido**
  para "conversor próprio".
- **Pendentes:**
  - `docs/MONTAGEM.md`, Passo 6: a ordem do `setCurrentLimit` ("depois do `setOutputPower`")
    contradiz o `CLAUDE.md` ("depois do `begin()`").
  - `PLANO_7B` (5KP26A) e `placa_radio_smt` (SMAJ18A "grampeia surtos"): o TVS não segura
    *load dump*.
  - O ganho do PA tem duas fontes que discordam: +10 dB pela tabela da Ebyte, +7 dB pela medida
    de terceiros (S5NC). A diferença muda se `p14` dá 24 ou 21 dBm na antena. Medir com um
    medidor de potência resolve.
- **Firmware, sugestões sem nada aplicado:**
  - descartar a amostra 0xFF (−128) do ruído;
  - separar "chip mudo" de "voltou de fábrica" no contador;
  - com o rádio sem responder, deixar NSS, NRST, TXEN, RXEN e SPI como entrada entre as
    tentativas.
