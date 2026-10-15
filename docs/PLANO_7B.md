# Plano 7B — o MTS na tela de 7 polegadas

> Escrito em **2026-09-08**, consolidando 15 frentes de pesquisa (datasheets Semtech e
> Ebyte lidos na íntegra, esquemático oficial da Waveshare, código do RadioLib e do
> Meshtastic, oito projetos abertos com E22, papers de propagação, norma ANATEL, e
> uma auditoria linha a linha do nosso firmware).
>
> **Alvo:** a Waveshare ESP32-P4-WIFI6-Touch-LCD-7B com E22-900M30S e GPS/PPS.
> **Objetivo:** dois carros se seguindo numa trilha, e o caminho até 30–50.

---

## Sumário — as sete coisas que mudaram

1. **O ganho do PA é +10 dB, não +7,25.** A frota a 6 dBm irradia **~16 dBm**, não
   13,25. Tabela oficial do manual E22-M V1.2.
2. **1 W é ilegal no Brasil em BW125.** O Ato ANATEL 14.448/2017 limita a densidade
   espectral a 8 dBm/3 kHz → teto de **24,2 dBm em BW125**.
3. **O header CAN/RS485 tem capacitância local zero** (um 100 nF). O degrau de 620 mA
   afunda o trilho antes de qualquer regulador reagir.
4. **A escada de potência de 28/08 não é interpretável** — a `paOptTable` do RadioLib
   não é monotônica: "6 dBm" programa `paVal = +21`.
5. **Quatro bugs críticos no nosso firmware**, e nenhum é do rádio.
6. **O alcance realista é 500 m entre carros**, não quilômetros.
7. **O maior ganho de alcance do projeto é multi-hop pelo comboio** — custo zero em
   hardware, ×8 em cobertura.

---

## Parte 1 — O diagnóstico, fechado

### 1.1. A cadeia causal completa

```
isqueiro (cortado na partida, contato por mola, vibra)
  → adaptador USB (entrega bem menos que o rótulo)
    → cabo USB 28 AWG  ······ 639 mV de queda a 1,5 A
      → USB-C da placa
        → ORing de dois AO3401 → FET do botão (DMP2066LSN)
          → Core_5V ··········· capacitância local: 100 nF. Só.
            → 15 cm de chicote
              → E22 pedindo 620 mA em degrau
```

Medido no fio: com só o rádio (0,62 A) chegam **4,71 V**; com a tela junto (~1,5 A),
**4,3 V**. O manual E22-M V1.2 exige **≥5 V para potência plena**, e o datasheet do
SX1262 exige **VBAT ≥ 3,3 V no chip** para +22 dBm.

### 1.2. Por que o colapso tem forma de degrau

Quatro mecanismos que se somam, todos documentados:

| Mecanismo | Fonte |
|---|---|
| A corrente é **inversamente proporcional a VBAT** (25,5 mA a 3,3 V → 48 mA a 1,8 V) | datasheet §5.1 |
| O **PA é alimentado direto do VBAT**, sem filtro — o DC-DC serve só o core | datasheet §5.1.3 |
| O **OCP corta** e aborta o TX sem gerar TX_DONE | datasheet tab. 5-2 |
| A **`paOptTable` não é monotônica** — corrente não escala com o dBm pedido | `SX1262.cpp:9-42` |

### 1.3. O que NÃO é

- **Não é módulo defeituoso.** O `XOSC_START_ERR` é **esperado no POR** e deve ser
  limpo com `ClearDeviceErrors` (datasheet §13.3.6) — e o `e22_ping` nunca limpa. Em
  runtime, o Meshtastic provou por reprodução que **−707 significa brownout, não
  TCXO** (PR #11678).
- **Não é a marca.** Não há relato de lote ruim ou falsificação de Ebyte em lugar
  nenhum. Todas as queixas do mundo são de documentação e configuração.
- **Não é o ESP32-P4.** A errata não tem nada de SPI, GPIO, IRQ ou timer; o
  `esp_timer` é 1 µs; a pinagem evita os bancos perigosos; e o painel MIPI-DSI é
  melhor que o RGB do S3.

---

## Parte 2 — Software (custo zero, faça primeiro)

### 2.1. Os quatro críticos da auditoria

**C1 — `gps.h:143-152`: o par PPS↔NMEA não verifica idade.**
O laço bloqueia centenas de ms (o TLS do eco); a 9600 bps o buffer guarda ~1 s de
NMEA. A GGA de `sec=0` casa com a borda de `t=1` e o `anchorSec` fica **1 segundo
errado** — com frame de 2 s, isso **inverte a fase do quadro**. E o nó anuncia
`TDMA_FL_PPS`, então **todos os outros o seguem**. Colisão total, silenciosa.

```c
// recusar o par quando a borda for velha
if (cnt && cnt != g_parePpsCnt &&
    (uint64_t)esp_timer_get_time() - us < 400000ULL) { ... }
// e descartar o backlog de NMEA depois de um bloqueio:
if (millis() - ultimaChamada > 300) while (Serial1.available()) Serial1.read();
```

**C2 — `startTransmit()` pode pendurar o firmware para sempre.**
`RadioLib SX126x.cpp:1143` faz `while(digitalRead(BUSY)) yield();` **sem timeout**.
Se o módulo cair em brownout com BUSY alto, `radioTransmiteMinhaPosicao()` nunca
retorna — e o vigia de TX que está logo acima nunca roda.

```c
// antes de cada startTransmit:
uint32_t t0 = micros();
while (digitalRead(PIN_LORA_BUSY) && micros() - t0 < 5000) { }
if (digitalRead(PIN_LORA_BUSY)) { g_txPreso++; /* recupera, nao chama TX */ }
```

**C3 — o recuo de potência é uma catraca.**
`g_txPresoSeguidos` sobe mesmo quando o `begin()` acabou de falhar. Três presos em
6 s, 4 dB por vez até o piso: **30 s de brownout levam de 22 para 2 dBm gravados na
NVS**, e "não sobe de volta de propósito". A falha é transitória; a punição é
permanente. Mover o bloco para dentro do `if (st == RADIOLIB_ERR_NONE)`, exigir
quadros consecutivos, e **ler `getDeviceErrors()` antes de decidir**.

**C4 — o predicado que protege a janela de TDMA é o errado.**
`g_podeBloquear = []{ return !radioTxEmVoo(); }` responde "pode" em **91 %** das
consultas — o TX ocupa 175 ms de 2000. A função certa existe e nunca é chamada:

```c
g_podeBloquear = []{ return !radioTxEmVoo() &&
                            tdmaUsUntilMySlot(g_tdma) > 600000UL; };
```

### 2.2. A sequência de inicialização correta

Validada contra o Meshtastic (milhares de nós com o mesmo módulo) e o datasheet:

```c
g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
g_lora.clearDeviceErrors();          // XOSC_START_ERR e ESPERADO no POR (§13.3.6)
g_lora.setDio2AsRfSwitch(false);     // begin() liga incondicionalmente
g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
g_lora.setCurrentLimit(140.0);       // begin() deixou 60; >140 e RECUSADO em silencio
g_lora.setOutputPower(g_rfPwr, false);   // false = PA fixo; a tabela e p/ SX126x nu
g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);  // DEPOIS: setOutputPower reseta
g_lora.setRxBoostedGainMode(true);   // ~2 dB de RX, no MONTAGEM.md e ausente do codigo
g_lora.startReceive();
```

Três coisas que custaram pesquisa para descobrir:

- **`setOutputPower()` reseta a rampa** para 200 µs (hardcoded em
  `SX126x_config.cpp:730`). O `setPaRampTime` tem de vir **depois**.
- **`optimize=false`** é o que o Meshtastic usa em placas de PA externo
  (`SX126X_NO_POWER_OPTIMIZATION_TABLE`, commit `80f8611`): *"a tabela é calibrada
  para um SX126x nu"*.
- **800 µs de rampa** é o valor que o Meshtastic usa na variante **T-Beam 1W**:
  *"requires >800us stabilization (default is 200us)"*.

### 2.3. O teste de uma linha que vale fazer antes de tudo

O modo DC-DC do SX1262 exige um **indutor de 15 µH entre os pinos 7 e 9**, e o
RadioLib usa DC-DC por padrão. Se o layout do E22 for LDO-only, o conversor chaveia
contra nada justamente sob o pico do PA.

```c
g_lora.setRegulatorLDO();   // antes do primeiro TX
```

É a única hipótese levantada que explica um colapso **em degrau** por caminho puramente
elétrico. Dois minutos.

### 2.4. Correções de documentação

| Onde | O que está errado |
|---|---|
| `CLAUDE.md` | "setCurrentLimit DEPOIS de setOutputPower" — a RadioLib preserva o OCP desde 2019. A regra é **depois do `begin()`** |
| `hardware.h` | "+7,25 dB do PA" → **+10 dB** (tabela oficial E22-M V1.2) |
| `MONTAGEM.md` | "15 pads" → o módulo tem **22 pads** |
| `tdma_core.h:56` | holdover "3,05 min" → **3,05 horas**; com guarda de 20 ms, **~33 min** seguros |
| `tdma_core.h:396` | contradiz `gps.h` sobre qual segundo casa com a borda |

---

## Parte 3 — Alimentação

### 3.1. Por que o `Core_5V` não serve

Do esquemático oficial: o pino 1 dos headers CAN/RS485 é ligação **direta** ao trilho.
**Sem fusível, sem polyfuse, sem diodo, sem ferrite.** O único componente no nó é
`C88 = 100 nF`. E o buck do 3,3 V (MP1658) e o boost do backlight penduram no mesmo nó.

A placa já consome **0,8–1,2 A** ali com o backlight no máximo.

> **Nunca injetar 5 V no `Core_5V`.** O diodo de corpo do Q8 (o FET do botão liga)
> conduz de volta: a fonte realimenta o `VCC_5V`, **ignora o botão de desligar**,
> energiza a entrada do carregador de bateria e, com o USB plugado, briga com o VBUS
> do host. Sem proteção nenhuma nesse caminho.
>
> **E nunca no `J4`/`BAT`** — é entrada de célula 1S, 3,0–4,2 V.

### 3.2. O circuito correto

Fonte **própria** para o rádio, terra comum num único ponto:

```
12 V ─[fusível ATO 3 A]─[TVS 5KP26A]─[P-FET reverso]─[ferrite CM]
     ─[100 µF 50 V low-ESR ∥ 100 nF]─[buck TPS54560]─ 5,x V
                                           │
                                        [LDO 5 V]      ← nota de aplicação AN2023005
                                           │
                                    [220 µF + 10 µF + 100 nF]  a <10 mm dos pads 9/10
                                           │
                                          E22
```

**Por que TPS54560 e não o buck do MeshAdv:** o ambiente automotivo tem *load dump*
de **35 V (Test B) a 101 V (Test A)** e *cold crank* que derruba para **6 V**.

| Conversor | Vin máx | Veredito |
|---|---|---|
| MP1584 / Mini560 | 28 V / ~20 V | **morrem no load dump** |
| XL4015 | 38–40 V | sobrevive Test B raspando |
| **TPS54560** | 60 V (65 abs) | ✅ a TI declara conformidade ISO 7637 |

**Por que LDO depois do buck:** a nota de aplicação **AN2023005** da Ebyte é
explícita — *"a frequência de chaveamento do DC-DC afeta o sinal de RF; recomenda-se
conectar um LDO em série após a saída do DC-DC"*.

**Por que 220 µF + 10 µF + 100 nF:** é o par que aparece em **todos** os seis
esquemáticos abertos que funcionam, sempre nos próprios pinos do módulo. O
`xiao-ble-pcb` põe como regra: *"capacitor de no mínimo 100 µF entre 5 V e GND **para
atingir os 30 dBm plenos**"*. Nenhum manual da Ebyte dá valor — os 470 µF do nosso
`MONTAGEM.md` são folclore.

### 3.3. O cabo — a correção mais barata do projeto

| AWG | queda ida+volta, 1 m @ 1,5 A |
|---|---|
| **28** (cabo USB comum) | **639 mV** |
| 20 | 100 mV |
| **18** | **63 mV** |

Trocar o cabo de alimentação de 28 para **18 AWG, par torcido** devolve ~600 mV
sozinho. Conector **Deutsch DT** (o JST não vibra bem em off-road).

### 3.4. Aterramento

Retorno do rádio **dedicado**, mesma bitola do positivo, encontrando o terra da tela
em **um único ponto estrela**. O `MONTAGEM.md:172` já registra o motivo e está certo:
620 mA pulsados no mesmo terra do GPS entram como salto de referência no PPS — que é
a régua de tempo do TDMA.

---

## Parte 4 — Antena

Suas medidas (`medidas_antena.csv`) mostram **12 dB entre a melhor e a pior montagem**.
É o maior ganho isolado disponível, e nem é caro.

**Diagnóstico:** "antena no IPX direto" é chicote sem plano de terra — um 1/4 de onda
sem contrapeso irradia contra o próprio cabo. E há uma pista de defeito mecânico: na
linha `5m TX915 nas duas` a mediana é −63 mas o **p10 é −76**. A 5 m isso não é
propagação; é **contato intermitente no IPEX**.

| Item | Escolha | ~R$ |
|---|---|---|
| Antena | base magnética 5 dBi SMA, **no teto**, cabo cortado para **1,5 m** (3 m de RG174 comem 2 dB) | 60–120 |
| Reserva | manter a TX915-JKD-20 (3,35 dBi medidos) | 25–40 |
| Rabicho | **um único** U.FL→SMA, ≤15 cm, **colado e amarrado**; daí para fora só SMA | 15 |
| Aferição | NanoVNA, aceitar **SWR < 2,0** em 915 | ~300 (uma vez) |

Com PA de 1 W sem isolador, **SWR entre 3:1 e 6:1 pode danificar o estágio final** —
é a razão física do "nunca energize sem antena" do `CLAUDE.md`. E muita antena vendida
como 915 é 868 relabelada, com SWR de 3,6.

---

## Parte 5 — Quanta potência usar (e a lei)

### 5.1. O teto legal

**Ato ANATEL 14.448/2017**, item 10.3.3: densidade espectral máxima de **8 dBm em
qualquer faixa de 3 kHz**. O teto conduzido vira `8 + 10·log₁₀(BW/3 kHz)`:

| BW | Teto legal |
|---|---|
| **125 kHz** (nosso) | **24,2 dBm** |
| 250 kHz | 27,2 dBm |
| 500 kHz | 30,2 dBm |

**Os 30 dBm do E22 são ilegais em BW125.** 1 W só é legal em BW500.

### 5.2. A recomendação

Com o ganho real de **+10 dB** do módulo:

```
#define RF_PWR  14      // 14 dBm no chip = 24 dBm na antena
```

- **Legal** (24 < 24,2 dBm em BW125)
- **+8 dB sobre o que a frota irradia hoje** (~16 dBm) → **×1,8 de alcance** em mata
- Longe do pico de corrente máximo, que é onde o brownout mora
- **Sem trocar hardware nenhum**

Subir para 20 dBm no chip (30 na antena) só faria sentido em BW500 — e a conta
mostra que não vale: em BW500 a sensibilidade piora tanto quanto o PA melhora, e o
orçamento de link **empata em 159 dB** com 22 dBm em BW125.

### 5.3. Se um dia trocar o módulo

O **E22-900M22S é pino-a-pino** com o M30S: mesmos 22 pads, mesmos RXEN/TXEN. A troca
é dessoldar, soldar, mudar 5 V para 3,3 V e regravar — **zero redesenho de chicote**.
Ele entrega 22 dBm com **119 mA** em vez de 620, some o rail de 5 V, o capacitor de
bulk e o brownout inteiro.

---

## Parte 6 — Alcance real, e o que ele significa para o produto

### 6.1. A expectativa, ancorada em medição

| Cenário | Alcance |
|---|---|
| Mata fechada com curva ou morro | **150–400 m** |
| Trilha típica (mata + relevo ondulado) | **500–900 m** |
| Trecho aberto, crista, visada parcial | 1,5–4 km |

Âncora principal: campanha na **Amazônia a 915 MHz, SF9, BW125, 20 dBm** — quase a
nossa configuração — com alcance confiável de **945 m** e expoente medido n = 2,78.
Uma segunda campanha a 923 MHz mediu **25 dB de perda excedente** só da vegetação, e
recomenda somar isso ao link budget.

**Planeje o produto para 500 m entre carros consecutivos.**

### 6.2. Duas armadilhas que a pesquisa desfez

**SF12 não é melhor.** Medições em vegetação tropical acharam **SF8 e SF9 com o maior
alcance**, e SF12 chegando a apenas 300 m — o ganho de sensibilidade é comido pela
sensibilidade a desvio de frequência. **Nosso SF9 está certo.**

**Doppler é irrelevante.** A 30 km/h o desvio é 25 Hz; o LoRa tolera BW/4 = 31,25 kHz.
Testes com carro a 110 km/h deram PDR ~100 %.

E a geometria manda: o raio da 1ª zona de Fresnel a 915 MHz é **6,4 m a 500 m**. Com
antenas a 2 m no teto, **o próprio chão obstrui a partir de ~150 m** — o MTS nunca
opera em visada limpa. Um morro custa 15–25 dB que **nenhuma potência compensa** (20 dB
seriam 100× a potência).

### 6.3. O maior ganho do projeto inteiro

> **Multi-hop pelo comboio. Custo zero em hardware.**
>
> Oito carros a 500 m cobrem **4 km** sem um único dB a mais. É geometria vencendo
> potência, e a nossa arquitetura TDMA já viabiliza — cada carro repete o rastro.

Comparação de retorno por real:

| Mudança | ×alcance |
|---|---|
| **Multi-hop pelo comboio** | **×8 em cobertura, R$ 0** |
| Antena e altura corretas | ×2,4 |
| 16 → 24 dBm (só mudar `RF_PWR`) | ×1,8 |
| 24 → 30 dBm (PA no limite, ilegal em BW125) | ×1,5 |

O PA externo é **o pior retorno por real do conjunto**.

---

## Parte 7 — Escala para 30–50

Capacidade com SF7, 16 B: air-time 51,5 ms + guarda 20 ms = **71,5 ms por nó**.

| Frame | 50 nós cabem? | Taxa |
|---|---|---|
| 3 s | não (41 nós) | — |
| **4 s** | **sim** | 0,25 Hz |
| 6 s | folgado | 0,167 Hz |

**O líder ganha 3 slots** espalhados no quadro → atualiza a 1 Hz, custa 2 slots de 56.

O que falta no `tdma_core.h`, em ordem, tudo modelável na bancada web antes de existir
hardware:

1. **Slot anunciado no pacote** (SOTDMA do AIS): 2 bytes com `próximo_slot` +
   `timeout`, sorteio entre candidatos livres. Hoje slot é configuração fixa, e
   duplicata é indetectável.
2. **Detecção de colisão de slot** — `tdmaSlotAt()` já existe.
3. **Um quadro de escuta obrigatório antes do primeiro TX** (o AIS obriga; custa 4 s e
   elimina uma classe inteira de falha).
4. **Reuso espacial por distância** — já transmitimos lat/lon, a métrica é de graça.
5. **Soltar a restrição de `frameSecs` dividir 18** — é redundante, já obrigamos UTC,
   e ela proíbe justamente o frame de 4 s.

**Corrigir também:** a rajada de três rosters no arranque deixa **6 s sem âncora**
contra um TTL de 4,5 s — é o bug de 29/08, ainda vivo. Intercalar (falas 1, 3, 5) ou
elevar o TTL.

E o que já fazemos **melhor** que Spoke e que o race tracker, e deve ser preservado:
fonte de tempo atrás de interface com degradação graciosa, eleição por espera crescente
sem estado compartilhado, e compensação de air-time na âncora.

---

## Parte 8 — Fases

| Fase | Entrega | Custo | Critério de saída |
|---|---|---|---|
| **0** | software corrigido + escada interpretável | **R$ 0** | 24 dBm estável nas 3 telas, `preso=0` |
| **1** | alimentação e antena | ~R$ 400 | ≥ 500 m em trilha, entrega ≥ 90 % |
| **2** | campo com mapa | — | uma trilha inteira sem intervenção |
| **3** | multi-hop pelo comboio | R$ 0 | 4 carros, o último vê a rota do líder |
| **4** | PCB e caixa | ~R$ 450 | frota replicável |
| **5** | 30–50 nós | — | 50 nós estáveis na bancada web |

### Fase 0, em detalhe (é o que dá para fazer hoje)

1. Aplicar C1–C4 e a sequência de init da Parte 2.
2. `setRegulatorLDO()` — o teste de uma linha.
3. `RF_PWR 14` (= 24 dBm na antena, legal).
4. Repetir a escada **com `setOutputPower(p,false)`**, alimentando o E22 por fonte
   separada de 5 V/1 A com terra comum. Registrar `getDeviceErrors()` em cada falha.
5. Refazer o teste do módulo "defeituoso" da tela A **com `ClearDeviceErrors` antes** —
   pode devolver um rádio à frota.

---

## Parte 9 — Lista de compras

**Agora (~R$ 400):**

| Item | Qtd | ~R$ |
|---|---|---|
| Buck **TPS54560** (módulo 60 V / 5 A) | 3 | 45 |
| TVS **5KP26A** + fusível ATO 3 A + porta-fusível | 3 | 20 |
| P-MOSFET IRF9540N (polaridade reversa) | 3 | 5 |
| Eletrolítico 220 µF/16 V + cerâmicos 10 µF / 1 µF / 100 nF | kit | 30 |
| Cabo **18 AWG** par torcido + conector Deutsch DT | 3 | 30 |
| Antena 915 MHz 5 dBi SMA com base magnética | 3 | 90 |
| NanoVNA (uma vez) | 1 | 300 |

**Não comprar agora:** rádio nenhum. O E22 a 24 dBm é legal, é +8 dB sobre hoje, e a
Fase 0 dirá se ele se sustenta. O M22S fica como plano B, e é pino-a-pino.

---

## Fontes

**Documentos primários:** SX1261/2 Data Sheet Rev 2.2 (Semtech, dez/2024) — tabelas
3-6, 4-4, 5-2, 13-21, 13-41, seções 5.1, 13.1.14, 13.3.6, 13.4.4, e a **seção 15
(Known Limitations)** · AN1200.37 e AN1200.40 (Semtech) · **E22-M Series User Manual
V1.2** (Ebyte, 2026-02-06) e **AN2023005** · [Esquemático oficial da
7B](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-7B/ESP32-P4-WIFI6-Touch-LCD-7B.pdf)
· [Ato ANATEL 14.448/2017](https://informacoes.anatel.gov.br/legislacao/atos-de-certificacao-de-produtos/2017/1139-ato-14448)
· ISO 16750-2 · ITU-R M.1371 (SOTDMA) · ITU-R P.833-9

**Código:** RadioLib 7.6.0 instalado localmente · [Meshtastic
`SX126xInterface.cpp`](https://github.com/meshtastic/firmware/blob/master/src/mesh/SX126xInterface.cpp)
e commit `80f8611` · PR #11678 (recuperação de brownout)

**Medições de terceiros:** [S5NC — engenharia reversa e potência do
E22](https://github.com/S5NC/CDEBYTE_Modules) · [RadioLib
#487](https://github.com/jgromes/RadioLib/discussions/487) · [Amazônia 915 MHz
SF9](https://pmc.ncbi.nlm.nih.gov/articles/PMC10935028/) · [floresta 923 MHz, 25 dB de
excesso](https://pmc.ncbi.nlm.nih.gov/articles/PMC13210814/) · [LoRa em
movimento](https://pmc.ncbi.nlm.nih.gov/articles/PMC10975671/)

**Projetos:** ver `docs/ESTUDO_E22.md`, seções 2 e 5.
