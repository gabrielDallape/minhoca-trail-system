/*
 * LoRaMESH - LIDER (MKR Zero, slave ID 1): envia um CONTADOR por RF ao seguidor.
 * ---------------------------------------------------------------------------
 * Usa "pacote de aplicacao" (comando 0x10, < 0x80): o payload sai integralmente
 * na UART de COMANDO do destino (o master). Nao precisa da UART transparente.
 *
 * 2a UART via SERCOM3: D6 (TX) / D7 (RX)  -> UART de comando do modulo (pinos 2/3).
 *   Modulo VCC(4)->MKR VCC(3,3V) ; GND(1)->GND ; RX_1(2)->D6 ; TX_1(3)->D7
 * ANTENA nos dois modulos (agora vai transmitir de verdade)!
 */
#include "LoRaMESH.h"
#include <wiring_private.h>

Uart Serial2(&sercom3, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2);
void SERCOM3_Handler() { Serial2.IrqHandler(); }

LoRaMESH lora(&Serial2);

const uint16_t DEST_MASTER = 0;   // seguidor = master ID 0
const uint8_t  APP_CMD_COUNTER = 0x10;
uint32_t counter = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" LIDER (MKR/slave) - enviando contador por RF");
  Serial.println("=========================================");

  Serial2.begin(9600);
  pinPeripheral(6, PIO_SERCOM_ALT);
  pinPeripheral(7, PIO_SERCOM_ALT);
  delay(200);

  lora.begin(true);
  Serial.print("Meu LocalID = "); Serial.print(lora.localId);
  Serial.println(lora.localId == 1 ? "  (slave, ok)" : "  (ATENCAO: esperado 1)");
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

  lora.PrepareFrameCommand(DEST_MASTER, APP_CMD_COUNTER, payload, 4);
  lora.SendPacket();                       // debug=true imprime o "TX: ..."
  Serial.print(">> enviado contador = "); Serial.println(counter);

  delay(1000);
}
