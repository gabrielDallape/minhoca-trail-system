// Peças visuais do MTS na tela P4 (1280x720).
//
// ARQUITETURA: TUDO e desenhado num sprite fora da tela (na PSRAM) e so no fim
// empurrado de uma vez. Nao e refinamento - e o conserto de um defeito visivel:
// o painel e framebuffer, o DPI varre o mesmo endereco que a gente escreve, entao
// qualquer fillScreen aparece como PISCADA PRETA. Com o buffer de tras a tela so
// ve quadro pronto. O grupo_ws das telas S3 ja usava esse padrao pelo mesmo motivo.
//
// Custo: 1280*720*2 = 1,8 MB de PSRAM (sobram 31,7) e ~25 ms por apresentacao,
// numa tela que faz 67 quadros por segundo. Barato.
//
// A paleta sai da PROPRIA arte do logo, amostrada com tools/gera_logo.py: o fundo
// quase preto, o laranja do sol e o bege das letras. Assim a abertura e as telas
// nao parecem dois aplicativos colados.
#pragma once
#include <stdint.h>
#include "icones.h"

// ------------------------------------------------------------------ paleta
// DOIS TEMAS, e a escolha nao e estetica - e norma.
//
// IHO S-52 sec. 2.2.1 (cartas nauticas) exige "imagem negativa da carta a noite,
// para nao prejudicar a visao noturna". O Organic Maps mantem os MESMOS 210 nomes
// de cor em dois arquivos e a regra que sai deles e: a MATIZ nunca muda; o que
// inverte e a luminancia em relacao ao fundo.
//   DIA   = fundo claro, feicoes escuras  (sob sol so a diferenca de luminancia
//           sobrevive; matiz nao contribui nada)
//   NOITE = fundo escuro, feicoes claras  (a vista leva 30-40 min para se adaptar
//           ao escuro e uma tela clara destroi isso)
//
// Contra a intuicao: a ISO 15008 sec. 4.3.2.1 exige MAIS contraste a noite (5:1)
// do que sob sol direto (2:1).
//
// ARMADILHA DO RGB565: cinza quase-preto nao cai na grade e ganha dominante VERDE
// (#050505 vira #000400). Por isso o preto daqui e #080808, nao #050505.
//
// NAO existe modo noturno vermelho aqui de proposito: a evidencia (NSMRL Rep.1036)
// diz que luz branca fraca e melhor, e o vermelho custa justamente a codificacao
// POR COR - que e como distinguimos os carros. Vermelho fica so no alerta.

static uint16_t C_BG, C_SURF, C_SURF2, C_LINE, C_INK, C_INK2, C_INK3;
static uint16_t C_SUN, C_TAN, C_RED, C_OK, C_WARN;
// Trajeto. A ESCOLHA DA MATIZ E DO USUARIO e esta travada no projeto desde o
// trilha_core.h: ROXO = falta andar, AZUL CLARO = ja andei. O que muda entre dia
// e noite e so a luminancia, como o Organic Maps faz. Cada uma tem seu contorno.
static uint16_t C_ROTA, C_ROTA_C, C_RASTRO, C_RASTRO_C, C_VAO;
static uint8_t  g_tema = 1;          // 0 = dia, 1 = noite

// ------------------------------------------------------------- tamanho da tela
// Preenchidos no boot a partir do painel. NAO cravar 1280/720: sao duas placas
// com resolucoes diferentes (720x1280 e 1024x600) e o codigo tem de servir as
// duas. Regra pratica ao posicionar: ancore no RODAPE (TELA_H - x) em vez de usar
// coordenada absoluta - foi o que quebrou na primeira tentativa, com botoes em
// y=596 e 88 de altura terminando em 684, fora de uma tela de 600.
static int TELA_W = 1280, TELA_H = 720;
inline void telaTamanho(int w, int h) { TELA_W = w; TELA_H = h; }

// ---------------------------------------------------------------- ZOOM
// Cinco niveis fixos, em metros por pixel. Nao ha zoom continuo de proposito:
// numa trilha se troca de zoom com a mao balancando e sem olhar, e degrau
// nomeado e o que da para acertar assim. Cada nivel e ~2,5x o anterior, que e o
// passo em que a mudanca se percebe sem perder a referencia do que estava na
// tela.
//
// A LARGURA COBERTA e o que interessa na hora de escolher (tela de 1280 px):
//   0 -> 0,4 m/px .... 512 m ..... manobra, ve a trilha bifurcar
//   1 -> 0,9 m/px ... 1,15 km .... andando devagar
//   2 -> 2,0 m/px ... 2,56 km .... padrao: da para ver o carro da frente e a curva
//   3 -> 5,0 m/px ... 6,40 km .... o grupo espalhado
//   4 -> 12 m/px ... 15,36 km .... onde estamos na regiao
static const double MAPA_ZOOMS[] = { 0.4, 0.9, 2.0, 5.0, 12.0 };
#define MAPA_NZOOM 5

// GUARDADO NA NVS. O aparelho desliga com a chave do carro, e reabrir sempre no
// mesmo zoom que o usuario escolheu e o que faz a tela parecer a MESMA tela -
// resetar, sair do grupo ou ficar sem energia nao muda. So o toque no botao muda.
static uint8_t g_zoom = 2;

// Contorno dos marcadores. FIXO nos dois temas: e ele que carrega o contraste de
// dia (14:1 contra o fundo claro); a noite o preenchimento contrasta o fundo
// direto. Mesma tecnica do nav_arrow + nav_arrow_stroke do OsmAnd.
#define C_CASING  0x1082    // #101010

// Cores dos carros: paleta Tol "bright", a unica projetada com o criterio
// "distintas de preto e branco". Com o contorno acima, 7 de 7 passam 3:1 nos DOIS
// fundos - o que nenhuma paleta consegue sozinha (o teto matematico e 3,94:1).
// Acima de ~8 carros nao se acrescenta cor: usa-se numero do slot.
static const uint16_t CORES_MAPA[] = {
  0x43B5,  // #4477AA azul
  0xEB2E,  // #EE6677 vermelho
  0x2446,  // #228833 verde
  0xCDC8,  // #CCBB44 amarelo
  0x667D,  // #66CCEE ciano
  0xA98E,  // #AA3377 roxo
  0xEBA6,  // #EE7733 laranja
  0xBDD7,  // #BBBBBB cinza
};
static const char* CORES_NOME[] = { "azul","vermelho","verde","amarelo","ciano","roxo","laranja","cinza" };
#define N_CORES 8

inline void aplicaTema(uint8_t t)
{
  g_tema = t ? 1 : 0;
  if (g_tema == 0) {           // ---- DIA: fundo claro, feicoes escuras
    C_BG    = 0xE71A;  // #E3E1D2  bege do estilo "vehicle/light" do Organic Maps
    C_SURF  = 0xD618;  // #D3D1C2
    C_SURF2 = 0xC618;  // #C8C6B8
    C_LINE  = 0xA534;  // #A8A69A
    C_INK   = 0x10A2;  // #141414  14:1 sobre o fundo
    C_INK2  = 0x5ACA;  // #5A5A52
    C_INK3  = 0x6B6C;  // #6E6C64  4,0:1 (era #8A8A82, 2,7:1 - nao dava no sol)
    C_SUN   = 0xB1C1;  // #B03A0F  laranja escurecido para contrastar no claro
    C_TAN   = 0x6A45;  // #6B4A2F  o bege da arte nao serve no claro; vira marrom
    C_RED   = 0xC8E3;  // #C81E1E
    C_OK    = 0x2446;
    C_WARN  = 0x9260;
    // CONTORNO ESCURO NO TEMA CLARO. Era BRANCO, e branco sobre o bege #E6E3D6 da
    // 1,29:1 - invisivel. E o contorno e o traco MAIS GROSSO (11 px contra 5 do
    // nucleo): com ele apagado, dois tercos do tracado sumiam e o caminho ficava
    // um risco fino. Agora o contorno usa o MESMO preto dos marcadores e das vias
    // (C_CASING, 14,8:1), o que da ao mapa inteiro um traco so.
    //
    // Com o contorno escuro, o nucleo nao precisa mais brigar com o fundo claro -
    // ele fica DENTRO do contorno. Por isso as duas matizes clarearam: agora o
    // criterio e contrastar com o proprio contorno, e ai da para usar a cor cheia
    // em vez da versao escurecida.
    // Matizes CHEIAS, nao versoes acinzentadas. Agora que o fundo vem do cartao -
    // estrada em laranja, trilha em bege, mata, agua - o traçado disputa atencao
    // com o mapa inteiro, e tem de ganhar sempre: e a unica coisa na tela que
    // diz para onde ir. Saturacao alta e o que separa "informacao" de "fundo".
    C_ROTA     = 0xA15E;  // #A32BF5 roxo   3,8:1 contra o contorno e 3,8:1 no fundo
    C_ROTA_C   = C_CASING;
    C_RASTRO   = 0x0CDD;  // #0A9BE8 azul   6,2:1 contra o contorno
    C_RASTRO_C = C_CASING;
    C_VAO      = 0x5ACA;  // #5A5952  5,5:1 (era 2,7:1: a ponte do sinal caido
                          // e justamente o que nao pode passar despercebido)
  } else {                     // ---- NOITE: fundo escuro, feicoes claras
    C_BG    = 0x0841;  // #080808  (nao #050505: viraria esverdeado no RGB565)
    C_SURF  = 0x18E3;  // #1A1714
    C_SURF2 = 0x2103;  // #241F1A
    C_LINE  = 0x3987;  // #3A332C
    C_INK   = 0xCE59;  // #C8C8C8
    C_INK2  = 0x8C51;  // #8A8A8A
    C_INK3  = 0x5AEB;  // #5A5A5A
    C_SUN   = 0xD243;  // #D2481E  o laranja do sol da arte
    C_TAN   = 0xDE35;  // #D8C7A8  o bege das letras da arte
    C_RED   = 0xFA27;  // #FF453A
    C_OK    = 0x4CCB;
    C_WARN  = 0xC544;
    C_ROTA     = 0xC3FF;  // #C77DFF roxo    7,5:1 no escuro
    C_ROTA_C   = 0x1845;  // #1A0A2E
    C_RASTRO   = 0x06BF;  // #00D4FF ciano  11,3:1 - o mais luminoso da tela
    C_RASTRO_C = 0x0926;  // #0A2430
    C_VAO      = 0x5AEB;
  }
}

// TROCA DE TEMA: so repinta, direto na tela.
//
// MEDIDO nesta placa, e o numero muda tudo:
//   fillScreen direto ....... 16 ms
//   desenhar dentro do sprite 14 ms
//   pushSprite de 1280x720 .. 719 ms   <-- 45x mais lento
//
// O pushSprite copia PSRAM para PSRAM pixel a pixel, sem caminho rapido. Era ELE
// a "transicao de PowerPoint": todo quadro do mapa custava 0,7 s.
//
// Logo: NUNCA use sprite de tela cheia neste painel. Desenhar direto e o caminho
// mais rapido que existe aqui, e 16 ms nao da tempo de ser visto. Tambem nao
// apague o backlight para esconder o redesenho - isso e que virava piscada preta.
template <typename TFT, typename FN>
void trocaTema(TFT& tft, uint8_t novo, FN repinta)
{
  aplicaTema(novo);
  repinta();
}

// Margem unica. Antes havia 40, 60 e 8 misturados - era isso que fazia a tela
// parecer torta mesmo onde nao havia sobreposicao.
// A MARGEM veio de 48 para 20, e nao e capricho: 48 de cada lado comem 96 px de
// uma tela de 1024, quase 10% da largura, para nao mostrar nada. O desenho novo usa
// 20 - o respiro minimo da borda do painel - e o espaco que sobra vai para o
// conteudo, que e o que o motorista tenta ler com o carro pulando.
#define M 20

// Alvo de toque minimo. E o dedo COM LUVA, nao a unha: qualquer coisa que se
// aperte tem de ter pelo menos isto de altura. Linha de lista que so se le pode
// ser menor, e por isso ha duas medidas e nao uma.
#define ALVO   76
#define LINHA_LEITURA 66
#define BARRA_H 88
#define RAIO   10


struct Ret { int16_t x, y, w, h; };
inline bool dentro(const Ret& r, int16_t px, int16_t py) {
  return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// Havia um "sol" (circulos concentricos laranja) atras do botao principal, como
// eco da arte do logo. Saiu a pedido: numa tela de acao ele disputava com o texto
// do botao em vez de guiar o olho. Fundo liso.

// Sol ou lua, desenhado em vetor. Mostra o modo para o qual VAI ao ser tocado -
// e a convencao do botao de dia/noite do OsmAnd.
// corFundo E OBRIGATORIA NA PRATICA: a lua e um circulo com uma MORDIDA, e a
// mordida e pintada com a cor do que esta atras. A versao anterior usava sempre
// C_BG, mas o icone e desenhado sobre C_SURF nas linhas de configuracao - entao a
// mordida saia como um borrao da cor errada, e era isso que deixava a lua feia.
// A mordida tambem era grande demais (0,85 do raio, deslocada so 0,5): sobrava um
// filete, nao uma lua.
template <typename G>
void iconeTema(G& g, int cx, int cy, int r, uint16_t cor, bool desenhaLua,
               uint16_t corFundo)
{
  if (desenhaLua) {
    g.fillCircle(cx, cy, r, cor);
    g.fillCircle(cx + (int)(r * 0.62f), cy - (int)(r * 0.30f),
                 (int)(r * 0.80f), corFundo);
  } else {
    g.fillCircle(cx, cy, r * 0.58f, cor);
    for (int i = 0; i < 8; i++) {
      float a = i * 0.7853982f;
      int x0 = cx + (int)(cosf(a) * r * 0.78f), y0 = cy + (int)(sinf(a) * r * 0.78f);
      int x1 = cx + (int)(cosf(a) * r * 1.15f), y1 = cy + (int)(sinf(a) * r * 1.15f);
      g.drawLine(x0, y0, x1, y1, cor);
      g.drawLine(x0 + 1, y0, x1 + 1, y1, cor);
    }
  }
}

// -------------------------------------------------------------- cabecalho
// ALTURA DO CABECALHO. Antes ele gastava 130 px com "MTS" em 24pt mais um
// subtitulo - 22% de uma tela de 600, so de enfeite, e o que sobrava obrigava as
// linhas de configuracao a caberem em 58 px cada (nao da para clicar). A marca ja
// aparece na abertura; repetir na tela toda nao informa nada.
#define CAB_H 88

// Cabecalho com a MARCA: "MTS" sublinhado pelo traco de sol (a assinatura
// visual do aparelho - a mesma barra laranja que marca o que e tocavel nas
// linhas de configuracao) e o nome da tela ao lado, em voz baixa. Nao e o
// titulo de 130 px que foi removido: cabe nos MESMOS 88 px que ja existiam,
// e da identidade ao que antes era uma faixa vazia.
// Passe sub = nullptr ou "" para so a marca (tela inicial).
template <typename G>
void cabecalho(G& g, const char* sub, bool comEngrenagem, Ret* eng = nullptr)
{
  g.setTextDatum(middle_left);
  g.setFont(&fonts::FreeSansBold18pt7b);
  g.setTextColor(C_SUN);
  g.drawString("MTS", M, CAB_H / 2 - 6);
  const int wm = g.textWidth("MTS");
  g.fillRoundRect(M, CAB_H / 2 + 16, wm, 5, 2, C_SUN);   // o traco de sol

  if (sub && sub[0]) {
    g.setFont(&fonts::FreeSans12pt7b);
    g.setTextColor(C_INK2);
    g.drawString(sub, M + wm + 20, CAB_H / 2 - 2);
  }

  if (comEngrenagem) {
    // engrenagem num botao REDONDO: os controles flutuantes do aparelho sao
    // circulos (mapa idem); retangulo e conteudo, circulo e acao
    const int cx = g.width() - M - 38, cy = CAB_H / 2;
    g.fillCircle(cx, cy + 3, 38, C_CASING);              // sombra deslocada
    g.fillCircle(cx, cy, 38, C_SURF);
    g.drawCircle(cx, cy, 38, C_LINE);
    for (int i = 0; i < 8; i++) {
      float a = i * 0.7853982f;
      g.fillCircle(cx + (int)(cosf(a) * 17), cy + (int)(sinf(a) * 17), 5, C_INK2);
    }
    g.fillCircle(cx, cy, 13, C_INK2);
    g.fillCircle(cx, cy, 6, C_SURF);
    if (eng) *eng = { (int16_t)(cx - 40), (int16_t)(cy - 40), 80, 80 };
  }
  g.setFont(&fonts::Font0);
}

// Botao flutuante redondo (mapa: zoom, dia/noite). A sombra DESLOCADA e o que
// o separa do mapa - a sombra colada de antes lia como borda suja.
template <typename G>
void fabFundo(G& g, int cx, int cy, int r, uint16_t fundo)
{
  g.fillCircle(cx, cy + 3, r, C_CASING);
  g.fillCircle(cx, cy, r, fundo);
  g.drawCircle(cx, cy, r, C_LINE);
}

// Pilula de HUD: chrome para texto que flutua sobre o MAPA. Texto pelado sobre
// mapa e ilegivel na primeira area clara; a pilula da fundo, contorno e sombra.
template <typename G>
void hudPilula(G& g, int x, int y, int w, int h)
{
  g.fillRoundRect(x, y + 3, w, h, h / 2, C_CASING);
  g.fillRoundRect(x, y, w, h, h / 2, C_SURF);
  g.drawRoundRect(x, y, w, h, h / 2, C_LINE);
}

// PASTILHA DE ESTADO: icone + valor, SEM rotulo escrito.
//
// A versao anterior escrevia "GPS  9 satelites" - duas palavras para dizer o que um
// desenho de satelite e o algarismo 9 dizem sozinhos, e melhor, porque o motorista
// reconhece a forma antes de conseguir ler. Texto na tela do aparelho ficou
// reservado a NOME, NUMERO e acao que nao pode ser ambigua.
//
// quem = 0 satelite (GPS), 1 antena (radio), 2 mapa/cartao.
template <typename G>
int pastilhaIc(G& g, int x, int y, uint8_t quem, const char* val, uint16_t cor,
               bool riscado = false)
{
  const int h = 52, ics = 26;
  g.setFont(&fonts::FreeSansBold12pt7b);
  const int wv = val && val[0] ? g.textWidth(val) : 0;
  const int w = 17 + ics + (wv ? 9 + wv : 0) + 17;
  // PILULA de verdade (raio = metade da altura) com contorno: a versao de
  // cantos 10 px parecia um botao quadrado orfao; a pilula le como ESTADO.
  g.fillRoundRect(x, y, w, h, h / 2, C_SURF);
  g.drawRoundRect(x, y, w, h, h / 2, C_LINE);
  const int cx = x + 17 + ics / 2, cy = y + h / 2;
  if      (quem == 0) icSatelite(g, cx, cy, ics, cor);
  else if (quem == 1) icAntena  (g, cx, cy, ics, cor);
  else                icMapa    (g, cx, cy, ics, cor, riscado);
  if (wv) {
    g.setTextDatum(middle_left);
    g.setTextColor(cor);
    g.drawString(val, x + 15 + ics + 9, cy + 1);
  }
  g.setFont(&fonts::Font0);
  return w;
}

// BOTAO SO DE ICONE, para as acoes secundarias: voltar, fechar, ajustes.
// Elas se repetem em toda tela, e escrever "VOLTAR" nove vezes e ruido - o
// chevron diz a mesma coisa e devolve a largura para o conteudo.
// ic: 0 seta-esq, 1 seta-dir, 2 X, 3 engrenagem, 4 comboio, 5 alerta, 6 info, 7 lapis
template <typename G>
void botaoIc(G& g, const Ret& r, uint8_t ic, uint16_t fundo, uint16_t frente,
             bool premido = false)
{
  g.fillRoundRect(r.x, r.y, r.w, r.h, RAIO, fundo);
  // A sombra interna e o que faz o retangulo parecer apertavel. Ela troca de
  // lado quando premido, entao o botao AFUNDA sob o dedo em vez de so mudar de cor.
  const uint16_t sombra = (fundo == C_SURF || fundo == C_SURF2) ? C_LINE : C_CASING;
  if (premido) g.fillRoundRect(r.x, r.y, r.w, 4, 2, sombra);
  else         g.fillRoundRect(r.x, r.y + r.h - 4, r.w, 4, 2, sombra);
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, s = 34;
  switch (ic) {
    case 0: icSeta      (g, cx, cy, s, frente, -1); break;
    case 1: icSeta      (g, cx, cy, s, frente, +1); break;
    case 2: icX         (g, cx, cy, s, frente);     break;
    case 3: icEngrenagem(g, cx, cy, s, frente);     break;
    case 4: icComboio   (g, cx, cy, s, frente);     break;
    case 5: icAlerta    (g, cx, cy, s, frente);     break;
    case 6: icInfo      (g, cx, cy, s, frente);     break;
    default:icLapis     (g, cx, cy, s, frente);     break;
  }
}

// ---------------------------------------------------------------- botoes
// Duas linhas: o verbo grande e o que acontece, pequeno. "CRIAR GRUPO" sozinho
// nao diz nada; a segunda linha explica em quatro palavras e ainda da altura ao
// alvo. Nada de alvo abaixo de 68 px nesta interface.
template <typename G>
void botao(G& g, const Ret& r, const char* titulo, const char* desc,
           uint16_t cor, bool cheio, bool premido = false)
{
  const int raio = 18;
  uint16_t fg;
  if (cheio) {
    // profundidade de verdade: banda de sombra embaixo (em cima quando premido,
    // para o botao AFUNDAR sob o dedo) + cunho de contorno escuro
    g.fillRoundRect(r.x, r.y, r.w, r.h, raio, premido ? C_TAN : cor);
    if (premido) g.fillRoundRect(r.x, r.y, r.w, 6, 3, C_CASING);
    else         g.fillRoundRect(r.x, r.y + r.h - 6, r.w, 6, 3, C_CASING);
    g.drawRoundRect(r.x, r.y, r.w, r.h, raio, C_CASING);
    fg = C_BG;
  } else {
    if (premido) g.fillRoundRect(r.x, r.y, r.w, r.h, raio, C_SURF);
    for (int k = 0; k < 2; k++)
      g.drawRoundRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, raio - k, cor);
    fg = cor;
  }
  g.setTextDatum(middle_center);
  g.setTextColor(fg);
  bool temDesc = desc && desc[0];
  g.setFont(&fonts::FreeSansBold18pt7b);
  g.drawString(titulo, r.x + r.w / 2, r.y + r.h / 2 - (temDesc ? 18 : 0));
  if (temDesc) {
    g.setFont(&fonts::FreeSans9pt7b);
    g.drawString(desc, r.x + r.w / 2, r.y + r.h / 2 + 26);
  }
  g.setFont(&fonts::Font0);
}

// Linha da lista de configuracao: rotulo pequeno em cima, valor grande embaixo.
// Da para ler o estado do aparelho de relance, sem tocar em nada. A barra laranja
// a esquerda marca o que e editavel - desabilitado parece desabilitado.
template <typename G>
void linhaCfg(G& g, const Ret& r, const char* rot, const char* val,
              const char* acao, bool ativa, uint16_t swatch = 0)
{
  // TUDO POSICIONADO PELA ALTURA DA LINHA, nada de deslocamento fixo. A versao
  // anterior cravava rotulo em y+24 e valor em y+56: numa linha de 58 px - que e
  // o que sobrava na tela de 1024x600 - os dois se sobrepunham e o toque nao
  // acertava nada. Foi o que deixou a tela de configuracao impossivel de usar.
  g.fillRoundRect(r.x, r.y, r.w, r.h, 14, C_SURF);
  if (ativa) g.fillRoundRect(r.x, r.y, 5, r.h, 2, C_SUN);

  const int px = 30;                    // respiro lateral
  const bool baixa = r.h < 96;          // linha apertada: some com o rotulo

  if (!baixa) {
    g.setTextDatum(top_left);
    g.setFont(&fonts::FreeSans9pt7b);
    g.setTextColor(C_INK3);
    g.drawString(rot, r.x + px, r.y + r.h / 5);
  }

  // o valor fica no meio vertical da linha, sempre
  int vx = r.x + px;
  const int vy = baixa ? (r.y + r.h / 2) : (r.y + r.h * 2 / 3);
  if (swatch) {
    int s = r.h / 4; if (s > 26) s = 26;
    g.fillRoundRect(vx, vy - s / 2, s, s, 6, swatch);
    vx += s + 14;
  }
  g.setTextDatum(middle_left);
  g.setFont(baixa ? &fonts::FreeSansBold12pt7b : &fonts::FreeSansBold18pt7b);
  g.setTextColor(ativa ? C_TAN : C_INK3);
  g.drawString(val, vx, vy);

  // Em linha apertada o rotulo vira o texto de acao, para nao perder o contexto
  g.setTextDatum(middle_right);
  g.setFont(&fonts::FreeSans9pt7b);
  g.setTextColor(ativa ? C_SUN : C_INK3);
  g.drawString(baixa ? rot : acao, r.x + r.w - px, r.y + r.h / 2);
  g.setFont(&fonts::Font0);
}

// ------------------------------------------------------------------ toque
// Espera o dedo SAIR e devolve onde ele estava. Usar a saida e nao a entrada
// deixa arrastar para fora do botao e desistir - o que todo mundo espera.
// GANCHO DE SERVICO. Toda tela que espera o dedo passa por aqui, e enquanto
// espera o aparelho nao pode ficar surdo: o slot de TDMA dura 105 ms e o radio
// precisa ser atendido dentro dele. Sem isto, a tela inicial - que espera 400 ms
// por vez - perdia 7 de cada 8 janelas de transmissao (medido: tx=1, perdi=7).
//
// Por que um gancho e nao uma tarefa do FreeRTOS: a tarefa resolveria o tempo,
// mas passaria a escrever g_carros[] em paralelo com quem desenha, e um double
// lido pela metade vira coordenada absurda no mapa. Aqui tudo continua numa
// linha de execucao so, e o custo e uma chamada indireta a cada 8 ms.
static void (*g_hookServico)() = nullptr;

// delay() que NAO deixa o aparelho surdo: serve o radio e o GPS enquanto espera.
// Toda espera de UI (piscada de alerta, animacao, debounce) tem de usar isto -
// um delay(150) cru custa uma janela inteira de TDMA e frases de NMEA.
inline void esperaServindo(uint32_t ms)
{
  uint32_t t0 = millis();
  do {
    if (g_hookServico) g_hookServico();
    delay(4);
  } while (millis() - t0 < ms);
}

template <typename TFT>
bool esperaToque(TFT& t, int16_t& x, int16_t& y, uint32_t limiteMs = 0)
{
  uint32_t t0 = millis();
  int16_t ux = -1, uy = -1;
  bool tocou = false;
  while (true) {
    if (g_hookServico) g_hookServico();
    int16_t tx, ty;
    if (t.getTouch(&tx, &ty)) { ux = tx; uy = ty; tocou = true; }
    else if (tocou) { x = ux; y = uy; return true; }
    if (limiteMs && (millis() - t0) > limiteMs) return false;
    delay(8);
  }
}
