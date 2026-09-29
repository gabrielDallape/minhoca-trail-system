# firmware/mts — o firmware da tela P4 de 7"

Alvo unico: **Waveshare ESP32-P4-WIFI6-Touch-LCD-7B** (EK79007 1024x600, GT911) +
**E22-900M30S** (SX1262) + **GPS NEO-7M com PPS no IO2** + microSD (mapa em
particoes brutas, diario na FAT). Nasceu do `mts_campo` v101 e do `mts_p4`
(referencias, nao mexer), reorganizados em camadas e recalculados para o
**quadro de 6 s com 12 slots de 500 ms**.

```
.\tools\build.ps1 firmware\mts -Board p4                     # compila
.\tools\build.ps1 firmware\mts -Board p4 -Upload -Port COMx  # grava (CH343 = "USB-Enhanced-SERIAL", VID 1A86)
.\tools\build.ps1 firmware\mts_selftest -Board p4 -Upload -Port COMx   # o core na placa, sem periferico
node web\bancada-trilha\mts_core_test.js                     # o core no PC + auditoria do fonte
```

Nao use `EraseFlash`: apaga a NVS (nome, slot, potencia, sessao). O esquema de
particao `default_8MB` tem dois slots de app (OTA). Antena no IPEX **antes** de
energizar: o radio transmite assim que o slot chega.

## Arquitetura

Dependencias so para baixo. Um arquivo de cima nunca e incluido por um de baixo.

```
 mts.ino          boot, laco principal, comandos do serial ('p' 's' 'm' 'x' 'a' 'd')
   |
 app/             servico.h    UM servico(), chamado de todo laco de tela: relogio, GPS,
   |                           posicao, rede, frota, wifi, diario, caixa-preta, watchdog
   |              rede.h       TDMA + protocolo + frota + rota sobre o radio (R/T/N/V no diario)
   |              registro.h   diario de bordo (anel na PSRAM, flush so com orcamento)
   |              config.h     carrega/migra/valida/grava a configuracao
   |              estado.h     o estado da aplicacao e a maquina de estados (EST_*)
   |              wifi_ota.h   OTA local, eco e nuvem em fatias (MTS_TEM_WIFI 1 apenas)
   |              telas/       ui, icones, splash, teclado, mapa, mapa_fundo, fila,
   |                           tela_inicial, tela_config, tela_diag, tela_grupo, tela_trilha, tela_wifi
   |
 hal/             pinos.h      a placa (pinos, chaves de recurso)     display.h  LovyanGFX DSI + GT911
   |              radio.h      SX1262 com as 12 armadilhas pagas       gps.h      NMEA + PPS (C1 corrigido)
   |              sd.h         montagem, setor bruto, arquivo           nvs.h      Preferences ("grupo", "wifi")
   |              caixa.h      caixa-preta RTC_NOINIT                   relogio.h  esp_timer + guarda de salto
   |              wdt.h        task watchdog da loopTask (10 s)
   |
 core/            params.h     TODAS as constantes + static_asserts     airtime.h  formula do datasheet
 (sem Arduino)    proto.h      pacote versionado                         tdma.h     -> ../tdma_core.h (compartilhado)
                  rota.h       rota numerada + catch-up                  trajeto.h  meu rastro numerado
                  frota.h      carros, TTLs, extrapolacao, acks          geo.h      haversine, rumo, avanco
                  cfg.h        esquema + migracao                        anel.h     buffer circular do diario
                  diario.h     cabecalhos do CSV                         orcamento.h quem manda no laco
```

`core/` nao inclui Arduino nem ESP-IDF: e espelhado em Node
(`web/bancada-trilha/mts_core_test.js`, que tambem **le** os `.h` e refaz as
contas) e compilado na placa por `firmware/mts_selftest`. `core/tdma.h` inclui o
`firmware/tdma_core.h` compartilhado com a bancada web em vez de copia-lo.

Regras: nada bloqueia sem timeout (a unica espera sem timeout do RadioLib, o
`startTransmit`, tem guarda de BUSY antes); nada aloca depois do boot no caminho
quente (sprite do mapa, anel do diario e caches do cartao nascem no `setup`);
todo contador de erro esta na tela de diagnostico (3x5 celulas) e na linha E do
diario; toda conta de tempo/tamanho e `static_assert`; a potencia NUNCA muda
sozinha (so `'p'` no serial ou a celula POTENCIA do diagnostico gravam).

## Tempo — TDMA

| Parametro | Valor | De onde vem |
|---|---|---|
| Quadro | **6 s** (divide 18 e 86400) | decisao do usuario; grade GPS x UTC imune |
| Slots | **12 x 500 ms**; carros nos slots 0..7; 8..11 sempre vazios (ruido) | `params.h` |
| Guarda | 20 ms (60 ms em holdover) | `TDMA_GUARD_US` |
| Air-time 16 B / 23 B (roster) / 68 B (trilha cheia) | 171 / 212 / 417 ms | `airtime.h`: SX1262 6.1.4 + 6,5 ms medidos em SF7 |
| Janela para o laco decidir o TX | 63 ms | 500 − 20 − 417 |
| Vigia de TX preso | 617 ms | maior pacote + 200 |
| Holdover (sem ancora) | 24,5 s = 4 quadros + 0,5 s | `TDMA_HOLDOVER_QUADROS` (`params.h`) |
| Eleicao da ancora sem GPS | 3 s + slot x 6 s (slot 7: 45 s) | `TDMA_ELECT_BASE_US` |
| PPS firme antes de ancorar | 10 pulsos | `TDMA_PPS_FIRME` |
| Roster | falas 1, 3, 5 e depois a cada 8 — **intercalado**, nunca 2 quadros sem beacon | I5 da auditoria |
| Posicao dos outros | a cada 6 s, **extrapolada** por rumo x velocidade ate 12 s | `frota.h` |
| Sinal velho / some da lista | 30 s (p = 3 % a 50 % de perda) / 90 s | `FROTA_*` |
| Passo do trajeto | **10 m** (era 5): o catch-up converge ate 30 km/h | conta em `params.h` |
| Catch-up | 10 pontos por pacote, quadros alternados; so conta quem ouve o lider | `rota.h`, `frota.h` |

Radio: 915 MHz, SF9, BW125, CR4/5, preambulo 8, sync 0x12, TCXO 2,2 V, OCP 140 mA,
rampa 800 us, `setOutputPower(p, false)`, RX boosted. Potencia padrao 10 dBm no
chip (~20 dBm na antena, PA +10 dB); teto legal em BW125: 24,2 dBm.

## Pacote (little-endian; `core/proto.h`)

```
[0]      PROTO_VERSAO (2)     diferente -> ignorado, contado, AVISADO na tela (linha V)
[1..2]   sala (hash 16 bits do codigo do grupo)
[3]      slot = identidade (0..7)
[4]      seq rolante (perda por remetente)
[5]      flags  0x01 FIX  0x02 ALERTA  0x04 LIDER  0x08 PPS(grade UTC)  0x40 TRILHA  0x80 ROSTER
[6..13]  lat, lon int32 grau x 1e7            (roster: [6] cor, [7..22] nome, 23 B)
[14]     rumo graus/2        [15] velocidade km/h                       -> 16 B
[16]     epoca do trajeto    [17..18] seq do 1o ponto   [19..20] ackAte   [21..22] ackDe
         (seguidor: [17..18] = topo, o ultimo ponto do lider que ele tem)
         (lider:    [19..20] = 0xFFFF, [21..22] = o ponto mais antigo que ele guarda = piso)
[23]     n pontos            [24..31] 1o ponto absoluto  depois (n-1) x (dlat, dlon int16, 1e-5 grau)
                                                                         -> ate 68 B
```

## Diario (`/log/HHMMSS_boot.csv` ou `/log/bootN.csv`; `core/diario.h`)

Primeira coluna = tipo. `E` estado a 1 Hz (40 colunas, cabecalho no arquivo),
`R` cada pacote recebido (RSSI/SNR/erro de frequencia, inclusive CRC ruim, perda
por sequencia), `T` cada transmissao com o **air-time medido**, `N` ruido de
fundo num slot vazio, `G` fix/PPS quando muda, `!` eventos (boot, sala, slot,
potencia por comando, tx preso, relogio voltou, flush lento, tela), `C` caixa-preta
do boot anterior, `V` pacote de versao de protocolo diferente.

## NVS

Namespace `grupo` (mesmas chaves do `grupo_ws`/`mts_campo`): `name color theme
zoom slot pwr boots cfgv pwrv epoca s_ativa s_lider s_gnome s_gcod`; `wifi`: `ssid
pass`. Esquema `cfgv 2`; a migracao de um aparelho v101 preserva tudo; de um
mts_p4/v100 sobe o `pwr` ao padrao, uma unica vez, SE estiver abaixo dele (o
valor podia ter vindo do recuo automatico) - nunca desce.

## O que nao esta aqui

Suporte a tela de 5" (`LGFX_P4_LCD5.h`) — um alvo so. Recuo automatico de
potencia, resgate de boot e teto de modo seguro — a regra e que a potencia nunca
muda sozinha; o que existe e um ALARME (faixa vermelha) apos 3 reinicios
violentos seguidos, com a potencia intacta. Simulador de outros
carros sem radio (o `mundoEntra` do mts_campo) — sem radio o mapa mostra so o
proprio carro. `tools/publica_ota.ps1` e `tools/frota.ps1` ainda apontam para
`firmware/mts_p4`: trocar para `firmware/mts` quando este firmware for para a frota.
