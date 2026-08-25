// O MUNDO: onde estao os carros e por onde o lider passou.
//
// Esta e a fronteira entre o que a tela desenha e de onde o dado vem. Hoje quem
// preenche isto e um simulador; amanha e o GPS e o radio. A tela nao sabe a
// diferenca - e esse o ponto.
//
// Por que nao reusei o trilha_core.h das telas S3: ele carrega o protocolo
// LoRaMESH inteiro (packWorld/packRoster/SLOT_MS), e o P4 vai de TDMA sobre
// SX1262. O que se aproveita dele e a MATEMATICA, que esta reescrita aqui igual:
// haversine e o passo minimo de 5 m para nao encher o trajeto de pontos parados.
#pragma once
#include <math.h>
#include "hardware.h"
#if MTS_TEM_GPS
  #include "gps.h"
#endif

#define MUNDO_MAX_CARROS  8
#define TRAJETO_MAX     1200      // ~6 km guardados, com passo de 5 m
#define TRAJETO_PASSO_M    5.0

struct Carro {
  bool     ativo;
  double   lat, lon;
  bool     fix;
  bool     alerta;
  bool     lider;
  uint8_t  cor;
  uint8_t  slot;                 // slot TDMA = identidade no ar
  float    rumo;                 // graus, 0 = norte; vem do pacote (RMC de la)
  uint8_t  vel;                  // km/h, teto 255
  bool     temNome;              // o nome veio do roster, nao e o rotulo provisorio
  char     nome[17];
  uint32_t ultimoMs;             // quando falou pela ultima vez
};

struct Ponto { double lat, lon; };

// estado global do aparelho
static Carro  g_carros[MUNDO_MAX_CARROS];
static Ponto  g_trajeto[TRAJETO_MAX];
static int    g_trajN = 0, g_trajCab = 0;
static double g_meuLat = 0, g_meuLon = 0;
static bool   g_meuFix = false;
static int    g_meuSlot = 0;
static uint8_t g_sats = 0;
// Rumo proprio: gira o MEU marcador e viaja no pacote para girar o marcador
// deste carro na tela dos outros. 0 = norte, mapa north-up.
static float   g_meuRumo = 0;
static uint8_t g_meuVel = 0;

// O radio entra DEPOIS dos globais acima de proposito: ele monta o pacote a
// partir da minha posicao e das minhas flags, entao precisa que elas ja existam.
// E ele declara mundoRecebePacote() sem definir, porque quem sabe o que e um
// carro e este arquivo, nao o radio - a definicao vem mais abaixo.
#include "fila.h"

#if MTS_TEM_RADIO
  #include "radio.h"
#endif

// --------------------------------------------------------------- geometria
inline double haversine(double la1, double lo1, double la2, double lo2)
{
  const double R = 6371000.0, r = M_PI / 180.0;
  double dla = (la2 - la1) * r, dlo = (lo2 - lo1) * r;
  double a = sin(dla / 2) * sin(dla / 2)
           + cos(la1 * r) * cos(la2 * r) * sin(dlo / 2) * sin(dlo / 2);
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

// Projecao equirretangular, centrada em MIM. Para os poucos quilometros de uma
// trilha o erro e desprezivel e o custo e uma multiplicacao - projecao de verdade
// (Mercator) so paga a pena quando o mapa cobre continentes.
//
// MAPA ORIENTADO PELO RUMO (heading-up), pedido em campo em 2026-08-24: "a tela
// nao esta seguindo a orientacao do caminho". A regra antiga de north-up existia
// por causa do RELEVO raster (girar retangulos nao da); com o relevo desligado
// (mesma decisao de campo), sobra so VETOR - e girar vetor custa duas
// multiplicacoes por ponto, que ja se pagava de qualquer jeito na projecao.
// O rumo persegue o alvo com filtro (0,4 por quadro) para o mapa nao chicotear
// a cada pacote; parado, o RMC congela o rumo e o mapa fica quieto.
static float g_rotSen = 0.0f, g_rotCos = 1.0f;   // rotacao atual do mapa
static float g_mapaRotG = 0.0f;                  // em graus, para os marcadores

inline void mapaOrientaPeloRumo()
{
  float d = g_meuRumo - g_mapaRotG;
  while (d > 180.0f)  d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  g_mapaRotG += d * 0.4f;
  while (g_mapaRotG >= 360.0f) g_mapaRotG -= 360.0f;
  while (g_mapaRotG < 0.0f)    g_mapaRotG += 360.0f;
  const float r = g_mapaRotG * 0.017453292f;
  g_rotSen = sinf(r);
  g_rotCos = cosf(r);
}

inline void paraTela(double lat, double lon, int cx, int cy, double mPorPx,
                     int& x, int& y)
{
  const double R = 6371000.0, r = M_PI / 180.0;
  double dx = (lon - g_meuLon) * r * R * cos(g_meuLat * r);   // leste
  double dy = (lat - g_meuLat) * r * R;                       // norte
  // gira o mundo em -rumo: o que esta a frente do carro sobe na tela
  double rx = dx * g_rotCos - dy * g_rotSen;
  double ry = dx * g_rotSen + dy * g_rotCos;
  x = cx + (int)(rx / mPorPx);
  y = cy - (int)(ry / mPorPx);
}

// --------------------------------------------------------------- trajeto
// So grava se andou TRAJETO_PASSO_M. Sem isso, um carro parado enche o buffer de
// pontos identicos e come os 6 km de historico em minutos.
inline void trajetoTalvezAdicione(double lat, double lon)
{
  static bool  temUlt = false;
  static double ultLat = 0, ultLon = 0;
  if (temUlt && haversine(ultLat, ultLon, lat, lon) < TRAJETO_PASSO_M) return;
  g_trajeto[g_trajCab] = { lat, lon };
  g_trajCab = (g_trajCab + 1) % TRAJETO_MAX;
  if (g_trajN < TRAJETO_MAX) g_trajN++;
  ultLat = lat; ultLon = lon; temUlt = true;
}

inline const Ponto& trajetoEm(int i)      // 0 = mais antigo
{
  return g_trajeto[(g_trajCab - g_trajN + i + TRAJETO_MAX) % TRAJETO_MAX];
}

inline int distanciaAte(int i)
{
  if (!g_carros[i].ativo || !g_carros[i].fix || !g_meuFix) return -1;
  return (int)haversine(g_meuLat, g_meuLon, g_carros[i].lat, g_carros[i].lon);
}

// ------------------------------------------------------------------ fonte
// TUDO que le hardware entra por aqui. Enquanto as chaves do hardware.h estao em
// zero, quem responde e o simulador - e ele anda de verdade, tracando uma trilha
// curva, para o mapa poder ser conferido sem sair de casa.
// QUEM EU SOU, disponivel desde o boot.
//
// Existe separado do mundoInicia() porque o radio comeca a anunciar assim que
// liga, e o mundoInicia() so roda quando o usuario entra numa trilha. Sem isto o
// roster saia com nome vazio e cor zero durante todo o tempo em que o aparelho
// ficasse na tela inicial - que e justamente quando os outros estao tentando
// descobrir quem chegou na sala de espera.
inline void mundoIdentidade(const char* meuNome, uint8_t minhaCor)
{
  g_carros[0].ativo = true;
  g_carros[0].slot  = (uint8_t)g_meuSlot;
  g_carros[0].cor   = minhaCor;
  strncpy(g_carros[0].nome, meuNome, sizeof(g_carros[0].nome) - 1);
  g_carros[0].nome[sizeof(g_carros[0].nome) - 1] = 0;
  g_carros[0].temNome = true;
}

inline void mundoInicia(const char* meuNome, uint8_t minhaCor, bool souLider)
{
#if MTS_TEM_RADIO
  // COM RADIO, NAO ZERA OS OUTROS NEM A ROTA. O g_carros ja e o mundo vivo:
  // quem foi ouvido na sala de espera continua existindo ao abrir a trilha
  // (zerar aqui fazia cada carro reaparecer duplicado ate o TTL limpar), e a
  // rota que o lider ja transmitiu e catch-up, nao lixo. A limpeza certa e na
  // TROCA DE SALA (mundoLimpaOutros) e no TTL do carro zumbi.
  g_carros[0].alerta = false;    // pedido de socorro nao atravessa sessoes
#else
  for (int i = 0; i < MUNDO_MAX_CARROS; i++) g_carros[i] = Carro();
#endif
  // g_meuSlot NAO e zerado aqui. Ele vem da NVS no carregaCfg() e e o endereco
  // deste aparelho na rede - o tdmaInit ja foi chamado com ele no setup. Zerar
  // aqui (o que esta funcao fazia) punha as duas placas no slot 0 assim que
  // alguem entrasse numa trilha, e as duas passavam a transmitir por cima uma da
  // outra. Como o TDMA nao acusa colisao, o sintoma seria "as vezes o outro
  // carro some" - caçado no lugar errado por horas.
  strncpy(g_carros[0].nome, meuNome, sizeof(g_carros[0].nome) - 1);
  g_carros[0].nome[sizeof(g_carros[0].nome) - 1] = 0;
  g_carros[0].temNome = true;
  g_carros[0].cor = minhaCor;
  g_carros[0].slot = (uint8_t)g_meuSlot;
  g_carros[0].lider = souLider;
  g_carros[0].ativo = true;
  g_trajN = g_trajCab = 0;
#if !MTS_TEM_RADIO
  rotaLimpa();     // sessao nova, rota nova (com radio isso e feito na troca de sala)
#endif

  // Um ponto de partida, nos DOIS casos. Com GPS de verdade ele nao e mentira:
  // g_meuFix continua false ate o fix chegar, entao ninguem trata isto como
  // posicao. Serve para o mapa ter onde se ancorar no primeiro quadro - sem isso
  // ele abriria na latitude 0, longitude 0, no meio do Atlantico, e o usuario
  // veria um oceano vazio enquanto o GPS procura satelite.
  g_meuLat = -23.5505; g_meuLon = -46.6333;

#if MTS_TEM_GPS
  // gpsInicia() NAO vem aqui: mundoInicia() so roda quando o usuario entra numa
  // trilha, e o GPS e do aparelho, nao da sessao. Ligado aqui, ele so comecaria
  // a procurar satelite depois que o mapa ja estivesse aberto - o usuario
  // esperaria a fixacao inteira olhando para um mapa sem ele. Sobe no setup().
  g_meuFix = false; g_sats = 0;
#else
  g_meuFix = true; g_sats = 9;
#endif
}

inline void mundoAtualiza()
{
#if MTS_TEM_GPS
  gpsAtualiza();
  // gpsFixValido() e nao g_gpsFix: o fix envelhece. Um GPS que emudeceu deixaria
  // o carro cravado no ultimo ponto, e o mapa mentiria com cara de certeza -
  // pior do que assumir que nao sabe onde esta.
  g_meuFix = gpsFixValido();
  if (g_meuFix) { g_meuLat = g_gpsLat; g_meuLon = g_gpsLon; }
  g_sats = g_gpsSats;
  g_meuRumo = g_gpsRumo;
  g_meuVel  = (g_gpsVelKmh > 254.0f) ? 255 : (uint8_t)(g_gpsVelKmh + 0.5f);
  // O PPS ainda nao alimenta o TDMA - isso e do tdma_core.h, no proximo bloco.
  // Ate la ele ja e lido e validado, entao a tela pode dizer se ha regua de tempo.
#else
  // Simulador: eu ando numa trilha e os outros me seguem atrasados.
  // 1 Hz: a taxa de um GPS de verdade. Antes corria a 5 Hz e obrigava o mapa a
  // repintar cinco vezes por segundo - metade da piscada vinha daqui.
  //
  // O QUE MUDOU E POR QUE. A versao anterior somava um SENO A POSICAO:
  //     g_meuLat += cos(ang) * 0.00007;
  //     g_meuLon += sin(ang * 0.7) * 0.00009;
  // Como o incremento e senoidal, a posicao e a integral dele - ou seja, oscila
  // em volta do ponto de partida em vez de sair dele. Dava uma figura de
  // Lissajous FECHADA de ~222 x 344 m, e o carro ficava dando voltas nela,
  // riscando por cima do proprio rastro. No zoom padrao a tela cobre 2.560 m,
  // entao o trajeto inteiro cabia num oitavo da largura: o "quadradinho".
  //
  // Agora quem oscila e o RUMO, e a posicao integra o rumo - que e como um carro
  // anda de verdade. As duas senoides de periodo incomensuravel dao curva longa
  // com serpenteado por cima, e nunca fecham exatamente, entao o traçado nao se
  // repete. A 8 m/s, os 1.200 pontos de 5 m viram ~9,6 km de trilha: bem mais que
  // a tela mostra em qualquer zoom, que e como tem de ser para dar para conferir
  // o mapa de verdade.
  static uint32_t t0 = 0;
  static double rumo = 0.6;      // radianos, 0 = norte
  static uint32_t passo = 0;
  if (millis() - t0 < 1000) return;
  t0 = millis();
  passo++;

  const double V = 8.0;          // m/s (~29 km/h, ritmo de trilha)
  rumo += 0.10 * sin(passo * 0.13) + 0.06 * sin(passo * 0.041 + 1.7);
  g_meuLat += cos(rumo) * V / 111320.0;
  g_meuLon += sin(rumo) * V / (111320.0 * cos(g_meuLat * M_PI / 180.0));
  g_meuFix = true;
  // o mesmo rumo do integrador, em graus, para o marcador girar tambem na bancada
  double rg = fmod(rumo * 180.0 / M_PI, 360.0);
  if (rg < 0) rg += 360.0;
  g_meuRumo = (float)rg;
  g_meuVel  = (uint8_t)(V * 3.6 + 0.5);
#endif

  if (g_meuFix) {
    g_carros[0].lat = g_meuLat; g_carros[0].lon = g_meuLon; g_carros[0].fix = true;
    trajetoTalvezAdicione(g_meuLat, g_meuLon);
  }

#if MTS_TEM_RADIO
  // O radio e servido no loop principal tambem, nao so aqui: dentro da trilha
  // este ponto e chamado a cada quadro, mas na tela inicial nao seria chamado
  // nunca, e o no perderia a sincronia justamente enquanto o usuario decide o
  // que fazer. Aqui a chamada garante o atendimento durante o mapa.
  radioAtualiza();

  // Carro que emudeceu deixa de ser carro. Sem isto, um aparelho que desligou ou
  // ficou fora de alcance continuaria desenhado na ultima posicao conhecida, e o
  // motorista seguiria um fantasma. Cinco frames de silencio: com frame de 1 s,
  // cinco segundos.
  //
  // DOIS ESTAGIOS, de proposito. Perder o fix e reversivel (sombra de morro, o
  // radio volta); sumir da lista nao pode ser nervoso assim, senao um carro
  // pisca na sala de espera a cada perda de pacote. Mas ALGUEM tem de zerar o
  // ativo: sem o segundo estagio, quem desligou ficava na lista e na contagem
  // para sempre - o carro zumbi que o grupo_ws ja teve e ja corrigiu.
  const uint32_t TTL_MS = 5000UL * TDMA_FRAME_S;
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
    if (!g_carros[i].ativo) continue;
    uint32_t quieto = millis() - g_carros[i].ultimoMs;
    if (quieto > TTL_MS) g_carros[i].fix = false;      // some do mapa
    if (quieto > 3 * TTL_MS) g_carros[i] = Carro();    // some da lista e da contagem
  }
#else
  // os outros carros seguem o meu trajeto, cada um mais atrasado que o anterior -
  // que e exatamente o que se ve numa trilha de verdade
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
    if (!g_carros[i].ativo) continue;
    int atraso = i * 14;
    int idx = g_trajN - 1 - atraso;
    if (idx < 0) idx = 0;
    if (g_trajN > 0) {
      const Ponto& p = trajetoEm(idx);
      g_carros[i].lat = p.lat; g_carros[i].lon = p.lon;
      g_carros[i].fix = true;
      g_carros[i].ultimoMs = millis();
    }
  }
#endif
}

#if MTS_TEM_RADIO
// CONFLITO DE SLOT. Eu nunca recebo o meu proprio pacote - o radio esta em
// standby durante o TX - entao um pacote da minha sala com o MEU slot so pode
// ser OUTRO aparelho gravado com a mesma identidade. E a configuracao mais
// provavel de acontecer amanha (todos saem de fabrica no slot 0) e a mais cara
// de achar: os dois transmitem um por cima do outro e nenhum ve o outro.
static uint32_t g_slotConflito = 0;
inline void mundoAvisaConflito()
{
  g_slotConflito++;
  static uint32_t tAviso = 0;
  if (millis() - tAviso > 5000) {
    tAviso = millis();
    Serial.printf("*** OUTRO aparelho esta no MEU slot (%d)! Troque um deles "
                  "pelo serial: s0..s%d (conflitos: %lu)\n",
                  g_meuSlot, TDMA_SLOTS - 1, (unsigned long)g_slotConflito);
  }
}

// Chegou pacote de outro carro. Identidade e o SLOT, nao a ordem de chegada:
// se um carro reiniciar, ele volta no mesmo slot e reocupa a mesma linha em vez
// de virar um segundo carro fantasma na tela.
void mundoRecebePacote(const TdmaPkt& k)
{
  if (k.slot == (uint8_t)g_meuSlot) { mundoAvisaConflito(); return; }

  int idx = -1;
  for (int i = 1; i < MUNDO_MAX_CARROS; i++)
    if (g_carros[i].ativo && g_carros[i].slot == k.slot) { idx = i; break; }

  if (idx < 0) {
    for (int i = 1; i < MUNDO_MAX_CARROS; i++)
      if (!g_carros[i].ativo) { idx = i; break; }
    if (idx < 0) return;                          // sem vaga: ignora em silencio
    g_carros[idx] = Carro();
    g_carros[idx].ativo = true;
    g_carros[idx].slot  = k.slot;
    // COR PELO SLOT, e nao pela ordem de chegada. Assim a cor de cada carro e a
    // mesma em todas as telas do grupo - se dependesse de quem chegou primeiro,
    // cada aparelho pintaria o mesmo carro de uma cor, e "seguir o azul" viraria
    // instrucao inutil.
    g_carros[idx].cor = k.slot;
    // Rotulo PROVISORIO, ate o roster daquele carro chegar - o que leva no
    // maximo um segundo, porque as tres primeiras falas de todo no sao roster.
    // Nao e nome inventado: e o slot, que e o unico identificador que ja se sabe.
    snprintf(g_carros[idx].nome, sizeof(g_carros[idx].nome), "CARRO %u", k.slot);
    g_carros[idx].temNome = false;
    Serial.printf("mundo: ouvi um carro novo no slot %u\n", k.slot);
  }

  g_carros[idx].lat    = k.lat;
  g_carros[idx].lon    = k.lon;
  g_carros[idx].fix    = tdmaPktFix(k);
  g_carros[idx].rumo   = k.heading;
  g_carros[idx].vel    = k.speed;
  // O CAMINHO DO LIDER. E aqui que "siga o lider" deixa de ser so a posicao dele
  // e vira o traçado: cada pacote com fix vira um ponto da rota, e o mapa desenha
  // essa linha a frente. Sem isto o seguidor via um ponto se mexendo, nunca o
  // caminho - que e a coisa que o produto promete.
  if (tdmaPktLeader(k) && tdmaPktFix(k)) rotaAdiciona(k.lat, k.lon);
  // o alerta que MUDA vira linha no serial: e a prova de bancada de que o
  // pedido de socorro atravessou o radio (na tela ele pinta a faixa de vermelho)
  if (g_carros[idx].alerta != tdmaPktAlert(k))
    Serial.printf("mundo: %s %s ALERTA\n", g_carros[idx].nome,
                  tdmaPktAlert(k) ? "PEDIU" : "cancelou o");
  g_carros[idx].alerta = tdmaPktAlert(k);
  g_carros[idx].lider  = tdmaPktLeader(k);
  g_carros[idx].ultimoMs = millis();
}

// Chegou o "quem eu sou" de alguem. Vem no mesmo slot do pacote de posicao, so
// que de oito em oito quadros.
//
// NAO carimba ultimoMs: presenca e provada por POSICAO. Se o roster contasse
// como sinal de vida, um carro cujo pacote de posicao parou de chegar - o que
// importa - continuaria vivo no mapa por causa de uma mensagem que so diz o
// nome dele.
void mundoRecebeRoster(uint8_t slot, uint8_t cor, bool lider, const char* nome)
{
  if (slot == (uint8_t)g_meuSlot) { mundoAvisaConflito(); return; }

  int idx = -1;
  for (int i = 1; i < MUNDO_MAX_CARROS; i++)
    if (g_carros[i].ativo && g_carros[i].slot == slot) { idx = i; break; }

  // Aceita roster de quem ainda nao mandou posicao: assim o nome ja aparece na
  // sala de espera, onde ninguem tem fix ainda e so o roster esta circulando.
  if (idx < 0) {
    for (int i = 1; i < MUNDO_MAX_CARROS; i++)
      if (!g_carros[i].ativo) { idx = i; break; }
    if (idx < 0) return;
    g_carros[idx] = Carro();
    g_carros[idx].ativo = true;
    g_carros[idx].slot  = slot;
    g_carros[idx].ultimoMs = millis();
  }

  if (!g_carros[idx].temNome || strncmp(g_carros[idx].nome, nome, sizeof(g_carros[idx].nome) - 1) != 0)
    Serial.printf("mundo: slot %u se apresentou: %s%s\n", slot, nome, lider ? " (lider)" : "");
  strncpy(g_carros[idx].nome, nome, sizeof(g_carros[idx].nome) - 1);
  g_carros[idx].nome[sizeof(g_carros[idx].nome) - 1] = 0;
  g_carros[idx].temNome = true;
  g_carros[idx].lider = lider;
  // A COR VEM DO DONO, e nao do slot. Cada um escolhe a sua na configuracao, e e
  // por ela que os outros o chamam no radio ("segue o azul"). Derivar do slot,
  // como era antes, dava cor estavel mas ALHEIA a escolha do usuario.
  if (cor < N_CORES) g_carros[idx].cor = cor;
}
#endif

// Entra um carro novo (hoje pelo simulador da sala de espera; amanha pelo JOIN)
inline int mundoEntra(const char* nome, uint8_t cor)
{
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) {
    if (g_carros[i].ativo) continue;
    g_carros[i] = Carro();
    strncpy(g_carros[i].nome, nome, 15);
    g_carros[i].cor = cor;
    g_carros[i].ativo = true;
    g_carros[i].ultimoMs = millis();
    return i;
  }
  return -1;
}

inline int mundoQuantos()
{
  int n = 0;
  for (int i = 0; i < MUNDO_MAX_CARROS; i++) if (g_carros[i].ativo) n++;
  return n;
}

// Esquece todos os OUTROS carros (e a rota do lider deles). Chamar sempre que a
// sala do radio muda: o filtro novo vale para os pacotes DAQUI EM DIANTE, mas
// quem ja tinha sido ouvido na sala antiga ficaria na lista - a sala de espera
// de um grupo recem-criado nasceria com os carros do grupo anterior dentro.
inline void mundoLimpaOutros()
{
  for (int i = 1; i < MUNDO_MAX_CARROS; i++) g_carros[i] = Carro();
  rotaLimpa();
}
