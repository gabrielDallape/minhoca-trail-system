/*
 * TESTE DE SAUDE do GIGA - NAO usa a tela.
 * Pisca o LED e imprime na serial. Se aparecer contando aqui, o MCU esta OK.
 */
void setup() {
  Serial.begin(115200);
  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDB, OUTPUT);
  digitalWrite(LEDR, HIGH);   // apagados (LED do GIGA e ativo em LOW)
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDB, HIGH);
}

unsigned long n = 0;
void loop() {
  n++;
  digitalWrite(LEDG, n % 2 ? LOW : HIGH);     // pisca verde
  Serial.print("GIGA VIVO E SAUDAVEL  contador=");
  Serial.print(n);
  Serial.print("  millis=");
  Serial.println(millis());
  delay(500);
}
