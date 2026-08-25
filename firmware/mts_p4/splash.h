// Tela de abertura do MTS.
//
// A arte vem em paleta de 16 cores com indices de 4 bits (logo_mts.h). Decodifica
// linha a linha para um buffer de uma linha so e empurra com pushImage: assim o
// custo de RAM e 2160 bytes, nao os 1,5 MB que a imagem teria em RGB565 inteiro.
//
// O fade e feito no BACKLIGHT, nao no pixel. Escurecer 777 mil pixels por quadro
// custaria caro; variar o PWM da luz e de graca e da o mesmo efeito.
#pragma once
#include "logo_mts.h"
#include "ui.h"       // esperaServindo: a abertura nao pode deixar o radio surdo

// Desenha a arte centralizada, com as laterais na mesma cor do fundo dela para a
// emenda nao aparecer (a tela e 1280 de largura, a arte 1080).
template <typename GFX>
void splashDesenhar(GFX& tft)
{
  const int x0 = (tft.width()  - MTS_LOGO_W) / 2;
  const int y0 = (tft.height() - MTS_LOGO_H) / 2;

  if (x0 > 0) {
    tft.fillRect(0, 0, x0, tft.height(), MTS_LOGO_BG);
    tft.fillRect(x0 + MTS_LOGO_W, 0, tft.width() - x0 - MTS_LOGO_W, tft.height(), MTS_LOGO_BG);
  }
  if (y0 > 0) {
    tft.fillRect(0, 0, tft.width(), y0, MTS_LOGO_BG);
    tft.fillRect(0, y0 + MTS_LOGO_H, tft.width(), tft.height() - y0 - MTS_LOGO_H, MTS_LOGO_BG);
  }

  static uint16_t linha[MTS_LOGO_W];
  const uint8_t* p = mtsLogoPix;
  tft.startWrite();
  for (int y = 0; y < MTS_LOGO_H; y++) {
    for (int x = 0; x < MTS_LOGO_W; x += 2) {
      uint8_t b = *p++;
      linha[x]     = mtsLogoPal[b >> 4];
      linha[x + 1] = mtsLogoPal[b & 0x0F];
    }
    // O CAST NAO E DECORATIVO. Um uint16_t* cru a LovyanGFX interpreta como
    // swap565_t (byte trocado - o formato de fio dos paineis SPI), e o painel DSI
    // e nativo rgb565. Sem o cast a arte sai ROSA: o laranja (vermelho alto, verde
    // medio) vira magenta quando os bytes trocam. Medido com faixas de cor
    // rotuladas na tela: fillRect e rgb565_t batem, swap565_t nao.
    tft.pushImage(x0, y0 + y, MTS_LOGO_W, 1, (const lgfx::rgb565_t*)linha);
  }
  tft.endWrite();
}

// Abertura: mostra e sai. SEM fade.
//
// Tinha um fade de entrada e saida aqui. Saiu a pedido - num aparelho de bancada
// e de trilha, animacao de abertura so atrasa quem quer usar. A luz fica apagada
// enquanto a arte e montada (senao da para ver ela pintando de cima para baixo) e
// acende de uma vez com o quadro pronto.
template <typename GFX>
void splashMostrar(GFX& tft, uint32_t seguraMs = 1400)
{
  tft.setBrightness(0);
  splashDesenhar(tft);
  tft.setBrightness(255);
  // Segura a arte SERVINDO o radio e o GPS: o radio ja esta no ar antes do
  // painel subir, e 2,4 s de delay cru custavam duas janelas de TDMA e ~2 KB de
  // NMEA logo no boot - justamente quando o no 0 esta ancorando a rede.
  esperaServindo(seguraMs);
  tft.fillScreen(TFT_BLACK);
}
