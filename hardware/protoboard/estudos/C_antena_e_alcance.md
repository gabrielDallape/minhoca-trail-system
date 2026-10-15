# Pesquisa C — RF do MTS: enlace, antena, ruido, configuracao e teste

Data: 2026-10-13. Nenhum arquivo do repo foi alterado. Scripts de analise em
`scratchpad/ruido.py` e `scratchpad/fit.py` (leem `dados_campo/`).

Legenda de procedencia, usada em todo numero:
- **[MEDIDO]** = sai dos logs do cartao de 2026-09-30 (`dados_campo/*_2026-09-30`) ou de `tools/medidas_antena.csv`
- **[DATASHEET]** = documento do fabricante ou norma
- **[PUBLICADO]** = medicao de terceiros com fonte
- **[ESTIMADO]** = conta minha; diz de onde vem cada parcela

---

## 0. O achado principal: o enlace de 09-30 estava limitado por RUIDO LOCAL, ~25 dB

### 0.1 O "-128 dBm no campo" nao e uma medida

- O valor -128 aparece **so** nos boots 263 e 265 da FaustoA: 7/7 e 202/202 amostras IGUAIS, com
  coordenadas no valor padrao (-23.5505, -46.6333 = sem fix, bancada) **[MEDIDO]**.
- No RadioLib instalado, `SX126x::getRSSI(false)` faz `GetRssiInst` e devolve `raw / -2`
  (`SX126x.cpp:699-700`). **-128 dBm (exibido de -127,5) = byte 0xFF.**
- 0xFF e o que o MISO devolve com o chip **sem os 5 V** ou fora de RX. Um ruido fisico oscila
  +/-1-2 dB entre amostras; 202 amostras identicas nao sao fisicas.
- A memoria do projeto confirma: "na bancada sem os 5 V do E22, o chip e alimentado pelos pinos
  de controle".
- **Sugestao ao firmware (nao aplicada):** tratar 0xFF/-127,5 como "sem amostra" e nao gravar
  linha N.

### 0.2 O piso real no carro, em 09-30, era de -87 a -95 dBm

Linhas `N` (RssiInst em slot livre) dos boots que andaram de carro **[MEDIDO]**:

| boot | movimento | N (mediana) | ruido implicito pelos pacotes (mediana) |
|---|---|---|---|
| FaustoA 261 | ~290 m (quase parado) | **-113** | -111,5 (n=28) |
| FaustoA 262 | 2,2 km de trilha | **-92** | -91,5 (n=42) |
| gino 19 | 37 m (parado) | **-89** | -84,7 (n=5) |
| gino 20 | — | -92 | -96,8 (n=11) |
| gino 21 | 2,2 km de trilha | **-88** | -87,4 (n=48) |

**O teste mecanico que separa "bug de medida" de "ruido real":**
- Para cada pacote com SNR < 10, calculei `ruido = RSSI_pacote - 10*log10(1 + 10^(SNR/10))`.
- Esse calculo usa **outro registrador** (GetPacketStatus) e **outro estimador** (o SNR do
  demodulador).
- Os dois metodos concordam dentro de 1-3 dB em todos os boots. O piso alto **e real, do ponto
  de vista do chip**.

### 0.3 O piso de ruido mandou no alcance

O pacote mais fraco que passou no CRC acompanha o piso **[MEDIDO]**:

| boot | piso | pacote mais fraco com CRC ok | cabecalho detectado (CRC ruim) |
|---|---|---|---|
| FaustoA 261 (silencioso) | -113 | **-121 dBm** (SNR -8,5) | -126 (SNR -12,5 a -13,5) |
| FaustoA 262 (ruidoso) | -92 | **-100 dBm** (SNR -5,2) | -108 (SNR -13,8) |
| gino 21 (ruidoso) | -88 | **-98 / -95 dBm** (SNR -10,5 / -8,2) | -102 (SNR -15,5) |

- **A mesma tela, no mesmo lugar, ouviu ~21 dB mais fundo no boot 261.**
- O limite de demodulacao (SNR de -10 a -12,5 em SF9) e o do datasheet. Quem mudou foi o piso.

### 0.4 O canal medido entre os carros (antenas dentro, 10 dBm no chip)

Ajuste por minimos quadrados, pacotes com CRC ok, distancias de 30 a 450 m **[MEDIDO]**:

```
gino recebe Fausto:  RSSI = +6,3 - 37,5*log10(d)   (n = 3,75; 45 pts)
Fausto recebe gino:  RSSI = +3,8 - 36,6*log10(d)   (n = 3,66; 55 pts)
300-400 m: RSSI mediano -87/-88, SNR mediano 0 a +5,5  ->  colado no piso de -88
```

- O expoente **n ~ 3,7** e o esperado para antenas baixas, a partir da distancia de cruzamento
  do modelo de dois raios (n = 4).
- Com piso -88 e limite ~-98, o ajuste da ~420 m na mediana. E exatamente o "nada acima de
  ~400 m".
- **A conta fecha com a medida.**

### 0.5 Como confirmar: a bancada de 10-08 e o boot 261

- **Bancada 10-08:** -85 dBm, coerente com os -88/-92 do carro. O ruido **anda com o aparelho**
  (tela, fonte, chicote), nao com o lugar.
- **Boot 261:** a unica pista contraria, e a mais valiosa. A FaustoA ficou ~25 min a -113, inclusive na
  tela da trilha (106-180 s), entao **nao e a pintura do mapa**.
- **Pergunta ao usuario:** o que estava diferente nesse boot? Fonte (power bank x adaptador de
  isqueiro), motor desligado, brilho, cabo USB.
- A gino ficou a -88 **parada** (boot 19), o que aponta para algo continuo, nao do motor em
  movimento.

---

## 1. Orcamento de enlace carro-carro

### 1.1 Parcelas

| Parcela | Valor | Procedencia |
|---|---|---|
| Sensibilidade SX1262, SF9/BW125 | -129 dBm (SF7 -124, SF10 -132, SF12 -137) | [DATASHEET] SX1261/2 |
| SNR minimo de demodulacao | SF7 -7,5 · SF8 -10 · **SF9 -12,5** · SF10 -15 · SF11 -17,5 · SF12 -20 dB | [DATASHEET]; confirmado em campo: cabecalho a -12,5..-15, CRC ok ate ~-10 |
| Sensibilidade **efetiva** = piso + SNRmin | -113 -> **~-122**; -88 -> **~-98** | [MEDIDO] tabela 0.3 |
| Potencia na antena, `pwr` 10 / 14 | 20 / 24 dBm pela tabela Ebyte (+10 dB); 17 / 21 dBm se o ganho for +7 dB | [DATASHEET] x [PUBLICADO] |
| Perda da lataria, antena dentro | ~9 dB de mediana por veiculo (painel/console); pior no assoalho | [PUBLICADO] Ofcom/LS telcom 2017, 800/900 MHz, varios veiculos |
| Duas latarias (carro-carro) | ~12-20 dB | [ESTIMADO] 2 x 6-10 dB |
| Distancia de cruzamento dos dois raios, `dc = 4*pi*ht*hr/lambda` | ht = hr = 1,0 m (dentro): **38 m**; 1,6 m (teto): **98 m** | [ESTIMADO] lambda = 0,328 m |
| Ganho de altura apos `dc`, `20*log10(ht*hr)` | de 1,0 para 1,6-1,7 m nos dois carros: **+8 a +9 dB** | [ESTIMADO] terra plana |
| Raio da 1a zona de Fresnel no meio do enlace, `8,66*sqrt(D_km/f_GHz)` | 400 m: **5,7 m**; 1 km: **9,1 m**; 2 km: 12,8 m | [ESTIMADO] |

**Sobre o ganho do PA.** O proprio `ESTUDO_E22.md` cita o S5NC medindo **+7 dB** com
instrumento (+8,11 a +29,37 dBm para +1 a +22 no chip). O `CLAUDE.md` adota os **+10** da tabela
Ebyte.

- Os dois numeros nao foram reconciliados.
- Para a ANATEL isso importa: `pwr 14` da 21 ou 24 dBm.
- Medir uma vez com atenuador e power meter (ou com RSSI relativo, ver secao 6) resolve.

### 1.2 Fresnel e altura

- Com antenas de 1-2 m e Fresnel de 6-13 m, **o MTS opera sempre na regiao de terra plana ou
  obstruida**. Nao existe visada limpa.
- Nessa regiao a potencia recebida cresce com o quadrado de `ht*hr`. Subir a antena do painel
  (~1,0 m) para o teto (~1,6-1,8 m) vale ~8-9 dB **so de altura**, nos dois carros juntos
  [ESTIMADO].
- Em morro/vale a difracao manda, e nenhuma antena resolve (PLANO_7B 6.2).

### 1.3 Alcance esperado

- **Metodo:** o modelo MEDIDO de 0.4 (antenas dentro, 20 dBm) e deslocado pelos dB de cada
  mudanca. Limite de projeto = piso - 5 dB, ou seja, SNR mediano de -5, cerca de 5 dB acima da
  falha (~90 % de entrega).
- **Fator de alcance** = `10^(dB/37)`, com o n = 3,7 medido.
- **Tudo alem de ~450 m e extrapolacao** de um ajuste feito de 30 a 450 m, em estrada com
  pouca obstrucao. Em trilha com mata, aplicar o excedente de vegetacao do PLANO_7B (ate 25 dB).

| Configuracao | Margem vs 09-30 | Alcance (~90 %) | Procedencia |
|---|---|---|---|
| Dentro, 20 dBm, piso -88 (o de 09-30) | 0 | **~300-400 m** | [MEDIDO] |
| Dentro, 24 dBm, piso -88 | +4 dB | ~400-500 m | [ESTIMADO] |
| Dentro, 20 dBm, **piso -113** | +25 dB | ~1,5-2 km em estrada | [ESTIMADO] |
| Teto, 20 dBm, piso -88 | +15..25 dB | ~1-1,7 km | [ESTIMADO] |
| **Teto, 20 dBm, piso -113** | +40..50 dB | ~5-10 km em plano limpo; na pratica limitado por relevo e curva | [ESTIMADO] |
| Teto, 24 dBm, piso -113 | +44..54 dB | idem: o relevo manda | [ESTIMADO] |

**Ordem de grandeza do retorno, por dB:**
- consertar o ruido: **~25 dB**;
- antena no teto: **~15-25 dB** (lataria x2 + altura + plano de terra - cabo);
- 20 -> 24 dBm: **4 dB**;
- SF9 -> SF10: **2,5 dB**.

---

## 2. Antena para o carro em 915 MHz

### 2.1 Tipos

| Tipo | Ganho | Diagrama vertical | Veredito |
|---|---|---|---|
| **1/4 de onda (~78-82 mm) com base magnetica no CENTRO do teto** | 2-3 dBi sobre plano grande; num teto finito o lobo sobe 10-30 graus | largo, perdoa morro/vale | **a escolha certa para trilha** |
| 5/8 de onda com bobina (~200 mm) | ~3 dBi (2-3 dBd) | lobo mais baixo que o 1/4, ainda largo (meia potencia +/-25-30 graus) | otima |
| Colinear 5-6 dBi (40-60 cm) | 5-6 dBi | +/-12-15 graus | boa em estrada plana; perde em desnivel |
| Colinear 8-9 dBi | 8-9 dBi | +/-7-8 graus | **nao**: em trilha os carros ficam fora do lobo |
| Pato de borracha sem plano de terra, dentro do carro | ~0 dBi efetivo ou menos | torto | e o de hoje |

Ganho de antena nao cria potencia: estreita o lobo.

- Carro 100 m acima a 500 m de distancia = **11 graus** de elevacao.
- Uma 8 dBi ja perde dB ai; uma 1/4 ou 5/8 nao.
- Para MTS, **3-5 dBi e o ponto doce** [ESTIMADO, geometria].

### 2.2 Produtos (verificar ficha e SWR ao receber)

- **Taoglas IS.05.B.301111**: ISM 433/868/915, montagem por furo, **3 m de CFD-200** (perda
  baixa) e SMA macho. E de fabricante serio, com ficha tecnica. [PUBLICADO: ficha do distribuidor]
- **Alpha Micro AMC-ANT-2J3915M-915**: base magnetica, 915 MHz, 3 m de LMR-100, SMA macho.
  [PUBLICADO: datasheet]
- **Tinkerforge "900 MHz SMA antenna external magnetic mount"**: magnetica, de fornecedor com
  ficha.
- **Generica de mercado livre "915 MHz 5 dBi base magnetica 3 m RG174"**: serve, **se** passar no
  NanoVNA (abaixo). No forum Meshtastic, antenas de serie foram medidas de **SWR 1,24 a 14**, e
  ha mag-mounts com a folha de terra da base mal ligada.

### 2.3 Como provar que a antena e de 915 (e nao 868/433)

- **NanoVNA** (o de ~R$ 300 do PLANO_7B), calibrado SOL **na ponta do cabo que vai no radio**.
  Medir a antena **montada no teto do carro**: a base magnetica usa o teto como plano de terra,
  e fora dele o SWR muda.
- **Aceite:** SWR < 1,5 em 915 (bom) ou < 2,0 (aceitavel; 11 % de potencia refletida, 0,5 dB).
- **Minimo deslocado:**
  - minimo em ~868 = antena europeia. Em 915 costuma dar SWR 2-3, ~1 dB de perda, e o PA de
    1 W reclama;
  - minimo fora de 800-1000 = antena de 433 ou de GSM.
- **Sem NanoVNA:** RSSI relativo com a outra tela a distancia fixa (secao 6). Antena boa e
  antena ruim diferem por 5-15 dB; da para ver.
- **Ajuste fino de 1/4 de onda:** cortar o chicote alguns mm por vez sobe a frequencia. Comecar
  em 82 mm.

### 2.4 Cabo coaxial

Perda em 900 MHz:

| Cabo | dB/m (900 MHz) | 1,5 m | 3 m | 5 m |
|---|---|---|---|---|
| RG174 (2,8 mm) | 0,6-0,85 | 0,9-1,3 | **1,8-2,6** | 3-4,3 |
| RG316 | ~0,56-0,8 | 0,8-1,2 | 1,7-2,4 | — |
| RG58 | 0,35-0,48 | 0,5-0,7 | **1,1-1,4** | 1,7-2,4 |
| LMR-200 / CFD-200 | ~0,23 | 0,35 | **0,7** | 1,15 |
| LMR-240 | ~0,17 | 0,26 | 0,5 | 0,85 |
| U.FL 1,13 mm (pigtail) | ~2-3 | — | — | — |

Fontes [PUBLICADO]: tabelas Raveon/w4rp/qsl.net (RG-174 18,1; RG-316 17,2; RG-58 10,6;
LMR-200 7,0; LMR-240 5,2 dB/100 ft em 900 MHz). A segunda fonte da RG-174 26,1 e RG-58 14,6.

**Regra:**
- **ate 3 m de RG58 ou LMR-200** do teto ate a caixa, ~1 dB;
- RG174 de 3 m custa 2-2,6 dB: aceitavel se for o que vem na antena, melhor encurtar para
  1,5-2 m;
- pigtail U.FL **<= 15 cm**.

### 2.5 Conectores: a armadilha SMA x RP-SMA

| | pino central | rosca |
|---|---|---|
| **SMA macho** (plug) | PINO | porca interna |
| **SMA femea** (jack) | FURO | rosca externa |
| **RP-SMA macho** | FURO | porca interna |
| **RP-SMA femea** | PINO | rosca externa |

- SMA com RP-SMA **rosqueia perfeitamente e nao liga o pino central**: pino com pino, ou furo
  com furo.
- Resultado: 20-40 dB de perda **e o PA de 1 W vendo circuito aberto**.
- Muitas antenas "WiFi" sao RP-SMA. Olhar o centro **antes** de rosquear.
- O pigtail do E22 e a antena precisam ser da mesma familia.

---

## 3. IPEX -> SMA x solda no pad 21

| | Perda | Vibracao | Comentario |
|---|---|---|---|
| U.FL/IPEX MHF1 + pigtail 1,13 mm 10-15 cm | ~0,1-0,2 dB do conector + ~0,3-0,4 dB do cabo [ESTIMADO, ficha tipica Hirose U.FL e cabo 1,13] | **fraca**: retencao de poucos newtons, vida de ~30 encaixes | **ja medido aqui:** p10 de -76 contra mediana de -63 a 5 m, e um "reencaixe resolveu" no CHANGELOG |
| Coax (RG316/RG178) soldado no pad 21, malha nos pads de GND vizinhos | ~0,1 dB; os 3-5 mm sem impedancia controlada sao 1-1,5 % de lambda, desprezivel | **boa**, se houver alivio de tensao (abracadeira/cola) a 1-2 cm | nao desencaixa |

**Recomendacao:**
- **(a)** IPEX: encaixar, travar com gota de cola quente ou silicone RTV neutro sobre o
  conector, e amarrar o cabo na placa a 1-2 cm. Um unico adaptador IPEX->SMA de antepara na
  caixa; dali para fora so SMA.
- **(b)** Pad 21: soldar RG316 direto com alivio de tensao.
- As duas ficam em ~0,3 dB.
- **Cuidado (nao verificado na ficha):** IPEX e pad 21 parecem ligados em paralelo no modulo.
  Conferir com ohmimetro (pino central do IPEX <-> pad 21). Se der 0 ohm, o lado nao usado
  **nao pode ter trilha** pendurada: um toco de alguns cm em 915 MHz desafina. A placa v4
  (IPEX, pad 21 livre) esta certa se o pad 21 nao tiver trilha.

---

## 4. Dessensibilizacao por ruido local

### 4.1 Quanto custa

| Comparacao | dB | Alcance com n = 3,7 medido | Em espaco livre (n = 2) |
|---|---|---|---|
| Piso -85 x -120 | 35 dB | **x8,8** | x56 |
| Piso -88 x -113 (o medido em 09-30) | 25 dB | **x4,7** | x18 |
| Piso -113 x teorico | ~0-6 dB | x1,0-1,5 | — |

**O piso teorico.**
- O ruido termico em 125 kHz e -174 + 51 = **-123 dBm**.
- Com o LNA do E22 na frente (BFU520, ganho estimado de 12-16 dB pelo S5NC, "chute sem medir"),
  o RssiInst le o piso **somado ao ganho do LNA**.
- O piso "limpo" lido pelo chip fica entre ~-106 e ~-115 conforme o ganho real.
- **Os -113 do boot 261 sao, portanto, um piso essencialmente limpo**, e -88 e 20-25 dB de
  ruido de fora do radio [ESTIMADO; o absoluto tem +/-5 dB pelo LNA nao medido, a diferenca
  nao].

### 4.2 Suspeitos

Em ordem de probabilidade [ESTIMADO, por mecanismo]:

1. **Adaptador USB de isqueiro / fonte chaveada barata.** Ruido de banda larga de chaveamento
   e do cabo que vira antena. Explica a variacao entre boots da mesma tela.
2. **Boost do backlight e step-down da 7B** (MP1658 no mesmo no do `Core_5V`, PLANO_7B 3.1). O
   radio puxa 5 V desse no e o chicote de 15 cm leva o ruido ao E22.
3. **Barramento MIPI-DSI e clocks do P4/PSRAM.** Harmonicas estreitas: aparecem como pente,
   sobem o RssiInst em algumas frequencias e em outras nao.
4. **ESP32-C6 (Wi-Fi) e USB.**
5. **GPS.** Pouco provavel: o NEO-7M e receptor, emite pouco.

E na placa v4: o **AP63205** (buck de ~1,1 MHz) fica a centimetros do E22. A ferrite e o banco
ajudam no conduzido, mas o irradiado so se mede (passo 0 abaixo, repetido na v4).

### 4.3 Procedimento de bancada, usando o proprio SX1262 como analisador

**Instrumento.**
- O firmware ja mede: uma linha `N,ms,utc,rssiInst,slot` por quadro (2 s) no cartao, e `ruido=`
  no status serial.
- O comando `m` (modo medida) so imprime o RSSI de **pacotes**. Para o piso, use `ruido=` e
  as linhas N.
- Condicao: a tela precisa ter ancora (sozinha ela "assume a ancora") e haver slot livre.
- **Cada leitura = mediana de >= 30 amostras (~1 min).** Diferenca >= 3 dB e real; a dispersao
  medida e +/-1-2 dB.

**Seguranca.** Um passo por vez, com `pwr` -9 (~1 dBm na antena) em toda a investigacao: o
aparelho continua transmitindo no seu slot.

| Passo | O que fazer | Se o piso cair muito |
|---|---|---|
| **0 (decisivo)** | Trocar a antena por uma **carga de 50 ohm SMA** (>= 1 W). Piso com carga x com antena | **carga ~-113 e antena ~-85**: o ruido e **irradiado** e entra pela antena; passos 1-6. **Carga tambem ~-85**: ruido **conduzido** (5 V, terra, acoplamento no chicote) ou medida errada; passos 3-5 com a carga no lugar |
| 1 | Antena em cabo de 1 m e depois 3 m, afastada da tela | distancia resolve: posicao da antena (teto!) |
| 2 | Backlight no minimo ou desligado; depois tela parada (sem repintar) | culpado: boost do backlight, ou DSI |
| 3 | Alimentacao: (a) power bank; (b) USB do PC; (c) adaptador de isqueiro; (d) **E22 numa fonte linear 5 V separada**, terra comum | (c) pior = adaptador; (d) melhor = ruido pelo `Core_5V` |
| 4 | Desconectar o GPS (fio e antena); tirar o cabo USB (log so no cartao) | culpado e o que saiu |
| 5 | Ferrite de clip no cabo USB e no chicote do E22, perto da placa | ferrite resolve: modo comum no cabo |
| 6 | Wi-Fi/OTA desligado (sem eco) | culpado: C6/esp_hosted |
| 7 (opcional) | Mudar `RF_FREQ_MHZ` para 903 / 910 / 920 / 927 em builds de teste | piso varia com a frequencia: harmonica de clock (estreita), muda-se o canal; piso igual: banda larga (fonte) |
| 8 (opcional) | **RTL-SDR (~R$ 200)** com antena ao lado: espectro 900-930 MHz | pente de linhas = clock; corcova larga = chaveamento |

**Criterio de pronto:** piso <= -110 com antena, na bancada e **no carro com o motor ligado**,
alimentado pelo caminho definitivo (placa v4 no 12 V).

---

## 5. Configuracao LoRa: SF, BW, ou antena e potencia?

### 5.1 Tempo no ar

`Tsym = 2^SF/BW`; payload `8 + ceil((8PL - 4SF + 28 + 16)/(4SF)) * 5` simbolos; preambulo
8 + 4,25; CRC ligado, cabecalho explicito, CR 4/5 [ESTIMADO pela formula da AN1200.13]:

| Config | Tsym | 68 B | 32 B | dB sobre SF9/125 | Cabe no slot de 500 ms (guarda 20 ms)? |
|---|---|---|---|---|---|
| **SF9/BW125** (hoje) | 4,1 ms | **411 ms** (o firmware mediu 417) | 226 ms | 0 | sim |
| SF10/BW125 | 8,2 ms | **739 ms** | 453 ms | +2,5 | **nao** com 68 B; com 32 B sobram ~27 ms |
| SF9/BW62,5 | 8,2 ms | ~822 ms | — | +3 | nao |
| SF11/BW125 (LDRO liga, Tsym >= 16 ms) | 16,4 ms | ~1,4 s | — | +5 | nao |

**Consequencias:**
- **SF10 com o pacote de trilha** exige slot de ~780 ms: 4 x 780 ms = 3,1 s, e como o quadro
  precisa dividir 18, vai para **6 s** (o lider 3x mais lento, o problema do v200). **Nao vale
  2,5 dB.**
- **BW62,5:** o teto ANATEL cai junto: `8 + 10*log10(62,5/3)` = **21,2 dBm**, contra 24,2 em
  BW125. Os +3 dB de sensibilidade sao devolvidos na potencia legal. **Ganho liquido zero.**
- **20 -> 24 dBm (`pwr` 14):** +4 dB, legal em BW125, custo zero de tempo no ar. Depende da
  fonte do E22 (PLANO_7B) e do ganho real do PA (+7 ou +10).
- **CR 4/5 -> 4/8:** ~1 dB de ganho de codigo em canal com desvanecimento, 1,6x de tempo no ar.
  Nao cabe.
- **Preambulo 8:** ok. Mais preambulo melhora a deteccao em SNR muito baixo marginalmente; nao
  e o gargalo.
- **CRC:** manter ligado. O log de CRC ruim com RSSI/SNR e o diagnostico mais util que existe.
- **LDRO:** correto desligado em SF9/BW125 (Tsym 4,1 < 16 ms). O RadioLib decide sozinho.
- **Boosted gain (registrador 0x08AC = 0x96):** **ja usado** (`radio.h:125`,
  `setRxBoostedGainMode(true)`). E reaplicado a cada `begin()` de recuperacao e persiste em
  standby/RX (so se perde em sleep, que o firmware nao usa). Para conferir em campo: ler 0x08AC
  na tela de diagnostico.
- **Patch 0x8B5** (Meshtastic/Heltec): ausente. Ganho nao documentado; baixa prioridade, so com
  medida antes/depois.

**Veredito:** em dB por custo, **ruido (25) >> antena no teto (15-25) >> potencia (4) > SF
(2,5, e custa o quadro)**. Nao mexer em SF/BW agora.

---

## 6. Bancada e teste de alcance controlado

### 6.1 Protoboard e bancada

- **Antena ou carga de 50 ohm sempre conectada** antes de energizar. Nunca pigtail solto: um
  U.FL solto e circuito aberto para o PA.
- **Nivel maximo na entrada:**
  - o SX1262 aguenta **+10 dBm absolutos** [DATASHEET, ja no MONTAGEM.md];
  - o LNA do E22 fica **na frente** e soma 12-16 dB;
  - a 1 m (FSPL 31,7 dB), 24 dBm chegam a **~-8 dBm** na antena receptora e **~+6 dBm** no chip.
    A 30 dBm, **~+12: acima do limite de dano**.
  - **Regra pratica:** manter <= -20 dBm na antena receptora. Ou seja, **`pwr` -9 (~1 dBm)
    na bancada**, ou >= 3 m a 20 dBm, ou atenuador de 30 dB em linha (SMA, >= 2 W).
  - Acima de ~-30 dBm o RSSI comprime e o SNR satura em +10..+12: **leituras de bancada a 1 m
    nao servem para comparar antenas**.
- **Fiacao do E22 na protoboard:** a protoboard e as jumpers sao para SPI e controle, **nunca
  para RF**. O 5 V do modulo com o banco 220 uF + 10 uF + 100 nF nos pads.

### 6.2 Teste de alcance reprodutivel com dois carros

**Desenho.**
- Uma variavel por vez (PROTOCOLO_FASE0).
- Comparar configuracoes por **dB na mesma distancia**, nao por "ate onde chegou". O dB precisa
  de 30 pacotes; o "ate onde" precisa de km e de sorte com o relevo.

**Local.** Estrada reta, pouco movimento, sem morro entre os pontos, uns 2 km. Marcos a cada
100 m (o GPS do log ja da a distancia; marcar so para parar no mesmo lugar). Mesmo trecho,
mesmo sentido, em toda rodada.

**Fixo.** Carro A parado no ponto 0, motor **ligado**, as duas telas com o mesmo firmware.

**Rodada** (~15 min):
1. 2 min parados lado a lado: piso N nas duas.
2. B vai parando em 100, 200, 300, 500, 700, 1000, 1500, 2000 m.
   - **60 s em cada parada** = 30 pacotes em cada sentido.
   - Volta pelo mesmo caminho em movimento (drive-by).
3. 2 min do piso com B de volta.

**Anotar em papel, por rodada:**
- hora;
- configuracao (antena dentro/teto, qual antena, cabo, `pwr`, fonte, motor, brilho);
- posicao exata da antena;
- clima.

O resto ja vai para o cartao: R com RSSI/SNR/CRC, N, T, E com GPS e UTC.

**Matriz minima, uma variavel por vez:**
- R1: como em 09-30 (dentro, 20 dBm) = linha de base;
- R2: mesma coisa com o ruido resolvido;
- R3: antena no teto;
- R4: `pwr` 14.

**Analise.** `tools/campo/cruza_mts.py` mais o ajuste `RSSI = A - 10n*log10(d)` e o piso N de
cada rodada.

- O ganho de cada mudanca e **delta-A em dB** com o mesmo n.
- O alcance previsto e `10^((A - (piso - 5))/(10n))`.
- A distancia de 50 % e 90 % de entrega confirma.
- **Assim fica separado o erro do modelo do erro do codigo:** se a entrega cair onde o SNR
  ainda e > -5, o problema nao e RF.

---

## 7. Recomendacao concreta

1. **Primeiro o ruido (R$ 0-50).**
   - Passo 0 da secao 4.3 (carga de 50 ohm) e depois a lista.
   - Descobrir o que o boot 261 da FaustoA tinha de diferente.
   - Meta: piso <= -110 no carro com motor ligado. Vale ~25 dB = **~x4-5 de alcance**.
2. **Antena no centro do teto.**
   - 1/4 ou 5/8 de onda, ou colinear de no maximo 5 dBi, com base magnetica e SMA macho
     (conferir que **nao** e RP-SMA).
   - **<= 3 m de RG58/LMR-200/CFD-200**; se vier RG174, encurtar para ~1,5-2 m.
   - SWR < 2 no NanoVNA, medida **com a antena no teto**.
   - Passar o cabo pela borracha da porta/porta-malas, sem esmagar.
   - Vale ~15-25 dB.
3. **Pigtail U.FL unico, <= 15 cm, colado e amarrado**, terminando em SMA de antepara na caixa;
   ou coax soldado no pad 21 com alivio de tensao.
4. **`pwr` 14 so depois de 1-3** e com a fonte do E22 resolvida. +4 dB, legal em BW125.
5. **Nao mexer em SF/BW.**
6. **Teste da secao 6.2** antes e depois de cada passo, registrando o dB ganho.

---

## Fontes

- Logs de campo: `dados_campo/fausto_2026-09-30/boot261,262,263,265.csv`,
  `dados_campo/gino_2026-09-30/boot19,21,230655_20.csv`; `tools/medidas_antena.csv`
- RadioLib instalado: `Documents/Arduino/libraries/RadioLib/src/modules/SX126x/SX126x.cpp:690-700`
  (getRSSI) e `SX126x_config.cpp:396-410` (boosted gain, 0x08AC)
- Repo: `firmware/mts/hal/radio.h`, `firmware/mts/core/params.h`, `firmware/mts/app/rede.h:469-517`,
  `docs/PLANO_7B.md`, `docs/ESTUDO_E22.md`, `docs/MONTAGEM.md`, `hardware/placa_radio_smt/README.md`
- Semtech SX1261/2 datasheet (sensibilidade e SNR por SF; +10 dBm de entrada maxima); Semtech AN1200.13 (tempo no ar)
- [S5NC/CDEBYTE_Modules](https://github.com/S5NC/CDEBYTE_Modules): LNA BFU520, 12-16 dB (estimado pelo autor), +7 dB medidos no PA
- [Ofcom / LS telcom, In-car mobile signal attenuation, 2017](https://www.lstelcom.com/fileadmin/content/lst/marketing/media/in-car-mobile-signal-attenuation-report.pdf): mediana de 8,9 dB (painel/console), varios veiculos, 800-2600 MHz
- [Aalto: Characterization of vehicle penetration loss](https://aaltodoc.aalto.fi/items/a2588a03-27fa-433d-a8d4-5c99996cf72f)
- Cabos: [Raveon](https://www.raveon.com/data_radio_info/?p=1036), [w4rp chart](https://eax.me/files/2019/11/coax-attenuation-chart-by-w4rp.pdf), [qsl.net co8tw](https://www.qsl.net/co8tw/Coax_Calculator)
- Antenas: [Meshtastic forum, car roof magnetic antennas](https://meshtastic.discourse.group/t/car-roof-magnetic-antennas/2451), [Meshtastic, antenna improved range](https://meshtastic.discourse.group/t/antenna-improved-range/227?page=6), [Taoglas IS.05](https://embeddedworks.net/product/ante1018/), [AMC-ANT-2J3915M](https://alphamicro.net/wp-content/uploads/2022/12/AMC-ANT-2J3915M-915-datasheet.pdf), [Tinkerforge 900 MHz magnetic](https://www.tinkerforge.com/en/shop/900mhz-sma-antenna-external-magnetic-mount.html)
- V2V LoRa: [Sensors 24(6):1801](https://www.mdpi.com/1424-8220/24/6/1801/htm), [JCOMSS 2025 V2V LoRa](https://jcoms.fesb.unist.hr/10.24138/jcomss-2025-0058/)
- [arXiv 2605.17063](https://arxiv.org/pdf/2605.17063): plato de RSSI perto do piso termico em presets Meshtastic
- ANATEL Ato 14.448/2017, item 10.3.3 (ja no PLANO_7B)
