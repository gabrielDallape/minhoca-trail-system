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
#include <algorithm>
#include "ar.h"
#include "no_api.h"
#include "core/params.h"      // os numeros do firmware de verdade (so #defines, sem Arduino)

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
// 8 posicoes: 4 telas + "fantasmas" (transmissores que nao sao telas, ar::transmite direto)
static std::function<double(double)> g_s[8];

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
  for (int i = 0; i < 4; i++) g_ligado[i] = false;
  for (int i = 0; i < 8; i++) g_s[i] = nullptr;
  ar::cfg = ar::Cfg{};
  for (auto& e : ar::est) e = ar::Est{};
  for (auto& l : ar::enlace) for (auto& e : l) e = ar::Enlace{};
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

// 14) O DIO1 DO LIDER MORRE (a FaustoA na bancada de 2026-10-01): o chip termina o
//     TX e recebe pacotes, mas o pino nao avisa a placa. A tela tem de perceber,
//     passar a consultar o chip pelo SPI e continuar recebendo.
static void cen_dio1_morto()
{
  comeca("dio1_morto", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  em(150, []() { NOS[0]->dio1Morto(true); });
  Taxa carrosL, icone;
  roda(600, [&](double t) {
    if (t > 240) { carrosL.conta(obs(0).carros == 2); icone.conta(obs(1).liderDesenhado); }
  });
  NoObs f = obs(0), g = obs(1);
  ck(carrosL.pct() >= 97.0, "com o DIO1 morto, o lider CONTINUA recebendo (ve o seguidor)", fmt("%.1f%%", carrosL.pct()));
  ck(icone.ok == icone.n, "o seguidor continua vendo o lider todos os segundos", fmt("%d de %d s", icone.ok, icone.n));
  ck(g.rotaVaos == 0 && (int16_t)(f.trajHead - g.ack) <= 12, "o caminho segue inteiro e em dia", fmt("vaos %d head %u ack %u", g.rotaVaos, f.trajHead, g.ack));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// ============================================================ ESTRESSE (2026-10-08, v201)
// Cenarios mais duros que os de cima: 4 carros, perda calibrada com o campo de
// 09-30 (52-65 % de entrega a 250-400 m), dezenas de minutos a horas de trilha
// com velocidade variando e paradas. Medem o que o usuario sente - de quanto em
// quanto tempo a posicao do lider CHEGA no seguidor - lendo o diario (linhas R)
// que o firmware escreveu, nao um contador do simulador. Nao entram no "todos"
// (levam minutos): rodam com "estresse" ou pelo nome.
//
// Criterios (revisao de projeto): posicao do lider no seguidor p50 <= 2,5 s e
// p95 <= 6 s com entrega >= 80 % (<= 10 s a 60 %); conflitos 0; 'perdi' <= 1 %
// dos quadros; vao de 300 m fecha em 60 s a 30 km/h; 0 reinicios.

// ---- perfil de trilha: trechos de 15-45 km/h e paradas, repetindo
struct Trecho { double seg, kmh; };
static const Trecho PERFIL[] = { { 180, 40 }, { 30, 0 }, { 240, 25 }, { 120, 45 }, { 60, 0 }, { 300, 30 },
                                 { 120, 15 }, { 20, 0 }, { 200, 35 }, { 90, 20 }, { 45, 0 } };
static std::vector<double> g_perfil;   // s(t) do lider, uma amostra por segundo
static void perfilPrepara(double tParte, double tFim)
{
  const int NP = (int)(sizeof(PERFIL) / sizeof(PERFIL[0]));
  g_perfil.assign((size_t)tFim + 2, 0.0);
  double s = 0, resta = PERFIL[0].seg; int ip = 0;
  for (size_t t = 1; t < g_perfil.size(); t++) {
    if ((double)t > tParte) {
      s += PERFIL[ip].kmh / 3.6;
      if (--resta <= 0) { ip = (ip + 1) % NP; resta = PERFIL[ip].seg; }
    }
    g_perfil[t] = s;
  }
}
static double perfilS(double t)
{
  if (t <= 0 || g_perfil.empty()) return 0.0;
  size_t i = (size_t)t;
  if (i + 1 >= g_perfil.size()) return g_perfil.back();
  double f = t - (double)i;
  return g_perfil[i] * (1 - f) + g_perfil[i + 1] * f;
}
// Carro que passa por cada ponto 'atraso' s depois do lider - uma fila de
// verdade: a distancia e v x atraso, encolhe nas paradas e abre nas retas. Em
// tPara o lider para de vez e os outros encostam.
static std::function<double(double)> comboio(double atraso, double tPara)
{
  return [=](double t) { double tt = t - atraso; return perfilS(tt < tPara ? tt : tPara); };
}

// ---- o que o DIARIO de uma tela diz (as linhas que o firmware escreveu)
struct Met {
  std::vector<double> gaps;      // s entre pacotes de POSICAO do slot 0 (o lider)
  double p50 = 0, p95 = 0, max = 0, pctMaior8 = 0, congeladoPct = 0, vazadoPct = 0;
  int    holdovers = 0, assumi = 0, fontes = 0, arquivos = 0, caixas = 0;
  unsigned long tx = 0, perdi = 0, preso = 0, conflito = 0, relogioVoltou = 0, seqPerd = 0, rx = 0,
                orcEstouros = 0, lacoPiorUs = 0, versaoErrada = 0;
  int    sync = 0;
};
static std::vector<std::string> campos(const std::string& l)
{
  std::vector<std::string> v; std::string a;
  for (char c : l) { if (c == ',') { v.push_back(a); a.clear(); } else if (c != '\r' && c != '\n') a += c; }
  v.push_back(a); return v;
}
static double pct(const std::vector<double>& v, double p)
{
  if (v.empty()) return 0; size_t i = (size_t)(p * (v.size() - 1) + 0.5); return v[i < v.size() ? i : v.size() - 1];
}
static Met metricas(const char* tela, double desdeS, double ateS)
{
  Met m;
  std::vector<std::filesystem::path> arqs;
  std::string dir = g_cen + "/" + tela + "/log";
  if (std::filesystem::exists(dir))
    for (auto& e : std::filesystem::directory_iterator(dir)) arqs.push_back(e.path());
  std::sort(arqs.begin(), arqs.end());
  m.arquivos = (int)arqs.size();
  const double desdeMs = desdeS * 1000, ateMs = ateS * 1000;
  for (auto& a : arqs) {
    FILE* f = fopen(a.string().c_str(), "r"); if (!f) continue;
    char buf[1024]; double prev = -1; int holdAnt = 0;
    while (fgets(buf, sizeof(buf), f)) {
      if (buf[0] == '#') continue;
      auto c = campos(buf);
      if (c.size() < 2) continue;
      double ms = atof(c[1].c_str());
      if (c[0] == "C") m.caixas++;
      if (ms < desdeMs || ms > ateMs) { if (c[0] == "E" && c.size() > 18) holdAnt = atoi(c[18].c_str()); continue; }
      if (c[0] == "R" && c.size() >= 12 && c[3] == "0" && c[11] == "pos") {
        if (prev >= 0) m.gaps.push_back((ms - prev) / 1000.0);
        prev = ms;
      } else if (c[0] == "E" && c.size() >= 40) {
        int h = atoi(c[18].c_str()); if (h && !holdAnt) m.holdovers++; holdAnt = h;
        m.sync = atoi(c[6].c_str());
        m.tx = strtoul(c[8].c_str(), 0, 10); m.perdi = strtoul(c[9].c_str(), 0, 10); m.preso = strtoul(c[10].c_str(), 0, 10);
        m.rx = strtoul(c[11].c_str(), 0, 10); m.conflito = strtoul(c[13].c_str(), 0, 10);
        unsigned long lp = strtoul(c[19].c_str(), 0, 10); if (lp > m.lacoPiorUs) m.lacoPiorUs = lp;
        m.seqPerd = strtoul(c[21].c_str(), 0, 10); m.relogioVoltou = strtoul(c[29].c_str(), 0, 10);
        m.versaoErrada = strtoul(c[35].c_str(), 0, 10); m.orcEstouros = strtoul(c[37].c_str(), 0, 10);
      } else if (c[0] == "!") {
        if (strstr(buf, "assumi a ancora")) m.assumi++;
        if (strstr(buf, "fonte de tempo")) m.fontes++;
      }
    }
    fclose(f);
  }
  std::vector<double> o = m.gaps; std::sort(o.begin(), o.end());
  m.p50 = pct(o, 0.50); m.p95 = pct(o, 0.95); m.max = o.empty() ? 0 : o.back();
  int n8 = 0; double cong = 0, vaz = 0;
  for (double g : m.gaps) { if (g > 8) { n8++; cong += g - 8; } if (g > 20) vaz += g - 20; }
  m.pctMaior8 = m.gaps.empty() ? 0 : 100.0 * n8 / m.gaps.size();
  const double jan = ateS - desdeS;
  m.congeladoPct = jan > 0 ? 100.0 * cong / jan : 0;     // icone parado (extrapolacao esgotada)
  m.vazadoPct = jan > 0 ? 100.0 * vaz / jan : 0;          // icone vazado (sinal velho)
  return m;
}
static void imprimeMet(const char* tela, const Met& m, double entrega)
{
  printf("  %-7s entrega lider->eu %5.1f %% | posicao do lider a cada: p50 %.1f  p95 %.1f  max %.1f s | >8 s %.1f %% dos gaps"
         " | congelado %.1f %% vazado %.1f %% do tempo | holdover %d assumi %d fonte %d | tx %lu perdi %lu (%.2f %%) preso %lu conflito %lu"
         " | sync %d relogioVoltou %lu lacoPior %lu us\n",
         tela, entrega, m.p50, m.p95, m.max, m.pctMaior8, m.congeladoPct, m.vazadoPct, m.holdovers, m.assumi, m.fontes,
         m.tx, m.perdi, m.tx ? 100.0 * m.perdi / m.tx : 0, m.preso, m.conflito, m.sync, m.relogioVoltou, m.lacoPiorUs);
}

// Quatro carros ligam, pegam GPS, o lider cria, os tres entram pela lista, o
// lider abre. g_s[] fica por conta do cenario.
static const char* NOMES4[] = { "FaustoA", "gino", "tela_c", "tela_d" };
static void base4(const char* cod)
{
  const double ppm[] = { +2, -3, +5, -6 };
  for (int i = 0; i < 4; i++) liga(i, 0.9 * i, NOMES4[i], (uint8_t)i, (uint8_t)i, ppm[i]);
  em(14, []() { for (int i = 0; i < 4; i++) NOS[i]->gpsFix(true); });
  em(40, [=]() { NOS[0]->criar(cod); });
  em(52, []() { NOS[1]->entrarLista(); });
  em(55, []() { NOS[2]->entrarLista(); });
  em(58, []() { NOS[3]->entrarLista(); });
  em(62, []() { NOS[0]->abrir(); });
}
// Os criterios de posicao do lider, por seguidor, contra a entrega medida no enlace.
static void confereLider(int i, const Met& m, double entrega)
{
  const char* n = NOMES4[i];
  const double p95Max = entrega >= 80 ? 6.0 : 10.0;
  const char* faixa = entrega >= 80 ? ">= 80 %" : (entrega >= 60 ? ">= 60 %" : "< 60 % (fora do criterio; usa 10 s)");
  ck(m.p50 <= 2.5, fmt("%s: posicao do lider p50 <= 2,5 s", n), fmt("p50 %.1f s (entrega %.0f %%)", m.p50, entrega));
  ck(m.p95 <= p95Max, fmt("%s: p95 <= %.0f s na faixa de entrega %s", n, p95Max, faixa), fmt("p95 %.1f s, max %.1f s, >8 s %.1f %%", m.p95, m.max, m.pctMaior8));
  ck(m.conflito == 0, fmt("%s: 0 conflitos de slot", n), fmt("%lu", m.conflito));
  ck(m.tx > 0 && 100.0 * m.perdi / m.tx <= 1.0, fmt("%s: 'perdi' <= 1 %% dos quadros", n), fmt("%lu de %lu", m.perdi, m.tx));
  ck(m.preso == 0 && m.relogioVoltou == 0 && m.caixas == 0, fmt("%s: 0 TX preso, 0 relogio voltou, 0 reinicio violento", n),
     fmt("preso %lu voltou %lu caixas %d", m.preso, m.relogioVoltou, m.caixas));
}

// 15) COMBOIO DE 4 COM A PERDA DO CAMPO: 45 min, 15-45 km/h com paradas, cada
//     carro 12 s atras do anterior (a 30 km/h: 100/200/300 m; a 45: 150/300/450).
//     Ruido de fundo subido ate a entrega a 300 m ficar nos ~60 % de 09-30.
static void cen_comboio4_perda()
{
  comeca("comboio4_perda", 4);
  const double tPara = 90 + 45 * 60, tFim = tPara + 150;
  perfilPrepara(90, tFim);
  const double atr[] = { 0, 12, 24, 36 };
  for (int i = 0; i < 4; i++) g_s[i] = comboio(atr[i], tPara);
  base4("45454");
  em(100, []() { ar::cfg.ruidoDbm = -80.0; });
  Taxa ic[4], firme[4]; double distMax[4] = { 0, 0, 0, 0 };
  roda(tFim, [&](double t) {
    if (t < 150) return;
    for (int i = 1; i < 4; i++) {
      NoObs o = obs(i); ic[i].conta(o.liderDesenhado); firme[i].conta(o.liderDesenhado && !o.liderVazado);
      double d = ar::distanciaM(0, i, (uint64_t)(t * 1e6)); if (d > distMax[i]) distMax[i] = d;
    }
  });
  for (int i = 1; i < 4; i++) {
    Met m = metricas(NOMES4[i], 150, tPara);
    double e = ar::enlace[0][i].pct();
    imprimeMet(NOMES4[i], m, e);
    printf("  %-7s icone %.1f %% (firme %.1f %%) | distancia maxima ao lider %.0f m\n", NOMES4[i], ic[i].pct(), firme[i].pct(), distMax[i]);
    confereLider(i, m, e);
    ck(ic[i].pct() >= 97.0, fmt("%s: icone do lider na tela >= 97 %% do tempo", NOMES4[i]), fmt("%.1f %% (firme %.1f %%)", ic[i].pct(), firme[i].pct()));
    NoObs o = obs(i);
    ck(o.rotaVaos == 0 && (int16_t)(obs(0).trajHead - o.ack) <= 12, fmt("%s: parados no fim, caminho inteiro e em dia", NOMES4[i]),
       fmt("vaos %d head %u ack %u rotaN %d", o.rotaVaos, obs(0).trajHead, o.ack, o.rotaN));
  }
  ck(ar::enlace[0][3].pct() >= 45 && ar::enlace[0][3].pct() <= 75, "o cenario reproduz a entrega do campo no carro mais longe (45-75 %)",
     fmt("lider->tela_d %.0f %%, ->tela_c %.0f %%, ->gino %.0f %%", ar::enlace[0][3].pct(), ar::enlace[0][2].pct(), ar::enlace[0][1].pct()));
  resumoAr(4, NOMES4);
  for (int i = 0; i < 4; i++) NOS[i]->fecha();
}

// 16) O LIDER REINICIA E, DEPOIS, PASSA O BASTAO - com 4 carros e perda moderada
//     (300 m: ~85 %). Reinicio: RETOMAR e os tres tem de ver o icone voltar.
//     Bastao: o lider SAI; o gino cria um grupo novo e abre; os outros dois e o
//     ex-lider entram pela lista (e o unico caminho que a sessao oferece).
static void cen_lider_troca4()
{
  comeca("lider_troca4", 4);
  for (int i = 0; i < 4; i++) g_s[i] = anda(70, 100.0 * (3 - i), 30);
  base4("31313");
  em(100, []() { ar::cfg.ruidoDbm = -84.0; });
  em(300, []() { NOS[0]->religa(300 * 1000000ull); });
  em(301, []() { NOS[0]->gpsFix(true); });
  bool retomou = false;
  em(330, [&]() { retomou = NOS[0]->retomar(); });
  // o bastao
  em(600, []() { NOS[0]->sair(); });
  em(602, []() { NOS[1]->sair(); });
  em(604, []() { NOS[1]->criar("77777"); });
  em(606, []() { NOS[1]->abrir(); });
  em(615, []() { NOS[2]->sair(); NOS[3]->sair(); });
  bool e2 = false, e3 = false, e0 = false;
  em(630, [&]() { e2 = NOS[2]->entrarLista(); e3 = NOS[3]->entrarLista(); });
  em(640, [&]() { e0 = NOS[0]->entrarLista(); });
  double volta[4] = { 0, -1, -1, -1 }, novo[4] = { -1, 0, -1, -1 };
  Taxa icNovo[4], carros4[4];
  roda(1200, [&](double t) {
    for (int i = 1; i < 4; i++) { NoObs o = obs(i); if (t > 330 && t < 600 && volta[i] < 0 && o.liderDesenhado && !o.liderVazado) volta[i] = t; }
    if (t > 640) for (int i = 0; i < 4; i++) {
      if (i == 1) continue;
      NoObs o = obs(i);
      if (novo[i] < 0 && o.liderDesenhado && !o.liderVazado) novo[i] = t;
      if (t > 720) { icNovo[i].conta(o.liderDesenhado); carros4[i].conta(o.carros == 4); }
    }
  });
  ck(retomou, "o lider religado oferece RETOMAR");
  for (int i = 1; i < 4; i++)
    ck(volta[i] > 0 && volta[i] - 330 <= 60, fmt("%s: o icone do lider volta firme em ate 60 s apos o RETOMAR", NOMES4[i]), fmt("em %.0f s", volta[i] - 330));
  ck(e2 && e3 && e0, "tela_c, tela_d e o ex-lider entram no grupo novo pela lista", fmt("%d %d %d", (int)e2, (int)e3, (int)e0));
  NoObs g = obs(1);
  ck(g.lider && !obs(0).lider && obs(0).sala == g.sala && obs(2).sala == g.sala && obs(3).sala == g.sala,
     "gino e o lider e os quatro estao na sala dele", fmt("salas %04X %04X %04X %04X", obs(0).sala, g.sala, obs(2).sala, obs(3).sala));
  for (int i = 0; i < 4; i++) {
    if (i == 1) continue;
    ck(novo[i] > 0 && novo[i] - 640 <= 60, fmt("%s: ve o NOVO lider firme em ate 60 s", NOMES4[i]), fmt("em %.0f s", novo[i] - 640));
    ck(icNovo[i].pct() >= 97.0 && carros4[i].pct() >= 95.0, fmt("%s: depois, icone do novo lider >= 97 %% e 4 carros na lista", NOMES4[i]),
       fmt("icone %.1f %% carros %.1f %%", icNovo[i].pct(), carros4[i].pct()));
    NoObs o = obs(i);
    ck(o.rotaVaos == 0 && (int16_t)(g.trajHead - o.ack) <= 12, fmt("%s: caminho do novo lider inteiro e em dia", NOMES4[i]),
       fmt("vaos %d head %u ack %u rotaN %d", o.rotaVaos, g.trajHead, o.ack, o.rotaN));
  }
  for (int i = 0; i < 4; i++) { Met m = metricas(NOMES4[i], 100, 1200); ck(m.conflito == 0 && m.preso == 0, fmt("%s: 0 conflito, 0 TX preso", NOMES4[i]), fmt("%lu %lu", m.conflito, m.preso)); }
  resumoAr(4, NOMES4);
  for (int i = 0; i < 4; i++) NOS[i]->fecha();
}

// 17) VAO DE 300 m COM 4 CARROS E PERDA PESADA: a 30 km/h, 100 m entre carros
//     (300 m: ~60 % de entrega), cada seguidor fica 36 s sem radio (= 30 pontos
//     de caminho, 300 m). O vao tem de fechar em ate 60 s depois que o radio
//     volta - com os outros dois seguidores tambem pedindo bloco.
static void cen_vao300_4carros()
{
  comeca("vao300_4carros", 4);
  for (int i = 0; i < 4; i++) g_s[i] = anda(70, 100.0 * (3 - i), 30);
  base4("30303");
  em(100, []() { ar::cfg.ruidoDbm = -80.0; });
  const double tCorte[4] = { 0, 700, 1000, 300 };       // tela_d (300 m) primeiro, depois gino (100) e tela_c (200)
  for (int i = 1; i < 4; i++) {
    em(tCorte[i], [=]() { NOS[i]->energiaRadio(false); });
    em(tCorte[i] + 36, [=]() { NOS[i]->energiaRadio(true); });
  }
  double fechou[4] = { 0, -1, -1, -1 }; int vaosAbertos[4] = { 0, 0, 0, 0 };
  roda(1300, [&](double t) {
    for (int i = 1; i < 4; i++) {
      if (t <= tCorte[i] + 36) continue;
      NoObs o = obs(i);
      if (t < tCorte[i] + 60 && o.rotaVaos > vaosAbertos[i]) vaosAbertos[i] = o.rotaVaos;
      if (fechou[i] < 0 && t > tCorte[i] + 40 && o.rotaVaos == 0 && (int16_t)(obs(0).trajHead - o.topoRota) <= 12) fechou[i] = t;
    }
  });
  for (int i = 1; i < 4; i++) {
    double e = ar::enlace[0][i].pct();
    printf("  %-7s entrega lider->eu %.0f %% | vaos abertos ate %d | fechou %.0f s depois de o radio voltar\n",
           NOMES4[i], e, vaosAbertos[i], fechou[i] > 0 ? fechou[i] - (tCorte[i] + 36) : -1.0);
    ck(fechou[i] > 0 && fechou[i] - (tCorte[i] + 36) <= 60, fmt("%s: o vao de 300 m fecha em ate 60 s (entrega %.0f %%)", NOMES4[i], e),
       fmt("%.0f s", fechou[i] > 0 ? fechou[i] - (tCorte[i] + 36) : -1.0));
  }
  int vaos = 0; for (int i = 1; i < 4; i++) vaos += obs(i).rotaVaos;
  ck(vaos == 0, "no fim, nenhum vao em nenhum seguidor", fmt("%d", vaos));
  resumoAr(4, NOMES4);
  for (int i = 0; i < 4; i++) NOS[i]->fecha();
}

// 18) OS QUATRO SEM PPS (so beacon): 2 h de comboio com perda, 10 s entre carros.
//     Conta holdover, eleicao e troca de fonte por hora - a rede nao pode ficar
//     se reelegendo; conflito 0; 'perdi' <= 1 %. (Em 1 h deu 1 eleicao no carro
//     de tras: 24,5 s sem ouvir o lider a ~375 m. Duas horas para a taxa valer.)
static void cen_sem_pps4()
{
  comeca("sem_pps4", 4);
  const double tPara = 90 + 120 * 60, tFim = tPara + 120;
  perfilPrepara(90, tFim);
  const double atr[] = { 0, 10, 20, 30 };
  for (int i = 0; i < 4; i++) g_s[i] = comboio(atr[i], tPara);
  base4("90909");
  em(5, []() { for (int i = 0; i < 4; i++) NOS[i]->gpsPps(false); });
  em(100, []() { ar::cfg.ruidoDbm = -80.0; });
  Taxa ic[4];
  roda(tFim, [&](double t) { if (t > 150) for (int i = 1; i < 4; i++) ic[i].conta(obs(i).liderDesenhado); });
  const double horas = (tPara - 150) / 3600.0;
  for (int i = 0; i < 4; i++) {
    Met m = metricas(NOMES4[i], 150, tPara);
    double e = ar::enlace[0][i].pct();
    imprimeMet(NOMES4[i], m, e);
    ck(m.sync == 1, fmt("%s: sincronizado por BEACON o tempo todo (nunca PPS)", NOMES4[i]), fmt("sync %d", m.sync));
    ck((m.holdovers + m.assumi) / horas <= 1.0, fmt("%s: holdover + eleicao <= 1 por hora (entrega %.0f %%)", NOMES4[i], e),
       fmt("%d holdover, %d assumi em %.1f h (fonte trocou %d x)", m.holdovers, m.assumi, horas, m.fontes));
    ck(m.conflito == 0, fmt("%s: 0 conflitos", NOMES4[i]), fmt("%lu", m.conflito));
    ck(m.tx > 0 && 100.0 * m.perdi / m.tx <= 1.0, fmt("%s: 'perdi' <= 1 %%", NOMES4[i]), fmt("%lu de %lu", m.perdi, m.tx));
    if (i > 0) {
      ck(ic[i].pct() >= 97.0, fmt("%s: icone do lider >= 97 %%", NOMES4[i]), fmt("%.1f %%", ic[i].pct()));
      ck(m.p50 <= 2.5 && m.p95 <= (e >= 80 ? 6.0 : 10.0), fmt("%s: p50 <= 2,5 s e p95 dentro da faixa (entrega %.0f %%)", NOMES4[i], e),
         fmt("p50 %.1f p95 %.1f max %.1f", m.p50, m.p95, m.max));
    }
  }
  resumoAr(4, NOMES4);
  for (int i = 0; i < 4; i++) NOS[i]->fecha();
}

// 19) TRES HORAS DE COMBOIO com 4 carros e perda moderada: rastro (1200) e rota
//     (1400) giram varias vezes, contadores de 16 bits rolam, o relogio passa
//     dos 71 min do micros(). Nada pode derivar: a ultima hora tem de ser igual
//     a primeira.
static void cen_longa4()
{
  comeca("longa4", 4);
  const double H = 3.0, tPara = 90 + H * 3600, tFim = tPara + 180;
  perfilPrepara(90, tFim);
  const double atr[] = { 0, 10, 20, 30 };
  for (int i = 0; i < 4; i++) g_s[i] = comboio(atr[i], tPara);
  base4("12121");
  em(100, []() { ar::cfg.ruidoDbm = -84.0; });
  Taxa ic[4]; int piorAtraso = 0; Taxa c0;
  roda(tFim, [&](double t) {
    if (t < 150) return;
    for (int i = 1; i < 4; i++) ic[i].conta(obs(i).liderDesenhado);
    c0.conta(obs(0).carros == 4);
    if (((int)t % 60) == 0) for (int i = 1; i < 4; i++) { int a = (int16_t)(obs(0).trajHead - obs(i).ack); if (a > piorAtraso) piorAtraso = a; }
  });
  NoObs f = obs(0);
  ck(f.trajN >= 1200, "o rastro do lider encheu e girou", fmt("trajN %d head %u", f.trajN, f.trajHead));
  ck(c0.pct() >= 97.0, "o lider ve os 4 carros 3 h", fmt("%.1f %%", c0.pct()));
  for (int i = 0; i < 4; i++) {
    Met m1 = metricas(NOMES4[i], 150, 150 + 3600), m3 = metricas(NOMES4[i], tPara - 3600, tPara), mt = metricas(NOMES4[i], 150, tPara);
    double e = ar::enlace[0][i].pct();
    imprimeMet(NOMES4[i], mt, e);
    if (i > 0) printf("  %-7s 1a hora p50 %.1f p95 %.1f | 3a hora p50 %.1f p95 %.1f | icone %.1f %%\n", NOMES4[i], m1.p50, m1.p95, m3.p50, m3.p95, ic[i].pct());
    ck(mt.arquivos == 1 && mt.caixas == 0 && mt.relogioVoltou == 0 && mt.preso == 0, fmt("%s: 0 reinicio, 0 relogio voltou, 0 TX preso em 3 h", NOMES4[i]),
       fmt("arquivos %d caixas %d voltou %lu preso %lu", mt.arquivos, mt.caixas, mt.relogioVoltou, mt.preso));
    ck(mt.conflito == 0 && mt.versaoErrada == 0, fmt("%s: 0 conflito, 0 versao errada", NOMES4[i]), fmt("%lu %lu", mt.conflito, mt.versaoErrada));
    ck(mt.tx > 0 && 100.0 * mt.perdi / mt.tx <= 1.0, fmt("%s: 'perdi' <= 1 %% em 3 h", NOMES4[i]), fmt("%lu de %lu", mt.perdi, mt.tx));
    ck(mt.holdovers + mt.assumi <= 3, fmt("%s: holdover + eleicao <= 3 em 3 h", NOMES4[i]), fmt("%d + %d (fonte %d)", mt.holdovers, mt.assumi, mt.fontes));
    if (i > 0) {
      confereLider(i, mt, e);
      ck(m3.p95 <= m1.p95 + 2.0 && m3.p50 <= m1.p50 + 0.5, fmt("%s: a 3a hora nao e pior que a 1a (sem deriva)", NOMES4[i]),
         fmt("p95 %.1f -> %.1f, p50 %.1f -> %.1f", m1.p95, m3.p95, m1.p50, m3.p50));
      ck(ic[i].pct() >= 97.0, fmt("%s: icone do lider >= 97 %% em 3 h", NOMES4[i]), fmt("%.1f %%", ic[i].pct()));
      NoObs o = obs(i);
      ck(o.rotaN >= 1390 && o.rotaVaos == 0 && (int16_t)(f.trajHead - o.ack) <= 12, fmt("%s: rota cheia girando, sem vao, em dia no fim", NOMES4[i]),
         fmt("rotaN %d vaos %d head %u ack %u", o.rotaN, o.rotaVaos, f.trajHead, o.ack));
    }
  }
  ck(piorAtraso <= 40, "o caminho continuo nunca ficou > 40 pontos atras durante as 3 h", fmt("pior %d", piorAtraso));
  resumoAr(4, NOMES4);
  for (int i = 0; i < 4; i++) NOS[i]->fecha();
// ============================================================ REVISAO v201 (2026-10-08)
// Cenarios escritos para a revisao adversaria do quadro de 2 s x 4 slots. Cada
// um existe para provar (ou nao) uma suspeita lida no diff - os numeros vao
// para o relatorio da revisao.

// Le as linhas N (ruido) do diario de uma tela: quantas amostras cairam em
// [tIniMs, tFimMs) e quantas delas ficaram acima de 'limiarDbm' (= mediram o
// TX de alguem, nao o ruido de fundo).
static void contaRuido(const char* nomeTela, uint32_t tIniMs, uint32_t tFimMs, double limiarDbm, int& n, int& altas, double& maxDbm)
{
  n = 0; altas = 0; maxDbm = -200;
  std::string dir = g_cen + "/" + nomeTela + "/log";
  if (!std::filesystem::exists(dir)) return;
  for (auto& e : std::filesystem::directory_iterator(dir)) {
    FILE* f = fopen(e.path().string().c_str(), "r");
    if (!f) continue;
    char l[512];
    while (fgets(l, sizeof(l), f)) {
      unsigned long ms, utc; double v; int slot;
      if (sscanf(l, "N,%lu,%lu,%lf,%d", &ms, &utc, &v, &slot) != 4) continue;
      if (ms < tIniMs || ms >= tFimMs) continue;
      n++; if (v > limiarDbm) altas++; if (v > maxDbm) maxDbm = v;
    }
    fclose(f);
  }
}

// 15) PINTURA A 0,5 s (TELA_REPINTA_ANDANDO_MS) com quadros de 70 ms quentes e um
//     frio de 250 ms a cada 8 - o CUSTO PEDIDO ao orcamento e o real. Com a guarda
//     nova (orcamentoNaMinhaJanela) nenhum TX pode ser perdido pela pintura.
//     (250 e nao 300: um laco parado >= ~280 ms em cima da frase NMEA quebra o par
//     PPS - e outro efeito, medido no cenario pps_laco_300.)
static void cen_pintura_05s()
{
  comeca("pintura_05s", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  for (int i = 0; i < 2; i++) NOS[i]->pintura(500, 70000, 250000, 8, false);
  Taxa icone; uint32_t perdi90[2] = { 0, 0 };
  // o 'perdi' de ~25 s e de todo cenario: a troca da ancora propria para a grade UTC no boot
  roda(660, [&](double t) { if (t == 90) { perdi90[0] = obs(0).txPerdidos; perdi90[1] = obs(1).txPerdidos; } if (t > 90) icone.conta(obs(1).liderDesenhado); });
  NoObs f = obs(0), g = obs(1);
  const uint32_t pf = f.txPerdidos - perdi90[0], pg = g.txPerdidos - perdi90[1];
  printf("  info: pinturas %u/%u (negadas %u/%u) | orc negados %u/%u concedidos %u/%u | laco pior %lu/%lu us | perdi na trilha %u/%u (no boot %u/%u)\n",
         f.pinturas, g.pinturas, f.pinturasNegadas, g.pinturasNegadas, f.orcNegados, g.orcNegados, f.orcConcedidos, g.orcConcedidos,
         (unsigned long)f.lacoPiorUs, (unsigned long)g.lacoPiorUs, pf, pg, perdi90[0], perdi90[1]);
  ck(f.pinturas > 1000 && g.pinturas > 1000, "o mapa pintou ~2x por segundo nas duas telas (nao foi faminto)", fmt("%u / %u em ~600 s", f.pinturas, g.pinturas));
  ck(pf == 0 && pg == 0, "com o custo conhecido, a pintura nunca come a janela de TX (perdi = 0 na trilha)", fmt("perdi %u / %u", pf, pg));
  ck(f.gpsBacklog == 0 && g.gpsBacklog == 0 && f.sync == 2 && g.sync == 2, "pinturas de 250 ms nao mexem com o PPS", fmt("backlog %u/%u sync %d/%d", f.gpsBacklog, g.gpsBacklog, f.sync, g.sync));
  ck(icone.ok == icone.n, "o icone do lider todos os segundos", fmt("%d de %d s", icone.ok, icone.n));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 15b) LACO PARADO 300 ms A CADA 4 s (o pior quadro frio medido, a 0,5 s de
//     repintura): GPS_LACO_PARADO_MS e 300, entao cada parada joga fora o NMEA do
//     segundo, o par PPS daquele segundo se perde, e tdmaOnPps ZERA o ppsRun - sao
//     precisos 10 s limpos para o PPS voltar a ancorar. Com paradas a cada 4 s o
//     PPS nunca volta: o no corre na ancora velha, entra em holdover aos 24,5 s,
//     aceita o beacon do outro, e cada troca de fonte custa um 'perdi'.
static void cen_pps_laco_300()
{
  comeca("pps_laco_300", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  for (int i = 0; i < 2; i++) NOS[i]->pintura(500, 70000, 300000, 8, false);
  Taxa icone, ppsF, ppsG; int trocasF = 0, trocasG = 0; uint8_t srcF = 0xFF, srcG = 0xFF;
  roda(660, [&](double t) {
    if (t < 100) return;
    NoObs f = obs(0), g = obs(1);
    icone.conta(g.liderDesenhado); ppsF.conta(f.sync == 2); ppsG.conta(g.sync == 2);
    if (f.ancoraSrc != srcF) { if (srcF != 0xFF) trocasF++; srcF = f.ancoraSrc; }
    if (g.ancoraSrc != srcG) { if (srcG != 0xFF) trocasG++; srcG = g.ancoraSrc; }
  });
  NoObs f = obs(0), g = obs(1);
  printf("  info: backlog NMEA descartado %u/%u | par PPS velho %u/%u | ppsRun final %u/%u | em PPS %.0f%%/%.0f%% do tempo | trocas de fonte %d/%d | perdi %u/%u\n",
         f.gpsBacklog, g.gpsBacklog, f.gpsParVelho, g.gpsParVelho, f.ppsRun, g.ppsRun, ppsF.pct(), ppsG.pct(), trocasF, trocasG, f.txPerdidos, g.txPerdidos);
  if (GPS_LACO_PARADO_MS <= 300UL || GPS_PAR_PPS_MAX_US <= 400000ULL) {
    // o firmware como esta (v201): documenta o colapso
    ck(f.gpsBacklog > 50, "DOCUMENTADO: cada parada de 300 ms descarta o backlog NMEA (GPS_LACO_PARADO_MS = 300)", fmt("%u descartes no lider", f.gpsBacklog));
    ck(ppsF.pct() < 90.0 || ppsG.pct() < 90.0, "DOCUMENTADO: o PPS deixa de disciplinar a rede (ppsRun zera a cada par perdido)", fmt("em PPS %.0f%% / %.0f%% do tempo", ppsF.pct(), ppsG.pct()));
    ck(trocasF + trocasG > 0 && f.txPerdidos + g.txPerdidos > 1, "DOCUMENTADO: trocas de fonte de tempo e TX perdidos por mudanca de fase", fmt("trocas %d/%d perdi %u/%u", trocasF, trocasG, f.txPerdidos, g.txPerdidos));
  } else {
    // com a correcao candidata (GPS_LACO_PARADO_MS 600 / GPS_PAR_PPS_MAX_US 700 ms)
    ck(f.gpsBacklog == 0 && g.gpsBacklog == 0 && f.gpsParVelho == 0 && g.gpsParVelho == 0, "parada de 300 ms NAO descarta NMEA nem recusa o par PPS", fmt("backlog %u/%u parVelho %u/%u", f.gpsBacklog, g.gpsBacklog, f.gpsParVelho, g.gpsParVelho));
    ck(ppsF.pct() >= 99.0 && ppsG.pct() >= 99.0, "as duas telas ficam em PPS o tempo todo", fmt("%.0f%% / %.0f%%", ppsF.pct(), ppsG.pct()));
    ck(trocasF + trocasG == 0, "nenhuma troca de fonte de tempo", fmt("trocas %d/%d perdi %u/%u", trocasF, trocasG, f.txPerdidos, g.txPerdidos));
  }
  ck(icone.pct() >= 97.0, "o icone sobrevive (os dois ficam na mesma grade, so nao e a UTC)", fmt("%.1f%%", icone.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 16) PINTURA FRIA PEDIDA COMO QUENTE: a tela_trilha.h pede ao orcamento
//     max(ultimoQuadroUs, piorRecenteUs), e zera o pior a cada 16 quadros. Um quadro
//     frio (setor novo na tela) chega SEM AVISO e custa 250 ms com 70 pedidos. A
//     guarda do v201 nao tem como ver isso - o TX do quadro se perde. Mede quanto.
static void cen_pintura_fria()
{
  comeca("pintura_fria", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  for (int i = 0; i < 2; i++) NOS[i]->pintura(500, 70000, 250000, 8, true);
  Taxa icone; uint32_t perdi90[2] = { 0, 0 };
  roda(660, [&](double t) { if (t == 90) { perdi90[0] = obs(0).txPerdidos; perdi90[1] = obs(1).txPerdidos; } if (t > 90) icone.conta(obs(1).liderDesenhado); });
  NoObs f = obs(0), g = obs(1);
  const uint32_t pf = f.txPerdidos - perdi90[0], pg = g.txPerdidos - perdi90[1];
  const unsigned frias = (f.pinturas / 8) + (g.pinturas / 8);
  printf("  info: pinturas %u/%u (~%u frias) | perdi na trilha %u/%u | orc negados %u/%u | laco pior %lu/%lu us | backlog NMEA %u/%u sync %d/%d\n",
         f.pinturas, g.pinturas, frias, pf, pg, f.orcNegados, g.orcNegados,
         (unsigned long)f.lacoPiorUs, (unsigned long)g.lacoPiorUs, f.gpsBacklog, g.gpsBacklog, f.sync, g.sync);
  ck(f.pinturas > 1000 && g.pinturas > 1000, "o modelo pintou ~2x por segundo", fmt("%u / %u", f.pinturas, g.pinturas));
  ck(f.gpsBacklog == 0 && g.gpsBacklog == 0 && f.sync == 2 && g.sync == 2, "o PPS ficou intacto (o perdi daqui e so da pintura)", fmt("backlog %u/%u sync %d/%d", f.gpsBacklog, g.gpsBacklog, f.sync, g.sync));
  // Documenta o buraco (nao e do diff v201, mas o quadro de 2 s o expoe 3x mais):
  // uma fracao das pinturas frias pedidas como quentes atravessa a janela.
  // A 500 ms a pintura trava de fase logo depois da janela (4 pinturas por quadro de
  // 2 s): da 0 ou 1 perdido conforme microssegundos - so informa. A prova esta no
  // cenario pintura_fria_530, que varre todas as fases.
  ck(true, "INFO: pintura fria pedida como quente (a 500 ms, fase travada - ver pintura_fria_530)",
     fmt("perdi %u + %u em ~%u frias (%.1f %% das frias)", pf, pg, frias, frias ? 100.0 * (pf + pg) / frias : 0));
  ck(icone.pct() >= 97.0, "mesmo assim o icone fica (extrapolacao cobre 1 quadro perdido)", fmt("%.1f%%", icone.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 16b) O MESMO, com a pintura a cada 530 ms: 500 divide o quadro de 2 s e a pintura
//     trava de fase logo depois da janela (otimista); 530 varre todas as fases.
static void cen_pintura_fria_530()
{
  comeca("pintura_fria_530", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  for (int i = 0; i < 2; i++) NOS[i]->pintura(530, 70000, 250000, 8, true);
  Taxa icone; uint32_t perdi90[2] = { 0, 0 };
  roda(660, [&](double t) { if (t == 90) { perdi90[0] = obs(0).txPerdidos; perdi90[1] = obs(1).txPerdidos; } if (t > 90) icone.conta(obs(1).liderDesenhado); });
  NoObs f = obs(0), g = obs(1);
  const uint32_t pf = f.txPerdidos - perdi90[0], pg = g.txPerdidos - perdi90[1];
  const unsigned frias = (f.pinturas / 8) + (g.pinturas / 8);
  printf("  info: pinturas %u/%u (~%u frias) | perdi na trilha %u/%u (%.1f %% das frias) | backlog NMEA %u/%u sync %d/%d\n",
         f.pinturas, g.pinturas, frias, pf, pg, frias ? 100.0 * (pf + pg) / frias : 0, f.gpsBacklog, g.gpsBacklog, f.sync, g.sync);
  ck(f.gpsBacklog == 0 && g.gpsBacklog == 0 && f.sync == 2 && g.sync == 2, "o PPS ficou intacto", fmt("backlog %u/%u sync %d/%d", f.gpsBacklog, g.gpsBacklog, f.sync, g.sync));
  ck(pf + pg > 0, "DOCUMENTADO: sem a trava de fase, a pintura fria pedida como quente perde TX", fmt("perdi %u + %u em ~%u frias", pf, pg, frias));
  ck(icone.pct() >= 97.0, "o icone fica", fmt("%.1f%%", icone.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 17) RUIDO COM APARELHO NA SALA NEUTRA: a tela_c liga, pega GPS e NUNCA entra em
//     grupo - fica no ar na sala neutra, no slot 2, a 40 m. Ela nao entra na frota
//     nem na lista de vizinhos (core/vizinhos.h ignora a sala neutra), entao o
//     redeSlotLivre(2) do grupo diz "livre" e a amostra de ruido cai no TX dela.
//     Em t=400 ela ENTRA no grupo: a partir dai o slot 2 e dela e as amostras param.
static void cen_ruido_neutro()
{
  comeca("ruido_neutro", 3);
  const char* nomes[] = { "FaustoA", "gino", "tela_c" };
  liga(0, 0.0, "FaustoA", 0, 0, +2.0);
  liga(1, 1.4, "gino", 1, 1, -3.0);
  liga(2, 2.9, "tela_c", 2, 2, +5.0);
  g_s[0] = anda(1e9, 80, 0); g_s[1] = anda(1e9, 0, 0); g_s[2] = anda(1e9, 40, 0);   // parados: bancada/estacionamento
  em(14, []() { for (int i = 0; i < 3; i++) NOS[i]->gpsFix(true); });
  em(40, []() { NOS[0]->criar("33333"); });
  em(52, []() { NOS[1]->entrarLista(); });
  em(60, []() { NOS[0]->abrir(); });
  bool e2 = false; NoObs f{}, c{};
  em(395, [&]() { f = obs(0); c = obs(2); });     // ANTES de a tela_c entrar
  em(400, [&]() { e2 = NOS[2]->entrarLista(); });
  roda(700, [&](double) {});
  for (int i = 0; i < 3; i++) NOS[i]->fecha();
  int n1, a1, n2, a2; double m1, m2;
  contaRuido("FaustoA", 100000, 395000, ar::cfg.ruidoDbm + 10, n1, a1, m1);
  contaRuido("FaustoA", 420000, 700000, ar::cfg.ruidoDbm + 10, n2, a2, m2);
  printf("  info: FaustoA ruidoN %u max %.0f med %.0f dBm (fundo %.0f) | neutro no ar: %d amostras, %d acima do fundo+10 (max %.0f) | depois de entrar: %d amostras, %d acima (max %.0f)\n",
         f.ruidoN, f.ruidoMax, f.ruidoMed, ar::cfg.ruidoDbm, n1, a1, m1, n2, a2, m2);
  ck(c.tx > 100, "a tela_c transmite na sala neutra (slot 2) sem estar em grupo", fmt("tx %u", c.tx));
  ck(f.carros == 2 && f.vizN == 0, "o grupo nao a ve: nem na frota nem na lista (sala neutra)", fmt("carros %d vizN %d", f.carros, f.vizN));
  ck(a1 > 0, "DOCUMENTADO: a amostra de ruido do grupo cai no TX do aparelho neutro", fmt("%d de %d amostras acima de %.0f dBm, max %.0f", a1, n1, ar::cfg.ruidoDbm + 10, m1));
  ck(e2 && a2 == 0, "depois de ela entrar no grupo, o slot 2 deixa de ser amostrado", fmt("entrou=%d, %d altas em %d", (int)e2, a2, n2));
  resumoAr(3, nomes);
}

// 18) UM v200 NO AR (proto 2, quadro de 6 s x 12): uma tela nao regravada, lider
//     no slot 0, a 30 m do seguidor. O v201 tem de REJEITAR e AVISAR (versaoErrada,
//     linha V). E como 6 e multiplo de 2, o slot 0 dela cai no slot 0 do lider v201
//     um quadro em cada tres: mede quanto o seguidor perde do lider por colisao.
static uint8_t g_fantasmaSeq = 0;
static void fantasmaV200(int no, uint8_t slot, int nBytes, double tIni, double tFim)
{
  // air-time de core/airtime.h (SF9/BW125): 16 B = 171 ms, 68 B = 417 ms
  const uint64_t dur = nBytes >= 68 ? 417000ull : 171000ull;
  for (double t = std::floor(tIni / 6.0) * 6.0; t < tFim; t += 6.0) {
    // borda do quadro de 6 s na grade UTC (o sim comeca as 12:00:00 = 43200 s, multiplo de 6)
    const double t0 = t + slot * 0.5 + 0.012;
    if (t0 < tIni) continue;
    em(t0 - 2.0, [=]() {       // empurra para o ar 2 s ANTES: a colisao e decidida por quem coleta depois
      uint8_t b[68]; memset(b, 0, sizeof(b));
      b[0] = 2; b[1] = 0x34; b[2] = 0x12; b[3] = slot; b[4] = g_fantasmaSeq++; b[5] = 0x01 | 0x04 | 0x40;
      ar::transmite(no, (uint64_t)(t0 * 1e6), dur, b, nBytes, 10);
    });
  }
}
static void cen_v200_fantasma()
{
  comeca("v200_fantasma", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  basePar(30, 80);
  g_s[7] = anda(60, 30, 30);                    // o v200 anda junto, 30 m a frente do seguidor
  fantasmaV200(7, 0, 68, 100.0, 660.0);
  Taxa icone; uint32_t vAos100 = 0;
  roda(660, [&](double t) { if (t > 120) icone.conta(obs(1).liderDesenhado); if (t == 100) vAos100 = obs(1).versaoErrada; });
  NoObs f = obs(0), g = obs(1);
  const uint64_t tot = ar::est[1].entregues + ar::est[1].alcance + ar::est[1].colisao;
  printf("  info: seguidor: versaoErrada %u (antes do fantasma %u) | colisoes no seguidor %llu de %llu | lider: colisoes %llu | fantasma tx %llu\n",
         g.versaoErrada, vAos100, (unsigned long long)ar::est[1].colisao, (unsigned long long)tot,
         (unsigned long long)ar::est[0].colisao, (unsigned long long)ar::est[7].tx);
  ck(ar::est[7].tx > 80, "o fantasma v200 transmitiu a cada 6 s", fmt("%llu", (unsigned long long)ar::est[7].tx));
  ck(g.versaoErrada > 50, "o seguidor v201 CONTA o v200 como versao incompativel (linha V, aviso na tela)", fmt("versaoErrada %u", g.versaoErrada));
  // O lider v201 esta no MESMO slot do v200: transmite no mesmo instante e fica
  // surdo para ele - justamente quem colide e quem nunca ve o aviso.
  ck(f.versaoErrada < g.versaoErrada / 4, "DOCUMENTADO: o lider (mesmo slot do v200) quase nao ouve o v200 - o aviso aparece so no seguidor",
     fmt("versaoErrada lider %u x seguidor %u", f.versaoErrada, g.versaoErrada));
  ck(g.carros == 2 && f.carros == 2, "o v200 nao entra na frota de ninguem", fmt("carros %d / %d", g.carros, f.carros));
  ck(ar::est[1].colisao > 30, "DOCUMENTADO: o slot 0 do v200 colide com o lider v201 um quadro em cada tres",
     fmt("%llu colisoes no seguidor em %llu pacotes (%.0f %%)", (unsigned long long)ar::est[1].colisao, (unsigned long long)tot, tot ? 100.0 * ar::est[1].colisao / tot : 0));
  ck(icone.pct() >= 97.0, "o icone do lider ainda fica (extrapolacao de 8 s cobre 1 em 3 perdido)", fmt("%.1f%%", icone.pct()));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// 19) CONFIG DO v200 COM SLOT 5: a NVS da gino vem do esquema 2 com slot 5. No boot
//     o slot vai a 0 com a FAIXA na tela - e colide com o lider (slot 0). Depois de
//     um RELIGAR a NVS ja diz slot 0, esquema 3: a faixa some e a colisao continua.
static void cen_cfg_slot_velho()
{
  comeca("cfg_slot_velho", 2);
  const char* nomes[] = { "FaustoA", "gino" };
  liga(0, 0.0, "FaustoA", 0, 0, +3.0);
  em(2.0, []() { NOS[1]->semeiaNvs(2, 5, "gino", 1); });
  liga(1, 2.3, "gino", 1, 1, -4.0);
  g_s[0] = anda(60, 80, 30); g_s[1] = anda(60, 0, 30);
  em(12, []() { NOS[0]->gpsFix(true); NOS[1]->gpsFix(true); });
  NoObs boot1{};
  em(20, [&]() { boot1 = obs(1); });
  em(40, []() { NOS[0]->criar("13520"); });
  em(52, []() { NOS[1]->entrarLista(); });
  em(55, []() { NOS[0]->abrir(); });
  NoObs antesReliga{};
  em(199, [&]() { antesReliga = obs(0); });
  em(200, []() { NOS[1]->religa(200 * 1000000ull); });
  em(201, []() { NOS[1]->gpsFix(true); });
  em(230, []() { NOS[1]->retomar(); });
  NoObs boot2{};
  em(240, [&]() { boot2 = obs(1); });
  Taxa icone;
  roda(400, [&](double t) { if (t > 90) icone.conta(obs(1).liderDesenhado); });
  NoObs f = obs(0), g = obs(1);
  ck(boot1.cfgSlot == 0 && boot1.cfgEsquema == 3, "slot 5 do v200 vai para 0, esquema 3", fmt("slot %u esquema %u", boot1.cfgSlot, boot1.cfgEsquema));
  ck(strncmp(boot1.aviso, "SLOT 5 NAO EXISTE MAIS", 22) == 0, "a faixa vermelha diz qual slot sumiu", fmt("'%s' (%d chars)", boot1.aviso, (int)strlen(boot1.aviso)));
  // Os dois no slot 0 transmitem no MESMO instante (grade PPS): cada um esta surdo
  // quando o outro fala. O "OUTRO aparelho esta no MEU slot" (conflitos) so dispara
  // se um pacote chega - o que aqui quase nunca acontece. O sintoma e SILENCIO.
  ck(antesReliga.carros == 1 && icone.ok == 0, "em conflito de slot ninguem ve ninguem: lider com 1 carro, seguidor nunca desenha o lider",
     fmt("carros no lider %d, icone %d de %d s, conflitos contados %u / %u", antesReliga.carros, icone.ok, icone.n, antesReliga.conflitos, g.conflitos));
  ck(boot2.cfgSlot == 0 && boot2.aviso[0] == 0, "DOCUMENTADO: depois de religar a faixa SOME (a NVS ja diz slot 0) e a colisao continua muda",
     fmt("slot %u aviso '%s' carros no lider %d", boot2.cfgSlot, boot2.aviso, f.carros));
  resumoAr(2, nomes);
  for (int i = 0; i < 2; i++) NOS[i]->fecha();
}

// ============================================================ main
int main(int argc, char** argv)
{
  ar::posicao = pose;
  struct { const char* nome; void (*f)(); bool pesado; } cens[] = {
    { "grupo_lista", cen_grupo_lista, false }, { "perde_e_volta", cen_perde_e_volta, false },
    { "lider_reinicia", cen_lider_reinicia, false }, { "radio_tarde", cen_radio_tarde, false },
    { "grupos_trocados", cen_grupos_trocados, false }, { "tres_carros", cen_tres_carros, false },
    { "gps_perde", cen_gps_perde, false }, { "alerta", cen_alerta, false }, { "trilha_longa", cen_trilha_longa, false },
    { "perda_pesada", cen_perda_pesada, false }, { "quatro_carros", cen_quatro_carros, false }, { "rapido", cen_rapido, false },
    { "entra_tarde", cen_entra_tarde, false }, { "dio1_morto", cen_dio1_morto, false },
    // estresse (minutos cada): "estresse" roda so estes, "tudo" roda todos
    { "comboio4_perda", cen_comboio4_perda, true }, { "lider_troca4", cen_lider_troca4, true },
    { "vao300_4carros", cen_vao300_4carros, true }, { "sem_pps4", cen_sem_pps4, true }, { "longa4", cen_longa4, true },
  struct { const char* nome; void (*f)(); } cens[] = {
    { "grupo_lista", cen_grupo_lista }, { "perde_e_volta", cen_perde_e_volta },
    { "lider_reinicia", cen_lider_reinicia }, { "radio_tarde", cen_radio_tarde },
    { "grupos_trocados", cen_grupos_trocados }, { "tres_carros", cen_tres_carros },
    { "gps_perde", cen_gps_perde }, { "alerta", cen_alerta }, { "trilha_longa", cen_trilha_longa },
    { "perda_pesada", cen_perda_pesada }, { "quatro_carros", cen_quatro_carros }, { "rapido", cen_rapido },
    { "entra_tarde", cen_entra_tarde }, { "dio1_morto", cen_dio1_morto },
    // revisao v201
    { "pintura_05s", cen_pintura_05s }, { "pps_laco_300", cen_pps_laco_300 }, { "pintura_fria", cen_pintura_fria },
    { "pintura_fria_530", cen_pintura_fria_530 }, { "ruido_neutro", cen_ruido_neutro },
    { "v200_fantasma", cen_v200_fantasma }, { "cfg_slot_velho", cen_cfg_slot_velho },
  };
  // "todos": CADA CENARIO NUM PROCESSO NOVO. As telas simuladas sao o firmware
  // inteiro, com NVS, sessao gravada e variaveis static - rodar um cenario depois
  // do outro no mesmo processo era nao desligar as telas entre dois testes (e o
  // segundo cenario herdava a sessao do primeiro).
  const bool todos = argc < 2 || strcmp(argv[1], "todos") == 0;
  const bool estresse = argc >= 2 && strcmp(argv[1], "estresse") == 0;
  const bool tudo = argc >= 2 && strcmp(argv[1], "tudo") == 0;
  if (todos || estresse || tudo) {
    int falhos = 0, n = 0;
    std::vector<std::string> quais;
    for (auto& c : cens) {
      if (!tudo && c.pesado != estresse) continue;
      n++;
      fflush(stdout);
      int rc = std::system((std::string("\"") + argv[0] + "\" " + c.nome).c_str());
      if (rc != 0) { falhos++; quais.push_back(c.nome); }
    }
    printf("\n==== %d cenarios, %d com falha%s", n, falhos, falhos ? ":" : "\n");
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
