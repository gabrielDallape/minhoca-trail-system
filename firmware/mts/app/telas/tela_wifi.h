#pragma once
// ============================================================================
//  TELA DO WIFI - escolher a rede da garagem (so compila com MTS_TEM_WIFI 1)
// ============================================================================
// Procura as redes do ar e deixa escolher UMA, com a senha digitada aqui mesmo.
// A rede mora na NVS ate ser trocada ou esquecida - nenhum ssid vive no codigo.

#include "ui.h"
#include "teclado.h"
#include "../wifi_ota.h"

template <typename TFT>
void telaWifi(TFT& tft)
{
  Ret rVolta = { M, (int16_t)(TELA_H - M - 76), 240, 76 };
  Ret rNovo  = { (int16_t)(TELA_W / 2 - 150), (int16_t)(TELA_H - M - 76), 300, 76 };
  Ret rEsq   = { (int16_t)(TELA_W - M - 320), (int16_t)(TELA_H - M - 76), 320, 76 };
  const int topo = CAB_H + 14, lh = 62, vao = 8;
  auto rRede = [&](int i) -> Ret { return Ret{ M, (int16_t)(topo + i * (lh + vao)), (int16_t)(TELA_W - 2 * M), (int16_t)lh }; };

  while (true) {
    tft.fillScreen(C_BG);
    cabecalho(tft, "wifi", false);
    tft.setTextDatum(middle_center); tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK2);
    tft.drawString("procurando redes...", TELA_W / 2, TELA_H / 2);
    tft.setFont(&fonts::Font0);

    // Varrer NO MEIO de uma tentativa de conexao devolve lista vazia (esp_hosted).
    WiFi.mode(WIFI_STA);
    if (!wifiConectado()) { WiFi.disconnect(); esperaServindo(200); }
    int n = WiFi.scanNetworks();
    if (n <= 0) { esperaServindo(400); n = WiFi.scanNetworks(); }

    const int MAXR = 20;
    String nomes[MAXR]; int forca[MAXR]; bool trancada[MAXR];
    int u = 0;
    for (int i = 0; i < n && u < MAXR; i++) {
      String ss = WiFi.SSID(i);
      if (!ss.length()) continue;
      int j = 0; for (; j < u; j++) if (nomes[j] == ss) break;
      if (j < u) { if (WiFi.RSSI(i) > forca[j]) forca[j] = WiFi.RSSI(i); continue; }
      nomes[u] = ss; forca[u] = WiFi.RSSI(i); trancada[u] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN; u++;
    }
    WiFi.scanDelete();
    for (int a = 1; a < u; a++)
      for (int b = a; b > 0 && forca[b] > forca[b - 1]; b--) {
        String ts = nomes[b]; nomes[b] = nomes[b-1]; nomes[b-1] = ts;
        int tf = forca[b]; forca[b] = forca[b-1]; forca[b-1] = tf;
        bool tt = trancada[b]; trancada[b] = trancada[b-1]; trancada[b-1] = tt;
      }
    if (u > 6) u = 6;

    tft.fillScreen(C_BG);
    cabecalho(tft, "wifi", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
    if (u <= 0) {
      tft.setTextDatum(middle_center); tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK3);
      tft.drawString("nenhuma rede encontrada", TELA_W / 2, TELA_H / 2 - 60);
      tft.drawString("toque PROCURAR DE NOVO", TELA_W / 2, TELA_H / 2 - 16);
      tft.setFont(&fonts::Font0);
    }
    for (int i = 0; i < u; i++) {
      Ret r = rRede(i);
      bool salva = g_wifiQuer && nomes[i] == g_wifiSsid;
      tft.fillRoundRect(r.x, r.y, r.w, r.h, 12, C_SURF);
      tft.fillRoundRect(r.x, r.y, 5, r.h, 2, salva ? C_OK : C_SUN);
      tft.setTextDatum(middle_left); tft.setFont(&fonts::FreeSansBold12pt7b); tft.setTextColor(C_INK);
      char nm[25]; strncpy(nm, nomes[i].c_str(), 24); nm[24] = 0;
      tft.drawString(nm, r.x + 24, r.y + r.h / 2);
      int niv = forca[i] > -55 ? 4 : forca[i] > -67 ? 3 : forca[i] > -80 ? 2 : 1;
      for (int b = 0; b < 4; b++) { int hh = 8 + b * 6; tft.fillRect(r.x + r.w - 70 + b * 11, r.y + r.h / 2 + 14 - hh, 7, hh, b < niv ? C_SUN : C_LINE); }
      if (salva || !trancada[i]) {
        tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(salva ? C_OK : C_INK3); tft.setTextDatum(middle_right);
        tft.drawString(salva ? (wifiConectado() ? "conectada" : "salva") : "aberta", r.x + r.w - 90, r.y + r.h / 2);
      }
    }
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    botao(tft, rNovo, "PROCURAR DE NOVO", "", C_INK2, false);
    if (g_wifiQuer) botao(tft, rEsq, "ESQUECER REDE", "", C_RED, false);
    tft.setFont(&fonts::Font0);

    bool refazer = false;
    while (!refazer) {
      int16_t x, y;
      if (!esperaToque(tft, x, y)) continue;
      if (dentro(rVolta, x, y)) return;
      if (dentro(rNovo, x, y)) { refazer = true; break; }
      if (g_wifiQuer && dentro(rEsq, x, y)) { wifiEsquece(); return; }
      for (int i = 0; i < u; i++) {
        if (!dentro(rRede(i), x, y)) continue;
        char pass[33] = "";
        if (trancada[i]) {
          char titulo[48]; snprintf(titulo, sizeof(titulo), "senha de %s", nomes[i].c_str());
          if (!tecladoTexto(tft, titulo, pass, sizeof(pass), true)) { refazer = true; break; }
        }
        wifiSalva(nomes[i].c_str(), pass);
        tft.fillScreen(C_BG);
        cabecalho(tft, "wifi", false);
        tft.setTextDatum(middle_center);
        bool ok = false;
        for (int tent = 1; tent <= 2 && !ok; tent++) {     // o esp_hosted perde o 1o begin() de vez em quando
          char msg[64]; snprintf(msg, sizeof(msg), "conectando em %s  (%d/2)", nomes[i].c_str(), tent);
          tft.fillRect(0, TELA_H / 2 - 40, TELA_W, 100, C_BG);
          tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK2);
          tft.drawString(msg, TELA_W / 2, TELA_H / 2);
          WiFi.disconnect(); esperaServindo(200);
          WiFi.begin(g_wifiSsid, g_wifiPass);
          uint32_t t0 = millis();
          while (!(ok = wifiConectado()) && millis() - t0 < 12000) esperaServindo(200);
        }
        tft.setFont(&fonts::FreeSansBold18pt7b);
        tft.fillRect(0, TELA_H / 2 - 40, TELA_W, 100, C_BG);
        if (ok) {
          char okm[64]; snprintf(okm, sizeof(okm), "conectado - IP %s", WiFi.localIP().toString().c_str());
          tft.setTextColor(C_OK); tft.drawString(okm, TELA_W / 2, TELA_H / 2);
        } else {
          tft.setTextColor(C_RED); tft.drawString("nao conectou", TELA_W / 2, TELA_H / 2 - 16);
          tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK3);
          tft.drawString("senha errada ou sinal fraco - toque a rede e tente de novo", TELA_W / 2, TELA_H / 2 + 36);
        }
        tft.setFont(&fonts::Font0);
        esperaServindo(2400);
        if (ok) return;
        refazer = true;
        break;
      }
    }
  }
}
