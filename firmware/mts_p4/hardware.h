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
#define MTS_TEM_SD     1     // cartao com o mapa do Brasil (vetor + relevo)
// WiFi do ESP32-C6 de bordo (esp_hosted), SO para atualizar pelo ar e por
// conveniencia de garagem - nada critico do MTS depende dele (os bugs abertos
// do esp_hosted estao registrados no RETOMAR). A rede e escolhida NA TELA
// (configuracao > WIFI) e fica na NVS; o aparelho aparece na rede com o nome
// do carro. Exige esquema de particao com DOIS slots (ver tools/build.ps1).
#define MTS_TEM_WIFI   1

// VERSAO DO FIRMWARE para a atualizacao pela internet (wifi_ota.h + servidor
// mts-ota.vercel.app). A tela so baixa quando o manifesto anuncia numero MAIOR
// que este. Publicar = subir este numero e rodar tools\publica_ota.ps1, que le
// daqui (fonte unica), compila, calcula o MD5 e sobe para a Vercel.
#define MTS_VERSAO     5

// A CAIXINHA DO CARRO MONTA A TELA DE CABECA PARA BAIXO (pedido de 2026-08-25).
// Girar AQUI vira a imagem E o toque juntos: a LovyanGFX soma a rotacao do
// painel ao offset_rotation do toque (Panel_Device::convertRawXY), entao nao ha
// segundo ajuste a fazer. O giro de 180 nao transpoe linhas (nada de trocar
// largura por altura), entao nao paga o custo de 719 ms que o giro de 90 pagava
// na tela de 5" - mas o numero do quadro impresso no serial e quem confirma.
//   0 = tela como a placa nasce   1 = montada invertida na caixinha
#define MTS_TELA_180   1

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
#define RF_SF        7       // o unico SF que sustenta 50 carros com frame de 4 s;
                             // subir para SF9 custa 3,2x o tempo no ar e compra so
                             // ~5 dB, que nao recupera link bloqueado por morro
#define RF_CR        5
#define RF_SYNC      0x12
// ATENCAO - POTENCIA DE BANCADA. Em 22 dBm (29 dBm com o PA) o pico de TX passa
// de 600 mA e, alimentada so pelo USB do PC SEM o capacitor de bulk de 470 uF,
// a placa AFUNDA e reseta em loop no primeiro pacote (rst:0x1 POWERON ~2 s apos
// o painel subir - medido na bancada em 2026-08-23, tela B). Para o CAMPO:
// montar o capacitor junto ao modulo, alimentar direito, e voltar para 22.
#define RF_PWR       22      // ESCADA DE POTENCIA em andamento (2026-08-24): 2 passou
                             // no USB do PC; 10 e o degrau atual com fonte de tomada;
                             // se estavel, sobe para 22 (maximo; PA soma +7,25 = 29,3)
#define RF_PRE       8
#define RF_TCXO      2.2f    // manual E22-M V1.2 (2026). NAO usar XTAL=true: este
                             // modulo TEM TCXO no DIO3
#define RF_OCP       140.0f  // DEPOIS do setOutputPower. Com o default de 60 mA a
                             // saida trava em ~19 dBm, sem erro nenhum no log

// ------------------------------------------------------------------- TDMA
#define TDMA_SLOTS       8
#define TDMA_FRAME_S     1   // 1, 2, 3, 6, 9 ou 18. NAO use 4 nem 5 sem fixar a
                             // grade de tempo em UTC - ver tdma_core.h
#define TDMA_GUARD_US    20000UL

// -------------------------------------------------------------------- GPS
#define GPS_BAUD       9600
