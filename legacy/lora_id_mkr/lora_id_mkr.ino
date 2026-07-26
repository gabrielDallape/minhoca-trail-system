/*
 * LoRaMESH - TESTE 1 no MKR ZERO: ler o ID local do modulo (lider/slave)
 * ---------------------------------------------------------------------------
 * O MKR Zero NAO tem Serial2 pronta como o GIGA. Aqui criamos uma 2a UART
 * via SERCOM3 nos pinos D6 (TX) e D7 (RX). A Serial1 (13/14) continua livre
 * para o MKR GPS Shield.
 *
 * D4/D5 NAO servem (SERCOM4 = cartao SD do MKR Zero). Use D6/D7 = SERCOM3.
 *
 * LIGACAO (UART de comando do modulo -> MKR, pelos pinos do shield):
 *   Modulo VCC  (pino 4) -> MKR VCC (3,3V)   (NAO usar 5V!)
 *   Modulo GND  (pino 1) -> MKR GND
 *   Modulo RX_1 (pino 2) -> MKR D6   (MKR transmite  -> modulo recebe)
 *   Modulo TX_1 (pino 3) -> MKR D7   (modulo transmite -> MKR recebe)
 *   (Se nao responder, inverta D6<->D7.)
 *
 * Abra o Serial Monitor a 115200. Deve imprimir o LocalID (esperado: 1).
 */
#include "LoRaMESH.h"
#include <wiring_private.h>   // pinPeripheral()

// 2a UART: SERCOM3, RX = D7 (PAD_3), TX = D6 (PAD_2)
Uart Serial2(&sercom3, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2);
void SERCOM3_Handler() { Serial2.IrqHandler(); }

LoRaMESH lora(&Serial2);

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" LoRaMESH no MKR ZERO - ler ID (SERCOM3 D6/D7)");
  Serial.println("=========================================");

  Serial2.begin(9600);
  // Redireciona os pinos D6/D7 para o SERCOM (essencial!)
  pinPeripheral(6, PIO_SERCOM_ALT);
  pinPeripheral(7, PIO_SERCOM_ALT);
  delay(200);

  lora.begin(true);           // debug=true -> imprime TX/RX cru

  Serial.print("LocalID       : "); Serial.println(lora.localId);
  Serial.print("UniqueID      : "); Serial.println(lora.localUniqueId);
  Serial.print("Senha (<=65535): "); Serial.println(lora.registered_password);
  Serial.println("-----------------------------------------");
}

void loop() {
  delay(3000);
  if (lora.localread()) {
    Serial.print("[OK]   LocalID=");  Serial.print(lora.localId);
    Serial.print("  UniqueID=");      Serial.println(lora.localUniqueId);
  } else {
    Serial.println("[FALHA] sem resposta -> confira D6<->D7 (inverta), VCC(3,3V)/GND e baud 9600");
  }
}
