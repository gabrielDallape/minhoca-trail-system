# Estudo 3 — O E22-900M30S na prática

> Escrito em **2026-09-08**, depois de varrer os projetos abertos que usam este
> módulo. O objetivo era um só: **descobrir como quem faz o E22 funcionar faz**, e
> comparar com o que fizemos aqui.
>
> Duas conclusões de uma linha: **todos os projetos que funcionam dão ao E22 uma fonte
> própria de 5 V com pelo menos 1 A, e soldam o módulo numa PCB.** Nós fazemos as
> duas coisas ao contrário. E **dois projetos já resolveram o mesmo produto que o MTS**
> (seção 5) — nenhum deles com o módulo de 1 W.

---

## 1. O módulo entrega. Está medido por terceiros.

Duas medições independentes, com instrumento, fora deste projeto:

| Fonte | O que mediu | Resultado |
|---|---|---|
| [S5NC/CDEBYTE_Modules](https://github.com/S5NC/CDEBYTE_Modules/blob/main/E22-900M30S%20power%20output%20testing.txt) | escada de +1 a +22 dBm no SX1262, potência real na saída | **+8,11 a +29,37 dBm** — ganho de +7 dB constante em toda a faixa |
| [RadioLib #487](https://github.com/jgromes/RadioLib/discussions/487) | potência máxima depois de corrigir o OCP | **29,43 dBm @ 915 MHz** e 30,66 dBm @ 868 MHz |

Ou seja: os 29,3 dBm que o `hardware.h` promete **existem**, são reprodutíveis, e
o ganho do PA é linear. Não há mistério no módulo.

### A armadilha nº 1 da comunidade — que nós já vencemos

O OCP de fábrica fica em **60 mA**, e com ele a saída trava em ~19,6 dBm **sem erro
nenhum no log**. Metade das reclamações de "meu E22 não passa de 20 dBm" na internet
é isso. Está corrigido aqui desde o começo (`RF_OCP 140` no `hardware.h`, aplicado
sempre *depois* do `setOutputPower`).

### A armadilha nº 2 — que não é nossa

O firmware do **Meshtastic limita a potência em 22 dBm por software**. Um usuário
[passou um ano](https://github.com/meshtastic/firmware/discussions/3948) tentando
tirar 30 dBm de um M30S/M33S trocando reguladores, capacitores e fiação, sem saber
que o teto era do firmware. Vale registrar porque a lição é transferível: **"só
chega a 22" quase nunca é o módulo.**

E o inverso também quebra: em
[SX126x-Arduino #12](https://github.com/beegee-tokyo/SX126x-Arduino/issues/12), um
usuário forçou `TX_OUTPUT_POWER` para 30 (fora da faixa do SX1262, que vai até 22) e
a saída **caiu para −5 dBm**. O valor pedido é o do chip, não o da antena.

---

## 2. Os oito projetos de montagem

Todos abertos, todos com E22 de alta potência, todos em produção ou uso real.

| Projeto | MCU | Módulo | Como alimenta o E22 |
|---|---|---|---|
| [MeshAdv Pi HAT v2](https://github.com/KaliAssistant/MeshAdv_piHAT_v2) | Raspberry Pi | E22-900M33S | **buck Mini560 12 V→5 V 5 A ligado direto no módulo**; AMS1117 só para a lógica |
| [MeshAdv Pi Hat](https://github.com/chrismyers2000/MeshAdv-Pi-Hat) | Raspberry Pi | E22-900M30S/33S | alimentação externa dedicada, opção PoE |
| [MeshAdv Mini](https://github.com/chrismyers2000/MeshAdv-Mini) | Pi Zero | E22-900M**22S** | 5 V/3,3 V do header (é o módulo de 22 dBm, não precisa de mais) |
| [**MeshAdventurer**](https://github.com/chrismyers2000/MeshAdventurer) | ESP32-WROOM-32D | E22-900M30S | **entrada 9–28 V por barrel jack — alimentação veicular** |
| [Ebytastic](https://github.com/Sobollion/Ebytastic) | nRF52840 | E22-900M30S/33S | **5 V externo dedicado, 1 A (1,3 A de pico)** |
| [FakeTec](https://github.com/gargomoma/fakeTec_pcb) | nRF52840 | E22/E220, RA-01SH | bateria/USB pelo ProMicro; duas variantes de firmware (com e sem TCXO) |
| [SpaceBridge HAB](https://github.com/eigenlucy/spacebridgehabpcb) | ESP32-S3 | E22-900M30S | **TPS63020 boost-buck, 5 V até 4 A** |
| [Meshtastic-DIY-sx126x](https://github.com/nicolabalzano/Meshtastic-DIY-sx126x) | ESP32 | E22 (SX126x) | guia de fiação — atenção, é para os módulos de 3,3 V |

### O projeto que é quase o nosso

O **MeshAdventurer** merece destaque: é hardware Meshtastic **feito para uso
off-road**, e a lista de peças é quase idêntica à nossa —

- **E22-900M30S** (o mesmo módulo)
- **GPS ATGM336H** (o mesmo GPS que especificamos)
- **entrada de 9 a 28 V por barrel jack**, para ligar direto no veículo
- montagem em painel/para-brisa, antena SMA intercambiável

A única diferença estrutural entre ele e nós é **de onde vem a energia**.

---

## 3. A convergência: o que todos fazem e nós não

Cinco padrões que se repetem em todos os projetos que funcionam.

### 3.1. Fonte de 5 V dedicada, dimensionada para ~1 A

O manual da Ebyte diz **500–620 mA**. Os projetos reais dizem outra coisa:

| Projeto | Corrente prevista para o E22 |
|---|---|
| Ebytastic | **1 A contínuo, 1,3 A de pico** (33 dBm) |
| MeshAdv Pi HAT v2 | **1,2 A médio em transmissão**, buck de 5 A |
| SpaceBridge HAB | conversor de 5 V com **até 4 A** |

Três fontes independentes dimensionam para **o dobro** do que o datasheet promete.
Ninguém tira essa corrente de uma porta USB, e ninguém a divide com a lógica.

> **O padrão universal:** um buck de 12 V → 5 V alimenta **o E22 diretamente**, e um
> LDO separado (AMS1117 é o mais comum) cuida só do 3,3 V da lógica. As duas
> alimentações nunca se misturam.

### 3.2. O desacoplamento não é um capacitor, são quatro

O Ebytastic especifica, junto ao módulo:

```
1× eletrolítico 100–220 µF / 16 V
1× cerâmico 4,7 µF
1× cerâmico 1 µF
1× cerâmico 0,1 µF
```

Isso é o clássico banco escalonado: o eletrolítico segura o pacote, os cerâmicos
seguram a **rampa do PA**, que é de 200 µs. Um eletrolítico sozinho tem ESR de
centenas de mΩ e simplesmente não responde nessa escala de tempo.

Nós temos **só o eletrolítico de 470 µF**, e nenhum cerâmico.

### 3.3. O módulo é soldado em PCB, nunca em fios volantes

Nenhum dos oito projetos usa fiação volante. Todos fazem placa — e o E22 tem pads
castelados justamente porque foi projetado para isso.

O MeshAdv v2 registra ainda uma nota de layout que vale copiar: **copper pour
extenso, com os planos de terra fundidos**, tanto por RF quanto por térmica.

### 3.4. Aviso que aparece em vários lugares

> "**Não alimente o dispositivo por USB e fonte externa simultaneamente**" — isso
> danifica o LDO. *(Ebytastic)*

E o universal, em todos:

> "Sempre tenha uma antena conectada quando energizado."

### 3.5. PPS do GPS para disciplinar tempo

O MeshAdv Pi Hat e o MeshAdv Mini usam **ATGM336H com PPS** para "time keeping muito
preciso". É exatamente a técnica do nosso `tdma_core.h` — e uma validação de que a
escolha do GPS estava certa.

---

## 4. O mecanismo, lido no datasheet do silício

Esta seção é a mais importante do estudo. Tudo abaixo vem do **SX1261/2 Data Sheet
Rev 2.2 (Semtech, dez/2024)**, extraído e lido direto — não de fórum.

### 4.1. O SX1262 precisa de tensão para dar potência

**Tabela 4-4 — Power Amplifier Summary**, coluna SX1262, linha *Output Power vs VBAT*:

| Para entregar | VBAT precisa ser |
|---|---|
| **+22 dBm** | **3,3 V** |
| +20 dBm | 2,7 V |
| +16 dBm | 1,8 V |

Não é uma degradação suave: é a especificação de quanto o chip precisa para cada
patamar.

### 4.2. E a corrente sobe quando a tensão cai

Mesma tabela, linha *IDDTX vs VBAT*: **"inversamente proporcional a VBAT"**. A nota
da seção 5.1 dá o número:

> "a corrente drenada é inversamente proporcional a VBAT (por exemplo, para
> Pout = +14 dBm, **25,5 mA a 3,3 V, e 48 mA a 1,8 V**)"

A corrente quase **dobra** quando a tensão cai. Isso é realimentação positiva: menos
tensão → mais corrente → mais queda na resistência do cabo → menos tensão.

### 4.3. O OCP fica a 19% do consumo real

**Tabela 3-6 — Power Consumption in Transmit Mode**: o SX1262 a **+22 dBm consome
118 mA** (DC-DC). O OCP que usamos é **140 mA**.

São **19% de margem**. Se a tensão cair e a corrente subir conforme 4.2, os 118 mA
passam dos 140 mA e o OCP corta. E a Semtech descreve exatamente essa consequência:

> "o limite de OCP deve ser configurado alto o suficiente para acomodar um aumento de
> corrente ou ser configurado dinamicamente. Outra estratégia é definir o OCP num
> limite específico e **aceitar uma queda da potência de saída quando o OCP começa a
> limitar a corrente**."

**O OCP máximo é 63 × 2,5 mA = 157,5 mA** (seção 5.1). Estamos usando 140 — dá para
subir 12% de graça.

### 4.4. Por isso o colapso tem forma de degrau

Juntando 4.1–4.3, o mecanismo fecha:

```
potência pedida sobe
   → hpMax dobra (a paOptTable do RadioLib faz isso entre 6 e 7 dBm)
      → corrente do chip sobe
         → com VBAT baixo, sobe MAIS (inversamente proporcional)
            → cruza o OCP
               → o OCP corta a corrente do PA
                  → a rampa/transmissão falha
                     → TX_DONE nunca vem  ->  "preso"
```

O OCP é um **comparador**, não um atenuador: ou está abaixo do limite, ou corta. É
por isso que 2 dB de diferença viram 0% → 100% de falha, o que nenhuma teoria
puramente analógica explicava.

E isso reconcilia as duas hipóteses do projeto: **o software escolhe a corrente
(paDutyCycle/hpMax) e a alimentação escolhe o multiplicador (VBAT).** Nenhum dos dois
sozinho explica; o produto dos dois explica tudo, inclusive por que a tela B passou
a 22 dBm com fonte de 2–3 A e falhou a 8 dBm no USB do PC.

### 4.5. A Semtech sanciona quatro modos. O RadioLib usa trinta e dois.

**Tabela 13-21 — PA Operating Modes with Optimal Settings**, SX1262:

| Saída | paDutyCycle | hpMax | Valor em SetTxParams |
|---|---|---|---|
| +22 dBm | 0x04 | 0x07 | **+22 dBm** |
| +20 dBm | 0x03 | 0x05 | **+22 dBm** |
| +17 dBm | 0x02 | 0x03 | **+22 dBm** |
| +14 dBm | 0x02 | 0x02 | **+22 dBm** |

Repare: nos quatro modos oficiais o `SetTxParams` é **sempre +22 dBm**, e o que muda
é a configuração do PA. O RadioLib faz o contrário — varia o valor do `SetTxParams` e
usa 32 combinações, das quais só quatro existem no datasheet.

E há um aviso explícito:

> **Caution!** "As seguintes restrições devem ser observadas para evitar *voltage
> overstress* no PA; exceder os valores máximos pode causar **dano irreversível** ao
> dispositivo. […] **Para o SX1262, paDutyCycle não deve ser maior que 0x04.**"

A `paOptTable` do RadioLib usa `paDutyCycle = 0x04` em vários pontos — no limite,
mas dentro. Já `hpMax`: "aumentar acima de 0x07 pode causar **envelhecimento precoce
ou danificar o dispositivo** em temperaturas extremas".

### 4.6. O "módulo defeituoso" talvez não seja defeituoso

**Seção 13.3.6 — SetDIO3AsTCXOCtrl:**

> "O flag XOSC_START_ERR é levantado **no POR ou no wake-up de Sleep em cold-start,
> quando um TCXO é usado. É um comportamento esperado**, já que o chip ainda não sabe
> que está sendo clocado por um TCXO. O usuário deve simplesmente **limpar essa flag
> com o comando ClearDeviceErrors**."

O `e22_ping.ino` lê `GetDeviceErrors` (0x17) mas **nunca chama `ClearDeviceErrors`
(0x07)** — verificado. O flag é pegajoso: uma vez levantado no boot, fica lá e
aparece em toda leitura posterior.

O diagnóstico de 2026-08-24 que condenou o módulo da tela A como defeituoso se
baseou em ler `0x0020` "ao armar TX acima de ~2 dBm". **Sem um `ClearDeviceErrors`
antes do teste, essa leitura não distingue "o TCXO falhou agora" de "o TCXO deu o
aviso normal de boot há dez minutos".**

Isso não prova que o módulo é bom — prova que a evidência usada para condená-lo não
é conclusiva. **Refazer o teste limpando os erros antes é meia hora de trabalho e
pode devolver um módulo de 1 W à frota.**

### 4.7. O chip tem watchdog de TX nativo, e nós reimplementamos por software

**Seção 13.4.1 — SetTx** aceita `timeout(23:0)`:

> "Um **IRQ TIMEOUT é disparado se o TX_DONE não for gerado** dentro do período de
> timeout. O chip volta para STDBY_RC depois de um IRQ TIMEOUT ou TX_DONE."

Nosso `radio.h` implementa esse watchdog em software, com `millis()` e um limite de
400 ms — e ele já custou **dois** bugs: o falso "preso" quando o TLS bloqueava o
laço, e o travamento permanente quando a ISR perdia a borda. O chip faz isso sozinho,
com o relógio dele, e avisa por IRQ.

### 4.8. A transmissão tem três pontos de falha, e o chip diz qual foi

Mesma seção, sequência do `SetTx()`:

```
STDBY_RC → oscilador (XOSC) liga → PLL prende → PA liga → rampa do PA
         → transmite o pacote → TX_DONE
```

Três bits de `GetDeviceErrors` cobrem os três primeiros passos:

| bit | erro | significa |
|---|---|---|
| 5 | `XOSC_START_ERR` | o TCXO não partiu |
| 6 | `PLL_LOCK_ERR` | o PLL não prendeu |
| 8 | `PA_RAMP_ERR` | **a rampa do PA falhou** |

Um `preso` é sempre uma falha em um desses três. O `e22_ping` sabe decodificar os
três; o `radio.h` de produção **não lê nenhum**. Hoje contamos o sintoma e
descartamos o diagnóstico que o chip está oferecendo de graça.

---

## 5. Dois projetos que resolvem o MESMO problema

As seções anteriores tratam de *como montar o E22*. Estas duas tratam de outra
coisa: gente que construiu **o mesmo produto que o MTS** — posição de um grupo,
por LoRa, com TDMA disciplinado por GPS. As escolhas deles são o melhor material
de comparação que este estudo tem.

### 5.1. Spoke — navegação relativa em grupo

[FeruzTopalov/spoke](https://github.com/FeruzTopalov/spoke) — *"Standalone LoRa-based
real-time off-grid relative GPS navigation for group of up to 5 people"*.

| | Spoke |
|---|---|
| MCU | STM32F103C8T6 ("BluePill") |
| Rádio | **E22-900M22S** (868 MHz) ou E22-400M22S (433 MHz) |
| GPS | QUESCAN G10A-F30 / TU10-F, **com PPS** |
| Tela | LCD monocromático 1,3", ST7567 |
| Extras | bússola LSM303DLHC, USB-C, BLE (opcionais) |
| Bateria | Li-ion 2200 mAh |

**Como o TDMA dele funciona** — e é aqui que está a lição:

- O PPS abre uma janela de processamento de 900 ms, dentro da qual o NMEA é lido.
- **O slot é o dígito do segundo.** Cada aparelho fala num segundo par do relógio
  GPS: x0, x2, x4, x6, x8. Cinco aparelhos, cinco slots.
- Intervalo configurável: 10, 30, 60 ou 120 s entre transmissões.
- **SF12, BW 125, CR 4/8** → *time on air* de **1,188 s por pacote**.
- Pacote de **13 bytes**: flags (1) + nº do dispositivo (1) + ID (1) + lat (4, float)
  + lon (4) + altitude (2).

**Interface:** um radar. Círculo de 360°, a sua direção é o raio ("spoke"), cada
membro do grupo é um entalhe na borda, com distância e azimute. Sem mapa, sem
cartão, sem tiles.

#### Quatro decisões dele que contrastam com as nossas

1. **Escolheu o E22-900M22S, não o M30S.** O projeto mais parecido com o MTS, feito
   por alguém que precisava de alcance em campo, optou pelos 22 dBm em 3,3 V em vez
   do 1 W em 5 V. Isso é um voto de peso a favor da alternativa que este estudo
   levanta na seção 8.
2. **O GPS é a âncora, e ponto final.** Não há eleição, beacon, promoção nem
   holdover: se o slot é o dígito do segundo, quem tem fix está sincronizado e quem
   não tem simplesmente não fala. Nós construímos uma máquina de eleição de âncora
   inteira (`tdmaEleicao`, `tdmaBeaconAceita`, `tdmaPromoveAncora`) **justamente
   porque exigimos que a rede funcione antes do primeiro fix** — o carro que liga sob
   as árvores. É um requisito nosso, não um descuido dele; mas vale saber quanto ele
   custa.
3. **SF12 contra o nosso SF9.** Ele trocou capacidade por alcance de forma muito mais
   agressiva — e pôde, porque são 5 nós falando a cada 10–120 s.
4. **Radar em vez de mapa.** Distância e azimute resolvem "onde está o grupo" sem
   cartografia. O nosso mapa existe para um requisito diferente (seguir o *caminho*
   do líder, não a *posição* dele), mas o Spoke mostra o quanto do sistema é opcional
   quando esse requisito cai.

### 5.2. Off-grid GPS race tracker — o TDMA que escala para 100+

[Hackaday, log "microsecond resolution TDMA using GPS PPS on
Arduino"](https://hackaday.io/project/13013-off-grid-gps-race-tracker-client-and-server/log/44413-microsecond-resolution-tdma-using-gps-pps-on-arduino).

Este responde diretamente a pergunta dos 30–50 carros, com números medidos:

- PPS numa **interrupção de hardware** → sincronia melhor que 100 ns entre aparelhos;
  **jitter de 7,8 µs medido em osciloscópio**, com o laço repetindo a ~128 kHz.
- Cada segundo dividido em **3 slots de 333 ms** → **180 dispositivos por minuto**,
  100+ por canal de RF.
- Pacote LoRa de **~280 ms**.
- Telemetria: posição, velocidade, rumo, altitude, bateria e **botão de pânico**.

**A armadilha do NMEA, resolvida do mesmo jeito que a nossa:**

> "ON the PPS, the NMEA sentence with the newly lapsed second value will not have been
> received and parsed yet"

Ele compensa **subtraindo 1 do valor da hora** ao comparar com o slot designado. É o
mesmo problema que o nosso `gps.h` resolve emparelhando o pulso com a frase seguinte
(`g_parePpsUs` / `g_pareSec`) — duas implementações independentes chegando à mesma
conclusão é um bom sinal de que a nossa está certa.

**E uma escolha de arquitetura idêntica à nossa:** ele **evitou timers secundários de
propósito**, preferindo manter o laço principal com funções pequenas e puláveis para
o PPS poder interromper. É exatamente a decisão do `esperaServindo()` /
`g_hookServico` (`ui.h:452`), tomada pelos mesmos motivos.

### 5.3. O que os dois somam

- **O conceito não é território inexplorado.** TDMA por PPS sobre LoRa funciona, está
  publicado, e em pelo menos um caso com o mesmo E22.
- **A escala de 100+ nós é atingível** com slots derivados do relógio GPS — sem
  SOTDMA, sem negociação, sem estado compartilhado. O race tracker fez com 3 slots de
  333 ms.
- **Nenhum dos dois usa o módulo de 1 W.** O Spoke, que é o caso mais próximo do
  nosso, usa o E22-900M**22S**.
- **A complexidade da nossa eleição de âncora é o preço de um requisito específico**
  (funcionar sem fix). Se um dia esse requisito for relaxado, some uma classe inteira
  de bugs — foram quatro corrigidos no `tdma_core.h` só nessa área.

---

## 6. O que fazemos diferente

| | Projetos que funcionam | MTS hoje |
|---|---|---|
| Fonte do E22 | buck 12 V→5 V dedicado, 1–5 A | header CAN/RS485 da tela (trilho de transceptor, ~50 mA de projeto) |
| Caminho da energia | 12 V → buck → módulo | isqueiro → adaptador USB → **cabo USB** → placa → header → 15 cm de fio |
| Corrente prevista | 1–1,3 A | 620 mA (o número do datasheet) |
| Desacoplamento | eletrolítico + 3 cerâmicos | só 470 µF eletrolítico |
| Interconexão | PCB com pads castelados | **16 fios soldados à mão**, ×3 aparelhos |
| Lógica 3,3 V | LDO separado | compartilhada |

E no software, contra o
[`SX126xInterface.cpp` do Meshtastic](https://github.com/meshtastic/firmware/blob/master/src/mesh/SX126xInterface.cpp)
(mesmo módulo, mesma RadioLib):

| Chamada | Meshtastic | nosso `radio.h` |
|---|---|---|
| `setPaRampTime()` | configurável — "para placas que exigem estabilização prolongada" | **ausente** → 200 µs fixos |
| `setDio2AsRfSwitch(false)` | chamado quando usa TXEN/RXEN externos | **ausente** → fica `true`, herdado do `begin()` |
| `setRxBoostedGainMode(true)` | sim | **ausente** (~2 dB de RX na mesa) |
| patch `SPIsetRegValue(0x8B5, 0x01, 0, 0)` | sim — patch Semtech/Heltec de sensibilidade RX | **ausente** |
| `setOutputPower(p, false)` | opção `SX126X_NO_POWER_OPTIMIZATION_TABLE` | **ausente** |

### A rampa do PA é a mais interessante

O RadioLib usa **200 µs fixos** (`SX126x_config.cpp:730`). O SX1262 aceita 10, 20,
40, 80, 200, 800, 1700 ou **3400 µs**. Rampa mais lenta = pico de corrente menor =
alimentação marginal aguenta.

E o chip tem um bit de erro chamado literalmente **`PA_RAMP_ERR` — "PA ramping
failed"** (`SX126x_commands.h:290`). O nosso `e22_ping.ino:504` já sabe decodificá-lo
como `RAMPA_DO_PA`, mas o `radio.h` de produção **não lê `GetDeviceErrors`**: quando
um TX trava, contamos um `preso` e nunca perguntamos ao chip se foi o TCXO ou o PA.

### Onde o `setDio2AsRfSwitch` está objetivamente errado

Verificado na biblioteca instalada: `SX126x.cpp:58` chama `setDio2AsRfSwitch(true)`
dentro do `begin()`, e o `setRfSwitchPins()` posterior só configura GPIOs do MCU —
**não desfaz nada no chip**. O Meshtastic chama `setDio2AsRfSwitch(false)`
explicitamente quando há TXEN/RXEN externos.

O maintainer do RadioLib
[avisa](https://github.com/jgromes/RadioLib/discussions/1336) que isso "aparentemente
funciona" a curta distância, e que só medições de RSSI e de potência revelam a
degradação — o que descreve bem uma bancada de 5 metros.

---

## 7. O que este estudo NÃO prova

Honestidade sobre os limites:

- **Software não é a causa do colapso acima de 6 dBm.** A escada de 24/08 mostrou a
  tela B fazendo 120 TX a 22 dBm com `preso=0` usando **este mesmo software**, e o
  `git` confirma que entre 24 e 28/08 mudaram apenas `RF_SF`, `TDMA_FRAME_S` e o
  air-time — nenhuma constante elétrica, nenhuma chamada de configuração.
- **Mas software é a margem.** As cinco divergências acima decidem *quanta folga* a
  alimentação precisa ter. Com fonte de 2–3 A, o software atual entrega 22 dBm; com
  o USB do PC, não entrega.
- Nenhum dos projetos pesquisados relata o degrau exato de 6→8 dBm que medimos. Ele
  continua sem explicação publicada — o que é esperado, já que ninguém mais alimenta
  um E22 por um header de transceptor CAN.

---

## 8. O que copiar

Em ordem de retorno, tudo derivado dos projetos acima:

1. **Buck 12 V → 5 V dedicado ao E22**, direto da fiação do carro, dimensionado para
   **1,5 A** (folga sobre o 1,2 A que o MeshAdv v2 usa). Isqueiro e cabo USB saem da
   cadeia de potência.
2. **LDO separado para a lógica de 3,3 V**, sem compartilhar com o rádio.
3. **Banco de capacitores do Ebytastic** — 220 µF + 4,7 µF + 1 µF + 0,1 µF, todos a
   menos de 10 mm dos pads 9/10.
4. **PCB com pads castelados e copper pour**, no lugar dos 16 fios.
5. **As cinco chamadas de software** — sobretudo `setPaRampTime()` mais longo e
   `getDeviceErrors()` no vigia de TX preso, que transforma cada `preso` num
   diagnóstico em vez de um número.

O **MeshAdventurer** é o esquemático de referência mais próximo do nosso caso:
mesmo módulo, mesmo GPS, mesma aplicação veicular, entrada de 9–28 V.

---

## Fontes

**Projetos:** [MeshAdv Pi HAT v2](https://github.com/KaliAssistant/MeshAdv_piHAT_v2)
· [MeshAdv Pi Hat](https://github.com/chrismyers2000/MeshAdv-Pi-Hat)
· [MeshAdv Mini](https://github.com/chrismyers2000/MeshAdv-Mini)
· [MeshAdventurer](https://github.com/chrismyers2000/MeshAdventurer)
· [Ebytastic](https://github.com/Sobollion/Ebytastic)
· [FakeTec](https://github.com/gargomoma/fakeTec_pcb)
· [SpaceBridge HAB](https://github.com/eigenlucy/spacebridgehabpcb)
· [Meshtastic-DIY-sx126x](https://github.com/nicolabalzano/Meshtastic-DIY-sx126x)

**Mesmo problema que o MTS:** [Spoke](https://github.com/FeruzTopalov/spoke)
· [Off-grid GPS race tracker — TDMA por PPS](https://hackaday.io/project/13013-off-grid-gps-race-tracker-client-and-server/log/44413-microsecond-resolution-tdma-using-gps-pps-on-arduino)

**Datasheet primário:** SX1261/2 Data Sheet Rev 2.2 (Semtech, dez/2024) — tabelas
3-6, 4-4, 5-2, 13-21, 13-40/41 e seções 5.1, 13.1.14, 13.3.6, 13.4.1/13.4.4.

**Medições e discussões:**
[S5NC — teste de potência](https://github.com/S5NC/CDEBYTE_Modules/blob/main/E22-900M30S%20power%20output%20testing.txt)
· [RadioLib #487 — OCP](https://github.com/jgromes/RadioLib/discussions/487)
· [RadioLib #1336 — DIO2 vs RF switch](https://github.com/jgromes/RadioLib/discussions/1336)
· [Meshtastic #3948 — E22](https://github.com/meshtastic/firmware/discussions/3948)
· [SX126x-Arduino #12](https://github.com/beegee-tokyo/SX126x-Arduino/issues/12)
· [`SX126xInterface.cpp`](https://github.com/meshtastic/firmware/blob/master/src/mesh/SX126xInterface.cpp)

**Verificado localmente:** RadioLib 7.6.0 em `Documents/Arduino/libraries/RadioLib`
(`SX126x.cpp:58`, `SX126x_config.cpp:719-735`, `SX126x_commands.h:167-174, 290`,
`SX1262.cpp:9-42, 154-156`).

**Estudos anteriores:** Estudo 1 (o rádio) e Estudo 2 (as telas), linkados no fim do
`MONTAGEM.md`.
