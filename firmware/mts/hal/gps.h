#pragma once
// ============================================================================
//  GPS - NMEA pela Serial1 e o PPS carimbado na interrupcao
// ============================================================================
//
// POR QUE UM PARSER PROPRIO, e nao a TinyGPSPlus: precisamos de tres campos
// (posicao, qualidade, satelites) e de uma coisa que as bibliotecas de
// conveniencia escondem: a RECUSA de linha corrompida. Na bancada o log mostrou
// frases assim, vindas de ruido na UART:
//     Li?b??bbb??b??b??b???b??R??j
// Uma linha dessas com um numero plausivel no campo errado teleporta o carro no
// mapa, e num aparelho de trilha isso e pior que nao mostrar nada. Aqui toda
// frase passa pelo checksum antes de virar posicao.
//
// PAREAMENTO PPS <-> SEGUNDO UTC. O pulso da a BORDA com precisao de
// nanossegundos mas nao diz que segundo e; o NMEA diz o segundo mas chega
// centenas de ms depois, pela UART. A frase NMEA que traz a hora T se refere ao
// pulso que ACABOU de acontecer: casamos T com a ultima borda registrada.
//
// C1 DA AUDITORIA (2026-09-08), corrigido aqui: o par so vale se a borda tem
// menos de GPS_PAR_PPS_MAX_US de idade. Sem isto, um laco parado por 1 s
// (TLS do eco) casava a GGA do segundo 0 com a borda do segundo 1: anchorSec
// 1 s errado, fase do quadro invertida, e como o no anuncia FL_PPS todos o
// seguiam. Colisao total e silenciosa. E se o laco ficou parado mais que
// GPS_LACO_PARADO_MS, o backlog de NMEA e descartado inteiro - frases velhas
// so servem para casar com a borda errada.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>
#include "esp_timer.h"
#include "pinos.h"
#include "../core/params.h"

struct GpsEstado {
  double   lat, lon;
  bool     fix;
  uint8_t  sats;           // usados na solucao
  uint8_t  vista;          // que a antena enxerga (GSV)
  uint8_t  snrAnt, snrPico;
  float    hdop;
  float    rumo;           // graus, 0 = norte; so muda andando (>= 3 km/h)
  float    velKmh;
  uint32_t ultimaFraseMs;  // ultima frase VALIDA com posicao
  uint32_t frases, ruins;
  uint32_t backlogDescartado;   // vezes que o NMEA velho foi jogado fora (laco parado)
  uint32_t parVelho;            // pares PPS/NMEA recusados por idade (C1)
  // hora
  uint32_t secDia;
  bool     horaOk;
  // par PPS <-> segundo pronto para o TDMA
  uint64_t parePpsUs;
  uint32_t pareSec, parePpsCnt;
  bool     parePronto;
  // PPS
  uint32_t ppsIntervaloUs;
  uint32_t ultimaChamadaMs;
};

static GpsEstado g_gps;

// So a ISR escreve; 64 BITS (esp_timer), nunca micros() - ver hal/relogio.h.
static volatile uint64_t g_ppsUs = 0;
static volatile uint32_t g_ppsCnt = 0;

void IRAM_ATTR gpsOnPps()
{
  g_ppsUs = (uint64_t)esp_timer_get_time();
  g_ppsCnt = g_ppsCnt + 1;     // (nao ++: volatile++ e deprecated no C++20)
}

inline void gpsLePps(uint32_t& cnt, uint64_t& us)
{
  // contador e carimbo copiados JUNTOS: o carimbo tem 64 bits e a ISR pode
  // escrever entre as duas metades da leitura num CPU de 32 bits
  noInterrupts();
  cnt = g_ppsCnt; us = g_ppsUs;
  interrupts();
}

// --------------------------------------------------------------- utilidades
// NMEA manda ddmm.mmmm: grau e minuto GRUDADOS. Dividir por 100 nao converte.
inline double nmeaParaGraus(const char* campo, char hemisferio)
{
  double v = atof(campo);
  int    g = (int)(v / 100.0);
  double m = v - g * 100.0;
  double d = g + m / 60.0;
  if (hemisferio == 'S' || hemisferio == 'W') d = -d;
  return d;
}

inline const char* nmeaCampo(const char* linha, int n)
{
  if (n == 0) return linha;
  int v = 0;
  for (const char* p = linha; *p; p++)
    if (*p == ',') { v++; if (v == n) return p + 1; }
  return nullptr;
}

inline bool nmeaChecksumOk(const char* linha)
{
  if (linha[0] != '$') return false;
  uint8_t soma = 0;
  const char* p = linha + 1;
  while (*p && *p != '*') soma ^= (uint8_t)*p++;
  if (*p != '*') return false;
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
inline void gpsProcessaGGA(const char* l)
{
  GpsEstado& g = g_gps;
  // Campo 1 = hhmmss.ss. Vem ANTES do fix, assim que o modulo decodifica a
  // mensagem de navegacao de um satelite.
  const char* cHora = nmeaCampo(l, 1);
  if (cHora && cHora[0] >= '0' && cHora[0] <= '9') {
    int hh = (cHora[0] - '0') * 10 + (cHora[1] - '0');
    int mm = (cHora[2] - '0') * 10 + (cHora[3] - '0');
    int ss = (cHora[4] - '0') * 10 + (cHora[5] - '0');
    if (hh < 24 && mm < 60 && ss < 60) {
      g.secDia = (uint32_t)hh * 3600UL + mm * 60UL + ss;
      g.horaOk = true;
      uint32_t cnt; uint64_t us;
      gpsLePps(cnt, us);
      if (cnt && cnt != g.parePpsCnt) {
        // C1: a borda tem de ser RECENTE. Uma borda velha e de outro segundo.
        uint64_t idade = (uint64_t)esp_timer_get_time() - us;
        if (idade < GPS_PAR_PPS_MAX_US) {
          g.parePpsUs  = us;
          g.pareSec    = g.secDia;
          g.parePpsCnt = cnt;
          g.parePronto = true;
        } else {
          g.parVelho++;
          g.parePpsCnt = cnt;       // nao tentar casar esta borda de novo
        }
      }
    }
  }

  const char* cQual = nmeaCampo(l, 6);
  if (!cQual) return;
  int qual = atoi(cQual);
  const char* cSats = nmeaCampo(l, 7);
  const char* cHdop = nmeaCampo(l, 8);
  if (cSats) g.sats = (uint8_t)atoi(cSats);
  if (cHdop) g.hdop = atof(cHdop);
  if (qual <= 0) { g.fix = false; return; }

  const char* cLat = nmeaCampo(l, 2);
  const char* cNS  = nmeaCampo(l, 3);
  const char* cLon = nmeaCampo(l, 4);
  const char* cEW  = nmeaCampo(l, 5);
  if (!cLat || !cNS || !cLon || !cEW) return;
  if (*cLat == ',' || *cLon == ',') return;

  double lat = nmeaParaGraus(cLat, *cNS);
  double lon = nmeaParaGraus(cLon, *cEW);
  // Ultima peneira: mesmo com checksum bom, recusa o impossivel.
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;
  if (lat == 0.0 && lon == 0.0) return;

  g.lat = lat; g.lon = lon;
  g.fix = true;
  g.ultimaFraseMs = millis();
}

// GSV: quantos satelites a antena ENXERGA e com que forca. Separa antena
// arrancada (vista = 0) de ceu bloqueado (vista > 0, SNR baixo).
inline void gpsProcessaGSV(const char* l)
{
  GpsEstado& g = g_gps;
  const char* c2 = nmeaCampo(l, 2);
  if (c2 && atoi(c2) == 1) { g.snrAnt = g.snrPico; g.snrPico = 0; }
  const char* c3 = nmeaCampo(l, 3);
  if (c3) { int v = atoi(c3); if (v >= 0 && v <= 64) g.vista = (uint8_t)v; }
  for (int k = 0; k < 4; k++) {
    const char* cs = nmeaCampo(l, 7 + k * 4);
    if (!cs || *cs == ',' || *cs == 0 || *cs == '*') continue;
    int snr = atoi(cs);
    if (snr > g.snrPico) g.snrPico = (uint8_t)snr;
  }
}

// RMC: rumo e velocidade sobre o solo. O rumo do GPS PARADO e ruido puro, entao
// so e atualizado andando (>= 3 km/h, o limiar classico).
inline void gpsProcessaRMC(const char* l)
{
  GpsEstado& g = g_gps;
  const char* cSt = nmeaCampo(l, 2);
  if (!cSt || *cSt != 'A') return;
  const char* cVel = nmeaCampo(l, 7);
  const char* cRum = nmeaCampo(l, 8);
  if (!cVel || *cVel == ',') return;
  float kmh = (float)atof(cVel) * 1.852f;
  if (kmh < 0 || kmh > 300.0f) return;
  g.velKmh = kmh;
  if (kmh >= 3.0f && cRum && *cRum != ',') {
    float r = (float)atof(cRum);
    if (r >= 0 && r < 360.0f) g.rumo = r;
  }
}

inline void gpsProcessaLinha(const char* l)
{
  if (!nmeaChecksumOk(l)) { g_gps.ruins++; return; }
  g_gps.frases++;
  // strstr e nao prefixo fixo: GP/GL/GN variam com a constelacao usada.
  if (strstr(l, "GSV")) gpsProcessaGSV(l);
  if (strstr(l, "GGA")) gpsProcessaGGA(l);
  if (strstr(l, "RMC")) gpsProcessaRMC(l);
}

// ------------------------------------------------------------------- publico
inline void gpsInicia()
{
  memset(&g_gps, 0, sizeof(g_gps));
  g_gps.hdop = 99.9f;
  // Buffer maior que os 256 B padrao: a 9600 bps chegam ~960 B/s e um laco que
  // espere 400 ms perderia frases inteiras sem deixar rastro.
  Serial1.setRxBufferSize(1024);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  pinMode(PIN_GPS_PPS, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), gpsOnPps, RISING);
  g_gps.ultimaChamadaMs = millis();
}

// Chamar do servico, sempre. Nao bloqueia: consome so o que ja esta no buffer.
inline void gpsAtualiza()
{
  static char linha[100];
  static int  n = 0;
  GpsEstado& g = g_gps;

  // C1: laco parado mais que GPS_LACO_PARADO_MS -> o que esta no buffer e velho
  // e casaria com a borda errada. Fora com tudo; o proximo segundo repoe.
  uint32_t agora = millis();
  if (agora - g.ultimaChamadaMs > GPS_LACO_PARADO_MS && Serial1.available()) {
    while (Serial1.available()) Serial1.read();
    n = 0;
    g.backlogDescartado++;
  }
  g.ultimaChamadaMs = agora;

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

  static uint64_t ppsAnteriorUs = 0;
  static uint32_t ppsAnteriorCnt = 0;
  uint32_t cnt; uint64_t us;
  gpsLePps(cnt, us);
  if (cnt != ppsAnteriorCnt) {
    if (ppsAnteriorUs) g.ppsIntervaloUs = (uint32_t)(us - ppsAnteriorUs);
    ppsAnteriorUs = us; ppsAnteriorCnt = cnt;
  }
}

// O fix ENVELHECE: um GPS que parou de falar nao pode deixar o carro cravado no
// ultimo ponto com cara de certeza.
inline bool gpsFixValido() { return g_gps.fix && (millis() - g_gps.ultimaFraseMs < GPS_FIX_VALIDO_MS); }

// PPS de verdade e 1 Hz. Pino solto da milhares de bordas por segundo (98.592
// pulsos de 3 us ja foram declarados "PPS funcionando" na bancada).
inline bool gpsPpsValido() { return g_gps.ppsIntervaloUs > 900000UL && g_gps.ppsIntervaloUs < 1100000UL; }
inline uint32_t gpsPpsCnt() { uint32_t c; uint64_t u; gpsLePps(c, u); return c; }

// Consome o par PPS <-> segundo, se houver um novo.
inline bool gpsPegaPar(uint64_t& ppsUs, uint32_t& sec)
{
  if (!g_gps.parePronto) return false;
  g_gps.parePronto = false;
  ppsUs = g_gps.parePpsUs; sec = g_gps.pareSec;
  return true;
}

inline uint32_t gpsUtc() { return g_gps.horaOk ? g_gps.secDia : 0; }
