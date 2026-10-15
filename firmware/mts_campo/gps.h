// GPS: NMEA pela Serial1 e o PPS carimbado na interrupcao.
//
// POR QUE UM PARSER PROPRIO, e nao a TinyGPSPlus. Precisamos de tres campos
// (posicao, qualidade, satelites) e de uma coisa que as bibliotecas de conveniencia
// escondem: a RECUSA de linha corrompida. Na bancada o log mostrou frases assim,
// vindas de ruido na UART:
//     Li?b??bbb??b??b??b???b??R??j
//     ?,86,76,75,85,,,,,,,,,1.68,0.86,1.44*1C
// Uma linha dessas com um numero plausivel no campo errado teleporta o carro no
// mapa, e num aparelho de trilha isso e pior que nao mostrar nada: o sujeito vira
// na direcao errada. Aqui toda frase passa pelo checksum antes de virar posicao.
//
// O PPS nao alimenta nada ainda alem de um contador - quem vai consumi-lo e o
// tdma_core.h. Mas ele ja e lido aqui porque e o mesmo fio e o mesmo modulo, e
// porque saber se ele pulsa e o diagnostico mais util que este arquivo oferece.
#pragma once
#include "hardware.h"
#include "esp_timer.h"

// ------------------------------------------------------------------ estado
static double   g_gpsLat = 0, g_gpsLon = 0;
static bool     g_gpsFix = false;
static uint8_t  g_gpsSats = 0;
static float    g_gpsHdop = 99.9f;
static uint32_t g_gpsUltimaMs = 0;      // quando chegou a ultima frase VALIDA
static uint32_t g_gpsFrases = 0, g_gpsRuins = 0;
// Vistos pela antena e o melhor sinal recebido. O pico e ZERADO a cada volta do
// GSV (ele vem em 2-4 frases seguidas), senao guardaria para sempre o melhor
// segundo da historia e nunca mostraria que o sinal caiu.
static uint8_t  g_gpsVista = 0, g_gpsSnrPico = 0;
static uint8_t  g_gpsSnrAnt = 0;
// Rumo e velocidade sobre o solo (RMC). O rumo do GPS PARADO e ruido puro -
// deriva da fase da portadora, nao de movimento - entao ele so e atualizado
// andando; parado, vale o ultimo rumo de quando andava, que e o que um carro
// parado de fato tem.
static float    g_gpsRumo = 0;        // graus, 0 = norte
static float    g_gpsVelKmh = 0;

// O PPS e marcado dentro da ISR: qualquer processamento antes do carimbo entra
// como erro na regua de tempo do TDMA.
//
// 64 BITS, nao micros(). O micros() do Arduino e o esp_timer truncado a 32 bits
// e estoura em 71,6 min; o tdma_core compara este carimbo com o relogio de 64
// bits, entao o estouro deslocava a ancora em ~967 ms (2^32 mod 1e6) e a rede
// inteira desalinhava depois de uma hora e dez ligada.
static volatile uint64_t g_ppsUs = 0;
static volatile uint32_t g_ppsCnt = 0;
static uint32_t g_ppsIntervaloUs = 0;

// PAREAMENTO PPS <-> SEGUNDO UTC. O pulso da a BORDA com precisao de nanosegundos
// mas nao diz que segundo e; o NMEA diz o segundo mas chega centenas de ms depois,
// pela UART, com jitter. Um sem o outro nao ancora nada.
//
// A regra: a frase NMEA que traz a hora T se refere ao pulso que ACABOU de
// acontecer. Entao casamos T com a ultima borda registrada.
//
// Guardo SEGUNDO DO DIA e nao epoch. Todos os frameSecs permitidos (1, 2, 3, 6,
// 9, 18) dividem 86400, entao a virada da meia-noite nao produz salto na conta
// (anchorSec % frameSecs) - que e o unico uso que o tdma_core faz deste numero.
// Epoch exigiria parsear a data e tratar ano bissexto para ganhar nada.
static uint32_t g_gpsSecDia = 0;
static bool     g_gpsHoraOk = false;
static uint64_t g_parePpsUs = 0;
static uint32_t g_pareSec = 0, g_parePpsCnt = 0;
static bool     g_parePronto = false;   // ha um par novo esperando o TDMA

void IRAM_ATTR gpsOnPps()
{
  g_ppsUs = (uint64_t)esp_timer_get_time();
  g_ppsCnt++;
}

// --------------------------------------------------------------- utilidades
// NMEA manda a latitude como ddmm.mmmm e a longitude como dddmm.mmmm - grau e
// minuto GRUDADOS no mesmo numero. Dividir por 100 nao converte: 2309.49 nao e
// 23,0949 graus, e 23 graus e 9,49 minutos = 23,158 graus. Errar isto desloca a
// posicao em dezenas de quilometros, e o mapa ainda parece plausivel.
inline double nmeaParaGraus(const char* campo, char hemisferio)
{
  double v = atof(campo);
  int    g = (int)(v / 100.0);
  double m = v - g * 100.0;
  double d = g + m / 60.0;
  if (hemisferio == 'S' || hemisferio == 'W') d = -d;
  return d;
}

// Devolve o inicio do campo n (0 = o identificador), ou nullptr se nao existir.
inline const char* nmeaCampo(const char* linha, int n)
{
  if (n == 0) return linha;
  int v = 0;
  for (const char* p = linha; *p; p++) {
    if (*p == ',') { v++; if (v == n) return p + 1; }
  }
  return nullptr;
}

// O checksum e o XOR de tudo entre o '$' e o '*', em hexadecimal. E a unica
// defesa contra a linha corrompida virar coordenada.
inline bool nmeaChecksumOk(const char* linha)
{
  if (linha[0] != '$') return false;
  uint8_t soma = 0;
  const char* p = linha + 1;
  while (*p && *p != '*') soma ^= (uint8_t)*p++;
  if (*p != '*') return false;                 // sem checksum: nao aceita
  const char* h = p + 1;
  if (!h[0] || !h[1]) return false;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  int hi = hex(h[0]), lo = hex(h[1]);
  if (hi < 0 || lo < 0) return false;
  return soma == (uint8_t)((hi << 4) | lo);
}

// ------------------------------------------------------------------ parsing
// GGA e a frase que traz posicao, QUALIDADE e numero de satelites de uma vez:
//   $--GGA,hora,lat,N,lon,E,qualidade,satelites,hdop,altitude,...
// Campo 6 = qualidade: 0 significa SEM FIX, e ai os campos de posicao vem vazios.
inline void gpsProcessaGGA(const char* l)
{
  // Campo 1 = hhmmss.ss. Vem preenchido ANTES do fix, assim que o modulo decodifica
  // a mensagem de navegacao de um satelite - por isso a hora e tratada aqui, em
  // separado da posicao, e nao junto com o bloco de qualidade la embaixo.
  const char* cHora = nmeaCampo(l, 1);
  if (cHora && cHora[0] >= '0' && cHora[0] <= '9') {
    int hh = (cHora[0] - '0') * 10 + (cHora[1] - '0');
    int mm = (cHora[2] - '0') * 10 + (cHora[3] - '0');
    int ss = (cHora[4] - '0') * 10 + (cHora[5] - '0');
    if (hh < 24 && mm < 60 && ss < 60) {
      g_gpsSecDia = (uint32_t)hh * 3600UL + mm * 60UL + ss;
      g_gpsHoraOk = true;
      // Casa com a borda que veio ANTES desta frase, e so uma vez por pulso: sem
      // a comparacao de contador, as varias frases do mesmo segundo (GGA, RMC,
      // GSA...) reancorariam o TDMA cinco vezes com o mesmo instante.
      // Contador e carimbo copiados JUNTOS, com a interrupcao suspensa: o carimbo
      // tem 64 bits e a ISR pode escrever entre as duas metades da leitura.
      noInterrupts();
      uint32_t cnt = g_ppsCnt;
      uint64_t us  = g_ppsUs;
      interrupts();
      if (cnt && cnt != g_parePpsCnt) {
        g_parePpsUs  = us;
        g_pareSec    = g_gpsSecDia;
        g_parePpsCnt = cnt;
        g_parePronto = true;
      }
    }
  }

  const char* cQual = nmeaCampo(l, 6);
  if (!cQual) return;
  int qual = atoi(cQual);

  const char* cSats = nmeaCampo(l, 7);
  const char* cHdop = nmeaCampo(l, 8);
  if (cSats) g_gpsSats = (uint8_t)atoi(cSats);
  if (cHdop) g_gpsHdop = atof(cHdop);

  if (qual <= 0) { g_gpsFix = false; return; }

  const char* cLat = nmeaCampo(l, 2);
  const char* cNS  = nmeaCampo(l, 3);
  const char* cLon = nmeaCampo(l, 4);
  const char* cEW  = nmeaCampo(l, 5);
  if (!cLat || !cNS || !cLon || !cEW) return;
  if (*cLat == ',' || *cLon == ',') return;    // campo vazio: ainda sem posicao

  double lat = nmeaParaGraus(cLat, *cNS);
  double lon = nmeaParaGraus(cLon, *cEW);

  // Ultima peneira: mesmo com checksum bom, recusa o impossivel. Custa duas
  // comparacoes e evita que um campo deslocado por um bug de parsing vire uma
  // posicao no meio do oceano - que na tela pareceria so "o mapa sumiu".
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
  if (lat == 0.0 && lon == 0.0) return;

  g_gpsLat = lat; g_gpsLon = lon;
  g_gpsFix = true;
  g_gpsUltimaMs = millis();
}

// GSV: QUANTOS SATELITES A ANTENA ENXERGA, e com que forca.
//
// O campo de satelites do GGA conta os USADOS na solucao - e ele da zero tanto
// para "antena arrancada" quanto para "estou embaixo de laje". Sao problemas
// completamente diferentes e o mesmo numero. O GSV separa os dois:
//
//   a vista = 0                 -> nao chega sinal nenhum: antena, cabo, conector
//   a vista > 0, SNR baixo      -> chega sinal fraco: ceu bloqueado, so esperar
//   a vista > 0, SNR bom, s/fix -> ainda calculando, e normal nos primeiros minutos
//
// $xxGSV,total,indice,A_VISTA,prn,elev,azim,SNR,prn,elev,azim,SNR,...
inline void gpsProcessaGSV(const char* l)
{
  const char* c2 = nmeaCampo(l, 2);
  if (c2 && atoi(c2) == 1) { g_gpsSnrAnt = g_gpsSnrPico; g_gpsSnrPico = 0; }
  const char* c3 = nmeaCampo(l, 3);
  if (c3) {
    int v = atoi(c3);
    if (v >= 0 && v <= 64) g_gpsVista = (uint8_t)v;
  }
  // o SNR e o 4o campo de cada satelite, comecando no campo 7
  for (int k = 0; k < 4; k++) {
    const char* cs = nmeaCampo(l, 7 + k * 4);
    if (!cs || *cs == ',' || *cs == 0 || *cs == '*') continue;
    int snr = atoi(cs);
    if (snr > g_gpsSnrPico) g_gpsSnrPico = (uint8_t)snr;
  }
}

// RMC: rumo e velocidade sobre o solo.
//   $--RMC,hora,status,lat,NS,lon,EW,vel_nos,rumo_graus,data,...
// Campo 2 = status: 'A' e solucao valida; 'V' vem antes do fix, com os campos
// de velocidade vazios ou lixo.
inline void gpsProcessaRMC(const char* l)
{
  const char* cSt = nmeaCampo(l, 2);
  if (!cSt || *cSt != 'A') return;
  const char* cVel = nmeaCampo(l, 7);
  const char* cRum = nmeaCampo(l, 8);
  if (!cVel || *cVel == ',') return;
  float kmh = (float)atof(cVel) * 1.852f;      // nos -> km/h
  if (kmh < 0 || kmh > 300.0f) return;         // campo deslocado: recusa
  g_gpsVelKmh = kmh;
  // 3 km/h e o limiar classico: abaixo disso o rumo reportado gira a esmo
  if (kmh >= 3.0f && cRum && *cRum != ',') {
    float r = (float)atof(cRum);
    if (r >= 0 && r < 360.0f) g_gpsRumo = r;
  }
}

inline void gpsProcessaLinha(const char* l)
{
  if (!nmeaChecksumOk(l)) { g_gpsRuins++; return; }
  g_gpsFrases++;
  if (strstr(l, "GSV")) gpsProcessaGSV(l);
  // strstr e nao comparacao fixa porque o prefixo varia com a constelacao usada:
  // GP so GPS, GL so GLONASS, GN solucao combinada. O M8 alterna entre eles
  // conforme o fix, entao amarrar em "$GPGGA" perderia frase de um modulo bom.
  if (strstr(l, "GGA")) gpsProcessaGGA(l);
  if (strstr(l, "RMC")) gpsProcessaRMC(l);
}

// ------------------------------------------------------------------- publico
inline void gpsInicia()
{
  // Buffer maior que o padrao de 256 B. O laco da tela inicial fica 400 ms
  // esperando toque, e a 9600 bps o GPS produz ~960 B por segundo: em 400 ms
  // chegam ~384 B e o buffer padrao transbordaria, comendo frases inteiras.
  // Perder GGA nao trava nada, mas atrasa o fix sem deixar rastro no log.
  Serial1.setRxBufferSize(1024);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  pinMode(PIN_GPS_PPS, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), gpsOnPps, RISING);
}

// Chamar no loop, sempre. Nao bloqueia: consome so o que ja esta no buffer.
inline void gpsAtualiza()
{
  static char linha[100];
  static int  n = 0;

  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (n > 5) { linha[n] = 0; gpsProcessaLinha(linha); }
      n = 0;
    } else if (n < (int)sizeof(linha) - 1) {
      linha[n++] = c;
    } else {
      n = 0;      // linha absurda: descarta inteira em vez de truncar e parsear
    }
  }

  // intervalo entre os dois ultimos PPS, para saber se e 1 Hz de verdade
  static uint64_t ppsAnteriorUs = 0;
  static uint32_t ppsAnteriorCnt = 0;
  noInterrupts();
  uint32_t cnt = g_ppsCnt;
  uint64_t us  = g_ppsUs;
  interrupts();
  if (cnt != ppsAnteriorCnt) {
    if (ppsAnteriorUs) g_ppsIntervaloUs = (uint32_t)(us - ppsAnteriorUs);
    ppsAnteriorUs = us; ppsAnteriorCnt = cnt;
  }
}

// O fix ENVELHECE. Sem isto, um GPS que parou de falar deixaria o carro parado
// no ultimo ponto para sempre, e o mapa mentiria com cara de certeza. Tres
// segundos = tres frases de 1 Hz perdidas.
inline bool gpsFixValido()
{
  return g_gpsFix && (millis() - g_gpsUltimaMs < 3000);
}

// PPS de verdade e 1 Hz. Pino solto da milhares de bordas por segundo - contar
// borda nao basta, tem de conferir o RITMO. Isto foi aprendido na bancada, com
// 98.592 pulsos de 3 us sendo declarados "PPS funcionando".
inline bool gpsPpsValido()
{
  return g_ppsIntervaloUs > 900000UL && g_ppsIntervaloUs < 1100000UL;
}
