# Protocolo da Fase 0 — fazer o rádio funcionar, com método

> Escrito em **2026-09-08**. Executa a Fase 0 do `PLANO_7B.md`.
> **Custo: R$ 0.** Usa as três telas P4, os E22 já soldados e uma fonte de 5 V.

---

## O princípio

O projeto já perdeu duas semanas por medir dez variáveis ao mesmo tempo. A escada de
28/08 mediu, simultaneamente: potência do chip, configuração de PA, alimentação por
USB, painel ligado, WiFi ativo e um recuo automático que **mudava a potência durante a
própria medição**. O resultado foi uma conclusão errada que virou doutrina.

Este protocolo tem três regras:

1. **Uma variável por vez.** Cada etapa muda uma coisa e mede.
2. **Instrumentar antes de corrigir.** Só se conserta o que se consegue ver.
3. **Cada mudança é uma hipótese com resultado previsto.** Se o resultado não vier,
   a hipótese cai — não se acrescenta uma segunda mudança por cima.

E uma regra de ouro que decorre delas:

> **Nenhuma correção "de melhoria" entra antes de a medição estar limpa.**
> `setRxBoostedGainMode`, `RF_PWR 14` e os bugs de TDMA são ganhos reais, mas entram
> **depois**, porque cada um deles muda uma variável do experimento.

---

## Etapa 1 — Enxergar (e parar de sabotar a própria medição)

**Nada aqui melhora o rádio.** Estas três mudanças existem para que as etapas seguintes
signifiquem alguma coisa.

### 1.1. Ler o que o chip diz

O SX1262 tem três bits que dizem **por que** um TX falhou. O `e22_ping` sabe
decodificá-los; o firmware de produção não lê nenhum.

```c
// radio.h, logo apos cada begin() bem-sucedido:
g_lora.clearDeviceErrors();   // XOSC_START_ERR e ESPERADO no POR (datasheet 13.3.6).
                              // Sem limpar, ele fica latcheado e contamina toda
                              // leitura futura - foi o que condenou o modulo da tela A.

// e no bloco 'venceuOTempo', ANTES de qualquer decisao:
uint16_t err = 0;
g_lora.getDeviceErrors(&err);
Serial.printf("radio: TX preso - erros=0x%04X%s%s%s\n", err,
              (err & (1<<5)) ? " XOSC(TCXO)"  : "",
              (err & (1<<6)) ? " PLL"          : "",
              (err & (1<<8)) ? " RAMPA_DO_PA"  : "");
```

| Bit | Significa |
|---|---|
| 5 `XOSC_START_ERR` | o TCXO não partiu |
| 6 `PLL_LOCK_ERR` | o PLL não prendeu |
| 8 `PA_RAMP_ERR` | **a rampa do PA falhou** |

Nenhum bit aceso + TX que não completa = **o OCP cortou** (o chip não reporta isso).

### 1.2. Impedir que o firmware trave no meio do teste

`RadioLib SX126x.cpp:1143` faz `while(digitalRead(BUSY)) yield();` **sem timeout**.
Módulo em brownout com BUSY alto = `startTransmit()` nunca retorna, e o vigia logo
acima nunca roda. O aparelho morre em silêncio no meio da escada.

```c
// radio.h, antes de CADA startTransmit:
uint32_t t0 = micros();
while (digitalRead(PIN_LORA_BUSY) && micros() - t0 < 5000) { }
if (digitalRead(PIN_LORA_BUSY)) {
  g_txPreso++;
  Serial.println("radio: BUSY preso alto 5 ms - NAO transmiti, vou reconfigurar");
  // cai direto na recuperacao, sem chamar startTransmit
  return;
}
```

### 1.3. Desarmar o recuo automático de potência

**Esta é a mais importante da etapa.** Hoje, três TX travados seguidos fazem o firmware
baixar 4 dB e **gravar na NVS**. Numa escada de potência isso significa que o
experimento **altera a variável que está medindo**, e o valor sobrevive à regravação.

Foi assim que as telas foram a campo com 6 dBm sem ninguém saber.

```c
// hardware.h
#define RADIO_RECUO_AUTO  0    // 1 = religa depois que a alimentacao estiver resolvida
```

E envolver o bloco de recuo em `#if RADIO_RECUO_AUTO`. Quando religar, corrigir também
os dois defeitos que a auditoria achou: ele conta preso mesmo quando o `begin()`
falhou, e não exige que os quadros sejam consecutivos.

**Antes de medir qualquer coisa**, zerar a NVS de potência nas três telas:
```
p22   (pelo serial, em cada tela)
```
e confirmar no boot que as três dizem o mesmo `pwr=`.

---

## Etapa 2 — Tornar a escada interpretável

### 2.1. O problema

A `paOptTable` do RadioLib 7.6.0 **não é monotônica**. O número que você pede não é o
que é programado:

| você pede | paDutyCycle | hpMax | vai no `SetTxParams` |
|---|---|---|---|
| +4 dBm | 1 | 1 | **+22** |
| +5 dBm | 2 | 2 | +11 |
| **+6 dBm** | **3** | **1** | **+21** |
| +7 dBm | 1 | 2 | +17 |

**"6 dBm" não é um ponto de baixa corrente — é `paVal = +21` com duty cycle 3.** A
escada de 28/08 comparou quatro configurações arbitrárias de amplificador, não uma
rampa de potência. Ela não pode ser interpretada e não deve ser citada de novo.

### 2.2. A correção

```c
g_lora.setOutputPower(g_rfPwr, false);   // false = paDutyCycle 0x04, hpMax 0x07,
                                          // paVal = o dBm pedido. Eixo monotonico.
```

É o que o Meshtastic faz em toda placa com PA externo
(`SX126X_NO_POWER_OPTIMIZATION_TABLE`, commit `80f8611`): *"a tabela é calibrada para
um SX126x nu"*.

> Isto vale também para `radioTrocaPotencia()`, senão o comando `p` do serial volta a
> usar a tabela e a escada muda de régua no meio.

---

## Etapa 3 — A medição de referência

**Só agora se mede.** Duas telas, dois E22, e a variável que interessa isolada.

### Montagem

| Item | Como |
|---|---|
| Alimentação do E22 | **fonte de 5 V separada**, ≥1 A, terra comum com a tela num único ponto |
| Alimentação da tela | USB-C, carregador de 3 A (não porta de PC) |
| Antenas | montadas, **≥30 cm entre as duas telas**, mesma antena nas duas |
| Chaves | `MTS_TEM_WIFI 0`, `MTS_TEM_SD 0` |
| Multímetro | pontas nos **pads 9/10 do módulo**, medindo durante o TX |

### O que registrar, por degrau

Um CSV, não uma impressão de tela:

```
dBm_pedido, dBm_antena_esperado, VBAT_repouso, VBAT_em_TX, tx, preso, erros, rssi_par
```

`dBm_antena_esperado = dBm_pedido + 10` (ganho medido do módulo, manual E22-M V1.2).

### A escada

**2, 6, 10, 14, 18, 22** — seis degraus, 110 s cada, na ordem crescente e depois
**descendo de volta**. A volta importa: se o degrau que falhou na subida passar na
descida, há histerese, e histerese é sintoma térmico ou de dano.

### Tabela de decisão

| Resultado | Conclusão | Vá para |
|---|---|---|
| 22 passa, `preso=0`, VBAT ≥ 4,8 V | o E22 é bom; o problema era o sistema | Etapa 5 |
| Falha em algum degrau, **VBAT cai abaixo de 4,8 V** | alimentação confirmada | Etapa 5, e a Fase 1 do plano vira prioridade |
| Falha com **VBAT firme** e `PA_RAMP_ERR` | é a rampa do PA | Etapa 4.2 |
| Falha com **VBAT firme** e nenhum bit aceso | **o OCP cortou** | Etapa 4.1 |
| Falha com `XOSC_START_ERR` **depois** do clear | TCXO de verdade | Etapa 4.3 |
| Um módulo passa e o outro não | defeito de unidade | trocar só aquele |

---

## Etapa 4 — As hipóteses, uma de cada vez

**Só execute a que a Etapa 3 apontou.** Cada uma é uma linha, e cada uma tem um
resultado previsto. Se o previsto não acontecer, a hipótese cai e você volta à tabela.

### 4.1. Hipótese "regulador" — a mais barata de todas

O modo DC-DC do SX1262 exige um **indutor de 15 µH entre os pinos 7 e 9**, e o RadioLib
usa DC-DC por padrão (`useRegulatorLDO = false`). Se o layout do E22 for LDO-only, o
conversor chaveia contra nada exatamente sob o pico do PA.

```c
g_lora.setRegulatorLDO();   // antes do primeiro TX
```

**Previsto:** se for isso, o degrau some ou muda de lugar. É a única hipótese que
explica um colapso **em degrau** por caminho puramente elétrico. Dois minutos.

### 4.2. Hipótese "rampa"

```c
// DEPOIS do setOutputPower - ele reseta a rampa para 200 us (SX126x_config.cpp:730)
g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);
```

**Precedente direto:** a variante **T-Beam 1W** do Meshtastic define exatamente isso —
*"requires >800us stabilization (default is 200us)"*. Rampa 4× mais lenta = pico de
corrente menor.

**Previsto:** se for isso, `PA_RAMP_ERR` some e os degraus altos passam.

Se 800 µs melhorar mas não resolver, tente 1700 e 3400 µs — **um de cada vez**.

### 4.3. Hipótese "OCP"

O OCP está em 140 mA e o chip consome 118 mA a +22 dBm — **19% de margem**. Com a
tensão caindo, a corrente sobe (é inversamente proporcional a VBAT) e cruza o limite.
O datasheet permite até **157,5 mA**, mas `setCurrentLimit` **recusa acima de 140 em
silêncio**:

```c
// escrever direto no registrador
uint8_t ocp = 0x3F;   // 63 x 2,5 mA = 157,5 mA
g_lora.writeRegister(RADIOLIB_SX126X_REG_OCP_CONFIGURATION, &ocp, 1);
```
*(requer `RADIOLIB_GODMODE=1` ou o método público equivalente)*

**Previsto:** ganha-se 12% de margem. Se resolver, a causa era o OCP — e isso é um
sintoma de tensão baixa, não a doença.

### 4.4. Se for TCXO de verdade

`XOSC_START_ERR` **depois** de um `clearDeviceErrors`, ao armar TX, é o defeito real.
Antes de condenar o módulo, verifique o óbvio que o Meshtastic documentou: **resíduo de
fluxo de solda** na região do módulo, e a tensão do TCXO (`RF_TCXO 2.2f`, correta pelo
manual V1.2 — a revisão existe justamente para acrescentar esse número).

**Nunca** use `radio.XTAL = true`: o E22 tem TCXO no DIO3 e não tem cristal nos pinos
XTA/XTB.

---

## Etapa 5 — Colher os ganhos

**Só depois que a Etapa 3 der um resultado limpo.** Cada item aqui é ganho garantido,
mas entram juntos porque nenhum é hipótese — são correções conhecidas.

```c
// radio.h, na sequencia correta:
g_lora.begin(RF_FREQ, RF_BW, RF_SF, RF_CR, RF_SYNC, g_rfPwr, RF_PRE, RF_TCXO);
g_lora.clearDeviceErrors();                          // etapa 1.1
g_lora.setDio2AsRfSwitch(false);                     // begin() liga incondicionalmente
g_lora.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
int st = g_lora.setCurrentLimit(140.0);              // CHECAR o retorno: >140 e recusado
if (st != RADIOLIB_ERR_NONE) Serial.println("radio: *** OCP NAO aplicado ***");
g_lora.setOutputPower(g_rfPwr, false);               // etapa 2.2
g_lora.setPaRampTime(RADIOLIB_SX126X_PA_RAMP_800U);  // DEPOIS do setOutputPower
g_lora.setRxBoostedGainMode(true);                   // ~2 dB de RX, esta no MONTAGEM.md
g_lora.startReceive();
```

E:
```c
#define RF_PWR  14     // 14 no chip = 24 dBm na antena (+10 dB do modulo).
                       // +8 dB sobre os ~16 dBm de hoje = ~1,8x de alcance em mata.
```

---

## Etapa 6 — Os bugs de tempo

**Separados de propósito.** Não afetam o rádio; afetam a *rede*. Fazer junto com o
rádio confundiria as duas coisas — que é exatamente o erro que este protocolo existe
para evitar.

### 6.1. O par PPS↔NMEA sem verificação de idade (`gps.h:143`)

O mais grave do firmware inteiro. O laço bloqueia centenas de ms; a 9600 bps o buffer
guarda ~1 s de NMEA; a frase de `sec=0` casa com a borda de `t=1`; `anchorSec` fica
**1 segundo errado**; com frame de 2 s isso **inverte a fase do quadro**; e o nó
anuncia `TDMA_FL_PPS`, então **todos os outros o seguem**. Colisão total, silenciosa.

```c
if (cnt && cnt != g_parePpsCnt &&
    (uint64_t)esp_timer_get_time() - us < 400000ULL) { /* aceita o par */ }
// e depois de um bloqueio longo, descartar o backlog:
if (millis() - ultimaChamada > 300) while (Serial1.available()) Serial1.read();
```

**Teste:** com PPS válido nas duas telas, provocar um bloqueio (ligar o WiFi e forçar
um eco) e confirmar no log que a âncora **não** salta.

### 6.2. A janela de TDMA desprotegida

```c
g_podeBloquear = []{ return !radioTxEmVoo() &&
                            tdmaUsUntilMySlot(g_tdma) > 600000UL; };
```
`tdmaUsUntilMySlot()` existe em `tdma_core.h:550` e **nunca foi chamada**.

**Teste:** `perdi` deve parar de crescer com o WiFi ativo.

### 6.3. A rajada de rosters no arranque

Três rosters seguidos = **6 s sem âncora**, contra um TTL de 4,5 s. É o bug de 29/08,
ainda vivo. Intercalar (falas 1, 3, 5) ou elevar o TTL para `frameUs*4`.

**Teste:** ligar as três telas juntas e confirmar **uma** âncora, `fonte=` apontando
para o mesmo slot nas três.

---

## Ordem final, em uma tela

| # | Etapa | Muda | Como sei que funcionou |
|---|---|---|---|
| 1 | Enxergar | erros do chip, timeout do BUSY, recuo desarmado | o log diz *por que* falhou; a potência não muda sozinha |
| 2 | Régua | `setOutputPower(p,false)` | a escada vira monotônica |
| 3 | **Medir** | nada — só mede | CSV com VBAT, preso e erros por degrau |
| 4 | Hipótese | **uma** linha, conforme a tabela | o degrau some ou a hipótese cai |
| 5 | Ganhos | init correta + `RF_PWR 14` | 24 dBm na antena, estável |
| 6 | Rede | PPS, janela, rosters | uma âncora só, `perdi` parado |

**Depois disso**, e só depois, a Fase 1 do `PLANO_7B.md`: fonte veicular, cabo 18 AWG,
capacitores no módulo e antenas padronizadas.

---

## Como reverter

Tudo em um commit por etapa, com o nome da etapa na mensagem. Se uma etapa piorar as
coisas, `git revert` daquele commit — não desfazer manualmente, porque metade destas
mudanças é sutil e a outra metade está espalhada.

Antes de começar: `git add -A && git commit` do estado atual, que tem semanas de
trabalho não commitado.
