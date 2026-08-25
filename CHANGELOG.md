# Changelog

Histórico das fases do projeto. As datas são aproximadas (marcos, não releases formais).

## Frota de 3 telas + atualização pela internet (2026-08-25)

O dia em que o sistema virou frota. Tudo validado na bancada com as três telas.

### Mapa regenerado do zero e cartão da tela C

- Os pacotes `brasil.vec`/`brasil.dem` tinham sido apagados do PC. Regenerados
  do zero com o pipeline do repo (`tools/mapa/`): OSM do Brasil (2,1 GB) +
  relevo SRTM (68 blocos, 1,58 GB) → `brasil.vec` 1.228 MB (332 mil setores,
  p50 de leitura 13–15 ms) + `brasil.dem` 3,90 GB. **Agora moram em
  `Desktop\gps-mapas\` e não se apagam** — cartão novo custa 4 min de gravação.
- Cartão da tela C gravado e validado (CRC 18/18 + 31/31 OK).

### Atualização de frota pela internet (o fim do cabo)

- **Servidor de release na Vercel**: https://mts-ota.vercel.app (fonte em
  `tools/ota_server/`). A raiz é um painel de status; `/version.json` é o
  manifesto que as telas leem.
- A tela, conectada em **qualquer WiFi** e parada na tela inicial, checa a cada
  15 min (e 40 s após ligar); versão maior → baixa por HTTPS, confere MD5,
  grava no 2º slot e reinicia. Erro no meio = continua na versão atual. **Na
  trilha nunca atualiza** (a checagem vive só no loop da tela inicial).
- **Armadilha que custou duas implementações**: o TLS do esp_hosted (C6) trava
  em respostas maiores que ~8 KB — nem laço manual nem `HTTPUpdate` passam.
  Solução: download **em fatias de 8 KB com HTTP Range** (o CDN responde 206)
  em conexão keep-alive, cada fatia bufferizada antes do `Update.write`.
- Publicar = subir `MTS_VERSAO` (hardware.h) + `tools\publica_ota.ps1`
  (compila, MD5, manifesto, deploy). **Prova ao vivo**: tela C se atualizou
  v4→v5 sozinha pela internet, 1,8 MB em fatias, reboot na versão nova.
- Colisão de macro: o `M` da ui.h vs parâmetro `M` do bignum.h (mbedTLS) —
  resolvido com push_macro/pop_macro em volta dos includes de rede.

### Giro de 180° para a caixinha

- A caixinha monta o painel de cabeça para baixo. `MTS_TELA_180` em hardware.h.
- **`setRotation()` é ignorado em silêncio pelo caminho DSI** (testado: nada
  vira). O giro de verdade é `offset_rotation = 2` na config do painel
  (LGFX_P4_LCD7.h) — o toque gira junto (a LovyanGFX soma o offset do painel ao
  do toque em `convertRawXY`, conferido no fonte).

### Tela de WiFi e teclado refeitos (feedback do usuário: "péssimo, bugado")

- **Causa real do scan vazio**: varrer durante a reconexão automática de 30 s
  devolve lista vazia. Agora derruba a tentativa antes de varrer.
- Redes **ordenadas por força e sem repetição** (mesh); botão PROCURAR DE NOVO;
  rede salva marcada; conexão tenta 2× (o C6 perde o 1º begin às vezes — isso
  aparecia como "senha errada" com senha certa); falha volta para a lista.
- **Teclado em layout de celular**: fileira de números SEMPRE visível, modo
  símbolos completo, senha começa em minúsculas e **não troca de caixa
  sozinha** (a troca automática é para nome próprio; em senha mudava o que a
  pessoa digitava), limite 24→32, textos de senha.
- Bug do rótulo empilhado no botão de modo: `botao()` vazado não pinta fundo —
  repintar por cima empilhava abc/#$%/ABC. Agora limpa o retângulo antes.
- Listra de sol do cartão ENTRAR removida (lida como "cor vazando" do CRIAR).

### Rede de 3 nós validada na bancada

- **Tela b** (líder, slot 0, rádio bom a 22 dBm): GPS com fix real (HDOP 0,98)
  e **PPS 1 Hz disciplinando o TDMA** (`sinc=PPS`) — estreia da âncora
  definitiva. **Tela a** (slot 1) e **Tela c** (slot 2) sincronizadas por
  beacon. Zero conflito, zero pacote ruim, roster cruzando ("slot 0 se
  apresentou: Tela b"), **alerta apertado na C acendeu na A e na B**.
- O detector de conflito de slot pegou o caso real ao vivo (B e C ambas em 0)
  e o aviso guiou o conserto — C voltou ao slot 2 pelo serial (`s2`).
- Tela C ficou surda (rx=0) após manuseio: **antena/chicote solto** — reencaixe
  resolveu. Regra de sempre: antena antes de energizar.
- Tela A em loop de POWERON no USB do PC ao ganhar WiFi (painel + rádio + GPS +
  C6 estouram o orçamento da porta) — na fonte de tomada, estável.

## Primeiro dia de campo + decisões de campo (2026-08-24)

Primeiro teste com carros de verdade. Aprendizados e mudanças pedidas do campo:

- **O GPS "mini" (NEO-7M de 5 pinos) EXPÕE PPS e ele já está fiado no IO2** — a
  nota "PPS não fiado / breakout não expõe" valia para o GY-NEO7MV2, outro
  breakout. Docs corrigidos. O caminho de PPS do firmware estreia sozinho
  quando houver fix (pastilha vira "PPS").
- **Fix a frio exige o carro PARADO com céu aberto** — confirmado em campo; com
  a pilha de backup o problema quase some (o MKR GPS Shield avaliado foi
  descartado: antena integrada e sem PPS no header; o mini com antena externa é
  melhor para carro).
- **Mapa orientado pelo rumo (heading-up)** — "a tela não está seguindo o
  caminho". O bloqueio antigo era o relevo raster; com o relevo desligado (ver
  abaixo) o vetor gira por matriz 2×2 no `VecXform` (janela de descarte pela
  inversa, cobertura pela meia-diagonal). O "N" virou bússola andando no anel;
  setas dos carros em rumo relativo.
- **Mapa enxuto** — "muita informação": ficam TRILHA, ESTRADA, RUA (perto),
  RIO/CÓRREGO/LAGO, PROTEGIDA e LIMITE (divisa de município, novo); saem
  relevo, mata, ferrovia, pista e POIs não críticos (porteira/vau/bloqueio
  ficam). Tudo reversível — nada sai do cartão.
- **Traçado mais grosso** — "linha muito fina" com o carro pulando: rota
  15/9 (era 11/5), rastro 11/5 (era 8/3), alerta 17/9.

### Escada de potência (noite do mesmo dia, fonte de tomada 2–3 A)

Medido com o `escada.ps1` (conta reboots + telemetria), 110 s por degrau:

| Degrau | Tela A | Tela B |
|---|---|---|
| 2 dBm | ✅ perfeita (confirmado de novo após mexida no 5 V) | ✅ |
| 5 dBm | ❌ todo TX trava | — |
| 10 dBm | ❌ todo TX trava (antes E depois de reforçar o fio de 5 V) | ✅ |
| 22 dBm | ❌ todo TX trava | ✅ **tx=120 rx=120 preso=0, zero reboots** |

Conclusão FINAL, com a confissão do próprio chip (`GetDeviceErrors` via
`e22_ping`): **o módulo E22 que estava na tela A é defeituoso** — ao armar TX
acima de ~2 dBm ele devolve `0x0020 XOSC_NAO_PARTIU` (o TCXO não parte) e
reseta, perdendo a configuração. Eliminados por teste: fio de 5 V (reforçado,
sem efeito), DUAS antenas diferentes (sem efeito), placa (a B idêntica passa a
22 dBm), firmware (idêntico) e as curas por software (`setRegulatorLDO`,
`setTCXO(2.4, 10 ms)` — sem efeito). A tela B está **validada em potência de
campo completa (29,3 dBm ERP)**. Remédio: trocar o módulo da A pelo terceiro
E22 (reserva); o defeituoso vira peça de bancada (funciona até ~2 dBm).

Bônus da sessão: **primeira âncora por PPS da história do projeto** — a tela A
fixou na janela e o log virou `sinc=PPS` sozinho: pulso → ISR → par com NMEA →
âncora, tudo de primeira. Com PPS nos dois, some a dependência do nó 0.

Adendo: a potência de TX virou **ajustável em execução e persistida na NVS**
(`p` + número no serial, `radioTrocaPotencia`), nascida de seis regravações num
dia só para trocar um `#define`. Com ela, o teto do módulo defeituoso foi
bissecado ao dBm: **6 dBm estável, 7 já trava** (e o teto subiu de <5 para 6
depois do reencaixe da antena — o conector também contribuía). A tela A ficou
cravada em 6 dBm (~13 dBm ERP com o PA, ~4× o alcance do modo bancada) até o
módulo reserva entrar; a B segue em 22.

Firmware ganhou **auto-conserto do rádio**: TX preso agora refaz o `begin()`
completo (um módulo que apagou por queda de 5 V esquecia a configuração e
ficava surdo na frequência de fábrica — agora volta sozinho em ~0,5 s).

## Revisão pré-MVP das duas telas P4 (2026-08-23)

Revisão completa do `firmware/mts_p4` antes do primeiro teste com duas telas.
Nove defeitos corrigidos; compila em 1034 KB; bancada 63+71+55 PASS. **Nada
disto rodou em hardware ainda** — gravar e conferir é o primeiro passo.

- **A rede morria aos 71,6 min de uso.** As ISRs do PPS e do rádio carimbavam com
  `micros()` (esp_timer truncado a 32 bits); o `tdma_core` compara com o relógio
  de 64 bits, então no estouro a âncora saltava ~967 ms (2³² mod 1e6) e todo nó
  desalinhava. Carimbos agora em 64 bits, copiados com a interrupção suspensa.
- **A janela de TX deixava o pacote vazar para o slot do vizinho.** Ela limitava o
  INÍCIO do TX; com o loop atrasado (mapa repintando por ~100 ms) um TX no fim da
  janela invadia até 48 ms do slot seguinte. `tdmaShouldTx` agora desconta o
  air-time do limite superior (com `airtimeUs = 0` nada muda). Espelhado no port
  da bancada, coberto pelo `tdma_selftest` [3b] e pelo `audit.js`.
- **A rota do líder nunca era desenhada.** `mundo.h` montava `g_rota[]` a cada
  pacote do líder e o `mapa.h` não a pintava — o seguidor via um ponto andando,
  nunca o caminho, que é a promessa do produto. Roxo, do ponto onde eu estou para
  a frente, com ponte tracejada nos vãos de sinal.
- **O alerta era ficção de ponta a ponta.** Não havia como pedir socorro (nada
  punha `TDMA_FL_ALERT` no ar) e o alerta recebido não acendia (a faixa lia uma
  cópia velha). Agora há o botão ALERTA (meu pedido, transmitido) e as faixas
  leem `g_carros` vivo.
- **A tela da trilha desenhava um retrato, não o mundo.** O seguidor entrava com a
  lista vazia e nunca via a faixa do líder; nome de roster não atualizava; e o
  `mudou()` só olhava a MINHA posição — parado, o líder ficava congelado no mapa.
- **Carro zumbi, de novo.** Nada zerava `ativo`: quem desligava ficava na lista e
  na contagem para sempre (o mesmo bug que o grupo_ws teve). Dois estágios: sem
  fix após 5 s de silêncio, fora da lista após 15 s.
- **Conflito de slot silencioso.** Pacote da minha sala com o meu slot é OUTRO
  aparelho com a minha identidade (eu nunca ouço meu próprio TX) — era descartado
  como "eco". Agora conta e grita no serial. Duas telas recém-gravadas saem AMBAS
  no slot 0: separá-las (`s0`/`s1` no serial) é pré-requisito do teste.
- **Nó mudo e surdo para sempre se o TX_DONE se perdesse** (fio do DIO1 mexido):
  o `standby()` pré-TX tirava o chip de RX e ninguém o rearmava. Vigia de 400 ms.
- **Esperas cegas deixavam o nó surdo.** Teclado, arrasto na trilha e piscada de
  alerta usavam `delay()` cru — perdiam janelas de TDMA e afogavam a UART do GPS.
  Tudo passou a servir rádio e GPS enquanto espera (`esperaServindo`).

E as consequências encadeadas: trocar de sala agora limpa os carros da sala
antiga (a sala de espera nascia com fantasmas do grupo anterior), sair da trilha
volta o rádio para SEMSALA (o aparelho continuava transmitindo no grupo de quem
ficou), o `mundoInicia` com rádio não apaga o mundo vivo (duplicava cada carro
por 15 s), o criador anuncia LIDER já na sala de espera, e o FICAR do diálogo de
sair não é mais recursivo.

Registrado e **não** corrigido (decisão de arquitetura, some com PPS em todos os
nós): no modo beacon a âncora herda o jitter do instante em que o nó 0
transmite dentro do próprio slot (até ~47 ms depois da janela com air-time;
comentário no `radio.h` manda usar slots baixos sem PPS).

### Bancada da noite, mesma data — DUAS TELAS P4 CONVERSANDO POR TDMA

Primeira comunicação real da pilha nova: duas 7B trocando posição e roster por
SX1262 com disciplina de slot — `tx=72/perdi=0/rx=72/ruim=0/conflito=0`,
RSSI −17/−24 dBm, roster cruzando ("slot 1 se apresentou"). No caminho, três
diagnósticos que valem registro:

- **Chicote P3 sem contato (as duas telas)**: rádio vivo no P1 (BUSY responde ao
  reset) mas SPI/GPS mudos. Provado por `firmware/chicote_probe/` (nova): escada
  de SPI ativa, testemunha do BUSY, permutação dos 4 pinos e caçador em 972
  combinações. Reencaixe resolveu; a sonda fica no repo.
- **Brownout no TX de 29 dBm**: alimentada só pelo USB do PC e sem o capacitor
  de 470 µF, a placa reseta em loop (`rst:0x1 POWERON` ~2 s após o painel, no
  primeiro pacote) — o modo de falha exato que o MONTAGEM.md previa, agora
  MEDIDO. `RF_PWR` está em **2 dBm (bancada)** no hardware.h; voltar a 22 no
  campo SÓ com o capacitor montado e alimentação de verdade.
- **`tdma_selftest` no silício do P4 pela primeira vez: 66 PASS, 0 FAIL**,
  incluindo a janela com air-time [3b] e o TTL por frame [7].

Pendências físicas: GPS da tela B mudo (frases=0, PPS pegando ruído de pino
solto — fios IO2/3/4 + 3V3 do rabicho dela; os do rádio estão bons) e o
capacitor/alimentação para potência de campo. Comando de bancada novo no serial:
`x` = sair da sala (descarta sessão, SEMSALA); prints "mundo:" quando um carro
novo é ouvido ou se apresenta.

### Segunda passada, mesma data (revisão completa + melhorias)

- **Rumo e velocidade entram no ar.** O pacote sempre teve `heading`/`speed`
  (seção 7 do plano) e iam zerados. Agora o `gps.h` lê o RMC (rumo só andando
  ≥ 3 km/h — parado, o rumo do GPS é ruído), o pacote os carrega e o marcador
  vira uma **seta que gira** (`marcaSetaRumo`): o meu no centro e o do líder na
  tela dos seguidores. Um triângulo fixo apontando o norte mentia sobre qual
  saída da bifurcação o líder pegou.
- **A `fila.h` saiu do papel.** A distância nas faixas agora é **pelo caminho**
  (projeção na rota do líder), com linha reta como reserva quando ainda não há
  rota; e o carro a mais de 150 m da rota mostra **FORA** em vez de um número
  que mente. Era o argumento do produto, calculado e nunca usado.
- **Holdover deixou de piscar em regime normal.** O TTL da âncora acompanha o
  frame (`max(1,5 s, 2×frame + 0,5 s)`): o roster do nó 0 troca o beacon de um
  frame a cada oito, então 2 s de silêncio com frame de 1 s é normal — o TTL
  fixo de 1,5 s triplicava a guarda e acendia alarme a cada 8 s sem nada errado.
  Espelhado na bancada; testes do holdover atualizados nos dois lados.
- **Roxo só na rota do líder.** O rastro próprio tinha um estado roxo "à frente"
  herdado do grupo_ws (lá o buffer era a rota compartilhada); aqui ele fingia
  rota num cruzamento com o próprio caminho. Rastro próprio é sempre azul.
- **A abertura não deixa mais o nó surdo**: os 2,4 s do splash servem rádio e
  GPS (o rádio sobe antes do painel, e é o nó 0 ancorando a rede nesse momento).
- **Código morto substituído removido**: `pastilha` antiga, `escadaZoom`,
  `iconeTemaAntigo`, `sinal` e `telaAviso` — todas versões já superadas, sem
  nenhum chamador. Nome na faixa cortado em 12 caracteres para não invadir a
  distância.
- Revisados sem pendência: `vec_pack.h`, `dem_pack.h` (limites, CRC, borda de
  setor), `sd_setor.h`, `LGFX_P4_LCD7.h`, `icones.h`, `splash.h`. O `e22_ping`
  usa `micros()` só em deltas curtos — correto, sem mudança.

## Bancada web + correções que ela encontrou (2026-07-31)

Simulador das telas em `web/bancada-trilha/` ([no ar](https://bancada-trilha.vercel.app)):
quatro canvas de 1024×600 reexecutando a lógica do firmware — `grupo_ws.ino` +
`trilha_core.h` nos modos GRUPO e P2P, e `tdma_core.h` + `tdma_test.ino` no modo
TDMA, com clock próprio por nó (offset + drift de ±40 ppm). Não substitui hardware:
é bancada de **lógica e UI**, e a página lista o que ela não simula.

Três verificações automáticas, nenhuma precisa de placa:

- `selftest.js` — 63 checagens de comportamento (fluxo de grupo, catch-up do P2P,
  TDMA, desenho das cinco telas, toque, 600 cliques aleatórios).
- `audit.js` — 70 checagens da **bancada contra o firmware**: constantes, comandos,
  temas em RGB565, paleta, strings de tela e regras numéricas de desenho. Pegou duas
  invenções da bancada (limpar `route[]` ao sair e `world[].color` em zero).
- `tdma_replay.js` — reexecuta os 9 grupos do `tdma_selftest.ino` sobre o port JS,
  com os mesmos números. Existe porque não há compilador C++ na máquina e o
  auto-teste do firmware roda no ESP32: 43 PASS.

### Bugs corrigidos no `tdma_core.h` / `tdma_test.ino`

Nenhum precisou de rádio para aparecer, e todos estão na Fase 4 — código que **nunca
foi executado**.

- **`frameBase` calava metade da rede.** `frameBase += (rxUs - anchorUs) / frameUs`
  era divisão inteira: num nó com cristal mais lento que a âncora o intervalo entre
  beacons medido localmente fica abaixo de `frameUs` (1 s a −40 ppm = 999,96 ms), o
  resultado era 0, o `frameBase` congelava e a guarda de "1 TX por frame" travava o
  nó **para sempre** — exatamente o sintoma que o campo existe para evitar. Medido em
  60 s: `0/−30/−20/−10 ppm → tx = 60/2/2/2`. Agora arredonda com mínimo 1 →
  `60/60/60/60`, inclusive com 30 % de beacon perdido. **Com dois nós o bug passava**,
  porque depende do sinal do erro relativo dos cristais.
- **Âncora do beacon atrasada de um pacote inteiro.** O `rxDone` vem no fim do
  pacote; sem descontar o air-time a âncora ficava 51,5 ms tarde (16 B em SF7/BW125),
  quase a folga inteira de um slot de 125 ms — quase todo pacote chegava em slot
  errado e, com 8 nós, virava colisão. `tdmaSetAirtime()` informa o valor medido e o
  `tdmaOnBeacon` desconta. Com `airtimeUs = 0` a correção é inerte, então o
  auto-teste antigo continua valendo.
- **Somar PPS ao beacon matava o TX.** O sketch ancorava no beacon sem checar a fonte
  de tempo; com PPS por cima, `sync` alternava e o `frameBase` congelava. Agora o
  beacon só vale enquanto não houver PPS — ele é fallback, não um segundo relógio.
- **O nó âncora vivia em holdover.** Ele ancora uma vez no `setup()` e nunca mais, e
  o `tdmaTick` o marcava em holdover após 1,5 s, triplicando a guarda dele para
  sempre. Novo campo `isAnchor` resolve.

### Bugs corrigidos no `grupo_ws.ino` / `trilha_core.h`

Mexer aqui foi decisão explícita do usuário. Compila em 449331 B (14 % de `huge_app`).

- **Carro zumbi.** Nada zerava `world[].active`, e o `parseRx` do seguidor refresca
  `lastMs` a cada `WORLD`: o `NODE_TTL` nunca disparava lá e um carro que saía do ar
  ficava no mapa dos seguidores, parado na última posição, para sempre. O `loop()`
  agora expira quem não reporta.
- **Alerta que não acendia em ninguém.** `drawMap` olhava só o primeiro peer ativo,
  então num grupo cheio o sino de um slot alto não acendia em nenhuma tela. Agora
  procura qualquer carro em alerta antes de cair no primeiro peer.
- **`slotDue` calculado e ignorado.** `parseRx` computava
  `slotDue = worldRxMs + curSlot*SLOT_MS` e o `loop()` mandava o uplink num timer
  livre de 1200 ms; como o `ROSTER` é broadcast, os seguidores entravam em fase e
  colidiam sistematicamente (até 100 % de colisão com 3 seguidores, com o canal 95 %
  livre). Passou a respeitar o slot, com fallback de 3 s: colisão a **zero** e
  ocupação de ar de 7,4 % para 4,0 %.
- **Vocabulário SALA → GRUPO** nas telas e no serial (pedido do usuário). A chave NVS
  segue `room` — nenhuma tela perde configuração ao regravar.
- **Cor por slot.** `g_color` nasce 0 no NVS de todos, então o JOIN pintava todo mundo
  com `PALETTE[0]`. Sem provisionamento, o slot vira a cor.
- **Rótulos empilhados no mapa.** Nomes escritos em `sx+12, sy-8` sem checar colisão:
  a 2 m/px, carros a 30 m ficam a 15 px e os nomes viravam uma mancha. Agora desviam.
- **Lixo ao sair do grupo.** `route[]` e `world[]` sobreviviam ao SAIR, então o
  trajeto antigo aparecia emendado no do grupo novo, com ponte tracejada ligando os
  dois lugares.

Ficaram registrados e **não** corrigidos, por serem decisão de produto ou de
arquitetura: o vão sem catch-up no caminho de grupo, o `FORA DO TRAJETO` de quem
entra atrás, `MAXN = 8` contra os 25–50 nós do plano, o roster ausente na pilha nova
e a resolução do trajeto ditada pelo frame no TDMA.

## Fase 4 — Rumo ao TDMA + mapa offline (em andamento)

Preparação da pilha nova, em sketches separados.

- **Ambiente de build reparado e documentado** (`docs/AMBIENTE_BUILD.md`): o core
  `esp32:esp32` não estava instalado e faltavam LovyanGFX, TinyGPSPlus e LoRaMESH
  — nada compilava. Agora em esp32 3.3.11. Script `tools/build.ps1` encapsula o
  caminho do `arduino-cli` (que vive dentro da Arduino IDE, fora do PATH) e o FQBN.
- **Tamanho do binário medido pela primeira vez**: `grupo_ws` usa 449 KB, só 14 %
  dos 3 MB de `huge_app`. Consequência: um esquema de partição com dois slots (OTA)
  caberia com folga de 4×, sem precisar de `partitions.csv` customizado.
- **Rádio novo**: 3× E22-900M30S (SX1262, 1 W, TCXO). O LoRaMESH é incompatível com
  TDMA por arquitetura — firmware de rede fechado atrás de UART, com roteamento e
  ACK próprios, janelas de recepção de 5–15 s e **sem sinal de TxDone**. Sem saber
  quando o pacote saiu, não há slot.
- **`firmware/tdma_core.h`** — fatiamento do tempo em frames/slots, com a fonte de
  tempo atrás de uma interface: beacon do nó 0 na bancada (fase 2) e PPS do GPS
  depois (fase 3), sem mudar o resto do código. Frame em segundos inteiros para
  ancorar no PPS. Guarda, holdover, 1 TX por frame, pacote de posição de 16 bytes.
- **`firmware/e22_ping/`** — ping-pong SX1262 que **mede** o tempo real entre
  `startTransmit` e o `txDone` da ISR, para dimensionar o slot com número medido em
  vez da estimativa de 40–60 ms.
- **`firmware/tdma_test/`** — TDMA de 3 nós com coordenadas falsas; conta colisão
  (CRC), pacote em slot errado, janela perdida e idade de cada vizinho.
- **Mapa offline**: `tools/tiles/` (baixar, converter para RGB565, empacotar com
  índice + CRC32 por tile) e `firmware/tile_pack.h` (leitor por setor, sem
  filesystem — nada de FAT para a queda de energia corromper). Formato documentado
  em `tools/tiles/README.md`.
- **`firmware/sd_bench/`** — mede na tela o que não tem resposta em documentação:
  se dá para inicializar o cartão com o **CS no expansor I2C** (EXIO4, não num
  GPIO), quanto custa de verdade ler um tile e um grid 3×3 por SPI, e **quanto o
  painel RGB sofre** durante a leitura (compara o tempo de `pushImage` com e sem
  cartão em uso — número, não impressão). Esses três valores decidem a
  arquitetura de render do mapa.
- **Testes que não exigem hardware**: `firmware/tdma_selftest/` e
  `firmware/tile_selftest/` rodam em qualquer ESP32 (sem rádio, GPS ou cartão);
  `tools/tiles/verify_reader.py` confere a conformidade entre o gravador em Python
  e o leitor em C. O selftest do TDMA já pegou um bug real antes de existir
  hardware: no modo beacon o índice do frame voltava a zero em cada re-âncora, o
  que faria o nó **parar de transmitir** depois do primeiro frame.

## Fase 3 — Duas telas Waveshare

Arquitetura final: duas telas **Waveshare ESP32-S3-Touch-LCD-7B** idênticas rodando o mesmo firmware (`firmware/grupo_ws/`), papel escolhido por toque (CRIAR SALA = líder, ENTRAR = seguidor).

- **Protocolo P2P** ponto-a-ponto (0x11 seguidor→líder com ACK, 0x12 líder→seguidor com bloco de trajeto).
- **Catch-up**: ao reconectar, o líder reenvia o trecho perdido em blocos até o seguidor alcançar — preenche o vão sem linha reta.
- **Bordas de aviso piscando**: vermelho (alerta), laranja (perda de sinal, com "REDUZA"/"SINAL PERDIDO"), amarelo ("FORA DO TRAJETO").
- **Alerta** pinta o trecho do caminho entre os dois carros de vermelho.
- **Trajeto real** (breadcrumb) em vez de linha reta; cores fixas roxo (a percorrer) / azul (percorrido).
- Buffer de trajeto ampliado para **~6 km** (PSRAM); busca do ponto mais próximo otimizada.
- Bring-up completo da Waveshare: GT911 (touch), CH32V003 (expansor IO), LoRaMESH, GPS.
- Reorganização do repositório em `firmware/`, `tools/`, `legacy/`, `docs/`.

## Fase 2 — Seguidor por LoRa (líder → seguidor)

- Líder (CYD) transmite posição + histórico; seguidor (GIGA) plota mapa estilo Waze.
- Núcleo compartilhado `trilha_core.h` (protocolo, temas, estado, geo).
- Modo Grupo no GIGA com temas (Rally/Tático/HUD), roster, distância por carro.
- Tentativa no Arduino GIGA descontinuada (módulo LoRa do GIGA falhou → migração para Waveshare).

## Fase 1 — Odômetro

- Projeto original de odômetro (`legacy/odometro/`), origem do sistema.
