/*
 * ws_quiet - Waveshare ESP32-S3-Touch-LCD-7B
 * ---------------------------------------------------------------------------
 * Sketch NEUTRO para teste de radio (lora_ping). NAO toca no modulo LoRa:
 * o modulo (UniqueID 13683 / LocalID 1) fica so energizado pela placa e
 * responde sozinho ao ping remoto do master (GIGA), sem colisao de comandos.
 *
 * Enquanto isso roda aqui, grave o lora_ping no GIGA e observe se o ping
 * volta [RF OK]. Se voltar, o link de radio 13680<->13683 esta bom.
 *
 * Nao inicializa Serial1 (LoRa em 44/43). So mantem a placa viva.
 */
void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 3000)) { }
  Serial.println();
  Serial.println("=========================================");
  Serial.println(" ws_quiet: modulo LoRa energizado e QUIETO");
  Serial.println(" (grave o lora_ping no GIGA e veja o ping)");
  Serial.println("=========================================");
}

void loop() {
  static unsigned long t = 0;
  if (millis() - t > 3000) {
    t = millis();
    Serial.println("ws_quiet vivo (LoRa livre para responder ao ping do GIGA)");
  }
}
