# Compras — bancada do E22 no carro (2026-10-13)

Base: `hardware/protoboard/README.md` §4–6 e §8, `estudos/B_alimentacao_e_terra.md` §7,
`estudos/C_antena_e_alcance.md` §2–3.

Legenda: **[VERIFICADO]** = vi o anuncio/pagina na web nesta pesquisa (preco do dia, pode mudar).
**[SUGESTAO]** = so termo de busca / preco tipico, nao abri anuncio.
Precos do AliExpress sao os exibidos em pt.aliexpress.com; confira no carrinho se o imposto
(Remessa Conforme) ja esta incluso. Mercado Livre bloqueia leitura automatica: os links ML abaixo
sao paginas de busca/anuncio vistos so pelo titulo, sem preco confirmado.

---

## 1. Alimentacao

| Item | Termo de busca / modelo | Onde | Preco | Status |
|---|---|---|---|---|
| Conversor B (radio) | "TPS54560 step down buck modulo" (5–60 V, 5 A) | AliExpress: pt.aliexpress.com/item/1005009531244352.html (R$ 11,99, 77 vendidos); /1005013281272721.html (R$ 26,79, "baixa ondulacao") | R$ 12–27 | [VERIFICADO] |
| Conversor B alternativa | "LM2596HVS 60V 3A" — **exigir "HV" no titulo e no CI** | ML: produto.mercadolivre.com.br/MLB-2092751827 ("2x LM2596HVS 60V 3A") | ~R$ 15–30 o par | [VERIFICADO titulo] |
| Conversor A (tela) | "conversor 8-40V para 5V 3A a prova d'agua" / "buck 12V 24V 5V 3A waterproof" (modulo encapsulado em resina) | AliExpress: /1005012185712691.html (8–40 V → 5 V 4 A, R$ 22); /1005010452657560.html (8–40 V, R$ 21,36, 103 vendidos); Gyvrm /1005004429715793.html (R$ 33) | R$ 21–33 | [VERIFICADO] |
| — | Eletrogate "Step Down 6/24VDC para 5V 3A USB": **nao serve** (entrada max 24 V) e esta sem estoque | eletrogate.com | — | [VERIFICADO] |
| Derivador de fusivel | "derivador de fusivel" / "add a circuit fuse tap" — **conferir o padrao do carro antes** (mini/ATM, ATO, micro2, low-profile) | AliExpress: kit 16 pecas todos os padroes /1005009110950812.html (R$ 31); mini /1005007508625773.html (R$ 6,80); micro2 /1005009330499314.html (R$ 36) | R$ 7–36 | [VERIFICADO] |
| F1 | "porta fusivel lamina em linha" + "fusivel lamina 2A" (mesmo padrao do derivador; muitos derivadores ja trazem o slot do novo fusivel — ai basta o fusivel de 2 A) | ML / autopecas | R$ 5–15 | [SUGESTAO] |
| D1 TVS | **1.5KE33CA** (Onsemi) | Soldafria: soldafria.com.br/diodo-supressor-15ke33ca-p-7246.html — R$ 1,49 (1 un. em estoque) | R$ 1,49 | [VERIFICADO] |
| D2 Schottky | **SB560** / SR560 / MBR560 (DO-201AD) | ML: "10 pecas diodo schottky SB560" mercadolivre.com.br/.../MLBU775551785 | ~R$ 1–2/un | [VERIFICADO titulo] |
| L1 | "indutor toroidal 5026 22uH 3A" (nucleo amarelo/branco T50-26) | AliExpress: /1005010129705510.html (5 pcs R$ 3,77) | R$ 4–10 o kit | [VERIFICADO] |
| C entrada | eletrolitico 100 µF/50 V 105 °C + ceramico 1 µF e 100 nF 50 V | Bau/Soldafria/Eletrogate | R$ 1–3 | [SUGESTAO] |
| Filtro E22 | **BLM31PG601SN1L** (Murata, 1206, 600 Ω, 1,5 A) — LCSC C16902, US$ 0,10 em 5+ | lcsc.com (frete/imposto a parte). Nacional: Soldafria "Ferrite Bead Axial 100MHz" R$ 1,05 **sem estoque e sem ficha** (nao use sem saber a corrente) | US$ 0,10 | [VERIFICADO] |
| Filtro E22 alt. | "indutor blindado 2.2uH 3A CD54" ou SMD "NR6045 2R2" | AliExpress/ML | R$ 1–5 | [SUGESTAO] |
| C1 | **Panasonic EEU-FR1C221** (220 µF/16 V, 105 °C, 6,3×11) — LCSC C264061, US$ 0,11. Nichicon HE / Rubycon ZLH equivalentes | LCSC, Mouser BR, DigiKey BR. **"Nichicon/Rubycon" no ML costuma ser falsificado** (o achado foi Nichicon 85 °C) | US$ 0,11 | [VERIFICADO LCSC] |
| C2 | 10 µF X7R 25 V 1206 (Murata GRM31CR71E106KA12L) | ML: "kit capacitor 10uF 1206 X7R 25V"; Bau 0805 25 V sem estoque; LCSC | R$ 0,5–2 | [VERIFICADO titulo ML] |
| C3 | 100 nF X7R 50 V (perna ou 0805/1206) | Bau "Capacitor Ceramico 100nF/50V" | centavos | [VERIFICADO titulo] |
| C RF ×2 | **100 pF C0G/NP0** — o disco comum de loja NAO e C0G garantido; prefira SMD NP0 | Multcomercial: "0805 100pF 50V NPO" (multcomercial.com.br/0805-100-p-f-50-v-npo-...imp14165.html) | centavos | [VERIFICADO titulo] |
| Resistores | 330 Ω ×4, 1 kΩ ×5, 10 kΩ ×2, 1/4 W | Eletrogate "Resistor 330R/1K/10K 1/4W (10 unidades)" | ~R$ 1 por pacote | [VERIFICADO] |
| Fio 5 V radio | silicone AWG 20 vermelho+preto | ML: "Fio 20 AWG silicone 100cm verm + 100cm preto" MLB-3650759639 (~R$ 26) | R$ 22–26 | [VERIFICADO titulo/preco do buscador] |
| Fio 12 V | silicone AWG 18 (ou cabo automotivo 1 mm²) | ML "fio siliconado 18 AWG" | R$ 20–30 | [SUGESTAO] |
| XT30 | **Amass XT30UPB** (o "U" e o que trava melhor; "PB" = PCB) — ou XT30U para fio | AliExpress: /1005005882386545.html (R$ 15,50, genuino Amass); ML "conector XT30" | R$ 15–40 (5 pares) | [VERIFICADO] |
| Borne 12 V | KF301-2P 5,08 mm | AliExpress /1005013298738240.html (10–30 pcs R$ 8,31) | R$ 2–8 | [VERIFICADO] |
| Cabo da tela | USB-C↔C **5 A / 100–240 W (e-marker), 30 cm** | AliExpress /1005005608671361.html ("100W 5A" 15–30 cm, R$ 55); nacional: Baseus/Ugreen 0,5 m 100 W | R$ 20–55 | [VERIFICADO Ali] |
| Base | fenolite/fibra **cobreada virgem** 10×15 | MakerHero "Placa de Fenolite Simples" (a partir de R$ 2,40); Soldafria "fenolite virgem dupla 10x15"; Bau fibra 10×15 **sem estoque** (15×30 = R$ 84,90) | R$ 3–25 | [VERIFICADO] |
| Base alt. | "placa perfurada ilhada fibra 10x15" + fio de cobre nu 16–18 AWG | Bau/ML | R$ 10–20 | [SUGESTAO] |
| Ferrite clamp | snap-on 3,5–5 mm (sem ficha ate 1 GHz em nenhum anuncio barato) | AliExpress /1005006123482342.html (10 pcs R$ 28) | R$ 3/un | [VERIFICADO] |

**Alerta do conversor A:** o 1.5KE33CA grampeia em ate ~45 V no pulso de corrente maxima (datasheet,
Vc), acima dos 40 V da entrada do conversor de 8–40 V. O conversor B (60 V) aguenta; o A depende
do pico ser curto. Vale um modulo A de 60 V se aparecer barato, ou aceitar o risco.

---

## 2. Antena e RF

**Fato do mercado:** todas as magneticas baratas encontradas sao vendidas como **"868/915"** (banda
larga mal definida) e quase todas vem com **RG174 de 3 m** (perda 1,8–2,6 dB). Nenhuma barata com
RG58 de 3 m + SMA foi achada no AliExpress/ML. As de RG58 sao de radioamador (NMO/PL259).

| Opcao | Produto | Ganho / cabo / conector | Preco | Status |
|---|---|---|---|---|
| **A (fabricante com ficha)** | **2J Antennas 2J3915M-915** — DigiKey (digikey.com.br/.../2J3915M-915/22463199): 902–928 MHz, VSWR 1,5, magnetica | 3 dBi, 3 m, **SMA macho** | US$ 15,12 + frete/impostos (28 em estoque) | [VERIFICADO] |
| A' | 2J7515M-915 (DigiKey): VSWR 1,4, 2,3 dBi, 3 m, SMA macho | 2,3 dBi | ver DigiKey | [VERIFICADO] |
| **B (nacional, barata)** | **Usinainfo "Antena LoRa 915Mhz 5Dbi com Cabo 3m e Base Magnetica SMA Macho"** (AN5-915, ~20 cm, base 30 mm) — usinainfo.com.br/...-9173.html | 5 dBi, 3 m (tipo do cabo **nao informado**, provavelmente RG174), SMA macho | **R$ 15,96 PIX**, "ultimas unidades" | [VERIFICADO] |
| C (Ali, mais vendida) | "LoRa Meshtastic 4dBi 868/915MHz Magnetic Mount" /1005006316602304 | 4 dBi, 3 m, SMA macho | R$ 12 | [VERIFICADO titulo/preco] |
| C' | "868/915 6dBi magnetic 261 mm" /1005012638952802 | 6 dBi, 3 m, SMA macho | R$ 110 | [VERIFICADO titulo] |
| D (RG58) | "base magnetica NMO cabo RG58" + chicote NMO 1/4 onda cortado em ~82 mm + adaptador para SMA macho | RG58, robusta | R$ 100–200 | [SUGESTAO] |

**Evitar:** as de **7 dBi** (lobo estreito, §2.1 do estudo C), as "38 dBi Helium" (numero falso),
as de **12 dBi** do ML, e qualquer "GSM/868" sem a palavra 915. Compre **2 modelos diferentes**
(ex.: B e A) e escolha no NanoVNA, com a antena no teto (SWR < 2 em 915). Se o minimo cair em
~868, encurte o chicote alguns mm (so nas de 1/4 de onda).

**Pigtail IPEX → SMA femea de painel, 15 cm**

- AliExpress "Cabo coaxial RG178 6 polegadas IPEX U.FL para SMA femea" /1005012749333103 (R$ 18,99);
  LILYGO "SMA femea para U.FL RG178" /32841439452 (R$ 5,60, 5–50 cm). [VERIFICADO titulo/preco]
- **ARMADILHA confirmada:** o "Cabo Pigtail U.FL IPX com conector SMA Femea 15cm" da Eletrogate
  diz "sem pino central" = **e RP-SMA femea** (e esta sem estoque). Varios do ML idem ("para RP-SMA").
  Exija **"SMA femea (jack) com pino FURO e rosca externa"**; no RP-SMA femea ha um PINO no centro.
  Se ficar em duvida, compre o "SMA fêmea" e o "SMA macho" da mesma familia da antena.
- RG178 e melhor que o 1,13 mm (menos perda e mais robusto). [D, estudo C]

**Bancada de RF (uma unidade so)**

| Item | Produto | Preco | Status |
|---|---|---|---|
| Carga 50 Ω | "2W 6GHz 50 ohm SMA macho terminacao" — /4001218132980 (2 pcs) | R$ 8,39 | [VERIFICADO] |
| Atenuador 30 dB | "atenuador SMA 2W 30dB DC-6GHz" — /1005006075206869 (escolher 30 dB) ou /1005007082918637 | R$ 14–19 | [VERIFICADO] |
| | **2 W e o limite**: a 24 dBm (250 mW) ok; a 30 dBm (1 W) fica no limite, use p ≤ 14 | | |
| NanoVNA | **NanoVNA-H4** 10 kHz–1,5 GHz — AliExpress (1.000+ vendidos) ~R$ 214; Zeenko H4 R$ 200; ML "Analisador NanoVNA-H" MLB-3382745567 | R$ 200–260 | [VERIFICADO Ali] |

---

## 3. Conector do chicote de sinal (12 vias: 9 sinais + 3 GND)

| Opcao | Trava | Pratico? | Preco | Status |
|---|---|---|---|---|
| **Molex Micro-Fit 3.0 (43025/43020) 2×6 = 12 vias** | trava positiva forte, terminal crimpado, 5 A/via | **Recomendado** se o conector fica dentro das caixas: vende **chicote ja crimpado** AWG 20, 30 cm | AliExpress /1005008640751312.html (12P 30 cm, R$ 28); carcacas+terminais /32873345845 (R$ 10) | [VERIFICADO] |
| M12 A-coded 12 pinos, cabo blindado moldado | rosca, IP67, **malha** | Melhor eletricamente (blindado, passa pela parede da caixa), mais caro e grosso | /1005008671294224.html (R$ 31,80), /1005008105710008.html (2 m moldado, R$ 58,79); painel /1005012529904105 (R$ 42,61) | [VERIFICADO] |
| GX16 (ate 10 pinos) / GX20 (ate 12–15) | rosca | Barato, mas GX12 so vai ate 7 pinos e GX16 ate 10: **12 vias exige GX20**; solda em copo, sem blindagem | /1005005892954149.html (R$ 7,89) | [VERIFICADO] |
| JST GH 1,25 com trava | trava ok | fio AWG 26–28, crimpagem dificil | — | [SUGESTAO] |
| JST XH | trava fraca | nao | — | — |

**Recomendacao:** Micro-Fit 3.0 2×6 pre-crimpado (R$ 28) se a caixa do radio ficar perto da tela.
Se o cabo atravessar a cabine ou precisar sair da caixa de aluminio, **M12 12 pinos blindado
moldado** com conector de painel: a malha vai ao GND da caixa do radio. **DB15/HD15 com parafusos**
e uma terceira via barata e travada, encontrada em qualquer loja (Santa Efigenia) [SUGESTAO].

---

## 4. Caixas

| Item | Produto | Preco | Status |
|---|---|---|---|
| Caixa do radio (aluminio extrudado) | AliExpress "caixa eletronica de aluminio extrudada 110x80x45" /1005013045310864.html | R$ 16,35 | [VERIFICADO] |
| alternativa | "Caixa de Aluminio DIY 38x88x100mm" /1005012013016972.html | R$ 34,99 | [VERIFICADO] |
| nacional | Soldafria categoria "Caixa de aluminio"; ML "caixa aluminio projetos eletronicos" | R$ 40–90 | [VERIFICADO categoria] |
| Tela | A tela P4 (ESP32-P4-WIFI6-Touch-LCD-7B) **ja vem em caixa metalica** (cnx-software): basta suporte | — | [VERIFICADO] |
| Suporte da tela | "suporte tablet carro ventosa 7 a 12 polegadas" — ML MLB-3194941252; Amazon Linkstyle (7–10,5"), OHLPRO (7–13") | R$ 30–195 | [VERIFICADO titulos; faixa do buscador] |

---

## 5. Instrumentos

| Item | Produto | Preco | Status |
|---|---|---|---|
| Multimetro MIN/MAX | **UNI-T UT139C** (MIN/MAX/REL, True RMS; MIN/MAX leva ~350 ms por leitura — **nao pega o afundamento de ms do TX**, so derivas) | ~R$ 200 (SUGESTAO de preco) | [VERIFICADO funcao] |
| | UNI-T UT61E+ (22.000 contagens, MIN/MAX, USB) — AliExpress /1005003468500352 R$ 333 | R$ 315–370 | [VERIFICADO] |
| | **Aneng AN8008 NAO tem MIN/MAX** (segmentos no LCD, funcao inexistente — lygte-info); Aneng AN9002 tem | — | [VERIFICADO] |
| | UNI-T UT33 (A+/B+/C+/D+): sem MIN/MAX pelo que se sabe — nao confirmei | — | [SUGESTAO] |
| Medidor USB-C | **FNIRSI FNB48S** /1005008828103778 (R$ 168,59) ou **FNB58** /1005012356414737 (R$ 78,89, mais barato e mais novo) | R$ 79–170 | [VERIFICADO] |
| Termometro IR | **GM320** (-50 a 380 °C) — ML MLB-735694012, Amazon Honeytecs GM320 | ~R$ 50–80 | [VERIFICADO titulo] |

Nem multimetro nem FNB pegam o degrau de 620 mA em microssegundos: para isso, osciloscopio
(estudo B §5.1) ou o INA226 citado no estudo B.

---

## 6. Lista unica por carro

| # | Item | Qtd | R$ (estim.) |
|---|---|---|---|
| 1 | Placa cobreada 10×15 | 1 | 15 |
| 2 | Derivador de fusivel (padrao do carro) + fusivel lamina 2 A | 1 + 2 | 25 |
| 3 | TVS 1.5KE33CA | 2 (1 reserva) | 3 |
| 4 | SB560 | 2 | 4 |
| 5 | Toroide 22 µH 3 A | 1 (kit de 5) | 5 |
| 6 | Eletrolitico 100 µF/50 V 105 °C + ceramicos 1 µF e 100 nF 50 V | 1 de cada | 4 |
| 7 | Conversor A 8–40 V → 5 V 3 A encapsulado | 1 | 22 |
| 8 | Cabo USB-C 5 A 30 cm | 1 | 30 |
| 9 | Modulo TPS54560 | 1 (+1 reserva no total) | 20 |
| 10 | BLM31PG601SN1L (ou indutor 2,2 µH blindado) | 2 | 2 |
| 11 | EEU-FR1C221 220 µF/16 V | 2 | 2 |
| 12 | 10 µF X7R 25 V + 100 nF X7R | 2 + 2 | 3 |
| 13 | Resistores 330 Ω ×4, 1 kΩ ×5, 10 kΩ ×2; 100 pF C0G ×2 | — | 5 |
| 14 | Fio silicone AWG 20 (1 m V + 1 m P) | 2 m | 26 |
| 15 | Fio AWG 18 (V + P) | 2 m | 25 |
| 16 | XT30U (par) | 2 pares | 8 |
| 17 | Borne KF301-2P | 2 | 2 |
| 18 | Antena 915 magnetica SMA macho 3 m | 1 | 30 |
| 19 | Pigtail IPEX → SMA femea de painel RG178 15 cm | 2 (1 reserva) | 20 |
| 20 | Conector do chicote (Micro-Fit 2×6 pre-crimpado ou M12) | 1 par | 30 |
| 21 | Caixa de aluminio 110×80×45 | 1 | 20 |
| 22 | Suporte de tablet com ventosa | 1 | 70 |
| 23 | Ferrite clamp | 2 | 6 |
| | **Total por carro** | | **~R$ 380** |

**Dois carros: ~R$ 760.**

**Bancada (uma vez):** carga 50 Ω (R$ 9) + atenuador 30 dB 2 W (R$ 15–40) + NanoVNA-H4 (R$ 210)
+ FNB58 (R$ 80) ou FNB48S (R$ 170) + UT139C (~R$ 200) + GM320 (~R$ 60) = **~R$ 575–690**.
Opcional: 2ª antena de marca (2J3915M-915, ~US$ 15 + impostos ≈ R$ 150 entregue) para comparar.

**Total geral (2 carros + bancada): ~R$ 1.350–1.450**, sem frete de lojas nacionais e sem LCSC
(pedido de passivos na LCSC: ~US$ 5 de pecas + ~US$ 10–20 de frete + impostos; so vale juntando
ferrite, 220 µF Panasonic e 10 µF Murata para os dois carros e reservas).

---

## Fontes principais

- Soldafria 1.5KE33CA: https://www.soldafria.com.br/diodo-supressor-15ke33ca-p-7246.html
- Usinainfo antena 915 5 dBi 3 m: https://www.usinainfo.com.br/transmissores-e-receptores/antena-lora-915mhz-5dbi-com-cabo-3m-e-base-magnetica-sma-macho-9173.html
- DigiKey 2J3915M-915: https://www.digikey.com.br/en/products/detail/2j-antennas/2J3915M-915/22463199
- DigiKey 2J7515M-915: https://www.digikey.com.br/en/products/detail/2j-antennas/2J7515M-915/27615648
- Eletrogate pigtail (RP-SMA "sem pino central"): https://www.eletrogate.com/cabo-pigtail-u.fl-ipx-com-conector-sma-femea-15cm
- LCSC BLM31PG601SN1L C16902; EEUFR1C221 C264061
- AliExpress (buscas): tps54560, 915mhz-magnetic-antenna-sma-male-3m, ipex-to-sma-female-pigtail-rg178,
  sma-30db-attenuator-2w, sma-dummy-load-50-ohm-2w, nanovna-h, fnirsi-fnb48s, gx12, m12 12 pin,
  micro-fit 3.0 12 pin, aluminum enclosure, add-a-circuit fuse tap, toroidal 22uH 3A, usb-c 5A 30cm,
  amass xt30upb, kf301
- Aneng AN8008 sem MIN/MAX: https://lygte-info.dk/review/DMMAnengAN8008%20UK.html
- UT139C MIN/MAX: https://lygte-info.dk/review/DMMUNI-T%20UT139C%20UK.html
- Caixa metalica da tela P4: https://www.cnx-software.com/?p=163356
- ML (titulos): LM2596HVS MLB-2092751827; SB560 MLBU775551785; fio 20 AWG MLB-3650759639;
  NanoVNA-H MLB-3382745567; GM320 MLB-735694012; suporte tablet MLB-3194941252
