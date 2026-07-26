/*
 * ws_lora_cfg - LE a configuracao completa do modulo LoRa (nao-destrutivo).
 * ---------------------------------------------------------------------------
 * Passo 1 do comissionamento do modulo NOVO (UniqueID 15794) na Waveshare 7B.
 * So LE: ID local, UniqueID, senha (password de rede) e config de radio
 * (BW/SF/CR) + classe/janela. Nada e' escrito. Serve pra comparar com o que
 * os modulos ja comissionados usam (senha 123) antes de gravar qualquer coisa.
 *
 * LoRa na Serial1 = RX GPIO44 / TX GPIO43 (header "UART2", switch em UART2).
 * Log pela USB NATIVA (CDCOnBoot=cdc), porta "USB" (VID 303A).
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial1);

void dump() {
  Serial.println("\n---------------- CONFIG DO MODULO ----------------");
  if (lora.localread()) {
    Serial.printf("  LocalID        : %u\n", lora.localId);
    Serial.printf("  UniqueID       : %u\n", lora.localUniqueId);
    Serial.printf("  Senha (rede)   : %lu\n", (unsigned long)lora.registered_password);
  } else {
    Serial.println("  [FALHA] localread sem resposta");
    return;
  }
  if (lora.read_config_bps()) {
    Serial.printf("  Radio BW/SF/CR : %u / %u / %u\n", lora.BW, lora.SF, lora.CR);
  } else {
    Serial.println("  [FALHA] read_config_bps");
  }
  if (lora.read_config_class()) {
    Serial.printf("  Classe/Janela  : %u / %u\n", lora.LoRa_class, lora.LoRa_window);
  } else {
    Serial.println("  [FALHA] read_config_class");
  }
  Serial.println("--------------------------------------------------");
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 5000) { }
  delay(600);
  Serial.println("\n===== ws_lora_cfg - leitura de config (nao escreve) =====");
  Serial1.begin(9600, SERIAL_8N1, 44, 43);   // LoRa
  delay(200);
  lora.begin(true);   // debug: mostra TX/RX cru
}

void loop() {
  dump();
  delay(5000);
}
