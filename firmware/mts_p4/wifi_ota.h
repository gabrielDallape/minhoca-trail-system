// WIFI + ATUALIZACAO PELO AR (OTA).
//
// O QUE ISTO E (e o que nao e): conveniencia de GARAGEM. A tela presa na
// caixinha do carro atualiza firmware sem cabo quando esta perto de um WiFi
// conhecido. Na trilha nao ha rede e nada aqui roda - o radio LoRa e outro
// chip e nao depende disto em nada. O cabo continua sendo o plano B eterno,
// porque o WiFi do P4 vive num ESP32-C6 auxiliar (esp_hosted) com bugs
// conhecidos - por isso NENHUMA funcao critica entra aqui.
//
// A rede NAO e fixa no codigo: o usuario escolhe na tela (configuracao >
// WIFI), digita a senha no teclado do aparelho, e o par ssid/senha mora na
// NVS. Trocou de casa, usa hotspot do celular? Escolhe outra rede na tela.
//
// O aparelho aparece na rede com o NOME DO CARRO (hostname), entao atualizar
// e: .\tools\build.ps1 firmware\mts_p4 -Upload -Port <IP da tela>
// (senha de OTA: "mts" - protege contra gravacao acidental do vizinho, nao
// contra adversario; quem esta no seu WiFi ja passou da sua porta)
#pragma once
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <Preferences.h>

static char g_wifiSsid[33] = "";
static char g_wifiPass[65] = "";
static bool g_wifiQuer = false;         // ha rede salva na NVS
static bool g_otaAtivo = false;
static bool g_otaEmCurso = false;
static uint32_t g_wifiTentaMs = 0;

// A tela de progresso e desenhada por quem conhece o painel (o .ino), via
// gancho - este arquivo nao sabe o que e um tft, de proposito.
static void (*g_otaDesenha)(int pct) = nullptr;

inline void wifiCarrega()
{
  Preferences p;
  p.begin("wifi", true);
  String s = p.getString("ssid", "");
  String w = p.getString("pass", "");
  p.end();
  strncpy(g_wifiSsid, s.c_str(), sizeof(g_wifiSsid) - 1);
  g_wifiSsid[sizeof(g_wifiSsid) - 1] = 0;
  strncpy(g_wifiPass, w.c_str(), sizeof(g_wifiPass) - 1);
  g_wifiPass[sizeof(g_wifiPass) - 1] = 0;
  g_wifiQuer = g_wifiSsid[0] != 0;
}

inline void wifiSalva(const char* ssid, const char* pass)
{
  Preferences p;
  p.begin("wifi", false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  strncpy(g_wifiSsid, ssid, sizeof(g_wifiSsid) - 1);
  g_wifiSsid[sizeof(g_wifiSsid) - 1] = 0;
  strncpy(g_wifiPass, pass, sizeof(g_wifiPass) - 1);
  g_wifiPass[sizeof(g_wifiPass) - 1] = 0;
  g_wifiQuer = g_wifiSsid[0] != 0;
  g_wifiTentaMs = 0;                    // tenta ja, nao daqui a 30 s
}

inline void wifiEsquece()
{
  wifiSalva("", "");
  WiFi.disconnect(true);
  Serial.println("wifi: rede esquecida");
}

inline bool wifiConectado() { return WiFi.status() == WL_CONNECTED; }

inline void otaInicia(const char* nomeAparelho)
{
  ArduinoOTA.setHostname(nomeAparelho);   // a tela aparece na rede pelo nome do carro
  ArduinoOTA.setPassword("mts");
  ArduinoOTA.onStart([]() {
    g_otaEmCurso = true;
    Serial.println("ota: iniciando - NAO desligue");
    if (g_otaDesenha) g_otaDesenha(0);
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    static int ultimo = -1;
    int pct = t ? (int)(p * 100UL / t) : 0;
    // repintar so quando o numero muda: desenhar a cada pacote atrasa a gravacao
    if (pct != ultimo && g_otaDesenha) { ultimo = pct; g_otaDesenha(pct); }
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("ota: gravado, reiniciando");
    if (g_otaDesenha) g_otaDesenha(100);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    g_otaEmCurso = false;
    Serial.printf("ota: erro %d\n", (int)e);
  });
  ArduinoOTA.begin();
  g_otaAtivo = true;
}

// No setup, DEPOIS do nome do carro estar carregado. Nao bloqueia: o begin do
// WiFi e assincrono e quem acompanha e o wifiAtualiza no loop.
inline void wifiInicia()
{
  wifiCarrega();
  if (!g_wifiQuer) return;
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_wifiSsid, g_wifiPass);
  Serial.printf("wifi: procurando '%s'...\n", g_wifiSsid);
}

// ------------------- atualizacao PELA INTERNET (servidor mts-ota na Vercel)
// O ArduinoOTA acima exige PC e tela no MESMO WiFi. Este caminho nao: a tela,
// em QUALQUER WiFi do mundo, consulta o manifesto do servidor de release e se
// atualiza sozinha quando ha versao maior que MTS_VERSAO. Publicar uma versao:
// subir MTS_VERSAO no hardware.h e rodar tools\publica_ota.ps1.
//
// Seguranca do v1, dita sem enfeite: HTTPS sem validar certificado
// (setInsecure) + MD5 do manifesto. Isso protege contra download corrompido e
// servidor errado por acidente; NAO protege contra adversario fazendo MITM na
// rede. Para 3 aparelhos de trilha e um risco aceito e registrado - assinatura
// de binario e o item 11 do PLANO_MELHORIAS.
// O "M" da ui.h (margem de 20 px) colide com um PARAMETRO chamado M dentro do
// bignum.h do mbedTLS, que estes tres includes puxam. Guarda o macro, some com
// ele durante os includes, e devolve depois.
#pragma push_macro("M")
#undef M
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Update.h>
#pragma pop_macro("M")

#define OTA_NUVEM_HOST "https://mts-ota.vercel.app"

// Extrai o valor de uma chave de um json RASO (o manifesto e nosso e tem 5
// campos - biblioteca de json inteira para isso seria peso morto).
inline bool otaJsonPega(const String& j, const char* chave, String& fora)
{
  char pat[24]; snprintf(pat, sizeof(pat), "\"%s\"", chave);
  int p = j.indexOf(pat); if (p < 0) return false;
  p = j.indexOf(':', p);  if (p < 0) return false;
  p++;
  while (p < (int)j.length() && (j[p] == ' ' || j[p] == '"')) p++;
  int f = p;
  while (f < (int)j.length() && j[f] != ',' && j[f] != '"' && j[f] != '}' &&
         j[f] != '\r' && j[f] != '\n') f++;
  fora = j.substring(p, f); fora.trim();
  return fora.length() > 0;
}

// CHAMAR SO DO LACO DA TELA INICIAL, nunca da trilha: a atualizacao para o
// radio e o GPS por ~1 min, e em movimento isso e inaceitavel. Como o loop()
// principal so roda na tela inicial (trilha e menus tem lacos proprios), o
// bloqueio ja acontece sozinho pela estrutura do programa.
inline void otaNuvemChecar()
{
  static uint32_t prox = 0;
  if (!wifiConectado() || g_otaEmCurso) return;
  if (!prox) prox = millis() + 40000;             // primeira: 40 s apos subir
  if ((int32_t)(millis() - prox) < 0) return;
  prox = millis() + 15UL * 60UL * 1000UL;         // depois: a cada 15 min

  NetworkClientSecure cli; cli.setInsecure(); cli.setTimeout(10000);
  HTTPClient http; http.setConnectTimeout(8000);
  if (!http.begin(cli, OTA_NUVEM_HOST "/version.json")) return;
  int rc = http.GET();
  if (rc != 200) { http.end(); Serial.printf("nuvem: manifesto HTTP %d\n", rc); return; }
  String j = http.getString(); http.end();

  String sv, sarq, smd5;
  if (!otaJsonPega(j, "versao", sv) || !otaJsonPega(j, "arq", sarq) ||
      !otaJsonPega(j, "md5", smd5)) { Serial.println("nuvem: manifesto ilegivel"); return; }
  int versao = sv.toInt();
  if (versao <= MTS_VERSAO) return;               // nada novo - silencio
  Serial.printf("nuvem: v%d disponivel (rodando v%d) - baixando %s\n",
                versao, MTS_VERSAO, sarq.c_str());

  // DOWNLOAD EM FATIAS (HTTP Range). Nem laco manual nem o HTTPUpdate do core
  // conseguem puxar 1,7 MB numa resposta so NESTA placa: o TLS do esp_hosted
  // para de entregar depois de ~8 KB (bug de transporte - o manifesto pequeno
  // funciona sempre, e o MESMO 1,7 MB desce inteiro por TCP puro no ArduinoOTA
  // da rede local; medido em 2026-08-25, duas implementacoes, mesmo sintoma).
  // Entao pedimos o binario em fatias de 8 KB com Range (o CDN da Vercel
  // responde 206, conferido por curl), numa conexao keep-alive para nao pagar
  // um aperto de mao TLS por fatia. Cada fatia e bufferizada INTEIRA antes do
  // Update.write, para a retentativa de uma fatia nao duplicar bytes gravados.
  String url = String(OTA_NUVEM_HOST) + sarq;
  int tam = 0;
  { String st; if (otaJsonPega(j, "bytes", st)) tam = st.toInt(); }
  if (tam <= 0) { Serial.println("nuvem: manifesto sem tamanho"); return; }
  if (!Update.begin(tam)) { Serial.println("nuvem: nao coube no slot"); return; }
  if (smd5.length() == 32) Update.setMD5(smd5.c_str());

  g_otaEmCurso = true;
  if (g_otaDesenha) g_otaDesenha(0);
  Serial.printf("nuvem: gravando %d bytes em fatias - NAO desligue\n", tam);

  static uint8_t fatia[8192];
  NetworkClientSecure cli2; cli2.setInsecure(); cli2.setTimeout(10000);
  HTTPClient h2; h2.setReuse(true); h2.setConnectTimeout(8000);
  int feito = 0, tent = 0, ultimo = -1;
  while (feito < tam && tent < 6) {
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
    h2.end();                      // com setReuse a conexao continua de pe
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
    delay(600);
    ESP.restart();
  }
  Update.abort();
  g_otaEmCurso = false;
  Serial.printf("nuvem: falhou (%d de %d bytes) - sigo na v%d, tento em 15 min\n",
                feito, tam, MTS_VERSAO);
}

// Chamar sempre (vai no servicoDeFundo). Reconecta sozinho a cada 30 s
// enquanto houver rede salva - e assim que "chegar em casa" liga o OTA sem
// ninguem tocar em nada.
inline void wifiAtualiza(const char* nomeAparelho)
{
  static bool conectadoAntes = false;
  const bool agora = wifiConectado();
  if (agora && !conectadoAntes) {
    Serial.printf("wifi: conectado a '%s', IP %s (OTA no ar como '%s')\n",
                  g_wifiSsid, WiFi.localIP().toString().c_str(), nomeAparelho);
    if (!g_otaAtivo) otaInicia(nomeAparelho);
  }
  if (!agora && conectadoAntes) Serial.println("wifi: conexao caiu");
  conectadoAntes = agora;

  if (!agora && g_wifiQuer && millis() - g_wifiTentaMs > 30000) {
    g_wifiTentaMs = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifiSsid, g_wifiPass);
  }
  if (g_otaAtivo && agora) ArduinoOTA.handle();
}
