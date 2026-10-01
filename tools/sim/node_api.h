// ============================================================================
//  NODE_API - o laco de UMA tela simulada. Incluido DENTRO do namespace do no,
//  DEPOIS do firmware: chama o firmware de verdade (servico, sessao, boot).
// ============================================================================

static std::deque<ar::Rx> s_filaRx;
static uint64_t s_ultColetaG = 0;
static bool     s_gpsFix = false;
static uint64_t s_proxSegG = 0;           // a proxima borda de segundo UTC (global)
struct SimNmea { uint64_t quandoG; std::string txt; };
static std::deque<SimNmea> s_nmea;
static bool     s_naTrilha = false;
static uint32_t s_ultPintaMs = 0;
static char     s_salaCod[8] = "";
static const uint64_t SIM_UTC0_US = 12ull * 3600ull * 1000000ull;   // a simulacao comeca as 12:00:00 UTC
// A tela da trilha pinta o mapa ~1 vez por segundo, quando o orcamento deixa
// (servicoCabe). 120 ms e o custo medido com cache quente; o pior frio passa de
// 250, mas um passo do simulador nao pode passar do menor pacote (171 ms).
static const uint32_t SIM_MAPA_US = 120000;
static const uint32_t SIM_LACO_US = 1500;

static void simTransmiteNoAr(const uint8_t* b, int n, uint64_t durUs)
{
  ar::transmite(s_no, simGlobal(s_us), durUs, b, n, g_rhw.pwr);
}

static std::string simNmeaFecha(const std::string& corpo)
{
  uint8_t s = 0;
  for (char c : corpo) s ^= (uint8_t)c;
  char t[8]; snprintf(t, sizeof(t), "*%02X\r\n", s);
  return "$" + corpo + t;
}
static std::string simGrausNmea(double v, bool lat)
{
  double a = std::fabs(v); int g = (int)a; double m = (a - g) * 60.0;
  char b[32]; snprintf(b, sizeof(b), lat ? "%02d%08.5f" : "%03d%08.5f", g, m);
  return b;
}
// O NEO-7M: a GGA e a RMC do segundo S chegam ~120 ms DEPOIS da borda do PPS de S.
static void simGeraNmea(uint64_t bordaG)
{
  const uint64_t utcS = (SIM_UTC0_US + bordaG) / 1000000ull;
  char h[16]; snprintf(h, sizeof(h), "%02d%02d%02d.00", (int)((utcS / 3600) % 24), (int)((utcS / 60) % 60), (int)(utcS % 60));
  ar::Pose p = ar::posicao(s_no, bordaG);
  std::string gga = std::string("GPGGA,") + h + ",";
  std::string rmc = std::string("GPRMC,") + h + ",";
  if (s_gpsFix) {
    std::string pos = simGrausNmea(p.lat, true) + (p.lat < 0 ? ",S," : ",N,") + simGrausNmea(p.lon, false) + (p.lon < 0 ? ",W" : ",E");
    gga += pos + ",1,08,1.0,700.0,M,-5.0,M,,";
    char vr[48]; snprintf(vr, sizeof(vr), ",%.2f,%.1f,011026,,,A", p.velKmh / 1.852, p.rumoGraus);
    rmc += "A," + pos + vr;
  } else {
    gga += ",,,,0,00,99.99,,,,,,";
    rmc += "V,,,,,,,011026,,,N";
  }
  s_nmea.push_back({ bordaG + 120000ull, simNmeaFecha(gga) + simNmeaFecha(rmc) });
}

// Tudo que aconteceu no mundo ate o relogio deste no: TX que terminou, pacote
// que chegou, borda de PPS, frase de NMEA.
static void simEventos()
{
  const uint64_t g = simGlobal(s_us);
  if (s_chip.emTx && s_us >= s_chip.txFimLocal) {
    s_chip.emTx = false;
    s_chip.flags |= RADIOLIB_SX126X_IRQ_TX_DONE;
    g_loraIrqUs = s_chip.txFimLocal; g_loraIrq = true;
  }
  if (g > s_ultColetaG) {
    std::vector<ar::Rx> v;
    ar::coleta(s_no, g_rhw.ok && s_chip.energia, s_ultColetaG, g, v);
    s_ultColetaG = g;
    for (auto& r : v) s_filaRx.push_back(r);
  }
  if (!g_loraIrq && !s_chip.emTx && !s_filaRx.empty()) {
    ar::Rx r = s_filaRx.front(); s_filaRx.pop_front();
    s_chip.pktN = (int)r.b.size();
    memcpy(s_chip.pkt, r.b.data(), r.b.size());
    s_chip.rssi = r.rssi; s_chip.snr = r.snr;
    s_chip.flags |= RADIOLIB_SX126X_IRQ_RX_DONE;
    g_loraIrqUs = simLocal(r.fimG); g_loraIrq = true;
  }
  while (s_proxSegG <= g) {
    const uint64_t b = s_proxSegG;
    s_proxSegG += 1000000ull;
    if (s_gpsFix && s_isrPps) { s_isrUs = simLocal(b); s_isrPps(); s_isrUs = 0; }
    simGeraNmea(b);
  }
  while (!s_nmea.empty() && s_nmea.front().quandoG <= g) {
    for (char c : s_nmea.front().txt) Serial1.q.push_back(c);
    s_nmea.pop_front();
  }
}

static void simAvanca(uint64_t us)
{
  const uint64_t fim = s_us + us;
  while (s_us < fim) {
    uint64_t passo = fim - s_us; if (passo > 1000) passo = 1000;
    s_us += passo;
    simEventos();
  }
}

static void simApi_boot(int no, uint64_t bootG, const char* nome, uint8_t slot, uint8_t cor, double ppm, const char* dir)
{
  s_no = no; s_bootG = bootG; s_ppm = ppm; s_us = 0;
  s_rand = 1234u + (uint32_t)no * 7919u;
  s_sdRaiz = dir;
  std::filesystem::create_directories(dir);
  if (!s_console) s_console = fopen((std::string(dir) + "/console.txt").c_str(), "w");
  if (s_nvs.empty()) {                       // a "fabrica": nome, slot e cor gravados uma vez
    Cfg c; cfgPadrao(c);
    strncpy(c.nome, nome, sizeof(c.nome) - 1); c.slot = slot; c.cor = cor;
    nvsGravaCfg(c, CFG_ESQUEMA);
  }
  s_ultColetaG = bootG;
  s_proxSegG = (bootG / 1000000ull + 1) * 1000000ull;
  Serial.printf("\n=== SIM no %d (%s) liga em t=%.3f s ===\n", no, nome, bootG / 1e6);
  appBootNucleo();
  sdMonta();
  appBootDiario();
  sessaoRecuperaNoBoot();
}

// Desligar e ligar: a RAM some (o que o boot nao zera, zera-se aqui), a NVS e a
// caixa-preta (RTC) ficam. Variaveis static DENTRO de funcoes do firmware nao
// tem como ser zeradas daqui - limitacao conhecida do simulador.
static void simApi_religa(uint64_t g)
{
  registroFecha();
  sdArquivoFecha();
  g_loraIrq = false; g_loraIrqUs = 0;
  s_chip.emTx = false; s_chip.flags = 0;
  s_filaRx.clear(); s_nmea.clear(); Serial1.q.clear();
  memset(&g_gps, 0, sizeof(g_gps)); g_ppsUs = 0; g_ppsCnt = 0;
  g_relogio = Relogio{ 0, 0, 0 };
  g_caixaPronta = false;
  avisoDefine("");
  s_isrPps = nullptr; s_naTrilha = false;
  s_motivo = ESP_RST_POWERON;
  char nome[ROSTER_NOME_N + 1]; strncpy(nome, g_cfg.nome, sizeof(nome) - 1); nome[sizeof(nome) - 1] = 0;
  simApi_boot(s_no, g, nome, g_cfg.slot, g_cfg.cor, s_ppm, s_sdRaiz.c_str());
}

static uint64_t simApi_agoraG() { return simGlobal(s_us); }

static void simApi_passo()
{
  simEventos();
  servico();
  simAvanca(SIM_LACO_US);
  if (s_naTrilha && millis() - s_ultPintaMs >= 1000 && servicoCabe(SIM_MAPA_US)) {
    s_ultPintaMs = millis();
    caixaEtapa(CP_MAPA_PINTA);
    simAvanca(SIM_MAPA_US);
    caixaEtapa(CP_LACO);
  }
}

static void simApi_energiaRadio(bool liga) { s_chip.energia = liga; if (!liga) g_rhw.ok = false; }
static void simApi_gpsFix(bool f) { s_gpsFix = f; }
static void simApi_criar(const char* cod) { strncpy(s_salaCod, cod, sizeof(s_salaCod) - 1); sessaoCriaSala(cod); }
static void simApi_abrir()
{
  char nome[GRUPO_NOME_MAX]; snprintf(nome, sizeof(nome), "Grupo do %s", g_cfg.nome);
  sessaoAbreComoLider(nome, s_salaCod);
  sessaoComecaTrilha(); s_naTrilha = true;
}
static bool simApi_entrarLista()
{
  int idx[4]; int n = vizinhosOrdena(g_viz, idx, 3);
  if (n <= 0) return false;
  const GrupoNoAr& gr = g_viz.g[idx[0]];
  char cod[8], nome[GRUPO_NOME_MAX];
  snprintf(cod, sizeof(cod), "#%04X", (unsigned)gr.sala);
  if (gr.temNome) snprintf(nome, sizeof(nome), "Grupo de %s", gr.nome); else snprintf(nome, sizeof(nome), "Grupo %04X", (unsigned)gr.sala);
  sessaoEntra(cod, nome);
  sessaoComecaTrilha(); s_naTrilha = true;
  return true;
}
static void simApi_entrarCodigo(const char* cod) { sessaoEntra(cod, ""); sessaoComecaTrilha(); s_naTrilha = true; }
static void simApi_sair() { sessaoEncerra(); s_naTrilha = false; }
static bool simApi_retomar() { if (!g_ses.ativa) return false; sessaoComecaTrilha(); s_naTrilha = true; return true; }

static void simApi_obs(NoObs& o)
{
  memset(&o, 0, sizeof(o));
  const uint32_t agora = millis();
  o.carros = frotaQuantos(g_frota);
  int li = frotaLider(g_frota);
  o.liderNaFrota = li > 0;
  if (li > 0) { double la, lo; bool vz = false; o.liderDesenhado = frotaParaDesenhar(g_frota.c[li], agora, la, lo, vz); o.liderVazado = vz; }
  o.rotaN = g_rotaC.n;
  for (int i = 1; i < g_rotaC.n; i++)
    if (rotaVaoAntes(g_rotaC, i) && !(g_rotaC.temPiso && g_rotaC.p[i].vseq <= g_rotaC.pisoV)) o.rotaVaos++;
  o.ack = rotaAck16(g_rotaC); o.topoRota = rotaTopo16(g_rotaC);
  o.trajHead = g_traj.seqUlt; o.trajN = g_traj.n;
  o.rx = g_rede.rxOk; o.tx = g_rede.tdma.txCount; o.txPreso = g_rede.txPreso; o.txPerdidos = g_rede.tdma.missedTx;
  o.sync = (int)g_rede.tdma.sync; o.ancora = g_rede.tdma.isAnchor; o.holdover = g_rede.tdma.holdover;
  o.fix = g_eu.fix;
  int idx[4]; o.vizN = vizinhosOrdena(g_viz, idx, 4);
  if (o.vizN > 0) {
    const GrupoNoAr& gr = g_viz.g[idx[0]];
    strncpy(o.vizNome, gr.temNome ? gr.nome : "", sizeof(o.vizNome) - 1);
    o.vizLider = gr.temLider; o.vizSala = gr.sala;
  }
  o.radioOk = g_rede.ok; o.naTrilha = s_naTrilha; o.lider = g_frota.c[0].lider;
  o.sala = g_rede.sala;
}
static void simApi_fecha() { registroFecha(); sdArquivoFecha(); if (s_console) { fclose(s_console); s_console = nullptr; } }
