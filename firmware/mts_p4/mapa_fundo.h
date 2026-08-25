// O FUNDO DO MAPA: vias, agua, mata e relevo lidos do cartao.
//
// Fica separado do mapa.h de proposito. O mapa.h desenha o que a REDE sabe
// (trajeto do lider, carros, alerta) e tem de funcionar sem cartao nenhum; este
// arquivo desenha o que o CARTAO sabe. Se o cartao faltar, sair, ou vier
// corrompido, o mapa.h continua igual - so sem fundo. Um aparelho que para de
// mostrar o carro da frente porque o cartao soltou no tranco seria pior que um
// aparelho sem mapa.
//
// ORDEM DE PINTURA, de baixo para cima. Nao e gosto: e o que faz a via ficar
// legivel sobre a mata e o trajeto ficar legivel sobre a via.
//     relevo sombreado -> areas (mata, lavoura, agua) -> agua linear ->
//     vias (contorno + nucleo) -> POI -> [o mapa.h desenha o trajeto e os carros]
//
// CACHE EM PSRAM, e nao leitura por quadro. MEDIDO: um setor de mapa custa 13 ms
// no caso tipico e um bloco de relevo 29 ms. Ler os 4 a 6 setores da tela a cada
// quadro seria 60 a 180 ms - o quadro inteiro tem 73 ms de orcamento. Como o
// carro anda devagar, o mesmo setor serve centenas de quadros: le uma vez ao
// cruzar a divisa e guarda.
#pragma once
#include "ui.h"
#include "sd_setor.h"
#include "../vec_pack.h"
#include "../dem_pack.h"

// Quantos setores guardar. 4 cobre a tela (2x2) e sobra para o vizinho para onde
// o carro esta indo.
//
// O TAMANHO DE CADA SLOT VEM DO PACOTE, nao daqui. A primeira versao cravava
// 448 KB, calculado convertendo o tempo de leitura do pior setor em bytes. O
// maior bloco de verdade tem 615 KB, entao os 5 setores mais pesados do Brasil -
// as maiores cidades, entre elas Sao Paulo - devolviam TOOBIG e simplesmente NAO
// APARECIAM. E so acontecia no zoom de 15 km, o unico que usa o nivel medio.
// Agora o gerador grava o maior bloco no cabecalho e o firmware aloca isso.
#define FUNDO_VEC_SLOTS   6
#define FUNDO_VEC_MIN     (64u * 1024u)
#define FUNDO_VEC_MAX     (2048u * 1024u)   // teto de sanidade contra cabecalho corrompido
#define FUNDO_DEM_SLOTS   6

// Declaradas a frente, definidas no mapa.h. Sao os mesmos tracos do trajeto -
// uma estrada e desenhada exatamente como o caminho do lider, so com outra cor e
// outra espessura. Reusar garante que o mapa inteiro tenha um traco so.
template <typename G>
void trecho(G& g, int x0, int y0, int x1, int y1,
            uint16_t cCont, uint16_t cNucleo, int wCont, int wNucleo);
template <typename G>
void tracejado(G& g, int x0, int y0, int x1, int y1, uint16_t cor);
template <typename G>
void fundoPoi(G& g, int x, int y, uint8_t sub, double mPorPx);

struct FundoSlot {
  uint8_t* dados = nullptr;
  uint32_t len = 0;
  int      nivel = -1;
  uint32_t x = 0, y = 0;
  uint32_t usoEm = 0;         // para o descarte do menos usado
  bool     valido = false;
};

struct DemSlot {
  uint8_t* dados = nullptr;
  int32_t  slot = -1;
  uint32_t usoEm = 0;
  bool     valido = false;
};

struct Fundo {
  uint32_t slotBytes = 0;      // tamanho de cada slot, vindo do pacote
  bool     ok = false;
  bool     temVec = false, temDem = false;
  VecPack  vec;
  DemPack  dem;
  SdArea   areaVec, areaDem;
  FundoSlot vs[FUNDO_VEC_SLOTS];
  DemSlot   ds[FUNDO_DEM_SLOTS];
  uint32_t relogio = 0;
  // diagnostico - e o que diz se o cartao esta morrendo ou se o cache esta pequeno
  uint32_t acertos = 0, faltas = 0, grandesDemais = 0;
  char     erro[64] = "";
};

static Fundo g_fundo;

// Cronometro por PEDACO do quadro. Existe porque eu ja errei duas vezes o palpite
// de onde estava o custo: otimizei o descarte de pontos e o sombreado e o quadro
// caiu 246 -> 222 ms, ou seja, o gargalo era outro. Medir por pedaco custa quatro
// chamadas de micros() por quadro e acaba com o chute.
static uint32_t g_usRelevo = 0, g_usVias = 0, g_usTraj = 0, g_usCarros = 0;

// --------------------------------------------------------------------- abrir
inline bool fundoAbre(Fundo& f) {
  f = Fundo();
  if (!sdSetorInit(g_sd)) { strcpy(f.erro, "cartao nao respondeu"); return false; }

  uint32_t base, n;
  if (sdAchaParticao("MTSVECT1", base, n)) {
    f.areaVec.base = base;
    f.temVec = vecPackOpen(f.vec, sdLeArea, &f.areaVec);
  }
  if (sdAchaParticao("MTSDEM01", base, n)) {
    f.areaDem.base = base;
    f.temDem = demPackOpen(f.dem, sdLeArea, &f.areaDem);
  }
  if (!f.temVec && !f.temDem) {
    strcpy(f.erro, "cartao sem particao de mapa");
    return false;
  }

  // Buffers na PSRAM. Se nao couber tudo, reduz o cache em vez de desistir: com
  // um slot so o mapa ainda desenha, so relendo mais.
  if (f.temVec) {
    uint32_t b = f.vec.maiorBloco;
    if (b == 0) b = 640u * 1024u;      // pacote antigo, sem o campo: chuta alto
    b = ((b + VECPACK_SECTOR - 1) / VECPACK_SECTOR) * VECPACK_SECTOR;
    if (b < FUNDO_VEC_MIN) b = FUNDO_VEC_MIN;
    if (b > FUNDO_VEC_MAX) b = FUNDO_VEC_MAX;
    f.slotBytes = b;
  }
  for (int i = 0; i < FUNDO_VEC_SLOTS && f.temVec; i++) {
    f.vs[i].dados = (uint8_t*)ps_malloc(f.slotBytes);
    if (!f.vs[i].dados) break;
  }
  uint32_t db = f.temDem ? f.dem.blocoSetores * DEMPACK_SECTOR : 0;
  for (int i = 0; i < FUNDO_DEM_SLOTS && f.temDem && db; i++) {
    f.ds[i].dados = (uint8_t*)ps_malloc(db);
    if (!f.ds[i].dados) break;
  }
  if (f.temVec && !f.vs[0].dados) { strcpy(f.erro, "sem PSRAM para o mapa"); return false; }

  f.ok = true;
  return true;
}

// ---------------------------------------------------------------- cache vec
inline FundoSlot* fundoSetor(Fundo& f, int nivel, uint32_t x, uint32_t y) {
  for (int i = 0; i < FUNDO_VEC_SLOTS; i++) {
    FundoSlot& s = f.vs[i];
    if (s.dados && s.valido && s.nivel == nivel && s.x == x && s.y == y) {
      s.usoEm = ++f.relogio;
      f.acertos++;
      return &s;
    }
  }
  // escolhe o slot mais antigo (ou um vazio)
  FundoSlot* alvo = nullptr;
  for (int i = 0; i < FUNDO_VEC_SLOTS; i++) {
    if (!f.vs[i].dados) continue;
    if (!f.vs[i].valido) { alvo = &f.vs[i]; break; }
    if (!alvo || f.vs[i].usoEm < alvo->usoEm) alvo = &f.vs[i];
  }
  if (!alvo) return nullptr;

  f.faltas++;
  uint32_t len = 0;
  VecPackResult r = vecPackReadBlock(f.vec, nivel, x, y, alvo->dados,
                                     f.slotBytes, len, false);
  if (r == VECPACK_TOOBIG) {
    f.grandesDemais++;
    // Falar alto na primeira vez. Antes isto so incrementava um contador que
    // ninguem lia, e o sintoma na tela era "a cidade sumiu" - sem pista nenhuma.
    if (f.grandesDemais == 1)
      Serial.printf("MAPA: setor (%d,%lu,%lu) nao cabe em %lu KB - nao desenhado. "
                    "Rode 'vetor.py tol' no pacote.\n",
                    nivel, (unsigned long)x, (unsigned long)y,
                    (unsigned long)(f.slotBytes / 1024));
  }
  if (r != VECPACK_OK) {
    // ABSENT tambem entra no cache, como setor vazio: sem isso o aparelho
    // tentaria reler o oceano a cada quadro.
    alvo->valido = (r == VECPACK_ABSENT);
    alvo->len = 0;
    alvo->nivel = nivel; alvo->x = x; alvo->y = y; alvo->usoEm = ++f.relogio;
    return alvo->valido ? alvo : nullptr;
  }
  alvo->len = len; alvo->nivel = nivel; alvo->x = x; alvo->y = y;
  alvo->usoEm = ++f.relogio; alvo->valido = true;
  return alvo;
}

inline DemSlot* fundoDem(Fundo& f, int32_t slot) {
  if (!f.temDem || slot < 0) return nullptr;
  for (int i = 0; i < FUNDO_DEM_SLOTS; i++) {
    DemSlot& s = f.ds[i];
    if (s.dados && s.valido && s.slot == slot) { s.usoEm = ++f.relogio; return &s; }
  }
  DemSlot* alvo = nullptr;
  for (int i = 0; i < FUNDO_DEM_SLOTS; i++) {
    if (!f.ds[i].dados) continue;
    if (!f.ds[i].valido) { alvo = &f.ds[i]; break; }
    if (!alvo || f.ds[i].usoEm < alvo->usoEm) alvo = &f.ds[i];
  }
  if (!alvo) return nullptr;
  DemPackResult r = demPackReadBlock(f.dem, slot, alvo->dados,
                                     f.dem.blocoSetores * DEMPACK_SECTOR, false);
  alvo->slot = slot; alvo->usoEm = ++f.relogio;
  alvo->valido = (r == DEMPACK_OK);
  return alvo->valido ? alvo : nullptr;
}

// ------------------------------------------------------------ estilo por classe
//
// O QUE ENTRA NA TELA, E EM QUE ZOOM. Esta tabela e a decisao de produto mais
// importante do mapa, e foi feita a pedido do usuario: um GPS de trilha nao e um
// mapa de cidade. Cada linha aqui responde "isto muda alguma decisao de quem esta
// dirigindo numa trilha?". O que nao muda, sai.
//
// Nada disso apaga dado do cartao - o pacote continua completo. E so o que se
// DESENHA. Mudar de ideia depois e mexer nesta tabela e regravar o firmware.
//
//   classe      espessura   aparece quando        por que
//   ----------  ----------  --------------------  ----------------------------
//   TRILHA      11/6        sempre                E O ASSUNTO. O traço mais
//                                                 grosso da tela, acima do
//                                                 asfalto - o contrario do que
//                                                 faz um mapa de rua.
//   ESTRADA      9/5        sempre                e como voce chega e sai
//   RIO          7/4        sempre                agua decide se ha caminho
//   CORREGO      0/2        ate 3 m/px            travessia; de longe vira lixo
//   LAGO        contorno    sempre                nao se atravessa
//   RUA          5/2        ate 3 m/px            so serve para chegar na cidade;
//                                                 de longe e o que entope a tela
//   PROTEGIDA   tracejado   sempre                limite LEGAL (parque, terra
//                                                 indigena) - entrar sem saber
//                                                 da multa
//   MATA        contorno    a partir de 3 m/px    campo x mata fechada e contexto
//                                                 de longe; dentro dela nao ajuda
//   PISTA        6/3        sempre                referencia e pouso de resgate
//   FERROVIA     5/2        ate 5 m/px            travessia de nivel
//   ----------  removidos
//   PEDESTRE    calçada e ciclovia. Num 4x4, nunca.
//   USO         lavoura e pasto. Nao muda decisao nenhuma, e sao milhoes de
//               pontos de contorno.
//   LIMITE      divisa de municipio. Linha administrativa invisivel no chao.
struct EstiloVia { uint8_t cont, nucleo; uint16_t cor; bool tracejada; };

// MAPA ENXUTO, decidido em campo (2026-08-24): dirigindo, o que importa e
// ESTRADA (terra e asfalto), AGUA e DIVISA (municipio, area protegida). Relevo,
// mata, ferrovia e pista sairam - "muita informacao" foi a reclamacao literal.
// Nada disso apaga dado do cartao; reativar e devolver um case.
inline bool estiloDe(uint8_t classe, double mPorPx, EstiloVia& e) {
  switch (classe) {
    case VEC_TRILHA:   e = { 11, 6, C_TAN,    false }; return true;
    case VEC_ESTRADA:  e = {  9, 5, C_SUN,    false }; return true;
    case VEC_RIO:      e = {  7, 4, C_RASTRO, false }; return true;
    case VEC_CORREGO:
      if (mPorPx > 3.0) return false;
      e = { 0, 2, C_RASTRO, false }; return true;
    case VEC_RUA:
      // So bem de perto, e SEM CONTORNO. Duas razoes, uma de projeto e uma
      // medida. De projeto: rua de cidade nao e o assunto deste aparelho, e
      // acima de 1,5 m/px ela vira uma malha cinza que engole a trilha.
      // Medida: a 2 m/px sobre Sao Paulo eram ~5.500 segmentos na tela, cada um
      // pedindo 7 linhas (contorno 5 + nucleo 2) = ~38.000 chamadas de desenho,
      // que a 3-4 us deram os 146 ms medidos. Sem contorno sao 2 linhas.
      if (mPorPx > 1.5) return false;
      e = { 0, 2, C_INK3, false }; return true;
    case VEC_PROTEGIDA: e = { 0, 3, C_OK,  true }; return true;
    case VEC_LIMITE:    e = { 0, 2, C_INK3, true }; return true;  // divisa de municipio
    default: return false;   // PEDESTRE, USO, MATA, FERROVIA, PISTA: fora
  }
}

// Areas. So a agua sobrou: e a unica que decide caminho.
inline bool corArea(uint8_t classe, double mPorPx, uint16_t& cor) {
  (void)mPorPx;
  switch (classe) {
    case VEC_LAGO: cor = C_RASTRO_C; return true;
    default: return false;
  }
}

// ------------------------------------------------------------------- desenho
// Janela da tela em coordenada LOCAL do setor (u16). Calculada uma vez por setor.
//
// E daqui que sai o maior ganho de desempenho do mapa. No zoom mais fechado a
// tela mostra 512 m de um setor que guarda 4,9 km: 99,3% da area do setor nao
// aparece. Antes eu transformava TODO ponto para a tela e so entao testava se
// caia fora - pagando duas multiplicacoes em ponto flutuante por ponto para
// descobrir que 99% deles nem chegam perto. Agora o teste e feito em u16 puro,
// antes de qualquer conta.
struct VecJanela { int32_t x0, x1, y0, y1; int32_t minL; };

inline void vecJanelaInit(VecJanela& j, const VecXform& xf, int w, int h) {
  // Com o mapa girando, a janela em coordenada LOCAL e o bbox dos quatro cantos
  // da tela levados pela INVERSA da matriz. Continua barata (roda uma vez por
  // setor) e o descarte por ponto segue em inteiro puro.
  const float MARGEM = 40.0f;
  const float det = xf.a * xf.d - xf.b * xf.c;
  if (det == 0) { j.x0 = j.y0 = 0; j.x1 = j.y1 = 65535; j.minL = 1; return; }
  const float ia =  xf.d / det, ib = -xf.b / det;
  const float ic = -xf.c / det, id =  xf.a / det;
  const float xs[4] = { -MARGEM, (float)w + MARGEM, (float)w + MARGEM, -MARGEM };
  const float ys[4] = { -MARGEM, -MARGEM, (float)h + MARGEM, (float)h + MARGEM };
  float lx0 = 1e9f, lx1 = -1e9f, ly0 = 1e9f, ly1 = -1e9f;
  for (int i = 0; i < 4; i++) {
    const float px = xs[i] - xf.ox, py = ys[i] - xf.oy;
    const float lx = ia * px + ib * py, ly = ic * px + id * py;
    if (lx < lx0) lx0 = lx;
    if (lx > lx1) lx1 = lx;
    if (ly < ly0) ly0 = ly;
    if (ly > ly1) ly1 = ly;
  }
  j.x0 = (int32_t)lx0; j.x1 = (int32_t)lx1;
  j.y0 = (int32_t)ly0; j.y1 = (int32_t)ly1;
  // Menor deslocamento que vale desenhar, em unidades locais. MEDIDO: a 2 m/px
  // em Sao Paulo as vias custavam 146 ms, e boa parte disso era segmento de rua
  // com menos de um pixel de comprimento - cada um pedindo 7 chamadas de linha
  // (contorno + nucleo) para pintar um pixel que ja estava la. O mapa.h ja fazia
  // esse descarte no rastro do lider desde sempre; faltava aqui.
  const float esc = sqrtf(xf.a * xf.a + xf.c * xf.c);
  j.minL = (int32_t)(1.5f / (esc > 0 ? esc : 1e-6f));
  if (j.minL < 1) j.minL = 1;
}

template <typename G>
static void fundoLinha(G& g, const uint8_t* pts, uint16_t n, const VecXform& xf,
                       const VecJanela& j, const EstiloVia& e, int w, int h) {
  int32_t ax = vpLE16(pts), ay = vpLE16(pts + 2);
  int sax = 0, say = 0;
  bool temA = false;
  for (uint16_t i = 1; i < n; i++) {
    int32_t bx = vpLE16(pts + i * 4), by = vpLE16(pts + i * 4 + 2);

    // Pula o ponto que cai praticamente no mesmo pixel do anterior. O ultimo
    // ponto NUNCA e pulado, senao a linha encolhe e o poligono nao fecha - o
    // erro classico de decimacao.
    if (i < n - 1) {
      int32_t dx = bx > ax ? bx - ax : ax - bx;
      int32_t dy = by > ay ? by - ay : ay - by;
      if (dx < j.minL && dy < j.minL) continue;
    }

    // Descarte por segmento, em inteiro. Por SEGMENTO e nao por ponto porque uma
    // reta longa pode atravessar a tela com as duas pontas fora dela.
    bool fora = (ax < j.x0 && bx < j.x0) || (ax > j.x1 && bx > j.x1)
             || (ay < j.y0 && by < j.y0) || (ay > j.y1 && by > j.y1);
    if (!fora) {
      if (!temA) {
        sax = (int)(xf.ox + (float)ax * xf.a + (float)ay * xf.b);
        say = (int)(xf.oy + (float)ax * xf.c + (float)ay * xf.d);
      }
      int sbx = (int)(xf.ox + (float)bx * xf.a + (float)by * xf.b);
      int sby = (int)(xf.oy + (float)bx * xf.c + (float)by * xf.d);
      if (e.tracejada) {
        tracejado(g, sax, say, sbx, sby, e.cor);
      } else if (e.cont) {
        trecho(g, sax, say, sbx, sby, C_CASING, e.cor, e.cont, e.nucleo);
      } else {
        for (int k = 0; k < e.nucleo; k++) g.drawLine(sax, say + k, sbx, sby + k, e.cor);
      }
      sax = sbx; say = sby;
      temA = true;
    } else {
      temA = false;
    }
    ax = bx; ay = by;
  }
}

// Sombreado do relevo.
//
// PERCORRE AS AMOSTRAS, NAO OS PIXELS. A primeira versao varria a tela de 8 em 8
// pixels e, para cada bloquinho, fazia uma busca geografica completa: dois
// floor() em double, varredura do cache e conversao para posicao dentro do bloco.
// MEDIDO: 170 ms dos 220 ms de desenho - 76% do quadro para desenhar o FUNDO.
//
// O absurdo aparece na escala: o modelo tem 90 m de resolucao, e no zoom fechado
// a tela mostra 512 m. Eram ~6 amostras cobrindo a tela inteira, e eu buscava
// cada uma 2.400 vezes. Agora o laco anda em amostra, o bloco e resolvido UMA vez
// (um bloco cobre 11 km, entao no maximo 2x2 blocos aparecem), e cada amostra
// pinta de uma vez o retangulo de tela que ela cobre.
template <typename G>
static void fundoRelevo(G& g, Fundo& f, int w, int h, double mPorPx,
                        double lat, double lon) {
  // DESLIGADO por decisao do usuario em campo (2026-08-24): com relevo, mata e
  // tudo mais o mapa ficou "muita informacao" - o que ele quer ver dirigindo e
  // estrada, agua e divisa. O codigo fica inteiro porque a decisao e reversivel
  // (um return a menos) e o relevo tambem e o unico bloqueio do mapa girado:
  // vetor gira de graca, retangulo de relevo nao.
  return;
  if (!f.temDem) return;
  // Abaixo disto o modelo nao tem o que dizer: a 1,5 m/px uma amostra de 90 m
  // ocupa 60 px. Desenhar relevo mais fechado que isso e inventar detalhe que o
  // dado nao tem - e custa caro para mentir.
  if (mPorPx < 1.5) return;

  const double DEG = 0.017453292519943295;
  float mx, my;
  demPassoMetros(f.dem, lat, mx, my);
  const double gLat = mPorPx / 111320.0;                       // graus por pixel
  const double gLon = mPorPx / (111320.0 * cos(lat * DEG));
  const int N = f.dem.n;
  const double passo = 1.0 / ((double)f.dem.porGrau * (N - 1));  // graus por amostra
  double pxX = passo / gLon, pxY = passo / gLat;                 // pixels por amostra

  // Nao desenhar mais que ~140 celulas na largura. No zoom mais aberto a tela
  // pega 171 amostras; sem esse teto seriam milhares de retangulos de 7 px.
  int salto = 1;
  while (pxX * salto < (double)w / 140.0) salto++;
  double cw = pxX * salto, ch2 = pxY * salto;
  if (cw < 1) cw = 1;
  if (ch2 < 1) ch2 = 1;

  // blocos que a tela toca (0,1 grau = 11,1 km cada, entao sao poucos)
  double laTop = lat + (h * 0.5) * gLat, laBot = lat - (h * 0.5) * gLat;
  double loEsq = lon - (w * 0.5) * gLon, loDir = lon + (w * 0.5) * gLon;
  int32_t l0 = (int32_t)floor((laBot + 90.0) * f.dem.porGrau);
  int32_t l1 = (int32_t)floor((laTop + 90.0) * f.dem.porGrau);
  int32_t c0 = (int32_t)floor((loEsq + 180.0) * f.dem.porGrau);
  int32_t c1 = (int32_t)floor((loDir + 180.0) * f.dem.porGrau);
  if (l1 - l0 > 3) l1 = l0 + 3;                 // trava contra conta absurda
  if (c1 - c0 > 3) c1 = c0 + 3;

  for (int32_t bl = l0; bl <= l1; bl++) {
    for (int32_t bc = c0; bc <= c1; bc++) {
      DemSlot* s = fundoDem(f, demPackSlotLC(f.dem, bl, bc));
      if (!s) continue;
      double bLat, bLon;
      demPackCantoSO(f.dem, bl, bc, bLat, bLon);

      for (int r = 0; r < N; r += salto) {
        // linha 0 do bloco e o NORTE
        double la = bLat + (1.0 / f.dem.porGrau) * (1.0 - (double)r / (N - 1));
        double py = h * 0.5 - (la - lat) / gLat;
        if (py < -ch2 || py > h) continue;
        for (int c = 0; c < N; c += salto) {
          double lo = bLon + (1.0 / f.dem.porGrau) * ((double)c / (N - 1));
          double px = w * 0.5 + (lo - lon) / gLon;
          if (px < -cw || px > w) continue;
          uint8_t v = demSombra(f.dem, s->dados, r, c, mx, my);
          // 180 e o valor do terreno plano (a luz padrao a 45 graus). So o
          // DESVIO do plano vira tinta, e de leve: relevo e fundo, nao dado a ler.
          int d = ((int)v - 180) / 6;
          if (d == 0) continue;
          uint16_t cor = g_tema ? (d > 0 ? C_SURF2 : C_CASING)
                                : (d > 0 ? C_SURF  : C_SURF2);
          g.fillRect((int)px, (int)py, (int)cw + 1, (int)ch2 + 1, cor);
        }
      }
    }
  }
}

// Desenha tudo que vem do cartao. Devolve quantos setores usou (0 = sem fundo).
template <typename G>
int fundoDesenha(G& g, Fundo& f, int w, int h, double mPorPx, double lat, double lon) {
  if (!f.ok || !f.temVec) return 0;

  int li = vecPackLevelForScale(f.vec, mPorPx, w, h, lat);
  uint32_t x0, y0, x1, y1;
  vecPackCoverage(f.vec, li, lat, lon, mPorPx, w, h, x0, y0, x1, y1);

  // trava de seguranca: se a conta de cobertura explodir por qualquer motivo,
  // melhor desenhar menos que travar a tela
  if ((x1 - x0) > 8) x1 = x0 + 8;
  if ((y1 - y0) > 8) y1 = y0 + 8;

  uint32_t tR = micros();
  fundoRelevo(g, f, w, h, mPorPx, lat, lon);
  g_usRelevo = micros() - tR;
  uint32_t tV = micros();

  // ORDEM DE PINTURA. O bloco guarda as camadas ordenadas por NUMERO de classe,
  // que nao e a ordem em que se desenha: estrada e a classe 1 e teria de vir
  // depois da mata, que e a 8. Renumerar as classes resolveria, mas o numero esta
  // gravado no cartao e renumerar quebraria todo pacote ja feito.
  static const uint8_t ORDEM[] = {
    VEC_LAGO, VEC_PROTEGIDA, VEC_LIMITE,             // fundo e divisas
    VEC_RIO, VEC_CORREGO,                            // agua
    VEC_RUA, VEC_ESTRADA, VEC_TRILHA,                // via mais importante por cima
  };
  const int N_ORDEM = sizeof(ORDEM);

  for (uint32_t ty = y0; ty <= y1; ty++) {
    for (uint32_t tx = x0; tx <= x1; tx++) {
      FundoSlot* s = fundoSetor(f, li, tx, ty);
      if (!s || !s->len) continue;
      VecXform xf;
      vecXformInit(xf, f.vec, li, tx, ty, lat, lon, mPorPx, w, h,
                   g_rotSen, g_rotCos);   // o fundo gira junto com o rumo
      VecJanela jan;
      vecJanelaInit(jan, xf, w, h);

      // Indexa as camadas numa passada. Percorrer camada custa O(feicoes) - le
      // dois bytes e pula - e nao O(pontos). A versao anterior fazia QUATRO
      // passadas completas pelo bloco, e cada uma transformava todos os pontos:
      // era 4x o trabalho para desenhar a mesma coisa.
      const uint8_t* ini[VEC_POI + 1] = { nullptr };
      uint16_t qtd[VEC_POI + 1] = { 0 };
      VecCursor cur;
      if (!vecCursorInit(cur, s->dados, s->len)) continue;
      while (vecCursorLayer(cur)) {
        if (cur.classe <= VEC_POI) { ini[cur.classe] = cur.p; qtd[cur.classe] = cur.feicoes; }
      }

      for (int k = 0; k < N_ORDEM; k++) {
        uint8_t cl = ORDEM[k];
        if (!ini[cl] || !qtd[cl]) continue;
        uint16_t corA;
        bool ehArea = corArea(cl, mPorPx, corA);
        EstiloVia e;
        if (ehArea) e = EstiloVia{ 0, 2, corA, false };
        else if (!estiloDe(cl, mPorPx, e)) continue;

        VecCursor c2;
        c2.p = ini[cl]; c2.fim = s->dados + s->len;
        c2.classe = cl; c2.feicoes = qtd[cl]; c2.camadas = 0;
        const uint8_t* pts; uint16_t n;
        while (vecCursorLine(c2, pts, n))
          fundoLinha(g, pts, n, xf, jan, e, w, h);
      }

      // POI por ultimo, para nao ficar debaixo de via
      if (ini[VEC_POI] && qtd[VEC_POI]) {
        VecCursor c2;
        c2.p = ini[VEC_POI]; c2.fim = s->dados + s->len;
        c2.classe = VEC_POI; c2.feicoes = qtd[VEC_POI]; c2.camadas = 0;
        uint16_t px, py; uint8_t sub, nl; const char* nome;
        while (vecCursorPoi(c2, px, py, sub, nome, nl)) {
          if ((int32_t)px < jan.x0 || (int32_t)px > jan.x1 ||
              (int32_t)py < jan.y0 || (int32_t)py > jan.y1) continue;
          int sx = (int)(xf.ox + (float)px * xf.a + (float)py * xf.b);
          int sy = (int)(xf.oy + (float)px * xf.c + (float)py * xf.d);
          if (sx < 0 || sx >= w || sy < 0 || sy >= h) continue;
          fundoPoi(g, sx, sy, sub, mPorPx);
        }
      }
    }
  }
  g_usVias = micros() - tV;
  return (x1 - x0 + 1) * (y1 - y0 + 1);
}

// Os POI que MUDAM A DECISAO de dirigir vem sempre; o resto so quando ha espaco
// na tela. Porteira, mata-burro e vau fazem parar o carro - nunca somem.
template <typename G>
void fundoPoi(G& g, int x, int y, uint8_t sub, double mPorPx) {
  bool critico = (sub == VECP_PORTEIRA || sub == VECP_CANCELA
               || sub == VECP_MATA_BURRO || sub == VECP_VAU
               || sub == VECP_BLOQUEIO);
  // mapa enxuto: so o que PARA o carro entra; o resto virou ruido junto com o
  // relevo e a mata (decisao de campo, 2026-08-24)
  if (!critico) return;
  (void)mPorPx;

  uint16_t cor = critico ? C_WARN : C_INK2;
  if (sub == VECP_COMBUSTIVEL || sub == VECP_HOSPITAL) cor = C_OK;

  g.fillCircle(x, y, 8, C_CASING);
  g.fillCircle(x, y, 6, cor);
  if (sub == VECP_VAU) {
    // vau = agua atravessando a estrada: duas ondas
    g.drawFastHLine(x - 9, y - 1, 18, C_RASTRO);
    g.drawFastHLine(x - 9, y + 2, 18, C_RASTRO);
  } else if (sub == VECP_PORTEIRA || sub == VECP_CANCELA) {
    g.drawFastVLine(x, y - 10, 20, C_CASING);
  }
}
