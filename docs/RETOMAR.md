# Retomar quando o hardware chegar

> Escrito em **2026-07-30**, no fim da sessão que preparou a Fase 4.
> Estado: **3× E22-900M30S em trânsito**, **1× Waveshare ESP32-P4 em trânsito**,
> 2 telas Waveshare ESP32-S3-Touch-LCD-7B em mãos e funcionando com o firmware
> atual (`firmware/grupo_ws`, LoRaMESH).
>
> Tudo abaixo **compila**. O que não pôde ser executado está marcado.
> Branch: `fase4-tdma-mapa`.

---

## Comece por aqui (não precisa de nada novo)

Três sketches rodam **hoje**, numa das telas que você já tem:

```powershell
.\tools\build.ps1 x -ListPorts                                          # VID 303A = USB nativa
.\tools\build.ps1 firmware\tdma_selftest -Board ws7b -Upload -Port COMx
.\tools\build.ps1 firmware\tile_selftest -Board ws7b -Upload -Port COMx
.\tools\build.ps1 firmware\sd_bench      -Board ws7b -Upload -Port COMx   # precisa de um microSD
```

Os dois primeiros imprimem `PASS`/`FAIL` no serial (115200) e validam a
matemática no silício real. O terceiro é o único código desta fase que **nunca
foi executado** — ele mede as três coisas que decidem a arquitetura do mapa:

1. Se o cartão inicializa com o **CS no expansor I2C** (EXIO4, não num GPIO) e
   com qual das duas estratégias.
2. Quanto custa ler 1 tile (128 KB) e um grid 3×3 (1,18 MB) por SPI.
3. **Quanto o painel RGB sofre** durante a leitura (fator de degradação do
   `pushImage`, medido — não impressão).

**Anote a saída dele.** Sem esses números, decidir camadas/cache de render é
chute.

Para ter um mapa de teste no cartão sem depender de chave de servidor:

```powershell
python tools\tiles\tiles.py synth --out synth\ --zoom 15-16 --bbox=-23.56,-46.66,-23.54,-46.64
python tools\tiles\tiles.py pack --in synth\ --out trilha.img
python tools\tiles\tiles.py verify --img trilha.img
# gravar: dd if=trilha.img of=\\.\PhysicalDriveN bs=1M   <- CONFIRME o numero do disco
```

---

## Quando os E22 chegarem

### Antes de energizar — três coisas que queimam ou frustram

1. **Antena no IPEX ANTES de ligar.** Transmitir 1 W sem antena danifica o PA.
2. **VCC do E22 em 5 V** (não 3,3 V), com **capacitor ≥470 µF** perto do módulo.
   O pico de TX passa de 600 mA; sem o capacitor a placa reseta ao transmitir e
   você vai caçar um fantasma. A lógica SPI é 3,3 V e casa direto.
3. **Antena do rádio longe da do GPS.** 1 W em 915 MHz dessensibiliza o receptor
   GNSS de 1575 MHz. O `README.md:42` já avisa do curto entre os módulos.

### Onde fiar

**Não cabe nas telas.** O painel RGB consome 20 dos 45 GPIOs; sobram 10
(`6, 11, 12, 13, 15, 16, 19, 20, 43, 44`), e desses 19/20 são o USB e 11/12/13 o
cartão. O E22 precisa de **8** (SCK, MOSI, MISO, NSS, BUSY, DIO1, RXEN, TXEN) —
com o console serial comendo um par, faltam 1 ou 2 pinos em qualquer arranjo.
Não use `setDio2AsRfSwitch` para economizar: `PLANO_IMPLEMENTACAO_MESH.md:99`
registra que nesse módulo o PA precisa de RXEN/TXEN de verdade.

Pinagem já escrita nos sketches (ESP32-S3-DevKitC-1):

| Sinal E22 | GPIO |
|---|---|
| SCK | 12 |
| MISO | 13 |
| MOSI | 11 |
| NSS | 10 |
| BUSY | 9 |
| DIO1 | 8 (precisa de interrupção) |
| NRST | 14 |
| RXEN | 17 |
| TXEN | 18 |

### Fase 1 — `firmware/e22_ping`

```powershell
.\tools\build.ps1 firmware\e22_ping -Board devkit -Upload -Port COMx
```

Digite `0` no serial numa placa e `1` na outra (salva em NVS). O nó 0 manda
beacon 1 Hz; o outro responde.

**O número que você vem buscar:** `txUs(min/med/max)` — o tempo real entre
`startTransmit` e o `txDone` da ISR. É isso que dimensiona o slot do TDMA. O
plano estima 40–60 ms de air-time para 16 B em SF7/BW125; o sketch imprime a
tabela do `getTimeOnAir` ao lado, porque essa função tem bug conhecido de CR.

Se `begin()` devolver **−707**: é TCXO. A etiqueta do seu lote diz TCXO 32 MHz,
então `RF_TCXO 1.8` deve funcionar; se não, tente `1.6`, e por último
`radio.XTAL = true` antes do `begin()` (algumas variantes do E22 vêm com XTAL).
O sketch já imprime essa receita quando falha.

### Fase 2 — `firmware/tdma_test` (aqui os 3 rádios importam)

```powershell
.\tools\build.ps1 firmware\tdma_test -Board devkit -Upload -Port COMx
```

Um `nodeId` por placa (`0`, `1`, `2` no serial; reinicia sozinho). **Ligue o nó 0
primeiro** — ele é a âncora do frame.

Ajuste `N_SLOTS`/`FRAME_SECS` com o air-time **medido** na Fase 1; o `setup()`
recusa a configuração se o pacote não couber no slot e imprime a folga.

O que observar no relatório de 5 s:

| Contador | Significado se subir |
|---|---|
| `rxCRC` | colisão ou link ruim |
| `slotErrado` | desalinhamento (guarda pequena, loop travado) |
| `perdiJanela` | o loop perdeu a janela do próprio slot |
| `idade` de cada peer | se cresce sem parar, aquele nó está passando fome |

**Com 3 nós é o primeiro teste onde colisão realmente existe** — com 2 não há o
que provar.

### Fase 3 — PPS (precisa resolver o GPS)

O GPS do projeto é **NEO-7M e o PPS não está fiado** — está escrito em
`tools/ws_diag/ws_diag.ino:23`: *"GPS TXD -> GPIO6 (RXD/PPS nao liga)"*. Pior: os
breakouts GY-NEO7MV2 baratos **não trazem PPS no header**; o sinal só sai
soldando no pad do LED de fix.

Duas saídas: soldar, ou trocar por **ATGM336H-5N-31** (~US$ 6–16), que traz PPS
na pinagem oficial de 5 vias e é multi-constelação (−162 dBm).

A troca no código é **uma linha**: onde hoje se chama `tdmaOnBeacon(tdma, rxUs)`,
passa a chamar `tdmaOnPps(tdma, ppsUs, utcSec)`. Foi para isso que a fonte de
tempo ficou atrás de interface. Regras da ISR:

- `IRAM_ATTR`, e dentro dela **só** `esp_timer_get_time()` + contador + flag.
  Nada de rádio, serial ou `printf`.
- O NMEA diz *qual* segundo é; o PPS dá a borda. A sentença chega **depois** do
  pulso, então o segundo a casar com a borda é `(ss do último NMEA) + 1`.
- Precisão do PPS no NEO-7 é 30 ns; o gargalo vai ser o ESP32 e o rádio, ordens
  de magnitude acima.

---

## Quando o P4 chegar

O plano (`docs/PLANO_IMPLEMENTACAO_MESH.md`) é ESP-IDF, **não** Arduino. O que
migra sem reescrever:

| Arquivo | Como migra |
|---|---|
| `firmware/tdma_core.h` | direto — só depende de `esp_timer_get_time()`, que é IDF nativo |
| `firmware/tile_pack.h` | direto — a leitura entra por callback; troque `SD.readRAW` por `sdmmc_read_sectors` (SDIO, 4-bit, ~8 MB/s contra ~1,7 do SPI) |
| `firmware/trilha_core.h` | quase: `millis()` → `esp_timer_get_time()/1000`, `radians(x)` → `(x*M_PI/180)` |
| RadioLib | roda nativo em ESP-IDF (tem HAL própria) — a Fase 1–3 validada no S3 vale |

Cuidados registrados na pesquisa:

- O SD do P4 fica no **slot 0, pinos fixos 39–48**, com comutação 3,3 V/1,8 V por
  LDO interno (`sd_pwr_ctrl_new_on_chip_ldo()`). Não consome GPIO do header.
- **WiFi via ESP32-C6 (`esp_hosted`) tem bugs abertos em 2026.** Não coloque
  função crítica nele — o próprio plano já trata WiFi como opcional (só OTA).
- Sobram 17–28 GPIOs livres conforme o modelo: folga para os 12 que o conjunto
  rádio + PPS + GPS precisa.
- MIPI-DSI usa pinos dedicados (não rouba GPIO do matrix), ao contrário do RGB
  paralelo do S3 — é por isso que no P4 tudo convive.

**TDMA precisa de ≥2 nós que se ouçam.** Com 1 P4 e rádio diferente das telas
(SX1262 vs LoRaMESH, que **não conversam entre si**), valide nos S3/devkits e só
depois porte.

---

## Lista de compras

| Item | Qtd | Por quê |
|---|---|---|
| ESP32-S3-DevKitC-1 | 2–3 | 8 GPIOs para o rádio não caem atrás do painel RGB. Sem isso as Fases 1–3 não rodam. ~R$ 40 |
| GPS com PPS no header (ATGM336H-5N-31) | 3 | o NEO-7M atual não expõe PPS. ~US$ 6–16 |
| Capacitor eletrolítico ≥470 µF | 3 | bulk no VCC do E22; sem ele a placa reseta no pico de TX |
| microSD industrial pSLC 32 GB, −40/+85 °C, com power-fail protection | 2 | tiles; o segundo é clone de reserva. ~US$ 39 |

Sobre o cartão: o uso é ~100 % leitura, então **"High Endurance" de dashcam é
dinheiro jogado fora** (vende endurance de *escrita*). O que sobra em read-only é
read disturb, retenção de carga a quente (painel ao sol passa de 70 °C) e — o
contra-intuitivo — **wear leveling em background disparado por leitura**: perder
energia nesse instante corrompe dados mesmo com o filesystem montado read-only.
Só o pSLC industrial endereça os três. E o ponto fraco que sobra é **mecânico**:
socket push-push sob vibração fica "clicado" mas eletricamente intermitente —
prenda o cartão com um spacer rígido pela tampa (sem colar, para poder trocar) e
ponha a placa sobre buchas de borracha.

---

## O que está pronto, e o que não está

| Componente | Estado |
|---|---|
| Ambiente de build | **funcionando**, documentado em `docs/AMBIENTE_BUILD.md` |
| `tdma_core.h` | escrito, **matemática validada por auto-teste** (falta rodar no silício) |
| `tile_pack.h` | escrito, **conformidade com o gravador validada** (28 PASS no PC) |
| `tools/tiles/` | **testado end-to-end** no PC: synth → pack → verify → extract |
| `e22_ping`, `tdma_test` | compilam; **nunca executados** (sem rádio) |
| `sd_bench` | compila; **nunca executado** (precisa de tela + cartão) |
| `grupo_ws` | intocado, compila em 449 KB |

## Lacunas conhecidas (decisões que ficaram abertas de propósito)

- **Roster no TDMA**: nomes e cores dos carros em baixa frequência ainda não
  existem na pilha nova. Hoje o `tdma_core.h` só carrega posição/flags. O
  `trilha_core.h` tem `packRoster`/`CMD_ROSTER` para reaproveitar.
- **Integração TDMA + display**: falta o sketch que junta `tdma_core.h` com
  `world[]`/`haversine`/`worldToScreen` do `trilha_core.h` e desenha. Dá para
  escrever e compilar sem rádio.
- **Camada de render do mapa**: grid de tiles, câmera north-up, ícone do carro
  girando, rastro e anéis de distância por cima. **Esperando os números do
  `sd_bench`** — é a decisão que não deve ser tomada por estimativa.
- **`multi-hop`**: a v1 assume todos os nós no alcance uns dos outros. Alcance
  real na mata é o risco nº 1 e só o campo prova.
- **OTA/WiFi**: investigado e adiado. Requer trocar a tabela de partições
  (`huge_app` tem um slot só) e uma última gravação por cabo. O binário atual usa
  14 % de 3 MB, então qualquer esquema de dois slots cabe com folga de 4× — não
  precisa de `partitions.csv` customizado.
