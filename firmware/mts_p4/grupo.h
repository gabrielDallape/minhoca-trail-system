// Telas de grupo do MTS.
//
// COMO FUNCIONA, decidido com o usuario:
//   CRIAR  - o aparelho propoe "Grupo do <nome do carro>" e sorteia um CODIGO de
//            5 digitos. O nome da para editar; o codigo e a senha que os outros
//            vao precisar. Quem cria vira o lider.
//   ENTRAR - lista os grupos por perto; ao escolher um, pede o codigo.
//
// Nada disto fala com radio ainda: a lista e simulada e esta rotulada como tal na
// tela, de proposito. Grupo de verdade precisa de dois nos que se ouçam.
#pragma once
#include "ui.h"
#include "teclado.h"

#define GRUPO_NOME_MAX 24
#define GRUPO_COD_DIG   5

struct GrupoVizinho {
  const char* nome;
  const char* lider;
  uint8_t     carros;
  int8_t      rssi;      // dBm
};

// Enquanto nao ha radio, e isto que a tela mostra - com aviso na propria tela.
static const GrupoVizinho VIZINHOS[] = {
  { "Grupo do Marcao",   "Marcao",   4, -62 },
  { "Trilha da Serra",   "Ze",       2, -78 },
  { "Grupo do Gabriel",  "Gabriel",  7, -91 },
};
static const int N_VIZINHOS = sizeof(VIZINHOS) / sizeof(VIZINHOS[0]);

// barrinhas de sinal, 4 niveis
template <typename G>
void sinal(G& g, int x, int y, int8_t rssi)
{
  int n = rssi > -70 ? 4 : rssi > -80 ? 3 : rssi > -90 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 8 + i * 6;
    g.fillRoundRect(x + i * 11, y + 26 - h, 7, h, 2, i < n ? C_SUN : C_LINE);
  }
}

// ---------------------------------------------------------------- CRIAR
// Devolve true se o usuario confirmou a abertura do grupo.
template <typename TFT>
bool telaCriarGrupo(TFT& tft, const char* nomeCarro, char* gNome, char* gCod)
{
  // proposta padrao: "Grupo do <carro>". O usuario pode trocar.
  if (!gNome[0]) snprintf(gNome, GRUPO_NOME_MAX, "Grupo do %s", nomeCarro);
  if (!gCod[0]) {
    // o codigo e a senha: sorteado uma vez, e o dono repassa de boca
    uint32_t r = (uint32_t)esp_random() % 90000 + 10000;
    snprintf(gCod, 8, "%lu", (unsigned long)r);
  }

  Ret rNome  = { M, 176, (int16_t)(1280 - 2 * M), 130 };
  Ret rCod   = { M, 328, (int16_t)(1280 - 2 * M), 150 };
  Ret rVolta = { M, 596, 240, 88 };
  Ret rAbrir = { (int16_t)(1280 - M - 380), 596, 380, 88 };

  auto desenha = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "criar grupo - voce e o lider", false);
    tft.drawFastHLine(0, 132, tft.width(), C_LINE);

    linhaCfg(tft, rNome, "NOME DO GRUPO", gNome, "tocar para mudar >", true);

    // o codigo em destaque: e o que o dono vai ditar para os outros
    tft.fillRoundRect(rCod.x, rCod.y, rCod.w, rCod.h, 14, C_SURF);
    tft.fillRoundRect(rCod.x, rCod.y, 5, rCod.h, 2, C_SUN);
    tft.setTextDatum(top_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("CODIGO DO GRUPO", rCod.x + 30, rCod.y + 22);
    tft.setTextDatum(middle_left);
    tft.setFont(&fonts::FreeSansBold24pt7b);
    tft.setTextColor(C_SUN);
    int cx = rCod.x + 30;
    for (int i = 0; i < GRUPO_COD_DIG; i++) {
      char s[2] = { gCod[i], 0 };
      tft.drawString(s, cx, rCod.y + 96);
      cx += 54;
    }
    tft.setTextDatum(middle_right);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK2);
    tft.drawString("quem quiser entrar precisa deste numero", rCod.x + rCod.w - 30, rCod.y + rCod.h / 2);

    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    botao(tft, rAbrir, "ABRIR TRILHA", "", C_SUN, true);
    tft.setFont(&fonts::Font0);
  };
  desenha();

  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return false;
    if (dentro(rAbrir, x, y)) return true;
    if (dentro(rNome, x, y)) {
      char tmp[GRUPO_NOME_MAX];
      strncpy(tmp, gNome, sizeof(tmp)); tmp[sizeof(tmp) - 1] = 0;
      if (tecladoTexto(tft, "nome do grupo", tmp, sizeof(tmp)) && strlen(tmp))
        strncpy(gNome, tmp, GRUPO_NOME_MAX - 1);
      desenha();
    }
  }
}

// --------------------------------------------------------------- ENTRAR
// Devolve o indice do grupo escolhido e ja validado pelo codigo, ou -1.
template <typename TFT>
int telaEntrarGrupo(TFT& tft)
{
  Ret rVolta = { M, 596, 240, 88 };
  auto cartao = [&](int i) -> Ret {
    return Ret{ M, (int16_t)(176 + i * 128), (int16_t)(1280 - 2 * M), 112 };
  };

  auto desenha = [&](int premido) {
    tft.fillScreen(C_BG);
    cabecalho(tft, "entrar num grupo", false);
    tft.drawFastHLine(0, 132, tft.width(), C_LINE);

    for (int i = 0; i < N_VIZINHOS; i++) {
      Ret r = cartao(i);
      tft.fillRoundRect(r.x, r.y, r.w, r.h, 14, premido == i ? C_SURF2 : C_SURF);
      tft.setTextDatum(top_left);
      tft.setFont(&fonts::FreeSansBold18pt7b);
      tft.setTextColor(C_TAN);
      tft.drawString(VIZINHOS[i].nome, r.x + 30, r.y + 24);
      tft.setFont(&fonts::FreeSans9pt7b);
      tft.setTextColor(C_INK3);
      char sub[64];
      snprintf(sub, sizeof(sub), "lider %s  -  %d carros", VIZINHOS[i].lider, VIZINHOS[i].carros);
      tft.drawString(sub, r.x + 30, r.y + 70);

      sinal(tft, r.x + r.w - 200, r.y + 42, VIZINHOS[i].rssi);
      tft.setTextDatum(middle_right);
      tft.setTextColor(C_SUN);
      tft.drawString("pede codigo >", r.x + r.w - 30, r.y + r.h / 2);
    }

    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    tft.setTextDatum(middle_right);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_WARN);
    tft.drawString("lista simulada - ainda sem radio", tft.width() - M, 640);
    tft.setFont(&fonts::Font0);
  };
  desenha(-1);

  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return -1;
    for (int i = 0; i < N_VIZINHOS; i++) {
      if (!dentro(cartao(i), x, y)) continue;
      char cod[8] = {0};
      char dica[80];
      snprintf(dica, sizeof(dica), "codigo de %s", VIZINHOS[i].nome);
      if (tecladoNumero(tft, "entrar no grupo", dica, cod, GRUPO_COD_DIG)) return i;
      desenha(-1);
      break;
    }
  }
}
