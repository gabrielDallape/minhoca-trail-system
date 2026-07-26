# Outdoor Trail Follow Me 🧭

![platform](https://img.shields.io/badge/platform-ESP32--S3-informational)
![display](https://img.shields.io/badge/display-Waveshare%207B%201024×600-informational)
![radio](https://img.shields.io/badge/radio-LoRaMESH%20915MHz-informational)
![build](https://img.shields.io/badge/arduino--cli-compila-success)
![license](https://img.shields.io/badge/license-MIT-blue)

Sistema **"siga o líder"** por rádio **LoRa** para trilhas off-road: dois aparelhos idênticos, um **LÍDER** e um **SEGUIDOR**, conversam ponto-a-ponto. O líder anda e transmite o **trajeto** que fez (não só a posição); o seguidor plota um **mapa estilo Waze** (você no centro, o caminho do líder à frente, o rastro já percorrido, distância), pra seguir o líder mesmo sem vê-lo.

Nasceu como a Parte 2 de um projeto de odômetro.

```
   LÍDER (Waveshare 7B)                          SEGUIDOR (Waveshare 7B)
  ┌────────────────────┐                        ┌────────────────────┐
  │ GPS ─► grava rota   │   0x12 pos+trajeto    │  recebe ─► mapa Waze │
  │ route[] (~6 km)     │ ─────────────────────►│  roxo: a percorrer   │
  │                     │                        │  azul: percorrido    │
  │ recebe pos+ACK      │◄───────────────────── │  ► envia pos + ACK   │
  │ catch-up p/ o vão   │   0x11 pos+alerta+ACK  │                      │
  └────────────────────┘        LoRa P2P         └────────────────────┘
```

## Arquitetura atual (2 telas Waveshare)

O projeto passou por várias fases de hardware (CYD líder + GIGA seguidor). A versão **atual e em uso** são **duas telas Waveshare ESP32-S3-Touch-LCD-7B** idênticas rodando o **mesmo firmware** (`grupo_ws/`). O papel é escolhido na tela inicial por toque:

- **CRIAR SALA** → vira **LÍDER** (grava o próprio trajeto e o envia).
- **ENTRAR** → vira **SEGUIDOR** (recebe o trajeto e navega em cima dele).

### Hardware por tela

| Componente | Detalhe |
|---|---|
| Placa | Waveshare ESP32-S3-Touch-LCD-7B (RGB 1024×600, PSRAM 8MB) |
| Touch | GT911 (I2C 0x5D via Wire nos GPIO 8/9) |
| Expansor IO | CH32V003 (I2C 0x24) — controla painel, backlight, reset do touch |
| GPS | u-blox (NMEA 9600) no **GPIO6** (Serial2 RX) |
| Rádio | Radioenge LoRaMESH 915 MHz (Serial1, **GPIO 44/43**) |
| Gráfico | LovyanGFX + LGFX_Sprite (double-buffer na PSRAM) |

> ⚠️ **Isolar o LoRa do GPS na montagem.** Um contato acidental entre os pinos dos dois módulos causa curto e esquentamento. Use fita/espaçador entre eles.

## Funcionalidades

- **Mapa heading-up** com você no centro, anéis de distância (radar) e norte.
- **Cores do caminho** (fixas, independem do tema): **roxo** = caminho a percorrer até o líder; **azul** = rastro já percorrido.
- **Trajeto real** (breadcrumb): o líder envia o histórico de pontos do caminho, não uma linha reta — o seguidor remonta as curvas.
- **Alerta**: qualquer um aperta o botão → o **trecho do caminho entre os dois carros fica vermelho** + **borda vermelha piscando** na tela.
- **Perda de sinal**: **borda laranja piscando** nos dois — texto **"REDUZA"** no líder (pra diminuir a velocidade) e **"SINAL PERDIDO"** no seguidor.
- **Catch-up**: ao reconectar, o líder reenvia o trecho perdido em blocos rápidos até o seguidor alcançar — **preenche o vão de verdade**, sem linha reta pontilhada.
- **Fora do trajeto**: se o seguidor se afasta do caminho, **borda amarela piscando** + **"FORA DO TRAJETO"**.
- **Buffer de ~6 km** de trajeto na memória (PSRAM), o traçado não some conforme anda.
- **Zoom** +/− por toque, seletor de **tema** (Rally/Tático/HUD), nome da tela editável.

**Prioridade das bordas** (se coincidirem): vermelho (alerta) → laranja (perda) → amarelo (fora do trajeto).

## Protocolo LoRa (ponto-a-ponto)

Par fixo: líder = id 0, seguidor = id 1. Escravo transmite com o próprio id; mestre endereça ao destino.

- **`0x11` — seguidor → líder**: `[flags(bit0=fix, bit1=alert, bit2=temTrajeto)] [lat i32 LE ×1e7] [lon i32 LE ×1e7] [lastSeq u16 LE]`.
  O `lastSeq` é o **ACK** — até que ponto do trajeto o seguidor já tem — usado para o catch-up.
- **`0x12` — líder → seguidor**: `[flags(bit0=fix, bit1=alert, bit3=catchup)] [lat] [lon] [N] [seqBase u16 LE]` + `N × (lat i32, lon i32)`.
  Envia posição + um bloco do trajeto começando em `seqBase`. Fluxo normal manda os últimos pontos; em catch-up (seguidor muito atrás) reenvia a partir de `ACK+1` em blocos, acelerando para ~3 Hz até alcançar.

O seguidor só aceita pontos **contíguos** (evita buracos), ou um salto quando a flag de catch-up indica perda maior que o buffer.

Rádio travado no mesmo canal em ambas via `config_bps(BW500, SF7, CR4_5)` no setup.

## Estrutura do repositório

```
firmware/            Firmware EM USO
  trilha_core.h        núcleo compartilhado (protocolo, temas, estado, route[]/breadcrumb, geo)
  grupo_ws/            firmware das 2 telas Waveshare (líder e seguidor no mesmo binário)
tools/               Ferramentas de bring-up/diagnóstico da Waveshare
  ws_diag/ ws_hello/ ws_quiet/ ws_lcd_oficial/   tela/painel/touch
  ws_touch_diag/                                 GT911 (varre I2C, Product ID)
  ws_lora_scan/                                  acha os pinos do LoRa (UART2)
  ws_lora_cfg/                                   lê config do LoRaMESH (não-destrutivo)
  ws_lora_align/ ws_lora_commission/             alinha/comissiona o rádio
  giga_lora_cfg/                                 lê config do LoRa do GIGA
legacy/              Fases anteriores (referência histórica)
  odometro/            projeto original (Parte 1)
  cyd_* giga_* lora_* gps_* test_* ra8875_*   experimentos e etapas
  grupo/ grupo_giga_follow/ grupo_radio/       Modo Grupo no CYD/GIGA (descontinuado)
docs/                Planos de desenvolvimento
```

## Fluxo de trabalho (gravar × usar)

A Waveshare tem **duas entradas USB-C**:
- **Porta USB nativa** (VID 303A) — usada para **gravar** o firmware e ler o serial/debug.
- **Porta UART1** (chip CH343, VID 1A86) — usada para **usar** em campo (só energia).

Regras aprendidas em campo:
- **Gravar** sempre pela porta USB nativa. As portas COM reenumeram muito — localize por VID 303A.
- **Usar** pela UART1 **com o switch em UART2** (a posição UART1 rouba os pinos 44/43 do LoRa).
- Abrir o serial na porta USB nativa **reseta a tela** — normal; por isso o uso em campo é pela UART1.

### GPS
- LED do GPS: **piscando 1×/s** = travou nos satélites (fix); **fixo/aceso** = ainda procurando.
- GPS **só pega a céu aberto** — atrás de vidro/telhado dá `sats=0` mesmo com o módulo perfeito. Cold start pode levar minutos.

## Build

Arduino CLI. Core: `esp32:esp32`.

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc" firmware/grupo_ws
arduino-cli upload  -p <PORTA_303A> --fqbn "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc" firmware/grupo_ws
```

Bibliotecas: LovyanGFX, TinyGPSPlus, LoRaMESH.

## Status

Duas telas Waveshare gravadas e validadas na bancada: touch, LoRa (pareamento + troca de pacotes) e GPS (fix a céu aberto) OK nas duas. **Pendente:** teste de campo com as duas em movimento (validar bordas, alerta pintando o caminho e catch-up preenchendo a perda de sinal).
