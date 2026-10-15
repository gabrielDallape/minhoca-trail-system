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
void salvaZoom();

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

// NAO EXISTE LISTA DE GRUPOS POR PERTO, e nao e omissao: o pacote de 16 bytes
// leva o codigo da sala como um HASH de 24 bits, nunca o nome. Um aparelho que
// escuta o ar consegue dizer "ha alguem transmitindo", mas nao consegue dizer
// "e o Grupo do Marcao" - o nome nunca vai ao ar.
//
// A versao anterior mostrava tres grupos inventados com nome, lider e barrinha
// de sinal, rotulados "lista simulada". Aquilo prometia uma funcao que o
// protocolo nao sustenta. Entrar num grupo e digitar o codigo que o lider ditou,
// que e como a coisa funciona de verdade numa trilha.

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
  Ret rSair  = { M, (int16_t)(TELA_H - M - 88), 240, 88 };
  Ret rAbrir = { (int16_t)(TELA_W - M - 380), (int16_t)(TELA_H - M - 88), 380, 88 };

  auto linhaMembro = [&](int i) -> Ret {
    return Ret{ (int16_t)(TELA_W / 2 + M), (int16_t)(190 + i * 56), (int16_t)(TELA_W / 2 - 2 * M), 50 };
  };
  auto pintaMembro = [&](int i) {
    Ret r = linhaMembro(i);
    // a lista para onde os botoes comecam: com 7-8 membros numa tela de 600 as
    // linhas de baixo cairiam por cima do ABRIR TRILHA (a contagem ja diz o total)
    if (r.y + r.h > TELA_H - M - 88 - 8) return;
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 14, C_SURF);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 14, C_LINE);
    // mesma linguagem das faixas da trilha: a cor do carro e uma barra inteira
    tft.fillRoundRect(r.x + 6, r.y + 6, 10, r.h - 12, 5, CORES_MAPA[membros[i].cor % N_CORES]);
    tft.setTextDatum(middle_left);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.setTextColor(C_INK);
    tft.drawString(membros[i].nome, r.x + 28, r.y + r.h / 2);
    if (membros[i].lider) {
      tft.setTextDatum(middle_right);
      tft.setFont(&fonts::FreeSans9pt7b);
      tft.setTextColor(C_SUN);
      tft.drawString("LIDER", r.x + r.w - 18, r.y + r.h / 2);
    }
    tft.setFont(&fonts::Font0);
  };
  auto pintaContagem = [&]() {
    tft.fillRect(TELA_W / 2 + M, 150, TELA_W / 2 - 2 * M, 30, C_BG);
    tft.setTextDatum(top_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    char c[40]; snprintf(c, sizeof(c), "NO GRUPO  (%d)", nMembros);
    tft.drawString(c, TELA_W / 2 + M, 152);
    tft.setFont(&fonts::Font0);
  };

  tft.fillScreen(C_BG);
  cabecalho(tft, "sala de espera", false);
  tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);

  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextColor(C_TAN);
  tft.drawString(gNome, M, 150);

  // o codigo fica GRANDE e permanente: o dono precisa ditar isso, e nao pode
  // depender de ter anotado em outro lugar
  // A caixa do codigo ocupa a METADE ESQUERDA. Com 560 fixos ela invadia a
  // lista de membros, que comeca em TELA_W/2 - em 1024 isso e sobreposicao.
  const int cxw = TELA_W / 2 - M - 20;
  const int cxh = (TELA_H < 700) ? 150 : 170;
  tft.fillRoundRect(M, 208, cxw, cxh, 14, C_SURF);
  tft.fillRoundRect(M, 208, 5, cxh, 2, C_SUN);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("CODIGO DO GRUPO", M + 30, 232);
  tft.setTextDatum(middle_left);
  tft.setFont(&fonts::FreeSansBold24pt7b);
  tft.setTextColor(C_SUN);
  int cx = M + 30;
  const int passo = (cxw - 60) / GRUPO_COD_DIG;
  for (int i = 0; i < GRUPO_COD_DIG; i++) { char s[2] = { gCod[i], 0 }; tft.drawString(s, cx, 208 + cxh / 2 + 18); cx += passo; }
  tft.setTextDatum(top_left);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK2);
  tft.drawString("dite este numero para quem for entrar", M + 30, 344);

  tft.setTextColor(C_INK3);
  tft.drawString("A trilha pode ser aberta a qualquer momento.", M, TELA_H - 168);
  tft.drawString("Quem chegar depois tambem entra com o mesmo codigo.", M, TELA_H - 140);

  botao(tft, rSair, "< CANCELAR", "", C_INK2, false);
  botao(tft, rAbrir, "ABRIR TRILHA", "", C_SUN, true);
  pintaContagem();
  for (int i = 0; i < nMembros; i++) pintaMembro(i);
  tft.setFont(&fonts::Font0);

  // QUEM ESTA NO GRUPO E QUEM ESTA NO AR. Nao ha lista inventada aqui: os
  // membros sao os carros de quem o radio ouviu pacote NESTA sala, e o codigo do
  // grupo e o filtro. Um carro que desligar some da lista quando parar de falar,
  // porque some do g_carros - a tela nao guarda quem ja viu.
  //
  // A versao anterior fazia cinco nomes chegarem sozinhos, com temporizador, para
  // a tela poder ser mostrada antes de existir radio. Isso ja cumpriu o papel.
  while (true) {
#if MTS_TEM_RADIO
    // remonta a lista a partir do mundo, e so repinta se ela MUDOU - repintar a
    // cada volta faria a tela tremer o tempo todo.
    int n = 1;                       // [0] sou eu, montado por quem chamou
    for (int i = 1; i < MUNDO_MAX_CARROS && n < MAX_MEMBROS; i++) {
      if (!g_carros[i].ativo) continue;
      strncpy(membros[n].nome, g_carros[i].nome, 15);
      membros[n].nome[15] = 0;
      membros[n].cor    = g_carros[i].cor;
      membros[n].lider  = g_carros[i].lider;
      membros[n].alerta = g_carros[i].alerta;
      membros[n].dist   = (g_carros[i].fix && g_meuFix)
                        ? (int16_t)haversine(g_meuLat, g_meuLon,
                                             g_carros[i].lat, g_carros[i].lon)
                        : -1;
      n++;
    }
    if (n != nMembros) {
      nMembros = n;
      // limpa a coluna da lista antes de repintar: encolher sem apagar deixaria
      // a linha do carro que saiu desenhada para sempre
      tft.fillRect(TELA_W / 2 + M, 186, TELA_W / 2 - 2 * M,
                   TELA_H - 186 - 140, C_BG);
      for (int i = 0; i < nMembros; i++) pintaMembro(i);
      pintaContagem();
    }
#endif
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

  Ret rNome  = { M, 176, (int16_t)(TELA_W - 2 * M), 130 };
  Ret rCod   = { M, 328, (int16_t)(TELA_W - 2 * M), 130 };
  Ret rVolta = { M, (int16_t)(TELA_H - M - 88), 240, 88 };
  Ret rAbrir = { (int16_t)(TELA_W - M - 380), (int16_t)(TELA_H - M - 88), 380, 88 };

  auto desenha = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "criar grupo - voce e o lider", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);

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
// A tela em que o aparelho VIVE. O mapa e a tela inteira; o resto flutua no canto.
//
// ---------------------------------------------------------------------------
// POR QUE ESTA TELA DESENHA DIFERENTE DE TODAS AS OUTRAS
//
// Todo redesenho de tela cheia LIMPA para o fundo antes de pintar, e esse instante
// em branco aparece: e a piscada. Um blit nao passa por ele - escreve o novo por
// cima do velho, sem estado intermediario.
//
// MEDIDO nesta placa (nao estimado):
//   fillScreen direto ...............  16 ms   mas pisca
//   quadro completo direto ..........  33 ms   pisca
//   pushSprite COM o painel girado .. 719 ms   inutilizavel
//   pushSprite SEM rotacao do painel .  73 ms  e nao pisca      <-- este
//
// Os 719 ms vinham da rotacao: girado, a copia transpoe pixel a pixel. Entao aqui
// o PAINEL fica na orientacao nativa (720x1280) e quem gira e o SPRITE. Medido:
// com setRotation(1) o sprite reporta 1280x720 e o push cai para 73 ms. Desenha-se
// em paisagem normalmente.
//
// O TOQUE precisa ser convertido a mao, porque o painel esta em 0 e a interface em
// 1. A formula saiu de LER Panel_Device::convertRawXY da LovyanGFX: para r=1 ela
// faz swap(x,y) e depois y = (altura-1) - y. Indo de bruto para paisagem:
//        X = y_bruto        Y = 719 - x_bruto
//
// SAIDA DE EMERGENCIA: o SAIR e testado nos DOIS sistemas de coordenada. Numa
// tentativa anterior um erro nesta conversao trancou o usuario dentro da tela,
// porque nem o botao de sair respondia. Nao pode acontecer de novo.
// ---------------------------------------------------------------------------
template <typename TFT>
bool telaTrilha(TFT& tft, const Sessao& s, Membro* membros, int nMembros)
{
  (void)membros; (void)nMembros;   // a lista viva vem do g_carros - ver abaixo
  Ret rSair   = { M, (int16_t)(TELA_H - M - 76), 140, 76 };
  // O ALERTA e MEU: aperta quem esta em apuros, e a flag viaja no pacote de
  // posicao para acender vermelho na tela dos OUTROS. A versao anterior nao
  // tinha este botao - tocava-se na faixa de um carro e isso marcava o alerta
  // DELE, so localmente: nada nunca ia ao ar, e o recurso inteiro era ficcao.
  Ret rAlerta = { (int16_t)(M + 156), (int16_t)(TELA_H - M - 76), 230, 76 };
  Ret rModo = { (int16_t)(TELA_W - 20 - 76), (int16_t)(TELA_H - 20 - 76), 76, 76 };
  Ret rZoom = { (int16_t)(TELA_W - 20 - 76), (int16_t)(TELA_H - 20 - 76 - 86), 76, 76 };
  const int cw = 290, ch = 54, cxr = TELA_W - 20 - cw;
  auto faixaCarro = [&](int i) -> Ret {
    return Ret{ (int16_t)cxr, (int16_t)(20 + (i - 1) * (ch + 6)), (int16_t)cw, (int16_t)ch };
  };
  // As faixas mostram os carros VIVOS, na ordem do g_carros; fila[] mapeia a
  // linha desenhada de volta ao indice do carro, para o toque acertar o certo.
  int fila[6]; int nFila = 0;
  auto montaFila = [&]() {
    nFila = 0;
    for (int i = 1; i < MUNDO_MAX_CARROS && nFila < 6; i++)
      if (g_carros[i].ativo) fila[nFila++] = i;
  };
  // O ZOOM MORA NA NVS (g_zoom, em ui.h), nao aqui. Antes era uma variavel local
  // que nascia em 0,9 m/px toda vez: sair do grupo, resetar ou perder energia
  // desfazia a escolha do usuario sem ele pedir. Agora so o toque no botao muda.

  // O TRUQUE DO SPRITE GIRADO E SO DA TELA DE 5".
  //
  // Ela tem painel 720x1280 RETRATO, e a interface e paisagem. Medido: pushSprite
  // com o painel girado custa 719 ms; com o painel na orientacao nativa e o
  // SPRITE girado, 73 ms - 10x. Por isso la o sprite nasce 720x1280 e recebe
  // setRotation(1).
  //
  // A de 7" e 1024x600 PAISAGEM NATIVA: nao ha rotacao em lugar nenhum, o sprite
  // e do tamanho da tela e o toque nao precisa de conversao. Medido: 47 ms.
  LGFX_Sprite cv(&tft);
  bool buf = false, girado = false;
  // Lambda e nao codigo corrido: o dialogo de SAIR destroi o sprite (restaura),
  // e quem escolhe FICAR precisa recria-lo. A versao anterior chamava telaTrilha
  // recursivamente no FICAR - cada "quase sai" empilhava um quadro de funcao
  // para sempre.
  auto cria = [&]() {
    cv.setPsram(true);
    cv.setColorDepth(16);
#if MTS_PLACA == 5
    buf = cv.createSprite(720, 1280);                    // NATIVO do painel
    if (buf) { cv.setRotation(1); tft.setRotation(0); }  // sprite paisagem
    girado = buf;
#else
    buf = cv.createSprite(TELA_W, TELA_H);
    girado = false;
#endif
  };
  cria();

  auto restaura = [&]() {
    if (buf) { cv.deleteSprite(); buf = false; }
#if MTS_PLACA == 5
    tft.setRotation(1);
#endif
  };

  // Medicao do quadro. Fica LIGADA: o custo do mapa depende de onde o carro
  // esta (cidade x mata) e de que nivel de detalhe o zoom escolheu, entao um
  // numero medido uma vez na bancada nao vale para o Brasil inteiro. Imprime a
  // cada 16 repintadas, o que e barato e aparece no monitor serial quando algo
  // ficar lento em campo.
  static uint32_t medN = 0, medMapa = 0, medPush = 0, medPior = 0;

  auto pinta = [&]() {
    uint32_t tA = micros();
    auto& g = buf ? (LovyanGFX&)cv : (LovyanGFX&)tft;
    mapaDesenha(g, TELA_W, TELA_H, MAPA_ZOOMS[g_zoom]);

    // O GRUPO numa pilula de HUD: chrome proprio para ser legivel sobre
    // QUALQUER pedaco de mapa - o nome pelado de antes sumia na primeira area
    // clara (cidade no tema dia).
    {
      char gn[21]; strncpy(gn, s.gNome, 20); gn[20] = 0;
      g.setFont(&fonts::FreeSansBold18pt7b);
      const int wg = g.textWidth(gn);
      const int ch = 60;
      hudPilula(g, 16, 16, 60 + wg + 26, ch);
      icComboio(g, 16 + 34, 16 + ch / 2, 26, C_SUN);
      g.setTextDatum(middle_left);
      g.setTextColor(C_INK);
      g.drawString(gn, 16 + 60, 16 + ch / 2);
      if (s.lider && s.gCod[0] && s.gCod[0] != '-') {
        // o codigo fica a vista do lider: e o que ele dita para quem chega
        g.setFont(&fonts::FreeSansBold12pt7b);
        const int wc = g.textWidth(s.gCod);
        hudPilula(g, 16, 16 + ch + 10, 100 + wc + 22, 44);
        g.setFont(&fonts::FreeSans9pt7b);
        g.setTextColor(C_INK3);
        g.drawString("CODIGO", 16 + 18, 16 + ch + 10 + 22);
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextColor(C_SUN);
        g.drawString(s.gCod, 16 + 100, 16 + ch + 10 + 22);
      }
    }

    // AS FAIXAS VEM DO MUNDO VIVO, nao de um retrato tirado ao entrar na tela.
    // A versao anterior desenhava membros[], montado UMA vez por quem chamou: o
    // seguidor entrava com a lista vazia e nunca via a faixa do lider; um nome
    // que chegasse pelo roster nao atualizava; e o alerta recebido pelo radio
    // (g_carros[i].alerta) nunca pintava, porque a faixa olhava a copia velha.
    montaFila();
    // A DISTANCIA E PELO CAMINHO quando a rota do lider existe. Em linha reta,
    // numa curva de retorno, um carro 200 m atras NO CAMINHO aparece a 30 m -
    // e o motorista para para esperar alguem que esta a duas curvas dali. A
    // projecao na rota (fila.h) da a quilometragem de cada um; a linha reta fica
    // como reserva para quando ainda nao ha rota (ex.: na tela do proprio lider).
    const float kmEu = filaMinhaKm();
    for (int r0 = 0; r0 < nFila; r0++) {
      const Carro& m = g_carros[fila[r0]];
      Ret r = faixaCarro(r0 + 1);
      int d = distanciaAte(fila[r0]);
      float mFora = 0;
      const bool fora = filaForaDaTrilha(fila[r0], mFora);
      if (!fora && kmEu >= 0) {
        float kmEle = filaKm(fila[r0]);
        if (kmEle >= 0) d = (int)fabsf(kmEle - kmEu);
      }
      g.fillRoundRect(r.x, r.y + 3, r.w, r.h, 16, C_CASING);   // sombra deslocada
      g.fillRoundRect(r.x, r.y, r.w, r.h, 16, m.alerta ? C_RED : C_SURF);
      g.drawRoundRect(r.x, r.y, r.w, r.h, 16, m.alerta ? C_CASING : C_LINE);
      // a COR DO CARRO vira barra de altura inteira: e por ela que se acha o
      // carro no mapa, entao ela abre a faixa - o quadradinho de 22 px sumia
      g.fillRoundRect(r.x + 6, r.y + 6, 10, r.h - 12, 5, CORES_MAPA[m.cor % N_CORES]);
      g.setTextDatum(middle_left);
      g.setFont(&fonts::FreeSansBold12pt7b);
      g.setTextColor(C_INK);
      // nome cortado em 12: mais que isso invade o numero da distancia
      char nm[13]; strncpy(nm, m.nome, 12); nm[12] = 0;
      g.drawString(nm, r.x + 28, r.y + r.h / 2);
      g.setTextDatum(middle_right);
      char t[16];
      if (fora) {
        // saiu da trilha: e o aviso mais util do aparelho, nao um numero a menos
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextColor(m.alerta ? C_INK : C_WARN);
        strcpy(t, "FORA");
      } else {
        g.setFont(&fonts::FreeSansBold18pt7b);
        g.setTextColor(m.alerta ? C_INK : (d < 0 ? C_INK3 : C_INK));
        if (d < 0)         strcpy(t, "-");
        else if (d < 1000) snprintf(t, sizeof(t), "%dm", d);
        else               snprintf(t, sizeof(t), "%.1fkm", d / 1000.0f);
      }
      g.drawString(t, r.x + r.w - 14, r.y + r.h / 2);
    }

    // SAIR discreto, numa pilula neutra. O unico vermelho PERMANENTE da tela e
    // o ALERTA - o SAIR vermelho-cheio de antes gritava o dia inteiro e diluia
    // a unica cor de emergencia do aparelho.
    hudPilula(g, rSair.x, rSair.y, rSair.w, rSair.h);
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(C_INK2);
    g.drawString("SAIR", rSair.x + rSair.w / 2, rSair.y + rSair.h / 2);

    // ALERTA: vazado em repouso, CHEIO com "NO AR" enquanto o pedido viaja -
    // o motorista sabe de relance se ainda esta pedindo socorro.
    {
      const bool on = g_carros[0].alerta;
      g.fillRoundRect(rAlerta.x, rAlerta.y + 3, rAlerta.w, rAlerta.h, 38, C_CASING);
      g.fillRoundRect(rAlerta.x, rAlerta.y, rAlerta.w, rAlerta.h, 38, on ? C_RED : C_SURF);
      for (int k = 0; k < 2; k++)
        g.drawRoundRect(rAlerta.x + k, rAlerta.y + k, rAlerta.w - 2 * k,
                        rAlerta.h - 2 * k, 38 - k, C_RED);
      icAlerta(g, rAlerta.x + 46, rAlerta.y + rAlerta.h / 2 - 2, 34, on ? C_INK : C_RED);
      g.setTextDatum(middle_left);
      g.setFont(&fonts::FreeSansBold18pt7b);
      g.setTextColor(on ? C_INK : C_RED);
      g.drawString(on ? "NO AR" : "ALERTA", rAlerta.x + 80, rAlerta.y + rAlerta.h / 2);
    }

    // Controles do mapa como botoes FLUTUANTES redondos (instrumento, nao
    // formulario): dia/noite embaixo, zoom acima, e a largura coberta numa
    // pilula ao lado do zoom - "2.6km na tela" informa; "m/px" nao.
    fabFundo(g, rModo.x + 38, rModo.y + 38, 38, C_SURF);
    iconeTema(g, rModo.x + 38, rModo.y + 38, 20, C_INK2, g_tema == 0, C_SURF);

    fabFundo(g, rZoom.x + 38, rZoom.y + 38, 38, C_SURF);
    for (int k = 0; k < MAPA_NZOOM; k++) {
      // barras que crescem: quanto mais alto o nivel, mais chao na tela
      int bx = rZoom.x + 38 - 26 + k * 11;
      bool aceso = (k <= g_zoom);
      int hh = 8 + k * 5;
      g.fillRect(bx, rZoom.y + 38 + 14 - hh, 7, hh, aceso ? C_SUN : C_LINE);
    }
    {
      char z[12];
      double mLarg = MAPA_ZOOMS[g_zoom] * (double)TELA_W;   // largura coberta
      if (mLarg < 1000) snprintf(z, sizeof(z), "%dm", (int)mLarg);
      else              snprintf(z, sizeof(z), "%.1fkm", mLarg / 1000.0);
      g.setFont(&fonts::FreeSansBold12pt7b);
      const int wz = g.textWidth(z);
      hudPilula(g, rZoom.x - wz - 40, rZoom.y + 18, wz + 28, 40);
      g.setTextDatum(middle_center);
      g.setTextColor(C_INK2);
      g.drawString(z, rZoom.x - wz - 40 + (wz + 28) / 2, rZoom.y + 38);
    }
    g.setFont(&fonts::Font0);

    uint32_t tB = micros();
    if (buf) cv.pushSprite(0, 0);
    uint32_t tC = micros();

    medMapa += (tB - tA); medPush += (tC - tB);
    uint32_t tot = tC - tA;
    if (tot > medPior) medPior = tot;
    if (++medN >= 16) {
      Serial.printf("quadro: relevo %lu + vias %lu + trajeto %lu + carros %lu | "
                    "desenho %lu + push %lu = %lu ms (pior %lu) | "
                    "nivel %d zoom %.1f m/px | cache %lu acertos %lu faltas%s\n",
                    (unsigned long)(g_usRelevo / 1000), (unsigned long)(g_usVias / 1000),
                    (unsigned long)(g_usTraj / 1000), (unsigned long)(g_usCarros / 1000),
                    (unsigned long)(medMapa / medN / 1000),
                    (unsigned long)(medPush / medN / 1000),
                    (unsigned long)((medMapa + medPush) / medN / 1000),
                    (unsigned long)(medPior / 1000),
                    g_fundo.ok ? vecPackLevelForScale(g_fundo.vec, MAPA_ZOOMS[g_zoom],
                                                      TELA_W, TELA_H, g_meuLat) : -1,
                    MAPA_ZOOMS[g_zoom],
                    (unsigned long)g_fundo.acertos, (unsigned long)g_fundo.faltas,
                    g_fundo.grandesDemais ? "  ATENCAO: setor grande demais" : "");
      medN = medMapa = medPush = medPior = 0;
    }
  };

  // repinta so quando algo mudou de fato
  double  uLat = 1e9, uLon = 1e9;
  int     uZi = -1, uTema = -1, uN = -1;
  uint8_t uAl = 0;
  uint32_t uOutros = 0;
  auto mudou = [&]() -> bool {
    uint8_t al = 0;
    for (int i = 0; i < MUNDO_MAX_CARROS && i < 8; i++) if (g_carros[i].alerta) al |= (1 << i);
    int n = mundoQuantos();
    // OS OUTROS TAMBEM SE MEXEM. A versao anterior so olhava a MINHA posicao:
    // um seguidor parado (esperando o grupo, atolado) via o lider congelado no
    // mapa ate ele proprio andar 2 px. O resumo abaixo muda quando qualquer
    // carro anda ~10 m, quando chega nome de roster e quando a rota cresce.
    uint32_t outros = (uint32_t)g_rotaN;
    for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
      if (!g_carros[i].ativo) continue;
      outros = outros * 31 + (uint32_t)(int32_t)(g_carros[i].lat * 1e4)
                           + (uint32_t)(int32_t)(g_carros[i].lon * 1e4)
                           + (g_carros[i].temNome ? 7u : 0u)
                           + (g_carros[i].fix ? 3u : 0u);
    }
    if (g_zoom != uZi || g_tema != uTema || n != uN || al != uAl || outros != uOutros) {
      uZi = g_zoom; uTema = g_tema; uN = n; uAl = al; uOutros = outros;
      uLat = g_meuLat; uLon = g_meuLon; return true;
    }
    if (uLat > 1e8) { uLat = g_meuLat; uLon = g_meuLon; return true; }
    // 2 pixels de deslocamento. Era 20, o que no zoom padrao significava andar
    // 40 m para o mapa se mexer - a 8 m/s, uma repintada a cada 5 segundos; e no
    // zoom de 15 km, uma a cada 30 s. Aquele numero foi posto quando o pushSprite
    // custava 719 ms e cada quadro doia; depois que ele caiu para 73 ms o freio
    // ficou, e virou a sensacao de "travado".
    if (haversine(uLat, uLon, g_meuLat, g_meuLon) >= MAPA_ZOOMS[g_zoom] * 2.0) {
      uLat = g_meuLat; uLon = g_meuLon; return true;
    }
    return false;
  };

  while (true) {
    mundoAtualiza();
    if (mudou()) pinta();

    int16_t bx, by;
    if (!tft.getTouch(&bx, &by)) { delay(8); continue; }
    int16_t lx = bx, ly = by, tx, ty;
    // enquanto o dedo esta na tela o radio continua sendo servido: um toque
    // longo nao pode custar a janela de TDMA nem afogar a UART do GPS
    while (tft.getTouch(&tx, &ty)) { lx = tx; ly = ty; if (g_hookServico) g_hookServico(); delay(8); }

    // (ux,uy) = coordenada da interface; (lx,ly) = bruta do painel
    // So a tela de 5" precisa converter: la o painel esta em 0 e a interface em 1.
    // A formula saiu de LER Panel_Device::convertRawXY da LovyanGFX.
    int16_t ux = girado ? ly : lx;
    int16_t uy = girado ? (int16_t)(TELA_H - 1 - lx) : ly;

    // SAIR aceita as DUAS convencoes: nunca ficar preso aqui dentro
    if (dentro(rSair, ux, uy) || (girado && dentro(rSair, lx, ly))) {
      restaura();
      const int16_t cbw = (int16_t)((TELA_W - 3 * M) / 2 > 300 ? 300 : (TELA_W - 3 * M) / 2);
      Ret sim = { (int16_t)(TELA_W / 2 - 20 - cbw), (int16_t)(TELA_H / 2 + 40), cbw, 96 };
      Ret nao = { (int16_t)(TELA_W / 2 + 20), (int16_t)(TELA_H / 2 + 40), cbw, 96 };
      tft.fillScreen(C_BG);
      tft.setTextDatum(middle_center);
      tft.setFont(&fonts::FreeSansBold24pt7b);
      tft.setTextColor(C_INK);
      tft.drawString("Sair da trilha?", TELA_W / 2, TELA_H / 2 - 110);
      tft.setFont(&fonts::FreeSans12pt7b);
      tft.setTextColor(C_INK2);
      tft.drawString("Voce sai do grupo e volta ao menu inicial", TELA_W / 2, TELA_H / 2 - 50);
      botao(tft, nao, "FICAR", "", C_TAN, false);
      botao(tft, sim, "SAIR", "", C_RED, true);
      tft.setFont(&fonts::Font0);
      bool sai = false;
      while (true) {
        int16_t a, b;
        if (!esperaToque(tft, a, b)) continue;
        if (dentro(sim, a, b)) { sai = true; break; }
        if (dentro(nao, a, b)) break;
      }
      if (sai) return true;
      // FICAR: recria o sprite que o restaura() destruiu e forca a repintura.
      // (Chamar telaTrilha de novo, como antes, empilhava um quadro de funcao a
      // cada "quase sai" - recursao sem fundo.)
      cria();
      uZi = -1;
      continue;
    }

    if (dentro(rModo, ux, uy)) { aplicaTema(g_tema ? 0 : 1); salvaTema(); continue; }
    if (dentro(rZoom, ux, uy)) {
      g_zoom = (uint8_t)((g_zoom + 1) % MAPA_NZOOM);
      salvaZoom();                 // so aqui: e a unica coisa que muda o zoom
      continue;
    }

    // ALERTA: liga/desliga o MEU pedido de socorro. A flag vai no proximo pacote
    // de posicao (TDMA_FL_ALERT) e acende vermelho na tela dos outros; aqui, a
    // moldura pisca para confirmar que o pedido esta no ar.
    if (dentro(rAlerta, ux, uy)) {
      g_carros[0].alerta = !g_carros[0].alerta;
      if (g_carros[0].alerta) {
        for (int k = 0; k < 3; k++) {
          // moldura piscando, DIRETO no painel: usa width/height atuais, que
          // acompanham a rotacao em que o painel esta agora
          int W = tft.width(), H = tft.height();
          tft.fillRect(0, 0, W, 14, C_RED);   tft.fillRect(0, H - 14, W, 14, C_RED);
          tft.fillRect(0, 0, 14, H, C_RED);   tft.fillRect(W - 14, 0, 14, H, C_RED);
          esperaServindo(150);
          pinta();
          esperaServindo(120);
        }
      } else {
        pinta();
      }
      continue;
    }
  }
}

// --------------------------------------------------------------- ENTRAR
// Devolve o indice do grupo escolhido e ja validado pelo codigo, ou -1.
template <typename TFT>
bool telaEntrarGrupo(TFT& tft, char* cod)
{
  // Direto no teclado: nao ha o que escolher antes. Uma tela intermediaria com
  // um botao so seria um toque a mais para nao informar nada.
  cod[0] = 0;
  return tecladoNumero(tft, "entrar num grupo",
                       "digite o codigo que o lider passou", cod, GRUPO_COD_DIG);
}
