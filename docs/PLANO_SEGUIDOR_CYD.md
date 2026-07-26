# Projeto "Siga o Líder" por LoRa — Plano Mestre (Líder MKR Zero + Seguidor CYD)

> Documento de planejamento. Objetivo: ter tudo pensado antes de executar por etapas.
> Última revisão: 2026-07-02.

---

## 1. Visão geral (o que a gente quer)

Dois aparelhos que se enxergam pelo rádio (LoRa), estilo "siga o líder" / Waze:

- **LÍDER** (vai na frente): lê o **próprio GPS** e **transmite** a coordenada por LoRa.
- **SEGUIDOR** (a base, com tela): recebe a coordenada do líder e mostra um **mapa estilo Waze**:
  - **EU no centro** da tela (posição do meu próprio GPS);
  - **rastro (breadcrumb)** do caminho que o líder fez;
  - **seta + distância** apontando pro líder;
  - mapa **gira com o meu rumo** (heading-up), como no Waze.

Sem internet, sem celular — dois aparelhos autônomos conversando direto por rádio.

---

## 2. Arquitetura (definição atual)

| Papel | Placa | GPS | Rádio LoRa | Tela |
|---|---|---|---|---|
| **LÍDER** | **Arduino MKR Zero** | MKR GPS Shield (Serial1, 13/14) | LoRaMESH **slave (ID 1)** | — |
| **SEGUIDOR** | **CYD (ESP32-2432S028R)** | módulo próprio (a definir) | LoRaMESH **master (ID 0)** | 2.8" 320×240 (integrada) |

- O **GIGA R1** sai do papel de tela (o display dele quebrou — contato do conector). A placa continua boa e pode ser reserva/nó extra.
- Direção dos dados: **líder → seguidor** = **slave → master** (uplink), que é a direção natural e robusta do mesh.

---

## 3. O que JÁ está pronto e provado ✅

- **Link LoRa** placa↔módulo funcionando (UART de comando, 9600 8N1).
- **Identidade dos módulos** (par de fábrica, senha 123):
  - Módulo **A = slave, ID 1**, UniqueID 13683.
  - Módulo **B = master, ID 0**, UniqueID 13680.
- **Link RF entre os dois módulos**: 100% de resposta (teste de ping).
- **Contador líder→seguidor**: 0% de perda a 1 Hz.
- **Pipeline GPS→LoRa→seguidor**: funcionando (falta só fix de céu aberto).
- **MKR Zero com 2ª UART** criada via SERCOM3 (D6=TX / D7=RX) pro LoRa — lê o módulo (ID 1) OK.
- Descoberta-chave: dá pra mandar **dados de aplicação pela UART de comando** (comando `0x11`, < 0x80),
  então **não precisa** da UART transparente nem de refiação.

### Formato do pacote de dados (comando 0x11, 10 bytes)
```
[0] flags      (bit0 = fix válido)
[1] satélites
[2..5] latitude   (int32 little-endian = graus × 1e7)
[6..9] longitude  (int32 little-endian = graus × 1e7)
```

---

## 4. Hardware e fiação

### 4.1. LÍDER — MKR Zero (não muda, já funciona)

- **GPS**: MKR GPS Shield empilhado → **Serial1** (pinos 13/14), NMEA 9600.
- **LoRa (slave, ID 1)** na 2ª UART via SERCOM3:
  ```
  Uart Serial2(&sercom3, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2);
  void SERCOM3_Handler(){ Serial2.IrqHandler(); }
  pinPeripheral(6, PIO_SERCOM_ALT); pinPeripheral(7, PIO_SERCOM_ALT);
  ```
  | Módulo LoRa | MKR Zero |
  |---|---|
  | VCC (4) | VCC (3,3V) |
  | GND (1) | GND |
  | RX_1 (2) | **D6** |
  | TX_1 (3) | **D7** |

### 4.2. SEGUIDOR — CYD (ESP32-2432S028R)

Pinos livres do CYD (confirmado): **IO35** (entrada-only), **IO22**, **IO27**, + **3.3V** (só no CN1) e **GND**.
Conectores: **CN1** = `GND·IO22·IO27·3.3V` · **P3** = `GND·IO35·IO22·IO21(backlight)` · **P1** = serial USB (não usa) · 2 pinos = alto-falante (não usa).

**Módulo LoRa (master, ID 0):**
| Módulo LoRa | CYD | Conector |
|---|---|---|
| VCC (4) | 3.3V | CN1 |
| GND (1) | GND | P3 |
| RX_1 (2) | **IO22** (ESP32 TX) | CN1 |
| TX_1 (3) | **IO35** (ESP32 RX) | P3 |

**GPS próprio do seguidor:**
| GPS | CYD | Conector |
|---|---|---|
| VCC | 3.3V | CN1 |
| GND | GND | CN1 |
| TX | **IO27** (ESP32 RX) | CN1 |
| RX | não liga | — |

**UARTs no ESP32** (qualquer UART aponta pra qualquer pino):
```
Serial1.begin(9600, SERIAL_8N1, /*RX=*/35, /*TX=*/22);  // LoRa
Serial2.begin(9600, SERIAL_8N1, /*RX=*/27, /*TX=*/-1);  // GPS (só leitura)
```
- Os 3 pinos (35/22/27) **não são strapping pins** → seguros no boot.
- **3.3V é um pino só** (CN1): alimenta os dois módulos (divide num "Y"). GND idem.

### 4.3. Energia
Sem WiFi (não usamos), consumo ~200 mA, pico ~300 mA no TX do LoRa. Regulador do CYD (AMS1117 ~800 mA–1 A) aguenta com folga.

---

## 5. Design da tela (Seguidor / CYD)

Biblioteca: **LovyanGFX** (reconhece o CYD; desenha direto, **sem** a thread de refresh que bugava o GIGA; suporta **sprite/offscreen** → sem flicker).

Layout (mapa + painel lateral fino):
- **EU** = triângulo verde fixo no **centro**, apontando pra cima (minha direção no heading-up).
- **Mapa heading-up**: gira conforme meu `course` do GPS (quando em movimento); parado, mantém o último rumo. (Flag pra alternar norte-fixo.)
- **Rastro do líder** (breadcrumb): linha/pontos em ciano, guardados como lat/lon num ring buffer, convertidos pra tela relativo a mim a cada quadro.
- **Líder**: círculo laranja na posição relativa; se sair da tela, **seta na borda** apontando pra ele.
- **Linha + distância + rumo** de mim até o líder.
- **Painel**: LINK LoRa, EU (fix), LÍDER (fix), DIST até o líder, RUMO→líder, minha velocidade, satélites (eu/líder), nº de pacotes.
- **Touch**: botão RESET (limpa rastro) e/ou **zoom** (+/-).
- Técnica anti-flicker: desenhar o mapa num **sprite** e dar `pushSprite` de uma vez.

Decisões visuais em aberto: orientação (paisagem 320×240 vs retrato 240×320), zoom padrão (m/pixel), tamanho de fontes.

---

## 6. Protocolo LoRa (referência)

- **Comando 0x11** (aplicação, < 0x80): sai na **UART de comando** do destino. É como mandamos os dados.
- `PrepareFrameCommand(destId, 0x11, payload, n)` + `SendPacket()` no líder;
  `ReceivePacketCommand(&id,&cmd,payload,&len,timeout)` no seguidor (id = origem = 1).
- **Limites**: payload **≤ 232 bytes/pacote** (+5 de cabeçalho/CRC). CRC automático (pacote corrompido é descartado).
- **Taxa**: 1 Hz hoje (ideal 1–2 Hz pra seguir).
- **IDs**: até 2047; ID 2047 = broadcast (só master).
- **Confiabilidade**: manual pede ACK a cada msg de aplicação (integridade do mesh). Hoje sem ACK e 0% de perda a 1 Hz/curto alcance. Se lá longe perder, adicionar ACK (líder reenvia se não confirmar).
- **Alcance/ajuste**: 915 MHz, +20 dBm, sens. -137 dBm. `config_bps` troca SF/BW (SF alto = mais alcance, mais lento).

---

## 7. Plano de execução por ETAPAS

> Regra: cada etapa é testável sozinha antes de seguir. Nada de juntar tudo de uma vez.

**Etapa 0 — Preparo físico**
- Definir como ligar nos conectores JST do CYD (cabinho JST 1.25 mm ou solda).
- Identificar o módulo GPS do seguidor (modelo + baud).
- Confirmar cabo USB de dados do CYD.

**Etapa 1 — Tela do CYD (HELLO)**  ✅ *sketch pronto: `cyd_hello/`*
- Gravar HELLO, validar display (cores, texto). Ajustar config se cor/inversão sair errada (ILI9341 × ST7789).

**Etapa 2 — CYD recebe o LoRa**
- Ligar o módulo master (ID 0) no CYD (IO35/IO22).
- Sketch: receber comando 0x11 do líder e **mostrar na tela** (fix, sats, lat, lon, nº pacotes) em texto.
- Meta: ver `LINK OK` e os dados do líder chegando na telinha.

**Etapa 3 — GPS próprio do seguidor**
- Ligar o GPS no CYD (IO27).
- Ler NMEA (TinyGPSPlus), mostrar minha posição/sats/velocidade na tela.

**Etapa 4 — Mapa Waze completo**
- Juntar tudo: eu no centro, rastro do líder, seta+distância, painel, heading-up, sprite anti-flicker, touch (reset/zoom).

**Etapa 5 — Teste de rua**
- As duas unidades ao ar livre (cada uma pega seu fix), em power bank. Validar o mapa ao vivo.

**Etapa 6 — Polimento**
- Ajuste de zoom/rotação, ACK se precisar de alcance, caixa/suporte, autonomia (power bank), possível log no cartão SD do CYD.

---

## 8. Decisões / perguntas em aberto

1. **Conexão física** nos JST do CYD: cabinho JST 1.25 mm, solda, ou headers?
2. **Qual GPS** vai no seguidor (modelo, baud)?
3. **Orientação** da tela: paisagem (320×240) ou retrato (240×320)?
4. **Zoom padrão** do mapa (ex.: quantos metros de raio na tela)?
5. Precisa **guardar histórico** no cartão SD do CYD? (ele tem leitor.)

---

## 9. Riscos e mitigação

| Risco | Mitigação |
|---|---|
| Variante do painel do CYD (ILI9341 × ST7789) → cor/inversão errada | Ajustar `invert`/`rgb_order` na config LovyanGFX (teste rápido). |
| Fiação frágil nos JST | Usar cabinho JST bom ou soldar com alívio de tensão. |
| Alcance curto na rua | Subir SF via `config_bps`; adicionar ACK/retransmissão. |
| Brownout no 3.3V | Não usar WiFi; se preciso, alimentar módulos por 5V+regulador próprio. |
| GPS demora/instável | Filtro de fix (≥4 sats), ignorar saltos absurdos (já feito na Parte 1). |

---

## 10. Referências

- **Pinos livres do CYD**: https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md
- **Pinout CYD (Random Nerd)**: https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/
- **Pinout CYD alta-res (Mischianti)**: https://mischianti.org/esp32-2432s028-cheap-yellow-display-high-resolution-pinout-datasheet-schema-and-specs/
- **Manual do módulo LoRaMESH (Radioenge)**: https://www.radioenge.com.br/wp-content/uploads/2021/08/manual-modulo-loramesh-abr2021.pdf
- **Biblioteca LoRaMESH (fork elcereza)**: https://github.com/Radioenge/LoRaMESH

### Ambiente de build (esta máquina)
- `arduino-cli` da IDE: `C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`
- Cores: `arduino:mbed_giga` (GIGA), `arduino:samd` (MKR Zero), `esp32:esp32` 3.3.10 (CYD).
- Libs: TinyGPSPlus, LoRaMESH, LovyanGFX, Arduino_GigaDisplay_GFX/Touch.
- FQBNs: `arduino:samd:mkrzero` · `esp32:esp32:esp32` · `arduino:mbed_giga:giga`.

### Sketches no projeto (Desktop/odometro)
- `cyd_hello/` — HELLO do CYD (Etapa 1). ✅ compila
- `lora_lead_gps/` — líder (MKR) envia GPS por LoRa. ✅
- `lora_follow_gps/` — recebe GPS (base do seguidor). ✅
- `lora_lead_counter/`, `lora_follow_counter/` — teste de contador. ✅
- `lora_id/`, `lora_id_mkr/`, `lora_ping/`, `lora_scan/` — diagnósticos LoRa. ✅
- `odometro.ino` — odômetro GPS Parte 1 (GIGA).
