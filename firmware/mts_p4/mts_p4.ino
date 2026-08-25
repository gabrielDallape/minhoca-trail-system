/*
 * MTS - Minhoca Trail System | tela ESP32-P4
 *
 * ETAPA 2: abertura, tela inicial e configuracao.
 *
 * Sem radio, sem GPS, sem cartao - so a placa e o cabo. O visual pode ser feito e
 * conferido agora; criar/entrar em grupo precisa de radio, e radio precisa de
 * DOIS nos que se ouçam.
 *
 * Sobre a rede: as telas S3 falam LoRaMESH e o P4 vai falar SX1262. Os dois NAO se
 * conversam. A rede TDMA sera E22 em placas P4.
 *
 *   .\tools\build.ps1 firmware\mts_p4 -Board p4 -Upload -Port COM8
 */
#define MTS_SKIP_PANEL_ID 1     // este painel trava se voce ler o ID dele
#include "hardware.h"          // define MTS_PLACA - TEM de vir antes do painel
#if MTS_PLACA == 7
  #include "LGFX_P4_LCD7.h"
#else
  #include "LGFX_P4_LCD5.h"
#endif
#include "ui.h"
#include "splash.h"
#include "teclado.h"
#include "mundo.h"
#include "grupo.h"
#include "sessao.h"
#include <Preferences.h>
#if MTS_TEM_WIFI
#include "wifi_ota.h"
#endif
#if MTS_TEM_SD
#include "SD_MMC.h"
#endif

LGFX_P4 tft;
Preferences prefs;

// Mesmo espaco e mesmas chaves do grupo_ws das telas S3, de proposito.
static char    g_nome[16] = "Carro";
static uint8_t g_cor = 0;

static Sessao g_ses;
static Membro g_membros[MAX_MEMBROS];
static int    g_nMembros = 0;

// entra o proprio aparelho como primeiro membro da lista
static void membrosReinicia(bool souLider) {
  g_nMembros = 0;
  strncpy(g_membros[0].nome, g_nome, 15); g_membros[0].nome[15] = 0;
  g_membros[0].cor = g_cor;
  g_membros[0].lider = souLider;
  g_membros[0].dist = 0;          // eu sou o centro do mapa
  g_membros[0].alerta = false;
  g_nMembros = 1;
  // O papel vale NO AR tambem, e desde ja: o roster do criador tem de dizer
  // LIDER enquanto ele ainda esta na sala de espera - e antes do mundoInicia,
  // que so roda ao abrir a trilha.
  g_carros[0].lider = souLider;
}

// chamada pelas telas de grupo quando o usuario troca dia/noite la dentro
void salvaTema() {
  Preferences p; p.begin("grupo", false); p.putUChar("theme", g_tema); p.end();
}

// Grava so quando o usuario TOCA no botao de zoom - nao a cada quadro. A NVS tem
// numero finito de escritas, e escrever em flash com o painel RGB rodando causa
// tearing (o painel le o framebuffer da PSRAM por DMA, sem bounce buffer).
void salvaZoom() {
  Preferences p; p.begin("grupo", false); p.putUChar("zoom", g_zoom); p.end();
}

static void carregaCfg() {
  prefs.begin("grupo", true);
  String n = prefs.getString("name", "Carro");
  g_cor = prefs.getUChar("color", 0);
  aplicaTema(prefs.getUChar("theme", 1));
  g_zoom = prefs.getUChar("zoom", 2);
  // SLOT TDMA: a identidade deste aparelho no ar. Fica na NVS e nao na tela de
  // configuracao porque nao e preferencia do usuario - e endereco de rede, e
  // dois carros com o mesmo slot transmitem por cima um do outro. Trocar pelo
  // serial ('s0'..'s7') ate o provisionamento por roster existir.
  g_meuSlot = prefs.getUChar("slot", 0);
#if MTS_TEM_RADIO
  // Potencia de TX na NVS: o padrao e o RF_PWR do hardware.h, mas cada aparelho
  // pode ser capado pelo serial ('p' + numero) - existe por causa do modulo
  // defeituoso que so opera ate ~2-4 dBm (ver CHANGELOG 2026-08-24).
  g_rfPwr = prefs.getChar("pwr", RF_PWR);
  if (g_rfPwr < -9 || g_rfPwr > 22) g_rfPwr = RF_PWR;
#endif
  prefs.end();
  if (g_meuSlot >= TDMA_SLOTS) g_meuSlot = 0;
  if (g_zoom >= MAPA_NZOOM) g_zoom = 2;   // NVS de uma versao antiga, ou lixo
  if (g_cor >= N_CORES) g_cor = 0;
  strncpy(g_nome, n.c_str(), sizeof(g_nome) - 1);
  g_nome[sizeof(g_nome) - 1] = 0;
}
static void salvaCfg() {
  // O que o usuario acabou de escolher vale no AR tambem, e nao so na tela: sem
  // isto ele trocaria o nome e continuaria aparecendo com o antigo nos outros
  // aparelhos ate reiniciar.
  mundoIdentidade(g_nome, g_cor);
  prefs.begin("grupo", false);
  prefs.putString("name", g_nome);
  prefs.putUChar("color", g_cor);
  prefs.putUChar("theme", g_tema);
  prefs.putUChar("zoom", g_zoom);
  prefs.end();
}

// ------------------------------------------------------------- tela inicial
static Ret hCriar, hEntrar, hEng, hRetomar, hDescarta;

// ESTADO DE VERDADE, e nao um rotulo fixo. Antes as duas pastilhas mostravam "-"
// e o rodape dizia "sem radio - etapa 2": a tela nao sabia nada sobre o aparelho.
// Agora ela responde as duas perguntas que decidem se dá para sair em trilha -
// tenho posicao? estou sincronizado com o grupo? - e por isso e repintada
// sozinha, sem esperar o usuario tocar em nada.
// Tres pastilhas: GPS, RADIO, CARTAO. Icone e numero, sem rotulo escrito - o
// desenho de satelite com um 9 ao lado diz mais rapido que "GPS: 9 satelites",
// porque a forma se reconhece antes de a palavra ser lida.
static void pintaPastilhas()
{
  // comecam DEPOIS da marca MTS do cabecalho (o traco de sol termina ~M+80);
  // limpar a partir dela, nunca do zero, senao a repintura de 1 Hz apaga a marca
  const int x0 = M + 110;
  const int py = (CAB_H - 52) / 2;
  tft.fillRect(x0 - 4, py - 2, TELA_W - x0 - 96, 56, C_BG);

  // GPS ------------------------------------------------------------------
  char vg[12] = "--";
  uint16_t cg = C_INK3;
#if MTS_TEM_GPS
  if (gpsFixValido())   { snprintf(vg, sizeof(vg), "%u", g_gpsSats); cg = C_OK; }
  else if (g_gpsFrases) { strcpy(vg, "...");  cg = C_WARN; }
  else                  { strcpy(vg, "X");    cg = C_RED;  }
#endif

  // RADIO ----------------------------------------------------------------
  char vr[12] = "--";
  uint16_t cr = C_INK3;
#if MTS_TEM_RADIO
  if      (!g_loraOk)                        { strcpy(vr, "X");    cr = C_RED;  }
  else if (g_tdma.holdover)                  { strcpy(vr, "~");    cr = C_WARN; }
  else if (g_tdma.sync == TDMA_SYNC_PPS)     { strcpy(vr, "PPS");  cr = C_OK;   }
  else if (g_tdma.sync == TDMA_SYNC_BEACON)  { strcpy(vr, "GRUPO");cr = C_OK;   }
  else                                       { strcpy(vr, "...");  cr = C_WARN; }
#endif

  int px = x0;
  px += pastilhaIc(tft, px, py, 0, vg, cg) + 12;
  px += pastilhaIc(tft, px, py, 1, vr, cr) + 12;
  // O cartao nao precisa de valor: o risco no proprio icone ja e a informacao.
  pastilhaIc(tft, px, py, 2, "", (g_fundo.temVec || g_fundo.temDem) ? C_OK : C_SUN,
               !(g_fundo.temVec || g_fundo.temDem));
}

static void desenhaInicial(int premido = -1)
{
  tft.fillScreen(C_BG);
  // SEM TITULO nesta tela, a pedido do usuario: a marca ja aparece na abertura, e
  // repetir "MTS" aqui so ocupa 130 px que fazem falta numa tela de 600.
  cabecalho(tft, "", true, &hEng);

  // Estado do radio e do GPS a ESQUERDA, engrenagem a direita. Antes as duas
  // pastilhas eram empurradas da direita para a esquerda ate encostarem na
  // engrenagem e no titulo - com 1024 de largura aquilo virava um amontoado.
  pintaPastilhas();

  tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);

  // Os dois botoes DIVIDEM a largura e ficam CENTRADOS no espaco entre o
  // cabecalho e o rodape. Assim a tela respira igual nas duas resolucoes, em vez
  // de eu escolher um y na mao para cada uma.
  // DOIS CARTOES, cada um com o icone em cima e o verbo embaixo. A segunda linha
  // explicativa saiu: "voce vira o lider da trilha" e legenda, e legenda no
  // aparelho e ruido. Quem cria sabe que vira lider; quem nao sabe descobre na
  // tela seguinte, que diz isso com o proprio conteudo.
  const int GAP  = 14;
  const int bw   = (TELA_W - 2 * M - GAP) / 2;
  int topo = CAB_H + 16;
  const int base = TELA_H - BARRA_H;

  // A FAIXA DE RETOMAR so existe se houver sessao guardada, e ela empurra os
  // cartoes para baixo em vez de flutuar por cima - assim nada se sobrepoe e a
  // tela continua a mesma quando nao ha o que retomar.
  hRetomar = { 0, 0, 0, 0 }; hDescarta = { 0, 0, 0, 0 };
  if (g_ses.ativa) {
    const int fh = 92;
    hRetomar  = { M, (int16_t)topo, (int16_t)(TELA_W - 2 * M - 92 - 10), (int16_t)fh };
    hDescarta = { (int16_t)(TELA_W - M - 92), (int16_t)topo, 92, (int16_t)fh };
    tft.fillRoundRect(hRetomar.x, hRetomar.y, hRetomar.w, hRetomar.h, 14, C_SURF);
    tft.fillRoundRect(hRetomar.x, hRetomar.y + hRetomar.h - 5, hRetomar.w, 5, 3, C_CASING);
    icComboio(tft, hRetomar.x + 36, hRetomar.y + fh / 2, 34, C_SUN);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextDatum(middle_left);
    tft.setTextColor(C_INK);
    tft.drawString(g_ses.gNome, hRetomar.x + 68, hRetomar.y + fh / 2);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.setTextDatum(middle_right);
    tft.setTextColor(C_SUN);
    tft.drawString("RETOMAR", hRetomar.x + hRetomar.w - 20, hRetomar.y + fh / 2);
    // o X ao lado NAO e enfeite: sem ele, uma sessao velha ficaria oferecida
    // para sempre e o unico jeito de recusar seria entrar nela para poder sair.
    botaoIc(tft, hDescarta, 2, C_SURF, C_INK3);
    topo += fh + 14;
  }
  const int bh   = base - topo - 16;
  hCriar  = { M, (int16_t)topo, (int16_t)bw, (int16_t)bh };
  hEntrar = { (int16_t)(TELA_W - M - bw), (int16_t)topo, (int16_t)bw, (int16_t)bh };

  // CARTAO COM EIXO: icone grande no centro do terco superior, verbo forte, e
  // uma dica de quatro palavras em voz baixa. A versao anterior espremia um
  // icone de 46 px num canto e o verbo noutro - parecia formulario, nao acao.
  auto cartao = [&](const Ret& r, const char* verbo, const char* dica,
                    bool forte, bool aceso, bool lupa) {
    const uint16_t fundo  = forte ? C_SUN : C_SURF;
    const uint16_t frente = forte ? C_BG  : C_INK;
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 22, fundo);
    if (aceso) tft.fillRoundRect(r.x, r.y, r.w, 7, 3, C_CASING);
    else       tft.fillRoundRect(r.x, r.y + r.h - 7, r.w, 7, 3, C_CASING);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 22, forte ? C_CASING : C_LINE);
    // SEM traco lateral no cartao secundario: a listra de sol que marcava o
    // ENTRAR como tocavel foi lida pelo usuario como "a cor do CRIAR vazando
    // pro outro botao" (2026-08-25). O relevo do cartao ja diz que e tocavel.
    const int cx = r.x + r.w / 2;
    const int s  = (r.h > 300) ? 104 : 76;
    if (lupa) icLupa(tft, cx, r.y + (int)(r.h * 0.34f), s, frente);
    else      icMais(tft, cx, r.y + (int)(r.h * 0.34f), s, frente);
    tft.setFont(&fonts::FreeSansBold24pt7b);
    tft.setTextDatum(middle_center);
    tft.setTextColor(frente);
    tft.drawString(verbo, cx, r.y + r.h - 92);
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(forte ? C_CASING : C_INK2);
    tft.drawString(dica, cx, r.y + r.h - 44);
    tft.setFont(&fonts::Font0);
  };
  cartao(hCriar,  "CRIAR GRUPO", "voce guia o comboio",      true,  premido == 0, false);
  cartao(hEntrar, "ENTRAR",      "com o codigo de quem guia", false, premido == 1, true);

  // Rodape: quem e este aparelho. Sem o rotulo "ESTE APARELHO" - a cor mais o
  // nome ja dizem, e a linha economizada vira altura para os cartoes.
  tft.drawFastHLine(0, TELA_H - BARRA_H, TELA_W, C_LINE);
  const int ry = TELA_H - BARRA_H / 2;
  tft.fillRoundRect(M, ry - 13, 26, 26, 7, CORES_MAPA[g_cor]);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextDatum(middle_left);
  tft.setTextColor(C_INK);
  tft.drawString(g_nome, M + 40, ry);
  // Estado da rede a direita: o comboio com o numero, ou um traco se fora de grupo.
  icComboio(tft, TELA_W - M - 92, ry, 26, C_INK3);
  tft.setFont(&fonts::FreeSansBold18pt7b);
  tft.setTextDatum(middle_right);
  tft.setTextColor(C_INK3);
#if MTS_TEM_RADIO
  char nq[8]; snprintf(nq, sizeof(nq), "%d", mundoQuantos());
  tft.drawString(g_ses.ativa ? nq : "-", TELA_W - M, ry);
#else
  tft.drawString("-", TELA_W - M, ry);
#endif
  tft.setFont(&fonts::Font0);
}

#if MTS_TEM_WIFI
// Progresso da atualizacao pelo ar, em tela cheia. Fica no .ino porque e quem
// conhece o painel; o wifi_ota.h so chama o gancho.
static void desenhaOta(int pct)
{
  static int ultimo = -1;
  if (pct <= ultimo && pct != 0) return;
  ultimo = (pct >= 100) ? -1 : pct;
  if (pct == 0) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(middle_center);
    tft.setFont(&fonts::FreeSansBold24pt7b);
    tft.setTextColor(C_INK);
    tft.drawString("ATUALIZANDO PELO WIFI", TELA_W / 2, TELA_H / 2 - 80);
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(C_WARN);
    tft.drawString("nao desligue o aparelho", TELA_W / 2, TELA_H / 2 - 20);
    tft.drawRoundRect(TELA_W / 2 - 300, TELA_H / 2 + 40, 600, 44, 12, C_LINE);
  }
  int w = (int)(592L * pct / 100);
  if (w > 0) tft.fillRoundRect(TELA_W / 2 - 296, TELA_H / 2 + 44, w, 36, 8, C_SUN);
  tft.setFont(&fonts::Font0);
}

// ----------------------------------------------------------- tela do wifi
// Procura as redes do ar e deixa escolher UMA, com a senha digitada aqui mesmo
// (o teclado tem modo 123 por causa disto). A rede escolhida mora na NVS ate
// ser trocada ou esquecida - nenhum ssid vive no codigo, entao a mesma tela
// atualiza em casa, no hotspot do celular ou na casa de um amigo.
static void telaWifi()
{
  Ret rVolta = { M, (int16_t)(TELA_H - M - 76), 240, 76 };
  Ret rNovo  = { (int16_t)(TELA_W / 2 - 150), (int16_t)(TELA_H - M - 76), 300, 76 };
  Ret rEsq   = { (int16_t)(TELA_W - M - 320), (int16_t)(TELA_H - M - 76), 320, 76 };
  const int topo = CAB_H + 14, lh = 62, vao = 8;
  auto rRede = [&](int i) -> Ret {
    return Ret{ M, (int16_t)(topo + i * (lh + vao)), (int16_t)(TELA_W - 2 * M), (int16_t)lh };
  };

  while (true) {                        // cada volta do laco = uma varredura nova
    tft.fillScreen(C_BG);
    cabecalho(tft, "wifi", false);
    tft.setTextDatum(middle_center);
    tft.setFont(&fonts::FreeSans12pt7b);
    tft.setTextColor(C_INK2);
    tft.drawString("procurando redes...", TELA_W / 2, TELA_H / 2);
    tft.setFont(&fonts::Font0);

    // O aparelho tenta a rede salva a cada 30 s em segundo plano; varrer NO
    // MEIO de uma tentativa dessas devolve lista vazia (esp_hosted) - e a tela
    // dizia "nenhuma rede" com a rede no ar. Se nao esta conectado, derruba a
    // tentativa antes de varrer. Conectado pode varrer normal.
    WiFi.mode(WIFI_STA);
    if (!wifiConectado()) { WiFi.disconnect(); esperaServindo(200); }
    int n = WiFi.scanNetworks();        // bloqueia ~3 s; o aviso acima segura
    if (n <= 0) { esperaServindo(400); n = WiFi.scanNetworks(); }  // o C6 falha a 1a de vez em quando

    // Junta ate 20, joga fora SSID vazio (rede oculta) e repetido (mesh anuncia
    // o mesmo nome por varios APs), e ordena por forca. Sem isso o corte de 6
    // linhas podia deixar a SUA rede de fora mesmo forte.
    const int MAXR = 20;
    String nomes[MAXR]; int forca[MAXR]; bool trancada[MAXR];
    int u = 0;
    for (int i = 0; i < n && u < MAXR; i++) {
      String ss = WiFi.SSID(i);
      if (!ss.length()) continue;
      int j = 0; for (; j < u; j++) if (nomes[j] == ss) break;
      if (j < u) { if (WiFi.RSSI(i) > forca[j]) forca[j] = WiFi.RSSI(i); continue; }
      nomes[u]    = ss;
      forca[u]    = WiFi.RSSI(i);
      trancada[u] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      u++;
    }
    WiFi.scanDelete();
    for (int a = 1; a < u; a++)         // insercao: 20 itens nao pedem mais que isso
      for (int b = a; b > 0 && forca[b] > forca[b - 1]; b--) {
        String ts = nomes[b];  nomes[b] = nomes[b-1];  nomes[b-1] = ts;
        int    tf = forca[b];  forca[b] = forca[b-1];  forca[b-1] = tf;
        bool   tt = trancada[b]; trancada[b] = trancada[b-1]; trancada[b-1] = tt;
      }
    if (u > 6) u = 6;                   // 6 linhas cabem; ordenado, a 7a e fraca mesmo

    tft.fillScreen(C_BG);
    cabecalho(tft, "wifi", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
    if (u <= 0) {
      tft.setTextDatum(middle_center);
      tft.setFont(&fonts::FreeSans12pt7b);
      tft.setTextColor(C_INK3);
      tft.drawString("nenhuma rede encontrada", TELA_W / 2, TELA_H / 2 - 60);
      tft.drawString("toque PROCURAR DE NOVO", TELA_W / 2, TELA_H / 2 - 16);
      tft.setFont(&fonts::Font0);
    }
    for (int i = 0; i < u; i++) {
      Ret r = rRede(i);
      bool salva = g_wifiQuer && nomes[i] == g_wifiSsid;
      tft.fillRoundRect(r.x, r.y, r.w, r.h, 12, C_SURF);
      tft.fillRoundRect(r.x, r.y, 5, r.h, 2, salva ? C_OK : C_SUN);
      tft.setTextDatum(middle_left);
      tft.setFont(&fonts::FreeSansBold12pt7b);
      tft.setTextColor(C_INK);
      char nm[25]; strncpy(nm, nomes[i].c_str(), 24); nm[24] = 0;
      tft.drawString(nm, r.x + 24, r.y + r.h / 2);
      // barras de forca, 4 niveis - o mesmo desenho das barras de zoom
      int niv = forca[i] > -55 ? 4 : forca[i] > -67 ? 3 : forca[i] > -80 ? 2 : 1;
      for (int b = 0; b < 4; b++) {
        int hh = 8 + b * 6;
        tft.fillRect(r.x + r.w - 70 + b * 11, r.y + r.h / 2 + 14 - hh, 7, hh,
                     b < niv ? C_SUN : C_LINE);
      }
      if (salva || !trancada[i]) {
        tft.setFont(&fonts::FreeSans9pt7b);
        tft.setTextColor(salva ? C_OK : C_INK3);
        tft.setTextDatum(middle_right);
        tft.drawString(salva ? (wifiConectado() ? "conectada" : "salva") : "aberta",
                       r.x + r.w - 90, r.y + r.h / 2);
      }
    }
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    botao(tft, rNovo, "PROCURAR DE NOVO", "", C_INK2, false);
    if (g_wifiQuer) botao(tft, rEsq, "ESQUECER REDE", "", C_RED, false);
    tft.setFont(&fonts::Font0);

    bool refazer = false;
    while (!refazer) {
      int16_t x, y;
      if (!esperaToque(tft, x, y)) continue;
      if (dentro(rVolta, x, y)) return;
      if (dentro(rNovo, x, y)) { refazer = true; break; }
      if (g_wifiQuer && dentro(rEsq, x, y)) { wifiEsquece(); return; }
      for (int i = 0; i < u; i++) {
        if (!dentro(rRede(i), x, y)) continue;
        char pass[33] = "";     // o teclado guarda ate 32; cobre quase toda senha domestica
        if (trancada[i]) {
          char titulo[48];
          snprintf(titulo, sizeof(titulo), "senha de %s", nomes[i].c_str());
          // cancelou o teclado: volta para a LISTA, nao para a configuracao
          if (!tecladoTexto(tft, titulo, pass, sizeof(pass), true)) { refazer = true; break; }
        }
        wifiSalva(nomes[i].c_str(), pass);
        tft.fillScreen(C_BG);
        cabecalho(tft, "wifi", false);
        tft.setTextDatum(middle_center);
        // DUAS tentativas: o esp_hosted perde o primeiro begin() de vez em
        // quando, e isso aparecia como "senha errada" com a senha certa.
        bool ok = false;
        for (int tent = 1; tent <= 2 && !ok; tent++) {
          char msg[64];
          snprintf(msg, sizeof(msg), "conectando em %s  (%d/2)", nomes[i].c_str(), tent);
          tft.fillRect(0, TELA_H / 2 - 40, TELA_W, 100, C_BG);
          tft.setFont(&fonts::FreeSans12pt7b);
          tft.setTextColor(C_INK2);
          tft.drawString(msg, TELA_W / 2, TELA_H / 2);
          WiFi.disconnect();
          esperaServindo(200);
          WiFi.begin(g_wifiSsid, g_wifiPass);
          uint32_t t0 = millis();
          while (!(ok = wifiConectado()) && millis() - t0 < 12000) esperaServindo(200);
        }
        tft.setFont(&fonts::FreeSansBold18pt7b);
        tft.fillRect(0, TELA_H / 2 - 40, TELA_W, 100, C_BG);
        if (ok) {
          char okm[64];
          snprintf(okm, sizeof(okm), "conectado - IP %s", WiFi.localIP().toString().c_str());
          tft.setTextColor(C_OK);
          tft.drawString(okm, TELA_W / 2, TELA_H / 2);
        } else {
          tft.setTextColor(C_RED);
          tft.drawString("nao conectou", TELA_W / 2, TELA_H / 2 - 16);
          tft.setFont(&fonts::FreeSans12pt7b);
          tft.setTextColor(C_INK3);
          tft.drawString("senha errada ou sinal fraco - toque a rede e tente de novo",
                         TELA_W / 2, TELA_H / 2 + 36);
        }
        tft.setFont(&fonts::Font0);
        esperaServindo(2400);
        if (ok) return;
        refazer = true;         // falhou: volta para a lista, sem sair da tela
        break;
      }
    }
  }
}
#endif

// ---------------------------------------------------------- configuracao
static void telaConfig()
{
  // QUATRO LINHAS dividindo o espaco que sobra - nunca posicao cravada (a
  // versao com y fixo sobrepunha linhas na tela de 600). O vao de 12 e o que
  // mantem cada linha nos 76 px do alvo de toque com luva na tela de 600.
  const int lw    = TELA_W - 2 * M;
  const int topo  = CAB_H + 24;
  const int base  = TELA_H - 48 - 76 - 24;     // acima do botao VOLTAR
  const int vao   = 12;
  const int lh    = (base - topo - 3 * vao) / 4;
  auto linha = [&](int i) -> Ret {
    return Ret{ M, (int16_t)(topo + i * (lh + vao)), (int16_t)lw, (int16_t)lh };
  };
  Ret rNome  = linha(0);
  Ret rCor   = linha(1);
  Ret rSlot  = linha(2);
  // A quarta linha era o MODO DIA/NOITE - saiu daqui porque ele ja mora no
  // botao redondo do proprio mapa (onde a troca acontece de verdade, a noite
  // chegando na trilha). O lugar virou o WIFI, que nao tem outro lar.
  Ret rWifi  = linha(3);
  Ret rVolta = { M, (int16_t)(TELA_H - 48 - 76), 280, 76 };

  // O texto do slot avisa o que importa: dois aparelhos no MESMO slot
  // transmitem um por cima do outro e nenhum ve o outro.
  auto pintaSlot = [&]() {
    char v[24];
    snprintf(v, sizeof(v), "slot %d de 0..%d", g_meuSlot, TDMA_SLOTS - 1);
    linhaCfg(tft, rSlot, "SLOT NA REDE (um por carro)", v, "tocar para trocar >", true);
  };

  // desenho completo, UMA vez. Depois so as linhas que mudam - redesenhar a tela
  // inteira a cada toque e o que fazia isto parecer transicao de slide.
  auto desenhaTudo = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "configuracao", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
    linhaCfg(tft, rNome, "NOME DESTE APARELHO", g_nome, "tocar para mudar >", true);
    linhaCfg(tft, rCor,  "COR NO MAPA", CORES_NOME[g_cor], "tocar para mudar >", true, CORES_MAPA[g_cor]);
    pintaSlot();
#if MTS_TEM_WIFI
    {
      char st[64];
      const char* v;
      if (!g_wifiQuer) v = "nenhuma rede salva";
      else { snprintf(st, sizeof(st), "%s", g_wifiSsid); v = st; }
      linhaCfg(tft, rWifi, "WIFI (atualizar pelo ar)", v,
               wifiConectado() ? "conectado >" : "tocar para escolher >", true);
    }
#else
    linhaCfg(tft, rWifi, g_tema ? "MODO NOITE" : "MODO DIA",
             g_tema ? "fundo escuro, feicoes claras" : "fundo claro, feicoes escuras",
             "tocar para trocar >", true);
#endif
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    tft.setTextDatum(top_right);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString("gravado no aparelho", tft.width() - M, TELA_H - 40);
    tft.setFont(&fonts::Font0);
  };
  desenhaTudo();

  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return;

    if (dentro(rNome, x, y)) {
      char tmp[sizeof(g_nome)];
      strncpy(tmp, g_nome, sizeof(tmp));
      bool ok = tecladoTexto(tft, "nome do aparelho", tmp, sizeof(tmp));
      if (ok) {
        if (!strlen(tmp)) strcpy(tmp, "Carro");
        strncpy(g_nome, tmp, sizeof(g_nome) - 1);
        g_nome[sizeof(g_nome) - 1] = 0;
        salvaCfg();
        Serial.printf("nome: %s\n", g_nome);
      }
      desenhaTudo();          // volta do teclado: aqui a tela inteira mudou mesmo
      continue;
    }
    if (dentro(rCor, x, y)) {
      g_cor = (g_cor + 1) % N_CORES;
      salvaCfg();
      // SO a linha da cor. Direto na tela, sem sprite de tela cheia.
      linhaCfg(tft, rCor, "COR NO MAPA", CORES_NOME[g_cor], "tocar para mudar >", true, CORES_MAPA[g_cor]);
      continue;
    }
    if (dentro(rSlot, x, y)) {
      uint8_t novo = (uint8_t)((g_meuSlot + 1) % TDMA_SLOTS);
#if MTS_TEM_RADIO
      radioTrocaSlot(novo);          // aplica no ar, sem reiniciar
#else
      g_meuSlot = novo;
      g_carros[0].slot = novo;
#endif
      Preferences p; p.begin("grupo", false); p.putUChar("slot", novo); p.end();
      pintaSlot();
      continue;
    }
    if (dentro(rWifi, x, y)) {
#if MTS_TEM_WIFI
      telaWifi();
      desenhaTudo();
#else
      trocaTema(tft, g_tema ? 0 : 1, desenhaTudo);
      salvaCfg();
#endif
      continue;
    }
  }
}

// Fica na trilha ate o usuario sair. So SAIR DA TRILHA encerra a sessao.
static void rodaTrilha() {
  mundoInicia(g_nome, g_cor, g_ses.lider);
#if !MTS_TEM_RADIO
  // So no simulador: com radio os carros ja estao no g_carros (vieram do ar), e
  // recria-los aqui pelo nome fabricava copias com slot 0 ate o TTL limpar.
  for (int i = 1; i < g_nMembros; i++) mundoEntra(g_membros[i].nome, g_membros[i].cor);
#endif
  if (telaTrilha(tft, g_ses, g_membros, g_nMembros)) {
    sessaoEncerra(g_ses);
#if MTS_TEM_RADIO
    // Sai do grupo = sai da sala. Sem isto o aparelho continuava transmitindo
    // com o codigo do grupo antigo e aparecia no mapa de quem ficou.
    radioTrocaSala("SEMSALA");
#endif
    mundoLimpaOutros();
    Serial.println("saiu da trilha");
  }
}

// ---------------------------------------------------------------------------
// Tudo que precisa acontecer enquanto o aparelho espera o dedo. Pendurado no
// esperaToque() do ui.h, roda a cada 8 ms em QUALQUER tela - inclusive no
// teclado, na sala de espera e na configuracao, onde antes o no ficava mudo.
static void servicoDeFundo()
{
#if MTS_TEM_GPS
  gpsAtualiza();
#endif
#if MTS_TEM_RADIO
  radioAtualiza();
#endif
#if MTS_TEM_WIFI
  wifiAtualiza(g_nome);
#endif
}

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) delay(10);
  Serial.printf("\n=== MTS - Minhoca Trail System (fw v%d) ===\n", MTS_VERSAO);

#if MTS_TEM_GPS
  // ANTES do painel, de proposito. Subir o painel e montar o cartao leva alguns
  // segundos, e sao segundos em que o GPS ja pode estar buscando satelite em vez
  // de esperar a interface ficar pronta. A primeira fixacao e o gargalo da
  // experiencia de ligar o aparelho - tudo que a adianta vale.
  gpsInicia();
  Serial.printf("gps: Serial1 a %d bps (RX=%d TX=%d PPS=%d)\n",
                GPS_BAUD, PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_PPS);
#endif

  carregaCfg();
  g_hookServico = servicoDeFundo;
  mundoIdentidade(g_nome, g_cor);   // o roster ja sai certo desde o primeiro anuncio

#if MTS_TEM_WIFI
  // WiFi sobe cedo e sem bloquear: se houver rede salva por perto, o OTA fica
  // no ar sozinho ("chegou na garagem, da para atualizar"). O progresso na
  // tela vem pelo gancho, porque o wifi_ota.h nao conhece o painel.
  g_otaDesenha = desenhaOta;
  wifiInicia();
#endif

#if MTS_TEM_RADIO
  // O radio sobe aqui, e nao ao entrar na trilha: um no que so liga o radio
  // quando o dono abre o mapa nao aparece para os outros enquanto ele estiver na
  // tela inicial - e o grupo veria um carro sumir sem motivo. Estar no ar e
  // estado do aparelho, nao da tela.
  //
  // ATENCAO: daqui em diante ele TRANSMITE em 29 dBm quando chegar o slot dele.
  // Nao energize sem antena no IPX - o PA nao sobrevive a isso.
  {
    Sessao s0; sessaoCarrega(s0);
    radioInicia((uint8_t)g_meuSlot, s0.ativa ? s0.gCod : "SEMSALA");
    radioAncoraSeSouZero();
  }
#endif
  if (!tft.init()) { Serial.println("ERRO: painel nao inicializou."); while (true) delay(1000); }
#if MTS_PLACA == 5
  tft.setRotation(MTS_TELA_180 ? 3 : 1);   // painel retrato, interface paisagem
#endif
  // Na 7B o giro de 180 da caixinha NAO e feito aqui: setRotation() nao
  // funciona no caminho DSI dela. Ele mora no offset_rotation do painel,
  // dentro de LGFX_P4_LCD7.h - ver o comentario la.
  // A partir daqui todo o layout usa TELA_W/TELA_H - nunca numero cravado.
  telaTamanho(tft.width(), tft.height());

  Serial.printf("painel %s %dx%d | psram livre %u | nome=%s\n",
                tft.panelName, tft.width(), tft.height(),
                (unsigned)ESP.getFreePsram(), g_nome);

  splashMostrar(tft, 2400);   // 1 s a mais, a pedido do usuario

  // ---- cartao do mapa. Tudo aqui e OPCIONAL: qualquer falha so tira o fundo,
  // e o aparelho segue mostrando trajeto, carros e alerta. Um GPS de trilha que
  // nao liga porque o cartao soltou no tranco seria pior que um sem mapa.
#if MTS_TEM_SD
  // O SLOT E DIFERENTE NAS DUAS PLACAS, e o modo de falha e o mesmo nas duas
  // (timeout 0x107, "o cartao nao existe"), entao errar aqui e caro de achar.
  //   5" ... pinos padrao do SDMMC, alimentacao por GPIO em nivel alto
  //   7" ... pinos proprios, alimentacao por LDO INTERNO canal 4
#if MTS_PLACA == 7
  SD_MMC.setPins(43, 44, 39, 40, 41, 42);
  #ifdef SOC_SDMMC_IO_POWER_EXTERNAL
    SD_MMC.setPowerChannel(4);
  #endif
#else
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);
  delay(120);
#endif
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("cartao: nao montou - seguindo sem fundo de mapa");
  } else if (!fundoAbre(g_fundo)) {
    Serial.printf("cartao: %s - seguindo sem fundo de mapa\n", g_fundo.erro);
  } else {
    Serial.printf("mapa: %s%s | leitura %s | psram livre %u\n",
                  g_fundo.temVec ? "vetor " : "", g_fundo.temDem ? "relevo" : "",
                  g_sd.multi ? "multi-setor" : "setor a setor (lenta)",
                  (unsigned)ESP.getFreePsram());
    for (int i = 0; i < g_fundo.vec.nLevels; i++)
      Serial.printf("  nivel %d: z%u, simplificado a %u m, grade %ux%u\n", i,
                    g_fundo.vec.levels[i].zoom, g_fundo.vec.levels[i].tolMetros,
                    g_fundo.vec.levels[i].w, g_fundo.vec.levels[i].h);
  }
#endif

  // RELIGOU NO MEIO DA TRILHA: a sessao e lembrada, mas o aparelho NAO entra nela
  // sozinho.
  //
  // Ele entrava - "volta direto para ela, sem passar pelo menu" - e isso criou um
  // beco sem saida na bancada: a tela de trilha e um laco bloqueante, entao o
  // loop() nunca rodava, nenhum diagnostico saia pelo serial, e o aparelho ficava
  // presa numa sessao velha que o dono nem lembrava de ter aberto. De fora parecia
  // travado. Para sair so regravando.
  //
  // Agora a sessao vira uma OFERTA na tela inicial. Quem religou no meio da trilha
  // toca uma vez e volta; quem esqueceu um grupo antigo simplesmente nao toca.
  sessaoCarrega(g_ses);
  if (g_ses.ativa) {
    membrosReinicia(g_ses.lider);
    Serial.printf("sessao guardada: %s (%s) - oferecendo RETOMAR\n",
                  g_ses.gNome, g_ses.lider ? "lider" : "seguidor");
#if MTS_TEM_RADIO
    radioTrocaSala(g_ses.gCod);      // ja volta a ouvir o grupo certo
#endif
  }
  desenhaInicial();
}

void loop()
{
#if MTS_TEM_GPS
  // O GPS e consumido TAMBEM na tela inicial, e nao so dentro da trilha. Uma
  // primeira fixacao leva de 30 a 60 s; se so comecassemos a ler ao entrar no
  // mapa, o usuario esperaria esse tempo olhando para um mapa sem ele. Lendo
  // desde o boot, o fix chega pronto.
  gpsAtualiza();
  static uint32_t tGps = 0;
  if (millis() - tGps > 5000) {
    tGps = millis();
    // "sats" sao os USADOS; "vista" e o que a antena enxerga. Os dois juntos
    // separam antena arrancada (vista=0) de ceu bloqueado (vista>0, snr baixo).
    Serial.printf("[gps] fix=%s sats=%u vista=%u snr=%u hdop=%.2f | %.6f, %.6f | "
                  "frases=%lu ruins=%lu | PPS=%s (%lu us) pulsos=%lu\n",
                  gpsFixValido() ? "SIM" : "nao", g_gpsSats,
                  g_gpsVista, g_gpsSnrAnt, g_gpsHdop,
                  g_gpsLat, g_gpsLon,
                  (unsigned long)g_gpsFrases, (unsigned long)g_gpsRuins,
                  gpsPpsValido() ? "1Hz" : "-", (unsigned long)g_ppsIntervaloUs,
                  (unsigned long)g_ppsCnt);
  }
#endif

#if MTS_TEM_RADIO
  // Servir o radio TAMBEM na tela inicial. Dentro da trilha quem chama e o
  // mundoAtualiza(); aqui fora, ninguem chamaria - e o no ficaria mudo e surdo
  // enquanto o dono nao abrisse o mapa.
  radioAtualiza();

  // Trocar de slot pelo serial: 's' seguido do numero. Existe porque dois carros
  // no mesmo slot transmitem por cima um do outro, e ate haver provisionamento
  // por roster nao ha outra forma de separar dois aparelhos recem-gravados - que
  // saem de fabrica os dois no slot 0.
  while (Serial.available()) {
    static bool esperandoNum = false;
    static bool esperandoPwr = false;
    static int  pwrAcc = -1;
    char c = Serial.read();
    // 'p' + numero = potencia de TX (0..22), aplicada na hora e salva na NVS
    if (c == 'p' || c == 'P') { esperandoPwr = true; pwrAcc = -1; esperandoNum = false; continue; }
    if (esperandoPwr) {
      if (c >= '0' && c <= '9' && pwrAcc < 3) {
        pwrAcc = (pwrAcc < 0 ? 0 : pwrAcc * 10) + (c - '0');
        continue;
      }
      esperandoPwr = false;
      if (pwrAcc >= 0) {
        radioTrocaPotencia((int8_t)pwrAcc);
        Preferences pp; pp.begin("grupo", false); pp.putChar("pwr", g_rfPwr); pp.end();
      }
      // o caractere que terminou o numero segue para os comandos abaixo
    }
    if (c == 's' || c == 'S') { esperandoNum = true; continue; }
    // BANCADA: 'x' sai de qualquer sala sem tocar na tela - descarta a sessao
    // guardada e volta para a sala neutra. Existe para testar duas telas pelo
    // serial: com sessoes antigas diferentes, cada uma escuta uma sala e os
    // pacotes da outra viram so "outraSala" - o 'x' nas duas as poe juntas.
    if (c == 'x' || c == 'X') {
      esperandoNum = false;
      sessaoEncerra(g_ses);
#if MTS_TEM_RADIO
      radioTrocaSala("SEMSALA");
#endif
      mundoLimpaOutros();
      Serial.println("sessao descartada pelo serial - sala neutra (SEMSALA)");
      desenhaInicial();
      continue;
    }
    // BANCADA: 'a' liga/desliga o MEU alerta sem tocar na tela, para provar o
    // ciclo completo pelo radio (a flag viaja no pacote de posicao).
    if (c == 'a' || c == 'A') {
      esperandoNum = false;
      g_carros[0].alerta = !g_carros[0].alerta;
      Serial.printf("alerta %s (vai no proximo pacote)\n",
                    g_carros[0].alerta ? "LIGADO" : "desligado");
      continue;
    }
    if (esperandoNum && c >= '0' && c <= '9') {
      esperandoNum = false;
      uint8_t novo = c - '0';
      if (novo < TDMA_SLOTS) {
        Preferences p; p.begin("grupo", false); p.putUChar("slot", novo); p.end();
        Serial.printf("slot = %u (salvo). Reiniciando para valer...\n", novo);
        delay(200);
        ESP.restart();     // o slot entra no tdmaInit: mais honesto reiniciar
      }
    }
    esperandoNum = false;
  }

  // As pastilhas contam o estado do aparelho, e o estado muda sozinho: o GPS
  // fixa, a sincronia entra, o holdover aparece. Repintar so no toque deixaria a
  // tela mentindo ate alguem encostar nela. So a faixa das pastilhas e repintada,
  // uma vez por segundo - a tela inteira custaria caro e piscaria.
  static uint32_t tPast = 0;
  if (millis() - tPast > 1000) { tPast = millis(); pintaPastilhas(); }
#if MTS_TEM_WIFI
  // So aqui, na tela inicial - na trilha o loop() nao roda, entao a atualizacao
  // pela internet nunca acontece em movimento.
  otaNuvemChecar();
#endif

  static uint32_t tRadio = 0;
  if (millis() - tRadio > 5000) {
    tRadio = millis();
    const char* sinc = g_tdma.sync == TDMA_SYNC_PPS    ? "PPS"
                     : g_tdma.sync == TDMA_SYNC_BEACON ? "beacon" : "-";
    Serial.printf("[radio] slot=%u sinc=%s%s | tx=%lu perdi=%lu preso=%lu | rx=%lu ruim=%lu outraSala=%lu conflito=%lu | rssi=%.0f snr=%.1f\n",
                  g_tdma.nodeId, sinc, g_tdma.holdover ? " HOLDOVER" : "",
                  (unsigned long)g_tdma.txCount, (unsigned long)g_tdma.missedTx,
                  (unsigned long)g_txPreso,
                  (unsigned long)g_rxOk, (unsigned long)g_rxRuim,
                  (unsigned long)g_rxForaDaSala, (unsigned long)g_slotConflito,
                  g_rxRssi, g_rxSnr);
  }
#endif

  int16_t x, y;
  if (!esperaToque(tft, x, y, 400)) return;

  if (dentro(hEng, x, y)) { telaConfig(); desenhaInicial(); return; }

  if (g_ses.ativa && dentro(hRetomar, x, y)) {
    membrosReinicia(g_ses.lider);
    rodaTrilha();
    desenhaInicial();
    return;
  }
  if (g_ses.ativa && dentro(hDescarta, x, y)) {
    sessaoEncerra(g_ses);
#if MTS_TEM_RADIO
    radioTrocaSala("SEMSALA");     // descartou o grupo: para de falar nele
#endif
    mundoLimpaOutros();
    Serial.println("sessao descartada");
    desenhaInicial();
    return;
  }

  // O retorno ao toque redesenha o CARTAO no estado premido (o desenhaInicial
  // ja sabe fazer isso pelo parametro) - pintar um botao generico por cima do
  // cartao novo criava um remendo visual de meio segundo.
  if (dentro(hCriar, x, y)) {
    desenhaInicial(0);
    char gn[GRUPO_NOME_MAX] = "", gc[8] = "";
    if (telaCriarGrupo(tft, g_nome, gn, gc)) {
      // sala de espera: o codigo fica a vista e a lista cresce conforme chegam
      membrosReinicia(true);
#if MTS_TEM_RADIO
      // Antes da sala de espera, nao depois: quem chegar enquanto o lider olha a
      // tela ja tem de ser contado, e para isso o filtro de sala precisa estar
      // valendo desde o primeiro quadro.
      radioTrocaSala(gc);
      mundoLimpaOutros();     // a sala nova nasce vazia, sem os carros da antiga
#endif
      if (telaSalaEspera(tft, gn, gc, g_membros, g_nMembros)) {
        g_ses.ativa = true; g_ses.lider = true;
        strncpy(g_ses.gNome, gn, sizeof(g_ses.gNome) - 1);
        strncpy(g_ses.gCod,  gc, sizeof(g_ses.gCod) - 1);
        sessaoSalva(g_ses);
        Serial.printf("trilha aberta: %s | codigo %s | %d no grupo\n", gn, gc, g_nMembros);
        rodaTrilha();
      }
    }
    desenhaInicial();
  } else if (dentro(hEntrar, x, y)) {
    desenhaInicial(1);
    char cod[8] = {0};
    if (telaEntrarGrupo(tft, cod)) {
      membrosReinicia(false);
      g_ses.ativa = true; g_ses.lider = false;
      // O nome do grupo nao viaja no ar - so o hash do codigo. Entao o seguidor
      // mostra o codigo, que e o que ele realmente sabe. Inventar um nome aqui
      // seria escrever na tela algo que ninguem confirmou.
      snprintf(g_ses.gNome, sizeof(g_ses.gNome), "Grupo %s", cod);
      strncpy(g_ses.gCod, cod, sizeof(g_ses.gCod) - 1);
      sessaoSalva(g_ses);
#if MTS_TEM_RADIO
      radioTrocaSala(cod);        // daqui em diante so escuto quem tem este codigo
      mundoLimpaOutros();         // sem os carros ouvidos na sala anterior
#endif
      Serial.printf("entrou no grupo de codigo %s\n", cod);
      rodaTrilha();
    }
    desenhaInicial();
  }
}
