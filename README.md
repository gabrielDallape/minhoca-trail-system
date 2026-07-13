# Outdoor Trail Follow Me 🧭

Sistema **"siga o líder"** por rádio **LoRa**: um aparelho **líder** anda e transmite a própria posição GPS; um aparelho **seguidor** recebe e plota um **mapa estilo Waze** (você no centro, o caminho do líder, distância), pra você seguir o líder mesmo sem vê-lo.

Nasceu como a Parte 2 de um projeto de odômetro (`odometro.ino`).

## Como funciona

- **LÍDER** (móvel): lê o próprio GPS e **envia** a posição por LoRa. Fica quieto no rádio até travar o GPS (evita auto-interferência) e depois manda a cada 2s. Cada pacote leva um **histórico rolante** dos últimos pontos (buffer) pra o seguidor remontar as curvas mesmo perdendo alguns pacotes.
- **SEGUIDOR** (base/carro de trás): **recebe** e desenha o mapa — EU no centro (heading-up), **rastro já percorrido em ciano**, **caminho à frente até o líder em roxo** (estilo Waze), distância, satélites, e zoom +/− por toque. Só recebe → sem colisão de rádio.

Comunicação **unidirecional** (líder → seguidor).

## Hardware

| Papel | Placa | GPS | Rádio |
|---|---|---|---|
| **Líder** | ESP32 CYD (ESP32-2432S028R) | u-blox NEO-7M + antena externa | Radioenge LoRaMESH (915 MHz) |
| **Seguidor** | Arduino GIGA R1 + Display Shield | u-blox NEO-7M + antena externa | Radioenge LoRaMESH (915 MHz) |

Detalhes de pinagem nos comentários de cada sketch.

## Sketches principais

- **`cyd_lead/`** — firmware do **LÍDER** (CYD): lê GPS, envia posição + histórico (cmd 0x12).
- **`giga_follow/`** — firmware do **SEGUIDOR** (GIGA): recebe e plota o mapa Waze, com zoom.

### Ferramentas de diagnóstico
- `gps_bench/`, `gps_bench_giga/` — cronômetro de fix (TTFF) + contagem de satélites.
- `gps_scan/` — descobre em qual pino GPIO o TXD do GPS está ligado.
- `lora_id/`, `lora_scan/`, `lora_ping/` — testes do link LoRa.

### Histórico / experimentos
Demais pastas (`cyd_dual`, `giga_dual`, `lora_*`, `ra8875_*`, `test_*`, etc.) são etapas e experimentos do desenvolvimento — mantidos como referência.

## Protocolo LoRa (cmd 0x12 — posição + histórico)

Payload: `[flags(bit0=fix)] [sats] [N] [seqNewest uint16 LE]` + `N × (lat int32 LE ×1e7, lon int32 LE ×1e7)`.
O seguidor deduplica por número de sequência e remonta o caminho sem buracos.

## Status

Sistema líder→seguidor funcionando end-to-end (GPS + LoRa + mapa + buffer). Pendências: teste de rua e melhorias de UI (ver `PLANO_SEGUIDOR_CYD.md`).

## Build

Arduino CLI. Cores: `esp32:esp32` (CYD) e `arduino:mbed_giga` (GIGA). Bibliotecas: LovyanGFX, Arduino_GigaDisplay_GFX, Arduino_GigaDisplayTouch, TinyGPSPlus, LoRaMESH.
