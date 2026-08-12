# Montagem — do zero aos dois carros se vendo

Passo a passo para montar um nó da rede: rádio E22 + tela ESP32-P4 + GPS.
Faça na ordem. Cada passo diz **o que fazer**, **como saber que deu certo** e só
avisa do perigo onde ele morde.

Detalhamento e fontes de cada número estão nos três estudos linkados no fim.

---

## Antes de começar

| Você tem | Falta comprar |
|---|---|
| 3× E22-900M30S (chegaram) | GPS **ATGM336H-5N-31** — 3× |
| 3× tela ESP32-P4 (1×5", 2×7") | Capacitor eletrolítico **≥470 µF / 10 V** — 3× |
| Antenas 3 dBi IPEX (vieram no kit) | Fio 26–28 AWG, ferro de ponta fina |
| | Filtro **SAW GNSS L1** — 3× (~US$ 2 cada) |

O ATGM336H substitui o NEO-7M porque **o NEO-7M não expõe o PPS no header**, e sem
PPS não há TDMA. Ele ainda vem com filtro SAW de fábrica, que é o que protege o GPS
do rádio de 1 W ao lado.

---

## Passo 0 — A placa de 5" já foi testada ✅

Em 2026-08-12 a tela de 5" rodou os dois sketches sem periférico nenhum, só o cabo
USB. **7 de 7 conferências bateram** com o que a pesquisa prometia, e o
`tdma_selftest` deu **61 PASS, 0 FAIL** no chip alvo.

```powershell
.\tools\build.ps1 firmware\p4_hello      -Board p4 -Upload -Port COM8
.\tools\build.ps1 firmware\tdma_selftest -Board p4 -Upload -Port COM8
```

Medido: ESP32-P4 rev 103, 2 núcleos a 360 MHz, **32 MB de PSRAM**, 32 MB de flash,
55 GPIOs sem lacuna, nenhum só-entrada, e os **12 pinos deste guia todos válidos** —
nenhum strapping, nenhum no banco do LDO.

**Duas armadilhas que custaram tempo:**

- **Cabo USB-C só de carga.** A tela acende, o LED de power acende, e o PC não vê
  nada. Se der isso, é o cabo — use o de passar arquivo do celular.
- **`PSRAM=enabled` no FQBN.** O padrão do core é *disabled*, e sem essa opção a
  PSRAM vem como **0 bytes**, sem erro nenhum. O `build.ps1 -Board p4` já corrige.

---

## Passo 1 — Descubra qual tela é qual

Sem ferramenta, só olhando:

| Tela | Conector | Chicote |
|---|---|---|
| **720 × 1280, em pé** (5" ou 7") | header 40 vias, 2,54 mm | **A** |
| **1024 × 600, deitada** (7B) | 2× JST-PH 2,0 de 12 vias | **B** |

As de 720×1280 têm header **idêntico pino a pino**, não importa o tamanho. Se as suas
três forem dessa família, você faz **um chicote só** e ele serve nas três.

> **Bom saber:** o número do GPIO está impresso no silkscreen ao lado de cada pino.
> Dá para conferir tudo sem consultar documento.

---

## Passo 2 — Solde o rádio (15 pontos)

Vista **de cima**, com o conector IPX no canto **inferior esquerdo**:

```
                    ┌──────────────────────┐
      12  GND ●─────┤                      ├─────● GND  11
      13 DIO1 ●─────┤                      ├─────● VCC  10
      14 BUSY ●─────┤                      ├─────● VCC   9
      15 NRST ●─────┤     E22-900M30S      ├─────○ DIO2   8   (pule)
      16 MISO ●─────┤                      ├─────● TXEN   7
      17 MOSI ●─────┤   SX1262 + YP2233W   ├─────● RXEN   6
      18  SCK ●─────┤                      ├─────○ GND    5   (pule)
      19  NSS ●─────┤                      ├─────○ GND    4   (pule)
                    │                      │
      20  GND ●─────┤                      ├─────○ GND    3   (pule)
      21  ANT ○─────┤                      ├─────○ GND    2   (pule)
      22  GND ●─────┤ [IPX]                ├─────○ GND    1   (pule)  ● ← pino 1
                    └──────────────────────┘

      ● solde     ○ pule
```

**Ordem:** lateral esquerda inteira, de cima para baixo (é uma sequência contínua,
você conta os pads uma vez só). Depois a direita.

**Como fazer:**

1. Estanhe os 15 pads **antes** de encostar qualquer fio. Ponta fina, ~320 °C, toque
   curto. Pads vizinhos ficam a 2,54 mm.
2. Fio 26–28 AWG, ~15 cm, todos do mesmo tamanho. Vermelho nos dois VCC, preto nos
   quatro GND.
3. Cola quente ou epóxi prendendo o feixe na borda — pad castelado descola com pouca
   força lateral.

**Confira antes de seguir:** multímetro em continuidade, cada fio contra os **dois
pads vizinhos**, não só contra o próprio. Curto entre adjacentes é o defeito nº 1 e
não aparece a olho nu. Ponte entre 9 (VCC) e 8 (DIO2) queima o módulo.

> **Por que nenhum dos 9 sinais é dispensável:** DIO1 é o TxDone (sem ele não existe
> TDMA — é o motivo de o E22 substituir o LoRaMESH). BUSY: o chip trava se receber
> comando enquanto processa. TXEN/RXEN: sem eles o PA e o LNA nunca ligam. SPI precisa
> dos 4. NRST é a única forma de destravar um rádio pendurado.

---

## Passo 3 — Monte o chicote

Mesmos GPIOs nas três placas. Mesmo firmware. Só o conector muda.

| Fio | GPIO | Chicote **A** (40 vias) | Chicote **B** (7B) |
|---|---|---|---|
| **RÁDIO** | | | |
| NSS | 28 | pino 15 | P3-6 |
| MOSI | 29 | pino 19 | P3-5 |
| SCK | 30 | pino 21 | P3-4 |
| MISO | 31 | pino 23 | P3-3 |
| BUSY | 49 | pino 31 | P1-4 |
| DIO1 | 50 | pino 33 | P1-3 |
| NRST | 51 | pino 35 | P1-2 |
| TXEN | 52 | pino 37 | P1-1 |
| RXEN | 5 | pino 12 | P3-7 |
| VCC (os dois) | — | **pinos 2 e 4 (5 V)** | **pino 1 do conector CAN** |
| GND (os quatro) | — | 6, 9, 14, 20 | P1-8, P1-11, P3-11 |
| **GPS** | | | |
| TXD do GPS → | 4 | pino 13 | P3-8 |
| ← RXD do GPS | 3 | pino 11 | P3-9 |
| PPS | 2 | pino 7 | P3-10 |
| VCC do GPS | — | **pino 1 ou 17 (3,3 V)** | P1-10 ou P3-12 |
| GND do GPS | — | qualquer GND | P1-8 |

**Mais duas coisas no chicote:**

- **Pull-down de 10 kΩ entre o GPIO 52 (TXEN) e o GND**, do lado do rádio. Sem ele o
  PA pode acordar transmitindo antes de o firmware subir, com a antena ainda fora.
- **O capacitor de ≥470 µF na ponta do rádio**, junto dos pads 9/10 — não na placa.
  Ele combate a indutância do fio do chicote; na placa não faz o trabalho.

> ### ⚠️ Não use os GPIO 46, 47 e 48
> Eles estão no banco `VDD_IO_5`, que a placa alimenta pelo **LDO interno** — o mesmo
> que comuta o cartão SD entre 3,3 V e 1,8 V. Se o cartão mudar de tensão, esses três
> pinos vão junto, com o seu rádio pendurado neles.
>
> **Na 7B isso é uma armadilha física:** no conector P1, os pinos 1 a 4 estão em 3,3 V
> fixo, mas os **pinos 5, 6 e 7 estão no banco do LDO**. São vizinhos, no mesmo
> conector, e nada os distingue a olho nu. **Deixe as posições 5, 6 e 7 do P1 vazias.**

Também não toque em: **GPIO 35** (botão BOOT), **34/36/37/38** (strapping e console),
**39–45** (cartão SD), **7/8** (toque), **14–19** (Wi-Fi).

---

## Passo 4 — Ligue o GPS

Quatro fios: VCC (3,3 V), GND, TXD, RXD — mais o **PPS**, que é o quinto e o que
importa.

- O ATGM336H é **3,3 V**. Os pinos digitais dele **não toleram 5 V**.
- O PPS sai direto num pino rotulado do header. Sem solda fina, sem loteria.
- Ligação direta no GPIO, sem level shifter e sem pull-up. Coloque um resistor de
  **100–330 Ω** em série por precaução — limita a corrente num curto acidental e
  amortece ringing.

> ### ⚠️ Separe a antena do GPS da antena do rádio
> Com 1 W a **30 cm** você acopla +13,8 dBm na entrada de RF do GPS — que é o **limite
> absoluto de dano**, não só de interferência.
>
> **Regra: mínimo 30 cm, alvo 50 cm ou mais.** Chicote LoRa baixo e atrás, patch GPS
> alto e à frente, lataria do carro entre os dois. E não passe os dois cabos no mesmo
> feixe.
>
> O harmônico do LoRa **não** é o problema (2×915 = 1830 MHz, longe dos 1575 do GPS).
> O problema é a potência bruta saturando o front-end. Por isso o filtro SAW resolve —
> e o ATGM336H-5N-31 já vem com um.

---

## Passo 5 — Antes de energizar

Quatro itens. Nenhum é opcional.

- [ ] **Antena no IPX do rádio.** Transmitir 1 W sem antena destrói o PA. E vale toda
      vez, não só na primeira.
- [ ] **Capacitor de 470 µF** nos pads 9/10.
- [ ] **Nenhum sinal de 5 V** encostando em SCK, MOSI, NSS, NRST, TXEN ou RXEN. VCC
      aceita 5 V; a lógica é 3,3 V.
- [ ] **Fonte de 5 V que dê 620 mA contínuos.** O capacitor cobre 0,23 ms de um
      pacote de 51 ms — os outros 99,5 % saem da fonte. Uma porta USB dividida com o
      console não sustenta.

> **Por que "antena antes" não tem margem:** o SX1262 tem proteção contra descasamento,
> mas o estágio protegido **não é o que está ligado na antena** — entre ele e o conector
> está o PA YP2233W, cujo datasheet não menciona VSWR em 23 páginas. Não é que o limite
> seja apertado: é que ninguém sabe qual é.

---

## Passo 6 — O rádio acorda?

Grave o `firmware/e22_ping` e abra o serial.

**Deu certo:** `begin()` retorna 0 e o log mostra a frequência e a potência.

**Deu erro −707:** é o TCXO. Ordem de tentativa:

1. `RF_TCXO 2.2f` ← **comece aqui.** É o valor do manual E22-M V1.2 (2026)
2. `1.8f` — valor do manual antigo de 2018
3. `1.6f` — o default do RadioLib, o pior dos três

**Não** use `radio.XTAL = true`. Esse flag diz "não há TCXO, é cristal passivo" — e o
E22-900M30S **tem** TCXO. Não é fallback, é descrever o hardware errado.

### A linha que vale 10 dB

```cpp
radio.begin(915.0, 125.0, 7, 5, 0x12, 22, 8, 2.2, false);
radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);
radio.setDio2AsRfSwitch(false);   // begin() liga isso sozinho; desfazer
radio.setOutputPower(22);          // maximo do SX1262; +7,25 dB do PA = 29,3 dBm
radio.setCurrentLimit(140.0);      // <-- DEPOIS do setOutputPower. Ver abaixo.
radio.setRxBoostedGainMode(true);  // as sensibilidades do datasheet sao as boosted
```

`setCurrentLimit(140)` é a diferença entre **29,4 dBm e 19,6 dBm** (medido). O
`begin()` do RadioLib deixa o limite em 60 mA, o chip corta a corrente e a potência
despenca — **sem erro, sem aviso, sem nada no log**. E tem que vir **depois** de
`setOutputPower()`, porque este reescreve o registrador.

Sem essa linha você vai a campo com um décimo da potência e conclui que o projeto não
fecha.

---

## Passo 7 — Ping-pong entre dois nós

Dois rádios, dois nós, `firmware/e22_ping` nos dois com IDs diferentes.

**Deu certo:** RSSI e SNR aparecendo dos dois lados, e o log informa o tempo real de
TX medido.

> ### ⚠️ Três rádios na mesma mesa
> A entrada de RF do SX1262 aguenta **+10 dBm absolutos**. Um E22 transmitindo 29 dBm
> perto da antena do vizinho estoura isso por acoplamento de campo próximo.
> **Separe as antenas ou use atenuadores.** Queimar o receptor do nó 2 testando o TX do
> nó 1 é o jeito mais bobo de perder um módulo.

---

## Passo 8 — Meça três números

Antes de subir para TDMA, a bancada precisa produzir estes três. Nenhum deve ser
estimado.

| O que | Como | Para que serve |
|---|---|---|
| **Tempo real de TX** | o `e22_ping` já mede | dimensiona o slot do TDMA |
| **Deriva do cristal, em ppm** | conte `esp_timer_get_time()` entre 300 bordas de PPS: `ppm = (Δt − 300e6)/300e6 × 1e6` | dimensiona a guarda de holdover |
| **Queda de C/N0 do GPS quando o rádio transmite** | registre satélites e C/N0 antes e durante rajadas, a 10/30/50/100 cm | decide a separação das antenas |

Grave o ppm de cada placa na NVS. Serve para corrigir a deriva em software quando o
PPS cair — e para separar "erro do modelo" de "erro do código" quando o TDMA falhar
em campo.

> **O plano tem um número errado aqui.** `PLANO_IMPLEMENTACAO_MESH.md:147` diz
> "~2 ms/dia de drift". Isso é 0,023 ppm — classe OCXO de laboratório. Um cristal
> comum de 10 ppm **gasta a guarda de 110 ms em cerca de 3 minutos** de holdover.
> Meça, não confie no número escrito.

---

## Passo 9 — Ligue o TDMA

Com o PPS ancorando os dois nós, suba para o `tdma_core.h`.

**Use SF7.** É o único que sustenta 50 carros com atualização de 4 s:

| SF | Tempo no ar (16 B) | Frame para 50 carros |
|---|---|---|
| **SF7** | 51,5 ms | **4 s** |
| SF9 | 164,9 ms | 10 s |
| SF10 | 329,7 ms | 18 s |

E subir o SF **não compra alcance em mata**: a diferença de perda entre SF7 e SF9 é de
1 a 3 dB, contra ~25 dB da vegetação. O que derruba o link é obstrução geométrica —
morro, curva, tronco na Fresnel — que SF nenhum atravessa. SF7→SF9 custa 3,2× o tempo
no ar por ~5 dB. Gaste a folga em mais nós.

> ### ⚠️ Bug adormecido em `tdma_core.h:201`
> `anchorSec % frameSecs` pressupõe que todos os nós rotulam o segundo na mesma grade
> de tempo. Existem duas — GPS e UTC, separadas por **18 segundos**.
>
> | `frameSecs` | `18 % frameSecs` | |
> |---|---|---|
> | 1, 2, 3, 6, 9, 18 | 0 | seguro |
> | **4, 5** | 2, 3 | **frames deslocados, colisão silenciosa** |
>
> Hoje o `tdma_test.ino` usa `FRAME_SECS 1` e não morde. Mas **50 carros exigem frame
> de 4 ou 5 segundos** — exatamente onde morde.
>
> **Conserto:** fixe a grade em UTC em todo nó, ou acrescente uma asserção de que
> `frameSecs` divide 18.

---

## As 5 coisas que queimam ou não funcionam

Se você só ler uma seção, leia esta.

1. **Antena antes de energizar.** Sempre. O limite do PA é desconhecido.
2. **`setCurrentLimit(140)` depois de `setOutputPower`.** Sem isso, 1/10 da potência,
   silenciosamente.
3. **GPIO 46/47/48 não.** Banco do LDO, pode virar 1,8 V.
4. **30 cm entre as antenas de GPS e LoRa.** Menos que isso é dano, não interferência.
5. **Lógica é 3,3 V.** VCC é 5 V. Não confunda.

---

## Falta legalizar

LoRa de canal único em BW 125 kHz a 1 W **provavelmente não é legal no Brasil**. A
Res. ANATEL 680/2017 (Ato 14.448) libera 1 W em 915 MHz só por duas portas: **FHSS com
≥35 canais**, ou **banda ≥500 kHz**. Canal fixo em 125 kHz reprova nas duas.

A saída é elegante: o FHSS exige que todos os nós concordem sobre o canal a cada
instante — que é **exatamente o relógio comum que o TDMA já constrói**. Saltar de canal
por frame é quase de graça em cima do PPS.

---

## Estudos detalhados

Cada número acima tem fonte citada nos três estudos, com o que é datasheet, o que é
cálculo e o que não foi encontrado em lugar nenhum:

- **Estudo 1 — o rádio:** https://claude.ai/code/artifact/34004eb2-f38a-4483-9c0d-f593e7f1506b
- **Estudo 2 — as telas:** https://claude.ai/code/artifact/a62948b0-a7a0-4b9e-bd22-1ecc466fa245
- **Estudo 3 — o GPS:** em construção

Fontes primárias: Ebyte *E22-M Series User Manual* V1.2 (2026-02-06) · Semtech
*SX1261/2 Datasheet* Rev. 1.2 · esquemáticos oficiais Waveshare das três placas ·
Espressif *ESP32-P4 Datasheet* v0.7 · ANATEL Res. 680/2017 e Ato 14.448/2017 ·
ITU-R P.833-10.
