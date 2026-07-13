/*
 * LoRaMESH - DIAGNOSTICO: varre baud rates e escuta QUALQUER resposta.
 * ---------------------------------------------------------------------------
 * Para cada baud, envia o comando "ler ID local" (0xE2) na Serial2 e escuta
 * ~800ms. Imprime em HEX tudo que voltar. Se algum baud responder, achamos
 * o baud certo. Se NENHUM responder em nenhum baud -> e' fiacao/energia.
 *
 * Modulo na Serial2:  GIGA 18(TX2)->modulo pino2(RX_1) ; GIGA 19(RX2)->modulo pino3(TX_1)
 *                     VCC->3V3 ; GND->GND
 */

const long BAUDS[] = {9600, 19200, 38400, 57600, 115200, 4800, 2400};
const int  NBAUDS  = sizeof(BAUDS) / sizeof(BAUDS[0]);

// Frame "local read" (id=0, cmd=0xE2, payload 00 00 00, CRC 15 B8) - o mesmo
// que a lib gera; enviamos cru para nao depender de nada.
const uint8_t CMD_LOCALREAD[] = {0x00, 0x00, 0xE2, 0x00, 0x00, 0x00, 0x15, 0xB8};

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }
  Serial.println();
  Serial.println("=================================================");
  Serial.println(" LoRaMESH - VARREDURA DE BAUD (Serial2, pinos 18/19)");
  Serial.println("=================================================");
}

void tryBaud(long baud) {
  Serial2.begin(baud);
  delay(60);
  while (Serial2.available()) Serial2.read();   // limpa lixo

  Serial2.write(CMD_LOCALREAD, sizeof(CMD_LOCALREAD));
  Serial2.flush();

  uint8_t buf[64];
  int n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 800 && n < (int)sizeof(buf)) {
    if (Serial2.available()) buf[n++] = Serial2.read();
  }

  Serial.print("baud ");
  Serial.print(baud);
  if (n == 0) {
    Serial.println("  -> nada");
  } else {
    Serial.print("  -> RESPOSTA (");
    Serial.print(n);
    Serial.print(" bytes): ");
    for (int i = 0; i < n; i++) {
      if (buf[i] < 0x10) Serial.print('0');
      Serial.print(buf[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
  }
  Serial2.end();
}

void loop() {
  Serial.println("\n--- rodada ---");
  for (int i = 0; i < NBAUDS; i++) tryBaud(BAUDS[i]);
  delay(2500);
}
