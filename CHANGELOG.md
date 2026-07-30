# Changelog

Histórico das fases do projeto. As datas são aproximadas (marcos, não releases formais).

## Fase 4 — Rumo ao TDMA + mapa offline (em andamento)

Preparação da pilha nova, em sketches separados. **O `firmware/grupo_ws/` não foi
alterado** — continua sendo o firmware que roda nas duas telas com LoRaMESH.

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
