# CLAUDE.md

Contexto para quem (ou o que) abre este repositório sem memória da sessão anterior.
**Leia `docs/RETOMAR.md` em seguida** — ele tem o passo a passo prático.

## O que é o projeto

**MTS — Minhoca Trail System.** Sistema **"siga o líder" por rádio LoRa para trilhas
off-road**. Dois ou mais
aparelhos com GPS e tela: o líder anda e transmite o **trajeto que fez** (não só a
posição atual); os seguidores plotam um mapa estilo Waze — você no centro, o
caminho do líder à frente, o rastro já percorrido, distância — e conseguem seguir
mesmo sem ver o carro da frente. Tem alerta (botão pinta o trecho entre os dois
carros de vermelho), aviso de perda de sinal e catch-up que preenche o vão quando
o rádio volta.

Nasceu como a Parte 2 de um projeto de odômetro (`legacy/odometro/`).

## Estado atual (2026-07-31)

Três coisas convivem, e **confundi-las é o erro mais fácil de cometer**:

### 1. Produção — `firmware/grupo_ws/` (ESP32-S3 + LoRaMESH)

Firmware que **funciona hoje** nas duas telas Waveshare ESP32-S3-Touch-LCD-7B.
Ponto-a-ponto de 2 nós, mapa vetorial, rádio LoRaMESH em UART.

> **REGRA DURA: não altere `firmware/grupo_ws/` sem o usuário pedir explicitamente.**
> É o único firmware validado em campo. A pilha nova nasce em sketches separados.
>
> Em 2026-07-31 o usuário pediu, e ele foi alterado: vocabulário SALA → GRUPO e sete
> correções de lógica encontradas na bancada web (carro zumbi, alerta que não acendia,
> `slotDue` ignorado, cor por slot, rótulos empilhados, lixo ao sair). **Compila em
> 449331 B, mas nada disso foi gravado nem testado em campo** — ao pegar as telas,
> gravar e conferir é o primeiro passo. Detalhes no `CHANGELOG.md`.

### 3. Bancada web — `web/bancada-trilha/` (sem hardware)

Simulador das telas em canvas de 1024×600: [bancada-trilha.vercel.app](https://bancada-trilha.vercel.app).
Reexecuta a lógica do firmware nos três modos (GRUPO, P2P, TDMA) com clock próprio
por nó. Foi ela que encontrou os bugs acima e os quatro do `tdma_core.h`.

```powershell
node web\bancada-trilha\selftest.js      # 63 checagens de comportamento
node web\bancada-trilha\audit.js         # 70 checagens da bancada CONTRA o firmware
node web\bancada-trilha\tdma_replay.js   # 43: os testes do tdma_selftest, em Node
```

### Simulador — `tools/sim/` (o firmware/mts DE VERDADE, várias telas, no PC)

```powershell
.\tools\sim\build.ps1        # 6 cenários de trilha: criar/entrar pela lista, perder sinal, líder reiniciando...
```

Compila `firmware/mts` (core, app e quase todo o hal) num container Docker, uma cópia
por tela, e faz as telas conversarem por um "ar" calibrado com o teste no carro.
**Rode antes de gravar qualquer mudança no `firmware/mts`.** Detalhes em `tools/sim/README.md`.

> **Se mexer no firmware, rode o `audit.js`.** Ele lê os `.h`/`.ino` de verdade e
> acusa quando a bancada divergir — inclusive quando um bug reproduzido de propósito
> for "consertado" sem aviso.

### 2. Fase 4 (em construção) — TDMA + mapa offline

Branch **`fase4-tdma-mapa`**. Objetivo: rede plana de 25–50 nós com **TDMA
disciplinado por GPS** (cada carro fala só no seu slot, zero colisão) sobre um
**mapa raster offline** lido do cartão. Alvo final: **ESP32-P4 em ESP-IDF**
(`docs/PLANO_IMPLEMENTACAO_MESH.md`), com as telas S3 servindo de bancada.

| Arquivo | O que é | Estado |
|---|---|---|
| `firmware/tdma_core.h` | frames/slots, fonte de tempo atrás de interface (beacon → PPS) | auto-teste + 4 bugs corrigidos na bancada (ver CHANGELOG) |
| `firmware/tile_pack.h` | leitor de tiles por setor, sem filesystem | conformidade com o gravador validada (28 PASS) |
| `tools/tiles/` | baixa, converte para RGB565, empacota com índice + CRC32 | testado end-to-end no PC |
| `firmware/e22_ping/` | ping-pong SX1262; **mede** o tempo real de TX | compila, **nunca executado** |
| `firmware/tdma_test/` | TDMA de 3 nós com coordenadas falsas | compila, **nunca executado**; agora informa o air-time e não mistura âncoras |
| `firmware/sd_bench/` | mede CS pelo expansor, custo de leitura, contenção do painel | compila, **nunca executado** |
| `firmware/*_selftest/` | testes que rodam em qualquer ESP32, sem periférico | compilam |

## Hardware

| Item | Situação |
|---|---|
| 2× Waveshare ESP32-S3-Touch-LCD-7B (RGB 1024×600, 8 MB PSRAM OPI, 16 MB flash) | **em mãos**, rodando o firmware de produção |
| 3× E22-900M30S (SX1262, 915 MHz, 1 W, TCXO 32 MHz) | **em trânsito** |
| 1× Waveshare ESP32-P4 | **em trânsito** |
| GPS NEO-7M "mini" (header de 5 pinos: VCC/GND/TXD/RXD/**PPS**) | em mãos, **PPS FIADO no IO2** das telas P4 (2026-08-24). A nota antiga "não expõe PPS" era sobre OUTRO breakout (GY-NEO7MV2) |
| Rádio LoRaMESH 915 MHz (Serial1, GPIO 44/43) | em mãos, nas telas |

## Como compilar

`arduino-cli` **não está no PATH** (vive dentro da Arduino IDE). Use sempre:

```powershell
.\tools\build.ps1 firmware\grupo_ws                            # placa padrão: ws7b
.\tools\build.ps1 firmware\tdma_test -Board devkit             # ESP32-S3-DevKitC
.\tools\build.ps1 firmware\sd_bench -Upload -Port COM7
.\tools\build.ps1 x -ListPorts                                 # VID 303A = USB nativa (gravar)
```

**Para a P4 (`firmware/mts`), use o Docker**: desde 2026-10-08 o Smart App Control
bloqueia o `riscv32-esp-elf-ld.exe` e o `build.ps1` falha no link (`collect2:
CreateProcess`). Mesmo core e mesmas bibliotecas deste PC; grava só o app, sem tocar
na NVS:

```powershell
.\tools\fw_docker\build.ps1                          # compila firmware\mts
.\tools\fw_docker\build.ps1 firmware\mts -Porta COM9 # compila e grava (CH343)
```

Versões exatas do ambiente em **`docs/AMBIENTE_BUILD.md`**. Não use `EraseFlash`:
apaga a partição `nvs` (nome da tela, tema, sala).

## Armadilhas que já custaram tempo

Todas verificadas neste repo ou em fonte primária. Não redescubra:

- **O LoRaMESH não pode fazer TDMA.** É um módulo com firmware de rede fechado
  atrás de uma UART: faz mesh multi-hop com ACK próprio, janelas de recepção de
  5–15 s, e **não tem sinal de TxDone**. Sem saber quando o pacote saiu, não há
  slot. Por isso os E22/SX1262.
- **`config_bps()` do LoRaMESH mente no retorno** — devolve false mesmo aplicando.
  Conferir relendo com `read_config_bps()` (`tools/ws_lora_align/`).
- **Escravo LoRaMESH transmite com o PRÓPRIO id**, não com o do destino. Inverter
  isso quebra o link silenciosamente.
- **O painel RGB não tem bounce buffer** (`LGFX_WS7B.h:58`) e lê o framebuffer
  direto da PSRAM por DMA; o PCLK já foi baixado para 12 MHz por starvation.
  Escrever em flash (OTA, NVS) ou saturar a PSRAM causa tearing/deslocamento.
- **Só sobram 10 GPIOs na tela** (`6, 11, 12, 13, 15, 16, 19, 20, 43, 44`): o
  painel come 20 e a PSRAM octal bloqueia 26–37. O E22 precisa de 8 → **não cabe
  na tela**, use um devkit.
- **Não use `setDio2AsRfSwitch`** no E22-900M30S: o PA externo precisa de RXEN e
  TXEN de verdade (`PLANO_IMPLEMENTACAO_MESH.md:99`).
- **E22: VCC em 5 V** (não 3,3; a lógica continua em 3,3), e **antena antes de
  energizar**. Pico de TX > 600 mA reseta a placa sem desacoplamento local.
  O capacitor: **nenhum manual da Ebyte da valor** — os "470 µF" desta base eram
  folclore. O consenso dos seis projetos abertos que funcionam e **100–220 µF
  eletrolitico + 10 µF ceramico + 100 nF, a menos de 10 mm dos pads 9/10**, e a nota de
  aplicacao AN2023005 acrescenta: *"a frequencia de chaveamento do DC-DC afeta o sinal
  de RF; recomenda-se um LDO em serie apos a saida do DC-DC"*.
- **Nunca deixe RXEN e TXEN altos ao mesmo tempo** — danifica. A engenharia reversa do
  modulo mediu isolamento do switch RF de apenas **29–30 dB**, e o manual da um
  *blocking power* maximo de 10 dBm.
- **`begin()` do SX1262 devolvendo −707** é TCXO: tente **2.2** → 1.8 → 1.6. O valor
  correto e o **2,2 V** — o manual E22-M V1.2 (2026-02-06) existe justamente para
  acrescentar a descricao do cristal, e o manual de 2018 dizia 1,8 V. O default do
  RadioLib e 1,6 V, o pior dos tres. **NAO use `radio.XTAL = true`**: esse flag diz
  "nao ha TCXO, e cristal passivo", e este modulo TEM TCXO no DIO3.
- **`setCurrentLimit(140)` DEPOIS do `begin()`** — nao "depois do setOutputPower",
  como esta regra dizia ate 2026-09-08. Com o OCP de fabrica (60 mA) a saida trava em
  ~19,6 dBm sem erro nenhum. O `begin()` grava 60 mA em **tres** pontos
  (`SX126x.cpp:55, 94, 147`); a ordem entre `setCurrentLimit` e `setOutputPower` e
  **indiferente**, porque a RadioLib salva e restaura o OCP em volta do `SetPaConfig`
  desde 2019 (commit `621da8a`). E **140 e o teto**: acima disso o `setCurrentLimit`
  devolve erro e **nao aplica nada, em silencio** (o datasheet permite 157,5 mA, mas
  so escrevendo o registrador `0x08E7` na mao).
- **O ganho do PA e +10 dB, nao +7,25.** Os +7,25 sao do datasheet do CI (YP2233W); a
  **tabela oficial do modulo montado** (manual E22-M V1.2, "RF output power control
  table", VCC 5,0 V, 900 MHz) da: pedido −1 → 9 dBm, 2 → 12, 5 → 15, 7 → 18, 11 → 21,
  14 → 24, 20 → 30. Consequencia pratica: **a frota a 6 dBm irradia ~16 dBm, nao
  13,25**, e `RF_PWR 14` = **24 dBm na antena**.
- **A `paOptTable` da RadioLib 7.5+ nao e monotonica, e sabota qualquer escada de
  potencia.** O dBm pedido nao e o programado: +4 → `paVal +22`; +5 → +11; **+6 →
  paDutyCycle 3, hpMax 1, paVal +21**; +7 → +17. A tabela foi medida num SX1262 **nu**
  ([RadioLib #1628](https://github.com/jgromes/RadioLib/issues/1628)) e o Meshtastic a
  desliga em toda placa com PA externo (`SX126X_NO_POWER_OPTIMIZATION_TABLE`, commit
  `80f8611`). **Com PA externo, use `setOutputPower(p, false)`.**
- **`setOutputPower()` reseta a rampa do PA para 200 us** (hardcoded,
  `SX126x_config.cpp:730`). Qualquer `setPaRampTime()` tem de vir **depois** dele. A
  variante T-Beam **1 W** do Meshtastic usa 800 us: *"requires >800us stabilization"*.
- **`startTransmit()` pode pendurar o firmware para sempre.** Ele termina em
  `while(digitalRead(BUSY)) yield();` **sem timeout** (`SX126x.cpp:1143`). Modulo em
  brownout com BUSY alto = a chamada nunca retorna e o vigia de TX nunca roda. Ver
  `radioBusyLivre()` em `radio.h`.
- **`XOSC_START_ERR` no boot e ESPERADO, nao defeito.** O datasheet (13.3.6) diz que o
  chip ainda nao sabe que ha TCXO e que *"o usuario deve simplesmente limpar essa flag
  com ClearDeviceErrors"*. O registrador e **latcheado**: sem limpar, toda leitura
  futura devolve o aviso de boot. **Foi um GetDeviceErrors lido sem clear que condenou
  o modulo da tela A em 2026-08-24** — o veredito de "defeituoso" nao esta provado.
- **Em runtime, −707 significa brownout, nao TCXO.** Depois que o `begin()` passou uma
  vez, o Meshtastic reproduziu isso simulando queda de alimentacao
  ([PR #11678](https://github.com/meshtastic/firmware/pull/11678)).
- **30 dBm e ilegal no Brasil em BW125.** O Ato ANATEL 14.448/2017, item 10.3.3, limita
  a densidade espectral a 8 dBm/3 kHz → teto conduzido de `8 + 10·log10(BW/3kHz)` =
  **24,2 dBm em BW125** (27,2 em BW250; 30,2 em BW500). Nao impede testar em trilha
  particular, mas fecha a porta de 1 W para produto.
- **O header CAN/RS485 nao serve para alimentar o E22.** No esquematico oficial da 7B,
  o `Core_5V` do pino 1 e ligacao direta ao trilho: **sem fusivel, sem diodo, sem
  ferrite, e com 100 nF de capacitancia local**. O degrau de 620 mA afunda o trilho
  antes de qualquer regulador reagir, e o buck do 3,3 V e o boost do backlight
  penduram no mesmo no. **E nunca injete 5 V ali**: o diodo de corpo do FET do botao
  (Q8) realimenta o `VCC_5V`, ignora o botao de desligar e briga com o VBUS do USB.
  `J4`/`BAT` e entrada de celula 1S (3,0–4,2 V), nao 5 V.
- **A LovyanGFX deste PC tem um patch manual ("PATCH REDEA", 2026-09-20) no
  `Bus_DSI.cpp`** que troca o clock do PHY DSI para XTAL (P4 rev 3). Nas nossas
  telas P4 (ROM `esp32p4-eco2`, rev 1.3) isso dá `abort()` no `esp_lcd_new_dsi_bus`:
  boot-loop de ~1,2 s com tela preta, em **qualquer** firmware compilado aqui.
  O `firmware/mts` não depende disso: `hal/bus_dsi.h` escolhe o clock pela revisão
  do chip. Os firmwares antigos (`mts_p4`, `mts_campo`) usam o `Bus_DSI` da
  biblioteca e **vão entrar em boot-loop se forem recompilados neste PC**.
- **Idade de carimbo no `firmware/mts`: sempre `tempoDesde()`** (`core/tempo.h`).
  O `servico()` lia `agora` no começo e o rádio carimbava o pacote depois; o
  `agora - ultimoMs` em `uint32_t` dava a volta e o carro era apagado da frota logo
  após chegar. No carro (2026-09-30) o seguidor via o traçado e nunca o ícone do
  líder. O `mts_core_test.js` reprova qualquer subtração crua que volte ao core.
- **Smart App Control (Windows 11) bloqueia compilador não assinado.** O MinGW
  instalado saía com `0xC0E90002` sem mensagem nenhuma, e o `cc1plus` do Arduino já
  deu "Permission denied" intermitente pelo mesmo motivo. O simulador compila no Docker.
  Não desligue o SAC: só volta reinstalando o Windows.
- **`huge_app` não tem partição OTA** (um slot só). O binário usa 14 % de 3 MB,
  então qualquer esquema de dois slots cabe — mas trocar o esquema exige uma
  gravação **por cabo**.
- **`--bbox` do `tiles.py` precisa de `=`**: `--bbox=-23.5,...`. Latitude negativa
  é interpretada como opção pelo argparse.

## Convenções de código

- **Comentários e mensagens em português sem acentuação** (o código existente é
  assim; fontes do display e serial não lidam bem com acentos). Identificadores em
  inglês ou português conforme o vizinho.
- Lógica compartilhada em **headers `inline`/header-only** (`trilha_core.h`,
  `tdma_core.h`, `tile_pack.h`), sem dependência de hardware, para o mesmo código
  servir S3 e P4. A "casca" (`.ino`) cuida de tela, toque, rádio e persistência.
- Comentários explicam **por que**, não o que — especialmente quando a escolha
  parece errada (ex.: por que o redraw é lento de propósito, por que o PCLK é
  12 MHz).
- Cada decisão contra-intuitiva vira comentário com a razão, para ninguém
  "consertar" e reintroduzir o bug.
- Testes que não precisam de hardware são bem-vindos: `*_selftest/` roda em
  qualquer ESP32, `tools/tiles/verify_reader.py` roda no PC.

## Próximo passo concreto

O que dá para fazer **sem hardware novo**, em ordem de valor (detalhes em
`docs/RETOMAR.md`):

1. **Rodar o `sd_bench` numa tela** — mede se o CS do cartão pelo expansor I2C
   funciona, o custo real de leitura e a contenção do painel. **A arquitetura de
   render do mapa está bloqueada nesses três números** e não deve ser decidida por
   estimativa.
2. ~~Integrar `tdma_core.h` com `world[]`/`worldToScreen`~~ — **feito na bancada
   web** (modo TDMA), que é o que expôs os quatro bugs do `tdma_core.h`. Falta o
   sketch equivalente para rodar na tela de verdade, com `#define BANCADA 1`.
3. **Roster** (nomes/cores) na pilha nova: hoje o pacote só leva posição e flags.
4. **Orçamento de link**: alcance esperado com 1 W + 5 dBi em mata fechada, e o
   custo em capacidade de subir o SF. É o risco nº 1 do projeto.

## Documentos

| Arquivo | Para quê |
|---|---|
| `docs/ESTUDO_E22.md` | **Estudo 3** — o E22 na prática: oito projetos abertos com o mesmo módulo, o mecanismo do colapso lido no datasheet, e dois projetos que já fizeram o MTS (Spoke, race tracker) |
| `docs/PLANO_7B.md` | **o plano da tela de 7"** — consolida 15 frentes de pesquisa: ANATEL, ganho real do PA, alimentação, antena, alcance medido, escala |
| `docs/PROTOCOLO_FASE0.md` | **como executar sem se enganar** — uma variável por vez, com tabela de decisão por resultado |
| `docs/AUDITORIA_RADIO.md` | os 12 achados da auditoria do caminho de rádio contra o datasheet e o Meshtastic |
| `docs/MONTAGEM.md` | **passo a passo de bancada** — soldar o E22, chicote para as telas P4, GPS/PPS, e as 5 coisas que queimam |
| `docs/RETOMAR.md` | **comece aqui** — passo a passo, pinagem, o que medir, compras |
| `docs/AMBIENTE_BUILD.md` | versões de core/bibliotecas, FQBN, tamanhos de binário |
| `docs/PLANO_IMPLEMENTACAO_MESH.md` | o plano do produto final (P4, ESP-IDF, TDMA, mapa) |
| `docs/PLANO_MODO_GRUPO.md` | modo grupo (roster, cores, provisionamento) |
| `docs/PLANO_SEGUIDOR_CYD.md` | fase anterior (CYD/GIGA) — referência histórica |
| `tools/tiles/README.md` | formato do container de tiles e licença dos mapas |
| `CHANGELOG.md` | histórico por fase |
