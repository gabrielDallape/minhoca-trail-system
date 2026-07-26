/*
 * ws_lora_align - ALINHA o radio do modulo da Waveshare (13683) ao GIGA.
 * ---------------------------------------------------------------------------
 * Diagnostico (lido com ws_lora_cfg/giga_lora_cfg):
 *   Waveshare 13683 : BW/SF/CR = 1/9/1  (BW250 / SF9 / CR4-5)
 *   GIGA      13680 : BW/SF/CR = 2/7/1  (BW500 / SF7 / CR4-5)  <- default de fabrica
 * Estavam em canais de radio diferentes -> nunca se ouviam.
 *
 * Este sketch escreve BW500/SF7/CR4-5 no 13683 (iguala ao GIGA) e LE DE VOLTA
 * pra confirmar. Roda uma vez; depois grave o ws_quiet de novo pra testar o ping.
 *
 * LoRa na Serial1 = RX GPIO44 / TX GPIO43. Log pela USB nativa (COM12).
 */
#include "LoRaMESH.h"

LoRaMESH lora(&Serial1);

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 5000) { }
  delay(600);
  Serial.println("\n===== ws_lora_align: BW500/SF7/CR4-5 no 13683 =====");
  Serial1.begin(9600, SERIAL_8N1, 44, 43);
  delay(200);
  lora.begin(true);
  lora.localread();
}

void loop() {
  static bool done = false;
  if (done) { delay(1000); return; }

  Serial.println("\n--- ws_lora_align ciclo ---");
  lora.read_config_bps();
  Serial.print("Antes  -> BW/SF/CR="); Serial.print(lora.BW);
  Serial.print("/"); Serial.print(lora.SF); Serial.print("/"); Serial.println(lora.CR);

  Serial.println("Escrevendo config_bps(BW500,SF7,CR4-5)...");
  bool ok = lora.config_bps(BW500, SF_LoRa_7, CR4_5);
  Serial.print("config_bps retornou: "); Serial.println(ok ? "true" : "false");

  delay(300);
  lora.read_config_bps();
  Serial.print("Depois -> BW/SF/CR="); Serial.print(lora.BW);
  Serial.print("/"); Serial.print(lora.SF); Serial.print("/"); Serial.println(lora.CR);

  if (lora.BW == BW500 && lora.SF == SF_LoRa_7 && lora.CR == CR4_5) {
    Serial.println(">>> ALINHADO (2/7/1). Grave ws_quiet e teste o ping.");
    done = true;
  }
  delay(4000);
}
