# Changelog

Histórico das fases do projeto. As datas são aproximadas (marcos, não releases formais).

## 2026-10-01 — simulador (tools/sim) e o que ele achou

`tools/sim/`: o firmware/mts DE VERDADE (core, app, quase todo o hal) compilado uma vez
por tela num container Docker, as telas conversando por um "ar" calibrado com o carro de
09-30. 13 cenários de trilha, todos passando; com o bug do ícone recolocado, falha.
Refatoração para tela e simulador usarem o mesmo código: `app/boot.h`, `app/sessao.h`,
`frotaParaDesenhar()`.

- **Ícone do líder sumia no seguidor** (o dia perdido de 09-30): `agora - carimbo` em
  `uint32_t` dava a volta e apagava o carro da frota. `core/tempo.h` (`tempoDesde`).
- **ENTRAR lista os grupos no ar** (`core/vizinhos.h`): um toque entra, sem código.
- **Rádio que volta de fábrica** depois de perder o 5 V entre dois TX: transmitia e
  escutava errado sem travar nada. `radioHwConfigOk()` (GetPacketType) a cada 5 s.
- **GPS do líder caindo** apagava o ícone dele no seguidor: agora fica o último ponto,
  vazado, "SEM GPS". `frotaSinal` passou a medir o RÁDIO (ultimoMs), não o ponto.
- **Catch-up**: a frente do caminho manda (contígua ao topo do seguidor); preenchimento
  só com sobra. Antes, a 40 km/h e sem perda nenhuma, 173 de 352 quadros iam em
  preenchimento e a frente ficava 1-2 quadros atrasada; agora 11 de 352.
- Menor, não corrigido: o contador `perdi` do tdma_core sobe quando a âncora muda de
  fase sem que um TX tenha faltado (só diagnóstico).

## 2026-09-29 — firmware/mts: revisao depois das duas quedas de sessao

A sessao que escreveu o `firmware/mts` caiu duas vezes. Tres revisores leram as 42
fontes: **nenhum sinal de trabalho interrompido** (nada pela metade, assinaturas
batem, compila com e sem WiFi). Mas acharam defeitos de logica, corrigidos aqui.
**Compila em 1.092.650 B; bancada toda verde (mts_core 88, audit 71, selftest 63,
tdma_replay 56, catchup 48, eleicao 160). Nada gravado nem testado em placa.**

- **Boot-loop com o radio mudo.** O `begin()` do RadioLib com o chip sem resposta
  leva >= 10 s (`findChip`: 10 resets x 1 s de `standby()`), e o WDT e de 10 s: o
  5 V do E22 caido virava reinicio eterno, sem mapa nem GPS. Agora um `GetStatus`
  cru (`radioHwResponde`, microssegundos) decide antes, e o `begin()` roda com o
  WDT pausado. O timeout de SPI de 100 ms vale ja no primeiro `begin()`.
- **Catch-up travado para sempre** (o bug do campo, em outra forma). Dois caminhos:
  o vao que o lider ja nao guarda (lider reiniciou, ou vao > buffer) prendia o
  ack do seguidor; e um seguidor surdo, pela regra do "menor ack manda", ditava o
  bloco de todo mundo. Correcao: o lider anuncia o ponto mais antigo que guarda
  (campo `ackDe` do pacote dele, que ia vazio) e o seguidor passa por cima dos
  vaos abaixo dele (**piso**); o seguidor manda o seu **topo** (campo `base`, que
  ia vazio) e so conta no bloco quem esta ouvindo o lider (`ROTA_OUVE_PONTOS`).
  Mesmo tamanho de pacote, mesmo air-time.
- **A potencia nao baixa mais sozinha em caso nenhum.** O "modo seguro" (2 dBm apos
  3 reinicios violentos) virou alarme: faixa vermelha, potencia intacta. A
  migracao de NVS esquema 0 so sobe ao padrao, nunca desce. `p30` no serial virava
  3 dBm (e gravava): agora le ate 2 digitos. Troca pedida com TX em voo e aplicada
  quando o radio libera (antes a NVS ficava com o novo e o chip com o velho).
- **Rastro zerado ao abrir a trilha**, como no v101: o lider transmitia tudo desde o
  boot e quem entrava passava ~20 min recebendo historico.
- **Caixa-preta** fecha a conta a cada etapa: antes, toda morte DENTRO de uma etapa
  (as que interessam) deixava a caixa invalida e o contador de reinicios nunca
  passava de 1. Carimbos anteriores ao `caixaBoot` nao apagam mais a caixa velha.
- **Holdover em 4 quadros** (24,5 s; `TDMA_HOLDOVER_QUADROS`, padrao 2 no
  `tdma_core.h` para bancada e `mts_p4`): com 46-62 % de entrega, 2 quadros tinham
  0,5 s de folga e cada beacon perdido perto do roster virava eleicao.
- Menores: `km[]` da rota nao andava com a rota cheia (distancia pela trilha errada
  apos ~14 km); o `redePodeGastar` liberava 250 ms DENTRO da janela de TX aberta;
  com 7 outros carros o lider podia sumir da lista (agora lider e alerta primeiro);
  "SEM SINAL HA N s" congelava comigo parado; alerta de quem esta A FRENTE nao
  pintava trecho vermelho; ao sair do grupo o aparelho seguia anunciando LIDER;
  epoca sorteada podia repetir a anterior (agora +1 na NVS); `/status` do WiFi
  escrevia fora do buffer.
- **Testes**: o `catchup_test.js` espelhava o `mts_campo` com constantes a mao e por
  isso passou verde com dois dos bugs acima. Agora espelha o `firmware/mts`, le o
  `params.h`, confere no C as linhas que decidem, e tem os cenarios A (lider
  reinicia), B (vao > buffer), C (seguidor surdo), D (rota cheia) - A, C e D
  **falham com a logica antiga**. O `mts_selftest` ganhou os mesmos casos com os
  headers reais (roda na placa).

## 2026-09-28 — firmware/mts v200: um firmware so, em camadas, para a tela P4 de 7"

Reescrita do `mts_campo` v101 + `mts_p4` em `firmware/mts/` (43 arquivos, `core/`
`hal/` `app/`), alvo unico Waveshare 7B. **Compila em 1.089.944 B (32 %), globais
92.088 B, zero warnings com -Wall; variante `MTS_TEM_WIFI 1` compila em 1.862.868 B.
Nada foi gravado nem testado em campo.** `mts_campo`, `mts_p4` e `grupo_ws` intactos.

- **Quadro de 6 s com 12 slots de 500 ms** (decisao do usuario). Carros nos slots 0..7;
  8..11 sempre vazios para o ruido. Contas refeitas em `core/params.h` com
  `static_assert`: janela do laco 63 ms, vigia 617 ms, holdover 12,5 s, sinal velho
  30 s (p = 3 % a 50 % de perda), some da lista 90 s, extrapolacao ate 12 s.
- **Passo do trajeto 10 m** (era 5): com quadro de 6 s e catch-up em quadros
  alternados, a 30 km/h o passo de 5 m abria um buraco novo a cada preenchimento
  e a rota nunca convergia. A conta esta no `params.h` e no teste.
- **Protocolo versionado** (`core/proto.h`, byte 0 = 2; sala em 16 bits). Pacote de
  outra versao e ignorado, contado (linha V do diario, celula do diagnostico) e
  avisado na tela — o problema das telas v7/v8 do campo.
- **Posicao dos outros carros extrapolada** por rumo e velocidade entre pacotes
  (`core/frota.h`); marcador vazado e "SEM SINAL Ns" na faixa apos 30 s; banner
  "SEM SINAL DO LIDER" para o seguidor.
- **Roster intercalado** (falas 1, 3, 5): nunca dois quadros sem beacon — com 6 s de
  quadro, os tres rosters seguidos da v101 (I5) partiriam a rede no arranque.
- Corrigidos da auditoria: **C1** (par PPS/NMEA confere idade e descarta backlog),
  **C4** (`redePodeGastar` usa `tdmaUsUntilMySlot`), M14 (IRQ 64 bits atomica),
  M16 (switch RF antes do `begin`), M18 (roster reanunciado ao trocar slot/sala).
- **Watchdog de tarefa** consciente (loopTask, 10 s, alimentado so no servico);
  sprite do mapa, anel do diario e caches alocados uma vez no boot; configuracao
  com esquema (`cfgv 2`) e migracao testada; maquina de estados explicita.
- Testes: `web/bancada-trilha/mts_core_test.js` (77 checagens: espelha o core e
  **le** os `.h` para refazer as contas e conferir as armadilhas do radio) e
  `firmware/mts_selftest/` (o core na placa, 344.100 B). Os cinco testes antigos
  continuam passando (audit 71, selftest 63, tdma_replay 56, catchup 25, eleicao 160).
- Fora, de proposito: tela de 5", recuo automatico/resgate de boot, simulador de
  outros carros sem radio. `tools/publica_ota.ps1`/`frota.ps1` ainda apontam para
  `mts_p4`.


## 2026-09-28 — mts_campo v101: o que o campo de 27 minutos ensinou

Dois cartões voltaram do campo (`dados_campo/`): entrega de 46–62 % a −91…−101 dBm
sem relação com distância, as duas telas reiniciadas por watchdog antes do teste e o
"resgate de boot" tendo baixado a potência de 10 para 6 dBm **na NVS, sem ninguém
saber**. Só `firmware/mts_campo/` mudou (mais testes e um script). **Compila; nada foi
gravado nem testado em campo.**

- **Potência nunca muda sozinha.** Removidos o recuo do `radio.h` e o resgate do boot.
  Fica só um *modo seguro em RAM* (`caixa.h`): após 3 reinícios violentos seguidos o
  boot roda com teto de 2 dBm, faixa vermelha permanente na tela, linha no diário; a
  NVS não é tocada e o 'p'/tela de diagnóstico desligam. Migração única v100→v101
  (chave `pwrv`) devolve a potência ao `RF_PWR`, porque o 6 gravado veio do recuo.
- **Diário em toda tela.** `servico.h`: um gancho único chamado pelo laço do mapa também.
  No v100 o mapa chamava só o rádio e o log ficou com 57 linhas em 27 min.
- **Linhas R/T/N.** RSSI/SNR/erro de frequência por pacote (inclusive CRC ruim), perda
  contada por sequência por remetente, air-time **medido** por transmissão, ruído de
  fundo lido no slot vazio. Cabeçalho do CSV descreve as colunas de verdade.
- **Caixa-preta** em `RTC_NOINIT_ATTR`: etapa em curso (startTransmit, BUSY, flush do
  cartão, pintura, push) e contadores; relatada no serial e no diário após reset
  violento, com o código cru do ROM (0x10 LP_WDT, 0x12 super WDT…). Guarda contra
  **relógio que volta** (a FaustoA registrou millis 38639→6098 no mesmo segundo UTC).
- **`perdi` de 10–30 % da gino:** o mapa era pintado logo após receber o pacote do
  slot 0, em cima da janela do slot 1. Agora `pinta()` pergunta ao orçamento antes.
- **Catch-up do trajeto** (`rota_core.h`, sem hardware): pontos numerados, rota ordenada
  com vão conhecido tracejado, dois acks (até onde é contínuo / desde onde tenho),
  época por boot do líder. Exigiu **4 slots de 500 ms** (o roster de 23 B já estourava
  o slot de 250 ms). Teste: `node web\bancada-trilha\catchup_test.js` (25 PASS) e
  `firmware/catchup_selftest/`.
- Vigia de TX preso passa a depender do maior pacote (`RADIO_VIGIA_MS`), timeout de
  SPI da RadioLib cai de 1000 para 100 ms, ancoragem por beacon corrige o air-time
  pelo tamanho real do pacote.

## 2026-09-08 — a pesquisa que refez o diagnóstico do rádio

Quinze frentes de pesquisa em paralelo (datasheets Semtech e Ebyte lidos na íntegra,
esquemático oficial da Waveshare, código do RadioLib e do Meshtastic, oito projetos
abertos com E22, papers de propagação, norma ANATEL) mais uma auditoria linha a linha
do nosso caminho de rádio. Saiu daí que **quase tudo que o projeto acreditava sobre o
rádio estava errado em algum grau** — e que o rádio nunca foi o problema.

Documentos novos: `docs/ESTUDO_E22.md`, `docs/PLANO_7B.md`, `docs/PROTOCOLO_FASE0.md`,
`docs/AUDITORIA_RADIO.md`.

### O que estava errado

- **O ganho do PA é +10 dB, não +7,25.** A tabela oficial do módulo montado (manual
  E22-M V1.2) dá: pedido 14 → **24 dBm** na antena; 20 → 30. Os +7,25 eram do datasheet
  do CI de PA. **A frota a 6 dBm irradia ~16 dBm, não 13,25** — todo o orçamento de
  link do projeto estava 3 dB pessimista.
- **A escada de potência de 28/08 não é interpretável.** A `paOptTable` da RadioLib
  7.5+ não é monotônica: "6 dBm" programa `paDutyCycle 3, hpMax 1, paVal +21`. Ela
  comparou quatro configurações arbitrárias de amplificador, não uma rampa de potência.
  O Meshtastic desliga essa tabela em toda placa com PA externo.
- **A regra do `setCurrentLimit` estava obsoleta desde 2019.** A RadioLib salva e
  restaura o OCP em volta do `SetPaConfig` (commit `621da8a`); a ordem contra o
  `setOutputPower` é indiferente. O que importa é vir depois do `begin()`.
- **O módulo da tela A pode não ser defeituoso.** O `XOSC_START_ERR` é **esperado** no
  POR quando há TCXO (datasheet 13.3.6) e o registrador é latcheado — e o `e22_ping`
  nunca chamou `ClearDeviceErrors`. O laudo de 24/08 leu exatamente esse aviso de boot.
- **O `Core_5V` não serve para alimentar o E22.** No esquemático oficial ele é ligação
  direta ao trilho, **sem fusível, sem diodo, e com 100 nF de capacitância local**.
- **Os 470 µF eram folclore.** Nenhum manual da Ebyte dá valor de capacitor; o consenso
  dos projetos que funcionam é 100–220 µF + 10 µF + 100 nF nos pads do módulo.
- **`tdma_core.h:56` errava o holdover em 60×** — dizia "3,05 min", são 3,05 horas; com
  a guarda atual, ~33 minutos são seguros. A conclusão do arquivo estava invertida.

### O mecanismo, finalmente

Quatro fatos do datasheet que se multiplicam: o SX1262 exige **VBAT ≥ 3,3 V para
+22 dBm**; a corrente é **inversamente proporcional a VBAT**; o **PA é alimentado
direto do VBAT**, sem filtro (o DC-DC serve só o core); e o consumo a +22 dBm é
**118 mA** contra um OCP de 140 — 19% de margem. Tensão cai → corrente sobe → cruza o
OCP → o TX aborta **sem TX_DONE**. O OCP é comparador, não atenuador: daí o degrau.

A cadeia de energia explica o resto: isqueiro → adaptador USB → **cabo de 28 AWG
(639 mV de queda a 1,5 A)** → header sem capacitância. Chegam **4,3 V** onde o manual
exige 5.

### Quatro bugs críticos, nenhum de rádio

1. **`gps.h:143`** — o par PPS↔NMEA não verifica idade. Um bloqueio do laço põe o
   `anchorSec` 1 s errado, o que **inverte a fase do quadro** — e o nó anuncia
   `TDMA_FL_PPS`, então todos os outros o seguem. Colisão silenciosa.
2. **`startTransmit()`** pendura o firmware para sempre se o BUSY ficar alto (laço sem
   timeout na RadioLib).
3. **O recuo automático de potência** era uma catraca: 30 s de brownout levavam de 22
   para 2 dBm gravados na NVS, sem volta.
4. **`g_podeBloquear`** responde "pode" em 91% das consultas; `tdmaUsUntilMySlot()`
   existe desde sempre e nunca foi chamada.

### Etapa 1 do protocolo, aplicada

Instrumentação e segurança — nada que mude como o rádio transmite:

- `clearDeviceErrors()` no boot e no `begin()` de recuperação
- `getDeviceErrors()` no vigia, decodificando XOSC / PLL / **RAMPA_DO_PA**, e tratando
  "nenhum erro" como suspeita de corte por OCP
- `radioBusyLivre()` — guarda de 5 ms antes dos dois `startTransmit`
- `RADIO_RECUO_AUTO 0` — o recuo agora só avisa
- subclasse `SX1262Mts` para reexpor os dois métodos `protected` do RadioLib, em vez de
  abrir a biblioteca inteira com `RADIOLIB_GODMODE`

Compila em **1.842.958 B (55%)**, globais em 30%. **Nada foi gravado nem testado.**

### MEDIDO na bancada, no mesmo dia (tela "Gabriel gino", fonte step-down 12→5,3 V)

Primeira medição com o firmware corrigido. **Alimentação: step-down própria nos 12 V
para o 5 V do E22; GND comum com a tela.** Antena montada. Sem capacitor no módulo.

| Potência no chip | Na antena | Resultado |
|---|---|---|
| 5–6 dBm | 15–16 | estável |
| **14 dBm** | **24** | **estável — 41 TX, `preso=0`, `perdi=0`, 90 s sem reinício, `sinc=PPS`** |
| 18 dBm | 28 | **reinicia por watchdog na primeira transmissão** |
| 22 dBm | 32 | reinicia antes de transmitir (`tx=0`) |

**O objetivo foi atingido: 24 dBm na antena, estável** — +8 dB sobre os ~16 dBm em que
a tela estava, e o TDMA ancorado no PPS do GPS (`fix=SIM`, `PPS=1Hz (999975 us)`).

### DUAS TELAS SE FALANDO a 24 dBm — o marco do dia

Depois de resgatar a segunda tela (ver abaixo), as duas rodaram juntas:

```
radio: fonte de tempo agora e o slot 0 (grade UTC, tem PPS)
mundo: slot 0 se apresentou: FaustoA
[radio] slot=1 sinc=PPS fonte=EU | pwr=14 dBm (~24 na antena)
        tx=12 perdi=0 preso=0 | rx=13 ruim=0 outraSala=0 conflito=0 | snr=10.5
```

- **`rx ≈ tx` nas duas** — cada uma ouvindo todos os pacotes da outra
- **`sinc=PPS fonte=EU`** na gino (que tem fix) e **`fonte=slot 1 UTC`** na FaustoA
- **roster funcionando**: cada uma sabe o nome da outra
- `preso=0`, `conflito=0`, `ruim=0` dos dois lados

RSSI saiu `-0` — saturação por proximidade (mesma bancada). Medir alcance exige
afastar as duas.

### A FaustoA estava com o conector P3 solto

O `begin()` dava −2 (CHIP_NOT_FOUND) e o GPS entregava `pulsos=48` mas `frases=0`. O
`chicote_probe` isolou em três degraus:

```
[0] celulas GPIO do P4: todas ok - a placa esta boa
[1] reset: BUSY subiu=sim -> chip ALIMENTADO e vivo (NRST/BUSY ok)
[2b] o chip NEM PERCEBEU: o comando nao chega
[2c] nenhuma das 24 permutacoes -> os fios NAO estao trocados: estao ROMPIDOS
GPS: o GPS esta SEM ENERGIA (3V3 vem do P3)
```

O padrão entrega a resposta: **tudo que morreu está no P3** (NSS, MOSI, SCK, MISO e o
3V3 do GPS); **tudo que funcionava está no P1** (BUSY, NRST). **Reencaixar o P3
resolveu os dois problemas de uma vez.**

Confirma que o P3 é ponto único de falha para rádio *e* GPS ao mesmo tempo — e que o
`chicote_probe` vale cada uma das suas 396 linhas.

> **Bug na sonda:** ela imprime duas conclusões contraditórias no degrau 2 ("MISO ok" e
> "só o MISO está rompido"). O dado bruto (`0x2A` com pull-up *e* pull-down) é
> inequívoco; a lógica de mensagem é que está errada. Corrigir.

### O que o teto de 14 dBm revelou

O crash em 18 dBm **não é brownout** (que seria `ESP_RST_BROWNOUT`, motivo 9). O
backtrace decodificado dá:

```
Core 0: spinlock_acquire → xPortEnterCriticalTimeout     (MCAUSE 0x18 = Interrupt WDT)
Core 1: xTaskIncrementTick → xPortSysTickHandler
```

**Testado e refutado: não é o esp_hosted.** Com `MTS_TEM_WIFI 0` e `MTS_TEM_SD 0` (o
binário caiu de 1,84 MB para 988 KB) o crash em 18 dBm **continua igual**.

A explicação que resta, e que casa com tudo: a 18 dBm o módulo apaga no meio do TX; o
BUSY fica alto; o `startTransmit()` do RadioLib entra no `while(digitalRead(BUSY))`
**sem timeout** (`SX126x.cpp:1143`) e nunca retorna; o watchdog de hardware reinicia a
placa. É alimentação, mas se manifesta como watchdog — porque quem apaga é o módulo, e
a placa (no USB) continua viva.

**Limitação conhecida da correção:** o `radioBusyLivre()` confere o BUSY **antes** de
transmitir; ele não protege o caso em que o BUSY prende **durante**. Para isso seria
preciso `stageMode`/`setTx(timeout)` manual, ou o capacitor.

### Bugs encontrados PELO próprio teste

- **Trocar a potência com TX em voo causava `ESP_RST_PANIC`.** O aviso existia e a ação
  não era impedida (achado I10 da auditoria). Agora `radioTrocaPotencia` espera a IRQ.
- **Havia um SEGUNDO recuo automático** que o `RADIO_RECUO_AUTO` não cobria: o "resgate
  do laço de reset" no `mts_p4.ino:96`, que desce 4 dB no boot após reset violento. Ele
  baixou a potência por causa do pânico que o próprio firmware causou. Desarmado junto.
- **A CH343 grava E fala.** O comentário do `build.ps1` diz que ela é "só energia" e que
  se grava pela USB nativa — está errado nos dois sentidos.

### O que a pesquisa também estabeleceu

- **30 dBm é ilegal no Brasil em BW125** (Ato ANATEL 14.448/2017, item 10.3.3: 8 dBm por
  3 kHz → teto de 24,2 dBm). Irrelevante para testar em trilha, decisivo para produto.
- **Alcance realista: 500–900 m em trilha típica**, 150–400 m com mata fechada e curva.
  Âncora: campanha na Amazônia a 915 MHz/SF9 com 945 m medidos.
- **SF12 é pior que SF9 em vegetação** (medido) — a escolha de SF9 está certa.
- **O maior ganho de alcance do projeto é multi-hop pelo comboio**, custo zero: oito
  carros a 500 m cobrem 4 km.
- **Não existe fabricante melhor que a Ebyte** para 1 W SPI a varejo; existe o
  **E22P-900M30S**, com ESD e FCC ID de verdade.
- **Dois projetos já fizeram o MTS**: Spoke e o off-grid race tracker. Nenhum dos dois
  usa módulo de 1 W.

## v14–v18 — a frota que se explica sozinha (2026-08-29)

Continuação do dia anterior. Com a eleição já funcionando entre **duas** telas, as
**três** foram ligadas juntas pela primeira vez — e isso expôs mais quatro
defeitos, três meus e um de hardware. Estado final medido: uma âncora só,
`rx ≈ 2× tx` nas três (cada uma ouvindo as outras duas), `preso=0` e zero
colisão de slot.

### ECO — a tela conta o próprio estado para o servidor

O `/status` por WiFi **não funciona de fora para dentro**. Medido: a tela anuncia
`conectado, IP 192.168.2.130, /status no ar`, e do PC na mesma sub-rede o ping
devolve *destino inalcançável* e o ARP volta `00-00-00-00-00-00`. O esp_hosted
não entrega tráfego de entrada, e desligar o modo de economia **não** resolveu —
a suspeita já estava escrita no `wifi_ota.h` desde 25/08.

Mas **sair, ela sai** (é assim que o OTA baixa 1,7 MB). Então a telemetria passou
a viajar na direção que funciona: `tools/ota_server/api/eco.js` recebe e registra;
a tela manda ao conectar e a cada minuto. Sem banco de dados — o `console.log`
cai no log de execução da Vercel, que já tem retenção e consulta.

Nasceu de uma placa que não transmitia e cujo **serial não chegava a lugar
nenhum**: o console dela saía pela UART0, e o cabo ligado era o USB nativo, que
grava mas não fala. Sem o eco, a única forma de saber o que ela tinha era alguém
ler a tela e contar. Foi o eco que entregou o diagnóstico dela.

> **O eco leva o NOME do aparelho.** A primeira versão não levava, e o relatório
> chegava como `ECO ?` — havia uma placa com defeito na frota e nem o dono nem eu
> conseguíamos dizer qual das três era. Relatório anônimo de frota é quase inútil.

### `preso` que não era travamento

O vigia de "TX preso" media o tempo pelo relógio do **laço**, não do rádio, e
rodava **antes** do tratador de interrupção. Com o laço parado (o TLS do eco
bloqueia por centenas de ms), uma transmissão que **completou** chegava lá
parecendo travada — e o "conserto" era reconfigurar um rádio perfeito.

Evidência: das três telas, só a que estava com WiFi ativo acusava `preso`, e o
log mostra a acusação na linha seguinte ao `eco: subiu -> HTTP 200`.

Correção dupla: o vigia **lê o registrador do chip** antes de acusar (se o
`TX_DONE` já está lá, não houve travamento), e o eco **não entra no ar com
transmissão em voo** (`g_podeBloquear`), o que também eliminou o `perdi`.
Resultado: duas das três foram de `preso>0` para zero.

### A rede se partia em duas no arranque

Com as três reiniciando juntas, **slot 0 e slot 1 viraram âncoras
simultâneas** — ambos com `fonte=EU`, grades diferentes, e `rx ≈ 1× tx` em vez
de 2 (cada um ouvindo só o terceiro nó).

Causa: as **três primeiras falas eram roster**, e roster **não ancora ninguém**
(é mais longo; usá-lo como referência deslocaria a âncora). Um nó que assumia a
âncora aos 3 s passava ~3 s emitindo nada seguível — tempo de sobra para o
vizinho, com relógio de eleição em 4 s, assumir também.

- **A primeira fala passou a ser POSIÇÃO.** Os três rosters seguidos continuam
  logo depois (o nome tem de aparecer rápido), mas nunca antes de existir uma
  referência no ar.
- **Escuta silenciosa**, o desempate: duas âncoras cujas grades caiam deslocadas
  de ~1 slot transmitem exatamente uma sobre a outra e ficam **surdas para
  sempre**, sem erro nenhum aparecer — o `tdmaBeaconAceita` não salva, porque ele
  só age quando o beacon *chega*. Agora quem é âncora **sem ser o menor slot**
  cala um quadro a cada 16 e escuta o quadro inteiro. Custa 6% das falas enquanto
  dura, e dura poucos segundos.

### Era o cabo USB

A tela FaustoA passou por três estados no mesmo dia: rádio que **não
inicializava** (`bg=-2`, `CHIP_NOT_FOUND`), depois rádio funcionando mas com 8%
de transmissões travadas, depois `preso=0`. A única variável entre eles foi o
**cabo USB**.

Fica registrado porque a conclusão errada era tentadora: cheguei a recomendar a
troca do módulo E22 — desoldar e soldar 15 fios — e teria sido trabalho perdido.
O módulo estava bom. **Antes de suspeitar do rádio, troque o cabo.**

### Ferramentas

- **`tools/frota.ps1`** — um comando: publica, descobre as telas pelo VID da
  ponte CH343, grava em todas, reinicia, observa 30 s e imprime uma tabela com
  veredito por tela. Avisa em vermelho quando a frota fica em versões
  diferentes — que foi como duas telas foram ao campo em v7 e v8.
- **`tools/ler_tela.ps1`** — lê o boot de uma tela e marca quais portas carregam
  o console. **Só a ponte CH343 (VID_1A86) fala**; a USB nativa do P4
  (VID_303A) grava mas não traz o `Serial`. Custou meio dia descobrir isso.

### O que continua aberto

O rádio não sustenta mais que **6 dBm** em nenhuma das três — 13 dBm na antena
contra os 29,3 do projeto, cerca de um terço do alcance. É alimentação de 5 V do
E22, e segue como o risco nº 1. O teste que decide: fonte de 5 V separada, terra
comum, e tentar 22 dBm.

## v13 — a rede que ficava muda (2026-08-28)

Teste de campo com três aparelhos: dois não se falaram e o alcance foi péssimo.
As três telas foram lidas pelo serial na bancada no mesmo dia, e os números
explicaram tudo — nenhum deles estava visível para quem estava na trilha.

### A causa: só o slot 0 ancorava a rede

- Medido nas telas: `sinc=-` e **`tx=0`** nas duas que falavam pelo serial —
  slots 1 e 2. Elas não estavam com sinal fraco: estavam **mudas**, sem nunca
  ter transmitido um único pacote.
- `tdmaShouldTx()` exige âncora (`tdma_core.h`, `if (!t.haveAnchor) return
  false`) e só o nó 0 se auto-ancorava, no setup. Com o aparelho do slot 0
  desligado, longe ou com o GPS ruim, **a rede inteira morria em silêncio**.
- E o PPS não socorre o arranque: no u-blox o pulso só começa **depois do fix**,
  que é justamente o que ainda não existe quando o carro liga sob as árvores.
  Medido: `PPS=- pulsos=0` nas duas, com o GPS ainda procurando.
- **Correção — eleição da âncora** (`tdmaEleicao`): quem passa
  `TDMA_ELECT_BASE_US + slot*frame` sem ouvir ninguém ancora em si mesmo. A
  espera cresce com o slot, então o menor slot presente vence e os outros o
  escutam antes do próprio relógio estourar. Sem mensagem de eleição e sem
  estado compartilhado — só espera desigual, que é o que a torna segura numa
  rede onde metade dos nós não se ouve.
- O beacon deixou de ser privilégio do slot 0: `tdmaOnBeaconFrom()` recua também
  o offset do slot da fonte, e `tdmaBeaconAceita()` define a política — a minha
  fonte é o **menor slot que eu ouço, e nunca um slot maior que o meu**. Sem
  essa última cláusula dois nós ancoram um no outro em círculo e nenhum é âncora.
- Um nó que perde a fonte se **promove sem mexer na fase** (`tdmaPromoveAncora`):
  o alinhamento que ele herdou da rede continua valendo.
- **Prova na bancada**: duas telas nos slots 1 e 2, nenhuma no slot 0. O slot 1
  assumiu (`fonte=EU`), o slot 2 o seguiu (`fonte=slot 1`), `conflito=0`,
  `ruim=0`. Antes da correção, as mesmas duas telas ficavam em `tx=0` para sempre.

### A potência estava em 6 dBm, e ninguém tinha como saber

- `g_rfPwr` vem da NVS (`prefs.getChar("pwr", RF_PWR)`) e **sobrevive à
  regravação**. A escada de potência de bancada de 24/08 deixou **6 dBm**
  gravados nas telas, e elas foram para a trilha assim: **13 dBm na antena em
  vez de 29,3** — 16 dB a menos, cerca de 6× menos alcance.
- O número não era impresso em lugar nenhum. Agora sai no boot, no `[radio]`,
  no `/status` e na tela de diagnóstico, e o boot **grita** quando a NVS traz
  menos que `RF_PWR`.

### PPS antes de slot, e um PPS só vale depois de se provar

Descoberto **ao vivo na bancada**, depois da eleição já funcionando — os dois
problemas só apareceram porque agora dá para ver o estado.

- **A rede se partiu em duas grades.** Assim que uma tela pegou fix, ela foi para
  `sinc=PPS fonte=EU` e passou a recusar a outra (PPS manda). A outra recusava
  ela de volta, por ter slot maior. Duas âncoras, duas grades de tempo, sem
  colidir por sorte e sem nada no log dizendo que a rede tinha se partido.
  **Correção**: flag `TDMA_FL_PPS` no pacote — quem não tem PPS prefere sempre
  uma fonte na grade UTC, **mesmo de slot maior**.
- **A promoção trancava a recaptura.** `tdmaPromoveAncora` preservava a alegação
  de estar em UTC. Sem PPS próprio o nó corre livre e nem consegue anunciar UTC,
  mas continuava dizendo a si mesmo que estava na grade — e por isso recusava a
  fonte de PPS de verdade quando ela tinha slot maior. A fase se preserva; a
  alegação, não.
- **Um PPS que pisca é pior que nenhum.** Com o GPS dentro de casa, o pulso ia e
  voltava e a rede inteira o perseguia: `cedi o lugar` / `ASSUMI a âncora` a cada
  poucos segundos, com `perdi` subindo junto. **Correção**: `TDMA_PPS_FIRME` —
  10 pulsos limpos e seguidos antes de o PPS mexer na âncora ou ser anunciado.
- **Fim do círculo de âncoras.** Aceitar qualquer fonte em holdover fazia dois
  nós ancorarem um no outro, sem nenhum ser âncora, com a fase dos dois
  derivando junta. Agora fonte sem PPS só é aceita de **slot estritamente
  menor**, e a eleição ganhou um terceiro caso: quem está seguindo um slot maior
  **assume**, mantendo a fase.

**Resultado medido, 3,5 min contínuos**: slot 2 virou a referência UTC
(`sinc=PPS fonte=EU`), slot 1 cedeu para ela mesmo tendo slot menor
(`fonte=slot 2 UTC`), e depois do arranque houve **zero** troca de âncora —
`perdi` parou de crescer, `conflito=0`, `ruim=0`, `preso=0`.

### Escada de potência: o teto medido é 6 dBm, e o degrau seguinte colapsa

Feita na bancada com as duas telas acessíveis pelo serial, antenas montadas,
alimentadas pelo USB do PC. Critério: `preso` (TX que não completou em 400 ms)
crescendo junto com `tx` = o E22 apagando no meio da transmissão.

| potência do chip | na antena | Δtx | Δpreso | Δrx | veredito |
|---|---|---|---|---|---|
| 6 dBm | ~13 dBm | 16 | **0** | 17 | estável |
| 8 dBm | ~15 dBm | 21 | **21** | 0 | 100 % travando |
| 10 dBm | ~17 dBm | 16 | **16** | 0 | 100 % travando |
| 14 dBm | ~21 dBm | 26 | **26** | 0 | 100 % travando |
| 22 dBm | ~29 dBm | 27 | **26** | 0 | 100 % travando |

Reproduzido nas duas placas, nos dois sentidos (voltar para 6 dBm recupera
integralmente: `preso` para de crescer e o `rx` volta a 1 pacote por quadro).

**O que isso significa medido**: hoje a frota transmite a ~13 dBm na antena em
vez de 29,3 — 16 dB, cerca de 6× menos alcance. Não é escolha; é o único
degrau em que o rádio funciona.

**O que ainda NÃO está isolado**: a causa. O degrau de 6 para 8 dBm é minúsculo
em potência (20 → 32 mW na saída) para um colapso de 0 % a 100 %, o que não tem
a forma de uma queda progressiva de alimentação. Duas hipóteses, nenhuma
confirmada:

1. **5 V do E22 afundando** — bate com a nota de campo de 2026-08-24 (tela A a
   10 dBm, `preso` 1:1 com `tx`) e com o cálculo do `MONTAGEM.md`: o capacitor
   cobre 0,23 ms de um pacote de 51 ms, os outros 99,5 % vêm da fonte.
2. **Algo no caminho PA/registrador acima de 6 dBm** — a nitidez do degrau e a
   repetição idêntica nas duas placas não são típicas de sag analógico.

**Uma hipótese já foi testada e REFUTADA**: RXEN/TXEN trocados no chicote (são
os pads 7 e 6 do módulo — vizinhos, como BUSY/DIO1, que já vieram trocados uma
vez). Invertidos no software (`RF_SWITCH_TROCADO`), o RSSI entre duas telas na
mesma bancada caiu de −68/−61 para **−112/−116 dBm**. Ou seja: a fiação está
certa e **o PA está ligando de verdade** — aqueles ~50 dB são o próprio ganho do
caminho PA/LNA. A chave ficou no `hardware.h`, em 0, com o resultado escrito, para
ninguém gastar outra tarde na mesma hipótese.

Fica também retirada a suspeita levantada a partir do RSSI de campo próximo: o
−65 dBm entre duas telas na bancada parecia ~40 dB abaixo do esperado para
13 dBm, mas duas antenas chicote deitadas, cruzadas em polarização e perto de
metal perdem 20–30 dB sozinhas. Não é evidência de defeito.

**Teste que ainda falta** (é hardware, não código): alimentar o E22 por uma fonte
de 5 V **separada**, com terra comum, mantendo a tela no USB. Se 22 dBm passar a
funcionar, é alimentação. Um multímetro nos pads 9/10 durante o TX também
responde.

**Este é hoje o risco nº 1 do projeto** — mais que TDMA, mapa ou roster. Sem
resolvê-lo o alcance em mata fica em ~1/60 da potência projetada.

### Tela de diagnóstico (o fim do teste de campo às cegas)

- `telaDiag()`: rádio, potência, slot, sincronia (**de quem** vem o tempo),
  transmitidos/perdidos, recebidos/ruins, RSSI+SNR, TX preso e GPS/PPS. Dez
  células, atualizadas 1×/s, verde para o esperado e vermelho para o que
  explica a falha.
- Abre tocando na **faixa de pastilhas** da tela inicial — ela já mostra o
  estado de GPS e rádio, então tocá-la para ver o detalhe é o gesto natural.
- Motivo de existir: slot, sincronia, potência e RSSI só saíam pelo serial
  (precisa de PC) ou pelo `/status` (precisa de WiFi). Na trilha não há nem um
  nem outro, e o dia virou "não funcionou" em vez de "não funcionou POR ISSO".

### Frota

- As três telas estavam em versões **diferentes** em campo (v7 e v8): o OTA não
  pegou numa delas. Niveladas na v9 **por cabo**.
- Testes: `tdma_replay.js` 56 PASS, `audit.js` 71 PASS, `selftest.js` 63 PASS.
- **Pendência**: a bancada web ainda não modela a eleição da âncora — ela
  reproduz o comportamento antigo (só o nó 0 ancora). O `audit.js` não acusa
  porque ainda não há checagem para isso.

## Frota de 3 telas + atualização pela internet (2026-08-25)

O dia em que o sistema virou frota. Tudo validado na bancada com as três telas.

### Mapa regenerado do zero e cartão da tela C

- Os pacotes `brasil.vec`/`brasil.dem` tinham sido apagados do PC. Regenerados
  do zero com o pipeline do repo (`tools/mapa/`): OSM do Brasil (2,1 GB) +
  relevo SRTM (68 blocos, 1,58 GB) → `brasil.vec` 1.228 MB (332 mil setores,
  p50 de leitura 13–15 ms) + `brasil.dem` 3,90 GB. **Agora moram em
  `Desktop\gps-mapas\` e não se apagam** — cartão novo custa 4 min de gravação.
- Cartão da tela C gravado e validado (CRC 18/18 + 31/31 OK).

### Atualização de frota pela internet (o fim do cabo)

- **Servidor de release na Vercel**: https://mts-ota.vercel.app (fonte em
  `tools/ota_server/`). A raiz é um painel de status; `/version.json` é o
  manifesto que as telas leem.
- A tela, conectada em **qualquer WiFi** e parada na tela inicial, checa a cada
  15 min (e 40 s após ligar); versão maior → baixa por HTTPS, confere MD5,
  grava no 2º slot e reinicia. Erro no meio = continua na versão atual. **Na
  trilha nunca atualiza** (a checagem vive só no loop da tela inicial).
- **Armadilha que custou duas implementações**: o TLS do esp_hosted (C6) trava
  em respostas maiores que ~8 KB — nem laço manual nem `HTTPUpdate` passam.
  Solução: download **em fatias de 8 KB com HTTP Range** (o CDN responde 206)
  em conexão keep-alive, cada fatia bufferizada antes do `Update.write`.
- Publicar = subir `MTS_VERSAO` (hardware.h) + `tools\publica_ota.ps1`
  (compila, MD5, manifesto, deploy). **Prova ao vivo**: tela C se atualizou
  v4→v5 sozinha pela internet, 1,8 MB em fatias, reboot na versão nova.
- Colisão de macro: o `M` da ui.h vs parâmetro `M` do bignum.h (mbedTLS) —
  resolvido com push_macro/pop_macro em volta dos includes de rede.

### Giro de 180° para a caixinha

- A caixinha monta o painel de cabeça para baixo. `MTS_TELA_180` em hardware.h.
- **`setRotation()` é ignorado em silêncio pelo caminho DSI** (testado: nada
  vira). O giro de verdade é `offset_rotation = 2` na config do painel
  (LGFX_P4_LCD7.h) — o toque gira junto (a LovyanGFX soma o offset do painel ao
  do toque em `convertRawXY`, conferido no fonte).

### Tela de WiFi e teclado refeitos (feedback do usuário: "péssimo, bugado")

- **Causa real do scan vazio**: varrer durante a reconexão automática de 30 s
  devolve lista vazia. Agora derruba a tentativa antes de varrer.
- Redes **ordenadas por força e sem repetição** (mesh); botão PROCURAR DE NOVO;
  rede salva marcada; conexão tenta 2× (o C6 perde o 1º begin às vezes — isso
  aparecia como "senha errada" com senha certa); falha volta para a lista.
- **Teclado em layout de celular**: fileira de números SEMPRE visível, modo
  símbolos completo, senha começa em minúsculas e **não troca de caixa
  sozinha** (a troca automática é para nome próprio; em senha mudava o que a
  pessoa digitava), limite 24→32, textos de senha.
- Bug do rótulo empilhado no botão de modo: `botao()` vazado não pinta fundo —
  repintar por cima empilhava abc/#$%/ABC. Agora limpa o retângulo antes.
- Listra de sol do cartão ENTRAR removida (lida como "cor vazando" do CRIAR).

### Rede de 3 nós validada na bancada

- **Tela b** (líder, slot 0, rádio bom a 22 dBm): GPS com fix real (HDOP 0,98)
  e **PPS 1 Hz disciplinando o TDMA** (`sinc=PPS`) — estreia da âncora
  definitiva. **Tela a** (slot 1) e **Tela c** (slot 2) sincronizadas por
  beacon. Zero conflito, zero pacote ruim, roster cruzando ("slot 0 se
  apresentou: Tela b"), **alerta apertado na C acendeu na A e na B**.
- O detector de conflito de slot pegou o caso real ao vivo (B e C ambas em 0)
  e o aviso guiou o conserto — C voltou ao slot 2 pelo serial (`s2`).
- Tela C ficou surda (rx=0) após manuseio: **antena/chicote solto** — reencaixe
  resolveu. Regra de sempre: antena antes de energizar.
- Tela A em loop de POWERON no USB do PC ao ganhar WiFi (painel + rádio + GPS +
  C6 estouram o orçamento da porta) — na fonte de tomada, estável.

## Primeiro dia de campo + decisões de campo (2026-08-24)

Primeiro teste com carros de verdade. Aprendizados e mudanças pedidas do campo:

- **O GPS "mini" (NEO-7M de 5 pinos) EXPÕE PPS e ele já está fiado no IO2** — a
  nota "PPS não fiado / breakout não expõe" valia para o GY-NEO7MV2, outro
  breakout. Docs corrigidos. O caminho de PPS do firmware estreia sozinho
  quando houver fix (pastilha vira "PPS").
- **Fix a frio exige o carro PARADO com céu aberto** — confirmado em campo; com
  a pilha de backup o problema quase some (o MKR GPS Shield avaliado foi
  descartado: antena integrada e sem PPS no header; o mini com antena externa é
  melhor para carro).
- **Mapa orientado pelo rumo (heading-up)** — "a tela não está seguindo o
  caminho". O bloqueio antigo era o relevo raster; com o relevo desligado (ver
  abaixo) o vetor gira por matriz 2×2 no `VecXform` (janela de descarte pela
  inversa, cobertura pela meia-diagonal). O "N" virou bússola andando no anel;
  setas dos carros em rumo relativo.
- **Mapa enxuto** — "muita informação": ficam TRILHA, ESTRADA, RUA (perto),
  RIO/CÓRREGO/LAGO, PROTEGIDA e LIMITE (divisa de município, novo); saem
  relevo, mata, ferrovia, pista e POIs não críticos (porteira/vau/bloqueio
  ficam). Tudo reversível — nada sai do cartão.
- **Traçado mais grosso** — "linha muito fina" com o carro pulando: rota
  15/9 (era 11/5), rastro 11/5 (era 8/3), alerta 17/9.

### Escada de potência (noite do mesmo dia, fonte de tomada 2–3 A)

Medido com o `escada.ps1` (conta reboots + telemetria), 110 s por degrau:

| Degrau | Tela A | Tela B |
|---|---|---|
| 2 dBm | ✅ perfeita (confirmado de novo após mexida no 5 V) | ✅ |
| 5 dBm | ❌ todo TX trava | — |
| 10 dBm | ❌ todo TX trava (antes E depois de reforçar o fio de 5 V) | ✅ |
| 22 dBm | ❌ todo TX trava | ✅ **tx=120 rx=120 preso=0, zero reboots** |

Conclusão FINAL, com a confissão do próprio chip (`GetDeviceErrors` via
`e22_ping`): **o módulo E22 que estava na tela A é defeituoso** — ao armar TX
acima de ~2 dBm ele devolve `0x0020 XOSC_NAO_PARTIU` (o TCXO não parte) e
reseta, perdendo a configuração. Eliminados por teste: fio de 5 V (reforçado,
sem efeito), DUAS antenas diferentes (sem efeito), placa (a B idêntica passa a
22 dBm), firmware (idêntico) e as curas por software (`setRegulatorLDO`,
`setTCXO(2.4, 10 ms)` — sem efeito). A tela B está **validada em potência de
campo completa (29,3 dBm ERP)**. Remédio: trocar o módulo da A pelo terceiro
E22 (reserva); o defeituoso vira peça de bancada (funciona até ~2 dBm).

Bônus da sessão: **primeira âncora por PPS da história do projeto** — a tela A
fixou na janela e o log virou `sinc=PPS` sozinho: pulso → ISR → par com NMEA →
âncora, tudo de primeira. Com PPS nos dois, some a dependência do nó 0.

Adendo: a potência de TX virou **ajustável em execução e persistida na NVS**
(`p` + número no serial, `radioTrocaPotencia`), nascida de seis regravações num
dia só para trocar um `#define`. Com ela, o teto do módulo defeituoso foi
bissecado ao dBm: **6 dBm estável, 7 já trava** (e o teto subiu de <5 para 6
depois do reencaixe da antena — o conector também contribuía). A tela A ficou
cravada em 6 dBm (~13 dBm ERP com o PA, ~4× o alcance do modo bancada) até o
módulo reserva entrar; a B segue em 22.

Firmware ganhou **auto-conserto do rádio**: TX preso agora refaz o `begin()`
completo (um módulo que apagou por queda de 5 V esquecia a configuração e
ficava surdo na frequência de fábrica — agora volta sozinho em ~0,5 s).

## Revisão pré-MVP das duas telas P4 (2026-08-23)

Revisão completa do `firmware/mts_p4` antes do primeiro teste com duas telas.
Nove defeitos corrigidos; compila em 1034 KB; bancada 63+71+55 PASS. **Nada
disto rodou em hardware ainda** — gravar e conferir é o primeiro passo.

- **A rede morria aos 71,6 min de uso.** As ISRs do PPS e do rádio carimbavam com
  `micros()` (esp_timer truncado a 32 bits); o `tdma_core` compara com o relógio
  de 64 bits, então no estouro a âncora saltava ~967 ms (2³² mod 1e6) e todo nó
  desalinhava. Carimbos agora em 64 bits, copiados com a interrupção suspensa.
- **A janela de TX deixava o pacote vazar para o slot do vizinho.** Ela limitava o
  INÍCIO do TX; com o loop atrasado (mapa repintando por ~100 ms) um TX no fim da
  janela invadia até 48 ms do slot seguinte. `tdmaShouldTx` agora desconta o
  air-time do limite superior (com `airtimeUs = 0` nada muda). Espelhado no port
  da bancada, coberto pelo `tdma_selftest` [3b] e pelo `audit.js`.
- **A rota do líder nunca era desenhada.** `mundo.h` montava `g_rota[]` a cada
  pacote do líder e o `mapa.h` não a pintava — o seguidor via um ponto andando,
  nunca o caminho, que é a promessa do produto. Roxo, do ponto onde eu estou para
  a frente, com ponte tracejada nos vãos de sinal.
- **O alerta era ficção de ponta a ponta.** Não havia como pedir socorro (nada
  punha `TDMA_FL_ALERT` no ar) e o alerta recebido não acendia (a faixa lia uma
  cópia velha). Agora há o botão ALERTA (meu pedido, transmitido) e as faixas
  leem `g_carros` vivo.
- **A tela da trilha desenhava um retrato, não o mundo.** O seguidor entrava com a
  lista vazia e nunca via a faixa do líder; nome de roster não atualizava; e o
  `mudou()` só olhava a MINHA posição — parado, o líder ficava congelado no mapa.
- **Carro zumbi, de novo.** Nada zerava `ativo`: quem desligava ficava na lista e
  na contagem para sempre (o mesmo bug que o grupo_ws teve). Dois estágios: sem
  fix após 5 s de silêncio, fora da lista após 15 s.
- **Conflito de slot silencioso.** Pacote da minha sala com o meu slot é OUTRO
  aparelho com a minha identidade (eu nunca ouço meu próprio TX) — era descartado
  como "eco". Agora conta e grita no serial. Duas telas recém-gravadas saem AMBAS
  no slot 0: separá-las (`s0`/`s1` no serial) é pré-requisito do teste.
- **Nó mudo e surdo para sempre se o TX_DONE se perdesse** (fio do DIO1 mexido):
  o `standby()` pré-TX tirava o chip de RX e ninguém o rearmava. Vigia de 400 ms.
- **Esperas cegas deixavam o nó surdo.** Teclado, arrasto na trilha e piscada de
  alerta usavam `delay()` cru — perdiam janelas de TDMA e afogavam a UART do GPS.
  Tudo passou a servir rádio e GPS enquanto espera (`esperaServindo`).

E as consequências encadeadas: trocar de sala agora limpa os carros da sala
antiga (a sala de espera nascia com fantasmas do grupo anterior), sair da trilha
volta o rádio para SEMSALA (o aparelho continuava transmitindo no grupo de quem
ficou), o `mundoInicia` com rádio não apaga o mundo vivo (duplicava cada carro
por 15 s), o criador anuncia LIDER já na sala de espera, e o FICAR do diálogo de
sair não é mais recursivo.

Registrado e **não** corrigido (decisão de arquitetura, some com PPS em todos os
nós): no modo beacon a âncora herda o jitter do instante em que o nó 0
transmite dentro do próprio slot (até ~47 ms depois da janela com air-time;
comentário no `radio.h` manda usar slots baixos sem PPS).

### Bancada da noite, mesma data — DUAS TELAS P4 CONVERSANDO POR TDMA

Primeira comunicação real da pilha nova: duas 7B trocando posição e roster por
SX1262 com disciplina de slot — `tx=72/perdi=0/rx=72/ruim=0/conflito=0`,
RSSI −17/−24 dBm, roster cruzando ("slot 1 se apresentou"). No caminho, três
diagnósticos que valem registro:

- **Chicote P3 sem contato (as duas telas)**: rádio vivo no P1 (BUSY responde ao
  reset) mas SPI/GPS mudos. Provado por `firmware/chicote_probe/` (nova): escada
  de SPI ativa, testemunha do BUSY, permutação dos 4 pinos e caçador em 972
  combinações. Reencaixe resolveu; a sonda fica no repo.
- **Brownout no TX de 29 dBm**: alimentada só pelo USB do PC e sem o capacitor
  de 470 µF, a placa reseta em loop (`rst:0x1 POWERON` ~2 s após o painel, no
  primeiro pacote) — o modo de falha exato que o MONTAGEM.md previa, agora
  MEDIDO. `RF_PWR` está em **2 dBm (bancada)** no hardware.h; voltar a 22 no
  campo SÓ com o capacitor montado e alimentação de verdade.
- **`tdma_selftest` no silício do P4 pela primeira vez: 66 PASS, 0 FAIL**,
  incluindo a janela com air-time [3b] e o TTL por frame [7].

Pendências físicas: GPS da tela B mudo (frases=0, PPS pegando ruído de pino
solto — fios IO2/3/4 + 3V3 do rabicho dela; os do rádio estão bons) e o
capacitor/alimentação para potência de campo. Comando de bancada novo no serial:
`x` = sair da sala (descarta sessão, SEMSALA); prints "mundo:" quando um carro
novo é ouvido ou se apresenta.

### Segunda passada, mesma data (revisão completa + melhorias)

- **Rumo e velocidade entram no ar.** O pacote sempre teve `heading`/`speed`
  (seção 7 do plano) e iam zerados. Agora o `gps.h` lê o RMC (rumo só andando
  ≥ 3 km/h — parado, o rumo do GPS é ruído), o pacote os carrega e o marcador
  vira uma **seta que gira** (`marcaSetaRumo`): o meu no centro e o do líder na
  tela dos seguidores. Um triângulo fixo apontando o norte mentia sobre qual
  saída da bifurcação o líder pegou.
- **A `fila.h` saiu do papel.** A distância nas faixas agora é **pelo caminho**
  (projeção na rota do líder), com linha reta como reserva quando ainda não há
  rota; e o carro a mais de 150 m da rota mostra **FORA** em vez de um número
  que mente. Era o argumento do produto, calculado e nunca usado.
- **Holdover deixou de piscar em regime normal.** O TTL da âncora acompanha o
  frame (`max(1,5 s, 2×frame + 0,5 s)`): o roster do nó 0 troca o beacon de um
  frame a cada oito, então 2 s de silêncio com frame de 1 s é normal — o TTL
  fixo de 1,5 s triplicava a guarda e acendia alarme a cada 8 s sem nada errado.
  Espelhado na bancada; testes do holdover atualizados nos dois lados.
- **Roxo só na rota do líder.** O rastro próprio tinha um estado roxo "à frente"
  herdado do grupo_ws (lá o buffer era a rota compartilhada); aqui ele fingia
  rota num cruzamento com o próprio caminho. Rastro próprio é sempre azul.
- **A abertura não deixa mais o nó surdo**: os 2,4 s do splash servem rádio e
  GPS (o rádio sobe antes do painel, e é o nó 0 ancorando a rede nesse momento).
- **Código morto substituído removido**: `pastilha` antiga, `escadaZoom`,
  `iconeTemaAntigo`, `sinal` e `telaAviso` — todas versões já superadas, sem
  nenhum chamador. Nome na faixa cortado em 12 caracteres para não invadir a
  distância.
- Revisados sem pendência: `vec_pack.h`, `dem_pack.h` (limites, CRC, borda de
  setor), `sd_setor.h`, `LGFX_P4_LCD7.h`, `icones.h`, `splash.h`. O `e22_ping`
  usa `micros()` só em deltas curtos — correto, sem mudança.

## Bancada web + correções que ela encontrou (2026-07-31)

Simulador das telas em `web/bancada-trilha/` ([no ar](https://bancada-trilha.vercel.app)):
quatro canvas de 1024×600 reexecutando a lógica do firmware — `grupo_ws.ino` +
`trilha_core.h` nos modos GRUPO e P2P, e `tdma_core.h` + `tdma_test.ino` no modo
TDMA, com clock próprio por nó (offset + drift de ±40 ppm). Não substitui hardware:
é bancada de **lógica e UI**, e a página lista o que ela não simula.

Três verificações automáticas, nenhuma precisa de placa:

- `selftest.js` — 63 checagens de comportamento (fluxo de grupo, catch-up do P2P,
  TDMA, desenho das cinco telas, toque, 600 cliques aleatórios).
- `audit.js` — 70 checagens da **bancada contra o firmware**: constantes, comandos,
  temas em RGB565, paleta, strings de tela e regras numéricas de desenho. Pegou duas
  invenções da bancada (limpar `route[]` ao sair e `world[].color` em zero).
- `tdma_replay.js` — reexecuta os 9 grupos do `tdma_selftest.ino` sobre o port JS,
  com os mesmos números. Existe porque não há compilador C++ na máquina e o
  auto-teste do firmware roda no ESP32: 43 PASS.

### Bugs corrigidos no `tdma_core.h` / `tdma_test.ino`

Nenhum precisou de rádio para aparecer, e todos estão na Fase 4 — código que **nunca
foi executado**.

- **`frameBase` calava metade da rede.** `frameBase += (rxUs - anchorUs) / frameUs`
  era divisão inteira: num nó com cristal mais lento que a âncora o intervalo entre
  beacons medido localmente fica abaixo de `frameUs` (1 s a −40 ppm = 999,96 ms), o
  resultado era 0, o `frameBase` congelava e a guarda de "1 TX por frame" travava o
  nó **para sempre** — exatamente o sintoma que o campo existe para evitar. Medido em
  60 s: `0/−30/−20/−10 ppm → tx = 60/2/2/2`. Agora arredonda com mínimo 1 →
  `60/60/60/60`, inclusive com 30 % de beacon perdido. **Com dois nós o bug passava**,
  porque depende do sinal do erro relativo dos cristais.
- **Âncora do beacon atrasada de um pacote inteiro.** O `rxDone` vem no fim do
  pacote; sem descontar o air-time a âncora ficava 51,5 ms tarde (16 B em SF7/BW125),
  quase a folga inteira de um slot de 125 ms — quase todo pacote chegava em slot
  errado e, com 8 nós, virava colisão. `tdmaSetAirtime()` informa o valor medido e o
  `tdmaOnBeacon` desconta. Com `airtimeUs = 0` a correção é inerte, então o
  auto-teste antigo continua valendo.
- **Somar PPS ao beacon matava o TX.** O sketch ancorava no beacon sem checar a fonte
  de tempo; com PPS por cima, `sync` alternava e o `frameBase` congelava. Agora o
  beacon só vale enquanto não houver PPS — ele é fallback, não um segundo relógio.
- **O nó âncora vivia em holdover.** Ele ancora uma vez no `setup()` e nunca mais, e
  o `tdmaTick` o marcava em holdover após 1,5 s, triplicando a guarda dele para
  sempre. Novo campo `isAnchor` resolve.

### Bugs corrigidos no `grupo_ws.ino` / `trilha_core.h`

Mexer aqui foi decisão explícita do usuário. Compila em 449331 B (14 % de `huge_app`).

- **Carro zumbi.** Nada zerava `world[].active`, e o `parseRx` do seguidor refresca
  `lastMs` a cada `WORLD`: o `NODE_TTL` nunca disparava lá e um carro que saía do ar
  ficava no mapa dos seguidores, parado na última posição, para sempre. O `loop()`
  agora expira quem não reporta.
- **Alerta que não acendia em ninguém.** `drawMap` olhava só o primeiro peer ativo,
  então num grupo cheio o sino de um slot alto não acendia em nenhuma tela. Agora
  procura qualquer carro em alerta antes de cair no primeiro peer.
- **`slotDue` calculado e ignorado.** `parseRx` computava
  `slotDue = worldRxMs + curSlot*SLOT_MS` e o `loop()` mandava o uplink num timer
  livre de 1200 ms; como o `ROSTER` é broadcast, os seguidores entravam em fase e
  colidiam sistematicamente (até 100 % de colisão com 3 seguidores, com o canal 95 %
  livre). Passou a respeitar o slot, com fallback de 3 s: colisão a **zero** e
  ocupação de ar de 7,4 % para 4,0 %.
- **Vocabulário SALA → GRUPO** nas telas e no serial (pedido do usuário). A chave NVS
  segue `room` — nenhuma tela perde configuração ao regravar.
- **Cor por slot.** `g_color` nasce 0 no NVS de todos, então o JOIN pintava todo mundo
  com `PALETTE[0]`. Sem provisionamento, o slot vira a cor.
- **Rótulos empilhados no mapa.** Nomes escritos em `sx+12, sy-8` sem checar colisão:
  a 2 m/px, carros a 30 m ficam a 15 px e os nomes viravam uma mancha. Agora desviam.
- **Lixo ao sair do grupo.** `route[]` e `world[]` sobreviviam ao SAIR, então o
  trajeto antigo aparecia emendado no do grupo novo, com ponte tracejada ligando os
  dois lugares.

Ficaram registrados e **não** corrigidos, por serem decisão de produto ou de
arquitetura: o vão sem catch-up no caminho de grupo, o `FORA DO TRAJETO` de quem
entra atrás, `MAXN = 8` contra os 25–50 nós do plano, o roster ausente na pilha nova
e a resolução do trajeto ditada pelo frame no TDMA.

## Fase 4 — Rumo ao TDMA + mapa offline (em andamento)

Preparação da pilha nova, em sketches separados.

- **Ambiente de build reparado e documentado** (`docs/AMBIENTE_BUILD.md`): o core
  `esp32:esp32` não estava instalado e faltavam LovyanGFX, TinyGPSPlus e LoRaMESH
  — nada compilava. Agora em esp32 3.3.11. Script `tools/build.ps1` encapsula o
  caminho do `arduino-cli` (que vive dentro da Arduino IDE, fora do PATH) e o FQBN.
- **Tamanho do binário medido pela primeira vez**: `grupo_ws` usa 449 KB, só 14 %
  dos 3 MB de `huge_app`. Consequência: um esquema de partição com dois slots (OTA)
  caberia com folga de 4×, sem precisar de `partitions.csv` customizado.
- **Rádio novo**: 3× E22-900M30S (SX1262, 1 W, TCXO). O LoRaMESH é incompatível com
  TDMA por arquitetura — firmware de rede fechado atrás de UART, com roteamento e
  ACK próprios, janelas de recepção de 5–15 s e **sem sinal de TxDone**. Sem saber
  quando o pacote saiu, não há slot.
- **`firmware/tdma_core.h`** — fatiamento do tempo em frames/slots, com a fonte de
  tempo atrás de uma interface: beacon do nó 0 na bancada (fase 2) e PPS do GPS
  depois (fase 3), sem mudar o resto do código. Frame em segundos inteiros para
  ancorar no PPS. Guarda, holdover, 1 TX por frame, pacote de posição de 16 bytes.
- **`firmware/e22_ping/`** — ping-pong SX1262 que **mede** o tempo real entre
  `startTransmit` e o `txDone` da ISR, para dimensionar o slot com número medido em
  vez da estimativa de 40–60 ms.
- **`firmware/tdma_test/`** — TDMA de 3 nós com coordenadas falsas; conta colisão
  (CRC), pacote em slot errado, janela perdida e idade de cada vizinho.
- **Mapa offline**: `tools/tiles/` (baixar, converter para RGB565, empacotar com
  índice + CRC32 por tile) e `firmware/tile_pack.h` (leitor por setor, sem
  filesystem — nada de FAT para a queda de energia corromper). Formato documentado
  em `tools/tiles/README.md`.
- **`firmware/sd_bench/`** — mede na tela o que não tem resposta em documentação:
  se dá para inicializar o cartão com o **CS no expansor I2C** (EXIO4, não num
  GPIO), quanto custa de verdade ler um tile e um grid 3×3 por SPI, e **quanto o
  painel RGB sofre** durante a leitura (compara o tempo de `pushImage` com e sem
  cartão em uso — número, não impressão). Esses três valores decidem a
  arquitetura de render do mapa.
- **Testes que não exigem hardware**: `firmware/tdma_selftest/` e
  `firmware/tile_selftest/` rodam em qualquer ESP32 (sem rádio, GPS ou cartão);
  `tools/tiles/verify_reader.py` confere a conformidade entre o gravador em Python
  e o leitor em C. O selftest do TDMA já pegou um bug real antes de existir
  hardware: no modo beacon o índice do frame voltava a zero em cada re-âncora, o
  que faria o nó **parar de transmitir** depois do primeiro frame.

## Fase 3 — Duas telas Waveshare

Arquitetura final: duas telas **Waveshare ESP32-S3-Touch-LCD-7B** idênticas rodando o mesmo firmware (`firmware/grupo_ws/`), papel escolhido por toque (CRIAR SALA = líder, ENTRAR = seguidor).

- **Protocolo P2P** ponto-a-ponto (0x11 seguidor→líder com ACK, 0x12 líder→seguidor com bloco de trajeto).
- **Catch-up**: ao reconectar, o líder reenvia o trecho perdido em blocos até o seguidor alcançar — preenche o vão sem linha reta.
- **Bordas de aviso piscando**: vermelho (alerta), laranja (perda de sinal, com "REDUZA"/"SINAL PERDIDO"), amarelo ("FORA DO TRAJETO").
- **Alerta** pinta o trecho do caminho entre os dois carros de vermelho.
- **Trajeto real** (breadcrumb) em vez de linha reta; cores fixas roxo (a percorrer) / azul (percorrido).
- Buffer de trajeto ampliado para **~6 km** (PSRAM); busca do ponto mais próximo otimizada.
- Bring-up completo da Waveshare: GT911 (touch), CH32V003 (expansor IO), LoRaMESH, GPS.
- Reorganização do repositório em `firmware/`, `tools/`, `legacy/`, `docs/`.

## Fase 2 — Seguidor por LoRa (líder → seguidor)

- Líder (CYD) transmite posição + histórico; seguidor (GIGA) plota mapa estilo Waze.
- Núcleo compartilhado `trilha_core.h` (protocolo, temas, estado, geo).
- Modo Grupo no GIGA com temas (Rally/Tático/HUD), roster, distância por carro.
- Tentativa no Arduino GIGA descontinuada (módulo LoRa do GIGA falhou → migração para Waveshare).

## Fase 1 — Odômetro

- Projeto original de odômetro (`legacy/odometro/`), origem do sistema.
