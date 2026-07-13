/*
 * RA8875 - descobre a fiacao SPI sozinho (bit-bang).
 * Testa TODAS as combinacoes dos 4 GPIOs (5,18,19,23) nos papeis SCK/MOSI/MISO/CS,
 * em modo 0 e modo 3, lendo o registrador 0 (deve dar 0x75 quando acerta).
 * Imprime a combinacao que funcionar.
 */
int PINS[4] = {5, 18, 19, 23};

uint8_t spiByte(int sck, int mosi, int miso, uint8_t out, bool cpol, bool cpha) {
  uint8_t in = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t bit = (out & 0x80) ? 1 : 0; out <<= 1;
    if (!cpha) {                       // sample na borda de subida (mode0)
      digitalWrite(mosi, bit);
      delayMicroseconds(3);
      digitalWrite(sck, !cpol);        // borda de amostragem
      delayMicroseconds(3);
      in = (in << 1) | (digitalRead(miso) & 1);
      digitalWrite(sck, cpol);
    } else {                           // mode3
      digitalWrite(sck, !cpol);
      digitalWrite(mosi, bit);
      delayMicroseconds(3);
      digitalWrite(sck, cpol);
      delayMicroseconds(3);
      in = (in << 1) | (digitalRead(miso) & 1);
    }
  }
  return in;
}

uint8_t readReg0(int sck, int mosi, int miso, int cs, bool cpol, bool cpha) {
  pinMode(sck, OUTPUT); pinMode(mosi, OUTPUT); pinMode(cs, OUTPUT); pinMode(miso, INPUT);
  digitalWrite(sck, cpol); digitalWrite(cs, HIGH);
  delayMicroseconds(10);
  // seleciona registrador 0: CMDWRITE(0x80) + 0x00
  digitalWrite(cs, LOW); delayMicroseconds(3);
  spiByte(sck, mosi, miso, 0x80, cpol, cpha);
  spiByte(sck, mosi, miso, 0x00, cpol, cpha);
  digitalWrite(cs, HIGH); delayMicroseconds(5);
  // le dado: DATAREAD(0x40) + le
  digitalWrite(cs, LOW); delayMicroseconds(3);
  spiByte(sck, mosi, miso, 0x40, cpol, cpha);
  uint8_t x = spiByte(sck, mosi, miso, 0x00, cpol, cpha);
  digitalWrite(cs, HIGH);
  return x;
}

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n=== RA8875 brute-force SPI (esperado reg0=0x75) ===");
  bool achou = false;
  for (int a = 0; a < 4; a++)
   for (int b = 0; b < 4; b++)
    for (int c = 0; c < 4; c++)
     for (int d = 0; d < 4; d++) {
       if (a==b||a==c||a==d||b==c||b==d||c==d) continue;  // todos distintos
       int sck=PINS[a], mosi=PINS[b], miso=PINS[c], cs=PINS[d];
       for (int m = 0; m < 2; m++) {
         bool cpol = (m==1), cpha = (m==1);   // modo 0 ou modo 3
         uint8_t x = readReg0(sck, mosi, miso, cs, cpol, cpha);
         if (x == 0x75) {
           Serial.printf(">>> ACHOU! SCK=GPIO%d  MOSI=GPIO%d  MISO=GPIO%d  CS=GPIO%d  (modo %d)  reg0=0x75\n",
                         sck, mosi, miso, cs, m*3);
           achou = true;
         }
       }
     }
  if (!achou) {
    Serial.println("NENHUMA combinacao leu 0x75.");
    Serial.println("-> algum fio esta SOLTO/desconectado (nao so trocado), ou sem GND comum, ou sem energia.");
    Serial.println("   Confira continuidade de cada fio JP1<->ESP e o GND comum.");
  } else {
    Serial.println("=== use a combinacao acima; eu monto o sketch final com esses pinos ===");
  }
}

void loop() { delay(2000); Serial.println("(fim da varredura)"); }
