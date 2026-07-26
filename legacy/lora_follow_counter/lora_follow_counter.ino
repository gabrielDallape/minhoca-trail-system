/*
 * LoRaMESH - SEGUIDOR (GIGA, master ID 0): recebe o CONTADOR do lider por RF.
 * ---------------------------------------------------------------------------
 * O modulo master recebe o "pacote de aplicacao" (cmd 0x10) pelo ar e o entrega
 * na UART de comando (GIGA Serial2, pinos 18/19 - ja fiada). Aqui so lemos e
 * imprimimos o contador, com a ID de origem (deve ser 1 = o lider).
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial2);          // GIGA Serial2 = D18(TX)/D19(RX)
const uint8_t APP_CMD_COUNTER = 0x10;

uint32_t rx_ok = 0, last = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" SEGUIDOR (GIGA/master) - recebendo contador");
  Serial.println("=========================================");

  Serial2.begin(9600);
  delay(200);
  lora.begin(true);
  Serial.print("Meu LocalID = "); Serial.print(lora.localId);
  Serial.println(lora.localId == 0 ? "  (master, ok)" : "  (ATENCAO: esperado 0)");
  Serial.println("Aguardando contador do lider...");
  Serial.println("-----------------------------------------");
}

void loop() {
  uint16_t id = 0xFFFF;
  uint8_t  cmd = 0, payload[240], plen = 0;

  if (lora.ReceivePacketCommand(&id, &cmd, payload, &plen, 3000)) {
    if (cmd == APP_CMD_COUNTER && plen >= 4) {
      uint32_t c = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                   ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
      rx_ok++;
      uint32_t perdidos = (last && c > last) ? (c - last - 1) : 0;
      last = c;
      Serial.print("[RECEBIDO] de ID="); Serial.print(id);
      Serial.print("  contador="); Serial.print(c);
      Serial.print("  (recebidos="); Serial.print(rx_ok);
      Serial.print(" perdidos_no_pulo="); Serial.print(perdidos);
      Serial.println(")");
    } else {
      Serial.print("[outro] id="); Serial.print(id);
      Serial.print(" cmd=0x"); Serial.print(cmd, HEX);
      Serial.print(" plen="); Serial.println(plen);
    }
  }
  // sem timeout-print pra nao poluir; se nao chegar nada, so fica quieto
}
