/*
 * PRIMEIRO SINAL DE VIDA no ESP32-P4.
 *
 * Nao precisa de radio, GPS, cartao nem fiacao nenhuma - so a placa e o cabo USB.
 * Serve para tres coisas, nesta ordem de importancia:
 *
 *   1. Provar que a placa grava e fala pelo serial (se isto nao passa, nada mais
 *      importa).
 *   2. CONFERIR contra a medida o que a pesquisa de datasheet afirmou sobre este
 *      chip. Cada linha marcada [confere] imprime o valor esperado ao lado do
 *      medido, entao uma divergencia salta aos olhos em vez de virar bug depois.
 *   3. Validar a pinagem do docs/MONTAGEM.md: os 12 GPIOs escolhidos existem
 *      neste chip, aceitam entrada e saida, e nenhum deles e strapping.
 *
 *   .\tools\build.ps1 firmware\p4_hello -Board p4 -Upload -Port COMx
 */
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"
#include "driver/gpio.h"

// ------------------------------------------------ o que a pesquisa afirmou
// Fonte de cada numero, para a divergencia ter onde ser conferida:
//   PSRAM 32 MB  -> esquematico Waveshare (ESP32-P4NRW32) + datasheet tab. 1-1
//   flash 32 MB  -> GD25Q256 no esquematico das tres placas
//   2 nucleos    -> datasheet sec. 4.1.1.1 ("dual-core", ate 360 MHz)
//   55 GPIOs     -> datasheet sec. 4.1.4.1 (GPIO0..GPIO54, sem lacuna)
#define ESPERA_PSRAM_MB   32
#define ESPERA_FLASH_MB   32
#define ESPERA_CORES       2
#define ESPERA_GPIO_COUNT 55

// ------------------------------------------------ a pinagem do MONTAGEM.md
struct PinoPlano { const char* nome; int gpio; };
static const PinoPlano PLANO[] = {
  { "E22 NSS  ", 28 }, { "E22 MOSI ", 29 }, { "E22 SCK  ", 30 }, { "E22 MISO ", 31 },
  { "E22 BUSY ", 49 }, { "E22 DIO1 ", 50 }, { "E22 NRST ", 51 }, { "E22 TXEN ", 52 },
  { "E22 RXEN ",  5 },
  { "GPS RX   ",  4 }, { "GPS TX   ",  3 }, { "GPS PPS  ",  2 },
};
static const int N_PLANO = sizeof(PLANO) / sizeof(PLANO[0]);

// GPIOs que o datasheet (sec. 3, tab. 3-1) marca como strapping. Se algum da
// pinagem cair aqui, o boot fica refem do que estiver ligado no fio.
static bool ehStrapping(int g){ return g == 34 || g == 35 || g == 36 || g == 37 || g == 38; }

// GPIOs no banco VDD_IO_5, que nas placas Waveshare e alimentado pelo LDO interno
// VO4 - o mesmo que comuta o cartao SD entre 3,3 V e 1,8 V. Ver docs/MONTAGEM.md.
static bool ehBancoLdo(int g){ return g >= 39 && g <= 48; }

int gOk = 0, gRuim = 0;
static void confere(bool ok, const char* oque, const char* detalhe){
  if (ok) { gOk++;   Serial.printf("  OK    %-34s %s\n", oque, detalhe); }
  else    { gRuim++; Serial.printf("  ALERTA %-33s %s   <-------\n", oque, detalhe); }
}

void setup(){
  Serial.begin(115200);
  // O console sai pelo USB Serial/JTAG nativo (GPIO24/25). Sem esta espera as
  // primeiras linhas se perdem enquanto o host ainda esta enumerando a porta.
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) delay(10);
  delay(400);

  Serial.println("\n=============================================");
  Serial.println(" ESP32-P4 - primeiro sinal de vida");
  Serial.println(" MTS - Minhoca Trail System");
  Serial.println("=============================================");

  // ---------------------------------------------------------------- 1. chip
  esp_chip_info_t info;
  esp_chip_info(&info);
  Serial.println("\n[1] identidade do chip");
  Serial.printf("  modelo        : %s\n", ESP.getChipModel());
  Serial.printf("  revisao       : %d\n", ESP.getChipRevision());
  Serial.printf("  nucleos       : %d\n", info.cores);
  Serial.printf("  clock da CPU  : %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  Serial.printf("  SDK           : %s\n", ESP.getSdkVersion());

  char det[64];
  snprintf(det, sizeof(det), "medido=%d  esperado=%d", info.cores, ESPERA_CORES);
  confere(info.cores == ESPERA_CORES, "[confere] 2 nucleos (datasheet)", det);

  // ------------------------------------------------------------- 2. memoria
  Serial.println("\n[2] memoria");
  size_t psram = ESP.getPsramSize();
  size_t flash = ESP.getFlashChipSize();
  Serial.printf("  PSRAM total   : %u bytes (%.1f MB)\n", (unsigned)psram, psram / 1048576.0);
  Serial.printf("  PSRAM livre   : %u bytes\n", (unsigned)ESP.getFreePsram());
  Serial.printf("  flash         : %u bytes (%.1f MB)\n", (unsigned)flash, flash / 1048576.0);
  Serial.printf("  heap livre    : %u bytes\n", (unsigned)ESP.getFreeHeap());

  // A PSRAM do P4 e INTERNA ao encapsulamento (datasheet sec. 2.7: "PSRAM is not
  // pinned out"). E a diferenca estrutural com o S3, onde a PSRAM octal externa
  // custava 12 GPIOs. Se vier 0 aqui, algo esta errado no FQBN, nao na placa.
  snprintf(det, sizeof(det), "medido=%.0f MB  esperado=%d MB", psram / 1048576.0, ESPERA_PSRAM_MB);
  confere(psram >= (size_t)ESPERA_PSRAM_MB * 1048576 * 0.9, "[confere] PSRAM interna de 32 MB", det);
  snprintf(det, sizeof(det), "medido=%.0f MB  esperado=%d MB", flash / 1048576.0, ESPERA_FLASH_MB);
  confere(flash >= (size_t)ESPERA_FLASH_MB * 1048576 * 0.9, "[confere] flash de 32 MB (GD25Q256)", det);

  // ----------------------------------------------------------------- 3. GPIO
  Serial.println("\n[3] matriz de GPIO");
  Serial.printf("  SOC_GPIO_PIN_COUNT : %d\n", SOC_GPIO_PIN_COUNT);
  snprintf(det, sizeof(det), "medido=%d  esperado=%d", SOC_GPIO_PIN_COUNT, ESPERA_GPIO_COUNT);
  confere(SOC_GPIO_PIN_COUNT == ESPERA_GPIO_COUNT, "[confere] 55 GPIOs, sem lacuna", det);

  // ------------------------------------------------------- 4. pinagem do doc
  Serial.println("\n[4] pinagem do docs/MONTAGEM.md neste chip");
  bool planoOk = true;
  for (int i = 0; i < N_PLANO; i++) {
    int g = PLANO[i].gpio;
    bool valido = GPIO_IS_VALID_GPIO(g);
    bool saida  = GPIO_IS_VALID_OUTPUT_GPIO(g);
    bool strap  = ehStrapping(g);
    bool ldo    = ehBancoLdo(g);
    bool bom    = valido && saida && !strap && !ldo;
    if (!bom) planoOk = false;
    Serial.printf("  GPIO %-2d  %s  %s%s%s%s\n", g, PLANO[i].nome,
      valido ? "valido " : "INVALIDO ",
      saida  ? "e/s "    : "SO-ENTRADA ",
      strap  ? "STRAPPING " : "",
      ldo    ? "BANCO-DO-LDO " : "");
  }
  confere(planoOk, "[confere] os 12 pinos do plano servem", "nenhum invalido/strapping/LDO");

  // Prova o que o Estudo 2 afirmou: nenhum GPIO do P4 e so-entrada (ao contrario
  // do ESP32 classico, onde 34-39 eram). Se isto falhar, a tabela do doc mente.
  int soEntrada = 0;
  for (int g = 0; g < SOC_GPIO_PIN_COUNT; g++)
    if (GPIO_IS_VALID_GPIO(g) && !GPIO_IS_VALID_OUTPUT_GPIO(g)) soEntrada++;
  snprintf(det, sizeof(det), "encontrados=%d  esperado=0", soEntrada);
  confere(soEntrada == 0, "[confere] nenhum GPIO e so-entrada", det);

  // --------------------------------------------------------- 5. base de tempo
  // esp_timer e o relogio que o tdma_core.h usa. Aqui so provamos que ele anda e
  // com que granularidade. O ppm do cristal - o numero que dimensiona a guarda de
  // holdover - NAO da para medir sem o PPS do GPS; ver docs/MONTAGEM.md passo 8.
  Serial.println("\n[5] base de tempo (a que o TDMA usa)");
  int64_t a = esp_timer_get_time();
  delay(100);
  int64_t b = esp_timer_get_time();
  int64_t dt = b - a;
  Serial.printf("  esp_timer em 100 ms de delay(): %lld us\n", (long long)dt);
  snprintf(det, sizeof(det), "medido=%lld us  esperado ~100000", (long long)dt);
  confere(dt > 95000 && dt < 105000, "[confere] esp_timer anda e bate com delay()", det);

  int64_t menorPasso = 0;
  int64_t p0 = esp_timer_get_time(), p1;
  do { p1 = esp_timer_get_time(); } while (p1 == p0);
  menorPasso = p1 - p0;
  Serial.printf("  menor passo observado        : %lld us\n", (long long)menorPasso);
  Serial.println("  (o systimer conta a 16 MHz = 62,5 ns e esp_timer trunca para us)");

  // ------------------------------------------------------------------ resumo
  Serial.println("\n=============================================");
  Serial.printf(" RESULTADO: %d OK, %d ALERTA\n", gOk, gRuim);
  if (gRuim == 0) {
    Serial.println(" A placa e o que a pesquisa dizia que era.");
    Serial.println(" Proximo passo: gravar o tdma_selftest nesta placa");
    Serial.println(" (.\\tools\\build.ps1 firmware\\tdma_selftest -Board p4 -Upload -Port COMx)");
  } else {
    Serial.println(" HA DIVERGENCIA - a documentacao do repo precisa de correcao,");
    Serial.println(" ou o FQBN/placa nao e o que se supos. Ver linhas com <-------");
  }
  Serial.println("=============================================");
}

void loop(){
  // pulso lento no serial: prova que a placa continua viva e nao entrou em
  // reboot loop depois do setup (o que aconteceria, por exemplo, se a fonte
  // nao aguentasse - sintoma que vamos reencontrar quando o E22 entrar).
  static uint32_t n = 0;
  Serial.printf("[vivo] %lu s  heap=%u\n", (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap());
  delay(5000);
  n++;
}
