#pragma once
// ============================================================================
//  TELA DA TRILHA - a tela em que o aparelho VIVE. O mapa e a tela inteira.
// ============================================================================
//
// POR QUE ESTA TELA DESENHA DIFERENTE DE TODAS AS OUTRAS: todo redesenho de tela
// cheia LIMPA para o fundo antes de pintar e esse instante em branco aparece (a
// piscada). Um blit nao passa por ele. Entao o quadro inteiro e desenhado num
// SPRITE na PSRAM e empurrado de uma vez. MEDIDO na 7B: 47 ms por quadro. O
// sprite e criado UMA vez no boot (mts.ino) - nada de alocar 1,2 MB a cada
// entrada na trilha.
//
// PERGUNTA ANTES DE PINTAR. Um quadro custa 47-73 ms quente e 220-300 ms frio,
// e a janela de TX e de ~63 ms: um quadro que comeca perto dela a engole
// inteira (era o 'perdi' de 10-30 % do campo de 09-28). O orcamento responde
// com o tempo que falta ate a janela; se nao cabe, a pintura espera a volta
// seguinte - o mapa atrasa 60 ms, o pacote nao atrasa 6 s.
//
// REPINTURA PERIODICA (novo): com posicao dos outros a cada 6 s, o marcador e
// EXTRAPOLADO (core/frota.h) e a tela repinta a cada ~1 s enquanto algum carro
// se move, para o marcador deslizar em vez de pular.

#include "ui.h"
#include "mapa.h"
#include "fila.h"
#include "../estado.h"
#include "../servico_api.h"
#include "../../hal/caixa.h"

// definidas no mts.ino: gravam tema/zoom (so por toque, nunca por quadro)
void salvaTema();
void salvaZoom();

template <typename TFT, typename SPR>
bool telaTrilha(TFT& tft, SPR& cv, bool temSprite, const Sessao& s)
{
  Ret rSair   = { M, (int16_t)(TELA_H - M - 76), 140, 76 };
  // O ALERTA e MEU: aperta quem esta em apuros, e a flag viaja no pacote para
  // acender vermelho na tela dos OUTROS.
  Ret rAlerta = { (int16_t)(M + 156), (int16_t)(TELA_H - M - 76), 230, 76 };
  Ret rModo = { (int16_t)(TELA_W - 20 - 76), (int16_t)(TELA_H - 20 - 76), 76, 76 };
  Ret rZoom = { (int16_t)(TELA_W - 20 - 76), (int16_t)(TELA_H - 20 - 76 - 86), 76, 76 };
  const int cw = 290, ch = 54, cxr = TELA_W - 20 - cw;
  auto faixaCarro = [&](int i) -> Ret {
    return Ret{ (int16_t)cxr, (int16_t)(20 + (i - 1) * (ch + 6)), (int16_t)cw, (int16_t)ch };
  };
  // 6 cartoes cabem acima do botao de zoom; com 8 slots pode haver 7 outros
  // carros. Quem fica de fora nunca e o LIDER nem quem pediu ALERTA: a lista e
  // montada nessa ordem (antes era por slot, e o 7o - talvez o lider - sumia).
  int fila[6]; int nFila = 0;
  auto montaFila = [&]() {
    nFila = 0;
    bool posto[FROTA_MAX_CARROS] = {};
    for (int passo = 0; passo < 3; passo++)
      for (int i = 1; i < FROTA_MAX_CARROS && nFila < 6; i++) {
        const Carro& c = g_frota.c[i];
        if (!c.ativo || posto[i]) continue;
        if (passo == 0 && !c.lider) continue;
        if (passo == 1 && !c.alerta) continue;
        fila[nFila++] = i; posto[i] = true;
      }
  };

  static uint32_t medN = 0, medMapa = 0, medPush = 0, medPior = 0;
  uint32_t ultimoQuadroUs = 300000, piorRecenteUs = 0;   // o primeiro quadro e sempre frio

  auto pinta = [&]() {
    const uint32_t agora = millis();
    uint32_t tA = micros();
    caixaEtapa(CP_MAPA_PINTA);
    auto& g = temSprite ? (LovyanGFX&)cv : (LovyanGFX&)tft;
    mapaDesenha(g, TELA_W, TELA_H, MAPA_ZOOMS[g_cfg.zoom], agora);

    // O GRUPO numa pilula de HUD
    int yAviso;
    {
      char gn[21]; strncpy(gn, s.gNome, 20); gn[20] = 0;
      g.setFont(&fonts::FreeSansBold18pt7b);
      const int wg = g.textWidth(gn);
      const int hh = 60;
      hudPilula(g, 16, 16, 60 + wg + 26, hh);
      icComboio(g, 16 + 34, 16 + hh / 2, 26, C_SUN);
      g.setTextDatum(middle_left); g.setTextColor(C_INK);
      g.drawString(gn, 16 + 60, 16 + hh / 2);
      if (s.lider && s.gCod[0] && s.gCod[0] != '-') {
        g.setFont(&fonts::FreeSansBold12pt7b);
        const int wc = g.textWidth(s.gCod);
        hudPilula(g, 16, 16 + hh + 10, 100 + wc + 22, 44);
        g.setFont(&fonts::FreeSans9pt7b); g.setTextColor(C_INK3);
        g.drawString("CODIGO", 16 + 18, 16 + hh + 10 + 22);
        g.setFont(&fonts::FreeSansBold12pt7b); g.setTextColor(C_SUN);
        g.drawString(s.gCod, 16 + 100, 16 + hh + 10 + 22);
      }
      yAviso = 16 + hh + 10 + 44 + 12;
      avisoDesenha(g, TELA_W, yAviso);
      if (avisoAtivo()) yAviso += 48;
    }

    // AVISOS DE REDE, abaixo das pilulas: lider sem sinal, aparelho incompativel.
    {
      char av[80]; av[0] = 0;
      int li = frotaLider(g_frota);
      if (!s.lider && li > 0 && frotaSinal(g_frota.c[li], agora) == SINAL_VELHO)
        snprintf(av, sizeof(av), "SEM SINAL DO LIDER HA %lu s", (unsigned long)(frotaSilencioMs(g_frota.c[li], agora) / 1000));
      else if (const char* t = avisoTmpAtivo(agora)) strncpy(av, t, sizeof(av) - 1);
      if (av[0]) {
        g.setFont(&fonts::FreeSansBold12pt7b);
        const int wa = g.textWidth(av) + 40;
        g.fillRoundRect(16, yAviso + 3, wa, 40, 20, C_CASING);
        g.fillRoundRect(16, yAviso, wa, 40, 20, C_WARN);
        g.setTextDatum(middle_left); g.setTextColor(C_CASING);
        g.drawString(av, 16 + 20, yAviso + 20);
      }
    }

    // AS FAIXAS VEM DA FROTA VIVA. A DISTANCIA E PELO CAMINHO quando a rota do
    // lider existe (numa curva de retorno, 200 m no caminho sao 30 m em linha reta).
    montaFila();
    const float kmEu = filaMinhaKm();
    for (int r0 = 0; r0 < nFila; r0++) {
      const Carro& m = g_frota.c[fila[r0]];
      Ret r = faixaCarro(r0 + 1);
      const FrotaSinal sinal = frotaSinal(m, agora);
      double la, lo; bool ext;
      int d = -1;
      if (g_eu.fix && frotaEstimativa(m, agora, la, lo, ext)) d = (int)geoHaversine(g_eu.lat, g_eu.lon, la, lo);
      float mFora = 0;
      const bool fora = filaForaDaTrilha(fila[r0], agora, mFora);
      if (!fora && kmEu >= 0) { float kmEle = filaKm(fila[r0], agora); if (kmEle >= 0) d = (int)fabsf(kmEle - kmEu); }
      g.fillRoundRect(r.x, r.y + 3, r.w, r.h, 16, C_CASING);
      g.fillRoundRect(r.x, r.y, r.w, r.h, 16, m.alerta ? C_RED : C_SURF);
      g.drawRoundRect(r.x, r.y, r.w, r.h, 16, m.alerta ? C_CASING : C_LINE);
      g.fillRoundRect(r.x + 6, r.y + 6, 10, r.h - 12, 5, CORES_MAPA[m.cor % N_CORES]);
      g.setTextDatum(middle_left);
      g.setFont(&fonts::FreeSansBold12pt7b);
      g.setTextColor(sinal == SINAL_VELHO ? C_INK3 : C_INK);
      char nm[13]; strncpy(nm, m.nome, 12); nm[12] = 0;
      g.drawString(nm, r.x + 28, r.y + r.h / 2);
      g.setTextDatum(middle_right);
      char t[20];
      if (sinal == SINAL_VELHO || sinal == SINAL_NUNCA) {
        // o aviso de perda de sinal, na propria faixa: e o que o motorista olha
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextColor(m.alerta ? C_INK : C_WARN);
        if (sinal == SINAL_NUNCA) strcpy(t, "SEM POS");
        else snprintf(t, sizeof(t), "SEM SINAL %lus", (unsigned long)(frotaSilencioMs(m, agora) / 1000));
      } else if (m.semGps) {
        // o radio dele fala, o GPS dele nao: o icone fica no ultimo ponto, vazado
        g.setFont(&fonts::FreeSansBold12pt7b);
        g.setTextColor(m.alerta ? C_INK : C_WARN);
        strcpy(t, "SEM GPS");
      } else if (fora) {
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

    // SAIR discreto: o unico vermelho PERMANENTE da tela e o ALERTA.
    hudPilula(g, rSair.x, rSair.y, rSair.w, rSair.h);
    g.setTextDatum(middle_center); g.setFont(&fonts::FreeSansBold12pt7b); g.setTextColor(C_INK2);
    g.drawString("SAIR", rSair.x + rSair.w / 2, rSair.y + rSair.h / 2);
    {
      const bool on = g_frota.c[0].alerta;
      g.fillRoundRect(rAlerta.x, rAlerta.y + 3, rAlerta.w, rAlerta.h, 38, C_CASING);
      g.fillRoundRect(rAlerta.x, rAlerta.y, rAlerta.w, rAlerta.h, 38, on ? C_RED : C_SURF);
      for (int k = 0; k < 2; k++)
        g.drawRoundRect(rAlerta.x + k, rAlerta.y + k, rAlerta.w - 2 * k, rAlerta.h - 2 * k, 38 - k, C_RED);
      icAlerta(g, rAlerta.x + 46, rAlerta.y + rAlerta.h / 2 - 2, 34, on ? C_INK : C_RED);
      g.setTextDatum(middle_left); g.setFont(&fonts::FreeSansBold18pt7b); g.setTextColor(on ? C_INK : C_RED);
      g.drawString(on ? "NO AR" : "ALERTA", rAlerta.x + 80, rAlerta.y + rAlerta.h / 2);
    }
    // controles flutuantes: dia/noite, zoom e a largura coberta
    fabFundo(g, rModo.x + 38, rModo.y + 38, 38, C_SURF);
    iconeTema(g, rModo.x + 38, rModo.y + 38, 20, C_INK2, g_tema == 0, C_SURF);
    fabFundo(g, rZoom.x + 38, rZoom.y + 38, 38, C_SURF);
    for (int k = 0; k < MAPA_NZOOM; k++) {
      int bx = rZoom.x + 38 - 26 + k * 11;
      int hh = 8 + k * 5;
      g.fillRect(bx, rZoom.y + 38 + 14 - hh, 7, hh, (k <= g_cfg.zoom) ? C_SUN : C_LINE);
    }
    {
      char z[12];
      double mLarg = MAPA_ZOOMS[g_cfg.zoom] * (double)TELA_W;
      if (mLarg < 1000) snprintf(z, sizeof(z), "%dm", (int)mLarg);
      else              snprintf(z, sizeof(z), "%.1fkm", mLarg / 1000.0);
      g.setFont(&fonts::FreeSansBold12pt7b);
      const int wz = g.textWidth(z);
      hudPilula(g, rZoom.x - wz - 40, rZoom.y + 18, wz + 28, 40);
      g.setTextDatum(middle_center); g.setTextColor(C_INK2);
      g.drawString(z, rZoom.x - wz - 40 + (wz + 28) / 2, rZoom.y + 38);
    }
    g.setFont(&fonts::Font0);

    uint32_t tB = micros();
    caixaEtapa(CP_MAPA_PUSH);
    if (temSprite) cv.pushSprite(0, 0);
    caixaEtapa(CP_LACO);
    uint32_t tC = micros();
    medMapa += (tB - tA); medPush += (tC - tB);
    uint32_t tot = tC - tA;
    if (tot > medPior) medPior = tot;
    ultimoQuadroUs = tot;
    if (tot > piorRecenteUs) piorRecenteUs = tot;
    if (++medN >= 16) {
      piorRecenteUs = 0;
      Serial.printf("quadro: relevo %lu + vias %lu + trajeto %lu + carros %lu | desenho %lu + push %lu = %lu ms (pior %lu) | "
                    "nivel %d zoom %.1f m/px | cache %lu acertos %lu faltas%s\n",
                    (unsigned long)(g_usRelevo / 1000), (unsigned long)(g_usVias / 1000),
                    (unsigned long)(g_usTraj / 1000), (unsigned long)(g_usCarros / 1000),
                    (unsigned long)(medMapa / medN / 1000), (unsigned long)(medPush / medN / 1000),
                    (unsigned long)((medMapa + medPush) / medN / 1000), (unsigned long)(medPior / 1000),
                    g_fundo.ok ? vecPackLevelForScale(g_fundo.vec, MAPA_ZOOMS[g_cfg.zoom], TELA_W, TELA_H, g_eu.lat) : -1,
                    MAPA_ZOOMS[g_cfg.zoom], (unsigned long)g_fundo.acertos, (unsigned long)g_fundo.faltas,
                    g_fundo.grandesDemais ? "  ATENCAO: setor grande demais" : "");
      medN = medMapa = medPush = medPior = 0;
    }
  };

  // repinta so quando algo mudou de fato - ou a cada ~1 s se alguem se move
  double  uLat = 1e9, uLon = 1e9;
  int     uZi = -1, uTema = -1, uN = -1;
  uint8_t uAl = 0;
  uint32_t uOutros = 0, uPintaMs = 0;
  auto mudou = [&]() -> bool {
    const uint32_t agora = millis();
    uint8_t al = 0;
    for (int i = 0; i < FROTA_MAX_CARROS; i++) if (g_frota.c[i].alerta) al |= (1 << i);
    int n = frotaQuantos(g_frota);
    uint32_t outros = (uint32_t)g_rotaC.n;
    bool alguemAnda = false, alguemMudo = false;
    for (int i = 1; i < FROTA_MAX_CARROS; i++) {
      const Carro& c = g_frota.c[i];
      if (!c.ativo) continue;
      outros = outros * 31 + (uint32_t)(int32_t)(c.lat * 1e4) + (uint32_t)(int32_t)(c.lon * 1e4)
             + (c.temNome ? 7u : 0u) + (c.fix ? 3u : 0u) + (c.semGps ? 17u : 0u) + (uint32_t)frotaSinal(c, agora) * 13u;
      const uint8_t sn = frotaSinal(c, agora);
      if (c.vel > 0 && sn == SINAL_VIVO) alguemAnda = true;
      if (sn != SINAL_VIVO) alguemMudo = true;
    }
    // alguemMudo: o "SEM SINAL HA 31 s" conta segundos. Sem isto, comigo parado
    // esperando e o lider mudo, nada mudava e o numero ficava congelado ate os
    // 90 s em que ele some da lista.
    const bool periodica = (alguemAnda || alguemMudo || avisoTmpAtivo(agora)) && (agora - uPintaMs >= 1000);
    if (g_cfg.zoom != uZi || g_tema != uTema || n != uN || al != uAl || outros != uOutros || periodica) {
      uZi = g_cfg.zoom; uTema = g_tema; uN = n; uAl = al; uOutros = outros;
      uLat = g_eu.lat; uLon = g_eu.lon; uPintaMs = agora; return true;
    }
    if (uLat > 1e8) { uLat = g_eu.lat; uLon = g_eu.lon; uPintaMs = agora; return true; }
    // 2 pixels de deslocamento proprio
    if (geoHaversine(uLat, uLon, g_eu.lat, g_eu.lon) >= MAPA_ZOOMS[g_cfg.zoom] * 2.0) {
      uLat = g_eu.lat; uLon = g_eu.lon; uPintaMs = agora; return true;
    }
    return false;
  };

  while (true) {
    servico();
    uint32_t custo = ultimoQuadroUs > piorRecenteUs ? ultimoQuadroUs : piorRecenteUs;
    // mudou() so e consultado quando da para pintar: tem efeito colateral.
    if (servicoCabe(custo) && mudou()) pinta();

    int16_t bx, by;
    if (!tft.getTouch(&bx, &by)) { delay(8); continue; }
    int16_t lx = bx, ly = by, tx, ty;
    // enquanto o dedo esta na tela o radio continua sendo servido
    while (tft.getTouch(&tx, &ty)) { lx = tx; ly = ty; servico(); delay(8); }
    const int16_t ux = lx, uy = ly;    // 7B: paisagem nativa, sem conversao

    if (dentro(rSair, ux, uy)) {
      const int16_t cbw = (int16_t)((TELA_W - 3 * M) / 2 > 300 ? 300 : (TELA_W - 3 * M) / 2);
      Ret sim = { (int16_t)(TELA_W / 2 - 20 - cbw), (int16_t)(TELA_H / 2 + 40), cbw, 96 };
      Ret nao = { (int16_t)(TELA_W / 2 + 20), (int16_t)(TELA_H / 2 + 40), cbw, 96 };
      tft.fillScreen(C_BG);
      tft.setTextDatum(middle_center); tft.setFont(&fonts::FreeSansBold24pt7b); tft.setTextColor(C_INK);
      tft.drawString("Sair da trilha?", TELA_W / 2, TELA_H / 2 - 110);
      tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_INK2);
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
      uZi = -1;              // FICAR: forca a repintura (o dialogo sujou a tela)
      continue;
    }
    if (dentro(rModo, ux, uy)) { aplicaTema(g_tema ? 0 : 1); salvaTema(); continue; }
    if (dentro(rZoom, ux, uy)) {
      g_cfg.zoom = (uint8_t)((g_cfg.zoom + 1) % MAPA_NZOOM);
      salvaZoom();
      ultimoQuadroUs = 300000;     // outro nivel = cache frio
      continue;
    }
    if (dentro(rAlerta, ux, uy)) {
      g_frota.c[0].alerta = !g_frota.c[0].alerta;
      registroLinha("!,%lu,alerta meu,%d", (unsigned long)millis(), g_frota.c[0].alerta ? 1 : 0);
      if (g_frota.c[0].alerta) {
        for (int k = 0; k < 3; k++) {
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
