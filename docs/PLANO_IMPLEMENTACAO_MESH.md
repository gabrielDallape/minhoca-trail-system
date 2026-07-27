# Plano de Implementação — Rede TDMA-LoRa (25–50 nós)

> Guia de construção do firmware da rede "siga o líder" com TDMA sincronizado por GPS.
> Consolida a pesquisa técnica de implementação (RadioLib+E22, TDMA por PPS, arquitetura ESP32-P4).
> **Data:** 2026-07-26. Os trechos de código são **referência** (verificados contra fontes reais, mas não testados no hardware final).

---

## 1. Visão geral da arquitetura

Cada aparelho (nó) é um **trio**: GPS → ESP32-P4 (cérebro, na tela) → rádio LoRa (E22/SX1262).

- **Rede plana, 1 salto, broadcast:** cada carro transmite sua posição no seu turno; **todos os outros escutam e remontam a lista (`world[]`) sozinhos**. Não precisa do líder reencaminhar (simplifica o modelo P2P anterior).
- **TDMA por GPS-PPS:** o tempo é fatiado em slots; cada carro fala só no seu slot, alinhado pelo pulso PPS do GPS (relógio comum, ~30 ns de precisão entre nós). Zero colisão.
- **Alcance na mata (futuro):** se o grupo esticar além do alcance, adicionar repasse salto-a-salto. Na v1, assume-se todos no alcance uns dos outros.

**Reaproveitamento:** o `firmware/trilha_core.h` (haversine, worldToScreen, pack/parse, `world[]`/`route[]`, lógica de slot `SLOT_MS`/`curSlot`/`CYCLE_MS`, temas) **migra quase intacto** — só troca `millis()` → `esp_timer_get_time()/1000` e `radians()` → `(x*M_PI/180)`. A lógica de mapa/alerta/roster é reusada.

---

## 2. Plataforma: ESP-IDF (não Arduino)

Para o ESP32-P4 com MIPI-DSI + LVGL + WiFi via C6, o Arduino-ESP32 ainda é **beta** nos periféricos que importam. A Waveshare entrega a placa **em ESP-IDF v5.3+ com BSP + LVGL já plugado**. RadioLib roda nativo em ESP-IDF (tem HAL própria `EspHal`), então a camada de rádio não é perdida.

- **WiFi (C6 via esp_hosted) é instável** em 2026 — mas é **opcional** (só p/ OTA). Não colocar função crítica nele.
- Plano B (Arduino via fork `pioarduino`): risco alto, ganho baixo. Não recomendado.

---

## 3. Hardware / ligação do rádio no ESP32-P4

Barramentos **separados** (sem disputa): LoRa em **SPI** (header 40-pin), tela em **MIPI-DSI**, cartão SD em **SDIO**, WiFi-C6 em **SDIO**, touch em **I2C**. O SX1262 tem o SPI só pra ele.

Fios do E22-900M30S → ESP32-P4 (SPI dedicado, ex. SPI2):

| E22 (SX1262) | Função | ESP32-P4 |
|---|---|---|
| MOSI / MISO / SCK | SPI dados/clock | GPIOs do SPI2 |
| NSS | chip-select | GPIO |
| DIO1 | IRQ (TX/RX done) | GPIO **com interrupção** |
| BUSY | status (RadioLib checa antes de cada comando) | GPIO |
| NRST | reset | GPIO |
| TXEN | liga o PA (transmissão) | GPIO |
| RXEN | liga o LNA (recepção) | GPIO |
| VCC | alimentação | **5V** (não 3.3V!) |
| GND | terra | GND |
| ANT | antena | conector IPEX (antena TX915 inclusa) |

- **VCC = 5V** para dar 1W; **lógica SPI = 3.3V** (casa direto, sem level shifter).
- Pico de TX **> 600 mA** → fonte 5V com folga + **capacitor de bulk** perto do VCC (senão brownout/reset).
- PPS do GPS → um GPIO com interrupção.

---

## 4. Estrutura de tasks (FreeRTOS)

Regra dura: **o render/flush do LVGL NUNCA no mesmo core que o TDMA.**

```
CORE 1  (tempo-crítico / protocolo)          CORE 0  (UI)
  ISR_PPS (borda) → semáforo (epoch TDMA)      lvgl_task    prio 2
  lora_task   prio 7  (pin core 1)               lv_timer_handler()
    vTaskDelayUntil ancorado no epoch PPS        render + flush DSI (DMA)
    TX no meu slot / RX nos outros               ~20-30 Hz, framebuffer PSRAM
    atualiza world[] sob mutex curto           (LVGL tick via esp_timer 1ms)
  gps_task    prio 4  (UART NMEA → myLat/lon)
  app_task    prio 3  (distância/roster/alerta)
```

Prioridades: ISR_PPS > lora_task(7) > gps_task(4) > app_task(3) > lvgl_task(2).

Por que o mapa não atrapalha o rádio: (1) `lora_task` acorda por `vTaskDelayUntil` ancorado no PPS, não por polling; (2) LVGL/DSI no outro core, flush por DMA (CPU livre); (3) `world[]` compartilhado por mutex de seção mínima ou snapshot double-buffer.

---

## 5. Rádio: RadioLib + E22-900M30S (SX1262)

**Maduro e testado** (é o mesmo módulo do ecossistema Meshtastic). 3 armadilhas que derrubam todo mundo — todas resolvidas abaixo.

```cpp
#include <RadioLib.h>
// Pinos (ajustar aos GPIOs do header 40-pin do P4)
#define PIN_NSS 5
#define PIN_DIO1 26   // precisa ter interrupção
#define PIN_NRST 27
#define PIN_BUSY 25
#define PIN_RXEN 4
#define PIN_TXEN 17

SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_NRST, PIN_BUSY);

void radioSetup() {
  // begin(freq, bw, sf, cr, syncWord, power, preamble, tcxoVoltage)
  // power=22 é o MÁXIMO do SX1262; o PA do E22 adiciona ~7 dB → ~29 dBm na saída.
  // tcxoVoltage=1.8 é CRÍTICO (o E22 tem TCXO). Se begin falhar (-707), testar 1.6.
  int st = radio.begin(915.0, 125.0, 7, 5, 0x12, 22, 8, 1.8);

  // ARMADILHA 1: sem isto, o PA nunca liga → "transmite nada".
  // ORDEM: (rxEn, txEn). NÃO usar setDio2AsRfSwitch nesse módulo.
  radio.setRfSwitchPins(PIN_RXEN, PIN_TXEN);

  // ARMADILHA 2: OCP padrão (~60mA) estrangula a potência. Liberar p/ ~1W.
  radio.setCurrentLimit(140.0);

  radio.setPacketReceivedAction(onRxDone);  // callback IRAM_ATTR
  radio.startReceive();                     // padrão: escutando
}
```

**TX/RX de bytes crus** (a posição do carro), por interrupção no DIO1:

```cpp
volatile bool rxDone=false, txDone=false;
void IRAM_ATTR onRxDone(){ rxDone=true; }
void IRAM_ATTR onTxDone(){ txDone=true; }

// transmitir (no meu slot):
radio.startTransmit((uint8_t*)&pkt, sizeof(pkt));   // non-blocking
// ...quando txDone: radio.finishTransmit(); radio.startReceive();

// receber (nos outros slots):
// ...quando rxDone: radio.readData((uint8_t*)&pkt, sizeof(pkt));
//    getRSSI(), getSNR() disponíveis; depois radio.startReceive();
```

**Armadilhas (checklist):**
1. Esquecer `setRfSwitchPins(RXEN,TXEN)` → não transmite. (ordem: rxEn, txEn)
2. `setCurrentLimit(140)` → sem isso a potência trava bem abaixo de 1W.
3. TCXO errado → `begin()` falha (-707). Testar 1.8 e 1.6V.
4. `setOutputPower` máx = **22** (o PA faz o resto; setar >22 é rejeitado).
5. VCC 5V com corrente + capacitor de bulk → senão reseta no pico de TX.
6. DIO1 em pino com interrupção; callbacks com `IRAM_ATTR`.
7. Usar SPI dedicado (`Module(...,spi,settings)`) p/ não brigar com outros barramentos.

---

## 6. TDMA por GPS-PPS

**Método** (do projeto Hackaday #13013, o análogo real com 180 nós/canal):

1. O **PPS** (1 pulso/s, alinhado ao topo do segundo UTC) é o "tique" comum a todos.
2. A **ISR do PPS** só grava `t0 = esp_timer_get_time()` + conta segundos + levanta flag. Nada de rádio/serial na ISR.
3. O **NMEA/UTC** diz *qual* segundo é (o PPS só dá a borda). Relógio global = (segundo UTC) + (µs desde a borda).
4. O tempo vira **frames e slots**. O **ID do nó → offset do slot**.
5. Cada nó transmite quando `agora ≈ borda + offset_do_meu_slot`; escuta o resto.
6. **Guard time** (10–30 ms no ESP32) absorve jitter/setup/drift.
7. **Holdover:** se o fix cai, continua contando com o relógio interno (~2 ms/dia de drift) por minutos até o PPS voltar.

```cpp
const uint32_t NODE_ID = 3;         // 0..N-1, único por carro (0 = líder)
const uint32_t FRAME_SECS = 3;      // frame (dimensionar, ver §8)
const uint32_t N_SLOTS = 50;
const uint32_t SLOT_US = (FRAME_SECS*1000000UL)/N_SLOTS;
const uint32_t GUARD_US = 15000;    // 15 ms

volatile uint32_t pps_us=0, pps_count=0; volatile bool pps_flag=false;
void IRAM_ATTR ppsISR(){ pps_us=esp_timer_get_time(); pps_count++; pps_flag=true; }

// no loop do lora_task:
//  - alimenta parser NMEA; a cada PPS, casa utc_second = (ss do NMEA)+1
//  - us_in_frame = (utc_second % FRAME_SECS)*1e6 + (esp_timer_get_time()-pps_us)
//  - my_start = NODE_ID * SLOT_US
//  - se (novo frame) e (us_in_frame dentro de [my_start+guarda/2, my_start+SLOT_US-guarda/2]):
//        radio.startTransmit(pkt); startReceive();   // 1 TX por frame
```

**Não existe biblioteca pronta de TDMA-PPS** — essa camada (~40 linhas) é custom, sobre RadioLib + TinyGPS++. É o caminho de todos que fazem isso. **Não usar LoRaMesher** (é roteador multi-hop free-running, não disciplinado por GPS — brigaria com o PPS).

---

## 7. Formato do pacote de posição (16 bytes)

Little-endian, lat/lon em `int32 = grau·1e7` (~1,1 cm de resolução). Reaproveita `packUplink` do core.

| Offset | Campo | Bytes | Tipo | Nota |
|--:|---|:-:|---|---|
| 0 | room | 3 | uint24 LE | filtra sala |
| 3 | id/slot | 1 | uint8 | slot TDMA = identidade (0=líder) |
| 4 | seq | 1 | uint8 | sequência rolante (perda/reordem) |
| 5 | flags | 1 | uint8 | bit0 fix · bit1 alert · bit2 leader |
| 6 | lat | 4 | int32 LE | grau·1e7 |
| 10 | lon | 4 | int32 LE | grau·1e7 |
| 14 | heading | 1 | uint8 | graus/2 (opcional, p/ seta) |
| 15 | speed | 1 | uint8 | km/h (opcional) |

Cabe folgado no SF7/BW125 (máx 242 B). Cada nó preenche `world[slot]` ao receber; **distância = `haversine(myLat,myLon,world[k].lat,world[k].lon)` local** (já implementado). `NODE_TTL` marca quem sumiu. Roster (nomes/cores) via `CMD_ROSTER` em baixa frequência.

---

## 8. Dimensionamento (checar antes de escalar)

`slots_por_frame = floor(frame_us / (air_time + guard))` · `nós = slots_por_frame`.

- 16 B em **SF7/BW125** → air-time ~40–60 ms; com guarda ~20 ms → slot ~60–80 ms → **~12–16 slots/s**.
- 50 nós → frame de ~3–4 s (cada carro reporta a cada ~3–4 s). OK pra off-road.
- Grupos pequenos (3–5) → quase tempo real (frame < 1 s).
- Medir air-time real com `radio.getTimeOnAir(bytes)` (atenção a bug conhecido de CR — conferir com calculadora).
- SF maior = mais alcance, menos capacidade. **Atenção ao limite de potência ANATEL (1W) — nunca o M33S (2W).**

---

## 9. Fases de implementação (construir + testar nesta ordem)

- **Fase 0 — Bring-up ESP-IDF:** projeto com BSP Waveshare, "hello LVGL" no painel + toque, montar SD. *Teste: barra de cor + toque.*
- **Fase 1 — LoRa cru (2 nós):** RadioLib SX1262, ping-pong TX/RX, confirmar pinos/BUSY. *Teste: RSSI/contador entre 2 placas.*
- **Fase 2 — TDMA free-running (2→3 nós):** slots sem GPS, coords fake, remonta `world[]`. *Teste: zero colisão, roster montado.*
- **Fase 3 — Disciplina por PPS:** ISR PPS → semáforo → epoch → `vTaskDelayUntil`. *Teste: slots alinhados ao PPS.*
- **Fase 4 — GPS real (M8):** parse NMEA no `gps_task`, lat/lon reais no beacon. *Teste: posições/fix no serial.*
- **Fase 5 — Mapa LVGL:** portar `drawMap`/roster/distância (reusar haversine, worldToScreen, temas). *Teste: 2 carros andando, seta+distância.*
- **Fase 6 — Alerta:** botão LVGL → flag no pacote → caminho vermelho + borda piscando. *Teste: apertar num, acende no outro.*
- **Fase 7 — Escala/tuning:** 8→50 nós, ajustar SF/slot/guard, TTL de dropout. *Teste: ciclo estável, sem starvation.*

Fases 0 e 1 podem ser paralelas (2 placas).

---

## 10. Riscos honestos

- **Arduino-P4 imaturo** → ESP-IDF (curva de aprendizado se o time só sabe Arduino).
- **WiFi via C6 instável** → aceitável (WiFi é opcional/OTA, não crítico).
- **Banda MIPI-DSI 720×1280 + LVGL** → exige framebuffer PSRAM + partial refresh; validar cedo (Fase 0). É o risco de fluidez do mapa (plano B: tela menor/800×480).
- **Alcance real na mata** → o risco nº 1; só o teste de campo prova. Pode exigir repetidores (multi-hop).
- **PPS** → fiar o pino PPS do M8 a um GPIO com ISR; sem PPS o TDMA cai pra free-running (guard maior).
- **RadioLib é blocking** → TX curto, no core do rádio; nunca de dentro de callback longo.

---

## 11. Fontes principais

**Rádio (RadioLib + E22):**
- RadioLib Discussion #487 — E22-900M30S: TCXO 1.8V, setRfSwitchPins, setCurrentLimit(140) → 29,4 dBm
- RadioLib Wiki — High-power Modules Guide (P_radiolib = P_total − ~7 dB do PA); SPI dedicado
- S5NC/CDEBYTE_Modules — medições reais de potência do E22-900M30S
- jgromes.github.io/RadioLib — class reference SX1262 (begin/setOutputPower/setCurrentLimit)

**TDMA-PPS:**
- Hackaday #13013 — off-grid GPS race tracker (método real: 333ms slot, 180 nós/canal, jitter 7,8 µs)
- liebman/esp32-gps-ntp — handling de PPS/µs no ESP32 (ISR + disciplinamento) reaproveitável
- TS-LoRa (Zorbas) — PPS-TDMA: utilização 85-95% vs 10-15% do ALOHA

**Arquitetura P4:**
- docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-5 — BSP, LVGL, barramentos (SPI livre, SD em SDIO)
- LoRaMesher (repo MIT) — por que NÃO usar (roteador multi-hop, sem disciplina GPS)

> **Nota:** código = referência (não testado no hardware final). Números do #13013 e drift do ESP32 = medições das fontes. Decisões de arquitetura (ESP-IDF, tasks, TDMA custom, pacote 16 B) = engenharia ancorada nas fontes + no `trilha_core.h` existente.
