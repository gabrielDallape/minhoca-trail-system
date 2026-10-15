# Revisao adversaria - hardware/protoboard/README.md (2026-10-13)

Procedencia: **[M]** medido nos logs do repo · **[D]** datasheet/codigo lido · **[C]** conta a partir de [M]/[D] · **[E]** estimativa/hipotese.
Nada no repo foi alterado.

---

## 0. Veredito em 6 linhas

1. Os resistores/capacitores da secao 5 **nao quebram nada no firmware** (conferido contra `radio.h` e o `Module.cpp` da RadioLib), mas **estao no lugar errado para a hipotese que mais pesa** e o README promete mais do que eles fazem.
2. A hipotese "retorno de terra pelo chicote" (1.2 do README) **nao fecha na conta a 10 dBm**: da milivolts. A hipotese **RF (campo da antena + chicote de 15 cm = lambda/2 ressonante entrando nos GPIO do P4)** fecha na conta e explica mais dados. E ha um **resto que nenhuma das duas explica** (8 de 18 saltos de relogio sem TX).
3. O projeto deveria ser desenhado para **eliminar o chicote longo** (ou blinda-lo), **tirar a antena de dentro do carro** e **filtrar no lado do P4**, nao no lado do E22.
4. O GPS (P3-8/9/10, PPS com interrupcao RISING sem filtro) esta **fora da protecao** e e candidato concreto a INT_WDT por tempestade de interrupcao.
5. Lista de material: conversor A de 40 V e incoerente com o TVS de 33 V (grampeia em ~46 V); ferrite especificado como 2 A e de 1,5 A; faltam caixa, trava, alivio, GPS no teto.
6. Antes de comprar qualquer coisa: o teste de carga 50 ohm **precisa de um controle positivo** (reproduzir a falha primeiro), senao "sem falha na carga" nao prova nada - a bancada a 10 dBm ja dava 0 eventos.

---

## 1. Resistores em serie e 100 pF - contra o firmware e os limiares

### 1.1 O que o firmware faz e se algo quebra

| Linha | Firmware ([D] `radio.h`) | Com a peca proposta | Quebra? |
|---|---|---|---|
| SPI (SCK/MOSI/NSS/MISO) 330 ohm | `SPISettings(2000000,...)`, modo 0; `radioHwResponde` faz GetStatus cru a 2 MHz | RC 330 x ~25 pF = 8 ns contra 250 ns de meio periodo [C] | Nao |
| NRST 1 k | pulso LOW de 500 us (`radioHwChecaFios`, `radioHwResponde`), depois 200 us e espera BUSY | divisor 1 k / ~50 k interno: 3,3 x 1/51 = **65 mV** no pino ao puxar baixo [C] | Nao |
| BUSY 1 k + 100 pF | RadioLib (`Module.cpp:386`, lido): depois de subir o NSS espera **so 1 us** e entao le o BUSY; se ja estiver baixo segue | tau = 100 ns; o P4 so ve alto em ~1,4 tau = **140 ns** (VIH 0,75 VDD) [C]; o SX1262 sobe o BUSY ate ~600 ns depois do NSS [D, conferir tab. 8-1]. Soma ~740 ns < 1 us | **Nao, mas a folga e ~260 ns.** Nao aumentar: 1 nF ou 10 k **quebram** (tau 1 us > 1 us de espera -> comando enviado com o chip ocupado, falha intermitente) |
| DIO1 1 k + 100 pF | `setDio1Action(loraOnIrq)`, borda de subida, carimbo em `esp_timer` | atraso fixo ~140 ns, irrelevante para TDMA de ms | Nao |
| `radioHwChecaFios` | conta amostras de BUSY x DIO1 por 5 ms | RC de 100 ns nao muda contagem de ms | Nao |
| TXEN/RXEN 1 k + 10 k | `setRfSwitchPins`, saida push-pull | **3,3 x 10/11 = 3,0 V** [C]. EN do TLV62568 (buck do PA) VIH max 1,2 V [D, via S5NC]. Se houver pull-down interno nao documentado, mesmo de 10 k: 3,3 x 5/6 = 2,75 V; de 2 k: 2,2 V - ainda > 1,2 V | Nao. Robusto. **Medir** a resistencia pad 7/6 -> GND com o modulo desligado para fechar a duvida |

Conclusao: nada quebra **com estes valores exatos**. Escrever no README a regra "BUSY: RC <= 150 ns" com a razao (RadioLib espera 1 us).

### 1.2 O que o README promete e as pecas nao entregam

- **"Cortam a injecao"** - nao cortam, limitam. Com o E22 sem 5 V, o NSS em repouso ALTO (o firmware deixa `NSS=HIGH`, `radioHwResponde`) empurra (3,3 - 0,6)/330 = **~8 mA** pelo diodo ESD [C]. O SX1262 em standby consome ~0,6-1,5 mA [D]: 8 mA **sobra para alimenta-lo por tras** - o trilho interno sobe e o chip fica no estado indefinido de sempre. Correcao: (a) **ACC unico ligando os dois** (ja previsto) e (b) o firmware por NSS/NRST/TXEN/RXEN/SPI em entrada com o radio mudo (ja listado como pendente - deveria subir para obrigatorio). Se quiser hardware: NSS com **1 k** (nao 330) - a 2 MHz o NSS e lento, cabe - corta para 2,7 mA.
- **"100 pF corta a RF de 915 MHz"** - um C0G de **furo** com 5 mm de perna tem ~5 nH: ressoa em ~225 MHz e a 915 MHz e um **indutor de ~27 ohm** [C]. Ainda divide com o 1 k (~ -30 dB), mas o resistor de 1/4 W tem ~0,3 pF em paralelo (~580 ohm a 915 MHz) [E] e a atenuacao real cai para ~20-25 dB. Usar **SMD 0603/0805 C0G 100 pF** soldado direto no pino + resistor SMD 0805.
- **O lugar errado.** O chicote de 15 cm e ~lambda/2 a 915 MHz (lambda = 32,8 cm) [C]. A RF que ele capta entra **no P4**. Um filtro na placa do E22 nao protege o P4 de nada do que o cabo pega depois dele. O README diz "ao GND do lado da tela", mas o P1/P3 e um PH na placa da tela: na pratica a peca acaba na placa do radio. Correcao: ver 2.4 (adaptador na tela ou chicote curto/blindado).
- **Linhas do GPS sem nada.** P3-8 (TXD->IO4), P3-9 (RXD<-IO3), **P3-10 PPS->IO2 com `attachInterrupt(..., RISING)`** (`hal/gps.h:238`), P3-12 3V3, tudo no mesmo cabo, ao lado do SPI. O PPS e uma entrada de interrupcao **sem pull, sem filtro**: RF retificada no pino = bordas falsas = tempestade de ISR = **INT_WDT** (o motivo de 4 dos 10 reinicios). Correcao: **1 k + 100 pF C0G SMD no IO2, IO4** (o lado P4), 1 k no IO3; idealmente **Schmitt 74LVC1G17** no PPS e no DIO1, junto da tela.

### 1.3 Detalhe que falta na tabela

Posicao de cada resistor: SCK/MOSI/NSS/NRST/TXEN/RXEN protegem o **E22** (vao no lado do E22); MISO/BUSY/DIO1 (e GPS TXD/PPS) protegem o **P4** (vao no lado da tela). O README so diz isso para MISO e para os capacitores.

---

## 2. Causa dos reinicios - qual hipotese os dados sustentam

### 2.1 Ground bounce a 10 dBm: a conta nao fecha [C]

- Potencia: 10 dBm no chip = ~20 dBm na antena (PA +10 dB) [D] -> 5 V puxando ~0,15-0,25 A [E, faixa dada].
- Rampa do PA em 800 us (`radio.h:124`) [D]: di/dt = 0,25 A / 800 us = 300 A/s -> queda indutiva desprezivel (5 nH x 300 A/s = 1,5 nV).
- DC: mesmo que **100 %** da corrente voltasse pelos 3 GND do PH (~16 mOhm, estudo B 1.3): 0,25 x 0,016 = **4 mV**. Com so 1 GND bom (~50 mOhm): **12 mV**. Para injetar pelos diodos ESD precisa de **~0,5 V** [D tipico]. Faltam 40x.
- Pelo laco de terra dos dois bucks a fatia que passa pelo PH e ~10 % (12 mOhm contra 75-140 mOhm, estudo B) -> **< 1 mV**.
- O que **pode** dar centenas de mV no carro e o laco com o chassi (partida, alternador) - mas isso **nao correlaciona com TX**, e os 10 de 10 tem TX em voo.

=> **(b) ground bounce pela corrente do PA nao explica a 10 dBm** [C]. So vira candidato com contato aberto **e** um caminho onde a corrente do PA nao tenha outra volta - nao e a montagem descrita.

### 2.2 RF: a conta fecha [C]

- Campo a d da antena: E = sqrt(30 P G)/d. Com 0,1 W e G 1,6: **11 V/m a 20 cm, 22 V/m a 10 cm**; a 14 dBm (0,25 W) **17 / 35 V/m**. Dentro do carro, a cabine metalica reflete e o campo sobe [E].
- Imunidade de equipamento comum: 3-10 V/m (IEC 61000-4-3) [D]. Uma placa de desenvolvimento com painel DSI, FPC de toque e chicote solto **nao foi ensaiada a nada**.
- O chicote de ~15 cm e um dipolo meia-onda ligado direto nos GPIO; o cabo USB-C de 30-100 cm, outro.
- Explica: correlacao com TX (10/10) [M]; dependencia de **geometria/unidade** (FaustoA limpa e gino falhando na mesma mesa a 14 dBm [M]); **bancada a 10 dBm com 0 eventos em 200 unidade-min e carro a 10 dBm com 10** [M] (no carro a antena esta mais perto e ha cavidade metalica); erro de **barramento** no registrador do GPIO8 (I2C do toque, cujo FPC e antena) [M]; INT_WDT (tempestade de ISR em PPS/DIO1) [E].
- Contra: nenhum dado mostra falha com carga de 50 ohm - porque o teste nunca foi feito.

### 2.3 O resto que nenhuma das duas explica

- **8 de 18 saltos do relogio sem TX em voo** (A, sec. 2) [M]; com duty de 4-21 % o acaso daria 1-4. Domingo: saltos tambem em boots POWERON sem reinicio (boot488, 42, 48, 49) [M].
- O padrao "bits >= 26 zerados em bloco" e assinatura **digital** (leitura de registrador corrompida no barramento), nao analogica. O P4 destas telas e **rev 1.3 (eco2)** [D, CLAUDE.md]; a errata da Espressif lista problemas de barramento/MSPI (ex.: MSPI-751 "data errors caused by asynchronous timing", APM-560 "AHB access may block PSRAM/flash") corrigidos em revisoes posteriores [D, pagina de revisoes da errata; aplicabilidade a rev 1.3 NAO verificada].
- Implicacao: **mesmo o hardware perfeito pode nao zerar os saltos.** O projeto precisa de um criterio que separe: rodar a tela **sem radio** (MTS_TEM_RADIO 0) 2 h no carro com o motor ligado e contar `relogioVoltou`. Se > 0, parte do problema e silicio/firmware.

### 2.4 O projeto deve cobrir todas de uma vez (ordem de valor)

1. **Antena no teto por coaxial** (ja no README) - corta o campo na tela por 20-30 dB [E] e o ruido. E o item que mais cobre hipoteses.
2. **Matar o chicote de 15 cm.** Opcoes, da melhor para a pior:
   - placa do radio **encaixada direto** nos headers P1/P3 da tela (mochila, <= 2-3 cm de pino), com o E22 na face oposta ao painel e a antena remota; ou
   - chicote **blindado** (cabo multivias com malha, malha soldada 360 graus ao plano de GND da placa do radio e ao GND do conector da tela), <= 15 cm; ou
   - manter o PH, mas com uma **placa-adaptador de filtro encaixada na tela** (R + C SMD + Schmitt do lado do P4) e o cabo depois dela.
3. **Clamp de ferrite NiZn para UHF** no chicote e no USB-C, **junto da tela** (material para 300 MHz-1 GHz, ex. Fair-Rite mix 61 / Wurth serie de alta frequencia) [E: material a confirmar na curva]. Mix 31/43 de "cabo de monitor" rende pouco a 915 MHz.
4. **E22 numa caixa de aluminio** (ou a placa inteira do radio numa caixa metalica), SMA de painel na parede, chicote saindo por um furo pequeno. A blindagem do proprio E22 nao cobre o pad 21/IPEX nem o pigtail.
5. **Tela + radio na mesma caixa metalica** so se a antena estiver FORA (no teto). Antena dentro de caixa metalica com a tela = pior caso.
6. **Laco de alimentacao minimo:** o USB-C da tela sai **da mesma placa** do ponto estrela e corre **junto (amarrado/trancado) com o chicote** - area de laco ~0. Hoje o README desenha o USB-C saindo do conversor A e o chicote saindo do radio, como dois caminhos separados.
7. **Filtro de RF na entrada do 5 V do E22** ja esta (ferrite); falta **100 pF C0G SMD** no 5 V e no 3V3 do GPS, no conector, para curto-circuitar RF que o par de alimentacao pegue.

### 2.5 Ordem dos testes - corrigir o roteiro

- **Teste 4 (carga 50 ohm) precisa de controle positivo**: primeiro reproduzir a falha na bancada **com a geometria do carro** (antena a 10-20 cm da tela, 14 dBm, cronometrar ate a 1a falha, ou contar `relogioVoltou`/h). So depois trocar por atenuador 30 dB + carga, **mesmo tempo**. Sem o controle, "zero falhas na carga" e o que ja se via a 10 dBm sem carga.
- Teste novo, barato: **antena no teto por coax, tudo o mais igual, no carro, 1 h a 10 dBm.** Responde reinicio e ruido ao mesmo tempo.
- Instrumentar (firmware, sem mexer agora): contador de ISR por segundo em **PPS e DIO1**, gravado na caixa-preta; se uma ISR passar de ~100/s durante TX, e tempestade.
- Teste "tela sem radio" (2.3) para separar silicio.

---

## 3. Ruido de -87/-89 dBm

### 3.1 Conta e suspeito novo

- kTB em 125 kHz = -174 + 51 = -123 dBm; + NF ~6 dB = **~-117 dBm** [C/D]. Medido -87/-89 [M]: ~30 dB de excesso.
- **Suspeito concreto que o README nao cita:** o DSI roda a **900 Mbps por lane** (`hal/display.h:74`, `EK7_LANE_MBPS 900`) [D]. A lane de clock e DDR a **450 MHz**; o **2o harmonico cai em 900 MHz, a 15 MHz do canal de 915** [C]. Qualquer desequilibrio do par diferencial / duty gera harmonico par, e as transicoes LP<->HS do video modulam isso em bandas laterais largas. O BSP da Waveshare usa 1000 Mbps (harmonico em 1000 MHz) [D, comentario do proprio display.h]. Celulares fazem exatamente isso: **troca dinamica do clock DSI para tirar o harmonico da banda do modem** (patentes US10241559, US10630396 e outras) [D, literatura; nao achei medicao publicada em dBm de painel DSI a 900 MHz].
- Outros harmonicos: XTAL 40 MHz -> 920 MHz (23o) a 5 MHz do canal; DPI 52 MHz -> 884/936 [C]. Fora do canal de 125 kHz, mas o espalhamento do PLL pode encostar.

### 3.2 O que entra no projeto/teste

1. **Varredura de RSSI com o proprio SX1262** de 902 a 928 MHz em passos de 250 kHz (GetRssiInst), com painel ligado/desligado e backlight em 0/50/100 %. Diz se o ruido e **espuria** (pico em frequencia, resolve trocando o lane rate ou o canal) ou **banda larga** (resolve com distancia/blindagem). Custo zero, ja no hardware de hoje. Deveria ser o Teste 1, passo 0.
2. Experimento: `EK7_LANE_MBPS` 900 -> 1000 e comparar o piso. Se cair, e o DSI.
3. Distancia: antena no teto (alvo -110 dBm ou melhor).
4. Backlight: o boost do backlight chaveia em centenas de kHz a ~1 MHz com bordas rapidas; o PWM do LEDC (kHz) liga/desliga o boost. Testar brilho 100 % (PWM parado) x 50 %.
5. Caixa de aluminio no radio + ferrite no chicote (2.4), que tambem cortam o caminho conduzido pelo GND do chicote ate o modulo.

---

## 4. Uso automotivo real - o que falta

| Item | Problema | Correcao |
|---|---|---|
| Caixa | README nao tem | Caixa de **aluminio** para o radio (ex. Hammond 1590B ou similar), IP54+, prensa-cabos PG7, **respiro com membrana** (condensacao) |
| Fixacao do E22 | "fita dupla-face" nao aguenta trilha (vibracao + 70 C amolece adesivo) | Castelados soldados no plano + **epoxi ou RTV neutro** (nunca RTV acetico: corroi) nos cantos |
| Conectores PH | PH nao tem trava positiva; e o conector que ja falhou (FaustoA, RXEN/DIO1) [M] | Lado da tela: **soldar o chicote nos pinos do header** por tras, ou PH + **cola quente/RTV** + presilha de alivio a 2 cm. Lado do radio: **JST GH** (trava) ou Molex Micro-Fit 3.0 |
| XT30 no 5 V | sem trava | **Micro-Fit 3.0 2 vias** (trava) ou solda |
| 12 V | borne de parafuso afrouxa com vibracao | **Superseal/Deutsch DT 2 vias** ou terminal ilhos crimpado |
| IPEX/u.FL | ~30 ciclos e solta com vibracao | **RG316 soldado no pad 21** (README ja oferece; tornar o padrao), ou IPEX com ponto de RTV + Kapton |
| Antena no teto | base magnetica + galho na trilha = arrancada; coax RG58 nao passa na borracha da porta | base magnetica **com cabo de seguranca** ou NMO parafusada; passagem por **jumper chato de porta** ou trecho curto de RG316 na borracha |
| Temperatura | painel LCD tipico opera 0-50 C e E22 ate 85 C [D]; painel no sol do painel do carro passa disso | sombra/viseira; eletroliticos 105 C long-life (ja); Teste 6 tambem com a **tela** ao sol |
| GPS | com a antena LoRa no teto e a tela dentro, o GPS fica onde? NEO-7M "mini" tem patch ceramico; dentro do carro perde C/N0. MONTAGEM.md fala em ATGM336H, o usuario usa NEO-7M - documentos divergentes | GPS com **u.FL/SMA + antena ativa magnetica no teto**, **>= 30-50 cm da antena LoRa** (MONTAGEM passo 4), medir queda de C/N0 com TX a 14 dBm. Modulo GPS perto da tela, cabo curto ate o P3 |
| Poeira/umidade | nada previsto | caixa IP54+, **verniz acrilico** (conformal) na placa, mascarando conectores e o E22 |
| Alivio de tensao | nao previsto | presilha/abracadeira a <= 3 cm de cada conector, laco de gotejamento |
| Desligar | ACC liga os dois: bom | conferir que o ACC nao cai na partida (alguns carros cortam o ACC ao girar a chave) |

---

## 5. Lista de material (sec. 8) - itens errados ou arriscados

| Item | Problema | Correcao |
|---|---|---|
| **Conversor A "entrada 8-40 V"** | Incoerente com a propria regra do README (60 V). O TVS 1.5KE33CA grampeia em **~45,7 V a Ipp** [D] -> acima de 40 V o conversor A morre no pico que o TVS deixa passar | Conversor A tambem **>= 60 V de entrada** (mesma familia do B, ou buck 5 V/3 A de 60 V) |
| **Ferrite BLM31PG601SN1 "2 A"** | E **1,5 A a 85 C, 0,08 ohm** [D, Murata via distribuidores]. A 0,25-0,65 A esta dentro, mas a impedancia de 600 ohm cai muito com corrente DC; 0,08 ohm x 0,65 A = 52 mV de queda | Corrigir a ficha para 1,5 A. Aceitavel ate p14. Para p20 (~650 mA) preferir o indutor blindado 2,2 uH / >= 2 A da alternativa |
| **LM2596HV** como alternativa | modulos "HV" de loja vem com capacitor de entrada de 50 V ou CI falsificado de 40 V [E, recorrente] | conferir o eletrolitico de entrada >= 63 V e marcar o CI; testar com 30 V na bancada antes |
| **TPS54560 "modulo"** | pouco comum como modulo pronto; ajuste de 5,05 V por trimpot deriva com vibracao/temperatura | se modulo com trimpot, **trocar o trimpot por resistor fixo** e travar com verniz. Medir 5,0-5,15 V a quente |
| **L1 10-22 uH + 100 uF + conversores** | filtro LC de entrada com conversores de impedancia negativa: Z0 = sqrt(22u/100u) = 0,47 ohm, f0 = 3,4 kHz [C]; amortecimento depende do ESR do 100 uF | 100 uF **eletrolitico comum** (ESR >= 0,2 ohm), nunca polimero (mesma logica do C1) - escrever isso na lista |
| **D2 SB560 em serie** | 0,5-0,7 V de queda; na partida (6 V) sobram ~5,3 V para dois bucks que precisam de ~5,6 V para dar 5 V [C] | aceitavel (o Teste 3 ja espera POWERON/BROWNOUT); alternativa sem queda: P-FET + zener (estudo B 2.3) |
| **D1 1.5KE33CA bidirecional** | ok (Vrwm 28,2 V > jump start de 24 V). Com D2 em serie a polaridade invertida ja esta coberta | sem mudanca |
| **100 pF C0G** sem encapsulamento | de furo nao funciona como desacoplador a 915 MHz (1.2) | **0603/0805 SMD** |
| **Resistores 1/4 W** | ok para BUSY/DIO1/SPI; parasitas reduzem filtro (1.2) | 0805 SMD nas linhas que entram no P4 |
| **Fio AWG 20 silicone** | ok | — |
| **Cabo USB-C "5 A"** | so existe com e-marker e e irrelevante a 5 V/1-2 A; o que importa e AWG do VBUS/GND | USB-C com **AWG 20-22 no VBUS/GND**, <= 50 cm (medir queda com FNB48) |
| **Antena "<= 5 dBi"** | ok; falta exigir **plano de terra/mola** para trilha | ver tabela 4 |
| **Faltando** | caixa de aluminio, ferrite clamp NiZn, Schmitt 74LVC1G17 x2, filtros do GPS, conectores com trava, GPS com antena ativa, membrana de respiro | adicionar |

---

## 6. Contradicoes internas encontradas

- README 1.2 diz "melhor encaixe [E]: retorno de terra"; o estudo B 1.3 calcula que so ~10 % do retorno passa pelo PH - os dois documentos, juntos, desmentem a hipotese principal. A 1.2 deveria rebaixar (b) e subir (c).
- `pinos.h:45` diz "pull-down de 10k no chicote" so no TXEN; README pede tambem no RXEN; ESTUDO_PECAS 6 tirou o do RXEN. Decidir e alinhar (recomendo **manter os dois**: custa 0,3 mA cada).
- MONTAGEM.md fala em GPS ATGM336H; usuario e CLAUDE.md usam NEO-7M.
- CLAUDE.md "29-30 dB medidos" no switch: ESTUDO_PECAS 6 ja apontou que e do datasheet do PE4259, nao medicao; e o estudo A usa "30 dB e a isolacao medida" para concluir RXEN sem contato. Conclusao continua plausivel, mas a base e [D de outro chip], nao [M].

## Fontes externas consultadas
- Espressif, ESP32-P4 SoC Errata, historico de revisoes: https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32p4/revision-history/index.html
- Murata BLM31PG601SN1L (1,5 A, 0,08 ohm): https://ch.farnell.com/murata/blm31pg601sn1l/ferritperle-600-ohm-1-5a-1206/dp/2672840
- Troca de clock MIPI contra desense: https://patents.google.com/patent/US10241559 ; US10630396
- RadioLib `Module.cpp:382-390` (1 us de espera do BUSY), lido na copia local.
