# Simulador — o firmware/mts de verdade, várias telas, no PC

```powershell
.\tools\sim\build.ps1                 # compila e roda todos os cenários (≈ 1 min)
.\tools\sim\build.ps1 grupo_lista     # um cenário só
```

Precisa do **Docker Desktop** aberto (o script abre se estiver fechado). O GCC roda
no container `gcc:14` porque o **Smart App Control** deste Windows bloqueia todo
compilador não assinado (o MinGW saía com `0xC0E90002`, sem mensagem).

## Por que existe

Em 2026-09-30 um dia de teste no carro foi perdido por um bug — o ícone do líder
sumia na tela do seguidor (`core/tempo.h` conta a história) — que passou por todos
os testes. Eles testavam **pedaços** (o núcleo, espelhos em JS); nenhum ligava duas
telas e deixava as duas conversarem. Este simulador faz isso.

## Como funciona

- O **mesmo código** que vai para a placa: `firmware/mts/core`, `app` e quase todo o
  `hal` (GPS com o parser NMEA e o casamento do PPS, NVS, relógio, caixa-preta,
  watchdog). Só `hal/radio.h` e `hal/sd.h` são trocados pelos de `fake_hal/`.
- Cada tela é o firmware compilado **dentro de um namespace** (`no.cpp` com
  `-DNO_NS=no0`, `no1`…): variáveis próprias, relógio próprio (com deriva de
  cristal), NVS própria. `node_env.h` dá a cada uma o `millis()`, a `Serial`, a
  `Serial1` (o GPS) e o `Preferences`.
- O GPS simulado dá o pulso PPS na borda de cada segundo UTC e as frases GGA/RMC
  ~120 ms depois, como o NEO-7M — o TDMA sincroniza pelo caminho real.
- O **ar** (`ar.cpp`) entrega os pacotes com air-time, perda por distância
  (calibrada com o carro de 2026-09-30: antena dentro, ~400 m de alcance),
  desvanecimento, colisão e surdez durante o próprio TX.
- **O código leva tempo**: cada leitura do relógio, cada TX e cada leitura de
  pacote custam alguns µs/ms. Sem isso o bug do ícone **passava verde** — foi
  medido: o cenário só o pegou depois que o custo de CPU entrou.
- As ações de tela (CRIAR, ENTRAR pela lista, SAIR, RETOMAR) chamam
  `app/sessao.h`, o mesmo arquivo que os toques chamam; o "ícone na tela" é
  `frotaParaDesenhar()`, a mesma função que o `mapa.h` usa.
- Cada cenário roda num **processo novo** (telas "de fábrica").

## Saída

`tools/sim/out/<cenário>/<tela>/` — `console.txt` (o Serial) e `log/*.csv` no
**formato do cartão**: `tools/campo/resumo_mts.py` e `cruza_mts.py` leem igual.

## Cenários (`sim.cpp`)

| cenário | o que encena | o que confere |
|---|---|---|
| `grupo_lista` | líder cria, seguidor entra **pela lista**, 10 min a 30 km/h, 80 m | ícone e 2 carros **todos os segundos**, caminho sem vão, PPS |
| `perde_e_volta` | seguidor para 3 min (líder some a > 1 km) e depois alcança | ícone volta, vão fecha (catch-up) |
| `lider_reinicia` | líder reinicia no meio da trilha e toca RETOMAR | ícone volta, sem vão preso (piso), numeração nova |
| `radio_tarde` | o 5 V do E22 chega 45 s depois do boot | o rádio entra sozinho |
| `grupos_trocados` | os dois tocam em CRIAR (o teste de 09-30) | cada um vê o outro na lista; SAIR + ENTRAR junta |
| `tres_carros` | três carros em fila | ícone nas duas seguidoras, líder vê 3 |
| `gps_perde` | o GPS do líder cai 60 s | o ícone FICA (último ponto, vazado, "SEM GPS"); o líder segue transmitindo |
| `alerta` | seguidor e depois líder apertam ALERTA e cancelam | chega e some na outra tela em ≤ 13 s |
| `trilha_longa` | 40 min a 40 km/h (~27 km): rastro e rota enchem e giram | ícone sempre; caminho contínuo ≤ 40 pontos atrás DURANTE o percurso |
| `perda_pesada` | ruído alto: ~50 % de entrega a 80 m (o campo de 09-28) | ícone ≥ 98 %; parados no fim, o caminho fecha |
| `quatro_carros` | quatro carros em fila | ícone nas três seguidoras, líder vê 4 |
| `rapido` | 60 km/h com o rádio do seguidor sem 5 V por 60 s | o chip volta configurado; ao reduzir, o caminho fecha |
| `entra_tarde` | o seguidor espera no km 6 e entra quando o líder passa | caminho recente em ≤ 30 s, 6 km de histórico sem vão |

### Estresse (`build.ps1 estresse`; `tudo` roda os dois grupos) — 4 carros, v201

Não entram no `todos`. Medem pelo **diário** (linhas R) de quanto em quanto tempo a
posição do líder chega em cada seguidor, com a entrega por enlace do "ar".

| cenário | o que encena | o que confere |
|---|---|---|
| `comboio4_perda` | 45 min, 15–45 km/h com paradas, 12 s entre carros, ruído até ~60 % de entrega a 300 m | p50 ≤ 2,5 s, p95 ≤ 6 s (≥ 80 %) / ≤ 10 s (≥ 60 %), conflito 0, `perdi` ≤ 1 %, caminho fecha |
| `lider_troca4` | líder reinicia + RETOMAR; depois SAI e o gino cria grupo novo, os outros entram pela lista | ícone volta ≤ 60 s nos três; todos na sala nova, caminho do novo líder inteiro |
| `vao300_4carros` | cada seguidor fica 36 s sem rádio (300 m de caminho) a 30 km/h com ~60 % de entrega | o vão fecha em ≤ 60 s |
| `sem_pps4` | 2 h só com beacon (PPS desligado nos quatro), comboio com perda | holdover + eleição ≤ 1/h, conflito 0, `perdi` ≤ 1 %, sync nunca PPS |
| `longa4` | 3 h de comboio, rastro e rota giram, relógio passa dos 71 min | 0 reinício/relógio voltou, 3ª hora igual à 1ª, caminho ≤ 40 pontos atrás |

> `build.sh` compila com `-DESP_PLATFORM`: sem ele o `tdma_core.h` lia o relógio por
> `micros()` (32 bits) e **todo cenário de mais de 71 min morria só no simulador**
> (`longa4` sem o flag: 3.648 eleições e `perdi` > 100 %). Na placa o core define `ESP32`.

## O que o simulador já achou (2026-10-01)

1. **Rádio que volta "de fábrica"** (`rapido`): um E22 que perde o 5 V entre dois TX
   reinicia sem configuração e transmite/escuta errado sem nunca travar um TX — a
   tela ficava surda a trilha inteira. Correção: `radioHwConfigOk()` a cada 5 s.
2. **Ícone some sem GPS** (`gps_perde`): pacote sem fix apagava a posição do líder.
   Agora fica o último ponto, vazado, e a faixa diz SEM GPS.
3. **Catch-up desperdiçando metade da capacidade** (`trilha_longa`): a 40 km/h, sem
   perder um pacote, metade dos quadros ia em preenchimento e a frente do caminho
   ficava atrasada. Causa: o quadro do roster. Agora a frente vai contígua ao topo
   do seguidor e o preenchimento só usa sobra (`trilhaEscolheBloco`).
4. **Bug do ícone de 2026-09-30** — o simulador o pega (com o custo de CPU modelado).

**Prova de que pega bug:** com o bug do ícone recolocado na cópia do simulador,
`grupo_lista` falha em 3 checagens; com o código atual, passa.

## Limites conhecidos

- Variáveis `static` *dentro de funções* do firmware não são zeradas num RELIGAR
  simulado (a RAM de verdade zeraria).
- Colisão só é decidida com os pacotes já conhecidos quando o receptor coleta.
- Tela, toque e fundo de mapa não são simulados — só a lógica do que eles mostram.
