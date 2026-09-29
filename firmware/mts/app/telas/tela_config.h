#pragma once
// ============================================================================
//  TELA DE CONFIGURACAO - nome, cor, slot e (wifi | dia/noite)
// ============================================================================
// QUATRO LINHAS dividindo o espaco que sobra, nunca posicao cravada. Desenho
// completo UMA vez; depois so a linha que mudou.

#include "ui.h"
#include "teclado.h"
#include "../estado.h"
#include "../config.h"
#include "../rede.h"
#if MTS_TEM_WIFI
#include "tela_wifi.h"
#endif

template <typename TFT>
void telaConfig(TFT& tft)
{
  const int lw = TELA_W - 2 * M, topo = CAB_H + 24, base = TELA_H - 48 - 76 - 24, vao = 12;
  const int lh = (base - topo - 3 * vao) / 4;
  auto linha = [&](int i) -> Ret { return Ret{ M, (int16_t)(topo + i * (lh + vao)), (int16_t)lw, (int16_t)lh }; };
  Ret rNome = linha(0), rCor = linha(1), rSlot = linha(2), rQuarta = linha(3);
  Ret rVolta = { M, (int16_t)(TELA_H - 48 - 76), 280, 76 };

  auto pintaSlot = [&]() {
    char v[24];
    snprintf(v, sizeof(v), "slot %d de 0..%d", g_cfg.slot, FROTA_MAX_CARROS - 1);
    linhaCfg(tft, rSlot, "SLOT NA REDE (um por carro)", v, "tocar para trocar >", true);
  };
  auto desenhaTudo = [&]() {
    tft.fillScreen(C_BG);
    cabecalho(tft, "configuracao", false);
    tft.drawFastHLine(0, CAB_H, tft.width(), C_LINE);
    linhaCfg(tft, rNome, "NOME DESTE APARELHO", g_cfg.nome, "tocar para mudar >", true);
    linhaCfg(tft, rCor,  "COR NO MAPA", CORES_NOME[g_cfg.cor], "tocar para mudar >", true, CORES_MAPA[g_cfg.cor]);
    pintaSlot();
#if MTS_TEM_WIFI
    {
      char st[64]; const char* v;
      if (!g_wifiQuer) v = "nenhuma rede salva";
      else { snprintf(st, sizeof(st), "%s", g_wifiSsid); v = st; }
      linhaCfg(tft, rQuarta, "WIFI (atualizar pelo ar)", v, wifiConectado() ? "conectado >" : "tocar para escolher >", true);
    }
#else
    linhaCfg(tft, rQuarta, g_tema ? "MODO NOITE" : "MODO DIA",
             g_tema ? "fundo escuro, feicoes claras" : "fundo claro, feicoes escuras", "tocar para trocar >", true);
#endif
    botao(tft, rVolta, "< VOLTAR", "", C_INK2, false);
    tft.setTextDatum(top_right); tft.setFont(&fonts::FreeSans9pt7b); tft.setTextColor(C_INK3);
    tft.drawString("gravado no aparelho", tft.width() - M, TELA_H - 40);
    tft.setFont(&fonts::Font0);
  };
  desenhaTudo();

  while (true) {
    int16_t x, y;
    if (!esperaToque(tft, x, y)) continue;
    if (dentro(rVolta, x, y)) return;
    if (dentro(rNome, x, y)) {
      char tmp[sizeof(g_cfg.nome)];
      strncpy(tmp, g_cfg.nome, sizeof(tmp));
      if (tecladoTexto(tft, "nome do aparelho", tmp, sizeof(tmp))) {
        if (!strlen(tmp)) strcpy(tmp, "Carro");
        strncpy(g_cfg.nome, tmp, sizeof(g_cfg.nome) - 1); g_cfg.nome[sizeof(g_cfg.nome) - 1] = 0;
        // vale no AR tambem, nao so na tela: senao o nome antigo continuaria nos outros
        frotaIdentidade(g_frota, g_cfg.nome, g_cfg.cor, g_cfg.slot);
        configGrava();
        Serial.printf("nome: %s\n", g_cfg.nome);
      }
      desenhaTudo();
      continue;
    }
    if (dentro(rCor, x, y)) {
      g_cfg.cor = (uint8_t)((g_cfg.cor + 1) % N_CORES);
      frotaIdentidade(g_frota, g_cfg.nome, g_cfg.cor, g_cfg.slot);
      configGrava();
      linhaCfg(tft, rCor, "COR NO MAPA", CORES_NOME[g_cfg.cor], "tocar para mudar >", true, CORES_MAPA[g_cfg.cor]);
      continue;
    }
    if (dentro(rSlot, x, y)) {
      // dois aparelhos no MESMO slot transmitem um por cima do outro e nenhum ve o outro
      g_cfg.slot = (uint8_t)((g_cfg.slot + 1) % FROTA_MAX_CARROS);
      redeTrocaSlot(g_cfg.slot);          // aplica no ar, sem reiniciar
      configGrava();
      pintaSlot();
      continue;
    }
    if (dentro(rQuarta, x, y)) {
#if MTS_TEM_WIFI
      telaWifi(tft);
      desenhaTudo();
#else
      aplicaTema(g_tema ? 0 : 1);
      g_cfg.tema = g_tema;
      configGrava();
      desenhaTudo();
#endif
      continue;
    }
  }
}
