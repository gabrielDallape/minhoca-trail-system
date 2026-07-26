/*
 * GPS PIN SCAN (CYD) - descobre em qual pino o TXD do GPS esta soldado.
 * Testa IO27, IO22 e IO35 como RX (3,5s cada) e conta bytes + mostra amostra NMEA.
 * O pino que aparecer "TEM DADO" = onde o TXD esta ligado.
 */
int pins[3] = {27, 22, 35};

void setup(){
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SCAN DE PINO DO GPS (IO27 / IO22 / IO35) ===");
  Serial.println("O pino com dado = onde o TXD do GPS esta soldado.\n");
}

void loop(){
  for(int i=0;i<3;i++){
    Serial2.begin(9600, SERIAL_8N1, pins[i], -1);
    delay(250);
    while(Serial2.available()) Serial2.read();     // limpa buffer
    unsigned long t=millis(); int cnt=0; char smp[70]; int sp=0; bool dollar=false;
    while(millis()-t < 3500){
      while(Serial2.available()){
        char c=Serial2.read(); cnt++;
        if(c=='$') dollar=true;
        if(dollar && sp<69 && c!='\r' && c!='\n') smp[sp++]=c;
      }
    }
    Serial2.end();
    smp[sp]=0;
    Serial.print("IO"); Serial.print(pins[i]);
    Serial.print(pins[i]<10?"  : ":" : ");
    Serial.print(cnt); Serial.print(" bytes  ");
    if(cnt>0){ Serial.print("<== TEM DADO!  "); Serial.print(smp); }
    else     { Serial.print("(nada)"); }
    Serial.println();
  }
  Serial.println("-------------------------------");
}
