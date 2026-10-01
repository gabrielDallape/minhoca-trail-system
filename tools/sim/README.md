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

**Prova de que pega bug:** com o bug do ícone recolocado na cópia do simulador,
`grupo_lista` falha em 3 checagens; com o código atual, passa.

## Limites conhecidos

- Variáveis `static` *dentro de funções* do firmware não são zeradas num RELIGAR
  simulado (a RAM de verdade zeraria).
- Colisão só é decidida com os pacotes já conhecidos quando o receptor coleta.
- Tela, toque e fundo de mapa não são simulados — só a lógica do que eles mostram.
