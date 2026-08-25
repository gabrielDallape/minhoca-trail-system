# Pinagem — o mapa pino a pino das três placas

Companheiro do `MONTAGEM.md`. Lá está o **passo a passo** de soldar e ligar; aqui
está **onde cada fio vai**, em cada uma das três placas, com a fonte de cada
número.

> **Verificado no desenho de montagem oficial de cada placa** (o silkscreen, que é
> o que você tem na mão), extraído dos esquemáticos publicados pela Waveshare em
> agosto de 2026. O que for inferência minha está marcado como tal.

---

## As três placas não são a mesma coisa

Você tem uma de 5" e vai ter duas de 7". **Elas divergem em quase tudo menos no
processador** — e uma delas se disfarça da outra no log de boot, o que já custou
uma tarde neste projeto.

| | 5" | 7B (as duas) |
|---|---|---|
| nome | ESP32-P4-WIFI6-Touch-LCD-**5** | ESP32-P4-WIFI6-Touch-LCD-**7B** |
| painel | HX8394, 720×1280 **em pé** | EK79007, 1024×600 **deitada** |
| backlight | GPIO 26, nível alto | **GPIO 32, nível BAIXO (invertido)** |
| reset do painel | GPIO 27, **ativo-ALTO** | GPIO 33, ativo-baixo |
| energia do painel | chip no I2C **0x45** | não precisa |
| cartão SD | pinos padrão + GPIO 45 | pinos 43/44/39-42 + **LDO interno 4** |
| expansão | **um** header J3 (2×20) | **dois** headers, P3 e P1 |

> **A armadilha:** a 7B imprime `ESP32_P4_EV` no boot de fábrica, porque a
> Waveshare copiou o BSP da Espressif e manteve a etiqueta de log. Ela **não** é a
> EV board da Espressif. Usar a pinagem da Espressif nela não dá erro nenhum — só
> não aparece imagem, e todas as chamadas retornam sucesso.

---

## Os doze fios do projeto

Nove para o rádio, três para o GPS. **Os mesmos GPIOs nas duas placas** — o que
muda é em qual conector eles saem.

| Sinal | GPIO | Para quê |
|---|---|---|
| `LORA_NSS` | 28 | seleção do SX1262 |
| `LORA_MOSI` | 29 | dados para o rádio |
| `LORA_SCK` | 30 | relógio do SPI |
| `LORA_MISO` | 31 | dados do rádio |
| `LORA_BUSY` | 49 | o rádio avisa que está ocupado |
| `LORA_DIO1` | 50 | **TxDone — é este pino que torna o TDMA possível** |
| `LORA_NRST` | 51 | reset do rádio |
| `LORA_TXEN` | 52 | liga o amplificador em transmissão |
| `LORA_RXEN` | 5 | liga o caminho de recepção |
| `GPS_PPS` | 2 | **a âncora de tempo do TDMA** |
| `GPS_TX` | 3 | vai para o RXD do GPS |
| `GPS_RX` | 4 | vem do TXD do GPS |

**Por que estes e não outros.** O SPI2 do P4 tem dois conjuntos de pinos no IO
MUX; o principal (GPIO 6–11) está todo ocupado nestas placas — I2C do toque,
UART do rádio WiFi, áudio. O conjunto alternativo (28–31) está livre **e sai em
conector nas duas placas**. Os pinos 49–52 idem.

E não sobra folga: contando o que cada placa expõe e tirando o que não se deve
usar, sobram **exatamente doze** pinos seguros. O projeto usa os doze.

---

## Placa de 5" — header J3

Um único header de 2×20 na borda de baixo. **A Waveshare imprime o número do GPIO
ao lado de cada pino** — use a etiqueta impressa, nunca conte posições.

```
fileira de cima:  GND  52  51  50  49  35  34 GND  31  30  29 3V3  28   4   3 GND   2 SCL SDA 3V3
fileira de baixo:  48  47  46 GND  32 GND  DP  DM  25  24 GND  22  21 GND   5  38  37 GND  5V  5V
```

Onde cada fio do projeto entra:

| Sinal | Etiqueta no J3 | Fileira |
|---|---|---|
| `LORA_TXEN` | **52** | cima |
| `LORA_NRST` | **51** | cima |
| `LORA_DIO1` | **50** | cima |
| `LORA_BUSY` | **49** | cima |
| `LORA_MISO` | **31** | cima |
| `LORA_SCK` | **30** | cima |
| `LORA_MOSI` | **29** | cima |
| `LORA_NSS` | **28** | cima |
| `GPS_RX` | **4** | cima |
| `GPS_TX` | **3** | cima |
| `GPS_PPS` | **2** | cima |
| `LORA_RXEN` | **5** | **baixo** |
| alimentação do E22 | **5V** | baixo (duas posições) |
| alimentação do GPS | **3V3** | cima |
| terra | **GND** | qualquer uma das seis |

**Bom para o chicote:** onze dos doze ficam na fileira de cima, em sequência. Só
o `RXEN` desce.

---

## Placas de 7" (7B) — headers P3 e P1

Aqui os doze fios se dividem em **dois** conectores, nos cantos de baixo.

**P3** (canto inferior esquerdo):
```
3V3  GND  IO2  IO3  IO4  IO5  IO28  IO29  IO30  IO31  IO34  IO36
```

**P1** (canto inferior direito):
```
BAT  GND  3V3  VO4  GND  IO46  IO47  IO48  IO49  IO50  IO51  IO52
```

| Sinal | Conector | Etiqueta |
|---|---|---|
| `GPS_PPS` | P3 | **IO2** |
| `GPS_TX` | P3 | **IO3** |
| `GPS_RX` | P3 | **IO4** |
| `LORA_RXEN` | P3 | **IO5** |
| `LORA_NSS` | P3 | **IO28** |
| `LORA_MOSI` | P3 | **IO29** |
| `LORA_SCK` | P3 | **IO30** |
| `LORA_MISO` | P3 | **IO31** |
| `LORA_BUSY` | P1 | **IO49** |
| `LORA_DIO1` | P1 | **IO50** |
| `LORA_NRST` | P1 | **IO51** |
| `LORA_TXEN` | P1 | **IO52** |
| alimentação do GPS | P3 ou P1 | **3V3** |
| terra | P3 ou P1 | **GND** |

### Os 5 V do rádio na 7B

**P3 não tem 5 V.** Ele existe em três lugares nessa placa:

- header **CAN** (pinos `GND` e `5V`)
- header **RS485** (pinos `GND` e `5V`)
- **P1**, no pino `BAT`

> **Não confirmei o que é o `VO4` do P1** — não consegui rastrear essa rede no
> esquemático. Meça com multímetro antes de ligar qualquer coisa nele. Não é
> necessário para o projeto: use o 5 V do header CAN ou RS485.

**Não use os pinos IO34 e IO36 do P3, nem IO46/47/48 do P1.** Os primeiros são
strapping (o 35 tem o botão BOOT em paralelo, e 34–38 decidem o modo de boot); os
segundos ficam no banco `VDD_IO_5`, alimentado pelo mesmo regulador interno que
comuta o cartão SD entre 3,3 V e 1,8 V — se o cartão trocar de tensão, eles vão
junto.

---

## Por dentro da 7B — todos os conectores

Lido no esquemático elétrico, seção "7inch Display" e vizinhas. **São dois flat
cables indo para o painel, não um.**

### P2 — o flat cable do vídeo (30 vias, passo 0,5 mm, trava basculante)

O maior da placa. Além dos dados, ele carrega **as tensões de polarização do
painel**, que não são 3,3 V:

| Vias | Sinal | O que é |
|---|---|---|
| 1, 2 | `LED+` | anodo da iluminação de fundo |
| 3 | `VGH` | tensão positiva de porta do painel |
| 4 | `VGL` | tensão negativa de porta |
| 5, 6 | `VCC_1V8` | lógica do painel, 1,8 V |
| 7, 8 | `LED-` | catodo da iluminação |
| 9 | `AVDD` | **9,6 V** — alimentação analógica do painel |
| 17, 18 | `DSI_CLK_P` / `DSI_CLK_N` | relógio diferencial do MIPI |
| 20, 21 | `DSI_D1_P` / `DSI_D1_N` | lane 1 |
| 23, 24 | `DSI_D0_P` / `DSI_D0_N` | lane 0 |
| 27 | `RESET_LCD` | vem do **GPIO 33**, por R42 de 0 Ω |
| 28 | `VCC_1V8` | |
| demais | `GND` | |

> **Não desconecte com a placa ligada.** Esse cabo tem 9,6 V e as tensões de porta
> do painel. Um repique de contato em `VGH`/`VGL` com o painel energizado é o jeito
> mais rápido de perder a tela — e nenhum firmware protege contra isso.

Os quatro pares `DSI_*` são **diferenciais de alta velocidade** (900 Mbps por
lane). Não force o cabo, não faça vinco e não o passe perto do chicote do rádio:
1 W em 915 MHz a poucos centímetros de um par MIPI é procurar problema.

### J3 — o flat cable do toque (6 vias, passo 0,5 mm)

Separado do vídeo. É pequeno e fácil de esquecer ao remontar:

| Via | Sinal |
|---|---|
| 1 | `RESET_TP` |
| 2 | `ESP_3V3` |
| 3 | `GND` |
| 4 | `INT_TP` |
| 5 | `ESP_I2C_SDA` |
| 6 | `ESP_I2C_SCL` |

> O `INT_TP` **está fiado no conector**, apesar de o BSP da Waveshare declarar
> `BSP_LCD_TOUCH_INT = GPIO_NUM_NC`. Não consegui rastrear em qual GPIO ele
> termina. Hoje não faz falta: o GT911 é lido por varredura.

### Os outros conectores

| Ref. | O que é | Formato |
|---|---|---|
| `J2` | câmera MIPI-CSI (o kit vem sem sensor) | FPC 15 vias |
| `H7` | **alto-falante** — é aqui que entram os 3 bipes do alerta | JST-PH 2,0 mm, 2 vias |
| `J4` | bateria, com `+` e `−` no silkscreen | JST 2 vias |
| `SD1` | cartão microSD | slot push-push |
| `H4` | UART do rádio Wi-Fi C6: `TXD RXD GND IO9` | header 4 vias |
| `J1` | USB-C **OTG** | |
| `H2` | USB-C | |
| `H1` | USB-C **USB TO UART** — **é por onde se grava** | |
| `SW2` | chave liga/desliga | |
| `Key1` / `Key2` | RESET / BOOT (o BOOT é o **GPIO 35**) | |
| `BAT1` | pilha de moeda do relógio de tempo real | |

O alto-falante é acionado por um amplificador **NS4150B**, habilitado pelo
**GPIO 53** (`BSP_POWER_AMP_IO`). Sem levar esse pino a nível alto, o codec toca e
não sai som — e o sintoma parece defeito de áudio.

### Os quatro headers laterais, e o jumper que decide a tensão

| Ref. | Header | Pinos |
|---|---|---|
| `H10` | **I2C** | `SCL SDA GND VCC` |
| `H8` | **UART** | `TXD RXD GND VCC` |
| `H11` | **CAN** | `L H GND 5V` |
| `H9` | **RS485** | `B A GND 5V` |

> ### ⚠️ O jumper H3 decide o que é "VCC"
> Ao lado dos headers há um jumper **`H3`** marcado **`5V | 3V3`**. Ele define a
> tensão do pino `VCC` **dos headers I2C e UART** — os dois.
>
> **Isto importa para o GPS.** Se você alimentar o GPS pelo `VCC` do header UART e
> o `H3` estiver em `5V`, você entrega 5 V a um módulo de 3,3 V. Confira o jumper
> **e meça** antes de ligar. Os headers CAN e RS485 são sempre 5 V, sem jumper.
>
> Os jumpers `H5` e `H6` são outra coisa: ligam o resistor de terminação de 120 Ω
> do CAN e do RS485. Não mexa neles.

---

## O chicote para as três

Aqui está a decisão de projeto que a diferença de conectores impõe: **um chicote
único não serve as duas placas**, porque na de 5" tudo sai de um header e na 7B
sai de dois, em cantos opostos.

Duas saídas, e eu recomendo a segunda:

**A) Dois chicotes diferentes.** Simples de fazer, chato de manter: você passa a
ter dois cabos parecidos e não intercambiáveis, e vai trocar um pelo outro em
campo alguma vez.

**B) Um conector no meio.** O E22 e o GPS terminam num conector padrão de 14 vias
(12 sinais + 5 V + GND). Desse conector saem **dois rabichos**: um que vira o J3
da 5" e outro que vira P3+P1 da 7B. O módulo é sempre o mesmo, o rabicho é que
muda de placa.

Com a opção B, o rádio e o GPS viram uma peça só, testável na bancada e movível
entre as três placas sem dessoldar nada — e é isso que você vai querer ao caçar
diferença entre nós durante o TDMA.

**Ordem dos fios no conector do meio**, na sequência do P3 seguida do P1, que é a
que dá menos cruzamento nos dois rabichos:

```
 1  GND
 2  5V        (só o E22)
 3  3V3       (só o GPS)
 4  GPS_PPS   -> IO2
 5  GPS_TX    -> IO3
 6  GPS_RX    -> IO4
 7  LORA_RXEN -> IO5
 8  LORA_NSS  -> IO28
 9  LORA_MOSI -> IO29
10  LORA_SCK  -> IO30
11  LORA_MISO -> IO31
12  LORA_BUSY -> IO49
13  LORA_DIO1 -> IO50
14  LORA_NRST -> IO51
15  LORA_TXEN -> IO52
```

São 15 vias, não 14 — o terra único vira dois na prática (um por lado) se o
chicote passar de uns 15 cm. Ver a nota de corrente abaixo.

---

## Alimentação — onde isto queima

**O E22-900M30S puxa mais de 600 mA no pico de transmissão.** Três consequências
que não são opcionais:

1. **5 V, não 3,3 V.** O módulo tem regulador próprio. Ligar em 3,3 V não queima,
   mas o amplificador não entrega potência e você vai caçar problema de alcance
   que é de alimentação.
2. **Capacitor eletrolítico de ≥470 µF / 10 V** entre 5 V e GND, **junto ao
   módulo**, não junto à placa. Sem ele o pico de TX derruba a tensão e a placa
   reinicia — o sintoma é o nó "sumir da rede" em intervalos regulares, que é
   exatamente o que parece um bug de TDMA.
3. **Antena antes de energizar.** Transmitir sem antena reflete a potência de
   volta para o amplificador. Um W refletido mata o PA.

E o fio: 600 mA em fio fino de 30 cm derruba tensão suficiente para importar. Use
pelo menos 26 AWG para 5 V e GND, e mantenha o par curto.

---

## GPS — e por que o PPS é o item mais importante da lista

O GPS entrega duas coisas, e a segunda é a que faz o projeto existir:

- **NMEA em 9600 bps** (`GPS_TX` → `IO4`), que dá a posição. É o óbvio.
- **PPS** (`IO2`), um pulso por segundo, **alinhado ao segundo do sistema GPS com
  erro de dezenas de nanossegundos**. É a régua que faz todos os carros
  concordarem sobre quando é o slot de cada um.

Sem PPS não há TDMA disciplinado — só um "cada um fala quando acha que é sua vez",
que colide assim que os relógios derivarem. **Um cristal comum de ESP32 deriva
alguns segundos por dia**; o slot inteiro tem milissegundos.

> **O NEO-7M que você tem não serve para isto do jeito que está**: o breakout não
> traz o PPS no header. O sinal existe no chip. Ou você solda um fio no pad do
> módulo, ou usa um GPS que já exponha PPS — o ATGM336H expõe.

No firmware, o PPS deve entrar por **captura em hardware** (GPIO → ETM →
`GPTIMER_ETM_TASK_CAPTURE`), não por interrupção de software: a interrupção
carimba o tempo depois de o sistema operacional escalonar, e isso já custa mais
que a precisão que você foi buscar.

---

## Ordem de montagem sugerida

Uma placa por vez, e **conferindo antes de somar a próxima**:

1. **Solde o E22 no primeiro módulo** e monte o chicote com o conector do meio.
2. **Ligue na placa de 5"**, que é a única que já rodou tudo. Rode o
   `firmware/e22_ping` — ele mede o tempo real de transmissão, que é o número que
   o TDMA precisa.
3. **Só então monte o segundo rádio.** Dois nós é o mínimo para o ping-pong
   dizer alguma coisa.
4. **GPS depois do rádio.** Ele não é necessário para o ping-pong, e adicionar
   duas variáveis ao mesmo tempo é como se perde uma tarde.
5. **TDMA por último**, com três nós, que é quando a disciplina de slot deixa de
   ser teoria.

---

## O que ainda não está verificado

Sendo honesto sobre os limites deste documento:

- **O `VO4` do P1 na 7B** — não rastreei essa rede. Meça antes de usar.
- **A numeração física dos pinos do J3** (qual ponta é o pino 1) — eu li as
  etiquetas de GPIO, que é o que importa e o que elimina erro de contagem, mas
  não confirmei a orientação do conector. Confira o `3V3` com multímetro antes de
  ligar o primeiro fio.
- **A terceira placa** ainda não chegou. Estou assumindo que é outra 7B, igual à
  que já está aqui. Se vier diferente, o log de boot dela dirá — e agora você já
  sabe que o nome no log pode mentir.

---

## Fontes

- Desenhos de montagem: `ESP32-P4-WIFI6-Touch-LCD-5-Schematic.pdf` e
  `ESP32-P4-WIFI6-Touch-LCD-7B.pdf`, publicados pela Waveshare
- BSP oficial: `waveshareteam/Waveshare-ESP32-components`, pasta
  `bsp/esp32_p4_wifi6_touch_lcd_7b`
- Rádio: manual Ebyte E22-M V1.2 (2026-02) e datasheet SX1261/2
- Detalhes do rádio e do TDMA: `docs/MONTAGEM.md` e
  `docs/PLANO_IMPLEMENTACAO_MESH.md`
