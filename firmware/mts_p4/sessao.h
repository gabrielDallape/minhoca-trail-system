// Estado de sessao do MTS: "eu estou numa trilha".
//
// POR QUE EXISTE: se o aparelho desligar no meio da trilha - bateria solta, cabo
// que pulou, reset - ele tem de VOLTAR para a trilha ao religar, nao para o menu.
// Numa trilha isso nao e conforto: e o motorista de olho na estrada em vez de
// remontando grupo e digitando codigo.
//
// So o botao SAIR DA TRILHA encerra a sessao.
//
// O QUE CABE AQUI E O QUE NAO CABE:
//   cabe   - identidade da sessao (grupo, codigo, papel). Poucas dezenas de bytes,
//            escritos so quando algo muda. NVS aguenta isso a vida toda.
//   NAO cabe - o TRAJETO. Ele cresce enquanto o carro anda; gravar isso na flash a
//            cada ponto gasta o chip. Trajeto vai no CARTAO SD, que esta na placa.
//            Ver trilhaArquivo() abaixo, onde o SD entra.
#pragma once
#include <Preferences.h>

#define GRUPO_NOME_MAX 24

struct Sessao {
  bool ativa   = false;
  bool lider   = false;
  char gNome[GRUPO_NOME_MAX] = "";
  char gCod[8]  = "";
  uint32_t desde = 0;          // millis do inicio; so para exibir tempo de trilha
};

inline void sessaoCarrega(Sessao& s) {
  Preferences p;
  p.begin("grupo", true);
  s.ativa = p.getBool("s_ativa", false);
  s.lider = p.getBool("s_lider", false);
  String n = p.getString("s_gnome", "");
  String c = p.getString("s_gcod", "");
  p.end();
  strncpy(s.gNome, n.c_str(), sizeof(s.gNome) - 1); s.gNome[sizeof(s.gNome) - 1] = 0;
  strncpy(s.gCod,  c.c_str(), sizeof(s.gCod)  - 1); s.gCod[sizeof(s.gCod)  - 1] = 0;
  if (!s.gNome[0]) s.ativa = false;      // sessao sem grupo nao existe
  s.desde = millis();
}

inline void sessaoSalva(const Sessao& s) {
  Preferences p;
  p.begin("grupo", false);
  p.putBool("s_ativa", s.ativa);
  p.putBool("s_lider", s.lider);
  p.putString("s_gnome", s.gNome);
  p.putString("s_gcod", s.gCod);
  p.end();
}

inline void sessaoEncerra(Sessao& s) {
  s.ativa = false;
  s.gNome[0] = 0;
  s.gCod[0] = 0;
  sessaoSalva(s);
}

// Onde o trajeto vai morar quando o GPS entrar. Um arquivo por grupo, no cartao:
// ao religar no meio da trilha, o mapa volta inteiro em vez de comecar do zero.
// AINDA NAO IMPLEMENTADO - o cartao so entra depois do sd_bench medir o custo de
// leitura com o painel ligado (docs/RETOMAR.md).
inline void trilhaArquivo(const Sessao& s, char* saida, size_t n) {
  snprintf(saida, n, "/mts/%s.trk", s.gCod[0] ? s.gCod : "sem");
}
