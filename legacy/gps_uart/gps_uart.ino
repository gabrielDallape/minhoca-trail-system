/*
 * TESTE DO MKR GPS POR UART (modo SHIELD) - saida pela Serial USB
 *
 * No header, o MKR GPS fala UART (NMEA 9600), NAO I2C.
 * (O I2C dele so sai pelo conector ESLOV.)
 *
 * LIGACAO:
 *   Shield VCC      -> GIGA 3V3
 *   Shield GND      -> GIGA GND
 *   Shield 13 (RX)  -> GIGA 0  (RX0)   <- aqui sai o dado do GPS
 *   Shield 14 (TX)  -> GIGA 1  (TX0)
 *
 * Serial1 do GIGA = pino 0 (RX) / pino 1 (TX).
 * u-blox SAM-M8Q manda NMEA a 9600 baud continuamente, mesmo sem fix.
 */

unsigned long totalBytes = 0;
unsigned long ultimoStatus = 0;

void setup() {
  Serial.begin(115200);
  unsigned long s = millis();
  while (!Serial && (millis() - s < 4000)) { }
  Serial.println();
  Serial.println("=========================================");
  Serial.println(" TESTE GPS UART (Serial1 @ 9600)");
  Serial.println(" Mostrando o NMEA cru vindo do GPS:");
  Serial.println("=========================================");

  Serial1.begin(9600);
}

void loop() {
  // ecoa tudo que chega do GPS para o USB
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.write(c);
    totalBytes++;
  }

  // status a cada 3s
  if (millis() - ultimoStatus > 3000) {
    ultimoStatus = millis();
    Serial.print("\n[STATUS] bytes recebidos do GPS: ");
    Serial.print(totalBytes);
    if (totalBytes > 0) Serial.println("  -> GPS FALANDO! :)");
    else                Serial.println("  -> nada ainda (ver fiacao RX/TX)");
  }
}
