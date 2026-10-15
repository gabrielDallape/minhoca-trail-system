/*
 * BENCH DE CARTAO + TILES na Waveshare ESP32-S3-Touch-LCD-7B.
 *
 * Existe para responder as TRES perguntas que travam a arquitetura de render do
 * mapa offline, e que nao tem resposta em documentacao nenhuma:
 *
 *   1) O CS do cartao esta no EXPANSOR I2C (EXIO4 do CH32V003, io_extension.h:46),
 *      nao num GPIO. Nenhuma biblioteca de SD espera isso. Da para inicializar
 *      com o CS comutado por I2C e depois deixa-lo assertado? O cartao exige CS
 *      ALTO durante os primeiros ~74 clocks, entao "prender em baixo" pode
 *      quebrar justamente a inicializacao. Aqui tentamos as duas estrategias e
 *      o serial diz qual funcionou.
 *
 *   2) Quanto custa DE VERDADE ler um tile (128 KB) e um grid 3x3 (1,18 MB) por
 *      SPI. A estimativa era 0,7 a 1,8 s - faixa larga demais para projetar.
 *
 *   3) O painel RGB glitcha durante a leitura? O Bus_RGB nao tem bounce buffer
 *      (LGFX_WS7B.h:58) e le o framebuffer direto da PSRAM por DMA; o PCLK ja
 *      foi baixado para 12 MHz por causa de starvation. Medimos o tempo de
 *      pushImage ANTES e DURANTE a leitura do cartao: se a banda de PSRAM
 *      estiver saturada, o push fica mais lento - e isso e um numero, nao
 *      impressao. O padrao de listras de 1px na tela deixa qualquer tearing
 *      ou deslocamento visivel a olho.
 *
 * Nao mexe em LoRa nem em GPS: usa so I2C (8/9), o painel e o SD (11/12/13).
 * Se houver um trilha.img gravado no cartao (tools/tiles/tiles.py pack + dd),
 * o teste 5 le tiles de verdade pelo tile_pack.h e confere o CRC.
 *
 *   .\tools\build.ps1 firmware\sd_bench -Board ws7b -Upload -Port COMx
 */
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "../grupo_ws/LGFX_WS7B.h"
#include "../tile_pack.h"

// ------------------------------------------------------------------ pinagem
#define PIN_SD_MOSI 11
#define PIN_SD_SCK  12
#define PIN_SD_MISO 13
// CS "dummy": a lib precisa de um pino; o CS real e o EXIO4 do expansor.
// GPIO16 nao e usado pelo painel nem pelo projeto - so serve de bode expiatorio.
#define PIN_CS_DUMMY 16

#define IOEXT_ADDR 0x24
#define IOEXT_MODE 0x02
#define IOEXT_OUT  0x03
#define IOEXT_SD_CS_BIT 0x10    // IO4 = CS do cartao (ativo baixo)
// Base do registro de saida com o painel ligado e o touch fora do reset,
// igual ao grupo_ws.ino:65 (0x5E). O bit 4 (CS do SD) fica setado = inativo.
#define IOEXT_BASE 0x5E

static LGFX        lcd;
static LGFX_Sprite spr(&lcd);
int SCR_W, SCR_H;

uint8_t  ioState = IOEXT_BASE;
uint8_t* tileBuf = NULL;          // 128 KB na PSRAM
SPIClass spiSD(HSPI);
bool     sdOk = false;
const char* sdStrategy = "nenhuma";

// contadores compartilhados com a task de push
volatile bool     pushRun = false;
volatile uint32_t pushCount = 0, pushWorstUs = 0, pushSumUs = 0;

// ------------------------------------------------------------------- utils
bool ioExt(uint8_t reg, uint8_t val){
  Wire.beginTransmission(IOEXT_ADDR); Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
// CS do cartao pelo expansor: cada troca custa uma transacao I2C (~25 us a
// 400 kHz). E justamente por isso que nao da para comutar por transacao SPI.
bool sdCs(bool active){
  if (active) ioState &= ~IOEXT_SD_CS_BIT;
  else        ioState |=  IOEXT_SD_CS_BIT;
  return ioExt(IOEXT_OUT, ioState);
}

void powerUpPanel(){
  Wire.begin(8, 9); Wire.setClock(400000);
  ioExt(IOEXT_MODE, 0xFF);            // todos os IOs como saida
  ioExt(IOEXT_OUT, 0x5C); delay(50);  // painel on, touch em reset
  ioExt(IOEXT_OUT, IOEXT_BASE);       // solta o touch; CS do SD fica alto
  ioState = IOEXT_BASE;
}

// mede quantos us uma transacao I2C de CS leva (entra na conta do custo por tile)
uint32_t benchCsToggleUs(){
  uint32_t t0 = micros();
  for (int i = 0; i < 100; i++) { sdCs(true); sdCs(false); }
  return (micros() - t0) / 200;
}

// ------------------------------------------------------------ tela de fundo
// Listras de 1px: qualquer tearing, deslocamento de fase ou linha perdida do
// painel RGB fica obvio a olho nu. Se durante a leitura do cartao a imagem
// "andar" ou virar chuvisco, e starvation de DMA da PSRAM.
void drawReference(){
  spr.fillScreen(TFT_BLACK);
  for (int y = 0; y < SCR_H; y += 2) spr.drawFastHLine(0, y, SCR_W, TFT_WHITE);
  for (int x = 0; x < SCR_W; x += 64) spr.drawFastVLine(x, 0, SCR_H, TFT_RED);
  spr.fillRect(20, 20, 420, 150, TFT_BLACK);
  spr.setTextColor(TFT_GREEN);
  spr.setFont(&fonts::FreeSansBold18pt7b);
  spr.drawString("BENCH SD + TILES", 30, 30);
  spr.setFont(&fonts::FreeSans12pt7b);
  spr.setTextColor(TFT_CYAN);
  spr.drawString("olhe as listras durante a leitura", 30, 80);
  spr.drawString("resultados no serial 115200", 30, 110);
}

// push por faixas, igual ao grupo_ws (10 faixas com respiro)
uint32_t pushFrame(){
  uint16_t* buf = (uint16_t*)spr.getBuffer();
  const int BANDS = 10;
  int bh = SCR_H / BANDS;
  uint32_t t0 = micros();
  for (int b = 0; b < BANDS; b++) {
    int y0 = b * bh, h = (b == BANDS - 1) ? (SCR_H - y0) : bh;
    lcd.pushImage(0, y0, SCR_W, h, buf + (size_t)y0 * SCR_W);
  }
  return micros() - t0;
}

// task que fica empurrando frames enquanto o outro core le o cartao ->
// permite medir a contencao de banda de PSRAM
void pushTask(void*){
  for (;;) {
    if (pushRun) {
      uint32_t dt = pushFrame();
      pushCount++; pushSumUs += dt;
      if (dt > pushWorstUs) pushWorstUs = dt;
    } else {
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }
  }
}
void pushStatsReset(){ pushCount = 0; pushWorstUs = 0; pushSumUs = 0; }

// ------------------------------------------------------------ init do cartao
// ESTRATEGIA A (pelo protocolo): CS ALTO + clocks dummy, depois CS baixo.
// O cartao SD exige >=74 clocks com CS alto para entrar em modo SPI.
bool sdInitStrategyA(){
  sdCs(false);                                  // CS ALTO (inativo)
  spiSD.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_CS_DUMMY);
  spiSD.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 12; i++) spiSD.transfer(0xFF);   // 96 clocks > 74
  spiSD.endTransaction();
  sdCs(true);                                   // CS BAIXO (ativo) e mantem
  return SD.begin(PIN_CS_DUMMY, spiSD, 20000000);
}

// ESTRATEGIA B (ingenua): CS baixo desde o inicio.
bool sdInitStrategyB(){
  sdCs(true);
  spiSD.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_CS_DUMMY);
  return SD.begin(PIN_CS_DUMMY, spiSD, 20000000);
}

// -------------------------------------------------------------- leitor raw
// SD.readRAW le UM setor por chamada - o overhead por chamada e parte do que
// estamos medindo. Se ficar lento, o proximo passo e sdmmc_read_sectors() da
// API do ESP-IDF, que le N setores numa transacao.
bool readSectors(uint32_t start, uint32_t count, void* dst){
  uint8_t* p = (uint8_t*)dst;
  for (uint32_t i = 0; i < count; i++) {
    if (!SD.readRAW(p + (size_t)i * 512, start + i)) return false;
  }
  return true;
}
// adaptador para o tile_pack.h
bool tilePackReaderSD(uint32_t startSector, uint32_t count, void* dst, void* user){
  (void)user;
  return readSectors(startSector, count, dst);
}

// --------------------------------------------------------------------- bench
void benchRaw(){
  Serial.println("\n[3] leitura crua do cartao");
  static uint8_t sec[512];

  // 1 setor
  uint32_t t0 = micros();
  bool ok = SD.readRAW(sec, 0);
  uint32_t dt1 = micros() - t0;
  Serial.printf("  1 setor (512 B) : %s %lu us\n", ok ? "OK" : "FALHOU", (unsigned long)dt1);

  // 256 setores = 1 tile de 256x256 RGB565
  t0 = micros();
  ok = readSectors(0, 256, tileBuf);
  uint32_t dtTile = micros() - t0;
  Serial.printf("  1 tile (128 KB) : %s %lu us (%.1f ms) -> %.2f MB/s\n",
    ok ? "OK" : "FALHOU", (unsigned long)dtTile, dtTile / 1000.0,
    dtTile ? (131072.0 / dtTile) : 0.0);

  // grid 3x3 = 9 tiles = 1,18 MB (o carregamento que acontece ao cruzar borda)
  t0 = micros();
  bool all = true;
  for (int i = 0; i < 9; i++) if (!readSectors((uint32_t)i * 256, 256, tileBuf)) { all = false; break; }
  uint32_t dtGrid = micros() - t0;
  Serial.printf("  grid 3x3 (1,18 MB): %s %lu ms -> %.2f MB/s\n",
    all ? "OK" : "FALHOU", (unsigned long)(dtGrid / 1000),
    dtGrid ? (9.0 * 131072.0 / dtGrid) : 0.0);
  Serial.printf("  >> um tile custa ~%lu ms; o grid inteiro ~%lu ms\n",
    (unsigned long)(dtTile / 1000), (unsigned long)(dtGrid / 1000));
  if (dtGrid > 300000)
    Serial.println("  >> LENTO: nao da para carregar grid no caminho do frame. "
                   "Carregar em task separada, 1 tile por vez, ao cruzar borda.");
}

void benchContention(){
  Serial.println("\n[4] contencao: painel x leitura do cartao");

  // baseline: so o painel, sem tocar no cartao
  pushStatsReset(); pushRun = true;
  delay(3000);
  pushRun = false;
  uint32_t baseN = pushCount, baseAvg = pushCount ? pushSumUs / pushCount : 0, baseWorst = pushWorstUs;
  Serial.printf("  baseline    : %lu frames em 3s | push medio %lu us | pior %lu us\n",
    (unsigned long)baseN, (unsigned long)baseAvg, (unsigned long)baseWorst);

  // agora o mesmo push, mas lendo o cartao sem parar no outro core
  pushStatsReset(); pushRun = true;
  uint32_t t0 = millis(); uint32_t reads = 0;
  while (millis() - t0 < 3000) { if (readSectors(0, 256, tileBuf)) reads++; }
  pushRun = false;
  uint32_t loadN = pushCount, loadAvg = pushCount ? pushSumUs / pushCount : 0, loadWorst = pushWorstUs;
  Serial.printf("  lendo SD    : %lu frames em 3s | push medio %lu us | pior %lu us (%lu tiles lidos)\n",
    (unsigned long)loadN, (unsigned long)loadAvg, (unsigned long)loadWorst, (unsigned long)reads);

  if (baseAvg && loadAvg) {
    float pen = (float)loadAvg / (float)baseAvg;
    Serial.printf("  >> push ficou %.2fx mais lento com o cartao em uso\n", pen);
    if (pen > 1.5f)
      Serial.println("  >> contencao ALTA de banda de PSRAM: parar o redraw durante o "
                     "carregamento, ou apagar o backlight.");
    else
      Serial.println("  >> contencao baixa: da para carregar tile em background sem "
                     "parar o desenho.");
  }
  Serial.println("  >> CONFIRA A TELA: as listras andaram, piscaram ou viraram chuvisco?");
}

void benchTilePack(){
  Serial.println("\n[5] tile_pack.h lendo o cartao de verdade");
  TilePack tp;
  if (!tilePackOpen(tp, tilePackReaderSD, NULL)) {
    Serial.println("  sem container TRILHAMP no cartao (setor 0 nao tem o magic).");
    Serial.println("  Para testar: python tools/tiles/tiles.py synth+pack e gravar o .img com dd.");
    return;
  }
  Serial.printf("  container OK: tile %ux%u, %lu tiles, %u niveis\n",
    tp.tilePx, tp.tilePx, (unsigned long)tp.nTiles, tp.nLevels);
  for (uint8_t i = 0; i < tp.nLevels; i++)
    Serial.printf("   z%u: grade %ux%u em x%lu y%lu\n", tp.levels[i].zoom,
      tp.levels[i].w, tp.levels[i].h,
      (unsigned long)tp.levels[i].xmin, (unsigned long)tp.levels[i].ymin);

  // le o primeiro tile do primeiro nivel, com CRC
  const TilePackLevel& L = tp.levels[0];
  uint32_t t0 = micros();
  TilePackResult r = tilePackReadTile(tp, L.zoom, L.xmin, L.ymin, tileBuf, true);
  uint32_t dt = micros() - t0;
  const char* names[] = {"OK", "AUSENTE", "ERRO DE IO", "CRC ERRADO"};
  Serial.printf("  tile z%u x%lu y%lu -> %s em %lu ms (com verificacao de CRC)\n",
    L.zoom, (unsigned long)L.xmin, (unsigned long)L.ymin, names[r], (unsigned long)(dt / 1000));

  // custo isolado do CRC (decide se vale checar em todo carregamento)
  t0 = micros();
  volatile uint32_t crc = tilePackCrc32(tileBuf, tilePackTileBytes(tp));
  uint32_t dtCrc = micros() - t0;
  (void)crc;
  Serial.printf("  CRC32 de 128 KB: %lu us (%.1f ms)\n", (unsigned long)dtCrc, dtCrc / 1000.0);

  // desenha o tile na tela: prova visual de que o pipeline inteiro fecha
  if (r == TILEPACK_OK) {
    spr.pushImage(SCR_W - tp.tilePx - 20, 20, tp.tilePx, tp.tilePx, (uint16_t*)tileBuf);
    pushFrame();
    Serial.println("  >> tile desenhado no canto superior direito da tela");
  }
  Serial.printf("  erros acumulados: crc=%lu io=%lu\n",
    (unsigned long)tp.crcErrors, (unsigned long)tp.readErrors);
}

void setup(){
  Serial.begin(115200);
  delay(600);
  Serial.println("\n=========================================");
  Serial.println(" BENCH SD + TILES (Waveshare 7B)");
  Serial.println("=========================================");

  powerUpPanel();
  lcd.init();
  SCR_W = lcd.width(); SCR_H = lcd.height();
  spr.setPsram(true); spr.setColorDepth(16);
  if (!spr.createSprite(SCR_W, SCR_H)) { Serial.println("ERRO: sprite nao alocou"); while (1) delay(1000); }
  drawReference();
  pushFrame();
  Serial.printf("painel %dx%d | PSRAM livre %lu KB | heap interno livre %lu KB\n",
    SCR_W, SCR_H,
    (unsigned long)(ESP.getFreePsram() / 1024),
    (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));

  tileBuf = (uint8_t*)ps_malloc(131072);
  if (!tileBuf) { Serial.println("ERRO: buffer de tile nao alocou na PSRAM"); while (1) delay(1000); }

  Serial.println("\n[1] CS do cartao pelo expansor I2C");
  Serial.printf("  uma troca de CS custa ~%lu us (transacao I2C a 400 kHz)\n",
    (unsigned long)benchCsToggleUs());
  Serial.println("  >> por isso NAO da para comutar CS por transacao SPI: o driver");
  Serial.println("     faria isso milhares de vezes por tile.");

  Serial.println("\n[2] inicializacao do cartao");
  if (sdInitStrategyA()) {
    sdOk = true; sdStrategy = "A (CS alto + 96 clocks dummy, depois CS baixo)";
  } else {
    Serial.println("  estrategia A falhou; tentando B");
    SD.end();
    if (sdInitStrategyB()) { sdOk = true; sdStrategy = "B (CS baixo desde o inicio)"; }
  }
  if (!sdOk) {
    Serial.println("  NENHUMA estrategia funcionou.");
    Serial.println("  Confira: cartao inserido e formatado, e se o CS do SD e mesmo o EXIO4.");
    Serial.println("  Proximo passo seria a API do IDF (sdspi_host + gpio_cs = GPIO_NUM_NC).");
    while (1) delay(2000);
  }
  Serial.printf("  FUNCIONOU com a estrategia %s\n", sdStrategy);
  Serial.printf("  tipo=%d tamanho=%llu MB\n", (int)SD.cardType(), SD.cardSize() / (1024ULL * 1024ULL));

  xTaskCreatePinnedToCore(pushTask, "push", 4096, NULL, 1, NULL, 0);   // painel no core 0

  benchRaw();
  benchContention();
  benchTilePack();

  Serial.println("\n=========================================");
  Serial.printf(" estrategia de CS: %s\n", sdStrategy);
  Serial.println(" Anote os numeros: eles decidem a arquitetura de render.");
  Serial.println("=========================================");

  pushRun = true;   // deixa o painel vivo para inspecao visual
}

void loop(){ delay(1000); }
