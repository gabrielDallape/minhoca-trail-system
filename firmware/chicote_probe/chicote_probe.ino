/*
 * SONDA DO CHICOTE v2 - descobre QUAL fio esta rompido, sem multimetro.
 *
 * Historia: nas duas telas 7B o begin() do SX1262 devolve -2 (chip nao
 * encontrado), mas o BUSY responde ao reset - o chip esta ALIMENTADO e VIVO.
 * O radio ja funcionou nestas placas (o air-time de 58 ms foi medido nelas),
 * entao algum fio parou de fazer contato desde entao.
 *
 * A v1 so olhava os pinos passivamente, e isso NAO prova nada para o SPI:
 * MOSI/SCK/NSS sao entradas de alta impedancia no radio e MISO fica em
 * tri-state com NSS alto - todos seguem os pulls MESMO conectados. O unico
 * veredicto passivo valido e o do GPS (o TX dele e dirigido em repouso).
 *
 * A v2 fala com o chip por SPI de bit-bang e sobe uma ESCADA, cada degrau
 * isolando um fio:
 *
 *   degrau 1  reset por NRST, BUSY cai?          -> NRST+BUSY+alimentacao
 *   degrau 2  GetStatus com MISO em pull-down e
 *             depois em pull-up: o byte muda?    -> se muda = MISO nao e
 *                                                   dirigido = NSS, SCK ou
 *                                                   MISO rompido
 *   degrau 3  SetStandby(RC) e GetStatus de novo:
 *             modo virou STBY_RC?                -> MOSI chega no chip
 *   degrau 4  ReadRegister(0x0740): sync word de
 *             fabrica 0x14?                      -> caminho completo ok
 *
 * E o GPS: 3 s olhando IO4 (TX) e IO2 (PPS) com pull-down - NMEA aparece como
 * rajada de bordas 1x/s com repouso ALTO; PPS como ~3 bordas de subida.
 *
 *   .\tools\build.ps1 firmware\chicote_probe -Board p4 -Upload -Port COMx
 */
#include <Arduino.h>
#include "../mts_p4/hardware.h"

// ------------------------------------------------------- SPI de bit-bang
// ~100 kHz, MODO 0. Lento de proposito: contato ruim aparece mais em
// velocidade alta, e aqui a pergunta e "o fio existe?", nao "e rapido?".
static void spiPinos()
{
  pinMode(PIN_LORA_NSS,  OUTPUT); digitalWrite(PIN_LORA_NSS, HIGH);
  pinMode(PIN_LORA_SCK,  OUTPUT); digitalWrite(PIN_LORA_SCK, LOW);
  pinMode(PIN_LORA_MOSI, OUTPUT); digitalWrite(PIN_LORA_MOSI, LOW);
  pinMode(PIN_LORA_BUSY, INPUT);
  pinMode(PIN_LORA_NRST, OUTPUT); digitalWrite(PIN_LORA_NRST, HIGH);
}

static uint8_t spiByte(uint8_t out)
{
  uint8_t in = 0;
  for (int b = 7; b >= 0; b--) {
    digitalWrite(PIN_LORA_MOSI, (out >> b) & 1);
    delayMicroseconds(4);
    digitalWrite(PIN_LORA_SCK, HIGH);
    delayMicroseconds(4);
    in = (in << 1) | digitalRead(PIN_LORA_MISO);
    digitalWrite(PIN_LORA_SCK, LOW);
  }
  return in;
}

static bool esperaBusyBaixo(uint32_t limUs)
{
  uint32_t t0 = micros();
  while (digitalRead(PIN_LORA_BUSY)) {
    if (micros() - t0 > limUs) return false;
  }
  return true;
}

// transacao generica: manda n bytes, devolve os n lidos no mesmo relogio
static bool spiTroca(const uint8_t* tx, uint8_t* rx, int n)
{
  if (!esperaBusyBaixo(20000)) return false;
  digitalWrite(PIN_LORA_NSS, LOW);
  delayMicroseconds(4);
  for (int i = 0; i < n; i++) rx[i] = spiByte(tx[i]);
  digitalWrite(PIN_LORA_NSS, HIGH);
  delayMicroseconds(4);
  return true;
}

static void imprimeStatus(const char* rotulo, uint8_t s)
{
  const uint8_t modo = (s >> 4) & 0x07, cmd = (s >> 1) & 0x07;
  static const char* nomeModo[] = {"?0","?1","STBY_RC","STBY_XOSC","FS","RX","TX","?7"};
  static const char* nomeCmd[]  = {"?0","reservado","dado ok","TIMEOUT","ERRO PROC","FALHOU","TX ok","?7"};
  Serial.printf("  %s status=0x%02X (modo=%s, ult.cmd=%s)\n", rotulo, s, nomeModo[modo], nomeCmd[cmd]);
}

static void escadaSpi()
{
  Serial.println("\n===== ESCADA DO SPI (radio) =====");
  spiPinos();

  // degrau 1: reset e BUSY
  digitalWrite(PIN_LORA_NRST, LOW);  delayMicroseconds(800);
  digitalWrite(PIN_LORA_NRST, HIGH);
  uint32_t t0 = micros();
  bool subiu = false;
  while (micros() - t0 < 3000) if (digitalRead(PIN_LORA_BUSY)) { subiu = true; break; }
  bool caiu = esperaBusyBaixo(50000);
  Serial.printf("[1] reset: BUSY subiu=%s, caiu=%s -> %s\n",
                subiu ? "sim" : "nao", caiu ? "sim" : "NAO",
                (subiu && caiu) ? "chip ALIMENTADO e vivo (NRST/BUSY ok)"
                                : "chip nao reagiu: alimentacao ou NRST/BUSY");
  if (!caiu) return;

  // degrau 2: o MISO e dirigido pelo chip? GetStatus (0xC0) le 1 byte de status
  // em QUALQUER estado, desde que NSS e SCK cheguem la. Se a leitura seguir o
  // pull interno, ninguem esta dirigindo o MISO durante a transacao.
  uint8_t tx2[2] = { 0xC0, 0x00 }, rxDn[2], rxUp[2];
  pinMode(PIN_LORA_MISO, INPUT_PULLDOWN);
  bool okDn = spiTroca(tx2, rxDn, 2);
  pinMode(PIN_LORA_MISO, INPUT_PULLUP);
  bool okUp = spiTroca(tx2, rxUp, 2);
  pinMode(PIN_LORA_MISO, INPUT);
  if (!okDn || !okUp) { Serial.println("[2] BUSY prendeu no meio - repita"); return; }
  Serial.printf("[2] GetStatus: com pull-down=0x%02X, com pull-up=0x%02X\n", rxDn[1], rxUp[1]);
  if (rxDn[1] == 0x00 && rxUp[1] == 0xFF) {
    Serial.println("    -> MISO NAO e dirigido: rompido o NSS, o SCK ou o MISO.");
    Serial.println("       (o chip esta vivo pelo degrau 1, entao e FIO, nao modulo)");
    return;
  }
  if (rxDn[1] == rxUp[1]) { imprimeStatus("->", rxDn[1]); Serial.println("    -> MISO dirigido: NSS+SCK+MISO ok"); }
  else Serial.println("    -> leitura instavel: contato intermitente em NSS/SCK/MISO");

  // degrau 3: o MOSI chega? SetStandby(RC)=0x80,0x00 muda o modo para STBY_RC.
  uint8_t tx3[2] = { 0x80, 0x00 }, rx3[2];
  spiTroca(tx3, rx3, 2);
  delayMicroseconds(200);
  uint8_t rx4[2];
  spiTroca(tx2, rx4, 2);
  imprimeStatus("[3] apos SetStandby(RC):", rx4[1]);
  uint8_t modo = (rx4[1] >> 4) & 0x07;
  Serial.println(modo == 2 ? "    -> comando ACEITO: MOSI ok"
                           : "    -> comando ignorado: MOSI rompido (ou SCK intermitente)");

  // degrau 4: caminho completo - ReadRegister(0x0740) = sync word, 0x14 de fabrica
  uint8_t tx5[6] = { 0x1D, 0x07, 0x40, 0x00, 0x00, 0x00 }, rx5[6];
  spiTroca(tx5, rx5, 6);
  Serial.printf("[4] ReadRegister(0x0740) = 0x%02X (fabrica: 0x14) -> %s\n",
                rx5[5], rx5[5] == 0x14 ? "CAMINHO COMPLETO OK - os 4 fios do SPI funcionam"
                                       : "leitura errada: conferir o degrau que falhou acima");
}

// ------------------------------------------------------------- GPS
static void olhaGps()
{
  Serial.println("\n===== GPS (IO4 = TX, IO2 = PPS) =====");
  pinMode(PIN_GPS_RX, INPUT_PULLDOWN);
  pinMode(PIN_GPS_PPS, INPUT_PULLDOWN);
  delay(20);
  uint32_t altoTx = 0, bordasTx = 0, subidasPps = 0, n = 0;
  int aTx = digitalRead(PIN_GPS_RX), aPps = digitalRead(PIN_GPS_PPS);
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    int v = digitalRead(PIN_GPS_RX);
    altoTx += v;
    if (v != aTx) { bordasTx++; aTx = v; }
    int p = digitalRead(PIN_GPS_PPS);
    if (p && !aPps) subidasPps++;
    aPps = p;
    n++;
  }
  pinMode(PIN_GPS_RX, INPUT);
  pinMode(PIN_GPS_PPS, INPUT);
  Serial.printf("TX do GPS em 3 s: alto %lu%%, %lu bordas | PPS: %lu subidas\n",
                (unsigned long)(altoTx * 100 / (n ? n : 1)), (unsigned long)bordasTx,
                (unsigned long)subidasPps);
  if (bordasTx > 50 && altoTx * 100 / n > 60)
    Serial.println("-> NMEA chegando: GPS alimentado e fiado. So falta o firmware de verdade.");
  else if (altoTx * 100 / n > 95)
    Serial.println("-> repouso alto sem dado: GPS ligado mas mudo (config? RX/TX trocados?)");
  else
    Serial.println("-> nada: TX do GPS nao chega no IO4 (fio, ou GPS sem energia - o 3V3 dele vem do P3)");
}

// ------------------------------------------------- CACADOR (v3)
// Se a escada falhar nos pinos documentados, procura o SPI do radio em TODAS
// as combinacoes de NSS/SCK/MISO dos pinos expostos. Funciona porque o SX1262
// poe o byte de STATUS no MISO em QUALQUER comando: basta selecionar e clocar
// - nem MOSI precisa. Se o chicote estiver em outros pinos, isto o acha; se
// nao achar em nenhuma combinacao, o fio esta rompido/solto, ponto final.
//
// Seguranca: nunca dirige IO49/IO51 (BUSY/NRST, provados) nem IO50 (DIO1 e
// SAIDA do radio - dirigir contra ele e curto) nem IO34/36 (strapping/banco do
// LDO - so leitura).
static const int CACA_DRV[]  = { 2, 3, 4, 5, 28, 29, 30, 31, 52 };
static const int CACA_MISO[] = { 2, 3, 4, 5, 28, 29, 30, 31, 34, 36, 50 };
#define N_DRV  (int)(sizeof(CACA_DRV) / sizeof(CACA_DRV[0]))
#define N_MISO (int)(sizeof(CACA_MISO) / sizeof(CACA_MISO[0]))

static bool tentaStatus(int nss, int sck, int miso, uint8_t& s1, uint8_t& s2)
{
  pinMode(nss, OUTPUT);  digitalWrite(nss, HIGH);
  pinMode(sck, OUTPUT);  digitalWrite(sck, LOW);
  pinMode(miso, INPUT_PULLDOWN);
  delayMicroseconds(10);
  digitalWrite(nss, LOW);
  delayMicroseconds(3);
  uint8_t b[2] = { 0, 0 };
  for (int by = 0; by < 2; by++)
    for (int i = 0; i < 8; i++) {
      digitalWrite(sck, HIGH); delayMicroseconds(2);
      b[by] = (b[by] << 1) | digitalRead(miso);
      digitalWrite(sck, LOW);  delayMicroseconds(2);
    }
  digitalWrite(nss, HIGH);
  delayMicroseconds(3);
  pinMode(nss, INPUT); pinMode(sck, INPUT); pinMode(miso, INPUT);
  s1 = b[0]; s2 = b[1];
  return (b[0] != 0x00) || (b[1] != 0x00);   // com pull-down, so fio dirigido le 1
}

static void cacaSpi()
{
  Serial.println("\n===== CACANDO o SPI em todos os pinos expostos =====");
  spiPinos();
  // reset para o chip estar quieto e em STDBY_RC (status != 0)
  digitalWrite(PIN_LORA_NRST, LOW); delayMicroseconds(800);
  digitalWrite(PIN_LORA_NRST, HIGH);
  esperaBusyBaixo(50000);

  int achados = 0;
  for (int a = 0; a < N_DRV; a++)
    for (int b = 0; b < N_DRV; b++) {
      if (a == b) continue;
      for (int m = 0; m < N_MISO; m++) {
        if (CACA_MISO[m] == CACA_DRV[a] || CACA_MISO[m] == CACA_DRV[b]) continue;
        uint8_t s1, s2;
        if (tentaStatus(CACA_DRV[a], CACA_DRV[b], CACA_MISO[m], s1, s2)) {
          Serial.printf("  >>> RESPOSTA com NSS=IO%d SCK=IO%d MISO=IO%d (bytes 0x%02X 0x%02X)\n",
                        CACA_DRV[a], CACA_DRV[b], CACA_MISO[m], s1, s2);
          achados++;
        }
      }
    }
  if (!achados) {
    Serial.println("  nenhuma resposta em NENHUMA combinacao (972 tentativas).");
    Serial.println("  -> os fios de SPI nao chegam ao processador por caminho nenhum:");
    Serial.println("     conector solto/meio encaixado ou crimp rompido no rabicho do P3.");
  }
}

static void cacaGps()
{
  Serial.println("===== CACANDO o TX do GPS em todos os pinos =====");
  int achou = 0;
  for (int m = 0; m < N_MISO; m++) {
    int pin = CACA_MISO[m];
    pinMode(pin, INPUT_PULLDOWN);
    delay(5);
    uint32_t alto = 0, bordas = 0, n = 0;
    int ant = digitalRead(pin);
    uint32_t t0 = millis();
    while (millis() - t0 < 250) {
      int v = digitalRead(pin);
      alto += v;
      if (v != ant) { bordas++; ant = v; }
      n++;
    }
    pinMode(pin, INPUT);
    if (n && alto * 100 / n > 60 && bordas > 10) {
      Serial.printf("  >>> IO%d parece UART: alto %lu%%, %lu bordas\n",
                    pin, (unsigned long)(alto * 100 / n), (unsigned long)bordas);
      achou++;
    }
  }
  if (!achou) Serial.println("  nenhum pino com cara de UART -> o GPS esta SEM ENERGIA (3V3 vem do P3)");
}

// ------------------------------------------------- v4: fechar os 100%
// Tres provas que eliminam as hipoteses restantes, uma a uma:
//   [0]  celulas GPIO do P4: cada pino vira SAIDA e le a si mesmo. Se falhar,
//        o defeito e na PLACA, nao no chicote. (nunca vimos falhar - e para
//        poder afirmar, nao supor)
//   [2b] TESTEMUNHA DO BUSY: SetStandby(XOSC) faz o chip tentar ligar o
//        oscilador, e o BUSY fica ALTO enquanto tenta. Se o BUSY piscar apos o
//        comando, o chip RECEBEU -> NSS+SCK+MOSI chegam e SO o MISO esta
//        rompido. Se nem piscar, nada chega ao chip.
//   [2c] PERMUTACAO: as 24 atribuicoes possiveis de NSS/SCK/MOSI/MISO dentro
//        dos 4 pinos documentados (28/29/30/31), com reset antes de cada uma
//        (lixo no MOSI pode virar SetSleep e cegar o resto). Cobre "fios
//        trocados entre si", que a cacada por MISO nao separa da ruptura.

static bool resetRadio()
{
  digitalWrite(PIN_LORA_NRST, LOW);  delayMicroseconds(800);
  digitalWrite(PIN_LORA_NRST, HIGH);
  return esperaBusyBaixo(60000);
}

static void celulasGpio()
{
  static const int PP[] = { 2, 3, 4, 5, 28, 29, 30, 31 };
  int ruim = 0;
  for (int i = 0; i < 8; i++) {
    pinMode(PP[i], OUTPUT);
    digitalWrite(PP[i], HIGH); delayMicroseconds(5);
    int h = digitalRead(PP[i]);
    digitalWrite(PP[i], LOW);  delayMicroseconds(5);
    int l = digitalRead(PP[i]);
    pinMode(PP[i], INPUT);
    if (h != 1 || l != 0) { ruim++; Serial.printf("[0] IO%d NAO le o que escreve!\n", PP[i]); }
  }
  Serial.printf("[0] celulas GPIO do P4 (8 pinos do P3): %s\n",
                ruim ? "DEFEITO NA PLACA" : "todas ok - a placa esta boa");
}

// manda 2 bytes por bit-bang em (nss,sck,mosi) e conta quanto o BUSY ficou
// alto nos 3 ms seguintes. Piscada = o chip processou o comando.
static uint32_t mandaEVigiaBusy(int nss, int sck, int mosi, uint8_t b0, uint8_t b1)
{
  pinMode(nss, OUTPUT);  digitalWrite(nss, HIGH);
  pinMode(sck, OUTPUT);  digitalWrite(sck, LOW);
  pinMode(mosi, OUTPUT); digitalWrite(mosi, LOW);
  delayMicroseconds(10);
  digitalWrite(nss, LOW); delayMicroseconds(3);
  uint8_t tx[2] = { b0, b1 };
  for (int by = 0; by < 2; by++)
    for (int b = 7; b >= 0; b--) {
      digitalWrite(mosi, (tx[by] >> b) & 1); delayMicroseconds(2);
      digitalWrite(sck, HIGH); delayMicroseconds(2);
      digitalWrite(sck, LOW);
    }
  digitalWrite(nss, HIGH);
  uint32_t altas = 0, t0 = micros();
  while (micros() - t0 < 3000) if (digitalRead(PIN_LORA_BUSY)) altas++;
  pinMode(nss, INPUT); pinMode(sck, INPUT); pinMode(mosi, INPUT);
  return altas;
}

static void testemunhaBusy()
{
  Serial.println("\n===== [2b] TESTEMUNHA DO BUSY (o chip percebe o comando?) =====");
  spiPinos();
  if (!resetRadio()) { Serial.println("  BUSY nao caiu apos reset"); return; }
  // linha de base: sem comando nenhum, o BUSY fica quieto?
  uint32_t base = 0, t0 = micros();
  while (micros() - t0 < 3000) if (digitalRead(PIN_LORA_BUSY)) base++;
  uint32_t altas = mandaEVigiaBusy(PIN_LORA_NSS, PIN_LORA_SCK, PIN_LORA_MOSI, 0x80, 0x01);
  Serial.printf("  BUSY alto: %lu amostras em repouso, %lu apos SetStandby(XOSC)\n",
                (unsigned long)base, (unsigned long)altas);
  if (altas > base + 20)
    Serial.println("  -> o chip PERCEBEU o comando: NSS+SCK+MOSI chegam. SO o MISO esta rompido.");
  else
    Serial.println("  -> o chip NEM PERCEBEU: o comando nao chega (NSS/SCK/MOSI nao encostam no modulo).");
}

static void permutaDocumentados()
{
  Serial.println("===== [2c] PERMUTACAO dos 4 pinos documentados (fios trocados?) =====");
  static const int P4S[4] = { PIN_LORA_NSS, PIN_LORA_MOSI, PIN_LORA_SCK, PIN_LORA_MISO };
  int acharam = 0;
  for (int a = 0; a < 4; a++)
   for (int b = 0; b < 4; b++)
    for (int c = 0; c < 4; c++) {
      if (a == b || a == c || b == c) continue;
      int d = 6 - a - b - c;                     // o que sobrou e o MISO
      spiPinos();
      if (!resetRadio()) continue;               // reset ANTES de cada combo:
                                                 // lixo pode ter virado SetSleep
      uint8_t s1, s2;
      bool viu = tentaStatus(P4S[a], P4S[b], P4S[d], s1, s2);
      bool plausivel = viu && s1 != 0xFF && s2 != 0xFF && ((s1 | s2) & 0x70);
      uint32_t altas = mandaEVigiaBusy(P4S[a], P4S[b], P4S[c], 0x80, 0x01);
      if (plausivel || altas > 40) {
        Serial.printf("  >>> vida com NSS=IO%d SCK=IO%d MOSI=IO%d MISO=IO%d "
                      "(status 0x%02X/0x%02X, busy %lu)\n",
                      P4S[a], P4S[b], P4S[c], P4S[d], s1, s2, (unsigned long)altas);
        acharam++;
      }
    }
  if (!acharam)
    Serial.println("  nenhuma das 24 permutacoes deu sinal de vida -> os fios NAO estao trocados: estao ROMPIDOS/soltos.");
}

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println("\n== SONDA DO CHICOTE v4 (escada + testemunha + permutacao + cacador) ==");
}

void loop()
{
  celulasGpio();
  escadaSpi();
  testemunhaBusy();
  permutaDocumentados();
  olhaGps();
  cacaGps();
  Serial.println("\n---- repetindo em 6 s (mexa/reencaixe o P3 e observe) ----");
  delay(6000);
}
