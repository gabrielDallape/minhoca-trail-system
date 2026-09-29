#pragma once
// ============================================================================
//  WIFI + OTA - conveniencia de GARAGEM, desligada por padrao (MTS_TEM_WIFI 0)
// ============================================================================
//
// O QUE ISTO E (e o que nao e): a tela presa na caixinha atualiza firmware sem
// cabo quando esta perto de um WiFi conhecido. Na trilha nao ha rede e nada
// aqui roda. O WiFi do P4 vive num ESP32-C6 auxiliar (esp_hosted) com bugs
// conhecidos - NENHUMA funcao critica entra aqui, e o HTTPS ja segurou o laco
// por centenas de ms no meio da janela de TDMA (2026-08-29). Por isso:
//
//   - so compila com MTS_TEM_WIFI 1 (hal/pinos.h);
//   - o eco e a checagem da nuvem so entram no ar se redePodeGastar() diz que
//     ha mais de 600 ms ate a proxima janela (C4 da auditoria) - nao basta
//     "nao ha TX em voo", que e verdade 91 % do tempo;
//   - a checagem da nuvem so roda na tela inicial (o servico sabe o estado).
//
// Tres caminhos, herdados do mts_p4:
//   1. ArduinoOTA na rede local (a tela aparece com o nome do carro; senha "mts")
//   2. /status por HTTP - so funciona de DENTRO da tela (o esp_hosted nao entrega
//      trafego de entrada; medido), por isso existe o
//   3. ECO: a tela conta o proprio estado ao servidor (mts-ota.vercel.app/api/eco)
//      ao conectar e a cada minuto - e a nuvem manda firmware novo em FATIAS DE
//      8 KB por Range (o TLS do esp_hosted trava acima disso).

#include <WiFi.h>
#include <ArduinoOTA.h>
#pragma push_macro("M")
#undef M
#include <WebServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#pragma pop_macro("M")
#include "../core/params.h"
#include "../hal/nvs.h"
#include "../hal/wdt.h"
#include "rede.h"

#define OTA_NUVEM_HOST "https://mts-ota.vercel.app"
#define OTA_PODE_BLOQUEAR_US 600000UL     // > o pior bloqueio medido do TLS

static WebServer* g_web = nullptr;
static void (*g_statusTexto)(char* fora, size_t n) = nullptr;   // o .ino monta (conhece tudo)
static void (*g_diagQuery)(char* fora, size_t n) = nullptr;     // versao para query string
static void (*g_otaDesenha)(int pct) = nullptr;                 // o .ino desenha (conhece o painel)

static char g_wifiSsid[33] = "";
static char g_wifiPass[65] = "";
static bool g_wifiQuer = false;
static bool g_otaAtivo = false;
static bool g_otaEmCurso = false;
static uint32_t g_wifiTentaMs = 0;

inline void wifiCarrega()
{
  nvsLeWifi(g_wifiSsid, sizeof(g_wifiSsid), g_wifiPass, sizeof(g_wifiPass));
  g_wifiQuer = g_wifiSsid[0] != 0;
}
inline void wifiSalva(const char* ssid, const char* pass)
{
  nvsGravaWifi(ssid, pass);
  strncpy(g_wifiSsid, ssid, sizeof(g_wifiSsid) - 1); g_wifiSsid[sizeof(g_wifiSsid) - 1] = 0;
  strncpy(g_wifiPass, pass, sizeof(g_wifiPass) - 1); g_wifiPass[sizeof(g_wifiPass) - 1] = 0;
  g_wifiQuer = g_wifiSsid[0] != 0;
  g_wifiTentaMs = 0;
}
inline void wifiEsquece() { wifiSalva("", ""); WiFi.disconnect(true); Serial.println("wifi: rede esquecida"); }
inline bool wifiConectado() { return WiFi.status() == WL_CONNECTED; }

inline void otaInicia(const char* nomeAparelho)
{
  ArduinoOTA.setHostname(nomeAparelho);
  ArduinoOTA.setPassword("mts");
  ArduinoOTA.onStart([]() { g_otaEmCurso = true; Serial.println("ota: iniciando - NAO desligue"); if (g_otaDesenha) g_otaDesenha(0); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    static int ultimo = -1;
    int pct = t ? (int)(p * 100UL / t) : 0;
    if (pct != ultimo && g_otaDesenha) { ultimo = pct; g_otaDesenha(pct); }
    wdtAlimenta();
  });
  ArduinoOTA.onEnd([]() { Serial.println("ota: gravado, reiniciando"); if (g_otaDesenha) g_otaDesenha(100); });
  ArduinoOTA.onError([](ota_error_t e) { g_otaEmCurso = false; Serial.printf("ota: erro %d\n", (int)e); });
  ArduinoOTA.begin();
  g_otaAtivo = true;
  if (!g_web) {
    g_web = new WebServer(80);      // uma vez por boot, fora do caminho quente
    g_web->on("/status", []() {
      char b[640]; b[0] = 0;
      if (g_statusTexto) g_statusTexto(b, sizeof(b)); else strncpy(b, "sem telemetria", sizeof(b) - 1);
      g_web->send(200, "text/plain; charset=utf-8", b);
    });
    g_web->begin();
  }
}

inline void wifiInicia()
{
  wifiCarrega();
  if (!g_wifiQuer) return;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);     // com sleep o C6 nao acorda para trafego de entrada
  WiFi.begin(g_wifiSsid, g_wifiPass);
  Serial.printf("wifi: procurando '%s'...\n", g_wifiSsid);
}

// O ECO leva o NOME do aparelho: sem ele o relatorio chega como "ECO ?" e nao da
// para saber de qual tela se fala (2026-08-29).
inline bool otaEco(const char* motivo)
{
  if (!wifiConectado() || g_otaEmCurso || !g_diagQuery) return false;
  if (!redePodeGastar(OTA_PODE_BLOQUEAR_US)) return false;
  char q[400]; q[0] = 0;
  g_diagQuery(q, sizeof(q));
  if (!q[0]) return false;
  String url = String(OTA_NUVEM_HOST) + "/api/eco?m=" + motivo + "&" + q;
  NetworkClientSecure cli; cli.setInsecure(); cli.setTimeout(8000);
  HTTPClient http; http.setConnectTimeout(6000);
  if (!http.begin(cli, url)) return false;
  int rc = http.GET();
  http.end();
  Serial.printf("eco: %s -> HTTP %d\n", motivo, rc);
  return true;
}

inline bool otaJsonPega(const String& j, const char* chave, String& fora)
{
  char pat[24]; snprintf(pat, sizeof(pat), "\"%s\"", chave);
  int p = j.indexOf(pat); if (p < 0) return false;
  p = j.indexOf(':', p);  if (p < 0) return false;
  p++;
  while (p < (int)j.length() && (j[p] == ' ' || j[p] == '"')) p++;
  int f = p;
  while (f < (int)j.length() && j[f] != ',' && j[f] != '"' && j[f] != '}' && j[f] != '\r' && j[f] != '\n') f++;
  fora = j.substring(p, f); fora.trim();
  return fora.length() > 0;
}

// CHAMAR SO DA TELA INICIAL: a atualizacao para o radio e o GPS por ~1 min.
inline void otaNuvemChecar()
{
  static uint32_t prox = 0;
  if (!wifiConectado() || g_otaEmCurso) return;
  if (!prox) prox = millis() + 40000;
  if ((int32_t)(millis() - prox) < 0) return;
  if (!redePodeGastar(OTA_PODE_BLOQUEAR_US)) return;
  prox = millis() + 15UL * 60UL * 1000UL;

  NetworkClientSecure cli; cli.setInsecure(); cli.setTimeout(10000);
  HTTPClient http; http.setConnectTimeout(8000);
  if (!http.begin(cli, OTA_NUVEM_HOST "/version.json")) return;
  int rc = http.GET();
  if (rc != 200) { http.end(); Serial.printf("nuvem: manifesto HTTP %d\n", rc); return; }
  String j = http.getString(); http.end();
  String sv, sarq, smd5;
  if (!otaJsonPega(j, "versao", sv) || !otaJsonPega(j, "arq", sarq) || !otaJsonPega(j, "md5", smd5)) return;
  int versao = sv.toInt();
  if (versao <= MTS_VERSAO) return;
  Serial.printf("nuvem: v%d disponivel (rodando v%d) - baixando %s\n", versao, MTS_VERSAO, sarq.c_str());

  // DOWNLOAD EM FATIAS (HTTP Range): o TLS do esp_hosted para de entregar depois
  // de ~8 KB numa resposta so. Cada fatia e bufferizada INTEIRA antes do
  // Update.write, para a retentativa nao duplicar bytes gravados.
  String url = String(OTA_NUVEM_HOST) + sarq;
  int tam = 0;
  { String st; if (otaJsonPega(j, "bytes", st)) tam = st.toInt(); }
  if (tam <= 0) return;
  if (!Update.begin(tam)) { Serial.println("nuvem: nao coube no slot"); return; }
  if (smd5.length() == 32) Update.setMD5(smd5.c_str());
  g_otaEmCurso = true;
  if (g_otaDesenha) g_otaDesenha(0);
  static uint8_t fatia[8192];
  NetworkClientSecure cli2; cli2.setInsecure(); cli2.setTimeout(10000);
  HTTPClient h2; h2.setReuse(true); h2.setConnectTimeout(8000);
  int feito = 0, tent = 0, ultimo = -1;
  while (feito < tam && tent < 6) {
    wdtAlimenta();       // este laco leva minutos e nao pode chamar o servico
    int fim = feito + (int)sizeof(fatia); if (fim > tam) fim = tam;
    if (!h2.begin(cli2, url)) { tent++; delay(400); continue; }
    char faixa[40]; snprintf(faixa, sizeof(faixa), "bytes=%d-%d", feito, fim - 1);
    h2.addHeader("Range", faixa);
    int rc2 = h2.GET();
    if (rc2 != 206) { h2.end(); cli2.stop(); tent++; delay(500); continue; }
    NetworkClient* s = h2.getStreamPtr();
    const int quero = fim - feito;
    int li = 0;
    uint32_t mudo = millis();
    while (li < quero && millis() - mudo < 8000) {
      int n = s->read(fatia + li, quero - li);
      if (n > 0) { li += n; mudo = millis(); } else delay(1);
    }
    h2.end();
    if (li != quero) { cli2.stop(); tent++; delay(500); continue; }
    if (Update.write(fatia, (size_t)quero) != (size_t)quero) break;
    feito = fim; tent = 0;
    int pct = (int)((int64_t)feito * 100 / tam);
    if (pct != ultimo && g_otaDesenha) { ultimo = pct; g_otaDesenha(pct); }
  }
  cli2.stop();
  if (feito == tam && Update.end(true)) {
    Serial.printf("nuvem: atualizado para v%d, reiniciando\n", versao);
    if (g_otaDesenha) g_otaDesenha(100);
    registroFecha();
    delay(600);
    ESP.restart();
  }
  Update.abort();
  g_otaEmCurso = false;
  Serial.printf("nuvem: falhou (%d de %d bytes) - sigo na v%d\n", feito, tam, MTS_VERSAO);
}

// No servico. Reconecta sozinho a cada 30 s enquanto houver rede salva.
inline void wifiAtualiza(const char* nomeAparelho)
{
  static bool conectadoAntes = false;
  const bool agora = wifiConectado();
  if (agora && !conectadoAntes) {
    WiFi.setSleep(false);
    Serial.printf("wifi: conectado a '%s', IP %s (OTA no ar como '%s')\n", g_wifiSsid, WiFi.localIP().toString().c_str(), nomeAparelho);
    if (!g_otaAtivo) otaInicia(nomeAparelho);
    otaEco("subiu");
  }
  if (!agora && conectadoAntes) Serial.println("wifi: conexao caiu");
  conectadoAntes = agora;
  static uint32_t proxEco = 0;
  if (agora && millis() - proxEco > 60000 && otaEco("periodico")) proxEco = millis();
  if (!agora && g_wifiQuer && millis() - g_wifiTentaMs > 30000) {
    g_wifiTentaMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid, g_wifiPass);
  }
  if (g_otaAtivo && agora) { ArduinoOTA.handle(); if (g_web) g_web->handleClient(); }
}
