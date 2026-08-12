/*
 * MTS - Minhoca Trail System | tela ESP32-P4
 *
 * ETAPA 2: abertura, tela inicial e configuracao.
 *
 * Sem radio, sem GPS, sem cartao - so a placa e o cabo. O visual pode ser feito e
 * conferido agora; criar/entrar em grupo precisa de radio, e radio precisa de
 * DOIS nos que se ouçam.
 *
 * Sobre a rede: as telas S3 falam LoRaMESH e o P4 vai falar SX1262. Os dois NAO se
 * conversam. A rede TDMA sera E22 em placas P4.
 *
 *   .\tools\build.ps1 firmware\mts_p4 -Board p4 -Upload -Port COM8
 */
#define MTS_SKIP_PANEL_ID 1     // este painel trava se voce ler o ID dele
#include "LGFX_P4_LCD5.h"
#include "ui.h"
#include "splash.h"
#include "teclado.h"
#include "grupo.h"
#include <Preferences.h>

LGFX_P4 tft;
Preferences prefs;

// Mesmo espaco e mesmas chaves do grupo_ws das telas S3, de proposito.
static char    g_nome[16] = "Carro";
static uint8_t g_cor = 0;
static const uint16_t CORES[] = { C_SUN, 0x2D7F, 0x4CCB, 0xFD20, 0xF81F, 0x07FF };
static const char*    CORES_NOME[] = { "laranja", "azul", "verde", "amarelo", "rosa", "ciano" };
static const int N_CORES = 6;

static char g_gNome[GRUPO_NOME_MAX] = "";
static char g_gCod[8] = "";

static void carregaCfg() {
  prefs.begin("grupo", true);
  String n = prefs.getString("name", "Carro");
  g_cor = prefs.getUChar("color", 0);
  prefs.end();
  if (g_cor >= N_CORES) g_cor = 0;
  strncpy(g_nome, n.c_str(), sizeof(g_nome) - 1);
  g_nome[sizeof(g_nome) - 1] = 0;
}
static void salvaCfg() {
  prefs.begin("grupo", false);
  prefs.putString("name", g_nome);
  prefs.putUChar("color", g_cor);
  prefs.end();
}

// ------------------------------------------------------------- tela inicial
static Ret hCriar, hEntrar, hEng;

static void desenhaInicial(int premido = -1)
{
  tft.fillScreen(C_BG);
  cabecalho(tft, "Minhoca Trail System", true, &hEng);

  // estado: hoje apagados, acendem quando o radio e o GPS entrarem
  int px = tft.width() - M - 68 - 16;
  int w2 = pastilha(tft, 0, 0, "GPS", "-", C_INK3);     // mede
  px -= w2; pastilha(tft, px, 48, "GPS", "-", C_INK3);
  int w1 = pastilha(tft, 0, 0, "RADIO", "-", C_INK3);
  px -= (w1 + 10); pastilha(tft, px, 48, "RADIO", "-", C_INK3);

  tft.drawFastHLine(0, 150, tft.width(), C_LINE);

  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString("COMECAR", M, 190);

  const int bw = 560, bh = 190, by = 238;
  hCriar  = { M, (int16_t)by, (int16_t)bw, (int16_t)bh };
  hEntrar = { (int16_t)(tft.width() - M - bw), (int16_t)by, (int16_t)bw, (int16_t)bh };
  botao(tft, hCriar,  "CRIAR GRUPO", "voce vira o lider da trilha", C_SUN, true,  premido == 0);
  botao(tft, hEntrar, "ENTRAR", "seguir alguem que ja saiu",        C_TAN, false, premido == 1);

  tft.drawFastHLine(0, 600, tft.width(), C_LINE);
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("ESTE APARELHO", M, 630);
  tft.fillRoundRect(M, 664, 22, 22, 5, CORES[g_cor]);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextColor(C_TAN);
  tft.drawString(g_nome, M + 34, 658);

  tft.setTextDatum(top_right);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("sem radio - etapa 2", tft.width() - M, 648);
  tft.setFont(&fonts::Font0);
}

// ---------------------------------------------------------------- avisos
static void telaAviso(const char* sub, const char* l1, const char* l2)
{
  Ret volta = { (int16_t)(tft.width() / 2 - 140), 470, 280, 88 };
  tft.fillScreen(C_BG);
  cabecalho(tft, sub, false);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.setTextColor(C_INK);
  tft.drawString(l1, tft.width() / 2, 300);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString(l2, tft.width() / 2, 366);
  botao(tft, volta, "VOLTAR", "", C_TAN, false);
  tft.setFont(&fonts::Font0);

  int16_t x, y;
  while (true) if (esperaToque(tft, x, y) && dentro(volta, x, y)) return;
}

// ---------------------------------------------------------- configuracao
static void telaConfig()
{
  const int lw = 1280 - 2 * M, lh = 118;
  Ret rNome  = { M, 172, (int16_t)lw, (int16_t)lh };
  Ret rCor   = { M, 306, (int16_t)lw, (int16_t)lh };
  Ret rTema  = { M, 440, (int16_t)lw, (int16_t)lh };
  Ret rVolta = { M, 596, 280, 88 };

  // desenho completo, UMA vez. Depois so as linhas que mudam - redesenhar a tela
  // inteira a cada toque e o que fazia isto parecer transicao de slide.
  auto desenhaTudo = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "configuracao", false);
    tft.drawFastHLine(0, 132, tft.width(), C_LINE);
    linhaCfg(tft, rNome, "NOME DESTE APARELHO", g_nome, "tocar para mudar >", true);
    linhaCfg(tft, rCor,  "COR NO MAPA", CORES_NOME[g_cor], "tocar para mudar >", true, CORES[g_cor]);
    linhaCfg(tft, rTema, "APARENCIA", "tema da trilha", "depois do mapa", false);
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    tft.setTextDatum(top_right);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("gravado no aparelho", tft.width() - M, 632);
    tft.setFont(&fonts::Font0);
  };
  desenhaTudo();

  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return;

    if (dentro(rNome, x, y)) {
      char tmp[sizeof(g_nome)];
      strncpy(tmp, g_nome, sizeof(tmp));
      bool ok = tecladoTexto(tft, "nome do aparelho", tmp, sizeof(tmp));
      if (ok) {
        if (!strlen(tmp)) strcpy(tmp, "Carro");
        strncpy(g_nome, tmp, sizeof(g_nome) - 1);
        g_nome[sizeof(g_nome) - 1] = 0;
        salvaCfg();
        Serial.printf("nome: %s\n", g_nome);
      }
      desenhaTudo();          // volta do teclado: aqui a tela inteira mudou mesmo
      continue;
    }
    if (dentro(rCor, x, y)) {
      g_cor = (g_cor + 1) % N_CORES;
      salvaCfg();
      // SO a linha da cor. Direto na tela, sem sprite de tela cheia.
      linhaCfg(tft, rCor, "COR NO MAPA", CORES_NOME[g_cor], "tocar para mudar >", true, CORES[g_cor]);
      continue;
    }
    if (dentro(rTema, x, y)) {
      telaAviso("aparencia", "Ainda nao", "O tema entra depois que o mapa existir");
      desenhaTudo();
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
  tft.setRotation(1);

  Serial.printf("painel %s %dx%d | psram livre %u | nome=%s\n",
                tft.panelName, tft.width(), tft.height(),
                (unsigned)ESP.getFreePsram(), g_nome);

  splashMostrar(tft);
  desenhaInicial();
}

void loop()
{
  int16_t x, y;
  if (!esperaToque(tft, x, y, 400)) return;

  if (dentro(hEng, x, y)) { telaConfig(); desenhaInicial(); return; }

  // O retorno ao toque vai DIRETO na tela, so no retangulo do botao. Redesenhar
  // a tela inteira so para acender um botao e o que dava sensacao de lentidao.
  if (dentro(hCriar, x, y)) {
    botao(tft, hCriar, "CRIAR GRUPO", "voce vira o lider da trilha", C_SUN, true, true);
    if (telaCriarGrupo(tft, g_nome, g_gNome, g_gCod)) {
      Serial.printf("grupo criado: %s | codigo %s\n", g_gNome, g_gCod);
      telaAviso(g_gNome, "Trilha aberta", "Falta o radio para os outros te acharem");
    }
    desenhaInicial();
  } else if (dentro(hEntrar, x, y)) {
    botao(tft, hEntrar, "ENTRAR", "seguir alguem que ja saiu", C_TAN, false, true);
    int g = telaEntrarGrupo(tft);
    if (g >= 0) {
      Serial.printf("entrou em: %s\n", VIZINHOS[g].nome);
      telaAviso(VIZINHOS[g].nome, "Codigo aceito", "Falta o radio para seguir de verdade");
    }
    desenhaInicial();
  }
}
