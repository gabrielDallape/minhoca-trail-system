/*
 * CYD (LIDER, master ID 0): envia um CONTADOR por RF ao seguidor (GIGA, slave ID 1).
 * ---------------------------------------------------------------------------
 * Teste de BANCADA da direcao master->slave (downlink), sem depender de GPS/tela.
 * Usa "pacote de aplicacao" (cmd 0x10, < 0x80): o payload sai na UART de comando
 * do destino. Envia ao DEST_FOLLOWER = 1 (o GIGA).
 *
 * LoRa no CYD: Serial1 RX=IO35, TX=IO22 (mesma fiacao do cyd_map).
 * ANTENA nos dois modulos!
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial1);

const uint16_t DEST_FOLLOWER  = 1;      // seguidor = GIGA (slave ID 1)
const uint8_t  APP_CMD_COUNTER = 0x10;
uint32_t counter = 0;

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println();
  Serial.println("=========================================");
  Serial.println(" CYD LIDER (master) - enviando contador -> GIGA (ID 1)");
  Serial.println("=========================================");

  Serial1.begin(9600, SERIAL_8N1, 35, 22);   // LoRa (comando)
  delay(200);
  lora.localread();
  Serial.print("Meu LocalID = "); Serial.println(lora.localId);
  Serial.println("-----------------------------------------");
}

void loop() {
  counter++;
  uint8_t payload[4] = {
    (uint8_t)(counter & 0xFF),
    (uint8_t)((counter >> 8) & 0xFF),
    (uint8_t)((counter >> 16) & 0xFF),
    (uint8_t)((counter >> 24) & 0xFF)
  };

  lora.PrepareFrameCommand(DEST_FOLLOWER, APP_CMD_COUNTER, payload, 4);
  lora.SendPacket();
  Serial.print(">> enviado contador = "); Serial.println(counter);

  delay(1000);
}
