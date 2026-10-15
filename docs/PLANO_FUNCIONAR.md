# Plano — fazer o MTS funcionar

> Escrito em **2026-09-08**, depois do `ESTUDO_E22.md`. Premissa dada: **qualquer
> hardware**. Nada é sagrado — nem o E22, nem o P4, nem a tela de 7".
>
> O objetivo não é "consertar o rádio". É chegar a **dois carros se seguindo numa
> trilha de verdade**, e só depois crescer. Tudo aqui está ordenado por isso.

---

## O diagnóstico que ordena o plano

O projeto tem muita engenharia boa e pronta: `tdma_core.h` com testes verdes, o mapa
vetorial do cartão, a bancada web que achou sete bugs, OTA pela internet, telemetria
por eco. E tem **um buraco no meio**:

> **Nenhum subsistema do MTS foi jamais medido sozinho.** Toda medição de rádio foi
> feita com tela, painel RGB, WiFi, GPS e cartão ligados ao mesmo tempo. Dez
> variáveis, um sintoma.

Foi assim que "era o cabo USB" virou nota de rodapé, que uma escada de potência
medida sem corrente virou o "risco nº 1 do projeto", e que um módulo provavelmente
bom foi condenado por uma flag que o datasheet diz ser normal no boot.

E há um segundo buraco, mais caro:

> **Nunca houve um teste de campo bem-sucedido.** O único dia de trilha (28/08) rodou
> com dois nós mudos e 13 dBm, e as correções que explicam aquilo nunca voltaram ao
> mato. **O orçamento de link — o risco nº 1 declarado do projeto — nunca foi
> medido.** Não sabemos a que distância dois carros se enxergam numa trilha.

O plano inteiro decorre de uma regra:

> ### Nada entra no sistema antes de ter um número próprio.

---

## Fase 0 — A bancada de rádio isolada

**Objetivo:** saber, com número, se o E22 que temos funciona.
**Custo:** ~R$ 0 em MCU · **Prazo:** 1 semana · **Bloqueia:** tudo.

> **Usa as telas P4 que já existem — não compre devkit.** Uma versão anterior deste
> plano pedia dois ESP32-S3-DevKitC, e estava errada por duas razões. Primeiro, o que
> precisa ser isolado é a **energia**, não o processador. Segundo, **o P4 é o hardware
> final**: medir num devkit responderia "o E22 funciona num devkit", e o principal
> suspeito — o 5 V vindo do header CAN/RS485 — só existe no P4. Trocar de placa
> trocaria o objeto do teste.

Duas telas P4, dois E22, uma fonte de 5 V separada. O `e22_ping.ino` **já tem os dois
caminhos de pinagem** (`PIN_LORA_*` do `hardware.h` do `mts_p4`, ou pinos fixos de
devkit) e o `build.ps1` já compila para `p4` — roda hoje, sem portar nada.

O isolamento se faz por alimentação e por chave de compilação:

- `MTS_TEM_WIFI 0` e `MTS_TEM_SD 0` — tira o TLS do eco e o cartão da equação
- o `e22_ping` não inicializa o painel, então o RGB e o backlight saem sozinhos
- **o E22 sai do header CAN e vai para uma fonte de 5 V separada, com terra comum** —
  é a condição exata de 2026-08-24, em que a tela B passou a 22 dBm com `preso=0`

Mais cinco correções no sketch, vindas do estudo:

| Correção | Por quê |
|---|---|
| `ClearDeviceErrors` no boot | o `XOSC_START_ERR` é **esperado** no POR (datasheet 13.3.6); sem limpar, toda leitura posterior mente |
| `getDeviceErrors()` após cada TX | separa `XOSC_START_ERR` / `PLL_LOCK_ERR` / **`PA_RAMP_ERR`** — o chip diz qual dos três falhou |
| OCP em **157,5 mA** (era 140) | é o máximo do registrador (63 × 2,5 mA); 12% de margem de graça |
| `setPaRampTime(800U)` | rampa 4× mais lenta reduz o pico de corrente |
| `setDio2AsRfSwitch(false)` | o `begin()` deixa em `true` e o `setRfSwitchPins()` não desfaz |

**O que medir**, com multímetro no pad 9/10 do módulo durante o TX:

| Medida | Instrumento | Serve para |
|---|---|---|
| VBAT durante TX, por degrau | multímetro DC | o datasheet exige **3,3 V para +22 dBm** |
| corrente em TX, por degrau | multímetro em série | a Semtech diz que ela **sobe quando a tensão cai** |
| qual bit de erro dispara | serial | XOSC, PLL ou rampa do PA |
| em que degrau colapsa | escada 6/8/14/22 | comparar com a escada de 28/08 |

### Tabela de decisão

| Resultado | Conclusão | Próximo passo |
|---|---|---|
| 22 dBm passa, `preso=0` | o E22 é bom; o problema era o sistema em volta | segue com E22 → Fase 1 |
| Colapsa, mas VBAT cai abaixo de 3,3 V | alimentação, confirmado | buck dedicado → Fase 1 |
| Colapsa com VBAT firme e `PA_RAMP_ERR` | é a rampa/PA | testar `setOutputPower(p,false)` e rampas mais longas |
| Colapsa com VBAT firme e `XOSC_START_ERR` **depois** do clear | módulo realmente ruim | trocar módulo |
| Um módulo passa e outro não | o defeito é de unidade | usar os bons, descartar o ruim |

**Refazer o teste do módulo "defeituoso" da tela A é obrigatório aqui** — com
`ClearDeviceErrors` antes. Pode devolver um rádio de 1 W à frota.

---

## Fase 1 — O nó mínimo, e o primeiro campo de verdade

**Objetivo:** dois carros se vendo numa trilha real. **Sem mapa.**
**Custo:** ~R$ 200 (buck, cabos, conectores) · **Prazo:** 2 semanas.

Esta é a fase que o projeto nunca fez, e é a que decide se o produto existe.

**O nó:** a tela P4 que já existe + rádio + GPS com PPS + **buck 12 V→5 V dedicado**,
direto da fiação do carro, com o banco de capacitores do Ebytastic (220 µF + 4,7 µF +
1 µF + 0,1 µF) nos pads do módulo. Fusível e TVS de 26 V na entrada. **USB fora da
cadeia de potência.**

**Sem mapa, não sem tela.** O `telaDiag()` já existe e mostra exatamente o que este
teste precisa: RSSI, SNR, `tx`/`perdi`/`preso`, sincronia e GPS. No mato não há
laptop, e foi por isso que ele foi escrito. O mapa e o cartão ficam desligados
(`MTS_TEM_SD 0`) para não disputar o laço nem a PSRAM.

### O que este campo tem de produzir

Um CSV, não uma impressão. Para cada pacote: distância entre os carros (dos dois
GPS), RSSI, SNR, e se chegou.

| Número | Por que importa |
|---|---|
| **distância máxima com 90% de entrega** em mata | é o orçamento de link, o risco nº 1, nunca medido |
| o mesmo em estrada de terra aberta | separa vegetação de geometria |
| `preso`, `perdi`, `conflito` ao longo de horas | valida o TDMA em movimento |
| deriva do PPS e tempo até o primeiro fix | valida a âncora de tempo |

**Critério de saída:** dois nós trocando posição de forma estável a **pelo menos
300 m em mata fechada**. Abaixo disso, o produto não funciona e a discussão vira
antena, SF e potência — não mapa, não tela, não roster.

---

## Fase 2 — Reintegrar a tela

**Objetivo:** o mapa de volta, sem perder o rádio.
**Prazo:** 2 semanas.

Só agora o rádio volta para o P4. E agora existe uma **referência**: se o alcance
cair ao entrar na tela, o culpado é a integração, e você tem o número de antes para
provar.

Correções de software que entram aqui, todas do estudo:

- as cinco da Fase 0, no `radio.h` de produção
- **`setRxBoostedGainMode(true)`** — ~2 dB de RX, está no `MONTAGEM.md` e nunca foi
  para o código
- **timeout nativo do `SetTx()`** no lugar do vigia de 400 ms com `millis()` — o chip
  tem watchdog próprio, e o nosso já custou dois bugs
- **desarmar o recuo automático de potência** até a alimentação estar resolvida: hoje
  ele rebaixa 4 dB e grava na NVS sem nunca subir de volta
- `getDeviceErrors()` no vigia, viajando no eco

**Critério de saída:** o alcance medido na Fase 1 se repete com a tela ligada,
pintando mapa, com WiFi ativo.

---

## Fase 3 — Endurecer para o carro

**Objetivo:** parar de depender de solda manual.
**Custo:** ~R$ 450 os três · **Prazo:** 3–4 semanas (fabricação).

Só aqui a PCB se justifica — quando já se sabe **o que** ela precisa carregar.

- placa com o módulo em pads castelados, buck, banco de capacitores, pull-down do
  TXEN, **um conector travado** no lugar de 16 fios
- copper pour extenso, planos de terra fundidos (nota do MeshAdv v2)
- antena única e padronizada, **SMA**, vertical, externa — as nossas medidas mostram
  **12 dB** entre a melhor e a pior montagem, e há aparelhos em campo com montagens
  diferentes
- alívio de tração e trava em tudo

---

## Fase 4 — Escala para 30–50 carros

**Objetivo:** a rede grande. **Prazo:** modelar antes de comprar.

Toda esta fase se prova na `web/bancada-trilha/` **sem hardware**, com 50 nós — foi
assim que os quatro bugs do `tdma_core.h` apareceram.

- **SF7, frame de 6 s** (frame precisa dividir 18): 76 slots, cabem 50 carros com
  folga. SF9 custaria 9 s de atualização.
- **O líder ganha 3 slots** espalhados no frame → atualiza a 1 Hz, custa 2 slots dos
  76.
- **Slot derivado do relógio GPS**, como o [race
  tracker](https://hackaday.io/project/13013-off-grid-gps-race-tracker-client-and-server/log/44413-microsecond-resolution-tdma-using-gps-pps-on-arduino)
  faz (3 slots de 333 ms → 180 nós/min, jitter de 7,8 µs medido). Sem negociação, sem
  estado compartilhado.
- **Reuso espacial**: 50 carros ocupam 5–15 km de trilha e o alcance é de centenas de
  metros — cada carro ouve 10–15 vizinhos, nunca os 50.
- **Três classes de tráfego**: posição (1 salto, segundos), rota do líder (todos,
  **minutos** — é histórica), alerta (todos, imediato, raro).

---

## As decisões de hardware, e quando tomá-las

O plano **não escolhe hardware agora**. Cada decisão tem um gatilho:

| Decisão | Quando | Gatilho |
|---|---|---|
| Manter o E22-900M30S | fim da Fase 0 | 22 dBm passa com fonte decente |
| Trocar por **E22-900M22S** | fim da Fase 0 | o M30S não sustenta, ou o custo de domá-lo é alto. 3,3 V, ~110 mA, +8,75 dB sobre o que a frota irradia hoje |
| Trocar por **Waveshare Pico-LoRa-SX1262-915M** ou **LoRa HAT** | fim da Fase 0 | quer pinagem pronta sem PCB: 22 dBm, 3,3 V, header 2,54 mm |
| Rádio-satélite com MCU próprio | fim da Fase 2 | o rádio degrada ao entrar na tela e não dá para separar de outro jeito |
| PCB própria | Fase 3 | o conceito está provado em campo |

**O que o estudo já indica**, sem decidir: os dois projetos que resolveram o mesmo
produto que o MTS — [Spoke](https://github.com/FeruzTopalov/spoke) e o race tracker —
**não usam módulo de 1 W**. O Spoke, que é o caso mais próximo, usa o E22-900M22S.

---

## O que este plano deliberadamente adia

Para não repetir o erro de construir sobre o que não foi provado:

- **roster, waypoints, pin de perigo, ritmo do grupo, ETA** — todo o
  `PLANO_MELHORIAS.md` fica parado até a Fase 1 fechar. Nenhuma feature nova enquanto
  dois carros não se veem no mato.
- **SOTDMA e reuso espacial** — Fase 4, e só em simulação até haver 10 nós reais.
- **PCB** — Fase 3. Desenhar placa agora é industrializar um problema que ninguém
  isolou.

---

## Resumo

| Fase | Entrega | Custo | Critério de saída |
|---|---|---|---|
| **0** | rádio medido sozinho, nas telas que já existem | R$ 0 | sei qual degrau falha e por quê |
| **1** | dois nós em trilha, sem mapa | R$ 200 | ≥ 300 m em mata, entrega ≥ 90% |
| **2** | mapa de volta | — | o alcance da Fase 1 se repete com tela |
| **3** | PCB, antena, alimentação veicular | R$ 450 | uma trilha inteira sem intervenção |
| **4** | 30–50 carros | — | 50 nós estáveis na bancada web |

**A Fase 0 pode tornar as Fases 1–3 muito mais baratas — ou revelar que o hardware
precisa mudar antes de qualquer investimento.** Ela **não custa hardware nenhum** — duas telas que já existem, dois E22 que já
existem e uma fonte de 5 V separada — e é a única coisa que hoje separa o projeto de
saber o que está acontecendo.
