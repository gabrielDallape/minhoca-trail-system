# Auditoria do caminho de rádio — 2026-09-08

> Auditoria linha a linha de `radio.h`, `gps.h`, `tdma_core.h` e da RadioLib 7.6.0
> instalada, contra o datasheet SX1261/2 rev 2.2 e a implementação de referência do
> Meshtastic.
>
> **Nenhum dos quatro achados críticos é do rádio.** São de GPS, de travamento, de
> proteção que se voltou contra o sistema, e de escalonamento.

**Legenda:** ✅ corrigido na Etapa 1 · ⬜ pendente · 📄 só documentação

---

## CRÍTICO

### C1 ⬜ `gps.h:143-152` — o par PPS↔NMEA não verifica idade

O pareamento casa o segundo da GGA com **a última borda registrada**, sem checar
quanto tempo passou.

**Como quebra:** o laço bloqueia por centenas de ms (o TLS do eco, medido em
2026-08-29 e comentado em `radio.h:365`). A 9600 bps o buffer de 1024 B guarda ~1 s de
NMEA. Bordas em t=0,1,2; laço parado de 0,2 a 1,5 s; ao voltar, a GGA de `sec=0` é
parseada e casa com a borda de **t=1**. O `anchorSec` fica **1 segundo errado**.

**Por que é o pior do firmware:** com `TDMA_FRAME_S 2`, o `tdmaUsInFrame()` faz
`anchorSec % frameSecs` — 1 s de erro **inverte a fase do quadro** (4 slots de
deslocamento). E esse nó anuncia `TDMA_FL_PPS`, então pelo `tdmaBeaconAceita` **todos
os outros o seguem**. Colisão total, silenciosa, sem nada no log. O `ppsRun` não
protege: ele valida o *ritmo das bordas*, não o *rótulo*.

```c
if (cnt && cnt != g_parePpsCnt &&
    (uint64_t)esp_timer_get_time() - us < 400000ULL) { /* aceita o par */ }
// e, depois de um bloqueio longo, descartar o backlog:
if (millis() - ultimaChamada > 300) while (Serial1.available()) Serial1.read();
```

**Teste:** com PPS válido nas duas telas, provocar um bloqueio (WiFi + eco) e conferir
no log que a âncora **não** salta.

### C2 ✅ `startTransmit()` pode pendurar o firmware para sempre

`RadioLib SX126x.cpp:1143` termina em `while(digitalRead(getGpio())) yield();` — e
`getGpio()` é o **BUSY**. **Sem timeout.** Um módulo que apagou no meio do TX (o
brownout que o próprio `radio.h:355` documenta) segura o BUSY alto, e
`radioTransmiteMinhaPosicao()` nunca retorna: o laço morre ali, o vigia logo acima
nunca roda, e o aparelho fica mudo sem uma linha no log.

**Corrigido** por `radioBusyLivre()` — espera 5 ms com relógio próprio; se o BUSY não
soltar, marca TX em voo e deixa o vigia dos 400 ms reconfigurar pelo caminho existente.

### C3 ✅ O recuo automático de potência era uma catraca

`g_txPresoSeguidos` subia em **qualquer** TX não completado, inclusive quando o
`begin()` de recuperação acabara de falhar (o `else` não impedia o bloco seguinte). Um
módulo sem 5 V acumula 3 presos em 3 quadros = 6 s; recuando 4 dB por vez até o piso,
**~30 s de brownout levavam de 22 para 2 dBm gravados na NVS** — e a NVS sobrevive à
regravação do firmware.

A queda de tensão é transitória; a punição era permanente. E, enquanto armado, ele
**altera a variável que qualquer escada de potência está medindo**.

**Corrigido** por `RADIO_RECUO_AUTO 0` (`hardware.h`). Ao religar, corrigir também:
(a) mover o bloco para dentro do `if (st == RADIOLIB_ERR_NONE)`; (b) exigir quadros
consecutivos, não só contíguos no contador; (c) decidir pelo `getDeviceErrors()`.

### C4 ⬜ O predicado que protege a janela de TDMA é o errado

`g_podeBloquear = []{ return !radioTxEmVoo(); }` (`mts_p4.ino:946`). O TX ocupa 175 ms
de um quadro de 2000 ms: **em 91% das consultas a resposta é "pode bloquear"**, e a
requisição HTTPS começa livremente 50 ms antes do slot e o atravessa inteiro.

A função que resolve isso existe e **nunca foi chamada**:

```c
g_podeBloquear = []{ return !radioTxEmVoo() &&
                            tdmaUsUntilMySlot(g_tdma) > 600000UL; };
```
*(`tdmaUsUntilMySlot()` está em `tdma_core.h:550`; 600 ms > o pior bloqueio medido)*

**Teste:** `perdi` deve parar de crescer com o WiFi ativo.

---

## IMPORTANTE

### I5 ⬜ `radio.h:542` — a rajada de arranque deixa 6 s sem beacon; o TTL é 4,5 s

`souRoster = (falas>0) && ((falas<=3) || (falas%8==0))` produz **três rosters
consecutivos**. Roster não ancora ninguém — e está certo assim. São 3 quadros × 2 s =
**6 s de silêncio de âncora**, contra `ttl = frameUs*2 + 500000` = **4,5 s**.

Quem seguia esse nó entra em holdover, triplica a guarda, e `tdmaEleicao` o promove a
âncora: **a rede se parte no arranque** — o mesmo sintoma de 2026-08-29, que se
acreditava corrigido. O comentário de `tdma_core.h:450` ("o maior silêncio NORMAL entre
âncoras é 2 frames") ficou errado quando a rajada foi criada.

**Correção:** intercalar (falas 1, 3, 5) ou elevar o TTL para `frameUs*4`.

### I6 ✅ Nenhum `getDeviceErrors()` no firmware de produção

O `e22_ping` decodificava e nunca limpava; o `radio.h` não lia. **Corrigido**: `clear`
no boot e no `begin()` de recuperação, `get` no vigia antes de qualquer decisão, com os
três bits decodificados e o caso "nenhum erro = suspeita de OCP".

### I7 ⬜ `setRxBoostedGainMode` ausente

Está em `docs/MONTAGEM.md:265` e nunca foi para o código. ~2 dB de sensibilidade de
graça, num projeto cujo risco nº 1 é alcance — mais barato que os 5 dB que custaram
3,2× de air-time ao subir para SF9. Entra na Etapa 5.

### I8 📄 Retornos descartados, e `RF_OCP` no teto exato

`setCurrentLimit`, `setOutputPower`, `startReceive` e `finishTransmit` têm o retorno
ignorado. `SX126x::setCurrentLimit` **recusa qualquer valor > 140** com
`RADIOLIB_ERR_INVALID_CURRENT_LIMIT` — em silêncio, deixando o OCP em 60 mA, que é a
armadilha dos 19,6 dBm. `RF_OCP 140.0f` está *exatamente* no teto: quem tentar 150
perde 3 dB sem aviso. O datasheet permite **157,5 mA** (0x3F), mas só escrevendo o
registrador `0x08E7`.

### I9 ⬜ `SetTx` sem timeout nativo

`launchMode()` chama `setTx(RADIOLIB_SX126X_TX_TIMEOUT_NONE)`. O datasheet oferece
timeout no `SetTx` justamente como cão-de-guarda — sem ele, um TX que não conclui deixa
o PA de 1 W chaveado e o TXEN alto até o vigia agir (400 ms + o `begin()`). Não dá para
passar por `startTransmit()`; a alternativa é `stageMode` + `setTx(timeout)` manual.

### I10 ⬜ `radioTrocaPotencia` ignora o estado do chip

Faz `standby()` sem checar `g_loraTxEmVoo`. Chamada pela tela de diagnóstico no meio de
um TX, **aborta a transmissão** e deixa `g_loraTxEmVoo` verdadeiro → 400 ms depois o
vigia conta um "preso" e (com o recuo armado) podia desfazer sozinho a potência que o
usuário acabara de escolher. A tela zera `g_txPreso` mas **não** `g_txPresoSeguidos`.

### I11 📄 A janela útil de TX é de 55 ms — 15 ms em holdover

Slot 250 ms, guarda 20 ms, air-time 175 ms → `[início+10 ms, início+65 ms]`. Em
holdover a guarda triplica e sobram **15 ms**. `radioAtualiza()` roda uma vez por
iteração do laço; qualquer repintura acima disso faz `perdi` subir sem explicação.

### I12 ✅ `tdma_core.h:56` — erro de 60× no holdover

Dizia "3,05 min"; são **3,05 horas**. Com a guarda real de 20 ms, o holdover seguro é
**~33 minutos**, não segundos. Isso inverte a conclusão que estava escrita. **Corrigido**
no comentário, com a referência ao RFC 7554.

### I13 ⬜ `tdma_core.h:396` contradiz `gps.h`

O contrato do header diz que o segundo a casar com a borda é "(ss do último NMEA) + 1";
`gps.h:147` casa `ss` com a borda **anterior** — que é o correto. Um porte que siga o
comentário desloca `anchorSec` em 1 s: metade do quadro.

---

## MENOR

| # | Onde | O quê |
|---|---|---|
| M14 ⬜ | `radio.h:384` | `g_loraIrqUs` (64 bits) escrito pelo laço fora de `noInterrupts()`, concorrendo com a ISR |
| M15 ⬜ | `begin()` | `setDio2AsRfSwitch(true)` fica ligado (`SX126x.cpp:58`, incondicional), contra a regra do `MONTAGEM.md`. Inofensivo com o DIO2 livre, mas o Meshtastic desliga explicitamente |
| M16 ⬜ | `radio.h:170` | `setRfSwitchPins` só é chamado **depois** do `begin()`; durante os ~100 ms dele o RXEN flutua (só o TXEN tem pull-down de 10 k) |
| M17 ⬜ | `radio.h:448` | se `getPacketLength()` devolver 0, `readData(buf, 0)` significa "leia tudo" para o RadioLib |
| M18 ⬜ | `radio.h:542` | `static uint32_t falas` não é reiniciado por `radioTrocaSlot`/`radioTrocaSala` — até 16 s sem anunciar o roster novo |
| M19 ⬜ | `radio.h:168` | `g_loraFiosOk` é guardado mas não bloqueia nada; com BUSY/DIO1 trocados o TX nunca completa |
| M20 📄 | `radio.h:599` | `radioPotenciaAnt()` devolve `g_rfPwr + 7`; o ganho real do módulo é **+10 dB** |

---

## Ordem sugerida

C2, C3 e I6 estão feitos (Etapa 1 do `PROTOCOLO_FASE0.md`) porque são o que torna a
medição possível. **C1 e C4 são de rede, não de rádio** — entram na Etapa 6, depois que
o rádio tiver um número, para não misturar as duas investigações.

I7 e M20 entram na Etapa 5. O resto é dívida conhecida, sem urgência.
