# Estudo — as peças em volta do E22 na placa do rádio

2026-10-01. Pergunta: *por que a placa tem C1, C2, C3, R1 e R2, se o chicote de hoje
funciona sem quase nada disso?* E o que mais deveria ter, ou não ter.

Procedência de cada número: **[M]** manual/datasheet lido nesta pesquisa · **[P]** projeto
aberto, esquemático lido · **[S]** simulação numérica nossa · **[E]** estimativa.

## 0. O ponto de partida

O chicote atual tem **só o pull-down de 10 kΩ no TXEN** (`firmware/mts/hal/radio.h`,
comentário M16) e, na medição da gino em setembro, **nenhum capacitor no módulo** — e ficou
estável a 14 dBm com o step-down. Funcionar sem as peças não prova que elas sobram: prova que
a fonte e o cabo daquele dia estavam bons.

## 1. O que os projetos que funcionam fazem

Oito esquemáticos abertos com o E22-900M30S, lidos no arquivo (MeshAdv Pi Hat v1.1 e v2,
MeshAdventurer V1 e S3, MeshPoE-S3, Ikoka Nano e Stick, Ebytastic) e a figura do manual da
Ebyte [P][M]:

| Item | O que se vê |
|---|---|
| Capacitor no VCC | **100 µF eletrolítico + 10 µF cerâmico** é o mais comum; dois usam só cerâmicos (2×47 + 22 µF); um tem filtro LC (15 µH) |
| Pull-down em TXEN/RXEN | **nenhum dos oito** |
| Resistor em série / pull em NSS, NRST, SPI | **nenhum** |
| Fusível / TVS | um só (PPTC 2 A + SMBJ5.0A, MeshAdv v2) |
| Diodo de polaridade, ferrite, LDO depois do buck | **nenhum** |
| Antena | pino 21 → SMA na borda (6 de 8); IPEX (2) |
| Cobre debaixo do módulo | o manual Ebyte pede: *"top layer under the module all copper-covered and well grounded"* |

**A diferença que muda tudo:** em todos eles o rádio e o processador estão **na mesma placa,
na mesma alimentação**. Os nossos têm **15 cm de cabo** e **fontes separadas** — a tela liga
sem o rádio e o rádio sem a tela. Nenhum dos oito trata esse caso, então "fazer igual a todo
mundo" não cobre a gente.

## 2. Peça por peça

### C1 — 220 µF eletrolítico. **Fica. Trocar a especificação para 16 V.**

- No pico de TX, a fonte demora 10–100 µs para reagir [E]. Nesse intervalo quem segura é o
  capacitor. Com 220 µF e 1 A a queda fica em ~0,23 V; com a rampa do PA em 800 µs (o firmware
  já usa), ~0,05 V [S/E]. Só o capacitor sem o buck: 0,9 V com 220 µF, 2,0 V com 100 µF.
- **470 µF não compra quase nada** (0,43 V no pior caso), e acima disso quem manda é o fio:
  num pacote de 50 ms, segurar 0,1 V exigiria 0,5 F.
- **Ele também protege na hora de plugar.** Simulação do cabo entrando no conector [S]:

| No VCC | Pico de tensão ao plugar 5,3 V |
|---|---|
| eletrolítico 220 µF (ESR 0,13 Ω) | ≤ 5,30 V |
| polímero 220 µF (ESR 13 mΩ) | até 6,4 V |
| **só os cerâmicos** | **7,0–9,5 V** — acima dos 5,5 V que queimam o módulo [M] |

  O ESR "ruim" do eletrolítico é o que amortece o cabo. **Nunca monte sem o C1**, e não troque
  por polímero.
- **Peça:** Panasonic **EEU-FR1C221** (220 µF, **16 V**, 105 °C, 6,3 mm, passo 2,5) [M]. É a
  mesma lata e o mesmo preço do de 10 V: margem de graça. Nichicon HE ou Rubycon ZLH servem.
- **Na v3 (placa sem furo) o C1 é SMD:** eletrolítico de 220 µF / 16 V, lata de 6,3 mm, 105 °C
  (série Panasonic FK ou equivalente). O ESR do SMD é ainda maior que o do FR, então o
  amortecimento ao plugar continua garantido [E]. Um FR de furo também serve, com as pernas
  dobradas e soldadas por cima dos pads.

### C2 — 10 µF cerâmico. **Fica. Comprar X7R de 16 ou 25 V.**

- O eletrolítico é lento demais para as bordas rápidas do PA e para o chaveamento do
  step-down; o cerâmico é que responde ali.
- Cerâmico perde capacitância com tensão. A 5,3 V, um 1206 de 10 µF fica com 7,3–8,8 µF [M,
  curvas Samsung] — em 1206, tensão nominal maior **não** garante mais capacitância. A razão
  para 16–25 V é margem contra o pico de plugar, não capacitância.
- **Peça:** Murata **GRM31CR71C106KAC7L** (16 V) ou **GRM31CR71E106KA12L** (25 V). Evite 6,3 V.

### C3 — 100 nF cerâmico. **Fica.** X7R, 1206, 25 V ou mais. Pega o ruído de MHz.

### R1 e R2 — 10 kΩ no TXEN e no RXEN. **Ficam. São a peça mais importante deste estudo.**

- O datasheet do ESP32-P4 (v0.7, tabela 2-1) [M]: os GPIO **5 e 52** (RXEN e TXEN) ficam
  **sem pull nenhum, em alta impedância**, do reset até o firmware configurar. Nenhum dos oito
  projetos sofre disso porque lá o processador e o rádio ligam juntos e perto.
- O manual da Ebyte [M] dá a tabela (TXEN 1 = TX, RXEN 1 = RX, os dois 0 = desligado) e **não
  documenta pull interno** no módulo.
- Correção desta base: o aviso "os dois altos danificam" **não está no manual da Ebyte**. Vem da
  engenharia reversa da comunidade (repositório S5NC/CDEBYTE_Modules), que mediu 29–30 dB de
  isolamento no switch. É plausível, não é documentado.
- Com os dois resistores, os dois pinos ficam em 0 = rádio desligado, que é o estado seguro.
  Hoje só o TXEN tem.

## 3. O que este estudo muda

### 3.1. **Voltar com o cobre debaixo do E22** (feito)

Na mudança de hoje o cobre de terra saiu de baixo do módulo para não dar curto. O manual da
Ebyte pede o contrário: cobre de terra cheio debaixo dele [M]. E sem esse cobre o terra de cima
e o de baixo da placa só se ligam por dentro do módulo.

O risco de curto é só se o **fundo do E22** tiver metal exposto. O certo é **olhar o fundo de
um E22**: se for todo verniz (verde/preto, sem ponto dourado), volta o cobre e pronto. Se tiver
ponto exposto, isola só aquela área com uma camada de tinta (a mesma da laser) antes de soldar.

### 3.2. **Ajustar o step-down para 5,0–5,1 V** (sem mexer na placa)

O máximo do E22 é **5,5 V** [M]; 5,3 V deixa 0,2 V de margem. Step-down barato sobe ~20 % acima
do valor na partida, perto do limite de entrada [M, osciloscópio de terceiro] → ~6,4 V. Em
5,0 V o módulo ainda está na faixa de "potência plena" (≥ 5 V), e a 10–14 dBm a diferença nem
aparece. Medir **com o rádio ligado**.

**O risco que sobra:** o transistor do step-down entrar em curto e jogar 12–14 V no rádio. Só um
LDO alimentado por uma tensão mais alta (TPS7A4501) ou um crowbar protege disso. Ficam para a
próxima versão; ver 3.5.

### 3.3. **Firmware: não empurrar corrente num rádio desligado**

Quando o rádio está sem 5 V e a tela ligada (aconteceu várias vezes na bancada), o firmware
segura o NSS em alto e pulsa o NRST a cada nova tentativa. A corrente passa pelos diodos de
proteção do SX1262 e alimenta o chip "por trás" [inferido de M; nem o SX1262 nem o ESP32-P4
publicam limite para pino com o chip desligado]. Nada queimou até hoje, mas é estresse sem
necessidade.

Correção barata, **no `firmware/mts`**: com o rádio sem responder, deixar NSS, NRST, TXEN, RXEN
e SPI como entrada entre as tentativas.

O caminho de hardware seria um resistor em série em cada linha: 1 kΩ nas lentas e 330–470 Ω no
SPI, que a 2 MHz cabe com folga. São 9 resistores numa placa de face simples já cheia, e nenhum
projeto aberto usa. Fica para a próxima versão.

### 3.4. O que **não** precisa

- **Pull-up no NSS:** o firmware reseta o rádio pelo NRST antes de qualquer uso
  (`radioHwChecaFios`), e o reset desfaz qualquer lixo do boot.
- **Pull-up no NRST:** o SX1262 já tem ~50 kΩ interno [M]. E **nunca** puxar para o VCC de 5 V,
  como a figura da Ebyte sugere: o chip aguenta 3,9 V [M].
- **TVS SMBJ5.0A no 5 V:** só começa a conduzir em 6,4 V e grampeia em 9,2 V [M]. Não protege um
  módulo de 5,5 V.
- **Polímero, NTC, resistor em série na alimentação:** pioram (ver C1) ou custam queda de tensão.
- **LDO a partir de 5,3 V:** sem folga (precisa de 1,2–1,5 V acima [M, AN2023005 tabela 1]). A
  própria AN2023005 aplica o LDO aos módulos de 3,3 V sem PA; para os de 1 W ela mostra um buck
  simples.

### 3.5. Para uma próxima versão da placa (não agora)

| Peça | Para quê | Custo |
|---|---|---|
| P-FET **AO3401A** (SOT-23) na entrada | polaridade invertida com ~60 mV de queda (o diodo Schottky perderia 0,4 V) | 1 peça + 1 resistor |
| Buck a 5,9 V + **TPS7A4501** a 5,0 V | o único que protege contra o buck em curto, e ainda filtra | 1 CI + 3 resistores, SOT-223 |
| Resistores em série nas 9 linhas | sem corrente para trás quando só um lado está ligado | 9 × 1206 |

## 4. Fora da placa, mas obrigatório

- **Fusível de 2–3 A no 12 V**, antes do step-down.
- **Cabo de alimentação AWG 22** ou mais grosso. No pacote de 50 ms quem decide a queda é a
  resistência do fio, não os capacitores.
- **Descobrir o modelo do step-down.** MP1584 e Mini560 aguentam 28 V e ~20 V na entrada; o
  surto de *load dump* do carro passa disso.

## 5. Lista de material atualizada

| Ref | Antes | Agora |
|---|---|---|
| C1 | 220 µF / 10 V | **220 µF / 16 V, 105 °C**: na v3, SMD (série FK); de furo, EEU-FR1C221 |
| C2 | 10 µF 1206 | **10 µF 1206 X7R 16–25 V** (GRM31CR71C106KAC7L) |
| C3 | 100 nF 1206 | 100 nF 1206 X7R ≥ 25 V |
| R1, R2 | 10 kΩ 1206 | sem mudança |

## Fontes

- Ebyte, *E22-M Series User Manual* V1.5 (2026-09-18), §2.2.1, §3.3, §4.1, §4.2 tab. 1, §5.1;
  *E22-900M30S User Manual* v1.20; nota de aplicação AN2023005.
- Semtech DS.SX1261-2 Rev 1.1: tabelas 3-1, 3-2, 3-10, 8-3; §8.1–8.3. A Rev 2.x não foi lida.
- Espressif, *ESP32-P4 Series Datasheet* v0.7, tabelas 2-1, 2-3, 5-1, 5-4; *Hardware Design
  Guidelines* §1.3.7, §1.3.9, tab. 6; ESP-IDF v5.5 `gpio.c`; arduino-esp32 `esp32-hal-gpio.c`.
- Esquemáticos: chrismyers2000/MeshAdv-Pi-Hat, KaliAssistant/MeshAdv_piHAT_v2,
  chrismyers2000/MeshAdventurer, deuxyeux/MeshAdventurer-S3 e MeshPoE-S3,
  ndoo/ikoka-nano-meshtastic-device, ndoo/ikoka-stick-meshtastic-device, Sobollion/Ebytastic.
- Panasonic série FR; curvas de DC-bias Samsung CL31A106MPHNNNE, CL31B106KBHNNNE,
  CL31A106KBHNNNE; onsemi NCP361; AOS AO3401A; TI TPS7A45; Littelfuse 1206L110/16.
- Engenharia reversa do switch: S5NC/CDEBYTE_Modules.

## 6. Segundo estudo: o mínimo (2026-10-01, depois da pergunta "não dá para usar sem?")

Duas fontes novas mudaram o quadro:

**O que há dentro do E22** (única desmontagem publicada, S5NC/CDEBYTE_Modules, blindagem tirada
e pad a pad medido):
- **O TXEN liga o EN de um buck TI TLV62568, que alimenta o PA.** O datasheet da TI: *"The EN
  input must be terminated and should not be left floating"*; sem pull interno; limiar de
  0,95–1,2 V. Relato de campo (ndoo, variante Meshtastic XIAO+E22): E22 **esquentando** porque o
  TXEN nunca foi a 0 e o PA ficou ligado. → **R1 é obrigatório.**
- **O RXEN liga o LDO do LNA** e o lado RX do switch. Solto, gasta energia; perigoso só com o
  TXEN alto junto, que o R1 já impede. → **R2 saiu.**
- **No VCC, dentro do módulo, só uns 4 cerâmicos pequenos** (leitura da foto; valores não
  medidos). Nada de eletrolítico. O manual Ebyte pede *"external ceramic filter capacitor"*.
  → **C2 fica; C3 (100 nF) saiu**: redundante com o C2 e com os cerâmicos internos.
- O switch de RF nunca foi identificado. Os "29–30 dB de isolamento" são do datasheet do
  PE4259 (o switch do desenho de referência da Semtech), **não uma medição** — o `CLAUDE.md`
  diz "mediu", e está errado.

**Os nossos dados de campo** (CHANGELOG, medido): sem nenhum capacitor, com o step-down, o rádio
ficou estável a 14 dBm e travou a 18 dBm (o módulo apaga no meio do TX e o watchdog reinicia
a tela). No carro em 2026-09-30, a 10 dBm, zero TX presos. → Sem capacitor funciona na nossa
potência, com ~4 dB de folga. **C1 fica**: é a folga, e é o que segura o pico ao plugar
(simulação da seção 2).

**Teste que decide o C1 de vez:** potência em 18 dBm (`p18` no serial), alguns minutos sem e
com 220 µF encostado no VCC do E22. Estável com e travando sem = provado.

Nenhuma placa queimou por falta de capacitor em relato documentado; também não achamos relato
detalhado de E22 rodando sem nenhum. A comunidade usa 100–220 µF + 10 µF.
