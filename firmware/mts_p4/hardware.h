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
#define MTS_TEM_SD     0     // BANCADA 2026-09-08: fora da medicao de radio     // cartao com o mapa do Brasil (vetor + relevo)
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
#define MTS_VERSAO     22

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

#define RF_PWR       14      // 14 no chip = 24 dBm na antena.
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

// RECUO AUTOMATICO DE POTENCIA: 0 = desarmado (padrao desde 2026-09-08).
//
// Ele desce 4 dB por vez e GRAVA NA NVS, que sobrevive a regravacao do firmware. Um
// brownout de 30 s leva de 22 para 2 dBm permanentemente. E, enquanto esta armado,
// nenhuma escada de potencia e confiavel - o experimento mexe na variavel que mede.
//
// RELIGADO em 2026-09-08 para o teste de campo, JA COM os dois defeitos que a
// auditoria achou corrigidos:
//   1. o bloco de recuo mudou para DENTRO do 'if (begin() ok)' - um modulo que nem
//      responde nao conta mais 'preso' nem derruba a potencia;
//   2. o veredito passa pelo getDeviceErrors(): so recua quando o chip aponta
//      RAMPA_DO_PA ou nao aponta nada (corte por OCP). XOSC/PLL nao recuam, porque
//      recuar nao conserta TCXO.
//
// Em BANCADA, volte para 0: enquanto ele esta armado, nenhuma escada de potencia e
// confiavel, porque o experimento mexe na variavel que mede.
#define RADIO_RECUO_AUTO  1

// ------------------------------------------------------------------- TDMA
#define TDMA_SLOTS       8
// QUADRO DE 2 s por causa do SF9: o pacote passou de 58 para ~171 ms e nao cabe
// mais num slot de 125 ms. Com 2 s e 8 slots, cada slot tem 250 ms e sobram 55 ms
// de folga depois do pacote e da guarda.
//
// Custo: a posicao dos outros carros atualiza a cada 2 s em vez de 1. A 30 km/h
// isso e um ponto do trajeto a cada 17 metros - continua continuo no mapa. So
// acima de ~60 km/h o rastro comecaria a ficar tracejado, e isso nao e trilha.
//
// 2 divide 18, entao continua imune ao problema de grade GPS x UTC (ver
// tdma_core.h). NAO use 4 nem 5.
#define TDMA_FRAME_S     2   // 1, 2, 3, 6, 9 ou 18
#define TDMA_GUARD_US    20000UL

// -------------------------------------------------------------------- GPS
#define GPS_BAUD       9600
