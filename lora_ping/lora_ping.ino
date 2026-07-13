/*
 * LoRaMESH - TESTE 2: PING RF do MASTER para o SLAVE pelo ar
 * ---------------------------------------------------------------------------
 * O modulo MASTER (ID 0) fica no GIGA (Serial2, comando). O SLAVE (ID 1) so
 * precisa de ENERGIA (VCC+GND) - o firmware do slave responde a comandos
 * remotos sozinho, sem precisar de outro Arduino.
 *
 * A cada ciclo o master pede o estado de um GPIO do slave (comando 0xC2
 * enderecado ao ID do slave). Se voltar uma resposta valida do ID certo,
 * o link de RADIO entre os dois modulos esta funcionando.
 *
 * ANTENA nos DOIS modulos antes de transmitir!
 *
 * Fiacao do MASTER (igual ao lora_id):
 *   VCC(4)->3V3  GND(1)->GND  RX_1(2)->GIGA 19  TX_1(3)->GIGA 18
 * SLAVE: so VCC(4)->3V3 e GND(1)->GND (pode ser o mesmo 3V3/GND do GIGA).
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial2);
const uint16_t SLAVE_ID = 1;

uint32_t ok = 0, fail = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }

  Serial.println();
  Serial.println("=========================================");
  Serial.println(" LoRaMESH - PING RF  (master -> slave ID 1)");
  Serial.println("=========================================");

  Serial2.begin(9600);
  delay(200);
  lora.begin(true);              // le o proprio ID (deve ser 0 = master)
  Serial.print("Este modulo LocalID = "); Serial.print(lora.localId);
  Serial.println(lora.localId == 0 ? "  (MASTER, ok)" : "  (ATENCAO: nao e' master!)");
  Serial.println("-----------------------------------------");
}

void loop() {
  // Pergunta ao slave (pelo ar) o estado do GPIO0.
  lora.get_gpio_status(SLAVE_ID, LoRa_GPIO0);

  uint16_t id = 0xFFFF;
  uint8_t  cmd = 0, payload[240], plen = 0;

  if (lora.ReceivePacketCommand(&id, &cmd, payload, &plen, 2500)) {
    if (id == SLAVE_ID && cmd == 0xC2) {
      ok++;
      Serial.print("[RF OK] resposta do slave ID=");
      Serial.print(id);
      Serial.print("  (ok=");  Serial.print(ok);
      Serial.print(" fail="); Serial.print(fail); Serial.println(")");
    } else {
      Serial.print("[?] resposta inesperada  id="); Serial.print(id);
      Serial.print(" cmd=0x"); Serial.println(cmd, HEX);
    }
  } else {
    fail++;
    Serial.print("[RF FALHA] slave nao respondeu pelo ar  (ok=");
    Serial.print(ok); Serial.print(" fail="); Serial.print(fail);
    Serial.println(") -> confira energia/antena do slave e ID");
  }

  delay(1500);
}
