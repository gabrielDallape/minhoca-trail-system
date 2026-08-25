// O MAPA.
//
// Reescrito depois de estudar o desenho do grupo_ws (telas S3), que ficou melhor
// que a minha primeira tentativa. O que veio de la, e por que cada coisa existe:
//
//   ESTRADA, nao linha. Cada trecho e contorno grosso escuro + nucleo por cima
//   (roadSeg). E o que permite a mesma cor sobreviver a fundo claro e escuro, e e
//   como todo estilo de mapa desenha via.
//
//   TRES ESTADOS DO TRAJETO, com ESPESSURA diferente - nao so cor. Codificacao
//   redundante sobrevive ao sol e ao daltonismo:
//     falta andar (roxo)      11/5 px   <- o mais grosso: e para onde voce vai
//     ja percorrido (azul)     8/3 px   <- fino: e memoria, nao instrucao
//     trecho em alerta (verm) 13/7 px   <- o mais grosso de todos
//
//   VAO VIRA PONTE TRACEJADA. Se dois pontos seguidos estao a mais de 40 m, houve
//   perda de sinal. Ligar com linha cheia MENTIRIA sobre o caminho; nao ligar
//   faria o trajeto sumir. Tracejado diz "por aqui, mas nao sei exatamente".
//
//   ANEIS DE DISTANCIA em volta de mim: dao escala sem precisar ler numero.
//
//   ROTULOS QUE NAO SE EMPILHAM. Numa fila de trilha lenta os carros ficam a 30 m
//   um do outro - a 2 m/px isso e 15 px, e os nomes viram mancha. Cada rotulo
//   desce ate achar lugar.
//
//   MARCADORES POR FORMA: lider = triangulo, seguidor = disco. Forma alem de cor,
//   pelo mesmo motivo da espessura.
//
// NORTH-UP: o mapa nao gira, so o icone. Girar tiles por software mata o FPS e o
// acelerador do P4 so faz 90 graus.
#pragma once
#include "ui.h"
#include "mundo.h"
#include "mapa_fundo.h"

// trecho de "estrada": contorno + nucleo
// Engrossa NA PERPENDICULAR da reta, nao nos dois eixos. A versao anterior
// desenhava 2*(wCont+wNucleo+2) linhas por trecho - umas 24 - e com 1200 pontos
// no rastro isso era o custo do quadro inteiro. Esta faz metade e fica mais
// regular, porque a espessura passa a ser perpendicular de verdade.
template <typename G>
void trecho(G& g, int x0, int y0, int x1, int y1,
            uint16_t cCont, uint16_t cNucleo, int wCont, int wNucleo)
{
  int dx = x1 - x0, dy = y1 - y0;
  float L = sqrtf((float)(dx * dx + dy * dy));
  if (L < 0.5f) L = 0.5f;
  float nx = -dy / L, ny = dx / L;          // normal unitaria
  for (int d = -wCont / 2; d <= wCont / 2; d++)
    g.drawLine(x0 + (int)(nx * d), y0 + (int)(ny * d),
               x1 + (int)(nx * d), y1 + (int)(ny * d), cCont);
  for (int d = -wNucleo / 2; d <= wNucleo / 2; d++)
    g.drawLine(x0 + (int)(nx * d), y0 + (int)(ny * d),
               x1 + (int)(nx * d), y1 + (int)(ny * d), cNucleo);
}

// ponte sobre o vao: tracejado. Nunca some, e nao mente sobre o caminho.
template <typename G>
void tracejado(G& g, int x0, int y0, int x1, int y1, uint16_t cor)
{
  int dx = x1 - x0, dy = y1 - y0;
  int n = (int)(sqrtf((float)(dx * dx + dy * dy)) / 12.0f);
  if (n < 1) n = 1;
  for (int i = 0; i < n; i += 2) {
    int ax = x0 + dx * i / n,       ay = y0 + dy * i / n;
    int bx = x0 + dx * (i + 1) / n, by = y0 + dy * (i + 1) / n;
    g.drawLine(ax, ay, bx, by, cor);
    g.drawLine(ax, ay + 1, bx, by + 1, cor);
  }
}

// Seta que aponta o RUMO, num mapa north-up (0 = norte, para cima). Substitui o
// triangulo fixo: um marcador que sempre aponta para o norte mente sobre para
// onde o carro vai - e saber a direcao do lider e o que decide em qual saida da
// bifurcacao ele entrou. O rumo vem do RMC (proprio) ou do pacote (dos outros);
// parado, vale o ultimo rumo em movimento, que e o que um carro parado tem.
// O entalhe atras (dois triangulos) e o que faz a frente ser inconfundivel.
template <typename G>
void marcaSetaRumo(G& g, int x, int y, uint16_t cor, int r, float rumoGraus)
{
  float a = rumoGraus * 0.017453292f;
  float sa = sinf(a), ca = cosf(a);
  // (lado, frente) no referencial do carro -> tela (y cresce para baixo)
  auto P = [&](float pl, float pf, int& ox, int& oy) {
    ox = x + (int)lroundf(pl * ca + pf * sa);
    oy = y + (int)lroundf(pl * sa - pf * ca);
  };
  auto seta = [&](float rr, uint16_t c) {
    int tx, ty, lx, ly, rx, ry, nx, ny;
    P(0,           rr * 1.15f,  tx, ty);   // bico
    P(-rr * 0.85f, -rr * 0.85f, lx, ly);   // traseira esquerda
    P( rr * 0.85f, -rr * 0.85f, rx, ry);   // traseira direita
    P(0,           -rr * 0.35f, nx, ny);   // entalhe
    g.fillTriangle(tx, ty, lx, ly, nx, ny, c);
    g.fillTriangle(tx, ty, nx, ny, rx, ry, c);
  };
  seta((float)r + 3.0f, C_CASING);
  seta((float)r, cor);
}

template <typename G>
void marcaDisco(G& g, int x, int y, uint16_t cor, int r, int slot)
{
  g.fillCircle(x, y, r + 3, C_CASING);
  g.fillCircle(x, y, r, cor);
  if (slot > 0) {
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(C_CASING);
    char s[4]; snprintf(s, sizeof(s), "%d", slot);
    g.drawString(s, x, y + 1);
    g.setFont(&fonts::Font0);
  }
}

// Qual ponto do trajeto esta mais perto de (lat,lon). Serve para saber onde eu
// estou no caminho: o que vem depois disso e "falta andar".
// ERA O GARGALO: isto roda a cada quadro, para cada carro em alerta, e a versao
// anterior chamava haversine em TODOS os pontos do rastro - quatro senos, um
// atan2 e uma raiz por ponto, 1200 vezes. Era o custo crescendo com o historico.
//
// Aqui basta ORDENAR distancias, nao medir: distancia ao quadrado em graus, com o
// cosseno da latitude calculado UMA vez, da a mesma resposta sem trigonometria
// por ponto.
inline int trechoMaisPerto(double lat, double lon)
{
  int melhor = -1; double d = 1e18;
  const double kx = cos(lat * M_PI / 180.0);
  for (int i = 0; i < g_trajN; i++) {
    const Ponto& p = trajetoEm(i);
    double a = (p.lat - lat), b = (p.lon - lon) * kx;
    double dd = a * a + b * b;
    if (dd < d) { d = dd; melhor = i; }
  }
  return melhor;
}

template <typename G>
void mapaDesenha(G& g, int w, int h, double mPorPx)
{
  const int cx = w / 2, cy = h / 2;
  mapaOrientaPeloRumo();     // o que esta a frente do carro sobe na tela
  g.fillRect(0, 0, w, h, C_BG);

  // O fundo do cartao (relevo, agua, mata, vias, POI) entra AQUI, antes de tudo
  // que vem da rede. Se nao houver cartao, fundoDesenha devolve 0 e o resto do
  // mapa continua identico: o trajeto e os carros nunca dependem do cartao.
  if (g_meuFix) fundoDesenha(g, g_fundo, w, h, mPorPx, g_meuLat, g_meuLon);

  if (!g_meuFix) {
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold24pt7b);
    g.setTextColor(C_WARN);
    g.drawString("PROCURANDO GPS", cx, cy);
    g.setFont(&fonts::Font0);
    return;
  }

  // aneis de distancia: escala sem precisar ler numero
  for (int r = 90; r < (h / 2 + 120); r += 90) g.drawCircle(cx, cy, r, C_SURF);
  // O NORTE agora passeia: com o mapa orientado pelo rumo, o "N" anda no anel
  // apontando para onde o norte realmente esta - e a bussola da tela.
  {
    const int nx = cx - (int)(g_rotSen * 180.0f);
    const int ny = cy - (int)(g_rotCos * 180.0f);
    g.fillCircle(nx, ny, 17, C_SURF);
    g.drawCircle(nx, ny, 17, C_LINE);
    g.setTextDatum(middle_center);
    g.setFont(&fonts::FreeSansBold12pt7b);
    g.setTextColor(C_INK2);
    g.drawString("N", nx, ny + 1);
  }

  // onde eu estou no caminho, e o trecho em alerta
  int meu = trechoMaisPerto(g_meuLat, g_meuLon);
  int aLo = -1, aHi = -2;
  for (int k = 1; k < MUNDO_MAX_CARROS; k++) {
    if (!g_carros[k].ativo || !g_carros[k].alerta || !g_carros[k].fix) continue;
    int pi = trechoMaisPerto(g_carros[k].lat, g_carros[k].lon);
    if (pi >= 0 && meu >= 0) { aLo = min(meu, pi); aHi = max(meu, pi); }
    break;
  }

  // O custo do quadro crescia com o tamanho do rastro (medido: 33 ms subindo para
  // 47 ms em 40 s). Pulando pontos que caem no MESMO lugar da tela, o custo passa
  // a depender do tamanho da TELA, nao do historico - e nada se perde, porque
  // eles seriam desenhados um por cima do outro de qualquer jeito.
  uint32_t tT = micros();

  // O CAMINHO DO LIDER, roxo, do ponto onde EU estou para a frente. E a promessa
  // do produto: o mundo.h ja montava g_rota[] a cada pacote do lider, mas nada a
  // desenhava - o seguidor via o lider como um ponto andando, nunca o caminho
  // ate ele. So o trecho A FRENTE entra: o que ficou para tras eu ja andei (esta
  // no meu rastro azul) ou nao me leva a lugar nenhum. Desenhado ANTES do meu
  // rastro para o azul (onde estou) ficar por cima do roxo (para onde vou) no
  // pedaco em que os dois se sobrepoem.
  if (g_rotaN >= 2) {
    int meuIdx; float fora;
    if (rotaProjeta(g_meuLat, g_meuLon, meuIdx, fora)) {
      int rx = -1, ry = 0; double rlat = 0, rlon = 0; bool rAnt = false;
      for (int i = meuIdx; i < g_rotaN; i++) {
        const Ponto& p = g_rota[i];
        int x, y; paraTela(p.lat, p.lon, cx, cy, mPorPx, x, y);
        bool ultimo = (i == g_rotaN - 1);
        if (rx >= 0 && !ultimo && abs(x - rx) < 3 && abs(y - ry) < 3) continue;
        bool vis = (x > -30 && x < w + 30 && y > -30 && y < h + 30);
        if (vis && rx >= 0) {
          // vao no radio: mesmo criterio dos 40 m do rastro
          bool vao = rAnt && haversine(rlat, rlon, p.lat, p.lon) > 40.0;
          // 15/9 (era 11/5): "muito fina" foi a reclamacao de campo - com o
          // carro pulando, a linha que diz para onde ir tem de ser gorda
          if (vao) tracejado(g, rx, ry, x, y, C_VAO);
          else     trecho(g, rx, ry, x, y, C_ROTA_C, C_ROTA, 15, 9);
        }
        rx = vis ? x : -1; ry = y; rlat = p.lat; rlon = p.lon; rAnt = true;
      }
    }
  }
  int px = -1, py = 0; double plat = 0, plon = 0; bool temAnt = false;
  for (int i = 0; i < g_trajN; i++) {
    const Ponto& p = trajetoEm(i);
    int x, y; paraTela(p.lat, p.lon, cx, cy, mPorPx, x, y);
    bool ultimo = (i == g_trajN - 1);
    if (px >= 0 && !ultimo && abs(x - px) < 3 && abs(y - py) < 3) continue;
    bool vis = (x > -30 && x < w + 30 && y > -30 && y < h + 30);
    if (vis && px >= 0) {
      bool vao = temAnt && haversine(plat, plon, p.lat, p.lon) > 40.0;
      // O rastro proprio e SEMPRE azul (mais o trecho de alerta em vermelho).
      // Havia um estado roxo "a frente" herdado do grupo_ws - la o buffer era a
      // rota COMPARTILHADA do lider e roxo significava "falta andar"; aqui este
      // buffer e o MEU proprio rastro, e pintar de roxo o pedaco dele que passa
      // perto de mim (um cruzamento com o proprio caminho) fingia rota onde nao
      // ha. Roxo agora e exclusivo da rota do lider, desenhada acima.
      if (vao)                       tracejado(g, px, py, x, y, C_VAO);
      else if (i > aLo && i <= aHi)  trecho(g, px, py, x, y, C_CASING, C_RED, 17, 9);
      else                           trecho(g, px, py, x, y, C_RASTRO_C, C_RASTRO, 11, 5);
    }
    px = vis ? x : -1; py = y; plat = p.lat; plon = p.lon; temAnt = true;
  }

  g_usTraj = micros() - tT;
  uint32_t tC = micros();

  // carros, com rotulo que nao empilha
  int lx[MUNDO_MAX_CARROS], ly[MUNDO_MAX_CARROS], nL = 0;
  for (int k = 1; k < MUNDO_MAX_CARROS; k++) {
    if (!g_carros[k].ativo || !g_carros[k].fix) continue;
    int x, y; paraTela(g_carros[k].lat, g_carros[k].lon, cx, cy, mPorPx, x, y);
    if (x < -30 || x > w + 30 || y < -30 || y > h + 30) continue;
    uint16_t c = g_carros[k].alerta ? C_RED : CORES_MAPA[g_carros[k].cor % N_CORES];
    // rumo RELATIVO ao mapa girado: a seta continua apontando para onde o carro
    // vai no MUNDO, seja qual for a rotacao da tela
    if (g_carros[k].lider) marcaSetaRumo(g, x, y, c, 15, g_carros[k].rumo - g_mapaRotG);
    else                   marcaDisco(g, x, y, c, 13, k);

    int ax = x + 20, ay = y - 10;
    for (int t = 0; t < MUNDO_MAX_CARROS; t++) {
      bool bateu = false;
      for (int i = 0; i < nL; i++)
        if (abs(lx[i] - ax) < 130 && abs(ly[i] - ay) < 22) { ay = ly[i] + 24; bateu = true; break; }
      if (!bateu) break;
    }
    if (nL < MUNDO_MAX_CARROS) { lx[nL] = ax; ly[nL] = ay; nL++; }
    g.setTextDatum(top_left);
    g.setFont(&fonts::FreeSans12pt7b);
    g.setTextColor(C_CASING);
    g.drawString(g_carros[k].nome, ax + 1, ay + 1);   // halo = cor do contorno
    g.setTextColor(c);
    g.drawString(g_carros[k].nome, ax, ay);
  }

  // eu, sempre no centro - e com o mapa orientado pelo rumo a minha seta fica
  // essencialmente para CIMA (a diferenca e so o filtro perseguindo o alvo)
  marcaSetaRumo(g, cx, cy, CORES_MAPA[g_carros[0].cor % N_CORES], 18,
                g_meuRumo - g_mapaRotG);
  g_usCarros = micros() - tC;

  // escala, centrada embaixo: o canto esquerdo agora e do botao SAIR
  int esc = (int)(100.0 / mPorPx);
  if (esc > 20 && esc < w - 60) {
    int x0 = w / 2 - esc / 2;
    g.drawFastHLine(x0, h - 24, esc, C_INK2);
    g.drawFastVLine(x0, h - 30, 12, C_INK2);
    g.drawFastVLine(x0 + esc, h - 30, 12, C_INK2);
    g.setTextDatum(bottom_center);
    g.setFont(&fonts::FreeSans9pt7b);
    g.setTextColor(C_INK2);
    g.drawString("100 m", w / 2, h - 32);
  }
  g.setFont(&fonts::Font0);
}
