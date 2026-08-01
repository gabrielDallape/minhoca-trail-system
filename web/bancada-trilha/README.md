# Bancada TRILHA — simulador das telas embarcadas

**No ar: https://bancada-trilha.vercel.app** (projeto Vercel `bancada-trilha`,
produção servida deste diretório; `vercel deploy --prod --yes` republica).

Página estática que **reexecuta a lógica de `firmware/grupo_ws/grupo_ws.ino`** em
canvas de 1024×600 — **quatro telas**, uma criando o grupo e três entrando pelo
código de 5 dígitos, trocando pacotes por um rádio simulado com custo de UART,
air-time e colisão. Serve para ver o comportamento do produto sem depender do
hardware — e para mexer em layout/UX antes de gravar firmware.

```powershell
start web\bancada-trilha\index.html        # abre local, nao precisa de servidor
node web\bancada-trilha\selftest.js        # 60 verificacoes: grupo, P2P, TDMA e os achados
node web\bancada-trilha\audit.js           # 62 checagens da bancada contra o firmware real
```

Três modos selecionáveis:

| Modo | Corresponde a | O que faz |
|---|---|---|
| **GRUPO** | `#define P2P 0` | código de 5 dígitos, `JOIN` → alocação de slot → `ROSTER`, `WORLD` em broadcast a cada 3,6 s, `UPLINK` a cada 1,2 s |
| **P2P** | `#define P2P 1` (gravado hoje) | par fixo sem código, `TRAIL` com catch-up por ack |
| **TDMA** | `tdma_core.h` + `tdma_test.ino` (Fase 4, nunca executado) | rede plana de 1 salto, pacote de 16 B, SX1262 cru sem ACK, slot por nó, âncora por beacon ou PPS |

No modo TDMA cada nó tem **relógio próprio** — offset de partida e drift de cristal de ±40 ppm.
Sem isso um TDMA parece perfeito de graça, e não é isso que se quer descobrir. Há nós fantasma
(sem tela) para medir capacidade com 8, 16 ou 32 slots.

## O que o modo TDMA encontrou antes do hardware

O mais grave, e mede-se com `node selftest.js`:

**`tdmaOnBeacon` cala metade da rede.** `t.frameBase += (rxUs - t.anchorUs) / t.frameUs` é divisão
inteira. Num nó com cristal mais lento que a âncora, o intervalo entre beacons medido localmente fica
abaixo de `frameUs` (1 s a −40 ppm = 999,96 ms), o resultado é 0, o `frameBase` congela e a guarda de
"1 TX por frame" bloqueia **para sempre** — exatamente o sintoma que esse campo existe para evitar.
Medido em 60 s com cristais forçados:

```
0 / -30 / -20 / -10 ppm  ->  tx = 60 /  2 /  2 /  2
0 / +30 / +20 / +10 ppm  ->  tx = 60 / 60 / 60 / 60
0 / -40 / +25 / -15 ppm  ->  tx = 60 /  1 / 60 /  1
```

**Com dois nós isso passa**, porque depende do sinal do erro relativo. A correção é uma linha —
arredondar em vez de truncar, com mínimo 1 — e o `tx` volta a 60/60/60/60 mesmo com 30 % de beacon
perdido. O botão "corrigir o frameBase" liga isso na bancada.

Os outros: a âncora do beacon chega no **fim** do pacote e come a folga inteira do slot
(`slotErrado` em quase todo pacote no beacon, zero com PPS); somar PPS sem apagar a linha do beacon
mata o TX; o nó 0 vive em **holdover permanente** (ancora uma vez no `setup()` e nunca mais);
`MAXN = 8` limita a tela mesmo com 16 ou 32 slots na rede; cabem **15 nós** num frame de 1 s
(air-time de 16 B em SF7/BW125 = 51,5 ms), então 25–50 nós exigem frame de 2–3 s; e acima de
~48 km/h com frame de 3 s o trajeto do líder visto pelos seguidores **vira tracejado**, porque o
pacote de 16 B não carrega histórico e cada nó só recebe uma posição por frame.

Conclusão prática: **o PPS não é "fase 3", é pré-requisito.** Com beacon há três defeitos; com PPS,
`slotErrado = 0`, `rxCRC = 0` e todos os nós transmitindo.

## Bug do firmware ou artefato da bancada?

A pergunta que decide se a bancada serve para algo. Ela tem resposta mecânica:
o botão **Rádio ideal** entrega tudo instantaneamente, sem colisão e sem perda.
**O que continua errado com o rádio ideal está no firmware; o que desaparece era
o modelo de rede.** O `selftest.js` roda os achados nos dois modos.

> **Correção de uma versão anterior:** o modelo tratava o rádio como um SX1262 cru
> (colisão = perda definitiva) e a página afirmava que o líder "perde carros
> inteiros" com 4 nós. **Errado.** O que está nas telas é um Radioenge LoRaMESH —
> módulo de rede com ACK e retransmissão próprios atrás da UART (é por causa desse
> firmware fechado que a Fase 4 migra para SX1262). Com o ACK no modelo, o líder
> **vê os quatro carros em todos os cenários**, com idade de dado de ~1,1 s. O
> sintoma do descompasso é tráfego e latência, não carro perdido.

**Bugs de lógica** (sobrevivem ao rádio ideal — são do código):

- **Carro zumbi.** Nada zera `world[].active`, e o `parseRx` do seguidor faz
  `if(active) lastMs = millis()` — refresca o relógio local a cada `WORLD`, então o
  `NODE_TTL` de 15 s nunca dispara lá. 41 s depois de um carro sair do ar, o líder
  já o dá por morto e os seguidores o mostram com idade de 2 s, congelado.
- **Alerta imprevisível.** `drawMap` olha só o *primeiro* peer ativo: o alerta de um
  carro de slot alto não acende em ninguém, e passa a acender se um slot menor
  expirar.
- **`slotDue` calculado e ignorado.** `parseRx` computa
  `slotDue = worldRxMs + curSlot*SLOT_MS` e liga `pendingUplink`; o `loop()` não usa
  nenhum dos dois. Ligando "usar o slot", a colisão vai a **zero** e o ar cai de
  **7,4 % para 4,0 %**.
- **Vão sem catch-up.** O `WORLD` repõe 60 m de trilha (`HIST_N` × `STEP_M`), então
  queda acima de ~11 s a 32 km/h deixa buraco que nem rádio perfeito fecha.

**Custo de rede** (depende do modelo — comparar cenários, não dimensionar): 4 telas
em 85 s, hoje 73 colisões / 69 reenvios do módulo / ar 7,4 %; com `slotDue` 0
colisões / ar 4,0 %. O modelo **não** tem a janela de recepção de 5–15 s do
LoRaMESH, multi-hop, nem alcance em mata — isso só o `firmware/e22_ping` com rádio
na mão resolve.

A lista completa, com o ponto exato do código, está na própria página.

## O que ela é (e o que não é)

A página é uma **transcrição** do firmware, não uma reimplementação livre:

| Camada | Origem |
|---|---|
| `drawHome/drawKeypad/drawSettings/drawSearching/drawMap/drawUI` | `grupo_ws.ino`, linha a linha, sobre um shim da API LovyanGFX |
| `haversine`, `worldToScreen`, `nearestRouteIdx`, `routeAdd`, temas | `trilha_core.h`, sem alteração |
| `sendTrail`, `sendP2P`, `readLoRa`, catch-up por `peerAck` | `grupo_ws.ino`, byte a byte — o inspetor mostra o hex real |
| Cores | `RGB16()` com a **mesma perda de bits** do painel de 16 bits |

**Não simula** (a página lista isso numa tabela, para ninguém decidir errado):
o tearing e a starvation de DMA do painel RGB sem bounce buffer, o PCLK de
12 MHz, os retries do reset do GT911, a janela de recepção de 5–15 s do LoRaMESH
e alcance real de rádio na mata. É uma bancada de **lógica e UI**, não de RF nem
de temporização de painel.

Aproximações declaradas: as fontes `FreeSans*pt7b` viram Helvetica/Arial do
sistema na altura equivalente (`CAP = 0.72`), `Font7` é emulada em 7 segmentos de
verdade, e o canvas suaviza diagonais que o LovyanGFX desenha com Bresenham. O
`roadSeg` **não** é aproximado: são as mesmas N linhas de 1 px deslocadas em x e y,
com o casing sobrescrevendo o fill do segmento anterior.

## Manter em sincronia com o firmware

O valor da bancada morre no dia em que ela divergir do `.ino`. `audit.js` é a
defesa contra isso: lê `trilha_core.h`, `grupo_ws.ino`, `tdma_core.h` e
`tdma_test.ino` e confere constantes, comandos, temas em RGB565, paleta, strings de
tela e regras numéricas. Ele já pegou duas invenções da bancada (limpar `route[]` ao
sair do grupo e `world[].color` inicializado com zero) e garante que os bugs
reproduzidos de propósito — como a divisão inteira do `frameBase` — não sejam
"consertados" sem aviso. O que ele não pega é divergência de **layout**: para isso,
compare com a tela.

## Publicar no Vercel

O arquivo é autocontido (CSS e JS inline, sem CDN, sem build) — o deploy é só
subir o diretório:

```powershell
cd web\bancada-trilha
npx vercel deploy            # preview, URL propria
npx vercel deploy --prod     # promove para bancada-trilha.vercel.app
```

O `.vercel/` criado aqui guarda o vínculo com o projeto e está no `.gitignore`.
O alias de produção é público; as URLs de deployment individuais (com hash) ficam
atrás do login da Vercel — se você mandar um link para alguém, mande o alias.

Ou, sem CLI: no painel do Vercel, **Add New → Project**, importe o repositório,
`Root Directory = web/bancada-trilha`, framework **Other**, sem comando de build. Qualquer
host estático (Netlify, GitHub Pages, Cloudflare Pages) serve igual.

## Controles

- **Clique na tela** entra pelo mesmo `handleTouch()` do firmware: sino de alerta,
  `+`/`−` de zoom, `SAIR DA SALA`, engrenagem e teclado.
- **Cortar sinal / Queda 20 s** — o caminho mais rápido para ver a borda laranja
  de `SINAL PERDIDO`, a ponte tracejada do vão e o catch-up a 3 Hz preenchendo.
- **Tirar seguidor da rota** — borda amarela de `FORA DO TRAJETO` (`OFFROUTE_M`).
- **Redesenho 1 Hz (real)** é o padrão porque o firmware desenha devagar **de
  propósito** (`vTaskDelay(700)`), para o toque responder no core 1. Em 10 Hz a UI
  parece mais fluida do que vai ser.
