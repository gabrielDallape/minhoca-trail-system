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
    C_INK3  = 0x8C50;  // #8A8A82
    C_SUN   = 0xB1C1;  // #B03A0F  laranja escurecido para contrastar no claro
    C_TAN   = 0x6A45;  // #6B4A2F  o bege da arte nao serve no claro; vira marrom
    C_RED   = 0xC8E3;  // #C81E1E
    C_OK    = 0x2446;
    C_WARN  = 0x9260;
    C_ROTA     = 0x78FA;  // #7A1FD1 roxo escuro   5,4:1 no claro
    C_ROTA_C   = 0xFFFF;  // contorno = branco (a polaridade do fundo)
    C_RASTRO   = 0x0B71;  // #0E6E8C azul escuro   4,4:1
    C_RASTRO_C = 0xFFFF;
    C_VAO      = 0x8C50;
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
    C_ROTA     = 0xCC5F;  // #C88BFF roxo claro    8,1:1 no escuro
    C_ROTA_C   = 0x1845;  // #1A0A2E
    C_RASTRO   = 0x4E9E;  // #4FD0F5 azul claro   11,0:1
    C_RASTRO_C = 0x0926;  // #0A2430
    C_VAO      = 0x5AEB;
  }
}

// TROCA DE TEMA INSTANTANEA.
//
// Trocar dia/noite repinta a tela inteira, e num painel de framebuffer isso
// APARECE: o video le o mesmo endereco que a gente escreve, entao da para ver a
// tela sendo pintada de cima para baixo. Parece transicao suave - e nao e, e
// lentidao visivel.
//
// O unico jeito de ficar instantaneo e apagar a luz, repintar no escuro e
// acender. O usuario ve o quadro novo pronto, nunca o meio do caminho. E o mesmo
// truque da abertura, e custa os ~15 ms do redesenho.
template <typename TFT, typename FN>
void trocaTema(TFT& tft, uint8_t novo, FN repinta)
{
  tft.setBrightness(0);
  aplicaTema(novo);
  repinta();
  tft.setBrightness(255);
}

// Margem unica. Antes havia 40, 60 e 8 misturados - era isso que fazia a tela
// parecer torta mesmo onde nao havia sobreposicao.
#define M 48


struct Ret { int16_t x, y, w, h; };
inline bool dentro(const Ret& r, int16_t px, int16_t py) {
  return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// Havia um "sol" (circulos concentricos laranja) atras do botao principal, como
// eco da arte do logo. Saiu a pedido: numa tela de acao ele disputava com o texto
// do botao em vez de guiar o olho. Fundo liso.

// Sol ou lua, desenhado em vetor. Mostra o modo para o qual VAI ao ser tocado -
// e a convencao do botao de dia/noite do OsmAnd.
template <typename G>
void iconeTema(G& g, int cx, int cy, int r, uint16_t cor, bool desenhaLua)
{
  if (desenhaLua) {                      // lua: circulo com uma mordida
    g.fillCircle(cx, cy, r, cor);
    g.fillCircle(cx + r / 2, cy - r / 3, r * 0.85f, C_BG);
  } else {                               // sol: disco com oito raios
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
// O subtitulo vai EMBAIXO do "MTS", nao ao lado. Isso conserta um bug real: a
// versao anterior media textWidth("MTS") DEPOIS de trocar a fonte para uma menor,
// entao o divisor e o subtitulo caiam em cima do logotipo.
template <typename G>
void cabecalho(G& g, const char* sub, bool comEngrenagem, Ret* eng = nullptr)
{
  g.setTextDatum(top_left);
  g.setFont(&fonts::FreeSansBold24pt7b);
  g.setTextColor(C_TAN);
  g.drawString("MTS", M, 30);

  g.setFont(&fonts::FreeSans9pt7b);
  g.setTextColor(C_INK3);
  g.drawString(sub, M, 92);

  if (comEngrenagem) {
    Ret r = { (int16_t)(g.width() - M - 68), 34, 68, 68 };
    g.drawRoundRect(r.x, r.y, r.w, r.h, 14, C_LINE);
    int cx = r.x + 34, cy = r.y + 34;
    for (int i = 0; i < 8; i++) {
      float a = i * 0.7853982f;
      g.fillCircle(cx + (int)(cosf(a) * 17), cy + (int)(sinf(a) * 17), 5, C_INK2);
    }
    g.fillCircle(cx, cy, 13, C_INK2);
    g.fillCircle(cx, cy, 6, C_BG);
    if (eng) *eng = { r.x, r.y, 68, 68 };
  }
  g.setFont(&fonts::Font0);
}

// Pastilha de estado (radio, gps). Hoje mostram travessao; quando o E22 e o GPS
// entrarem, acendem. O lugar fica reservado agora para o layout nao mudar depois.
template <typename G>
int pastilha(G& g, int x, int y, const char* rot, const char* val, uint16_t cor)
{
  g.setFont(&fonts::FreeSans9pt7b);
  int w = 34 + g.textWidth(rot) + 10 + g.textWidth(val) + 20;
  g.drawRoundRect(x, y, w, 40, 20, C_LINE);
  g.fillCircle(x + 20, y + 20, 5, cor);
  g.setTextDatum(middle_left);
  g.setTextColor(C_INK3);
  g.drawString(rot, x + 34, y + 21);
  g.setTextColor(cor == C_INK3 ? C_INK3 : C_INK2);
  g.drawString(val, x + 34 + g.textWidth(rot) + 10, y + 21);
  g.setFont(&fonts::Font0);
  return w;
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
    g.fillRoundRect(r.x, r.y, r.w, r.h, raio, premido ? C_TAN : cor);
    fg = C_BG;
  } else {
    if (premido) g.fillRoundRect(r.x, r.y, r.w, r.h, raio, C_SURF);
    for (int k = 0; k < 3; k++)
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
  g.fillRoundRect(r.x, r.y, r.w, r.h, 14, C_SURF);
  if (ativa) g.fillRoundRect(r.x, r.y, 5, r.h, 2, C_SUN);

  g.setTextDatum(top_left);
  g.setFont(&fonts::FreeSans9pt7b);
  g.setTextColor(C_INK3);
  g.drawString(rot, r.x + 30, r.y + 24);

  int vx = r.x + 30;
  if (swatch) { g.fillRoundRect(vx, r.y + 60, 26, 26, 6, swatch); vx += 40; }
  g.setFont(&fonts::FreeSansBold18pt7b);
  g.setTextColor(ativa ? C_TAN : C_INK3);
  g.drawString(val, vx, r.y + 56);

  g.setTextDatum(middle_right);
  g.setFont(&fonts::FreeSans9pt7b);
  g.setTextColor(ativa ? C_SUN : C_INK3);
  g.drawString(acao, r.x + r.w - 30, r.y + r.h / 2);
  g.setFont(&fonts::Font0);
}

// ------------------------------------------------------------------ toque
// Espera o dedo SAIR e devolve onde ele estava. Usar a saida e nao a entrada
// deixa arrastar para fora do botao e desistir - o que todo mundo espera.
template <typename TFT>
bool esperaToque(TFT& t, int16_t& x, int16_t& y, uint32_t limiteMs = 0)
{
  uint32_t t0 = millis();
  int16_t ux = -1, uy = -1;
  bool tocou = false;
  while (true) {
    int16_t tx, ty;
    if (t.getTouch(&tx, &ty)) { ux = tx; uy = ty; tocou = true; }
    else if (tocou) { x = ux; y = uy; return true; }
    if (limiteMs && (millis() - t0) > limiteMs) return false;
    delay(8);
  }
}
