/*
 * giga_lora_cfg - LE a configuracao completa do modulo LoRa do GIGA (13680).
 * ---------------------------------------------------------------------------
 * Igual ao ws_lora_cfg, mas no GIGA: LoRa em Serial2 (18/19). So LE, nao
 * escreve. Serve pra comparar a config de radio (senha + BW/SF/CR + classe)
 * com a do modulo da Waveshare (13683) e descobrir onde estao desalinhados.
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial2);

void dump() {
  Serial.println("\n---------------- CONFIG DO MODULO (GIGA) ----------------");
  if (lora.localread()) {
    Serial.print("  LocalID        : "); Serial.println(lora.localId);
    Serial.print("  UniqueID       : "); Serial.println(lora.localUniqueId);
    Serial.print("  Senha (rede)   : "); Serial.println((unsigned long)lora.registered_password);
  } else {
    Serial.println("  [FALHA] localread sem resposta");
    return;
  }
  if (lora.read_config_bps()) {
    Serial.print("  Radio BW/SF/CR : ");
    Serial.print(lora.BW); Serial.print(" / ");
    Serial.print(lora.SF); Serial.print(" / ");
    Serial.println(lora.CR);
  } else {
    Serial.println("  [FALHA] read_config_bps");
  }
  if (lora.read_config_class()) {
    Serial.print("  Classe/Janela  : ");
    Serial.print(lora.LoRa_class); Serial.print(" / ");
    Serial.println(lora.LoRa_window);
  } else {
    Serial.println("  [FALHA] read_config_class");
  }
  Serial.println("---------------------------------------------------------");
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 5000) { }
  delay(600);
  Serial.println("\n===== giga_lora_cfg - leitura de config (nao escreve) =====");
  Serial2.begin(9600);
  delay(200);
  lora.begin(true);
}

void loop() {
  dump();
  delay(5000);
}
