/*
 * ws_touch_diag - diagnostico do toque GT911 na Waveshare 7B (so serial).
 * Escaneia o I2C (8/9), confirma em qual endereco o GT911 responde (0x5D ou 0x14),
 * e le o status continuamente pra ver ao vivo se ele registra o toque.
 * A tela pode ficar estranha (nao inicializo o LovyanGFX aqui) - e' so diagnostico.
 * Grave pela porta UART/CH343 (switch UART1), CDCOnBoot=default.
 */
#include <Wire.h>

bool ioExt(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x24);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

// sequencia EXATA do grupo_ws/powerUpPanelAndTouch (a que funcionou):
// IO1=0 -> 100ms -> INT(GPIO4) baixo -> 100ms -> IO1=1 -> 200ms -> GPIO4 INPUT
void resetGT911(bool intLow) {
  ioExt(0x02, 0xFF); pinMode(4, OUTPUT);
  ioExt(0x03, 0x5C); delay(100);
  digitalWrite(4, intLow ? LOW : HIGH); delay(100);
  ioExt(0x03, 0x5E); delay(200);
  pinMode(4, INPUT);
}

void scan() {
  Serial.print("scan I2C:");
  for (uint8_t a = 0x08; a < 0x78; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
  }
  Serial.println();
}

uint8_t readReg8(uint8_t addr, uint16_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return 0xEE;   // NACK no endereco de registro
  Wire.requestFrom((int)addr, 1);
  if (!Wire.available()) return 0xEF;                  // sem dado
  return Wire.read();
}

uint8_t gtAddr = 0x5D;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== ws_touch_diag ===");
  Wire.begin(8, 9);
  Wire.setClock(400000);

  Serial.println("-- reset com INT baixo (esperado addr 0x5D) --");
  resetGT911(true);
  scan();

  // confirma qual endereco responde ao registro de status
  uint8_t s5d = readReg8(0x5D, 0x814E);
  uint8_t s14 = readReg8(0x14, 0x814E);
  Serial.printf("status inicial: 0x5D->0x%02X  0x14->0x%02X\n", s5d, s14);
  if (s5d != 0xEE && s5d != 0xEF) gtAddr = 0x5D;
  else if (s14 != 0xEE && s14 != 0xEF) gtAddr = 0x14;
  Serial.printf(">> usando GT911 em 0x%02X\n", gtAddr);

  // Product ID (0x8140, 4 bytes ASCII "911\0") - confirma leitura de dados reais + chip certo
  char pid[5] = {0};
  for (int i = 0; i < 4; i++) pid[i] = readReg8(gtAddr, 0x8140 + i);
  Serial.printf("Product ID (0x8140): '%c%c%c%c'  (hex %02X %02X %02X %02X)\n",
    pid[0]>=32?pid[0]:'.', pid[1]>=32?pid[1]:'.', pid[2]>=32?pid[2]:'.', pid[3]>=32?pid[3]:'.',
    pid[0],pid[1],pid[2],pid[3]);
  uint8_t fwL = readReg8(gtAddr, 0x8144), fwH = readReg8(gtAddr, 0x8145);
  uint8_t cfgVer = readReg8(gtAddr, 0x8047);
  uint8_t xL = readReg8(gtAddr, 0x8146), xH = readReg8(gtAddr, 0x8147);
  uint8_t yL = readReg8(gtAddr, 0x8148), yH = readReg8(gtAddr, 0x8149);
  Serial.printf("FW ver=0x%02X%02X  configVer=0x%02X  resX=%d resY=%d\n",
    fwH, fwL, cfgVer, xL|(xH<<8), yL|(yH<<8));

  // manda comando NORMAL/acordar (0x00 em 0x8040) caso esteja em sleep
  Wire.beginTransmission(gtAddr); Wire.write(0x80); Wire.write(0x40); Wire.write((uint8_t)0x00); Wire.endTransmission();
  delay(50);

  Serial.println("Agora TOQUE na tela - o status deve mudar (bit7=1, low nibble=nº de dedos).");
}

void loop() {
  uint8_t st = readReg8(gtAddr, 0x814E);
  uint8_t nt = st & 0x0F;
  int x = -1, y = -1;
  if ((st & 0x80) && nt > 0 && nt <= 5) {
    Wire.beginTransmission(gtAddr);
    Wire.write(0x81); Wire.write(0x50);        // ponto 1
    Wire.endTransmission(false);
    Wire.requestFrom((int)gtAddr, 5);
    if (Wire.available() >= 5) {
      Wire.read();
      uint8_t xl = Wire.read(), xh = Wire.read(), yl = Wire.read(), yh = Wire.read();
      x = xl | (xh << 8); y = yl | (yh << 8);
    }
  }
  // limpa o flag
  Wire.beginTransmission(gtAddr);
  Wire.write(0x81); Wire.write(0x4E); Wire.write((uint8_t)0);
  Wire.endTransmission();

  static unsigned long lastLog = 0, lastScan = 0;
  if (millis() - lastScan > 4000) {          // re-scan periodico (robusto p/ captura)
    lastScan = millis();
    scan();
    Serial.printf("addr usado=0x%02X | status 0x5D=0x%02X 0x14=0x%02X\n",
      gtAddr, readReg8(0x5D, 0x814E), readReg8(0x14, 0x814E));
  }
  if (millis() - lastLog > 500) {            // status SEMPRE, mesmo sem toque
    lastLog = millis();
    Serial.printf("status=0x%02X dedos=%d", st, nt);
    if (x >= 0) Serial.printf("  ponto=(%d,%d)", x, y);
    Serial.println();
  }
  delay(20);
}
