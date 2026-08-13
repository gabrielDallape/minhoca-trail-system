// Pinos e chaves de recurso do MTS na placa ESP32-P4.
//
// COMO USAR: enquanto o E22 e o GPS nao estao soldados, deixe as tres chaves em 0.
// O firmware roda inteiro com dados simulados. Ao soldar, vire a chave daquele
// pedaco - nada mais muda de lugar, porque tudo que le hardware esta atras da
// interface do mundo.h.
#pragma once

#define MTS_TEM_RADIO  0     // E22-900M30S soldado e ligado
#define MTS_TEM_GPS    0     // ATGM336H (ou NEO-7M) ligado
#define MTS_TEM_SD     0     // cartao com os tiles

// ---------------------------------------------------------------- pinagem
// Do Estudo 2, conferida no esquematico. A MESMA nas tres placas P4 (5", 7" e 7B)
// - so o conector muda: header de 40 vias nas duas primeiras, JST-PH na 7B.
// Ver docs/MONTAGEM.md para qual pino do conector e cada um.
//
// SPI2 pelo conjunto IO MUX ALTERNATIVO: o principal (GPIO 6-11) esta todo ocupado
// nestas placas (I2C do toque, radio C6, audio). Estes quatro estao livres e
// expostos nas tres.
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
#define RF_PWR       22      // maximo do SX1262; o PA YP2233W soma +7,25 dB = 29,3
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
