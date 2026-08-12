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
#define C_BG      0x0861   // (12,12,12) - o mesmo fundo da arte
#define C_SURF    0x18E3   // linhas de configuracao
#define C_SURF2   0x2103   // teclas
#define C_LINE    0x3987   // horizonte, contorno quieto
#define C_INK     0xF77C
#define C_INK2    0xA4D1
#define C_INK3    0x6B0A
#define C_SUN     0xD243   // laranja do sol da arte
#define C_SUN_DK  0x7942   // borda do degrade do sol
#define C_TAN     0xDE35   // bege das letras "MTS"
#define C_OK      0x4CCB
#define C_WARN    0xC544

// Margem unica. Antes havia 40, 60 e 8 misturados - era isso que fazia a tela
// parecer torta mesmo onde nao havia sobreposicao.
#define M 48

// Cores dos carros no mapa. Escolhidas para se distinguirem sobre o fundo escuro
// E entre si - e o que separa um carro do outro quando forem 25.
static const uint16_t CORES_MAPA[] = { C_SUN, 0x2D7F, 0x4CCB, 0xFD20, 0xF81F, 0x07FF };
static const char*    CORES_NOME[] = { "laranja", "azul", "verde", "amarelo", "rosa", "ciano" };
#define N_CORES 6

struct Ret { int16_t x, y, w, h; };
inline bool dentro(const Ret& r, int16_t px, int16_t py) {
  return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

// Havia um "sol" (circulos concentricos laranja) atras do botao principal, como
// eco da arte do logo. Saiu a pedido: numa tela de acao ele disputava com o texto
// do botao em vez de guiar o olho. Fundo liso.

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
