/*
 * ws_lora_commission - COMISSIONA o modulo novo na rede do grupo.
 * ---------------------------------------------------------------------------
 * Escreve no modulo LoRa (UniqueID 15794) a SENHA 123 (igual aos comissionados
 * 13680/ID0 e 13683/ID1) e o LocalID = 2 (livre). Escreve UMA vez no setup,
 * relendo antes/depois pra confirmar. Nao mexe na config de radio (BW/SF/CR).
 *
 * LoRa na Serial1 = RX GPIO44 / TX GPIO43 (header "UART2", switch em UART2).
 * Log/gravacao pela USB NATIVA (CDCOnBoot=cdc), porta "USB" (VID 303A).
 *
 * SEGURO: toda config e' pela UART com fio, que independe de senha/rede.
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial1);

const uint16_t NOVO_ID = 2;
const uint32_t SENHA   = 123;

void dump(const char* quando) {
  Serial.printf("\n----- CONFIG (%s) -----\n", quando);
  if (lora.localread()) {
    Serial.printf("  LocalID  : %u\n", lora.localId);
    Serial.printf("  UniqueID : %u\n", lora.localUniqueId);
    Serial.printf("  Senha    : %lu\n", (unsigned long)lora.registered_password);
  } else {
    Serial.println("  [FALHA] sem resposta");
  }
  if (lora.read_config_bps())
    Serial.printf("  BW/SF/CR : %u / %u / %u\n", lora.BW, lora.SF, lora.CR);
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 5000) { }
  delay(600);
  Serial.println("\n===== ws_lora_commission =====");
  Serial1.begin(9600, SERIAL_8N1, 44, 43);
  delay(200);
  lora.begin(true);

  dump("ANTES");

  Serial.print("\n>> Setando SENHA=123 ... ");
  bool okP = lora.setpassword(SENHA);
  Serial.println(okP ? "OK" : "FALHA");
  delay(400);

  lora.localread();                 // garante localUniqueId carregado
  Serial.print(">> Setando ID=2 ... ");
  bool okI = lora.setnetworkId(NOVO_ID);
  Serial.println(okI ? "OK" : "FALHA");
  delay(400);

  dump("DEPOIS");
  Serial.println("\n(esperado DEPOIS: LocalID=2, Senha=123)");
}

void loop() {
  delay(6000);
  dump("estado atual");
}
