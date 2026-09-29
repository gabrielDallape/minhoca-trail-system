#pragma once
// ============================================================================
//  PINOS - a placa: Waveshare ESP32-P4-WIFI6-Touch-LCD-7B, e so ela
// ============================================================================
//
// UM ALVO SO. O mts_campo carregava a tela de 5" (HX8394, 720x1280) como
// alternativa e isso custou uma tarde de confusao (o log de boot da 7B diz
// "ESP32_P4_EV" porque a Waveshare manteve a etiqueta do BSP da Espressif).
// Este firmware nao tem MTS_PLACA: e a 7B, com os pinos abaixo, conferidos no
// esquematico oficial e em docs/PINAGEM.md.
//
// CHAVES DE RECURSO. Enquanto o E22 ou o GPS nao estao no chicote, vire a chave
// e o firmware roda inteiro com o pedaco ausente desligado (a tela diz "--").

#define MTS_TEM_RADIO  1     // E22-900M30S no chicote (P3 + P1)
#define MTS_TEM_GPS    1     // GPS NEO-7M com PPS fiado no IO2
#define MTS_TEM_SD     1     // cartao: mapa em particoes brutas + diario na FAT
// WiFi do ESP32-C6 de bordo (esp_hosted), SO para atualizar pelo ar na garagem.
// DESLIGADO POR PADRAO EM CAMPO: o HTTPS do eco segurou o laco por centenas de
// ms e o vigia acusou TX preso onde nao havia (medido 2026-08-29); e o esp_hosted
// tem bug aberto em que a task SDIO trava e derruba o P4 (#167, #221). Ligar
// exige recompilar - de proposito, para nao ir ligado para a trilha por engano.
#define MTS_TEM_WIFI   0

// A CAIXINHA DO CARRO MONTA A TELA DE CABECA PARA BAIXO (pedido de 2026-08-25).
// O giro mora no offset_rotation do painel (hal/display.h): setRotation() em
// tempo de execucao e ignorado em silencio pelo caminho DSI. Imagem e toque
// giram juntos (a LovyanGFX soma o offset do toque ao do painel).
#define MTS_TELA_180   1

// MAPA "SO VIAS" (pedido de campo, 2026-08-26): so trilha, estrada e rua do
// cartao. Nada sai do cartao: virar para 0 devolve agua, divisas e POI.
#define MTS_MAPA_SO_VIAS  1

// ---------------------------------------------------------------- radio (SPI2)
// SPI2 pelo conjunto IO MUX ALTERNATIVO: o principal (GPIO 6-11) esta ocupado
// nesta placa (I2C do toque, UART do C6, audio). Estes saem no header P3.
#define PIN_LORA_NSS   28    // P3
#define PIN_LORA_MOSI  29    // P3
#define PIN_LORA_SCK   30    // P3
#define PIN_LORA_MISO  31    // P3
#define PIN_LORA_BUSY  49    // P1
#define PIN_LORA_DIO1  50    // P1  TxDone - e o pino que torna o TDMA possivel
#define PIN_LORA_NRST  51    // P1
#define PIN_LORA_TXEN  52    // P1  pull-down de 10k no chicote: sem ele o PA pode
#define PIN_LORA_RXEN   5    // P3  acordar em TX com a antena fora

// TXEN e RXEN sao os pads 7 e 6 do modulo - VIZINHOS, como BUSY/DIO1 (13/14), que
// ja vieram trocados uma vez. A suspeita de que estes tambem estivessem trocados
// foi TESTADA em 2026-08-28 e REFUTADA: com esta chave em 1 o RSSI entre duas
// telas na mesma bancada caiu de -68/-61 para -112/-116 dBm. A fiacao esta
// certa. NAO vire para 1 sem motivo novo - a 22 dBm isso injeta 800 mW no LNA.
#define RF_SWITCH_TROCADO 0

// ---------------------------------------------------------------- GPS (UART1)
#define PIN_GPS_RX      4    // P3  TXD do GPS entra aqui
#define PIN_GPS_TX      3    // P3  vai para o RXD do GPS
#define PIN_GPS_PPS     2    // P3  a ancora de tempo do TDMA

// ---------------------------------------------------------------- cartao (SDMMC)
// Pinos proprios da 7B e alimentacao pelo LDO INTERNO canal 4 (nao por GPIO como
// na 5"). O modo de falha de errar isto e timeout 0x107, "o cartao nao existe".
#define SD_PIN_CLK     43
#define SD_PIN_CMD     44
#define SD_PIN_D0      39
#define SD_PIN_D1      40
#define SD_PIN_D2      41
#define SD_PIN_D3      42
#define SD_LDO_CANAL    4

// NAO USAR: 46, 47 e 48 ficam no banco VDD_IO_5, alimentado pelo mesmo LDO que
// comuta o cartao entre 3,3 V e 1,8 V. 34-38 sao strapping; 35 tem o BOOT.
