/*
 * ws_lora_scan - descobre em QUAIS pinos o header "UART2" (LoRa) esta ligado.
 * ---------------------------------------------------------------------------
 * Contexto: na Waveshare 7B o switch de cima seleciona UART1<->UART2. O LoRa
 * esta no header "UART2"; a porta USB-C de gravacao/log e' a "UART1". Quando o
 * switch vai pra UART2, o log pela porta UART1 some -> a gente fica cego.
 *
 * SOLUCAO: logar pela USB NATIVA do ESP32-S3 (porta USB-C "USB", NAO a "UART"),
 * que nao passa pelo switch. Assim o log aparece com o switch em qualquer posicao.
 * Por isso este sketch deve ser gravado com "USB CDC On Boot: ENABLED" e lido
 * pela COM da porta USB nativa (VID 303A).
 *
 * O sketch varre varios pares (RX,TX) e diz em qual o modulo LoRa responde
 * (UniqueID != 0). Mantem o GPS na Serial2 (RX=6) como referencia de sanidade.
 *
 * ---- GRAVACAO ----
 * Plugue a porta USB-C "USB" (nativa). FQBN esp32s3 com CDCOnBoot=cdc.
 */
#include "LoRaMESH.h"
#include <TinyGPSPlus.h>

struct Pair { int rx, tx; const char* name; };
Pair PAIRS[] = {
  {44, 43, "GPIO44(RX)/43(TX)  [UART0 = header UART2?]"},
  {43, 44, "GPIO43(RX)/44(TX)  [invertido]"},
  {15, 16, "GPIO15(RX)/16(TX)  [RS485 / palpite orig]"},
  {16, 15, "GPIO16(RX)/15(TX)  [invertido]"},
  {17, 18, "GPIO17(RX)/18(TX)"},
  {18, 17, "GPIO18(RX)/17(TX)"},
};
const int NP = sizeof(PAIRS) / sizeof(PAIRS[0]);

LoRaMESH lora(&Serial1);
TinyGPSPlus gps;

bool tryPair(int rx, int tx) {
  Serial1.end();
  delay(40);
  Serial1.begin(9600, SERIAL_8N1, rx, tx);
  delay(150);
  for (int i = 0; i < 3; i++) {
    if (lora.localread() && lora.localUniqueId != 0) return true;
    delay(250);
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && millis() - s < 5000) { }
  delay(600);
  Serial.println();
  Serial.println("================================================");
  Serial.println(" ws_lora_scan - varredura de pinos do LoRa");
  Serial.println(" (log pela USB NATIVA, imune ao switch)");
  Serial.println("================================================");
  Serial2.begin(9600, SERIAL_8N1, 6, -1);   // GPS (referencia)
}

void loop() {
  Serial.println("\n--- varrendo pares (RX,TX) ---");
  int found = -1;
  for (int i = 0; i < NP; i++) {
    Serial.printf("  %-42s ... ", PAIRS[i].name);
    if (tryPair(PAIRS[i].rx, PAIRS[i].tx)) {
      Serial.printf("ACHOU!  UniqueID=%u  LocalID=%u\n", lora.localUniqueId, lora.localId);
      found = i;
      break;
    }
    Serial.println("nada");
  }

  // GPS: referencia de que o sketch roda e a placa esta viva
  unsigned long t = millis();
  while (millis() - t < 1500) { while (Serial2.available()) gps.encode(Serial2.read()); }
  Serial.printf("[GPS ref] NMEA_chars=%lu sats=%d\n",
    gps.charsProcessed(),
    gps.satellites.isValid() ? (int)gps.satellites.value() : -1);

  if (found >= 0) {
    Serial.printf(">>> LoRa RESPONDE em: %s <<<\n", PAIRS[found].name);
    delay(5000);
  } else {
    Serial.println(">>> LoRa mudo em TODOS os pares -> conferir LED/energia do modulo, antena, ou switch em UART2. <<<");
    delay(2500);
  }
}
