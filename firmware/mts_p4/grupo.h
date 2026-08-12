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
#include "sessao.h"

#define GRUPO_COD_DIG   5
#define MAX_MEMBROS     8

// Quem esta no grupo. Enquanto nao ha radio, entram sozinhos com o tempo, so para
// a sala de espera poder ser vista funcionando.
struct Membro { char nome[16]; uint8_t cor; bool lider; };

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

// ------------------------------------------------------- SALA DE ESPERA
// Depois de criar, antes de abrir a trilha: o codigo em destaque para o dono
// ditar, e a lista de quem ja entrou crescendo. Quem chega DEPOIS de abrir a
// trilha tambem entra - abrir nao fecha o grupo, so tira o lider desta tela.
//
// Desenha uma vez e depois so a linha nova. Repintar a lista inteira a cada
// chegada e o que dava sensacao de transicao de slide.
template <typename TFT>
bool telaSalaEspera(TFT& tft, const char* gNome, const char* gCod,
                    Membro* membros, int& nMembros)
{
  Ret rSair  = { M, 596, 240, 88 };
  Ret rAbrir = { (int16_t)(1280 - M - 380), 596, 380, 88 };

  auto linhaMembro = [&](int i) -> Ret {
    return Ret{ (int16_t)(M + 640), (int16_t)(190 + i * 62), (int16_t)(1280 - M - (M + 640)), 54 };
  };
  auto pintaMembro = [&](int i) {
    Ret r = linhaMembro(i);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, C_SURF);
    tft.fillRoundRect(r.x + 16, r.y + 16, 22, 22, 5, CORES_MAPA[membros[i].cor % 6]);
    tft.setTextDatum(middle_left);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.setTextColor(C_TAN);
    tft.drawString(membros[i].nome, r.x + 50, r.y + r.h / 2);
    if (membros[i].lider) {
      tft.setTextDatum(middle_right);
      tft.setFont(&fonts::FreeSans9pt7b);
      tft.setTextColor(C_SUN);
      tft.drawString("LIDER", r.x + r.w - 18, r.y + r.h / 2);
    }
    tft.setFont(&fonts::Font0);
  };
  auto pintaContagem = [&]() {
    tft.fillRect(M + 640, 150, 1280 - M - (M + 640), 30, C_BG);
    tft.setTextDatum(top_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    char c[40]; snprintf(c, sizeof(c), "NO GRUPO  (%d)", nMembros);
    tft.drawString(c, M + 640, 152);
    tft.setFont(&fonts::Font0);
  };

  tft.fillScreen(C_BG);
  cabecalho(tft, "sala de espera", false);
  tft.drawFastHLine(0, 132, tft.width(), C_LINE);

  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextColor(C_TAN);
  tft.drawString(gNome, M, 150);

  // o codigo fica GRANDE e permanente: o dono precisa ditar isso, e nao pode
  // depender de ter anotado em outro lugar
  tft.fillRoundRect(M, 208, 560, 170, 14, C_SURF);
  tft.fillRoundRect(M, 208, 5, 170, 2, C_SUN);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("CODIGO DO GRUPO", M + 30, 232);
  tft.setTextDatum(middle_left);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.setTextColor(C_SUN);
  int cx = M + 30;
  for (int i = 0; i < GRUPO_COD_DIG; i++) { char s[2] = { gCod[i], 0 }; tft.drawString(s, cx, 310); cx += 56; }
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString("dite este numero para quem for entrar", M + 30, 344);

  tft.setTextColor(C_INK3);
  tft.drawString("A trilha pode ser aberta a qualquer momento.", M, 412);
  tft.drawString("Quem chegar depois tambem entra com o mesmo codigo.", M, 440);

  botao(tft, rSair, "< CANCELAR", "", C_INK2, false);
  botao(tft, rAbrir, "ABRIR TRILHA", "", C_SUN, true);
  pintaContagem();
  for (int i = 0; i < nMembros; i++) pintaMembro(i);
  tft.setFont(&fonts::Font0);

  // SIMULACAO: sem radio, os seguidores "chegam" sozinhos para a tela poder ser
  // vista funcionando. Com o E22 isto vira o pacote de JOIN.
  static const char* FALSOS[] = { "Marcao", "Ze do Pneu", "Bia", "Tuninho", "Serra" };
  uint32_t proximo = millis() + 2600;
  int falsoIdx = 0;

  while (true) {
    if (millis() > proximo && nMembros < MAX_MEMBROS && falsoIdx < 5) {
      strncpy(membros[nMembros].nome, FALSOS[falsoIdx++], 15);
      membros[nMembros].nome[15] = 0;
      membros[nMembros].cor = (uint8_t)(nMembros + 1);
      membros[nMembros].lider = false;
      pintaMembro(nMembros);          // SO a linha nova
      nMembros++;
      pintaContagem();
      proximo = millis() + 3200 + (esp_random() % 2600);
    }
    int16_t x, y;
    if (!esperaToque(tft, x, y, 150)) continue;
    if (dentro(rAbrir, x, y)) return true;
    if (dentro(rSair, x, y))  return false;
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

// ---------------------------------------------------------------- TRILHA
// A tela em que o aparelho VIVE durante a trilha. Aqui vai entrar o mapa; por
// enquanto mostra o grupo, o codigo (para quem chegar depois) e quem esta dentro.
//
// E para ca que o aparelho volta ao religar: se a sessao estiver ativa, o menu e
// PULADO. Numa trilha, religar e ter de remontar grupo seria o motorista mexendo
// na tela em vez de olhar a estrada.
//
// Devolve true quando o usuario sai da trilha.
template <typename TFT>
bool telaTrilha(TFT& tft, const Sessao& s, Membro* membros, int nMembros)
{
  Ret rSair = { (int16_t)(1280 - M - 300), (int16_t)(720 - 108), 300, 88 };

  tft.fillScreen(C_BG);
  cabecalho(tft, s.lider ? "voce e o lider" : "seguindo", false);
  tft.drawFastHLine(0, 132, tft.width(), C_LINE);

  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.setTextColor(C_TAN);
  tft.drawString(s.gNome, M, 154);

  // o codigo continua a vista: quem chegar depois ainda precisa dele
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("CODIGO", M, 216);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextColor(C_SUN);
  tft.drawString(s.gCod, M + 90, 210);

  // onde o mapa entra
  tft.drawRoundRect(M, 260, 700, 330, 14, C_LINE);
  tft.setTextDatum(middle_center);
  tft.setFont(&fonts::FreeSans12pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("o mapa entra aqui", M + 350, 410);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.drawString("precisa de GPS e radio", M + 350, 446);

  // quem esta na trilha
  int lx = M + 740;
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  char c[40]; snprintf(c, sizeof(c), "NA TRILHA  (%d)", nMembros);
  tft.drawString(c, lx, 262);
  for (int i = 0; i < nMembros && i < 6; i++) {
    int y = 292 + i * 58;
    tft.fillRoundRect(lx, y, 1280 - M - lx, 50, 10, C_SURF);
    tft.fillRoundRect(lx + 14, y + 14, 22, 22, 5, CORES_MAPA[membros[i].cor % N_CORES]);
    tft.setTextDatum(middle_left);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.setTextColor(C_TAN);
    tft.drawString(membros[i].nome, lx + 48, y + 25);
    if (membros[i].lider) {
      tft.setTextDatum(middle_right);
      tft.setFont(&fonts::FreeSans9pt7b);
      tft.setTextColor(C_SUN);
      tft.drawString("LIDER", 1280 - M - 16, y + 25);
    }
  }

  botao(tft, rSair, "SAIR DA TRILHA", "", C_INK2, false);
  tft.setTextDatum(middle_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("desligar e religar volta para esta tela", M, 720 - 64);
  tft.setFont(&fonts::Font0);

  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y, 300)) continue;
    if (dentro(rSair, x, y)) {
      // confirmar: sair e destrutivo (perde o grupo), e o dedo escorrega
      Ret sim = { (int16_t)(1280 / 2 - 320), 400, 300, 96 };
      Ret nao = { (int16_t)(1280 / 2 + 20), 400, 300, 96 };
      tft.fillScreen(C_BG);
      cabecalho(tft, "sair da trilha", false);
      tft.setTextDatum(middle_center);
      tft.setFont(&fonts::FreeSansBold24pt7b);
      tft.setTextColor(C_INK);
      tft.drawString("Sair do grupo?", 1280 / 2, 250);
      tft.setFont(&fonts::FreeSans12pt7b);
      tft.setTextColor(C_INK2);
      tft.drawString("Voce sai da trilha e volta ao menu inicial", 1280 / 2, 310);
      botao(tft, nao, "FICAR", "", C_TAN, false);
      botao(tft, sim, "SAIR", "", C_SUN, true);
      tft.setFont(&fonts::Font0);
      while (true) {
        int16_t a, b;
        if (!esperaToque(tft, a, b)) continue;
        if (dentro(sim, a, b)) return true;
        if (dentro(nao, a, b)) return telaTrilha(tft, s, membros, nMembros);
      }
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
