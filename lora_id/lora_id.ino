/*
 * LoRaMESH - TESTE 1: ler o ID LOCAL do modulo
 * ---------------------------------------------------------------------------
 * Objetivo: validar fiacao + energia + UART + modulo vivo, e descobrir o
 * ID de fabrica / UniqueID de cada modulo Radioenge LoRaMESH V4.
 * Precisa de UMA placa e UM modulo. Rode uma vez para cada modulo.
 *
 * Placa: Arduino GIGA R1 WiFi. Modulo na Serial2 (UART de COMANDO).
 *
 * LIGACAO (UART de comando do modulo -> GIGA Serial2) - CONFIRMADA FUNCIONANDO:
 *   Modulo VCC  -> GIGA 3V3      (NAO usar 5V!)
 *   Modulo GND  -> GIGA GND
 *   Modulo RX_1 (pino 2) -> GIGA pino 19   (GIGA transmite  -> modulo recebe)
 *   Modulo TX_1 (pino 3) -> GIGA pino 18   (modulo transmite -> GIGA recebe)
 *   (Obs: e' o inverso do que o #define SERIAL2_TX/RX sugere; confirmado no HW.)
 *
 * (Serial1 = pinos 0/1 continua livre para o GPS.)
 *
 * Abra o Serial Monitor a 115200. Deve imprimir LocalID / UniqueID.
 * A lib esta com debug ligado, entao ela tambem imprime os bytes TX/RX crus.
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial2);     // UART de comando do modulo

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" LoRaMESH - leitura do ID local (Serial2)");
  Serial.println("=========================================");

  Serial2.begin(9600);       // UART de comando: 9600 8N1
  delay(200);

  lora.begin(true);          // debug=true -> imprime TX/RX cru no USB

  Serial.print("LocalID       : "); Serial.println(lora.localId);
  Serial.print("UniqueID      : "); Serial.println(lora.localUniqueId);
  Serial.print("Senha (<=65535): "); Serial.println(lora.registered_password);
  Serial.println("-----------------------------------------");
}

void loop() {
  // Re-tenta a cada 3s para facilitar depurar a fiacao ao vivo.
  delay(3000);
  if (lora.localread()) {
    Serial.print("[OK]   LocalID=");  Serial.print(lora.localId);
    Serial.print("  UniqueID=");      Serial.println(lora.localUniqueId);
  } else {
    Serial.println("[FALHA] sem resposta do modulo -> confira RX<->TX cruzados, VCC(3V3)/GND e baud 9600");
  }
}
