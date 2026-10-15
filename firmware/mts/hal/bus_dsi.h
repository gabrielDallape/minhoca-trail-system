#pragma once
// ============================================================================
//  BUS DSI (hal) - o barramento MIPI-DSI da tela, com a fonte de clock do PHY
//  escolhida pela REVISAO DO CHIP, e nao pela LovyanGFX instalada no PC.
// ============================================================================
//
// POR QUE EXISTE (medido na tela a, COM11, 2026-09-29): o firmware subia, o radio
// subia, e no primeiro passo do display a placa dava abort() e reiniciava - 124
// vezes em 150 s, tela preta. O backtrace:
//
//   abort <- _mipi_dsi_ll_set_phy_pllref_clock_source (mipi_dsi_ll.h:272)
//         <- esp_lcd_new_dsi_bus <- lgfx::Bus_DSI::init (Bus_DSI.cpp:59)
//
// A LovyanGFX DESTE PC tem um patch manual ("PATCH REDEA", 2026-09-20, de outro
// projeto) que troca a fonte do PHY de MIPI_DSI_PHY_CLK_SRC_DEFAULT (o legado,
// PLL_F20M) para MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT (XTAL), porque o P4 de
// producao (rev 3) nao tem o F20M. So que no core 3.3.11 o switch do
// mipi_dsi_ll.h conhece F20M / RC_FAST / F25M e mais nada: XTAL cai no default e
// chama abort(). As nossas telas sao P4 ANTIGOS (a ROM diz "esp32p4-eco2", rev
// < 3) - para elas o certo e o legado.
//
// A biblioteca e compartilhada com o outro projeto e nao e mexida. Esta classe
// herda do lgfx::Bus_DSI e reescreve SO o init(): cria o LDO do PHY, o barramento
// e o canal de comandos exatamente como o original, mas com a fonte de clock
// decidida em tempo de execucao pela revisao do chip. O resto (release, painel,
// envio de imagem) e o da biblioteca, intocado.
//
// O Bus_DSI guarda os handles em membros PRIVADOS, e o painel os le por
// getMipiDsiBus()/getPanelIo(). Para escrever neles sem editar a biblioteca, o
// truque padrao do C++: a instanciacao EXPLICITA de um template ignora o controle
// de acesso ([temp.explicit]/12), entao um ponteiro-para-membro privado pode ser
// "exportado" por uma funcao amiga. E legal, compila sem aviso, e se a
// LovyanGFX renomear os membros o erro aparece NA COMPILACAO, nunca em campo.

#include "lgfx/v1/platforms/esp32p4/Bus_DSI.hpp"
#include <esp_chip_info.h>
#include <string.h>

namespace mts_dsi {
  template <typename Tag, typename Tag::tipo M>
  struct Exporta { friend typename Tag::tipo pega(Tag) { return M; } };

  struct Barramento { typedef esp_lcd_dsi_bus_handle_t  lgfx::Bus_DSI::*tipo; friend tipo pega(Barramento); };
  struct Ldo        { typedef esp_ldo_channel_handle_t  lgfx::Bus_DSI::*tipo; friend tipo pega(Ldo); };
  struct Io         { typedef esp_lcd_panel_io_handle_t lgfx::Bus_DSI::*tipo; friend tipo pega(Io); };

  template struct Exporta<Barramento, &lgfx::Bus_DSI::_mipi_dsi_bus>;
  template struct Exporta<Ldo,        &lgfx::Bus_DSI::_phy_pwr_chan>;
  template struct Exporta<Io,         &lgfx::Bus_DSI::_io_dbi>;
}

class BusDsiMts : public lgfx::Bus_DSI
{
public:
  // Revisao do chip (major*100 + minor) e a fonte usada: o boot imprime os dois.
  int revisaoChip = -1;
  bool clockLegado = false;

  bool init(void) override
  {
    esp_lcd_dsi_bus_handle_t&  bus = this->*pega(mts_dsi::Barramento());
    esp_ldo_channel_handle_t&  ldo = this->*pega(mts_dsi::Ldo());
    esp_lcd_panel_io_handle_t& io  = this->*pega(mts_dsi::Io());
    if (bus) return true;

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    revisaoChip = chip.revision;
    // rev >= 3: o F20M nao existe, vai o PLLREF (XTAL) - o que o PATCH REDEA queria.
    // rev <  3 (as nossas, eco2): o legado, como a LovyanGFX original.
    clockLegado = chip.revision < 300;

    const auto& c = config();
    esp_lcd_dsi_bus_config_t bus_config;
    memset(&bus_config, 0, sizeof(bus_config));
    bus_config.bus_id = c.bus_id;
    bus_config.num_data_lanes = c.lane_num;
    bus_config.phy_clk_src = clockLegado
      ? static_cast<decltype(bus_config.phy_clk_src)>(MIPI_DSI_PHY_CLK_SRC_DEFAULT)
      : static_cast<decltype(bus_config.phy_clk_src)>(MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT);
    bus_config.lane_bit_rate_mbps = c.lane_mbps;

    esp_ldo_channel_config_t ldo_cfg;
    memset(&ldo_cfg, 0, sizeof(ldo_cfg));
    ldo_cfg.chan_id = c.ldo_chan_id;
    ldo_cfg.voltage_mv = c.ldo_voltage_mv;

    esp_lcd_dbi_io_config_t dbi_config;
    memset(&dbi_config, 0, sizeof(dbi_config));
    dbi_config.virtual_channel = 0;
    dbi_config.lcd_cmd_bits = c.lcd_cmd_bits;
    dbi_config.lcd_param_bits = c.lcd_param_bits;

    if (ESP_OK == esp_ldo_acquire_channel(&ldo_cfg, &ldo)
     && ESP_OK == esp_lcd_new_dsi_bus(&bus_config, &bus)
     && ESP_OK == esp_lcd_new_panel_io_dbi(bus, &dbi_config, &io))
      return true;
    release();
    return false;
  }
};
