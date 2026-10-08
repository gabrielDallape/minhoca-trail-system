#include "ar.h"
#include <cstring>
#include <algorithm>

namespace ar {

Cfg cfg;
PosFn posicao = nullptr;
Est est[8];

struct Pkt { int de; uint64_t ini, fim; std::vector<uint8_t> b; int8_t pwr; };
static std::vector<Pkt> g_pkts;

void transmite(int de, uint64_t iniG, uint64_t durUs, const uint8_t* b, int n, int8_t pwrChipDbm)
{
  Pkt p; p.de = de; p.ini = iniG; p.fim = iniG + durUs; p.b.assign(b, b + n); p.pwr = pwrChipDbm;
  g_pkts.push_back(p);
  if (de >= 0 && de < 8) est[de].tx++;
}

double distanciaM(int a, int b, uint64_t tG)
{
  Pose pa = posicao(a, tG), pb = posicao(b, tG);
  const double k = 111320.0;
  double dy = (pb.lat - pa.lat) * k;
  double dx = (pb.lon - pa.lon) * k * std::cos(pa.lat * M_PI / 180.0);
  return std::sqrt(dx * dx + dy * dy);
}

// desvanecimento DETERMINISTICO por (pacote, receptor): a mesma semente da o
// mesmo resultado, entao uma falha do simulador se reproduz.
static double gauss(uint64_t chave)
{
  uint64_t x = chave * 0x9E3779B97F4A7C15ull + cfg.semente;
  auto u = [&]() { x ^= x >> 33; x *= 0xff51afd7ed558ccdull; x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ull; x ^= x >> 33;
                   return ((x >> 11) + 0.5) / 9007199254740992.0; };
  double u1 = u(), u2 = u();
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(2 * M_PI * u2);
}

static double rssiDe(const Pkt& p, int para)
{
  double d = distanciaM(p.de, para, (p.ini + p.fim) / 2);
  if (d < 1.0) d = 1.0;
  double pl = cfg.pl0 + 10.0 * cfg.n * std::log10(d);
  uint64_t chave = (p.ini << 8) ^ ((uint64_t)p.de << 4) ^ (uint64_t)para;
  return p.pwr + cfg.ganhoPaDb - pl + cfg.fadingDb * gauss(chave);
}

void coleta(int para, bool ligado, uint64_t desdeG, uint64_t ateG, std::vector<Rx>& out)
{
  for (const Pkt& p : g_pkts) {
    if (p.de == para || p.fim <= desdeG || p.fim > ateG) continue;
    Est& e = est[para];
    if (!ligado) { e.desligado++; continue; }
    bool euFalava = false;
    for (const Pkt& q : g_pkts)
      if (q.de == para && q.ini < p.fim && q.fim > p.ini) { euFalava = true; break; }
    if (euFalava) { e.surdoTx++; continue; }
    double r = rssiDe(p, para);
    double snr = r - cfg.ruidoDbm;
    if (snr < cfg.snrMinDb) { e.alcance++; continue; }
    bool perdeu = false;
    for (const Pkt& q : g_pkts) {
      if (&q == &p || q.de == para || q.de == p.de) continue;
      if (q.ini >= p.fim || q.fim <= p.ini) continue;
      if (rssiDe(q, para) > r - cfg.capturaDb) { perdeu = true; break; }
    }
    if (perdeu) { e.colisao++; continue; }
    Rx x; x.b = p.b; x.rssi = (float)r; x.snr = (float)snr; x.fimG = p.fim; x.de = p.de;
    out.push_back(x);
    e.entregues++;
  }
}

double rssiNoAr(int para, uint64_t tG)
{
  double r = -200.0;
  for (const Pkt& p : g_pkts) {
    if (p.de == para || p.ini > tG || p.fim <= tG) continue;
    double v = rssiDe(p, para);
    if (v > r) r = v;
  }
  return r;
}

void limpaVelhos(uint64_t antesDeG)
{
  g_pkts.erase(std::remove_if(g_pkts.begin(), g_pkts.end(), [&](const Pkt& p) { return p.fim + 2000000ull < antesDeG; }),
               g_pkts.end());
}

}  // namespace ar
