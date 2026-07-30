# CLAUDE.md

Contexto para quem (ou o que) abre este repositório sem memória da sessão anterior.
**Leia `docs/RETOMAR.md` em seguida** — ele tem o passo a passo prático.

## O que é o projeto

Sistema **"siga o líder" por rádio LoRa para trilhas off-road**. Dois ou mais
aparelhos com GPS e tela: o líder anda e transmite o **trajeto que fez** (não só a
posição atual); os seguidores plotam um mapa estilo Waze — você no centro, o
caminho do líder à frente, o rastro já percorrido, distância — e conseguem seguir
mesmo sem ver o carro da frente. Tem alerta (botão pinta o trecho entre os dois
carros de vermelho), aviso de perda de sinal e catch-up que preenche o vão quando
o rádio volta.

Nasceu como a Parte 2 de um projeto de odômetro (`legacy/odometro/`).

## Estado atual (2026-07-30)

Duas coisas rodam em paralelo, e **confundi-las é o erro mais fácil de cometer**:

### 1. Produção — `firmware/grupo_ws/` (ESP32-S3 + LoRaMESH)

Firmware que **funciona hoje** nas duas telas Waveshare ESP32-S3-Touch-LCD-7B.
Ponto-a-ponto de 2 nós, mapa vetorial, rádio LoRaMESH em UART.

> **REGRA DURA: não altere `firmware/grupo_ws/` sem o usuário pedir explicitamente.**
> É o único firmware validado em campo. A pilha nova nasce em sketches separados.

### 2. Fase 4 (em construção) — TDMA + mapa offline

Branch **`fase4-tdma-mapa`**. Objetivo: rede plana de 25–50 nós com **TDMA
disciplinado por GPS** (cada carro fala só no seu slot, zero colisão) sobre um
**mapa raster offline** lido do cartão. Alvo final: **ESP32-P4 em ESP-IDF**
(`docs/PLANO_IMPLEMENTACAO_MESH.md`), com as telas S3 servindo de bancada.

| Arquivo | O que é | Estado |
|---|---|---|
| `firmware/tdma_core.h` | frames/slots, fonte de tempo atrás de interface (beacon → PPS) | matemática validada por auto-teste |
| `firmware/tile_pack.h` | leitor de tiles por setor, sem filesystem | conformidade com o gravador validada (28 PASS) |
| `tools/tiles/` | baixa, converte para RGB565, empacota com índice + CRC32 | testado end-to-end no PC |
| `firmware/e22_ping/` | ping-pong SX1262; **mede** o tempo real de TX | compila, **nunca executado** |
| `firmware/tdma_test/` | TDMA de 3 nós com coordenadas falsas | compila, **nunca executado** |
| `firmware/sd_bench/` | mede CS pelo expansor, custo de leitura, contenção do painel | compila, **nunca executado** |
| `firmware/*_selftest/` | testes que rodam em qualquer ESP32, sem periférico | compilam |

## Hardware

| Item | Situação |
|---|---|
| 2× Waveshare ESP32-S3-Touch-LCD-7B (RGB 1024×600, 8 MB PSRAM OPI, 16 MB flash) | **em mãos**, rodando o firmware de produção |
| 3× E22-900M30S (SX1262, 915 MHz, 1 W, TCXO 32 MHz) | **em trânsito** |
| 1× Waveshare ESP32-P4 | **em trânsito** |
| GPS NEO-7M, NMEA 9600 no GPIO6 (só RX) | em mãos — **PPS NÃO fiado**, e o breakout não expõe PPS no header |
| Rádio LoRaMESH 915 MHz (Serial1, GPIO 44/43) | em mãos, nas telas |

## Como compilar

`arduino-cli` **não está no PATH** (vive dentro da Arduino IDE). Use sempre:

```powershell
.\tools\build.ps1 firmware\grupo_ws                            # placa padrão: ws7b
.\tools\build.ps1 firmware\tdma_test -Board devkit             # ESP32-S3-DevKitC
.\tools\build.ps1 firmware\sd_bench -Upload -Port COM7
.\tools\build.ps1 x -ListPorts                                 # VID 303A = USB nativa (gravar)
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
- **E22: VCC em 5 V** (não 3,3) com capacitor ≥470 µF, e **antena antes de
  energizar**. Pico de TX > 600 mA reseta a placa sem o capacitor.
- **`begin()` do SX1262 devolvendo −707** é TCXO: tente 1.8 → 1.6 →
  `radio.XTAL = true`.
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
2. **Integrar `tdma_core.h` com `world[]`/`worldToScreen` do `trilha_core.h`** e
   desenhar — roda nas telas com posições falsas, sem rádio.
3. **Roster** (nomes/cores) na pilha nova: hoje o pacote só leva posição e flags.
4. **Orçamento de link**: alcance esperado com 1 W + 5 dBi em mata fechada, e o
   custo em capacidade de subir o SF. É o risco nº 1 do projeto.

## Documentos

| Arquivo | Para quê |
|---|---|
| `docs/RETOMAR.md` | **comece aqui** — passo a passo, pinagem, o que medir, compras |
| `docs/AMBIENTE_BUILD.md` | versões de core/bibliotecas, FQBN, tamanhos de binário |
| `docs/PLANO_IMPLEMENTACAO_MESH.md` | o plano do produto final (P4, ESP-IDF, TDMA, mapa) |
| `docs/PLANO_MODO_GRUPO.md` | modo grupo (roster, cores, provisionamento) |
| `docs/PLANO_SEGUIDOR_CYD.md` | fase anterior (CYD/GIGA) — referência histórica |
| `tools/tiles/README.md` | formato do container de tiles e licença dos mapas |
| `CHANGELOG.md` | histórico por fase |
