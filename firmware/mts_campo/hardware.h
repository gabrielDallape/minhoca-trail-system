// Pinos e chaves de recurso do MTS na placa ESP32-P4.
//
// COMO USAR: enquanto o E22 e o GPS nao estao soldados, deixe as tres chaves em 0.
// O firmware roda inteiro com dados simulados. Ao soldar, vire a chave daquele
// pedaco - nada mais muda de lugar, porque tudo que le hardware esta atras da
// interface do mundo.h.
#pragma once

// ---------------------------------------------------------------- placa
// Qual das duas telas P4 esta sendo gravada. Elas NAO sao variacoes uma da outra:
// mudam painel, resolucao, orientacao, pinos e ate a polaridade do backlight.
//
//   5 = Waveshare ESP32-P4-WIFI6-Touch-LCD-5    HX8394   720x1280 retrato
//   7 = Waveshare ESP32-P4-WIFI6-Touch-LCD-7B   EK79007  1024x600 paisagem
//
// CUIDADO AO IDENTIFICAR A PLACA PELO LOG: a de 7" imprime "ESP32_P4_EV" no boot
// de fabrica, porque a Waveshare copiou o BSP da Espressif e manteve a etiqueta.
// Ela NAO e a EV board, e usar a pinagem da Espressif nela nao da erro nenhum -
// so nao aparece imagem. Ver LGFX_P4_LCD7.h.
#define MTS_PLACA      7

#define MTS_TEM_RADIO  1     // E22-900M30S soldado e ligado
#define MTS_TEM_GPS    1     // ATGM336H (ou NEO-7M) ligado
#define MTS_TEM_SD     1     // CAMPO: o cartao serve o mapa E recebe o log (registro.h)
// WiFi do ESP32-C6 de bordo (esp_hosted), SO para atualizar pelo ar e por
// conveniencia de garagem - nada critico do MTS depende dele (os bugs abertos
// do esp_hosted estao registrados no RETOMAR). A rede e escolhida NA TELA
// (configuracao > WIFI) e fica na NVS; o aparelho aparece na rede com o nome
// do carro. Exige esquema de particao com DOIS slots (ver tools/build.ps1).
// DESLIGADO PARA A MEDICAO (2026-09-08). O esp_hosted tem bug ABERTO em que a task
// SDIO trava e derruba o P4 (espressif/esp-hosted-mcu #167, #221), e o crash medido
// aqui em 18 dBm foi um deadlock de spinlock no tick do FreeRTOS - nao brownout.
// Enquanto o WiFi estiver ligado nao da para separar as duas causas.
#define MTS_TEM_WIFI   0

// VERSAO DO FIRMWARE para a atualizacao pela internet (wifi_ota.h + servidor
// mts-ota.vercel.app). A tela so baixa quando o manifesto anuncia numero MAIOR
// que este. Publicar = subir este numero e rodar tools\publica_ota.ps1, que le
// daqui (fonte unica), compila, calcula o MD5 e sobe para a Vercel.
// v101 (2026-09-28): potencia nunca muda sozinha; diario em toda tela; linhas
// R/T/N por pacote e ruido de fundo; caixa-preta que sobrevive ao watchdog;
// catch-up do trajeto do lider (4 slots de 500 ms). Ver CHANGELOG.
#define MTS_VERSAO     101

// A CAIXINHA DO CARRO MONTA A TELA DE CABECA PARA BAIXO (pedido de 2026-08-25).
// Girar AQUI vira a imagem E o toque juntos: a LovyanGFX soma a rotacao do
// painel ao offset_rotation do toque (Panel_Device::convertRawXY), entao nao ha
// segundo ajuste a fazer. O giro de 180 nao transpoe linhas (nada de trocar
// largura por altura), entao nao paga o custo de 719 ms que o giro de 90 pagava
// na tela de 5" - mas o numero do quadro impresso no serial e quem confirma.
//   0 = tela como a placa nasce   1 = montada invertida na caixinha
#define MTS_TELA_180   1

// MAPA "SO VIAS" (pedido de campo, 2026-08-26): desenhar APENAS trilha,
// estrada e rua do pacote do cartao - sem agua, sem area protegida, sem
// divisa, sem POI. Nada sai do cartao: virar esta chave para 0 e regravar
// devolve tudo. A rota do lider, o rastro e os carros nao passam por aqui.
#define MTS_MAPA_SO_VIAS  1

// ALIMENTACAO DO SLOT DO CARTAO. MEDIDO na p4_sd: sem levar este pino a nivel
// ALTO o sdmmc_init_ocr da timeout 0x107 e o cartao parece nao existir - nao ha
// mensagem dizendo "falta energia".
#define PIN_SD_PWR     45

// ---------------------------------------------------------------- pinagem
// CONFERIDA NO DESENHO DE MONTAGEM DAS DUAS PLACAS (agosto/2026). Os doze pinos
// abaixo sao os MESMOS nas duas e saem em conector nas duas - mas em conectores
// DIFERENTES, e e isso que decide o formato do chicote:
//
//   5"  ... um unico header J3 de 2x20. Todos os doze saem nele.
//   7B  ... DOIS headers: P3 (2, 3, 4, 5, 28, 29, 30, 31) e P1 (49, 50, 51, 52).
//
// A versao anterior deste comentario dizia "header de 40 vias nas duas primeiras,
// JST-PH na 7B". Era errado: veio de um estudo feito antes de eu descobrir que a
// placa de 7" e a 7B (EK79007, 1024x600), e nao a que eu tinha documentado.
// Ver docs/PINAGEM.md, que tem o mapa pino a pino das duas.
//
// SPI2 pelo conjunto IO MUX ALTERNATIVO: o principal (GPIO 6-11) esta todo ocupado
// nestas placas (I2C do toque, radio C6, audio). Estes quatro estao livres e
// expostos nas duas.
#define PIN_LORA_NSS   28
#define PIN_LORA_MOSI  29
#define PIN_LORA_SCK   30
#define PIN_LORA_MISO  31
#define PIN_LORA_BUSY  49
#define PIN_LORA_DIO1  50    // TxDone - e o pino que torna o TDMA possivel
#define PIN_LORA_NRST  51
#define PIN_LORA_TXEN  52    // pull-down de 10k no chicote: sem ele o PA pode
#define PIN_LORA_RXEN   5    // acordar em TX com a antena fora

// TXEN e RXEN sao os pads 7 e 6 do modulo - VIZINHOS, como BUSY/DIO1 (13/14),
// que ja vieram trocados uma vez. A suspeita de que estes tambem estivessem
// trocados foi TESTADA em 2026-08-28 e REFUTADA: com esta chave em 1, o RSSI
// entre duas telas na mesma bancada caiu de -68/-61 para -112/-116 dBm.
//
// Portanto: a fiacao esta CERTA e o PA esta ligando de verdade - aqueles ~50 dB
// de diferenca sao o proprio ganho do caminho PA/LNA. Se o PA nao entrasse na
// transmissao, inverter nao mudaria nada.
//
// A chave fica aqui, em 0, com o resultado escrito: e barato deixar, e evita que
// alguem gaste outra tarde levantando a mesma hipotese. NAO vire para 1 sem
// motivo novo - a 22 dBm isso injeta 800 mW na entrada do LNA.
#define RF_SWITCH_TROCADO 0

#define PIN_GPS_RX      4    // TXD do GPS entra aqui
#define PIN_GPS_TX      3    // vai para o RXD do GPS (configuracao)
#define PIN_GPS_PPS     2    // a ancora de tempo do TDMA

// NAO USAR: 46, 47 e 48 ficam no banco VDD_IO_5, alimentado pelo LDO interno - o
// mesmo que comuta o cartao SD entre 3,3 V e 1,8 V. Se o cartao trocar de tensao,
// esses tres vao junto. E 34-38 sao strapping; 35 tem o botao BOOT em paralelo.

// ------------------------------------------------------------------ radio
// Do Estudo 1. Cada numero tem motivo:
#define RF_FREQ      915.0f
#define RF_BW        125.0f
// SF9 EM VEZ DE SF7 (2026-08-30). O SF7 era o ajuste para 50 carros; a frota real
// tem tres. Trocamos capacidade por alcance, que e o que falta:
//
//   SF7 -> SF9   = +5 dB de sensibilidade   = ~1,4x de distancia em mata
//                  3,2x o tempo no ar (58 ms -> ~171 ms)
//
// Os 5 dB nao recuperam link bloqueado por morro - isso continua verdade, e por
// isso o comentario antigo desaconselhava. Mas com 70 dB de margem medidos na
// bancada e o alcance sendo o gargalo do projeto, 5 dB de graca valem mais que
// 42 slots que ninguem usa.
//
// Voltar e trivial: SF 7, frame 1 s, air-time 58000. Nada mais muda.
#define RF_SF        9
#define RF_CR        5
#define RF_SYNC      0x12
// GANHO REAL DO MODULO: +10 dB, nao +7,25.
//
// Os +7,25 sao do datasheet do CI de PA (YP2233W). A tabela oficial do MODULO MONTADO
// (manual E22-M V1.2, "RF output power control table", VCC 5,0 V, 900 MHz) da:
//   pedido  -1 ->  9 dBm     5 -> 15     11 -> 21     20 -> 30
//           2 -> 12         7 -> 18     14 -> 24
// Ou seja +10 dB constante. Consequencia: a frota a 6 dBm irradiava ~16 dBm, nao 13,25.
#define RADIO_GANHO_PA_DB  10

#define RF_PWR       10      // 10 no chip = 20 dBm na antena.
                             //
                             // ERA 14, E CAIU PARA 10 EM 2026-09-10 POR MEDIDA, NAO
                             // POR CAUTELA. Com este mesmo binario, a 14 dBm:
                             //   gino    ... estavel, preso=0, sinc=PPS, log gravando
                             //   FaustoA ... 'TX preso' e LP_WDT_RESET em ciclo
                             // Duas placas, um binario, comportamentos opostos: o
                             // limite e da FaustoA, nao do firmware. Testado sem o
                             // cartao tambem - trava igual, entao nao e consumo do SD.
                             // Enquanto a alimentacao dela nao for resolvida, a frota
                             // anda no degrau que as DUAS sustentam.
                             //
                             // POR QUE 14 E NAO 22: sao +8 dB sobre o que a frota
                             // irradia hoje (~1,8x de alcance em mata) e e o ponto em
                             // que a corrente ainda e sustentavel - acima disso e onde
                             // o radio trava. De quebra, 24,2 dBm e o teto legal em
                             // BW125 pelo Ato ANATEL 14.448 (8 dBm por 3 kHz).
                             //
                             // Subir so DEPOIS de a alimentacao estar medida, e subindo
                             // com o getDeviceErrors ligado para saber o que falha.
#define RF_PRE       8
#define RF_TCXO      2.2f    // manual E22-M V1.2 (2026). NAO usar XTAL=true: este
                             // modulo TEM TCXO no DIO3
#define RF_OCP       140.0f  // DEPOIS do begin(), que deixa 60 mA - e com 60 mA a
                             // saida trava em ~19 dBm, sem erro nenhum no log.
                             //
                             // CORRECAO 2026-09-08: a regra antiga dizia "depois do
                             // setOutputPower". Nao e isso. A RadioLib SALVA e RESTAURA
                             // o OCP em volta do SetPaConfig desde 2019 (commit 621da8a),
                             // entao a ordem entre setOutputPower e setCurrentLimit e
                             // indiferente. O que importa e vir depois do begin(), que
                             // grava 60 mA em tres pontos (SX126x.cpp:55, 94, 147).
                             //
                             // 140 e o TETO do setCurrentLimit: acima disso ele devolve
                             // RADIOLIB_ERR_INVALID_CURRENT_LIMIT e NAO APLICA NADA, em
                             // silencio. O datasheet permite 157,5 mA (0x3F x 2,5), mas
                             // so escrevendo o registrador 0x08E7 na mao.

// A POTENCIA NUNCA MUDA SOZINHA (regra do usuario, 2026-09-28: "isso nao pode
// acontecer"). NAO EXISTE MAIS RADIO_RECUO_AUTO nem "resgate de boot".
//
// Historico, para ninguem reinventar: ate a v100 havia DOIS mecanismos que baixavam
// a potencia 4 dB e GRAVAVAM NA NVS - o recuo do radio.h (3 TX presos seguidos) e
// o resgate do boot (reset por watchdog/panico/brownout). Os dois foram desarmados
// em 09-08 e religados em 09-10 porque, sem eles, a FaustoA entrava em ciclo de
// reinicio a 14 dBm. No campo de 09-28 o resgate disparou nas DUAS telas logo no
// comeco (watchdog, motivo 7) e o teste inteiro rodou a 6 dBm em vez de 10, sem
// ninguem saber. O instrumento mexeu na variavel que media, de novo.
//
// O que existe agora (radio.h, RADIO_MODO_SEGURO):
//   - a potencia vem da NVS e so muda por comando explicito ('p' no serial ou a
//     celula POTENCIA da tela de diagnostico);
//   - a caixa-preta (caixa.h) conta reinicios VIOLENTOS SEGUIDOS em RAM que
//     sobrevive ao reset. Apos RADIO_SEGURO_APOS reinicios seguidos, ESTE boot
//     roda com teto de RADIO_PWR_SEGURO dBm - em RAM, nunca na NVS - com faixa
//     vermelha permanente na tela e linha no diario. Um boot normal (POWERON) ou
//     um boot que sobreviva 2 min zera a serie e volta a potencia configurada.
//   - 'p' no serial ou a tela de diagnostico desligam o modo seguro na hora.
//
// Por que assim: o ciclo de reinicio infinito continua impossivel (o teto entra
// no terceiro boot seguido), mas a potencia gravada nunca e tocada, entao um
// aparelho que reiniciou tres vezes por um cabo solto nao sai da garagem capado
// para sempre - e o log DIZ que o teto entrou, em vez de esconder.
#define RADIO_SEGURO_APOS  3     // reinicios violentos seguidos antes do teto
#define RADIO_PWR_SEGURO   2     // teto do modo seguro (o piso antigo do recuo)

// ------------------------------------------------------------------- TDMA
//
// 4 SLOTS DE 500 ms (era 8 de 250 ms) - 2026-09-28, por causa do CATCH-UP.
//
// A conta que decide isto e a do air-time em SF9/BW125/CR4-5 (radioAirtimeUs, em
// radio.h, formula do datasheet + 6,5 ms de SPI e rampa medidos em SF7):
//
//   bytes   simbolos   air-time       o que e
//     16       28       171 ms       posicao (tdma_core.h)
//     23       38       212 ms       roster - JA NAO CABIA: o slot de 250 ms com
//                                    guarda de 20 e janela de TX dimensionada para
//                                    175 ms deixava o roster invadir ~37 ms do slot
//                                    vizinho (bug latente da v100)
//     27       43       233 ms       posicao + UM ponto de trajeto: nao cabe em 250
//     68       88       417 ms       posicao + 10 pontos de trajeto (catch-up)
//
// Com 8 slots de 250 ms o teto de pacote e ~25 bytes: nao ha catch-up possivel, e
// o roster ja estourava. Com 4 slots de 500 ms cabem 68 bytes com 63 ms de janela
// para o laco (500 - 20 de guarda - 417), que e a mesma folga que o pacote de 16 B
// tinha antes (55 ms). A frota tem 3 carros nos slots 0, 1 e 2; o 4o slot sobra e
// serve de "slot vazio" para medir o ruido de fundo (linha N do diario).
//
// O que NAO mudou: quadro de 2 s (2 divide 18, imune ao problema de grade GPS x
// UTC do tdma_core.h - NAO use 4 nem 5) e a taxa de posicao dos outros (a cada
// 2 s). O que se perdeu: 4 carros a mais que nunca existiram. Para um 5o carro,
// TDMA_FRAME_S 3 com 6 slots da os mesmos 500 ms de slot.
//
// ATENCAO ao gravar: um aparelho com slot >= 4 na NVS cai para 0 no carregaCfg e
// COLIDE com o slot 0. Confira o slot na tela de configuracao apos gravar.
#define TDMA_SLOTS       4
#define TDMA_FRAME_S     2   // 1, 2, 3, 6, 9 ou 18
#define TDMA_GUARD_US    20000UL

// CATCH-UP DO TRAJETO (radio.h / rota_core.h). Quantos pontos de trajeto viajam
// por pacote: cabecalho 24 B + primeiro ponto absoluto 8 B + 9 x 4 B (delta int16
// de lat/lon em 1e-5 grau, ~1,1 m) = 68 bytes. 10 pontos de 5 m = 50 m de trilha
// por quadro; um vao de 60 s a 30 km/h (500 m, 100 pontos) e preenchido em 10
// quadros de catch-up, que alternam com os de fluxo normal: ~40 s.
#define TRILHA_PONTOS_PKT  10

// -------------------------------------------------------------------- GPS
#define GPS_BAUD       9600
