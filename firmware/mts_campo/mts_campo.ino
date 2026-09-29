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
#include "registro.h"   // diario de bordo no cartao (novo no mts_campo)
#include "caixa.h"      // caixa-preta: o que estava em curso quando o watchdog matou
#include <Preferences.h>
#include "esp_system.h"   // esp_reset_reason()
#include "esp_rom_sys.h"  // esp_rom_get_reset_reason(): o codigo CRU do hardware (0x10 = LP WDT...)
#include "esp_random.h"   // epoca do trajeto (rota_core.h)
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
  // pode ser capado pelo serial ('p' + numero) ou pela tela de diagnostico.
  //
  // MIGRACAO UNICA v100 -> v101 (chave "pwrv"). Ate a v100 o valor gravado aqui
  // podia ter sido escrito pelo recuo automatico ou pelo resgate de boot, sem o
  // usuario saber - e foi: as duas telas do campo de 09-28 estavam em 6 quando
  // ninguem tinha pedido 6. Como nao ha como distinguir "6 porque o dono quis"
  // de "6 porque o firmware baixou", a PRIMEIRA vez que este firmware sobe ele
  // volta a potencia para o RF_PWR do hardware.h, grava a marca de versao e
  // registra no serial e no diario. Daqui em diante so comando explicito muda.
  // NAO e "a potencia mudando sozinha": e um acerto unico, anunciado, para
  // limpar o que os mecanismos removidos deixaram.
  g_rfPwrConfig = prefs.getChar("pwr", RF_PWR);
  if (g_rfPwrConfig < -9 || g_rfPwrConfig > 22) g_rfPwrConfig = RF_PWR;
  const uint8_t pwrVersao = prefs.getUChar("pwrv", 0);
#endif
  prefs.end();
#if MTS_TEM_RADIO
  if (pwrVersao < 101) {
    Serial.printf("radio: NVS de firmware anterior (pwr %d gravado, possivelmente pelo recuo "
                  "automatico que nao existe mais) - voltando para o padrao %d dBm, UMA vez\n",
                  (int)g_rfPwrConfig, (int)RF_PWR);
    g_rfPwrConfig = RF_PWR;
    Preferences pw; pw.begin("grupo", false);
    pw.putChar("pwr", g_rfPwrConfig); pw.putUChar("pwrv", 101); pw.end();
  }
  g_rfPwr = g_rfPwrConfig;
  // O RESGATE DO LACO DE RESET NAO EXISTE MAIS. Ate a v100 um reset violento
  // descia 4 dB e gravava na NVS aqui. A regra agora esta no hardware.h ("A
  // POTENCIA NUNCA MUDA SOZINHA") e o que sobrou dele - o teto em RAM apos tres
  // reinicios violentos seguidos - e decidido no setup(), depois de ler a
  // caixa-preta (caixa.h).
#endif
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
static Ret hCriar, hEntrar, hEng, hRetomar, hDescarta, hDiag;

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
  // A propria faixa de pastilhas e o alvo de toque do diagnostico: ela ja diz o
  // estado de GPS e radio, entao tocar nela para ver o detalhe e o gesto que a
  // pessoa ja tenta. Um botao a mais na tela inicial custaria espaco dos cartoes.
  hDiag = { (int16_t)(x0 - 4), (int16_t)(py - 2), (int16_t)(TELA_W - x0 - 96), 56 };
  // a faixa do modo seguro (ui.h) fica logo abaixo do cabecalho, repintada
  // junto com as pastilhas para nunca sumir
  avisoDesenha(tft, TELA_W, CAB_H + 4);

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
// A telemetria de http://<ip>/status - o mesmo conteudo dos prints [gps] e
// [radio] do serial, para inspecionar a tela SEM cabo (o gancho vive no
// wifi_ota.h; a montagem fica aqui porque e o .ino quem conhece gps e radio).
static void statusTexto(char* fora, size_t n)
{
  int usado = snprintf(fora, n, "%s | fw v%d | ar %lu s\n",
                       g_nome, MTS_VERSAO, (unsigned long)(millis() / 1000));
#if MTS_TEM_GPS
  usado += snprintf(fora + usado, n - usado,
                "[gps] fix=%s sats=%u vista=%u snr=%u hdop=%.2f | %.6f, %.6f | "
                "frases=%lu ruins=%lu | PPS=%s pulsos=%lu\n",
                gpsFixValido() ? "SIM" : "nao", g_gpsSats,
                g_gpsVista, g_gpsSnrAnt, g_gpsHdop, g_gpsLat, g_gpsLon,
                (unsigned long)g_gpsFrases, (unsigned long)g_gpsRuins,
                gpsPpsValido() ? "1Hz" : "-", (unsigned long)g_ppsCnt);
#endif
#if MTS_TEM_RADIO
  const char* sinc = g_tdma.sync == TDMA_SYNC_PPS    ? "PPS"
                   : g_tdma.sync == TDMA_SYNC_BEACON ? "beacon" : "-";
  // DE QUEM vem o meu tempo. E a pergunta que o log antigo nao respondia: com
  // 'sinc=beacon' ainda faltava saber quem era o beacon, e sem isso nao da para
  // ver que a rede se partiu em duas ancoras.
  char fonte[20];
  if (!g_tdma.haveAnchor)     strcpy(fonte, "ninguem");
  else if (g_tdma.isAnchor)   strcpy(fonte, "EU");
  else snprintf(fonte, sizeof(fonte), "slot %u%s", g_tdma.anchorSrc,
                g_tdma.anchorSrcPps ? " UTC" : "");
  usado += snprintf(fora + usado, n - usado,
                "[radio] slot=%u sinc=%s%s fonte=%s | pwr=%d dBm (~%d na antena) | "
                "tx=%lu perdi=%lu preso=%lu | "
                "rx=%lu ruim=%lu outraSala=%lu conflito=%lu | rssi=%.0f snr=%.1f\n",
                g_tdma.nodeId, sinc, g_tdma.holdover ? " HOLDOVER" : "", fonte,
                radioPotencia(), radioPotenciaAnt(),
                (unsigned long)g_tdma.txCount, (unsigned long)g_tdma.missedTx,
                (unsigned long)g_txPreso,
                (unsigned long)g_rxOk, (unsigned long)g_rxRuim,
                (unsigned long)g_rxForaDaSala, (unsigned long)g_slotConflito,
                g_rxRssi, g_rxSnr);
#endif
  (void)usado;
}

// A MESMA informacao, compacta e ja segura para query string. Vai no otaEco().
//
// Os dois primeiros campos sao os que importam numa placa que nao transmite, e
// sao justamente os que so existiam como texto no serial:
//   fios=0  -> o radioChecaFios() viu BUSY e DIO1 trocados no chicote. O begin()
//              passa, o RX parece bom, e NENHUMA transmissao completa.
//   bg=<n>  -> codigo do begin() do RadioLib. 0 = subiu. -2 = o chip nao
//              respondeu ao SPI (alimentacao ou fio de NSS/MISO). -707 = TCXO.
//
// O NOME VAI JUNTO, e isso nao e detalhe: sem ele o relatorio chega como
// "ECO ?" e nao da para saber DE QUAL tela se esta falando. Aconteceu em
// 2026-08-29 - havia uma placa com defeito no relatorio e ninguem, nem o dono
// nem eu, conseguia dizer qual das tres era. Um relatorio anonimo de uma frota
// e quase inutil. Vai sanitizado (so letras e numeros) para nao precisar de
// escape de URL - o resto vira '_'.
static void diagQuery(char* fora, size_t n)
{
  char nm[17];
  size_t k = 0;
  for (const char* p = g_nome; *p && k < sizeof(nm) - 1; p++) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    nm[k++] = ok ? c : '_';
  }
  nm[k] = 0;
  if (!k) strcpy(nm, "sem_nome");

  int u = snprintf(fora, n, "n=%s&v=%d&up=%lu", nm, MTS_VERSAO,
                   (unsigned long)(millis() / 1000));
#if MTS_TEM_RADIO
  u += snprintf(fora + u, n - u,
                "&slot=%u&fios=%d&bg=%d&ok=%d&pwr=%d&sinc=%d&fonte=%d&utc=%d"
                "&tx=%lu&perdi=%lu&preso=%lu&rx=%lu&ruim=%lu&sala=%lu&conf=%lu&rssi=%d",
                g_tdma.nodeId, radioFiosOk() ? 1 : 0, radioBeginSt(), g_loraOk ? 1 : 0,
                radioPotencia(), (int)g_tdma.sync,
                g_tdma.haveAnchor ? (g_tdma.isAnchor ? 99 : (int)g_tdma.anchorSrc) : -1,
                g_tdma.anchorSrcPps ? 1 : 0,
                (unsigned long)g_tdma.txCount, (unsigned long)g_tdma.missedTx,
                (unsigned long)g_txPreso, (unsigned long)g_rxOk, (unsigned long)g_rxRuim,
                (unsigned long)g_rxForaDaSala, (unsigned long)g_slotConflito,
                (int)g_rxRssi);
#endif
#if MTS_TEM_GPS
  u += snprintf(fora + u, n - u, "&fix=%d&sats=%u&pps=%lu",
                gpsFixValido() ? 1 : 0, g_gpsSats, (unsigned long)g_ppsCnt);
#endif
  (void)u;
}

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

// ------------------------------------------------------------ diagnostico
// POR QUE ESTA TELA EXISTE. Em 2026-08-28 tres aparelhos foram para a trilha e
// dois nao se falaram. Nada na tela dizia o porque: slot, sincronia, potencia,
// pacotes transmitidos e RSSI so saiam pelo serial (precisa de PC) ou pelo
// /status (precisa de WiFi). Na trilha nao ha nem um nem outro, e o dia virou
// "nao funcionou" em vez de "nao funcionou POR ISSO".
//
// Regra desta tela: numeros crus, sem interpretacao bonita. Verde e o esperado,
// vermelho e o que explica a falha. Ela nao toca em nada - so olha.
static void telaDiag()
{
  const int GAPX = 12, GAPY = 10;
  const int topo = CAB_H + 16;
  const int base = TELA_H - 48 - 76;               // acima do VOLTAR
  const int cw   = (TELA_W - 2 * M - GAPX) / 2;
  const int ch   = (base - topo - 24 - 4 * GAPY) / 5;
  Ret rVolta = { M, (int16_t)base, 280, 76 };
  // A celula da POTENCIA e tocavel. Nao e enfeite: esse numero mora na NVS,
  // sobrevive a regravacao, e ha placa nesta frota cujo serial nao chega a lugar
  // nenhum - sem um controle na tela, a unica forma de corrigi-lo era um cabo
  // que nem sempre existe. Aqui o defeito e o conserto ficam no mesmo lugar.
  Ret rPwr = { (int16_t)(M + cw + GAPX), (int16_t)topo, (int16_t)cw, (int16_t)ch };

  auto celula = [&](int col, int lin, const char* rot, const char* val, uint16_t cor) {
    const int x = M + col * (cw + GAPX);
    const int y = topo + lin * (ch + GAPY);
    tft.fillRoundRect(x, y, cw, ch, 10, C_SURF);
    tft.setTextDatum(top_left);
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextColor(C_INK3);
    tft.drawString(rot, x + 14, y + 8);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    tft.setTextColor(cor);
    tft.drawString(val, x + 14, y + ch - 32);
    tft.setFont(&fonts::Font0);
  };

  auto pintaCelulas = [&]() {
    char v[48];
#if MTS_TEM_RADIO
    // O CODIGO DO ERRO TRADUZIDO. "FALHOU NO BOOT" nao ajuda ninguem com a tela
    // na mao no meio do mato: diz que quebrou, nao o que conferir. Cada codigo
    // do RadioLib aponta para um ponto FISICO diferente do chicote, e e essa a
    // informacao util. Custou um dia descobrir isso lendo log de servidor.
    const char* rRot; const char* rVal; uint16_t rCor;
    if (!g_loraOk) {
      rRot = "RADIO - NAO SUBIU"; rCor = C_RED;
      switch (radioBeginSt()) {
        // -2 = CHIP_NOT_FOUND: o SPI foi e nao voltou. Alimentacao do modulo ou
        //      um dos dois fios que fecham a conversa (NSS escolhe o chip, MISO
        //      traz a resposta).
        case -2:   rVal = "nao responde - ver 5V, NSS e MISO"; break;
        // -707 = SPI_CMD_FAILED, que neste modulo e quase sempre a tensao do TCXO
        case -707: rVal = "TCXO - conferir 2,2 V";             break;
        case 999:  rVal = "nem chegou a iniciar";              break;
        default:   rVal = "falhou ao iniciar";                 break;
      }
    } else if (!radioFiosOk()) {
      // O begin() passou e mesmo assim pode nao transmitir: dois pads vizinhos
      // trocados na solda. Aviso, nao erro - o radio esta de pe.
      rRot = "RADIO - FIACAO SUSPEITA"; rVal = "BUSY e DIO1 trocados?"; rCor = C_WARN;
    } else {
      rRot = "RADIO"; rVal = "no ar"; rCor = C_OK;
    }
    celula(0, 0, rRot, rVal, rCor);

    // A potencia vem da NVS, nao do firmware: sobrevive a regravacao. Uma das
    // telas ficou em 22 dBm enquanto as outras estavam em 6, e travava 2 de cada
    // 3 transmissoes - era o slot 0, e derrubou a rede inteira no campo. Por
    // isso este valor tem numero E veredito: verde e o que esta transmitindo.
    if (radioEmModoSeguro())
      snprintf(v, sizeof(v), "%d dBm SO NESTE BOOT (cfg %d)  >", radioPotencia(), radioPotenciaConfig());
    else
      snprintf(v, sizeof(v), "%d dBm  (~%d na antena)  >", radioPotencia(), radioPotenciaAnt());
    const bool travando = g_txPreso > 0 && g_tdma.txCount > 0 &&
                          g_txPreso * 2 > g_tdma.txCount;   // metade ou mais travou
    celula(1, 0, radioEmModoSeguro() ? "POTENCIA - MODO SEGURO (3 REINICIOS)"
                 : travando ? "POTENCIA - ALTA DEMAIS, TOCAR PARA BAIXAR" : "POTENCIA (tocar para trocar)",
           v, (travando || radioEmModoSeguro()) ? C_RED : C_OK);

    snprintf(v, sizeof(v), "%d  (de 0 a %d)", g_meuSlot, TDMA_SLOTS - 1);
    celula(0, 1, "MEU SLOT", v, C_INK);

    // A pergunta que decide tudo: de quem vem o meu tempo? Sem ancora o no NAO
    // TRANSMITE - nem um pacote - e ate esta tela existir isso era invisivel.
    uint16_t cs = C_OK;
    if (!g_tdma.haveAnchor)                 { strcpy(v, "NENHUMA - nao transmito"); cs = C_RED; }
    else if (g_tdma.sync == TDMA_SYNC_PPS)  { strcpy(v, "PPS do GPS"); }
    else if (g_tdma.isAnchor)               { strcpy(v, g_tdma.anchorSrcPps
                                                ? "eu sou a referencia (UTC)"
                                                : "eu sou a referencia"); }
    else snprintf(v, sizeof(v), "do slot %u%s", g_tdma.anchorSrc,
                  g_tdma.anchorSrcPps ? " (grade UTC)" : " (grade do grupo)");
    if (g_tdma.holdover && g_tdma.haveAnchor) { strncat(v, "  (holdover)", sizeof(v) - strlen(v) - 1); cs = C_WARN; }
    celula(1, 1, "SINCRONIA", v, cs);

    snprintf(v, sizeof(v), "%lu enviados, %lu perdidos",
             (unsigned long)g_tdma.txCount, (unsigned long)g_tdma.missedTx);
    celula(0, 2, "TRANSMITI", v, g_tdma.txCount ? C_OK : C_RED);

    snprintf(v, sizeof(v), "%lu bons, %lu com erro",
             (unsigned long)g_rxOk, (unsigned long)g_rxRuim);
    celula(1, 2, "RECEBI", v, g_rxOk ? C_OK : C_WARN);

    if (g_rxOk) snprintf(v, sizeof(v), "%.0f dBm   SNR %.1f", g_rxRssi, g_rxSnr);
    else        strcpy(v, "nunca ouvi ninguem");
    celula(0, 3, "SINAL DO ULTIMO PACOTE", v,
           !g_rxOk ? C_WARN : (g_rxRssi > -110 ? C_OK : C_WARN));

    // TX preso subindo junto com tx = o E22 apagando no meio da transmissao por
    // queda do 5 V. E defeito de alimentacao, nao de radio - e a unica forma de
    // ver isso sem osciloscopio.
    snprintf(v, sizeof(v), "%lu travados, %lu de outro grupo",
             (unsigned long)g_txPreso, (unsigned long)g_rxForaDaSala);
    celula(1, 3, g_txPreso ? "TX PRESO - CONFIRA O 5V!" : "TX PRESO", v,
           g_txPreso ? C_RED : C_OK);
#endif
#if MTS_TEM_GPS
    if (gpsFixValido()) snprintf(v, sizeof(v), "fix com %u satelites", g_gpsSats);
    else                snprintf(v, sizeof(v), "SEM FIX (%u a vista)", g_gpsVista);
    celula(0, 4, "GPS", v, gpsFixValido() ? C_OK : C_WARN);

    // O PPS so pulsa DEPOIS do fix na maioria dos modulos: "sem pulso" com o GPS
    // ainda procurando e o normal, nao um fio solto.
    if (gpsPpsValido()) snprintf(v, sizeof(v), "1 Hz, %lu pulsos", (unsigned long)g_ppsCnt);
    else if (g_ppsCnt)  snprintf(v, sizeof(v), "irregular (%lu pulsos)", (unsigned long)g_ppsCnt);
    else                strcpy(v, "sem pulso (normal sem fix)");
    celula(1, 4, "PPS", v, gpsPpsValido() ? C_OK : C_INK2);
#endif
  };

  tft.fillScreen(C_BG);
  cabecalho(tft, "diagnostico", false);
  tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
  pintaCelulas();
  botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
  tft.setTextDatum(top_right);
  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(C_INK3);
  tft.drawString("atualiza sozinho, 1x por segundo", TELA_W - M, base + 28);
  tft.setFont(&fonts::Font0);

  uint32_t tAtu = millis();
  while (true) {
    int16_t x, y;
    // O timeout curto e o que faz a tela viver: sem toque nenhum ela ainda
    // repinta os numeros, que e justamente o que se quer olhar parado.
    if (esperaToque(tft, x, y, 250)) {
      if (dentro(rVolta, x, y)) return;
#if MTS_TEM_RADIO
      if (dentro(rPwr, x, y)) {
        // Degraus, nao 1 em 1 dBm: o que muda o comportamento deste hardware sao
        // saltos grandes, e 31 toques para atravessar a faixa seria hostil.
        // Comeca no MENOR: quem esta aqui quase sempre veio consertar um radio
        // que nao transmite, e o caminho para isso e para BAIXO.
        static const int8_t DEGRAUS[] = { 2, 6, 10, 14, 17, 22 };
        const int nD = sizeof(DEGRAUS) / sizeof(DEGRAUS[0]);
        int i = 0;
        while (i < nD && DEGRAUS[i] <= radioPotenciaConfig()) i++;
        if (i >= nD) i = 0;                      // passou do topo: volta ao piso
        // COMANDO EXPLICITO: o unico caminho (com o 'p' do serial) que muda a
        // potencia gravada. Tambem desliga o modo seguro, se estava ligado.
        radioTrocaPotencia(DEGRAUS[i]);
        avisoDefine("");
        caixaEtapa(CP_NVS);
        Preferences p; p.begin("grupo", false); p.putChar("pwr", DEGRAUS[i]); p.end();
        caixaEtapa(CP_LACO);
        // Zera os contadores: os travamentos velhos sao da potencia ANTERIOR, e
        // deixa-los somados esconderia se a nova esta funcionando.
        g_txPreso = 0; g_tdma.txCount = 0;
        pintaCelulas();
      }
#endif
      continue;
    }
    if (millis() - tAtu > 1000) { tAtu = millis(); pintaCelulas(); }
  }
}

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


// ===========================================================================
//  DIARIO DE BORDO - a unica coisa que o mts_campo acrescenta ao mts_p4
// ===========================================================================
//
// POR QUE EXISTE: o teste de campo de 2026-09-10 voltou com "a conexao ficou bem
// instavel" e nenhum numero. Quatro causas produzem essa frase, e cada uma deixa
// um contador diferente - perdi (grade de tempo), conflito (slots separados),
// ruim (link marginal), preso (TX travado). Nenhuma foi registrada, porque o
// firmware daquele dia estava sem cartao e sem WiFi.
//
// O ESQUEMA DE CAMPOS NAO FOI INVENTADO: e o do diagQuery() logo acima, que ja
// era compacto e ja tinha sido pensado para caber numa URL. Um CSV derivado dele
// sai de graca e mantem as duas ferramentas comparaveis.
static Orcamento g_orc;
static uint32_t  g_regUltEstadoMs = 0;
static bool      g_regUltFix = false;
static CaixaPreta g_caixaAnterior;     // copia da caixa-preta do boot que morreu
static uint32_t  g_regUltPpsRun = 0;

// E = estado, 1 Hz. A fotografia do no, com posicao e hora em toda linha - e o
// que transforma o CSV na curva "distancia x RSSI x entrega" que o orcamento de
// link do PLANO_FUNCIONAR.md pede e que nunca foi medida.
// As 21 primeiras colunas sao as da v100 (tools/campo/cruza_logs.py le por
// indice); as novas vao DEPOIS, para o script antigo continuar valendo.
static void logEstado(uint32_t agoraMs)
{
#if MTS_TEM_RADIO
  registroLinha("E,%lu,%lu,%.6f,%.6f,%d,%d,%s,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%u,%d,%lu,%lu,"
                "%lu,%.0f,%.0f,%lu,%lu,%d,%d,%lu,%lu,%d,%d,%lu,%lu,%lu",
    (unsigned long)agoraMs,
    (unsigned long)(g_gpsHoraOk ? g_gpsSecDia : 0),
    g_meuLat, g_meuLon,
    (int)g_meuSlot,
    (int)g_tdma.sync,
    g_tdma.isAnchor ? "EU" : "rede",
    (unsigned long)g_tdma.txCount,
    (unsigned long)g_tdma.missedTx,
    (unsigned long)g_txPreso,
    (unsigned long)g_rxOk,
    (unsigned long)g_rxRuim,
    (unsigned long)g_slotConflito,
    (int)g_rxRssi,
    (int)radioPotencia(),
    (int)(g_meuFix ? 1 : 0),
    (unsigned)g_gpsSats,
    (int)(g_tdma.holdover ? 1 : 0),
    (unsigned long)g_orc.atrasoMaxUs,
    (unsigned long)orcamentoLimiteUs(g_tdma),
    // v101
    (unsigned long)g_seqPerdTotal,
    g_ruidoUlt,
    g_ruidoN ? (g_ruidoSoma / g_ruidoN) : 0.0f,
    (unsigned long)g_txAirUltUs,
    (unsigned long)g_txAirMaxUs,
    (int)g_rotaN,
    (int)g_rotaC.prefixo,
    (unsigned long)g_catchups,
    (unsigned long)g_relogioVoltou,
    (int)radioPotenciaConfig(),
    (int)(radioEmModoSeguro() ? 1 : 0),
    (unsigned long)g_reg.flushes,
    (unsigned long)g_reg.piorFlushUs,
    (unsigned long)g_reg.descartadas);
  orcamentoRelatou(g_orc);
#endif
}

// G = GPS, so quando MUDA. Fix indo e voltando e a hipotese central da
// instabilidade de campo: sem fix o pulso para, o no cai em holdover, e quando o
// fix volta a ancora e sobrescrita de uma vez (tdma_core.h:432) com degrau de ate
// um frame inteiro. Registrar a transicao e o que permite provar ou descartar.
static void logGpsSeMudou(uint32_t agoraMs)
{
#if MTS_TEM_RADIO && MTS_TEM_GPS
  bool fix = g_meuFix;
  bool mudouFix = (fix != g_regUltFix);
  bool caiuPps  = (g_tdma.ppsRun < g_regUltPpsRun);
  if (mudouFix || caiuPps) {
    registroLinha("G,%lu,%s,%u,%u,%lu,%lu,%d", (unsigned long)agoraMs,
      mudouFix ? (fix ? "fix ON" : "fix OFF") : "pps reset",
      (unsigned)g_gpsSats, (unsigned)g_gpsVista,
      (unsigned long)g_tdma.ppsRun, (unsigned long)g_ppsCnt, (int)g_tdma.sync);
    g_regUltFix = fix;
  }
  g_regUltPpsRun = g_tdma.ppsRun;
#endif
}

// Chamado do servicoDeFundo, ou seja de TODA tela, a cada ~8 ms. Barato: quase
// sempre so compara dois millis e volta.
static void registroServico()
{
  uint32_t agora = millis();

  // Mede o atraso da propria volta. E o numero que o projeto nunca teve: hoje o
  // laco lento so aparece DEPOIS do estrago, como missedTx.
  orcamentoVolta(g_orc, micros(), orcamentoLimiteUs(g_tdma));

  // A caixa-preta fecha a conta uma vez por volta, com os contadores que dizem
  // "ate onde chegou" se o boot seguinte for por watchdog.
#if MTS_TEM_RADIO
  caixaVolta(g_tdma.txCount, g_rxOk, g_txPreso, g_reg.flushes,
             g_gpsHoraOk ? g_gpsSecDia : 0, radioPotencia(), (uint8_t)g_txTipo, g_loraTxDesdeMs);
#endif
  if (!registroAtivo()) return;

  logGpsSeMudou(agora);
  if (agora - g_regUltEstadoMs >= 1000) { g_regUltEstadoMs = agora; logEstado(agora); }

  // A GRAVACAO NO CARTAO SO ACONTECE COM ESPACO MORTO. Sem esta guarda o logger
  // seria o proximo HTTPS do eco: um flush que caia num garbage collection do
  // microSD segura centenas de ms, estoura o vigia de TX preso do radio.h e
  // conta 'preso' - o instrumento destruindo a medida.
  registroTick(g_orc, g_tdma, radioTxEmVoo(), agora);
}

// ---------------------------------------------------------------------------
// Tudo que precisa acontecer enquanto o aparelho espera o dedo. Pendurado no
// gancho do servico.h, roda a cada ~8 ms em QUALQUER tela - inclusive no
// teclado, na sala de espera, na configuracao E NO MAPA (v101: o laco da trilha
// chamava so o radio, e o diario ficava mudo justamente durante o teste).
static void servicoDeFundo()
{
#if MTS_TEM_GPS
  caixaEtapa(CP_GPS);
  gpsAtualiza();
#endif
#if MTS_TEM_RADIO
  radioAtualiza();
#endif
#if MTS_TEM_WIFI
  caixaEtapa(CP_WIFI);
  wifiAtualiza(g_nome);
#endif
  caixaEtapa(CP_LACO);
  registroServico();

  // O BOOT VINGOU: dois minutos no ar sem morrer encerram a serie de reinicios
  // violentos seguidos (caixa.h). Sem isto, tres travadas espalhadas por um dia
  // de trilha contariam como tres seguidas e ligariam o modo seguro a toa.
  static bool bootOk = false;
  if (!bootOk && millis() > 120000UL) { bootOk = true; caixaBootOk(); }
}

// "Cabe custoUs agora?" para o laco do mapa (servico.h / orcamento.h).
static bool servicoCabeAgora(uint32_t custoUs)
{
#if MTS_TEM_RADIO
  return orcamentoCabe(g_orc, g_tdma, radioTxEmVoo(), custoUs);
#else
  (void)custoUs; return true;
#endif
}

// O boot anterior morreu de morte violenta: conta a caixa-preta no serial e no
// diario. Fica numa funcao porque o diario so abre depois do cartao, e o serial
// quer saber antes de tudo - entao roda duas vezes, uma para cada destino.
static void relataCaixa(const CaixaPreta& c, esp_reset_reason_t motivo, bool paraLog)
{
  const int rom = (int)esp_rom_get_reset_reason(0);
  if (paraLog) {
    registroLinha("C,%lu,motivo %d,rom 0x%02X,seguidos %lu,etapa %s,antes %s,ha %lu ms,"
                  "ultimoMs %lu,tx %lu,rx %lu,presos %lu,flushes %lu,utc %lu,pwr %d,txTipo %c,txHa %lu ms",
                  (unsigned long)millis(), (int)motivo, rom, (unsigned long)c.resetsViolentos,
                  caixaNomeEtapa(c.etapa), caixaNomeEtapa(c.etapaAnt),
                  (unsigned long)(c.ultimoMs - c.etapaMs), (unsigned long)c.ultimoMs,
                  (unsigned long)c.txCount, (unsigned long)c.rxCount, (unsigned long)c.presos,
                  (unsigned long)c.flushes, (unsigned long)c.utc, (int)c.pwr,
                  c.txTipo ? (char)c.txTipo : '-',
                  (unsigned long)(c.txTipo ? (c.ultimoMs - c.txDesdeMs) : 0));
    return;
  }
  Serial.printf("\n*** CAIXA-PRETA: o boot anterior morreu (motivo %d, rom 0x%02X, %lu seguidos) ***\n",
                (int)motivo, rom, (unsigned long)c.resetsViolentos);
  Serial.printf("***   etapa em curso: %s (antes: %s), iniciada ha %lu ms; ultima volta em %lu ms\n",
                caixaNomeEtapa(c.etapa), caixaNomeEtapa(c.etapaAnt),
                (unsigned long)(c.ultimoMs - c.etapaMs), (unsigned long)c.ultimoMs);
  Serial.printf("***   tx=%lu rx=%lu presos=%lu flushes=%lu utc=%lu pwr=%d tx em voo=%c ha %lu ms\n",
                (unsigned long)c.txCount, (unsigned long)c.rxCount, (unsigned long)c.presos,
                (unsigned long)c.flushes, (unsigned long)c.utc, (int)c.pwr,
                c.txTipo ? (char)c.txTipo : '-',
                (unsigned long)(c.txTipo ? (c.ultimoMs - c.txDesdeMs) : 0));
  Serial.println("***   motivo: 4=PANIC 5=INT_WDT 6=TASK_WDT 7=outro WDT (LP/RTC/super) 9=BROWNOUT 14=PWR_GLITCH");
  Serial.println("***   rom (P4): 0x07 MWDT 0x09/0x0D RWDT 0x0F brownout 0x10 LP_WDT 0x12 super WDT 0x13 glitch 0x1B PSDET\n");
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
  g_hookCabe    = servicoCabeAgora;
  mundoIdentidade(g_nome, g_cor);   // o roster ja sai certo desde o primeiro anuncio
  // Epoca do meu trajeto (rota_core.h): muda a cada boot para quem me ouve saber
  // que a numeracao dos pontos recomecou. esp_random e o gerador de hardware.
  g_trajEpoca = (uint8_t)(esp_random() & 0xFF);

  // CAIXA-PRETA DO BOOT ANTERIOR (caixa.h). Le antes de qualquer coisa que possa
  // demorar, e decide o modo seguro ANTES de o radio subir - o begin() ja
  // programa a potencia certa. O relato vai para o diario quando ele abrir.
  const esp_reset_reason_t g_motivoBoot = esp_reset_reason();
  uint32_t violentosSeguidos = 0;
  const bool temCaixa = caixaBoot(g_motivoBoot, g_caixaAnterior, violentosSeguidos);
  if (temCaixa) relataCaixa(g_caixaAnterior, g_motivoBoot, false);
  else if (caixaMotivoViolento(g_motivoBoot))
    Serial.printf("*** o boot anterior morreu (motivo %d, rom 0x%02X) e a caixa-preta nao "
                  "estava valida (RAM corrompida ou firmware anterior) ***\n",
                  (int)g_motivoBoot, (int)esp_rom_get_reset_reason(0));
#if MTS_TEM_RADIO
  if (violentosSeguidos >= RADIO_SEGURO_APOS) {
    // Ver hardware.h, "A POTENCIA NUNCA MUDA SOZINHA": teto em RAM, so neste
    // boot, com faixa vermelha na tela ate alguem mandar 'p' ou tocar na
    // potencia do diagnostico. A NVS nao e tocada.
    radioModoSeguro(true);
    char av[64];
    snprintf(av, sizeof(av), "%lu REINICIOS SEGUIDOS - RADIO EM %d dBm SO NESTE BOOT",
             (unsigned long)violentosSeguidos, (int)RADIO_PWR_SEGURO);
    avisoDefine(av);
    Serial.printf("radio: *** %s (configurada: %d dBm, NAO alterada) ***\n", av, (int)radioPotenciaConfig());
  }
#endif

#if MTS_TEM_WIFI
  // WiFi sobe cedo e sem bloquear: se houver rede salva por perto, o OTA fica
  // no ar sozinho ("chegou na garagem, da para atualizar"). O progresso na
  // tela vem pelo gancho, porque o wifi_ota.h nao conhece o painel.
  g_otaDesenha = desenhaOta;
  g_statusTexto = statusTexto;
  g_diagQuery   = diagQuery;    // o eco: a tela conta o proprio estado ao servidor
#if MTS_TEM_RADIO
  // O radio manda no relogio: nada de rede bloqueante com transmissao no ar.
  g_podeBloquear = []() { return !radioTxEmVoo(); };
#endif
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

  // O DIARIO ABRE AQUI, logo depois do cartao (o radio ja subiu antes do painel;
  // o que o begin() dele disse esta no serial e nos campos fios/bg do eco). Um
  // contador de boots na NVS da o nome do arquivo quando ainda nao ha hora de GPS.
  {
    caixaEtapa(CP_NVS);
    Preferences pb; pb.begin("grupo", false);
    uint32_t bootId = pb.getUInt("boots", 0) + 1;
    pb.putUInt("boots", bootId); pb.end();
    caixaEtapa(CP_LACO);
    orcamentoInicia(g_orc);
    if (registroInicia(g_gpsHoraOk ? g_gpsSecDia : 0, bootId))
      Serial.printf("log: %s\n", registroCaminho());
    else
      Serial.println("log: NAO ABRIU - o teste vai voltar sem dado (confira o cartao)");
    registroLinha("!,%lu,boot,%s,slot %d,pwr %d,motivo %d,rom 0x%02X,pwrCfg %d,seguro %d,fw %d,epoca %u",
                  (unsigned long)millis(), g_nome, (int)g_meuSlot, (int)radioPotencia(),
                  (int)g_motivoBoot, (int)esp_rom_get_reset_reason(0), (int)radioPotenciaConfig(),
                  radioEmModoSeguro() ? 1 : 0, (int)MTS_VERSAO, (unsigned)g_trajEpoca);
    if (temCaixa) relataCaixa(g_caixaAnterior, g_motivoBoot, true);
    if (avisoAtivo()) registroLinha("!,%lu,MODO SEGURO,%s", (unsigned long)millis(), g_avisoTexto);
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
        // COMANDO EXPLICITO: o unico caminho (com a tela de diagnostico) que
        // muda a potencia gravada. Desliga o modo seguro e apaga a faixa.
        radioTrocaPotencia((int8_t)pwrAcc);
        avisoDefine("");
        caixaEtapa(CP_NVS);
        Preferences pp; pp.begin("grupo", false); pp.putChar("pwr", radioPotenciaConfig()); pp.end();
        caixaEtapa(CP_LACO);
        desenhaInicial();
      }
      // o caractere que terminou o numero segue para os comandos abaixo
    }
    // BANCADA: 'm' liga/desliga o modo medida - uma linha por pacote recebido,
    // para comparar duas antenas por mediana de RSSI em vez de leitura solta.
    if (c == 'm' || c == 'M') {
      esperandoNum = false;
      g_rssiVerboso = !g_rssiVerboso;
      Serial.printf("modo medida %s\n", g_rssiVerboso ? "LIGADO" : "desligado");
      continue;
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
    char fonte[20];
    if (!g_tdma.haveAnchor)   strcpy(fonte, "ninguem");
    else if (g_tdma.isAnchor) strcpy(fonte, "EU");
    else snprintf(fonte, sizeof(fonte), "slot %u%s", g_tdma.anchorSrc,
                g_tdma.anchorSrcPps ? " UTC" : "");
    Serial.printf("[radio] slot=%u sinc=%s%s fonte=%s | pwr=%d dBm (~%d na antena) | "
                  "tx=%lu perdi=%lu preso=%lu | rx=%lu ruim=%lu outraSala=%lu conflito=%lu | rssi=%.0f snr=%.1f\n",
                  g_tdma.nodeId, sinc, g_tdma.holdover ? " HOLDOVER" : "", fonte,
                  radioPotencia(), radioPotenciaAnt(),
                  (unsigned long)g_tdma.txCount, (unsigned long)g_tdma.missedTx,
                  (unsigned long)g_txPreso,
                  (unsigned long)g_rxOk, (unsigned long)g_rxRuim,
                  (unsigned long)g_rxForaDaSala, (unsigned long)g_slotConflito,
                  g_rxRssi, g_rxSnr);

    // Estado do DIARIO junto do estado do radio: sem isto so da para saber se o
    // log esta gravando tirando o cartao e pondo no PC - e no meio de um teste
    // de campo isso nao acontece. 'perdidas' > 0 significa buffer cheio: o
    // cartao nao esta acompanhando. 'piorFlush' grande denuncia garbage
    // collection do microSD, que e o que poderia atrapalhar o radio.
    Serial.printf("[log] %s | %lu linhas | %lu KB no cartao | perdidas=%lu | "
                  "flushes=%lu pior=%lu us adiados=%lu | laco pior=%lu us lim=%lu us\n",
                  registroAtivo() ? "gravando" : "PARADO",
                  (unsigned long)g_reg.linhas, (unsigned long)(g_reg.bytesGravados / 1024),
                  (unsigned long)g_reg.descartadas, (unsigned long)g_reg.flushes,
                  (unsigned long)g_reg.piorFlushUs, (unsigned long)g_reg.adiados,
                  (unsigned long)g_orc.atrasoMaxUs, (unsigned long)orcamentoLimiteUs(g_tdma));
    // v101: o que separa as hipoteses do campo - ruido de fundo, air-time medido,
    // perda por sequencia por remetente, e o estado do catch-up.
    Serial.printf("[v101] ruido ult=%.0f med=%.0f min=%.0f max=%.0f (%lu am.) | air tx ult=%lu min=%lu max=%lu us "
                  "(calc %lu/%lu) | seqPerd total=%lu [",
                  g_ruidoUlt, g_ruidoN ? g_ruidoSoma / g_ruidoN : 0.0f, g_ruidoMin, g_ruidoMax,
                  (unsigned long)g_ruidoN, (unsigned long)g_txAirUltUs,
                  (unsigned long)(g_txAirMinUs == 0xFFFFFFFFu ? 0 : g_txAirMinUs),
                  (unsigned long)g_txAirMaxUs, (unsigned long)radioAirtimeUs(TRILHA_CAB_N),
                  (unsigned long)RADIO_AIRTIME_US, (unsigned long)g_seqPerdTotal);
    for (int s = 0; s < TDMA_SLOTS; s++)
      Serial.printf("s%d:%lu/%lu ", s, (unsigned long)g_seqPerd[s], (unsigned long)g_rxPorSlot[s]);
    Serial.printf("] | rota %d pts prefixo %d ack %u catchups %lu | relogioVoltou %lu | pwr cfg %d prog %d%s\n",
                  g_rotaN, g_rotaC.prefixo, (unsigned)rotaAck16(g_rotaC), (unsigned long)g_catchups,
                  (unsigned long)g_relogioVoltou, (int)radioPotenciaConfig(), (int)radioPotencia(),
                  radioEmModoSeguro() ? " SEGURO" : "");
  }
#endif

  int16_t x, y;
  if (!esperaToque(tft, x, y, 400)) return;

  if (dentro(hEng, x, y)) { telaConfig(); desenhaInicial(); return; }
  if (dentro(hDiag, x, y)) { telaDiag(); desenhaInicial(); return; }

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
