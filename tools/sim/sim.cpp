// ============================================================================
//  SIM - cenarios de trilha com 2-3 telas rodando o firmware/mts DE VERDADE.
// ============================================================================
//
//   docker ... sh tools/sim/build.sh todos         (ou: .\tools\sim\build.ps1 -Roda)
//   ... sh tools/sim/build.sh grupo_lista          (um cenario so)
//
// Cada cenario encena o que acontece na trilha (ligar, criar, entrar, andar,
// perder sinal, reiniciar) e confere, SEGUNDO A SEGUNDO, o que cada tela
// mostraria - pelas mesmas funcoes que o mapa usa (frotaParaDesenhar, rota). O
// diario de cada tela sai em tools/sim/out/<cenario>/<no>/log/, no formato do
// cartao: tools/campo/resumo_mts.py e cruza_mts.py leem igual leem o campo.
//
// POR QUE EXISTE: em 2026-09-30 um dia de teste foi perdido por um bug (o icone
// do lider sumia na tela do seguidor, core/tempo.h) que passou por todos os
// testes - eles testavam pedacos; nenhum ligava duas telas e as deixava
// conversar. O cenario grupo_lista pega aquele bug no primeiro minuto.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include "ar.h"
#include "no_api.h"

extern NoApi api_no0, api_no1, api_no2, api_no3;
static NoApi* NOS[4] = { &api_no0, &api_no1, &api_no2, &api_no3 };

// ------------------------------------------------------------------ placar
static int g_pass = 0, g_fail = 0;
static std::vector<std::string> g_falhas;
static std::string g_cen;
static void ck(bool c, const std::string& nome, const std::string& info = "")
{
  if (c) { g_pass++; printf("  PASS %s%s%s\n", nome.c_str(), info.empty() ? "" : "   ", info.c_str()); }
  else { g_fail++; printf("  FAIL %s   %s\n", nome.c_str(), info.c_str()); g_falhas.push_back(g_cen + ": " + nome + "  " + info); }
}
static std::string fmt(const char* f, ...)
{
  char b[512]; va_list a; va_start(a, f); vsnprintf(b, sizeof(b), f, a); va_end(a); return b;
}

// ------------------------------------------------------------------ o mundo
// Uma estrada para leste com curvas suaves. Cada carro esta a 's' metros do
// inicio dela, dado por uma funcao do tempo (segundos) definida no cenario.
static const double LAT0 = -23.150, LON0 = -46.550;
static std::function<double(double)> g_s[4];

static void estradaXY(double s, double& x, double& y) { x = s; y = 40.0 * std::sin(s / 250.0); }
static ar::Pose pose(int no, uint64_t tG)
{
  const double t = tG / 1e6;
  const double s = g_s[no] ? g_s[no](t) : 0.0;
  const double s2 = g_s[no] ? g_s[no](t + 1.0) : 0.0;
  double x, y, x2, y2; estradaXY(s, x, y); estradaXY(s2 > s ? s2 : s + 0.01, x2, y2);
  ar::Pose p;
  p.lat = LAT0 + y / 111320.0;
  p.lon = LON0 + x / (111320.0 * std::cos(LAT0 * M_PI / 180.0));
  double rumo = std::atan2(x2 - x, y2 - y) * 180.0 / M_PI; if (rumo < 0) rumo += 360.0;
  p.rumoGraus = rumo;
  p.velKmh = std::fabs(s2 - s) * 3.6;
  return p;
}
// Andar a v km/h a partir de t0, saindo de s0 (parado antes de t0).
static std::function<double(double)> anda(double t0, double s0, double kmh)
{
  return [=](double t) { return t <= t0 ? s0 : s0 + (t - t0) * kmh / 3.6; };
}

// ------------------------------------------------------------------ o relogio do mundo
struct Evento { double t; std::function<void()> f; bool feito = false; };
static std::vector<Evento> g_ev;
static bool g_ligado[4];
static int g_nNos = 2;

static void em(double t, std::function<void()> f) { g_ev.push_back({ t, f }); }

// Roda o mundo ate tFim (s). 'porSegundo' e chamado a cada segundo inteiro.
static void roda(double tFim, std::function<void(double)> porSegundo)
{
  double proxSeg = 0;
  for (;;) {
    int k = -1; uint64_t gMin = UINT64_MAX;
    for (int i = 0; i < g_nNos; i++)
      if (g_ligado[i]) { uint64_t g = NOS[i]->agoraG(); if (g < gMin) { gMin = g; k = i; } }
    const double tAgora = (k < 0) ? proxSeg : gMin / 1e6;
    while (proxSeg <= tAgora) {
      for (auto& e : g_ev) if (!e.feito && e.t <= proxSeg) { e.feito = true; e.f(); }
      if (proxSeg > 0) porSegundo(proxSeg);
      proxSeg += 1.0;
      if (k < 0) break;
    }
    if (tAgora >= tFim) break;
    if (k < 0) continue;
    NOS[k]->passo();
    static uint32_t n = 0;
    if ((++n & 0xFFF) == 0) ar::limpaVelhos(gMin);
  }
}

static void liga(int no, double t, const char* nome, uint8_t slot, uint8_t cor, double ppm)
{
  em(t, [=]() {
    std::string dir = g_cen + "/" + nome;          // o sim roda DE DENTRO de tools/sim/out
    std::filesystem::create_directories(dir);
    NOS[no]->boot(no, (uint64_t)(t * 1e6), nome, slot, cor, ppm, dir.c_str());
    g_ligado[no] = true;
  });
}
static NoObs obs(int no) { NoObs o; NOS[no]->obs(o); return o; }

static void comeca(const char* nome, int nNos)
{
  g_cen = nome; g_nNos = nNos; g_ev.clear();
  for (int i = 0; i < 4; i++) { g_ligado[i] = false; g_s[i] = nullptr; }
  ar::cfg = ar::Cfg{};
  for (auto& e : ar::est) e = ar::Est{};
  std::filesystem::remove_all(nome);
  printf("\n=== %s ===\n", nome);
}
static void resumoAr(int nNos, const char** nomes)
{
  for (int i = 0; i < nNos; i++) {
    NoObs o; NOS[i]->obs(o);
    printf("  tela | %-8s sala %04X %s | carros %d | lider na frota %d desenhado %d%s | rota %d pts %d vaos ack %u | rx %u tx %u | sync %d%s | radio %d\n",
           nomes[i], o.sala, o.lider ? "LIDER" : "segue", o.carros, (int)o.liderNaFrota, (int)o.liderDesenhado,
           o.liderVazado ? " (vazado)" : "", o.rotaN, o.rotaVaos, o.ack, o.rx, o.tx, o.sync, o.holdover ? " HOLDOVER" : "", (int)o.radioOk);
  }
  for (int i = 0; i < nNos; i++)
    printf("  ar | %-8s tx %llu | recebeu %llu | perdeu: alcance %llu, colisao %llu, estava transmitindo %llu, radio desligado %llu\n",
           nomes[i], (unsigned long long)ar::est[i].tx, (unsigned long long)ar::est[i].entregues,
           (unsigned long long)ar::est[i].alcance, (unsigned long long)ar::est[i].colisao,
           (unsigned long long)ar::est[i].surdoTx, (unsigned long long)ar::est[i].desligado);
}

// Contador de "quantos segundos a condicao valeu" numa janela.
struct Taxa { int ok = 0, n = 0; void conta(bool c) { n++; if (c) ok++; } double pct() const { return n ? 100.0 * ok / n : 0; } };

// ============================================================ CENARIOS
// 1) O FLUXO NORMAL, e o bug de 2026-09-30: lider cria, seguidor entra PELA LISTA,
//    os dois andam 10 min. O icone do lider tem de estar na tela do seguidor.
static void cen_grupo_lista()
{
  comeca("grupo_lista", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, +3.0);
  liga(1, 2.3, "gino", 1, 1, -4.0);
  g_s[0] = anda(60, 80, 30); g_s[1] = anda(60, 0, 30);         // gino 80 m atras
  em(15, []() { NOS[0]->gpsFix(true); });
  em(18, []() { NOS[1]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("13520"); });
  bool entrou = false; NoObs naLista{};
  em(52, [&]() { naLista = obs(1); entrou = NOS[1]->entrarLista(); });
  em(55, []() { NOS[0]->abrir(); });
  Taxa icone, carrosG, carrosF;
  roda(660, [&](double t) {
    if (t < 90) return;
    NoObs g = obs(1), f = obs(0);
    icone.conta(g.liderDesenhado); carrosG.conta(g.carros == 2); carrosF.conta(f.carros == 2);
  });
  NoObs g = obs(1), f = obs(0);
  ck(naLista.vizN >= 1 && naLista.vizLider, "ENTRAR: o grupo do lider aparece na lista, marcado LIDER NO AR",
     fmt("vizN=%d lider=%d", naLista.vizN, (int)naLista.vizLider));
  ck(strcmp(naLista.vizNome, "FaustoA") == 0, "ENTRAR: o cartao mostra o nome do lider", fmt("nome='%s'", naLista.vizNome));
  ck(entrou && g.sala == f.sala, "o toque no cartao poe o seguidor NA MESMA SALA do lider", fmt("sala %04X x %04X", g.sala, f.sala));
  // A 80 m, com sinal bom, o lider NUNCA pode sumir - nem por um segundo. (Com o
  // bug de 2026-09-30 recolocado, isto da ~96 %: um limite de 97 % pegava por um fio.)
  ck(icone.ok == icone.n, "o ICONE do lider esta na tela do seguidor TODOS os segundos", fmt("%d de %d s", icone.ok, icone.n));
  ck(carrosG.ok == carrosG.n, "a lista do seguidor tem 2 carros TODOS os segundos", fmt("%d de %d s", carrosG.ok, carrosG.n));
  ck(carrosF.ok == carrosF.n, "a lista do lider tem 2 carros TODOS os segundos", fmt("%d de %d s", carrosF.ok, carrosF.n));
  ck(g.rotaN > 50 && g.rotaVaos == 0, "o caminho do lider esta inteiro na tela do seguidor (sem vao)", fmt("rotaN=%d vaos=%d", g.rotaN, g.rotaVaos));
  ck((int16_t)(f.trajHead - g.ack) <= 12, "o seguidor esta em dia com o caminho do lider", fmt("head %u ack %u", f.trajHead, g.ack));
  ck(f.txPreso == 0 && g.txPreso == 0, "nenhum TX preso", fmt("%u/%u", f.txPreso, g.txPreso));
  ck(f.sync == 2 && g.sync == 2, "as duas telas na grade do GPS (PPS)", fmt("sync %d/%d", f.sync, g.sync));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 2) PERDER O SINAL E VOLTAR: o seguidor para por 3 min (o lider some a mais de
//    1 km), depois corre e alcanca. O vao tem de fechar (catch-up) e o icone voltar.
static void cen_perde_e_volta()
{
  comeca("perde_e_volta", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, +3.0);
  liga(1, 1.7, "gino", 1, 1, -2.0);
  g_s[0] = anda(60, 80, 30);
  g_s[1] = [](double t) {
    if (t <= 60) return 0.0;
    if (t <= 200) return (t - 60) * 30 / 3.6;               // junto
    const double s200 = 140 * 30 / 3.6;
    if (t <= 380) return s200;                              // PARADO 3 min: o lider some
    const double lider = 80 + (t - 60) * 30 / 3.6;
    double s = s200 + (t - 380) * 60 / 3.6;                 // corre a 60 ate encostar
    return s < lider - 80 ? s : lider - 80;
  };
  em(12, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("40404"); });
  em(48, []() { NOS[1]->entrarLista(); });
  em(50, []() { NOS[0]->abrir(); });
  int vaosNoMeio = 0; double voltou = -1; Taxa iconeFim;
  roda(1080, [&](double t) {
    NoObs g = obs(1);
    if (t > 300 && t < 400 && g.rotaVaos > vaosNoMeio) vaosNoMeio = g.rotaVaos;
    if (t > 380 && voltou < 0 && g.liderDesenhado && !g.liderVazado) voltou = t;
    if (t > 900) iconeFim.conta(g.liderDesenhado);
  });
  NoObs g = obs(1), f = obs(0);
  ck(ar::est[1].alcance > 10, "o cenario de fato tirou o seguidor do alcance", fmt("perdidos por alcance: %llu", (unsigned long long)ar::est[1].alcance));
  ck(voltou > 0, "o icone do lider VOLTA quando o sinal volta", fmt("voltou em t=%.0f s", voltou));
  ck(g.rotaVaos == 0, "o vao do caminho FECHOU (catch-up)", fmt("vaos=%d (no pior momento %d), rotaN=%d", g.rotaVaos, vaosNoMeio, g.rotaN));
  ck((int16_t)(f.trajHead - g.ack) <= 12, "o seguidor terminou em dia com o caminho", fmt("head %u ack %u", f.trajHead, g.ack));
  ck(iconeFim.pct() >= 97.0, "no fim, juntos de novo, icone >= 97%", fmt("%.1f%%", iconeFim.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 3) O LIDER REINICIA no meio da trilha (WDT, queda de energia) e toca RETOMAR.
//    A epoca muda; o seguidor nao pode travar no vao que ninguem mais tem.
static void cen_lider_reinicia()
{
  comeca("lider_reinicia", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, +1.0);
  liga(1, 3.1, "gino", 1, 1, -3.0);
  g_s[0] = anda(60, 80, 25); g_s[1] = anda(60, 0, 25);
  em(12, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("31415"); });
  em(48, []() { NOS[1]->entrarLista(); });
  em(50, []() { NOS[0]->abrir(); });
  em(300, []() { NOS[0]->religa(300 * 1000000ull); });
  em(301, []() { NOS[0]->gpsFix(true); });
  bool retomou = false;
  em(330, [&]() { retomou = NOS[0]->retomar(); });
  Taxa icone;
  roda(900, [&](double t) { if (t > 420) icone.conta(obs(1).liderDesenhado); });
  NoObs g = obs(1), f = obs(0);
  ck(retomou, "o lider religado oferece RETOMAR e volta para a trilha");
  ck(f.lider && f.sala == g.sala, "o lider volta como lider, na mesma sala", fmt("lider=%d sala %04X x %04X", (int)f.lider, f.sala, g.sala));
  ck(icone.pct() >= 97.0, "depois do reinicio o icone do lider volta e fica", fmt("%.1f%%", icone.pct()));
  ck(g.rotaVaos == 0, "nenhum vao preso depois do reinicio (piso)", fmt("vaos=%d", g.rotaVaos));
  ck((int16_t)(f.trajHead - g.ack) <= 12, "o seguidor acompanha a numeracao NOVA do lider", fmt("head %u ack %u", f.trajHead, g.ack));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 4) O 5 V DO RADIO CHEGA DEPOIS DO BOOT (ordem da ignicao): o radio tem de
//    entrar sozinho, sem reiniciar a tela.
static void cen_radio_tarde()
{
  comeca("radio_tarde", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, 0.0);
  em(0.5, []() { NOS[1]->energiaRadio(false); });
  liga(1, 1.0, "gino", 1, 1, 0.0);
  g_s[0] = anda(1e9, 60, 0); g_s[1] = anda(1e9, 0, 0);
  em(10, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  NoObs antes{};
  em(30, [&]() { antes = obs(1); NOS[0]->criar("27182"); NOS[0]->abrir(); });
  em(45, []() { NOS[1]->energiaRadio(true); });
  bool entrou = false;
  em(75, [&]() { entrou = NOS[1]->entrarLista(); });
  Taxa icone;
  roda(300, [&](double t) { if (t > 120) icone.conta(obs(1).liderDesenhado); });
  NoObs g = obs(1);
  ck(!antes.radioOk, "sem 5 V o radio do seguidor esta fora (e a tela segue)");
  ck(g.radioOk, "o radio ENTRA SOZINHO quando o 5 V chega");
  ck(entrou, "depois disso o seguidor ve o grupo e entra pela lista");
  ck(icone.pct() >= 97.0, "e o icone do lider aparece", fmt("%.1f%%", icone.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 5) OS DOIS TOCARAM EM CRIAR (o teste de 2026-09-30): cada um ve o outro na
//    lista; o seguidor sai do grupo dele e entra no do lider pela lista.
static void cen_grupos_trocados()
{
  comeca("grupos_trocados", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, 0.0);
  liga(1, 2.0, "gino", 1, 1, 0.0);
  g_s[0] = anda(1e9, 40, 0); g_s[1] = anda(1e9, 0, 0);
  em(10, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  em(20, []() { NOS[0]->criar("11111"); NOS[0]->abrir(); });
  em(22, []() { NOS[1]->criar("22222"); NOS[1]->abrir(); });
  NoObs vistoPeloGino{}, vistoPelaFausto{};
  em(80, [&]() { vistoPeloGino = obs(1); vistoPelaFausto = obs(0); });
  bool entrou = false;
  em(81, [&]() { NOS[1]->sair(); });
  em(84, [&]() { entrou = NOS[1]->entrarLista(); });
  Taxa icone;
  roda(240, [&](double t) { if (t > 120) icone.conta(obs(1).liderDesenhado); });
  ck(vistoPeloGino.carros == 1 && vistoPeloGino.vizN >= 1, "em grupos diferentes: ninguem na frota, mas o outro grupo na LISTA",
     fmt("carros=%d vizN=%d", vistoPeloGino.carros, vistoPeloGino.vizN));
  ck(strcmp(vistoPeloGino.vizNome, "FaustoA") == 0 && strcmp(vistoPelaFausto.vizNome, "gino") == 0,
     "cada um ve o NOME do outro na lista", fmt("'%s' / '%s'", vistoPeloGino.vizNome, vistoPelaFausto.vizNome));
  ck(entrou && obs(1).sala == obs(0).sala, "SAIR + ENTRAR pela lista junta os dois no mesmo grupo");
  ck(icone.pct() >= 97.0, "e o icone do lider aparece", fmt("%.1f%%", icone.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 6) TRES CARROS em fila, 100 m entre cada um.
static void cen_tres_carros()
{
  comeca("tres_carros", 3);
  const char* nomes[] = { "FaustoA", "gino", "tela_c" };
  liga(0, 0.0, "FaustoA", 0, 0, +2.0);
  liga(1, 1.4, "gino", 1, 1, -3.0);
  liga(2, 2.9, "tela_c", 2, 2, +5.0);
  g_s[0] = anda(70, 200, 30); g_s[1] = anda(70, 100, 30); g_s[2] = anda(70, 0, 30);
  em(14, []() { for (int i = 0; i < 3; i++) NOS[i]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("33333"); });
  bool e1 = false, e2 = false;
  em(52, [&]() { e1 = NOS[1]->entrarLista(); });
  em(56, [&]() { e2 = NOS[2]->entrarLista(); });
  em(60, []() { NOS[0]->abrir(); });
  Taxa i1, i2, c0;
  roda(600, [&](double t) {
    if (t < 100) return;
    i1.conta(obs(1).liderDesenhado); i2.conta(obs(2).liderDesenhado); c0.conta(obs(0).carros == 3);
  });
  NoObs a = obs(1), b = obs(2), f = obs(0);
  ck(e1 && e2, "os dois seguidores entram pela lista");
  ck(i1.pct() >= 97.0 && i2.pct() >= 97.0, "o icone do lider nas DUAS telas seguidoras", fmt("%.1f%% / %.1f%%", i1.pct(), i2.pct()));
  ck(c0.pct() >= 95.0, "o lider ve os 3 carros", fmt("%.1f%%", c0.pct()));
  ck(a.rotaVaos == 0 && b.rotaVaos == 0, "caminho inteiro nos dois seguidores", fmt("vaos %d / %d", a.rotaVaos, b.rotaVaos));
  ck(f.txPreso + a.txPreso + b.txPreso == 0, "nenhum TX preso");
  resumoAr(3, nomes);
  for (int i = 0; i < 3; i++) NOS[i]->fecha();
}

// ------------------------------------------------------------------ base comum
// Lider (no 0) e seguidor (no 1) prontos na trilha: ligam, pegam GPS, o lider
// cria, o seguidor entra pela lista, o lider abre. Os dois andando a 'kmh' a
// partir de t=60, o seguidor 'atras' metros atras.
static void basePar(double kmh, double atras)
{
  liga(0, 0.0, "FaustoA", 0, 0, +3.0);
  liga(1, 2.3, "gino", 1, 1, -4.0);
  g_s[0] = anda(60, atras, kmh); g_s[1] = anda(60, 0, kmh);
  em(12, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("13520"); });
  em(52, []() { NOS[1]->entrarLista(); });
  em(55, []() { NOS[0]->abrir(); });
}

// 7) O GPS DO LIDER CAI 60 s (mata fechada, tunel). O seguidor nao pode perder o
//    lider de vista: o icone fica no ultimo ponto conhecido (vazado se demorar).
static void cen_gps_perde()
{
  comeca("gps_perde", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  em(300, []() { NOS[0]->gpsFix(false); });
  em(360, []() { NOS[0]->gpsFix(true); });
  Taxa durante, depois; double volta = -1;
  uint32_t txAnt = 0; double tUltTx = 0, maiorVaoTx = 0;
  roda(600, [&](double t) {
    NoObs g = obs(1);
    NoObs fl = obs(0);
    if (t > 120) { if (fl.tx != txAnt) { if (tUltTx > 0 && t - tUltTx > maiorVaoTx) maiorVaoTx = t - tUltTx; tUltTx = t; txAnt = fl.tx; } }
    else txAnt = fl.tx;
    if (t > 302 && t < 360) durante.conta(g.liderDesenhado);
    if (t > 360 && volta < 0 && g.liderDesenhado && !g.liderVazado) volta = t;
    if (t > 380) depois.conta(g.liderDesenhado);
  });
  NoObs f = obs(0), g = obs(1);
  ck(durante.pct() >= 95.0, "com o GPS do lider fora, o icone dele CONTINUA na tela (ultimo ponto)", fmt("%.1f%% dos %d s", durante.pct(), durante.n));
  ck(volta > 0 && volta - 360 <= 15, "quando o GPS volta, o icone volta firme em ate 15 s", fmt("voltou em t=%.0f", volta));
  ck(depois.pct() >= 99.0, "depois, icone todos os segundos", fmt("%.1f%%", depois.pct()));
  // O contador 'perdidas' do tdma_core sobe quando a ancora muda de fase (o lider
  // passa a seguir o relogio do seguidor, que tem PPS, e depois volta) SEM que um
  // TX tenha faltado - o que importa e medido aqui: buraco entre transmissoes.
  ck(maiorVaoTx <= 7.0, "o lider transmitiu todo quadro sem o PPS (sem buraco > 7 s)", fmt("maior intervalo %.1f s", maiorVaoTx));
  ck(g.rotaVaos == 0, "o caminho continua sem vao", fmt("vaos %d", g.rotaVaos));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 8) ALERTA: quem aperta tem de aparecer em alerta na tela do outro em ate dois
//    quadros (12 s), e o cancelamento tambem.
static void cen_alerta()
{
  comeca("alerta", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(25, 80);
  em(200, []() { NOS[1]->alerta(true); });
  em(260, []() { NOS[1]->alerta(false); });
  em(320, []() { NOS[0]->alerta(true); });
  em(380, []() { NOS[0]->alerta(false); });
  double liderViu = -1, liderViuFim = -1, segViu = -1, segViuFim = -1;
  roda(450, [&](double t) {
    NoObs f = obs(0), g = obs(1);
    if (t >= 200 && liderViu < 0 && f.alertasVistos > 0) liderViu = t;
    if (t >= 260 && liderViuFim < 0 && f.alertasVistos == 0) liderViuFim = t;
    if (t >= 320 && segViu < 0 && g.alertasVistos > 0) segViu = t;
    if (t >= 380 && segViuFim < 0 && g.alertasVistos == 0) segViuFim = t;
  });
  ck(liderViu > 0 && liderViu - 200 <= 13, "o ALERTA do seguidor chega ao lider em ate 13 s", fmt("em %.0f s", liderViu - 200));
  ck(liderViuFim > 0 && liderViuFim - 260 <= 13, "o cancelamento chega ao lider em ate 13 s", fmt("em %.0f s", liderViuFim - 260));
  ck(segViu > 0 && segViu - 320 <= 13, "o ALERTA do lider chega ao seguidor em ate 13 s", fmt("em %.0f s", segViu - 320));
  ck(segViuFim > 0 && segViuFim - 380 <= 13, "o cancelamento chega ao seguidor em ate 13 s", fmt("em %.0f s", segViuFim - 380));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 9) TRILHA LONGA: 40 min a 40 km/h (~27 km). O rastro do lider (1200 pontos) e
//    a rota do seguidor (1400) enchem e passam a girar.
static void cen_trilha_longa()
{
  comeca("trilha_longa", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(40, 80);
  Taxa icone; int piorAtraso = 0;
  roda(60 + 40 * 60, [&](double t) {
    if (t > 90) icone.conta(obs(1).liderDesenhado);
    // o atraso do ACK (o caminho continuo) DURANTE o percurso, nao so no fim: um
    // buraco do comeco que so some quando a rota gira e empurra ele para fora
    // passava despercebido (prefixo parado 20 min no ponto 20)
    if (t > 300 && ((int)t % 30) == 0) { int a = (int16_t)(obs(0).trajHead - obs(1).ack); if (a > piorAtraso) piorAtraso = a; }
  });
  NoObs f = obs(0), g = obs(1);
  ck(piorAtraso <= 40, "o caminho continuo nunca fica mais de 40 pontos atras do lider DURANTE o percurso", fmt("pior atraso %d pontos", piorAtraso));
  ck(f.trajN >= 1200, "o rastro do lider encheu (o teste chegou onde queria)", fmt("trajN %d", f.trajN));
  ck(g.rotaN >= 1390, "a rota do seguidor encheu e esta girando", fmt("rotaN %d", g.rotaN));
  ck(icone.ok == icone.n, "o icone do lider todos os segundos, 40 min", fmt("%d de %d s", icone.ok, icone.n));
  ck(g.rotaVaos == 0, "o caminho sem vao depois de girar", fmt("vaos %d", g.rotaVaos));
  ck((int16_t)(f.trajHead - g.ack) <= 12, "o seguidor em dia no fim", fmt("head %u ack %u", f.trajHead, g.ack));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 10) PERDA PESADA (o campo de 2026-09-28: 46-62 % de entrega): ruido de fundo
//     alto o bastante para perder ~metade a 80 m. Andando, o icone nao pode sumir
//     (no maximo vazado); parados no fim, o caminho tem de fechar.
static void cen_perda_pesada()
{
  comeca("perda_pesada", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(25, 80);
  g_s[0] = [](double t) { double tt = t < 600 ? t : 600; return tt <= 60 ? 80.0 : 80 + (tt - 60) * 25 / 3.6; };
  g_s[1] = [](double t) { double tt = t < 600 ? t : 600; return tt <= 60 ? 0.0 : (tt - 60) * 25 / 3.6; };
  em(100, []() { ar::cfg.ruidoDbm = -61.0; });
  Taxa icone, firme;
  roda(900, [&](double t) {
    if (t > 120 && t < 600) { NoObs g = obs(1); icone.conta(g.liderDesenhado); firme.conta(g.liderDesenhado && !g.liderVazado); }
  });
  NoObs f = obs(0), g = obs(1);
  uint64_t tot = ar::est[1].entregues + ar::est[1].alcance + ar::est[1].colisao;
  double entrega = tot ? 100.0 * ar::est[1].entregues / tot : 0;
  ck(entrega > 35 && entrega < 75, "o cenario reproduz a entrega do campo (~50 %)", fmt("entrega %.0f %%", entrega));
  ck(icone.pct() >= 98.0, "com metade dos pacotes perdidos, o icone do lider fica na tela", fmt("%.1f%% (firme %.1f%%)", icone.pct(), firme.pct()));
  ck(g.rotaVaos == 0, "parados no fim, o caminho FECHA", fmt("vaos %d", g.rotaVaos));
  ck((int16_t)(f.trajHead - g.ack) <= 12, "e o seguidor fica em dia", fmt("head %u ack %u", f.trajHead, g.ack));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 11) QUATRO CARROS em fila, 80 m entre cada um.
static void cen_quatro_carros()
{
  comeca("quatro_carros", 4);
  const char* nomes[] = { "FaustoA", "gino", "tela_c", "tela_d" };
  const double ppm[] = { +2, -3, +5, -6 };
  for (int i = 0; i < 4; i++) {
    liga(i, 0.9 * i, nomes[i], (uint8_t)i, (uint8_t)i, ppm[i]);
    g_s[i] = anda(80, 80.0 * (3 - i), 30);
  }
  em(14, []() { for (int i = 0; i < 4; i++) NOS[i]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("44444"); });
  bool e[4] = { true, false, false, false };
  em(52, [&]() { e[1] = NOS[1]->entrarLista(); });
  em(55, [&]() { e[2] = NOS[2]->entrarLista(); });
  em(58, [&]() { e[3] = NOS[3]->entrarLista(); });
  em(62, []() { NOS[0]->abrir(); });
  Taxa ic[4], c0;
  roda(600, [&](double t) {
    if (t < 110) return;
    for (int i = 1; i < 4; i++) ic[i].conta(obs(i).liderDesenhado);
    c0.conta(obs(0).carros == 4);
  });
  ck(e[1] && e[2] && e[3], "os tres seguidores entram pela lista");
  ck(ic[1].ok == ic[1].n && ic[2].ok == ic[2].n && ic[3].ok == ic[3].n, "o icone do lider nas TRES telas seguidoras, todos os segundos",
     fmt("%.1f / %.1f / %.1f %%", ic[1].pct(), ic[2].pct(), ic[3].pct()));
  ck(c0.pct() >= 98.0, "o lider ve os 4 carros", fmt("%.1f%%", c0.pct()));
  int vaos = 0; for (int i = 1; i < 4; i++) vaos += obs(i).rotaVaos;
  ck(vaos == 0, "caminho inteiro nas tres seguidoras", fmt("vaos %d", vaos));
  resumoAr(4, nomes);
  for (int i = 0; i < 4; i++) NOS[i]->fecha();
}

// 12) 60 km/h COM UMA QUEDA DE 60 s. O catch-up foi dimensionado para 30 km/h
//     (params.h, VEL_PROJETO_KMH): a 60, o vao pode nao fechar andando. Ao reduzir
//     para 20 km/h ele tem de fechar.
static void cen_rapido()
{
  comeca("rapido", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(60, 100);
  auto perfil = [](double s0) {
    return [=](double t) {
      if (t <= 60) return s0;
      if (t <= 420) return s0 + (t - 60) * 60 / 3.6;
      return s0 + 360 * 60 / 3.6 + (t - 420) * 20 / 3.6;    // reduz para 20 km/h
    };
  };
  g_s[0] = perfil(100); g_s[1] = perfil(0);
  em(200, []() { NOS[1]->energiaRadio(false); });
  em(260, []() { NOS[1]->energiaRadio(true); });
  int vaosA60 = 0; double fechou = -1;
  roda(1000, [&](double t) {
    NoObs g = obs(1);
    if (t > 300 && t < 420 && g.rotaVaos > vaosA60) vaosA60 = g.rotaVaos;
    if (t > 270 && fechou < 0 && g.rotaVaos == 0 && g.rotaN > 50) fechou = t;
  });
  NoObs f = obs(0), g = obs(1);
  printf("  info: a 60 km/h o maior numero de vaos abertos foi %d; fechou em t=%.0f s\n", vaosA60, fechou);
  ck(g.rotaVaos == 0, "depois de reduzir para 20 km/h o caminho FECHA", fmt("vaos %d", g.rotaVaos));
  ck((int16_t)(f.trajHead - g.ack) <= 12, "e o seguidor fica em dia", fmt("head %u ack %u", f.trajHead, g.ack));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 13) ENTRA TARDE: o lider ja andou 5 km quando o seguidor entra. O caminho a
//     frente aparece logo; o historico chega de tras para a frente.
static void cen_entra_tarde()
{
  comeca("entra_tarde", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, +3.0);
  liga(1, 2.3, "gino", 1, 1, -4.0);
  // o lider sai do km 0 a 30 km/h; o seguidor ESPERA no km 6 e, quando o lider
  // passa por ele, vai atras a 30 km/h (80 m atras)
  g_s[0] = anda(60, 0, 30);
  const double tPassa = 60 + 6000 / (30 / 3.6);                 // ~780 s: o lider chega ao km 6
  g_s[1] = [=](double t) { return t <= tPassa + 10 ? 6000.0 : 6000 + (t - tPassa - 10) * 30 / 3.6; };
  em(12, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("55555"); NOS[0]->abrir(); });
  bool entrou = false;
  em(tPassa - 10, [&]() { entrou = NOS[1]->entrarLista(); });   // o lider ~80 m antes de chegar
  Taxa icone; double frenteOk = -1;
  roda(tPassa + 1500, [&](double t) {
    if (t < tPassa + 20) return;
    NoObs g = obs(1);
    icone.conta(g.liderDesenhado);
    if (frenteOk < 0 && (int16_t)(obs(0).trajHead - g.topoRota) <= 12) frenteOk = t;
  });
  NoObs f = obs(0), g = obs(1);
  ck(entrou, "quem espera mais a frente ve o grupo chegando e entra pela lista");
  ck(icone.ok == icone.n, "o icone do lider todos os segundos depois de entrar", fmt("%d de %d s", icone.ok, icone.n));
  ck(frenteOk > 0 && frenteOk - tPassa <= 30, "o caminho RECENTE do lider aparece em ate 30 s", fmt("em %.0f s", frenteOk - tPassa));
  ck(g.rotaN >= 590 && g.rotaVaos == 0, "o historico de ~6 km chega inteiro, sem vao", fmt("rotaN %d vaos %d", g.rotaN, g.rotaVaos));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// ============================================================ main
int main(int argc, char** argv)
{
  ar::posicao = pose;
  struct { const char* nome; void (*f)(); } cens[] = {
    { "grupo_lista", cen_grupo_lista }, { "perde_e_volta", cen_perde_e_volta },
    { "lider_reinicia", cen_lider_reinicia }, { "radio_tarde", cen_radio_tarde },
    { "grupos_trocados", cen_grupos_trocados }, { "tres_carros", cen_tres_carros },
    { "gps_perde", cen_gps_perde }, { "alerta", cen_alerta }, { "trilha_longa", cen_trilha_longa },
    { "perda_pesada", cen_perda_pesada }, { "quatro_carros", cen_quatro_carros }, { "rapido", cen_rapido },
    { "entra_tarde", cen_entra_tarde },
  };
  // "todos": CADA CENARIO NUM PROCESSO NOVO. As telas simuladas sao o firmware
  // inteiro, com NVS, sessao gravada e variaveis static - rodar um cenario depois
  // do outro no mesmo processo era nao desligar as telas entre dois testes (e o
  // segundo cenario herdava a sessao do primeiro).
  if (argc < 2 || strcmp(argv[1], "todos") == 0) {
    int falhos = 0;
    std::vector<std::string> quais;
    for (auto& c : cens) {
      fflush(stdout);
      int rc = std::system((std::string("\"") + argv[0] + "\" " + c.nome).c_str());
      if (rc != 0) { falhos++; quais.push_back(c.nome); }
    }
    printf("\n==== %d cenarios, %d com falha%s", (int)(sizeof(cens) / sizeof(cens[0])), falhos, falhos ? ":" : "\n");
    for (auto& q : quais) printf(" %s", q.c_str());
    if (falhos) printf("\n");
    return falhos ? 1 : 0;
  }
  const char* so = argv[1];
  int rodados = 0;
  for (auto& c : cens) if (strcmp(so, c.nome) == 0) { c.f(); rodados++; }
  if (!rodados) { printf("cenario desconhecido: %s\n", so); return 2; }
  printf("\n%d PASS, %d FAIL\n", g_pass, g_fail);
  for (auto& f : g_falhas) printf("  FALHOU: %s\n", f.c_str());
  return g_fail ? 1 : 0;
}
