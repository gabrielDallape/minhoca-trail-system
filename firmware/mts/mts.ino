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
#include "esp_system.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#if MTS_TEM_SD
#include "hal/sd.h"
#endif

LGFX_P4     tft;
LGFX_Sprite g_cv(&tft);        // o quadro do mapa: criado UMA vez no boot, na PSRAM
static bool g_temSprite = false;
static CaixaPreta g_caixaAnterior;

// chamados pelas telas quando o usuario troca tema/zoom (nunca por quadro)
void salvaTema() { g_cfg.tema = g_tema; nvsGravaUChar("theme", g_cfg.tema); }
void salvaZoom() { nvsGravaUChar("zoom", g_cfg.zoom); }

static void sessaoEncerra()
{
  g_ses.ativa = false; g_ses.gNome[0] = 0; g_ses.gCod[0] = 0;
  nvsGravaSessao(g_ses);
  redeTrocaSala("SEMSALA");        // sai do grupo = sai da sala: para de falar nele
  frotaLimpaOutros(g_frota);
  rotaLimpaCore(g_rotaC);
  // Fora do grupo eu nao guio nem peco socorro: sem isto o aparelho seguia
  // anunciando LIDER (e talvez ALERTA) na sala neutra.
  g_frota.c[0].lider = false;
  g_frota.c[0].alerta = false;
}

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
  ACUM(u, fora, n, "[rede] slot=%u sinc=%s%s fonte=%s | pwr=%d dBm (~%d) cfg=%d%s | tx=%lu perdi=%lu preso=%lu | "
                "rx=%lu ruim=%lu outraSala=%lu conflito=%lu versao=%lu | rssi=%.0f snr=%.1f | ruido=%.0f | air=%lu/%lu\n",
                t.nodeId, sinc, t.holdover ? " HOLDOVER" : "", fonte, redePotencia(), radioHwPotenciaAntena(redePotencia()),
                redePotenciaConfig(), redeEmModoSeguro() ? " SEGURO" : "", (unsigned long)t.txCount,
                (unsigned long)t.missedTx, (unsigned long)g_rede.txPreso, (unsigned long)g_rede.rxOk,
                (unsigned long)g_rede.rxRuim, (unsigned long)g_rede.rxForaDaSala, (unsigned long)g_frota.conflitos,
                (unsigned long)g_frota.versaoErrada, g_rede.rxRssi, g_rede.rxSnr, g_rede.ruidoUlt,
                (unsigned long)g_rede.txAirUltUs, (unsigned long)REDE_AIRTIME_US);
#endif
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

// ---------------------------------------------------------------- caixa-preta
static void relataCaixa(const CaixaPreta& c, esp_reset_reason_t motivo, bool paraLog)
{
  const int rom = (int)esp_rom_get_reset_reason(0);
  if (paraLog) {
    registroLinha("C,%lu,motivo %d,rom 0x%02X,seguidos %lu,etapa %s,antes %s,ha %lu ms,ultimoMs %lu,tx %lu,rx %lu,presos %lu,flushes %lu,utc %lu,pwr %d,txTipo %c,txHa %lu ms",
                  (unsigned long)millis(), (int)motivo, rom, (unsigned long)c.resetsViolentos,
                  caixaNomeEtapa(c.etapa), caixaNomeEtapa(c.etapaAnt), (unsigned long)(c.ultimoMs - c.etapaMs),
                  (unsigned long)c.ultimoMs, (unsigned long)c.txCount, (unsigned long)c.rxCount, (unsigned long)c.presos,
                  (unsigned long)c.flushes, (unsigned long)c.utc, (int)c.pwr, c.txTipo ? (char)c.txTipo : '-',
                  (unsigned long)(c.txTipo ? (c.ultimoMs - c.txDesdeMs) : 0));
    return;
  }
  Serial.printf("\n*** CAIXA-PRETA: o boot anterior morreu (motivo %d, rom 0x%02X, %lu seguidos) ***\n", (int)motivo, rom, (unsigned long)c.resetsViolentos);
  Serial.printf("***   etapa em curso: %s (antes: %s), iniciada ha %lu ms; ultima volta em %lu ms\n",
                caixaNomeEtapa(c.etapa), caixaNomeEtapa(c.etapaAnt), (unsigned long)(c.ultimoMs - c.etapaMs), (unsigned long)c.ultimoMs);
  Serial.printf("***   tx=%lu rx=%lu presos=%lu flushes=%lu utc=%lu pwr=%d tx em voo=%c ha %lu ms\n",
                (unsigned long)c.txCount, (unsigned long)c.rxCount, (unsigned long)c.presos, (unsigned long)c.flushes,
                (unsigned long)c.utc, (int)c.pwr, c.txTipo ? (char)c.txTipo : '-', (unsigned long)(c.txTipo ? (c.ultimoMs - c.txDesdeMs) : 0));
  Serial.println("***   motivo: 4=PANIC 5=INT_WDT 6=TASK_WDT 7=outro WDT (LP/RTC/super) 9=BROWNOUT 14=PWR_GLITCH");
  Serial.println("***   rom (P4): 0x07 MWDT 0x09/0x0D RWDT 0x0F brownout 0x10 LP_WDT 0x12 super WDT 0x13 glitch 0x1B PSDET\n");
}

// ---------------------------------------------------------------- trilha
static void rodaTrilha()
{
  g_frota.c[0].lider = g_ses.lider;
  g_frota.c[0].alerta = false;        // pedido de socorro nao atravessa sessoes
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
    if (c == 'd' || c == 'D') { char b[1200]; statusTexto(b, sizeof(b)); Serial.print(b); continue; }
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

#if MTS_TEM_GPS
  // ANTES do painel: a primeira fixacao e o gargalo de ligar o aparelho.
  gpsInicia();
  Serial.printf("gps: Serial1 a %d bps (RX=%d TX=%d PPS=%d)\n", GPS_BAUD, PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_PPS);
#endif

  char relatoCfg[96];
  configCarrega(relatoCfg, sizeof(relatoCfg));
  if (relatoCfg[0]) Serial.println(relatoCfg);
  aplicaTema(g_cfg.tema);
  frotaInicia(g_frota);
  frotaIdentidade(g_frota, g_cfg.nome, g_cfg.cor, g_cfg.slot);
  rotaLimpaCore(g_rotaC);
  orcamentoInicia(g_orc);

  // CAIXA-PRETA DO BOOT ANTERIOR: le antes de qualquer coisa que possa demorar,
  // e decide o modo seguro ANTES de o radio subir.
  const esp_reset_reason_t motivoBoot = esp_reset_reason();
  uint32_t violentos = 0;
  const bool temCaixa = caixaBoot(motivoBoot, g_caixaAnterior, violentos);
  // Depois do caixaBoot: a epoca grava na NVS, e o carimbo CP_NVS nao pode cair
  // na caixa do boot anterior antes de ela ser lida.
  trajetoInicia(g_traj, nvsProximaEpoca((uint8_t)(esp_random() & 0xFF)));   // epoca nova a cada boot
  if (temCaixa) relataCaixa(g_caixaAnterior, motivoBoot, false);
  else if (caixaMotivoViolento(motivoBoot))
    Serial.printf("*** o boot anterior morreu (motivo %d, rom 0x%02X) e a caixa-preta nao estava valida ***\n",
                  (int)motivoBoot, (int)esp_rom_get_reset_reason(0));
  // ALARME DE REINICIOS: so avisa. A potencia continua a do usuario.
  bool seguro = false;
  if (violentos >= RADIO_SEGURO_APOS) {
    seguro = true;
    char av[64];
    snprintf(av, sizeof(av), "%lu REINICIOS SEGUIDOS - CONFERIR O 5V DO RADIO", (unsigned long)violentos);
    avisoDefine(av);
    Serial.printf("rede: *** %s (potencia %d dBm, NAO alterada) ***\n", av, (int)g_cfg.pwr);
  }

  Serial.printf("wdt: %s (%d s, loopTask)\n", wdtInicia() ? "ativo" : "NAO configurou", WDT_TIMEOUT_S);

#if MTS_TEM_WIFI
  g_otaDesenha = desenhaOta; g_statusTexto = statusTexto; g_diagQuery = diagQuery;
  wifiInicia();
#endif

  // O radio sobe AQUI, nao ao entrar na trilha: estar no ar e estado do aparelho.
  // ATENCAO: daqui em diante ele TRANSMITE quando chegar o slot. Antena antes.
  {
    Sessao s0; nvsLeSessao(s0);
    redeInicia(g_cfg.slot, s0.ativa ? s0.gCod : "SEMSALA", g_cfg.pwr, seguro);
  }

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
  {
    uint32_t bootId = nvsIncrementaBoots();
    if (registroInicia(redeUtc(), bootId)) Serial.printf("log: %s\n", registroCaminho());
    else Serial.println("log: NAO ABRIU - o teste vai voltar sem dado (confira o cartao)");
    registroLinha("!,%lu,boot,%s,slot %d,pwr %d,motivo %d,rom 0x%02X,pwrCfg %d,seguro %d,fw %d,proto %d,epoca %u,cfgv %u",
                  (unsigned long)millis(), g_cfg.nome, (int)g_cfg.slot, (int)redePotencia(), (int)motivoBoot,
                  (int)esp_rom_get_reset_reason(0), (int)g_cfg.pwr, seguro ? 1 : 0, (int)MTS_VERSAO, (int)PROTO_VERSAO,
                  (unsigned)g_traj.epoca, (unsigned)g_cfgEsquema);
    if (relatoCfg[0]) registroLinha("!,%lu,%s", (unsigned long)millis(), relatoCfg);
    if (temCaixa) relataCaixa(g_caixaAnterior, motivoBoot, true);
    if (avisoAtivo()) registroLinha("!,%lu,REINICIOS SEGUIDOS,%s", (unsigned long)millis(), g_avisoTexto);
  }
#endif

  // RELIGOU NO MEIO DA TRILHA: a sessao vira uma OFERTA (RETOMAR) na tela
  // inicial, nunca entra sozinha - uma tela presa numa sessao velha parecia
  // travada e so saia regravando.
  nvsLeSessao(g_ses);
  if (g_ses.ativa) {
    g_frota.c[0].lider = g_ses.lider;
    Serial.printf("sessao guardada: %s (%s) - oferecendo RETOMAR\n", g_ses.gNome, g_ses.lider ? "lider" : "seguidor");
    redeTrocaSala(g_ses.gCod);
  }
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
  if (millis() - tRel > 5000) { tRel = millis(); char b[1200]; statusTexto(b, sizeof(b)); Serial.print(b); }

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
      g_frota.c[0].lider = true;       // o roster do criador ja diz LIDER na sala de espera
      redeTrocaSala(gc);
      frotaLimpaOutros(g_frota);       // a sala nova nasce vazia
      rotaLimpaCore(g_rotaC);
      estadoVai(EST_SALA);
      if (telaSalaEspera(tft, gn, gc)) {
        g_ses.ativa = true; g_ses.lider = true;
        strncpy(g_ses.gNome, gn, sizeof(g_ses.gNome) - 1);
        strncpy(g_ses.gCod,  gc, sizeof(g_ses.gCod) - 1);
        nvsGravaSessao(g_ses);
        Serial.printf("trilha aberta: %s | codigo %s\n", gn, gc);
        // Trilha nova, rastro novo (como o v101): sem isto o lider transmitia tudo
        // desde o boot - o caminho ate a entrada da trilha, ou a trilha anterior -
        // e quem entrasse passava ~20 min recebendo historico. A numeracao
        // continua (so a EPOCA diz "recomecei"), entao ninguem ve um vao falso.
        trajetoLimpa(g_traj);
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
      g_ses.ativa = true; g_ses.lider = false;
      // O nome do grupo nao viaja no ar - so o hash. Da LISTA vem "Grupo de <lider>";
      // pelo codigo digitado, o seguidor mostra o codigo.
      if (nomeGrupo[0]) strncpy(g_ses.gNome, nomeGrupo, sizeof(g_ses.gNome) - 1);
      else snprintf(g_ses.gNome, sizeof(g_ses.gNome), "Grupo %s", cod);
      g_ses.gNome[sizeof(g_ses.gNome) - 1] = 0;
      strncpy(g_ses.gCod, cod, sizeof(g_ses.gCod) - 1);
      nvsGravaSessao(g_ses);
      g_frota.c[0].lider = false;
      redeTrocaSala(cod);
      frotaLimpaOutros(g_frota);
      rotaLimpaCore(g_rotaC);
      Serial.printf("entrou no grupo de codigo %s\n", cod);
      trajetoLimpa(g_traj);
      rodaTrilha();
    }
    estadoVai(EST_INICIAL);
    desenhaInicial(tft);
  }
}
