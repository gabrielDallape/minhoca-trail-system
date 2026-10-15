/*
 * MTS - Minhoca Trail System | firmware da tela ESP32-P4 de 7" (Waveshare 7B)
 *
 * "Siga o lider" por LoRa para trilha off-road: o lider transmite o TRAJETO que
 * fez; os seguidores veem o mapa (eu no centro, o caminho do lider a frente, meu
 * rastro, distancia), alerta, aviso de perda de sinal e catch-up do trajeto.
 * TDMA de 12 slots de 500 ms num quadro de 6 s, disciplinado pelo PPS do GPS.
 *
 * ARQUITETURA (dependencias so para baixo - ver README.md):
 *   core/   header-only, sem Arduino: protocolo, TDMA, rota/catch-up, frota,
 *           geodesia, config, diario, orcamento. Testado no PC (Node) e na placa.
 *   hal/    a placa: display, radio SX1262, GPS+PPS, cartao, NVS, caixa-preta,
 *           relogio, watchdog.
 *   app/    o servico (um so, chamado de todo laco), a rede, o diario, as telas.
 *   mts.ino este arquivo: boot, laco principal, comandos do serial.
 *
 *   .\tools\build.ps1 firmware\mts -Board p4 -Upload -Port COMx
 */
#include "hal/pinos.h"
#include "hal/display.h"
#include "hal/relogio.h"
#include "hal/caixa.h"
#include "hal/wdt.h"
#include "hal/nvs.h"
#include "core/params.h"
#include "app/estado.h"
#include "app/config.h"
#include "app/registro.h"
#include "app/rede.h"
#include "app/telas/ui.h"
#include "app/telas/splash.h"
#include "app/telas/teclado.h"
#include "app/telas/mapa_fundo.h"
#include "app/telas/mapa.h"
#include "app/telas/tela_inicial.h"
#include "app/telas/tela_config.h"
#include "app/telas/tela_diag.h"
#include "app/telas/tela_grupo.h"
#include "app/telas/tela_trilha.h"
#if MTS_TEM_WIFI
#include "app/wifi_ota.h"
#endif
#include "app/servico.h"
#include "app/sessao.h"
#include "app/boot.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#if MTS_TEM_SD
#include "hal/sd.h"
#endif

LGFX_P4     tft;
LGFX_Sprite g_cv(&tft);        // o quadro do mapa: criado UMA vez no boot, na PSRAM
static bool g_temSprite = false;

// chamados pelas telas quando o usuario troca tema/zoom (nunca por quadro)
void salvaTema() { g_cfg.tema = g_tema; nvsGravaUChar("theme", g_cfg.tema); }
void salvaZoom() { nvsGravaUChar("zoom", g_cfg.zoom); }

// ---------------------------------------------------------------- telemetria
// snprintf ACUMULADO que nunca passa do fim. O retorno do snprintf e o que ele
// TERIA escrito: somado sem limite, 'n - u' dava a volta (size_t) e a escrita
// seguinte ia para fora do buffer - o /status do WiFi (640 B) recebia ~700.
#define ACUM(u, fora, n, ...) do { \
    if ((size_t)(u) < (size_t)(n)) { \
      int k_ = snprintf((fora) + (u), (size_t)(n) - (size_t)(u), __VA_ARGS__); \
      if (k_ > 0) (u) += k_; \
    } \
    if ((size_t)(u) >= (size_t)(n)) (u) = (int)(n) - 1; \
  } while (0)
static void statusTexto(char* fora, size_t n)
{
  int u = 0; ACUM(u, fora, n, "%s | fw v%d proto v%d | ar %lu s | tela %s\n", g_cfg.nome, MTS_VERSAO, PROTO_VERSAO,
                   (unsigned long)(millis() / 1000), EST_NOMES[g_estado]);
#if MTS_TEM_GPS
  ACUM(u, fora, n, "[gps] fix=%s sats=%u vista=%u snr=%u hdop=%.2f | %.6f, %.6f | frases=%lu ruins=%lu | PPS=%s (%lu us) pulsos=%lu parVelho=%lu backlog=%lu\n",
                gpsFixValido() ? "SIM" : "nao", g_gps.sats, g_gps.vista, g_gps.snrAnt, g_gps.hdop, g_gps.lat, g_gps.lon,
                (unsigned long)g_gps.frases, (unsigned long)g_gps.ruins, gpsPpsValido() ? "1Hz" : "-",
                (unsigned long)g_gps.ppsIntervaloUs, (unsigned long)gpsPpsCnt(), (unsigned long)g_gps.parVelho,
                (unsigned long)g_gps.backlogDescartado);
#endif
#if MTS_TEM_RADIO
  const Tdma& t = g_rede.tdma;
  const char* sinc = t.sync == TDMA_SYNC_PPS ? "PPS" : t.sync == TDMA_SYNC_BEACON ? "beacon" : "-";
  char fonte[20];
  if (!t.haveAnchor) strcpy(fonte, "ninguem");
  else if (t.isAnchor) strcpy(fonte, "EU");
  else snprintf(fonte, sizeof(fonte), "slot %u%s", t.anchorSrc, t.anchorSrcPps ? " UTC" : "");
#if MTS_TEM_RADIO
  if (g_rede.ok && !g_rede.txEmVoo) {
    int dio1; uint32_t fl; radioHwDiag(dio1, fl);
    ACUM(u, fora, n, "[dio1] pino=%d eventos no chip=0x%04lX | chip %s | vigia pegou TX sem IRQ: %lu (com o pino ALTO: %lu)\n", dio1, (unsigned long)fl,
         radioHwConfigOk() ? "configurado" : "DE FABRICA", (unsigned long)g_rede.txSemIrq, (unsigned long)g_rede.txSemIrqPinoAlto);
  }
#endif
  ACUM(u, fora, n, "[rede] slot=%u sinc=%s%s fonte=%s | pwr=%d dBm (~%d) cfg=%d%s | tx=%lu perdi=%lu preso=%lu | "
                "rx=%lu ruim=%lu outraSala=%lu conflito=%lu versao=%lu | rssi=%.0f snr=%.1f | ruido=%.0f | air=%lu/%lu\n",
                t.nodeId, sinc, t.holdover ? " HOLDOVER" : "", fonte, redePotencia(), radioHwPotenciaAntena(redePotencia()),
                redePotenciaConfig(), redeEmModoSeguro() ? " SEGURO" : "", (unsigned long)t.txCount,
                (unsigned long)t.missedTx, (unsigned long)g_rede.txPreso, (unsigned long)g_rede.rxOk,
                (unsigned long)g_rede.rxRuim, (unsigned long)g_rede.rxForaDaSala, (unsigned long)g_frota.conflitos,
                (unsigned long)g_frota.versaoErrada, g_rede.rxRssi, g_rede.rxSnr, g_rede.ruidoUlt,
                (unsigned long)g_rede.txAirUltUs, (unsigned long)REDE_AIRTIME_US);
#endif
  {
    // [trilha] O QUE A TELA DA TRILHA MOSTRARIA - pelas mesmas funcoes do mapa
    // (frotaParaDesenhar) e da rota. Para testar por cabo, sem olhar a tela.
    const uint32_t ag = millis();
    const int li = frotaLider(g_frota);
    bool des = false, vz = false; double la, lo;
    if (li > 0) des = frotaParaDesenhar(g_frota.c[li], ag, la, lo, vz);
    int vaos = 0;
    for (int i = 1; i < g_rotaC.n; i++)
      if (rotaVaoAntes(g_rotaC, i) && !(g_rotaC.temPiso && g_rotaC.p[i].vseq <= g_rotaC.pisoV)) vaos++;
    int idx[4]; const int nv = vizinhosOrdena(g_viz, idx, 4);
    ACUM(u, fora, n, "[trilha] %s %s | carros %d | lider %s%s%s | rota %d pts %d vaos ack %u topo %u | meu rastro %d pts | grupos no ar %d%s%s%s\n",
         g_ses.ativa ? g_ses.gNome : "(sem grupo)", g_frota.c[0].lider ? "LIDER" : "segue", frotaQuantos(g_frota),
         li > 0 ? (des ? "DESENHADO" : "na frota mas NAO desenhado") : "-", vz ? " vazado" : "",
         (li > 0 && g_frota.c[li].semGps) ? " SEM GPS" : "",
         g_rotaC.n, vaos, (unsigned)rotaAck16(g_rotaC), (unsigned)rotaTopo16(g_rotaC), g_traj.n,
         nv, nv ? ": " : "", nv ? (g_viz.g[idx[0]].temNome ? g_viz.g[idx[0]].nome : "(nome a caminho)") : "",
         (nv && g_viz.g[idx[0]].temLider) ? " LIDER NO AR" : "");
  }
  ACUM(u, fora, n, "[log] %s | %lu linhas | %lu KB | perdidas=%lu | flushes=%lu pior=%lu us adiados=%lu | laco pior=%lu us negados=%lu estouros=%lu | rota %d pts prefixo %d catchups %lu | relogioVoltou %lu\n",
                registroAtivo() ? "gravando" : "PARADO", (unsigned long)g_reg.anel.linhas, (unsigned long)(g_reg.bytesGravados / 1024),
                (unsigned long)g_reg.anel.descartadas, (unsigned long)g_reg.flushes, (unsigned long)g_reg.piorFlushUs,
                (unsigned long)g_reg.adiados, (unsigned long)g_orc.atrasoMaxUs, (unsigned long)g_orc.negados,
                (unsigned long)g_orc.estouros, g_rotaC.n, g_rotaC.prefixo, (unsigned long)g_rede.catchups,
                (unsigned long)g_relogio.voltou);
  (void)u;
}

#if MTS_TEM_WIFI
// A mesma informacao, compacta e segura para query string (o eco). O NOME vai
// junto, sanitizado: relatorio anonimo de frota e quase inutil.
static void diagQuery(char* fora, size_t n)
{
  char nm[17]; size_t k = 0;
  for (const char* p = g_cfg.nome; *p && k < sizeof(nm) - 1; p++) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    nm[k++] = ok ? c : '_';
  }
  nm[k] = 0;
  if (!k) strcpy(nm, "sem_nome");
  int u = 0; ACUM(u, fora, n, "n=%s&v=%d&pv=%d&up=%lu", nm, MTS_VERSAO, PROTO_VERSAO, (unsigned long)(millis() / 1000));
#if MTS_TEM_RADIO
  const Tdma& t = g_rede.tdma;
  ACUM(u, fora, n, "&slot=%u&fios=%d&bg=%ld&ok=%d&pwr=%d&sinc=%d&fonte=%d&utc=%d&tx=%lu&perdi=%lu&preso=%lu&rx=%lu&ruim=%lu&sala=%lu&conf=%lu&ver=%lu&rssi=%d&ruido=%d",
                t.nodeId, g_rhw.fiosOk ? 1 : 0, (long)g_rhw.beginSt, g_rede.ok ? 1 : 0, redePotencia(), (int)t.sync,
                t.haveAnchor ? (t.isAnchor ? 99 : (int)t.anchorSrc) : -1, t.anchorSrcPps ? 1 : 0,
                (unsigned long)t.txCount, (unsigned long)t.missedTx, (unsigned long)g_rede.txPreso,
                (unsigned long)g_rede.rxOk, (unsigned long)g_rede.rxRuim, (unsigned long)g_rede.rxForaDaSala,
                (unsigned long)g_frota.conflitos, (unsigned long)g_frota.versaoErrada, (int)g_rede.rxRssi, (int)g_rede.ruidoUlt);
#endif
#if MTS_TEM_GPS
  ACUM(u, fora, n, "&fix=%d&sats=%u&pps=%lu", gpsFixValido() ? 1 : 0, g_gps.sats, (unsigned long)gpsPpsCnt());
#endif
  (void)u;
}

static void desenhaOta(int pct)
{
  static int ultimo = -1;
  if (pct <= ultimo && pct != 0) return;
  ultimo = (pct >= 100) ? -1 : pct;
  if (pct == 0) {
    estadoVai(EST_OTA);
    tft.fillScreen(C_BG);
    tft.setTextDatum(middle_center); tft.setFont(&fonts::FreeSansBold24pt7b); tft.setTextColor(C_INK);
    tft.drawString("ATUALIZANDO PELO WIFI", TELA_W / 2, TELA_H / 2 - 80);
    tft.setFont(&fonts::FreeSans12pt7b); tft.setTextColor(C_WARN);
    tft.drawString("nao desligue o aparelho", TELA_W / 2, TELA_H / 2 - 20);
    tft.drawRoundRect(TELA_W / 2 - 300, TELA_H / 2 + 40, 600, 44, 12, C_LINE);
  }
  int w = (int)(592L * pct / 100);
  if (w > 0) tft.fillRoundRect(TELA_W / 2 - 296, TELA_H / 2 + 44, w, 36, 8, C_SUN);
  tft.setFont(&fonts::Font0);
}
#endif

// ---------------------------------------------------------------- trilha
static void rodaTrilha()
{
  sessaoComecaTrilha();
  estadoVai(EST_TRILHA);
  if (telaTrilha(tft, g_cv, g_temSprite, g_ses)) {
    estadoVai(EST_SAIR);
    sessaoEncerra();
    Serial.println("saiu da trilha");
  }
  estadoVai(EST_INICIAL);
}

// ---------------------------------------------------------------- serial
// Comandos de bancada. 'p' e o unico (com a tela de diagnostico) que muda a potencia.
static void serialComandos()
{
  static bool esperandoSlot = false, esperandoPwr = false;
  static int  pwrAcc = -1, pwrDig = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'p' || c == 'P') { esperandoPwr = true; pwrAcc = -1; pwrDig = 0; esperandoSlot = false; continue; }
    if (esperandoPwr) {
      // Ate DOIS digitos (a faixa -9..22 cabe). Era 'pwrAcc < 3': p30 virava 3 dBm
      // e p40..p9x viravam 4..9 - e isso ia para a NVS.
      if (c >= '0' && c <= '9' && pwrDig < 2) { pwrAcc = (pwrAcc < 0 ? 0 : pwrAcc * 10) + (c - '0'); pwrDig++; continue; }
      esperandoPwr = false;
      if (pwrAcc >= 0) {
        redeTrocaPotencia((int8_t)pwrAcc);
        g_cfg.pwr = redePotenciaConfig();
        avisoDefine("");
        configGrava();
        if (g_estado == EST_INICIAL) desenhaInicial(tft);
      }
    }
    if (c == 'm' || c == 'M') { g_rede.rssiVerboso = !g_rede.rssiVerboso; Serial.printf("modo medida %s\n", g_rede.rssiVerboso ? "LIGADO" : "desligado"); continue; }
    if (c == 's' || c == 'S') { esperandoSlot = true; continue; }
    if (c == 'x' || c == 'X') { sessaoEncerra(); Serial.println("sessao descartada - sala neutra"); if (g_estado == EST_INICIAL) desenhaInicial(tft); continue; }
    if (c == 'a' || c == 'A') { g_frota.c[0].alerta = !g_frota.c[0].alerta; Serial.printf("alerta %s\n", g_frota.c[0].alerta ? "LIGADO" : "desligado"); continue; }
    if (c == 'd' || c == 'D') { char b[1400]; statusTexto(b, sizeof(b)); Serial.print(b); continue; }
    // BANCADA POR CABO: CRIAR+ABRIR e ENTRAR pela lista, pelas MESMAS funcoes dos
    // toques (app/sessao.h). A tela fica na inicial (a da trilha so desenha); a
    // linha [trilha] do status ('d') diz o que ela mostraria.
    if ((c == 'c' || c == 'C') && g_estado == EST_INICIAL) {
      char gc[8], gn[GRUPO_NOME_MAX];
      snprintf(gc, sizeof(gc), "%05lu", (unsigned long)(esp_random() % 90000 + 10000));
      snprintf(gn, sizeof(gn), "Grupo do %s", g_cfg.nome);
      sessaoCriaSala(gc); sessaoAbreComoLider(gn, gc); sessaoComecaTrilha();
      Serial.printf("bancada: CRIOU %s (codigo %s) - LIDER, trilha aberta\n", gn, gc);
      desenhaInicial(tft);
      continue;
    }
    if ((c == 'e' || c == 'E') && g_estado == EST_INICIAL) {
      int idx[4]; const int nv = vizinhosOrdena(g_viz, idx, 3);
      if (!nv) { Serial.println("bancada: nenhum grupo no ar para entrar"); continue; }
      const GrupoNoAr& gr = g_viz.g[idx[0]];
      char cod[8], nome[GRUPO_NOME_MAX];
      snprintf(cod, sizeof(cod), "#%04X", (unsigned)gr.sala);
      if (gr.temNome) snprintf(nome, sizeof(nome), "Grupo de %s", gr.nome); else snprintf(nome, sizeof(nome), "Grupo %04X", (unsigned)gr.sala);
      sessaoEntra(cod, nome); sessaoComecaTrilha();
      Serial.printf("bancada: ENTROU em %s (%s)\n", nome, cod);
      desenhaInicial(tft);
      continue;
    }
    if (esperandoSlot && c >= '0' && c <= '9') {
      esperandoSlot = false;
      uint8_t novo = c - '0';
      if (novo < FROTA_MAX_CARROS) {
        g_cfg.slot = novo; redeTrocaSlot(novo); configGrava();
        Serial.printf("slot = %u (aplicado e salvo)\n", novo);
      }
    }
    esperandoSlot = false;
  }
}

// ---------------------------------------------------------------- boot
void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) delay(10);
  Serial.printf("\n=== MTS - Minhoca Trail System (fw v%d, proto v%d, quadro %d s x %d slots) ===\n",
                MTS_VERSAO, PROTO_VERSAO, TDMA_FRAME_S, TDMA_SLOTS);

  // Tudo que nao e tela, na ordem certa (app/boot.h) - o simulador chama o mesmo.
  appBootNucleo();
  aplicaTema(g_cfg.tema);
#if MTS_TEM_WIFI
  g_otaDesenha = desenhaOta; g_statusTexto = statusTexto; g_diagQuery = diagQuery;
  wifiInicia();
#endif

  if (!tft.init()) { Serial.println("ERRO: painel nao inicializou."); while (true) { delay(1000); wdtAlimenta(); } }
  telaTamanho(tft.width(), tft.height());
  Serial.printf("painel %s %dx%d | psram livre %u | nome=%s\n", tft.panelName, (int)tft.width(), (int)tft.height(),
                (unsigned)ESP.getFreePsram(), g_cfg.nome);
  // O sprite do mapa, UMA vez, na PSRAM: 1024x600x2 = 1,2 MB.
  g_cv.setPsram(true);
  g_cv.setColorDepth(16);
  g_temSprite = g_cv.createSprite(TELA_W, TELA_H);
  if (!g_temSprite) Serial.println("mapa: SEM sprite (PSRAM?) - desenhando direto, vai piscar");

  splashMostrar(tft, 2400);

#if MTS_TEM_SD
  // Tudo do cartao e OPCIONAL: qualquer falha so tira o fundo e o diario.
  if (!sdMonta()) Serial.println("cartao: nao montou - seguindo sem fundo de mapa nem diario");
  else if (!fundoAbre(g_fundo)) Serial.printf("cartao: %s - seguindo sem fundo de mapa\n", g_fundo.erro);
  else {
    Serial.printf("mapa: %s%s | leitura %s | psram livre %u\n", g_fundo.temVec ? "vetor " : "", g_fundo.temDem ? "relevo" : "",
                  g_sd.multi ? "multi-setor" : "setor a setor (lenta)", (unsigned)ESP.getFreePsram());
  }
  appBootDiario();
#endif

  // RELIGOU NO MEIO DA TRILHA: oferta de RETOMAR (app/sessao.h)
  if (sessaoRecuperaNoBoot())
    Serial.printf("sessao guardada: %s (%s) - oferecendo RETOMAR\n", g_ses.gNome, g_ses.lider ? "lider" : "seguidor");
  estadoVai(EST_INICIAL);
  desenhaInicial(tft);
}

// ---------------------------------------------------------------- laco
void loop()
{
  servico();
  serialComandos();

  static uint32_t tPast = 0;
  if (millis() - tPast > 1000) { tPast = millis(); pintaPastilhas(tft); }
#if MTS_TEM_WIFI
  otaNuvemChecar();     // so aqui, na tela inicial
#endif
  static uint32_t tRel = 0;
  if (millis() - tRel > 5000) { tRel = millis(); char b[1400]; statusTexto(b, sizeof(b)); Serial.print(b); }

  int16_t x, y;
  if (!esperaToque(tft, x, y, 400)) return;

  if (dentro(g_ini.eng, x, y))  { estadoVai(EST_CONFIG); telaConfig(tft); estadoVai(EST_INICIAL); desenhaInicial(tft); return; }
  if (dentro(g_ini.diag, x, y)) { estadoVai(EST_DIAG);   telaDiag(tft);   estadoVai(EST_INICIAL); desenhaInicial(tft); return; }
  if (g_ses.ativa && dentro(g_ini.retomar, x, y)) { rodaTrilha(); desenhaInicial(tft); return; }
  if (g_ses.ativa && dentro(g_ini.descarta, x, y)) { sessaoEncerra(); Serial.println("sessao descartada"); desenhaInicial(tft); return; }

  if (dentro(g_ini.criar, x, y)) {
    desenhaInicial(tft, 0);
    estadoVai(EST_CRIAR);
    char gn[GRUPO_NOME_MAX] = "", gc[8] = "";
    if (telaCriarGrupo(tft, g_cfg.nome, gn, gc)) {
      sessaoCriaSala(gc);
      estadoVai(EST_SALA);
      if (telaSalaEspera(tft, gn, gc)) {
        sessaoAbreComoLider(gn, gc);
        Serial.printf("trilha aberta: %s | codigo %s\n", gn, gc);
        rodaTrilha();
      } else {
        sessaoEncerra();
      }
    }
    estadoVai(EST_INICIAL);
    desenhaInicial(tft);
  } else if (dentro(g_ini.entrar, x, y)) {
    desenhaInicial(tft, 1);
    estadoVai(EST_ENTRAR);
    char cod[8] = {0}, nomeGrupo[GRUPO_NOME_MAX] = {0};
    if (telaEntrarGrupo(tft, cod, nomeGrupo, sizeof(nomeGrupo))) {
      sessaoEntra(cod, nomeGrupo);
      Serial.printf("entrou no grupo %s (%s)\n", g_ses.gNome, cod);
      rodaTrilha();
    }
    estadoVai(EST_INICIAL);
    desenhaInicial(tft);
  }
}
