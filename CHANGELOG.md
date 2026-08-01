# Changelog

Histórico das fases do projeto. As datas são aproximadas (marcos, não releases formais).

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
