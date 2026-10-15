# Pesquisa A - forense: ruido, reinicios e o aviso de "5V" (2026-10-13)

Legenda: **[M]** medido (log/CSV), **[I]** inferido de dados medidos, **[E]** estimado/hipotese.
Nada no repo foi alterado. Script: `tools/campo/resumo_diario.py`; saida em 09-30: `scratchpad/resumo_0930.txt`.

## 1. Ruido: bancada -85 x campo -128

### A premissa esta errada: o -128 do campo nao e ruido
- [M] -128 aparece SO em boots com o E22 sem 5 V: FaustoA boot263/265, gino boot18/22/23/24.
  Todos com "radio subiu tarde" 3-6 s, T=0, R=0 e 100 % dos TX presos com err 0x0020 (XOSC/TCXO).
- [M] RadioLib 7.6.0 `getRSSI(false)` = `-byte/2`. -128 impresso com `%.0f` = -127,5 = byte **0xFF**.
  E o chip alimentado pelos pinos de controle, sem RX de verdade. Em 10-08, `ruido=-0` (byte 0x00)
  e o chip mudo/MISO baixo: 170 linhas em p14_COM11, durante o loop "voltou de fabrica".
- [M] Nos boots NO CARRO (com TX funcionando), o ruido foi:
  - gino boot19/20/21: p50 **-88** (p5..p95 -95..-84);
  - FaustoA boot262: **-92..-96**;
  - FaustoA boot261: **-113**.
  - Na bancada 10-06..10-08: -83..-88. **Campo e bancada sao o mesmo numero.**

### O -85/-90 e real (confirmado por uma medida independente)
- [M] O SNR dos pacotes e uma razao, entao nao depende da calibracao do RSSI. Os pacotes com SNR ~0
  chegam com RSSI -86..-95. Os pacotes no limite (SNR -10) chegaram a -98/-100/-105.
  O piso estimado pelos pacotes (rssi-snr) foi: gino -86; FaustoA 262 ~-90; FaustoA 261 -111.
  Isso bate com as linhas N em todos os boots.
- [E] O piso esperado do SX1262 em BW125 e ~-116..-120 dBm. Isso da a sensibilidade de -129 dBm
  menos o limite de -12,5 dB do SF9. Estamos ~**28-30 dB acima**: o receptor decodifica so ate
  ~-100 dBm, contra ~-129.
  E a explicacao mais simples para "sinal ruim" e para o alcance de ~400 m. Nao e bug de medida:
  - `ruidoUlt` e o valor instantaneo; `ruidoMed` (E) e a media do boot;
  - a leitura ocorre no meio de um slot livre, sem TX em voo;
  - o v200 tinha amostras contaminadas pelo TX do outro grupo, de -31 e -73, visiveis no p95.
    O v201+ ja filtra isso (slotOuvido).

### FaustoA boot261 (-113) nao era um ambiente silencioso, era o RX atenuado
- [M] Comparei pares de pacotes no mesmo segundo UTC (+-5 s), F recebendo de g contra g recebendo de F:
  - boot261: diferenca mediana **-32 dB** (p10/p90 -41/-25), em 56 pares;
  - boot262: **-1 dB**, em 81 pares.
- [I] Pela reciprocidade, so o caminho de RX da FaustoA perdia ~30 dB; o TX estava normal (o gino a
  ouvia a -57). 30 dB e a isolacao medida do switch RF. Assinatura de **RXEN (P3 pino 7, IO5) sem
  contato** naquele boot. A FaustoA ja teve DIO1 sem chegar em 10-01 e reinicios curados reencaixando
  P1/P3: chicote dela suspeito.
- [I] Corolario: o ruido caiu junto com o sinal (~25 dB). Logo **entra pela antena/front-end**, nao
  e gerado no SX1262 depois do switch. A fonte provavel e o proprio aparelho (painel DSI 2 lanes
  @ 900 Mbps, backlight, P4/PSRAM, step-down) irradiando para a antena proxima. Na bancada, sem
  carro, da o mesmo -85; logo o carro nao e necessario. [E] A fonte exata nao esta provada.

### Testes para achar a fonte (um por vez, olhando a linha N / `ruido=`)
1. Trocar a antena por uma carga de 50 ohm:
   - N cai para ~-115: o ruido vem pela antena;
   - N fica em -88: entra por alimentacao/cabos.
2. Antena a 1-2 m da tela, com extensao coaxial: se N cai, a tela/chicote e o radiador.
3. E22 alimentado por bateria (sem step-down): se N cai, e o step-down. A Ebyte AN2023005 pede LDO
   depois do DC-DC.
4. Brilho minimo / tela parada x mapa repintando.

## 2. TX do E22 -> falhas no ESP32-P4: o mecanismo

### Fatos medidos
- [M] **As 4 caixas-pretas de panico/INT_WDT tem TX em voo**:
  - longo.log: 101 e 192 ms;
  - faseA: 24 ms;
  - p14 gino: 126 ms.
  - Todas com a etapa "laco (antes: gps)", isto e, a CPU nao estava em codigo de radio.
  - [E] Por acaso seria ~(0,04..0,2)^4.
- [M] Cada "relogio voltou" e um multiplo exato de 2^26 ticks do SYSTIMER de 16 MHz (4,194304 s),
  menos ~1 volta do laco. Casos: 0,998 / 1,998 / 3,998 / 4,998 / 7,998 / 11,998 / 13,998 / 23,998 /
  26,998 / 111,998 / 269,998. Na maioria, o salto e igual a toda a parte do contador a partir do
  bit 26: os bits altos sao zerados em bloco. E corrupcao de registrador, nao conta de software.
- [M] Em 10 dos 18 saltos havia TX em voo. Assinaturas no `[rede]` seguinte:
  - air encurtado (293 us, 2 ms, 14-36 ms, 188 ms), porque `redeRelogioVoltou` re-carimba o inicio do TX;
  - ou ~620 ms, TX fechado pelo vigia.
  - [E] Com duty de TX de 4-20 %, o esperado seria 1-3.
- [M] O panico do gino: MTVAL 0x500E0578 = `GPIO_FUNC8_OUT_SEL_CFG_REG` (GPIO8, I2C do toque),
  conferido no gpio_reg.h do core 3.3.11. Store num registrador valido dando "access fault" =
  **erro de barramento**. Ponteiro ruim nao explica.
- [M] Dose e unidade, bancada 10-08:
  - 10 dBm: 0 eventos em ~200 unidade-min;
  - 14 dBm: gino com 5 saltos + panico em ~15 min; FaustoA com 0 no mesmo tempo e na mesma mesa.
  - 10-06 (FaustoA): faseA 10 dBm com 2 saltos + INT_WDT; faseB 0 dBm com 0; faseC 10 dBm com 0.
    A dose e fraca: 2 contra 0.
  - Depois do panico o E22 ficou mudo (GetStatus falha mesmo com pulso no NRST) ate cortar a
    energia.
- [M] O "voltou de fabrica" em loop de 10-08 e **falso positivo**: chip mudo le 0x00 no
  GetPacketType, que vale "GFSK". O contador `chipReiniciou` mistura "chip resetou" com "chip mudo".
- [M] Car 09-30 (v200, 10 dBm): zero resets violentos e zero presos nos 5 boots do carro.
  O v202 transmite 3-5x mais tempo: o lider faz 419 ms a cada 2 s = 21 %, contra ~4-7 % do v200.

### Hipoteses: o que cada uma preve
| Hipotese | Preve | Encaixe |
|---|---|---|
| **(a) Queda do 5 V do E22** | Reset do SX1262 (volta de fabrica de verdade, responde ao GetStatus); TX preso err 0. So afeta o P4 se a fonte for comum, e ai o esperado e motivo 9/14 (BROWNOUT/GLITCH) | Nao explica salto do SYSTIMER com a CPU viva, nem o chip mudo que o NRST nao recupera |
| **(b) Retorno de terra pelo GND do chicote** (ground bounce / injecao) | Pico de ~0,3-0,6 A achando caminho pelo GND do P3 e pelo plano da tela. As saidas do E22 (MISO/BUSY/DIO1) passam do VDD_IO do P4 e injetam pelos diodos ESD (o back-powering do 09-30 prova que o caminho existe); as entradas do E22 injetam no SX1262, o que da **latch-up** (mudo ate cortar a energia). Depende do contato e cresce com a corrente/potencia | **Melhor encaixe [I]**: dependencia de unidade e de conector, cura ao reencaixar, latch-up, erro de barramento em periferico qualquer |
| **(c) RF da antena acoplando** | Some com carga de 50 ohm na mesma potencia; depende da posicao e da distancia da antena; pegaria as duas telas lado a lado de forma parecida | Possivel contribuinte (I2C do toque); fraco porque a FaustoA, na mesma mesa a 14 dBm, ficou limpa |
| **(d) Mau contato** | Anomalias de sinal especifico (DIO1 10-01, RXEN 09-30) e eventos com vibracao | Quase certo como gatilho; um GND ruim vira (b) |

### Testes que separam as hipoteses
1. **14 dBm em carga de 50 ohm / atenuador** (30-60 min por tela):
   - falhas persistem: e conduzido, (a)/(b);
   - falhas somem: e RF, (c).
2. **Osciloscopio durante o TX**:
   - VCC-GND nos pads 9/10 do E22: queda > 0,5 V confirma (a);
   - GND do E22 contra o GND do P3 pino 11 / USB da tela: > 100-300 mV confirma (b).
3. **Fio de terra grosso e curto** (>= 0,5 mm2) do GND do E22 ao GND da tela, num ponto so; ou E22
   numa bateria isolada com terra num ponto unico. Se as falhas somem, e (b).
4. **Wiggle no conector** a 2 dBm, olhando txSemIrq, RSSI assimetrico e linha N: confirma (d).
   Solucao: conector travado ou solda.
5. Escada de 0/6/10/14 dBm, 30 min cada, contando relogioVoltou. So separa combinada com o teste 1.
6. [E] Instrumentar (depois, sem mexer agora): contar as ISRs do DIO1 durante o TX. Uma tempestade
   de bordas explicaria INT_WDT.

## 3. Os avisos de "5V" no firmware v202

| Aviso | Onde aparece | Condicao exata |
|---|---|---|
| **"N REINICIOS SEGUIDOS - CONFERIR O 5V DO RADIO"** | `app/boot.h:99`. Faixa vermelha `avisoDesenha` (ui.h:463) na tela inicial e no mapa; tambem vai para o diario como `!,ms,AVISO,...` | `resetsViolentos >= RADIO_SEGURO_APOS (3)`. Violento = PANIC 4, INT_WDT 5, TASK_WDT 6, WDT 7, BROWNOUT 9, PWR_GLITCH 14, CPU_LOCKUP 15. Exige caixa-preta valida (LP RAM com checksum) e que nenhum boot intermediario passe de `BOOT_VINGOU_MS` = 120 s. POWERON zera. **A faixa nao some sozinha**: fica ate desligar ou trocar a potencia |
| "TX PRESO - CONFIRA O 5V!" | `tela_diag.h:99`, titulo de celula; so na tela DIAG | `txPreso > 0` desde o boot (vigia de 617 ms) |
| "POTENCIA - REINICIOS SEGUIDOS" / "POTENCIA - ALTA DEMAIS?" | DIAG | seguro / `txPreso*2 > txCount` |
| "nao responde - ver 5V, NSS e MISO" | DIAG | radio nao subiu, beginSt -2 |
| rede.h:170, 594, 609 | **so serial** | boot com seguro / rebegin apos preso / chip "de fabrica" |

[I] O usuario quase certamente viu a **faixa do boot.h** ("CONFERIR" bate com o que ele disse).
Isso implica, em cada tela, >= 3 resets violentos seguidos, cada boot morrendo antes de 2 min.
Ele desligou e ligou: o POWERON zera o contador e apaga a faixa, e (se havia latch-up) tambem
reseta o E22. Daí o "voltava a funcionar".

### Linhas e colunas do diario de domingo que respondem a pergunta
- `!,..,boot,..,motivo N,rom 0x..,seguro S` em cada arquivo: a serie de resets e seus motivos.
- `C,...`: caixa-preta (etapa, txTipo, txHa ms, seguidos, pwr). Responde "morreu durante o TX?".
- `!,..,AVISO,...`: confirma o texto na tela.
- `!,..,relogio voltou,X us` e a coluna `relogioVoltou` da E: testar se X/4194304 e quase inteiro.
- `!,..,tx preso,..,err 0x....`: 0x0000 = OCP/5 V; 0x0020 = sem 5 V/TCXO.
- `!,..,chip voltou de fabrica`, `busy preso`, `startTransmit falhou`, `radio subiu tarde`.
- `N`: ruido. -128 = sem 5 V, 0 = chip mudo, -85..-95 = o ruido "normal" alto.
- `R` (rssi, snr, crc, sala) e `E` (lat, lon, fix, utc): RSSI/SNR/entrega por distancia e o teste
  de assimetria (contato do RXEN).
- `T`: TX completos, air-time.

### Script
`python -I tools/campo/resumo_diario.py <pasta_FaustoA> <pasta_gino>`, so biblioteca padrao. O que ele faz:
- por arquivo: boot/motivo, caixa-preta, AVISO, presos (err), voltou de fabrica, saltos do relogio
  em unidades de 2^26, RX bons/ruins/outra sala com RSSI/SNR, piso pelos pacotes, ruido N em
  percentis separando -128/0/>-60, e a linha E final;
- totais por pasta e ruido por versao e classe de boot;
- com 2 pastas: RSSI/SNR e entrega por faixa de distancia.

Testado em 09-30 (saida em `resumo_0930.txt`) e num diario v202 sintetico (C/AVISO/salto/preso/0 dBm).
Limitacao: os arquivos `MTS campo v100` (09-28) so tem o boot lido. A entrega inclui periodos em
grupos diferentes.

Resultado em 09-30 [M]: entrega F->g de 84 / 76 / 52 / 0 % (0-100 / 100-250 / 250-400 / 400-650 m);
g->F de 83 / 72 / 87 / 0 %.
