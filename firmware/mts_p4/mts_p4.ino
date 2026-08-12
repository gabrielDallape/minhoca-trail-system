/*
 * MTS - Minhoca Trail System | tela ESP32-P4
 *
 * ETAPA 2: abertura + tela inicial + configuracao.
 *
 * O que JA da para testar sem periferico nenhum: a abertura, o toque, a tela
 * inicial e a troca do nome do aparelho (que persiste na NVS).
 * O que AINDA nao existe: criar/entrar em grupo de verdade - isso precisa de
 * radio, e radio precisa de DOIS nos que se ouçam.
 *
 * Sobre a rede: as telas S3 falam LoRaMESH e o P4 vai falar SX1262. Os dois NAO se
 * conversam. Uma rede TDMA precisa de dois E22 em duas placas P4.
 *
 *   .\tools\build.ps1 firmware\mts_p4 -Board p4 -Upload -Port COM8
 */
#define MTS_SKIP_PANEL_ID 1     // o painel Waveshare trava se voce ler o ID dele
#include "LGFX_P4_LCD5.h"
#include "splash.h"
#include "ui.h"
#include "teclado.h"
#include <Preferences.h>

LGFX_P4 tft;
Preferences prefs;

// Mesmo espaco e mesmas chaves do grupo_ws das telas S3, de proposito: a ideia de
// configuracao e a mesma, so a tela mudou.
static char  g_nome[16] = "Carro";
static uint8_t g_tema = 0;

static void carregaCfg() {
  prefs.begin("grupo", true);
  String n = prefs.getString("name", "Carro");
  g_tema = prefs.getUChar("theme", 0);
  prefs.end();
  strncpy(g_nome, n.c_str(), sizeof(g_nome) - 1);
  g_nome[sizeof(g_nome) - 1] = 0;
}
static void salvaCfg() {
  prefs.begin("grupo", false);
  prefs.putString("name", g_nome);
  prefs.putUChar("theme", g_tema);
  prefs.end();
}

// ------------------------------------------------------------- tela inicial
static Ret hCriar, hEntrar, hEng;

static void desenhaInicial()
{
  tft.fillScreen(C_BG);
  hEng = cabecalho(tft, "trilha", true);

  const int bw = 460, bh = 132;
  const int gap = 40;
  const int y = 300;
  hCriar  = { (int16_t)(tft.width() / 2 - bw - gap / 2), (int16_t)y, (int16_t)bw, (int16_t)bh };
  hEntrar = { (int16_t)(tft.width() / 2 + gap / 2),      (int16_t)y, (int16_t)bw, (int16_t)bh };

  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString("Comece uma trilha ou entre na de alguem", tft.width() / 2, 210);

  botao(tft, hCriar,  "CRIAR GRUPO",  C_ORANGE, true);
  botao(tft, hEntrar, "ENTRAR", C_TAN, false);

  // rodape: quem e este aparelho
  tft.drawFastHLine(0, tft.height() - 78, tft.width(), C_LINE);
  tft.setTextDatum(middle_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("ESTE APARELHO", 40, tft.height() - 46);
  tft.setFont(&fonts::FreeSansBold12pt7b);
  tft.setTextColor(C_TAN);
  tft.drawString(g_nome, 40 + 150, tft.height() - 45);

  tft.setTextDatum(middle_right);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("sem radio - etapa 2", tft.width() - 40, tft.height() - 46);
  tft.setFont(&fonts::Font0);
}

// ------------------------------------------------------------- configuracao
static void telaAviso(const char* titulo, const char* linha1, const char* linha2)
{
  tft.fillScreen(C_BG);
  cabecalho(tft, titulo, false);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextColor(C_INK);
  tft.drawString(linha1, tft.width() / 2, 300);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString(linha2, tft.width() / 2, 360);

  Ret volta = { (int16_t)(tft.width() / 2 - 130), 480, 260, 96 };
  botao(tft, volta, "VOLTAR", C_TAN, false);
  tft.setFont(&fonts::Font0);
  int16_t x, y;
  while (true) { if (esperaToque(tft, x, y) && dentro(volta, x, y)) return; }
}

static void telaConfig()
{
  while (true) {
    tft.fillScreen(C_BG);
    cabecalho(tft, "configuracao", false);

    const int lx = 60, lw = tft.width() - 120, lh = 110;
    Ret rNome  = { (int16_t)lx, 150, (int16_t)lw, (int16_t)lh };
    Ret rTema  = { (int16_t)lx, 280, (int16_t)lw, (int16_t)lh };
    Ret rVolta = { (int16_t)(tft.width() / 2 - 130), 560, 260, 96 };

    // linha: nome do aparelho
    tft.fillRoundRect(rNome.x, rNome.y, rNome.w, rNome.h, 12, C_SURF);
    tft.setTextDatum(middle_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("NOME DESTE APARELHO", rNome.x + 28, rNome.y + 34);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextColor(C_TAN);
    tft.drawString(g_nome, rNome.x + 28, rNome.y + 74);
    tft.setTextDatum(middle_right);
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(C_ORANGE);
    tft.drawString("tocar para mudar", rNome.x + rNome.w - 28, rNome.y + rNome.h / 2);

    // linha: aparencia (ainda nao)
    tft.fillRoundRect(rTema.x, rTema.y, rTema.w, rTema.h, 12, C_SURF);
    tft.setTextDatum(middle_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("APARENCIA", rTema.x + 28, rTema.y + 34);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("tema da trilha", rTema.x + 28, rTema.y + 74);
    tft.setTextDatum(middle_right);
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("em breve", rTema.x + rTema.w - 28, rTema.y + rTema.h / 2);

    botao(tft, rVolta, "VOLTAR", C_TAN, false);
    tft.setFont(&fonts::Font0);

    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return;
    if (dentro(rNome, x, y)) {
      char tmp[sizeof(g_nome)];
      strncpy(tmp, g_nome, sizeof(tmp));
      if (tecladoTexto(tft, "nome do aparelho", tmp, sizeof(tmp))) {
        if (strlen(tmp) == 0) strcpy(tmp, "Carro");
        strncpy(g_nome, tmp, sizeof(g_nome) - 1);
        g_nome[sizeof(g_nome) - 1] = 0;
        salvaCfg();
        Serial.printf("nome salvo: %s\n", g_nome);
      }
    }
    if (dentro(rTema, x, y)) {
      telaAviso("aparencia", "Ainda nao", "O tema entra depois que o mapa existir");
    }
  }
}

// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) delay(10);
  Serial.println("\n=== MTS - Minhoca Trail System ===");

  carregaCfg();

  if (!tft.init()) { Serial.println("ERRO: painel nao inicializou."); while (true) delay(1000); }
  // Painel nativo 720x1280 em pe; giramos para paisagem. Medido: a rotacao por
  // software custa 0% aqui (14,9 ms a tela cheia nas duas orientacoes).
  tft.setRotation(1);
  Serial.printf("painel %s %dx%d | nome=%s\n", tft.panelName, tft.width(), tft.height(), g_nome);

  splashMostrar(tft);
  desenhaInicial();
}

void loop()
{
  int16_t x, y;
  if (!esperaToque(tft, x, y, 500)) return;

  if (dentro(hEng, x, y)) { telaConfig(); desenhaInicial(); return; }

  if (dentro(hCriar, x, y)) {
    botao(tft, hCriar, "CRIAR GRUPO", C_ORANGE, true, true);
    telaAviso("criar grupo", "Precisa do radio", "Solde um E22 e ligue em duas placas");
    desenhaInicial();
    return;
  }
  if (dentro(hEntrar, x, y)) {
    botao(tft, hEntrar, "ENTRAR", C_TAN, false, true);
    telaAviso("entrar no grupo", "Precisa do radio", "Solde um E22 e ligue em duas placas");
    desenhaInicial();
    return;
  }
}
