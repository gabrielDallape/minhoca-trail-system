#pragma once
// ============================================================================
//  NVS - o que persiste no aparelho (Preferences), e nada de logica aqui
// ============================================================================
//
// Namespace "grupo", com as mesmas chaves do grupo_ws das telas S3 e do
// mts_campo: as tres telas P4 da frota ja tem nome, slot e potencia gravados, e
// a migracao de esquema (core/cfg.h) precisa le-los. Namespace "wifi" para a
// rede da garagem.
//
// REGRA: escrever na flash carimba CP_NVS na caixa-preta, porque uma escrita
// pode segurar o laco por dezenas de ms - e so acontece por acao do usuario
// (nunca por quadro).

#include <Preferences.h>
#include "../core/cfg.h"
#include "caixa.h"

#define NVS_NS_GRUPO "grupo"
#define NVS_NS_WIFI  "wifi"

// Le a configuracao crua. Devolve o esquema lido (0 se nunca gravado) e a marca
// pwrv da v101, para a migracao decidir.
inline void nvsLeCfg(Cfg& c, uint8_t& esquema, uint8_t& pwrv)
{
  cfgPadrao(c);
  Preferences p;
  p.begin(NVS_NS_GRUPO, true);
  String n = p.getString("name", "Carro");
  strncpy(c.nome, n.c_str(), sizeof(c.nome) - 1); c.nome[sizeof(c.nome) - 1] = 0;
  c.cor   = p.getUChar("color", 0);
  c.tema  = p.getUChar("theme", 1);
  c.zoom  = p.getUChar("zoom", 2);
  c.slot  = p.getUChar("slot", 0);
  c.pwr   = p.getChar("pwr", RF_PWR_PADRAO);
  c.boots = p.getUInt("boots", 0);
  esquema = p.getUChar("cfgv", 0);
  pwrv    = p.getUChar("pwrv", 0);
  p.end();
}

inline void nvsGravaCfg(const Cfg& c, uint8_t esquema)
{
  caixaEtapa(CP_NVS);
  Preferences p;
  p.begin(NVS_NS_GRUPO, false);
  p.putString("name", c.nome);
  p.putUChar("color", c.cor);
  p.putUChar("theme", c.tema);
  p.putUChar("zoom", c.zoom);
  p.putUChar("slot", c.slot);
  p.putChar("pwr", c.pwr);
  p.putUChar("cfgv", esquema);
  p.putUChar("pwrv", 101);     // a marca da v101 continua valendo para um downgrade
  p.end();
  caixaEtapa(CP_LACO);
}

// Escritas pontuais - uma chave so, por acao do usuario.
inline void nvsGravaUChar(const char* chave, uint8_t v)
{
  caixaEtapa(CP_NVS);
  Preferences p; p.begin(NVS_NS_GRUPO, false); p.putUChar(chave, v); p.end();
  caixaEtapa(CP_LACO);
}
inline void nvsGravaChar(const char* chave, int8_t v)
{
  caixaEtapa(CP_NVS);
  Preferences p; p.begin(NVS_NS_GRUPO, false); p.putChar(chave, v); p.end();
  caixaEtapa(CP_LACO);
}
inline uint32_t nvsIncrementaBoots()
{
  caixaEtapa(CP_NVS);
  Preferences p; p.begin(NVS_NS_GRUPO, false);
  uint32_t b = p.getUInt("boots", 0) + 1;
  p.putUInt("boots", b); p.end();
  caixaEtapa(CP_LACO);
  return b;
}

// ---------------------------------------------------------------- sessao
// "Eu estou numa trilha": grupo, codigo e papel. Poucas dezenas de bytes,
// escritos so quando algo muda. O TRAJETO nao cabe aqui (cresce enquanto o
// carro anda; gastaria a flash) - ele fica na RAM e o catch-up o recompoe.
#define GRUPO_NOME_MAX 24

struct Sessao {
  bool ativa   = false;
  bool lider   = false;
  char gNome[GRUPO_NOME_MAX] = "";
  char gCod[8]  = "";
};

inline void nvsLeSessao(Sessao& s)
{
  Preferences p;
  p.begin(NVS_NS_GRUPO, true);
  s.ativa = p.getBool("s_ativa", false);
  s.lider = p.getBool("s_lider", false);
  String n = p.getString("s_gnome", "");
  String c = p.getString("s_gcod", "");
  p.end();
  strncpy(s.gNome, n.c_str(), sizeof(s.gNome) - 1); s.gNome[sizeof(s.gNome) - 1] = 0;
  strncpy(s.gCod,  c.c_str(), sizeof(s.gCod)  - 1); s.gCod[sizeof(s.gCod)  - 1] = 0;
  if (!s.gNome[0]) s.ativa = false;      // sessao sem grupo nao existe
}

inline void nvsGravaSessao(const Sessao& s)
{
  caixaEtapa(CP_NVS);
  Preferences p;
  p.begin(NVS_NS_GRUPO, false);
  p.putBool("s_ativa", s.ativa);
  p.putBool("s_lider", s.lider);
  p.putString("s_gnome", s.gNome);
  p.putString("s_gcod", s.gCod);
  p.end();
  caixaEtapa(CP_LACO);
}

// ---------------------------------------------------------------- wifi
inline void nvsLeWifi(char* ssid, size_t ns, char* pass, size_t np)
{
  Preferences p;
  p.begin(NVS_NS_WIFI, true);
  String s = p.getString("ssid", "");
  String w = p.getString("pass", "");
  p.end();
  strncpy(ssid, s.c_str(), ns - 1); ssid[ns - 1] = 0;
  strncpy(pass, w.c_str(), np - 1); pass[np - 1] = 0;
}
inline void nvsGravaWifi(const char* ssid, const char* pass)
{
  caixaEtapa(CP_NVS);
  Preferences p;
  p.begin(NVS_NS_WIFI, false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.end();
  caixaEtapa(CP_LACO);
}
