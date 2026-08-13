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
#include "mapa.h"

// definido no sketch: grava o tema escolhido na NVS
void salvaTema();

#define GRUPO_COD_DIG   5
#define MAX_MEMBROS     8

// Quem esta no grupo. Enquanto nao ha radio, entram sozinhos com o tempo, so para
// a sala de espera poder ser vista funcionando.
struct Membro {
  char    nome[16];
  uint8_t cor;
  bool    lider;
  int16_t dist;     // metros ate mim. -1 = ainda sem posicao
  bool    alerta;   // este carro pediu socorro
};

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
      membros[nMembros].dist = -1;      // ainda sem posicao
      membros[nMembros].alerta = false;
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
// A tela em que o aparelho VIVE. O MAPA E A TELA INTEIRA; o resto flutua no canto
// ocupando o minimo. Numa trilha o motorista olha o caminho - nome de grupo,
// codigo e lista de carros sao consulta rapida, nao conteudo.
//
// AQUI o sprite de tela cheia se justifica, ao contrario das outras telas: o mapa
// redesenha varias vezes por segundo e sem buffer de tras a tela piscaria. Nas
// telas que so mudam ao toque ele era desperdicio e dava sensacao de slide.
//
// Devolve true quando o usuario sai da trilha.
template <typename TFT>
bool telaTrilha(TFT& tft, const Sessao& s, Membro* membros, int nMembros)
{
  Ret rSair = { M, (int16_t)(720 - M - 76), 200, 76 };
  Ret rModo = { (int16_t)(1280 - 20 - 76), (int16_t)(720 - 20 - 76), 76, 76 };
  Ret rZoom = { (int16_t)(1280 - 20 - 76), (int16_t)(720 - 20 - 76 - 86), 76, 76 };
  const int cw = 290, ch = 54, cxr = 1280 - 20 - cw;
  auto faixaCarro = [&](int i) -> Ret {
    return Ret{ (int16_t)cxr, (int16_t)(20 + (i - 1) * (ch + 6)), (int16_t)cw, (int16_t)ch };
  };

  static const double ZOOMS[] = { 0.4, 0.9, 2.0, 5.0, 12.0 };
  int zi = 1;

  LGFX_Sprite cv(&tft);
  cv.setPsram(true); cv.setColorDepth(16);
  bool temBuffer = cv.createSprite(1280, 720);

  auto pinta = [&]() {
    auto& g = temBuffer ? (LovyanGFX&)cv : (LovyanGFX&)tft;

    mapaDesenha(g, 1280, 720, ZOOMS[zi]);

    // canto superior esquerdo: nome do grupo. Codigo so para o LIDER - e ele quem
    // dita o numero; seguidor nao tem o que fazer com ele.
    g.setTextDatum(top_left);
    g.setFont(&fonts::FreeSansBold24pt7b);
    g.setTextColor(C_TAN);
    g.drawString(s.gNome, 20, 18);
    if (s.lider && s.gCod[0] && s.gCod[0] != '-') {
      g.setFont(&fonts::FreeSans9pt7b);
      g.setTextColor(C_INK3);
      g.drawString("CODIGO", 20, 68);
      g.setFont(&fonts::FreeSansBold12pt7b);
      g.setTextColor(C_SUN);
      g.drawString(s.gCod, 92, 64);
    }

    // canto superior direito: os OUTROS carros, com distancia. Eu nao apareco -
    // sou o centro do mapa.
    for (int i = 1; i < nMembros && i <= 6; i++) {
      Ret r = faixaCarro(i);
      const Membro& m = membros[i];
      int d = distanciaAte(i);
      g.fillRoundRect(r.x, r.y, r.w, r.h, 10, m.alerta ? C_RED : C_SURF);
      g.fillRoundRect(r.x + 12, r.y + 16, 22, 22, 5, CORES_MAPA[m.cor % N_CORES]);
      g.setTextDatum(middle_left);
      g.setFont(&fonts::FreeSansBold12pt7b);
      g.setTextColor(m.alerta ? C_INK : C_TAN);
      g.drawString(m.nome, r.x + 44, r.y + r.h / 2);
      g.setTextDatum(middle_right);
      g.setFont(&fonts::FreeSansBold18pt7b);
      g.setTextColor(m.alerta ? C_INK : (d < 0 ? C_INK3 : C_INK));
      char t[16];
      if (d < 0)          strcpy(t, "-");
      else if (d < 1000)  snprintf(t, sizeof(t), "%dm", d);
      else                snprintf(t, sizeof(t), "%.1fkm", d / 1000.0f);
      g.drawString(t, r.x + r.w - 14, r.y + r.h / 2);
    }

    botao(g, rSair, "SAIR", "", C_RED, true);

    g.fillRoundRect(rModo.x, rModo.y, rModo.w, rModo.h, 14, C_SURF);
    g.drawRoundRect(rModo.x, rModo.y, rModo.w, rModo.h, 14, C_LINE);
    iconeTema(g, rModo.x + 38, rModo.y + 38, 20, C_INK2, g_tema == 0);

    g.fillRoundRect(rZoom.x, rZoom.y, rZoom.w, rZoom.h, 14, C_SURF);
    g.drawRoundRect(rZoom.x, rZoom.y, rZoom.w, rZoom.h, 14, C_LINE);
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold18pt7b);
    g.setTextColor(C_INK2);
    g.drawString("+/-", rZoom.x + 38, rZoom.y + 38);
    g.setFont(&fonts::Font0);

    if (temBuffer) cv.pushSprite(0, 0);
  };

  uint32_t ultimo = 0;
  while (true) {
    mundoAtualiza();
    if (millis() - ultimo > 180) { pinta(); ultimo = millis(); }

    int16_t x, y;
    if (!tft.getTouch(&x, &y)) { delay(8); continue; }
    int16_t ux = x, uy = y, tx, ty;
    while (tft.getTouch(&tx, &ty)) { ux = tx; uy = ty; delay(8); }

    if (dentro(rModo, ux, uy)) {
      // instantaneo: apaga, repinta no escuro, acende. Sem isso da para ver a
      // tela virando de cima para baixo.
      trocaTema(tft, g_tema ? 0 : 1, pinta);
      salvaTema();
      ultimo = millis();
      continue;
    }
    if (dentro(rZoom, ux, uy)) { zi = (zi + 1) % 5; continue; }

    bool tratou = false;
    for (int i = 1; i < nMembros && i <= 6; i++)
      if (dentro(faixaCarro(i), ux, uy)) {
        membros[i].alerta = !membros[i].alerta;
        g_carros[i].alerta = membros[i].alerta;
        if (membros[i].alerta) {
          // ALERTA: pisca a moldura da tela inteira. E para ser visto de canto de
          // olho com o carro andando - por isso a tela toda, nao um icone.
          // O apito de tres toques entra quando o codec ES8311 tiver driver.
          for (int k = 0; k < 3; k++) {
            tft.fillRect(0, 0, 1280, 14, C_RED); tft.fillRect(0, 706, 1280, 14, C_RED);
            tft.fillRect(0, 0, 14, 720, C_RED);  tft.fillRect(1266, 0, 14, 720, C_RED);
            delay(150);
            pinta(); delay(120);
          }
        }
        tratou = true; break;
      }
    if (tratou) continue;

    if (dentro(rSair, ux, uy)) {
      Ret sim = { (int16_t)(1280 / 2 - 320), 400, 300, 96 };
      Ret nao = { (int16_t)(1280 / 2 + 20), 400, 300, 96 };
      tft.fillScreen(C_BG);
      tft.setTextDatum(middle_center);
      tft.setFont(&fonts::FreeSansBold24pt7b);
      tft.setTextColor(C_INK);
      tft.drawString("Sair da trilha?", 640, 250);
      tft.setFont(&fonts::FreeSans12pt7b);
      tft.setTextColor(C_INK2);
      tft.drawString("Voce sai do grupo e volta ao menu inicial", 640, 310);
      botao(tft, nao, "FICAR", "", C_TAN, false);
      botao(tft, sim, "SAIR", "", C_RED, true);
      tft.setFont(&fonts::Font0);
      while (true) {
        int16_t a, b;
        if (!esperaToque(tft, a, b)) continue;
        if (dentro(sim, a, b)) { if (temBuffer) cv.deleteSprite(); return true; }
        if (dentro(nao, a, b)) { pinta(); break; }
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
