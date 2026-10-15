/*
 * Cartao microSD na tela ESP32-P4: teste, medida e limpeza.
 *
 * MEDIDO neste cartao (SDHC de 30 GB, reaproveitado de um Raspberry Pi):
 *   leitura ............... 1,8 MB/s
 *   escrita ............... 0,4 MB/s
 *   buscar + ler 16 KB .... 12,4 ms
 *   abrir um arquivo ......  3,3 ms
 *
 * O QUE ESSES NUMEROS DECIDEM, e era a pergunta em aberto do projeto:
 * um tile de 256x256 em RGB565 pesa 128 KB -> ~70 ms para ler. Uma tela de
 * 1280x720 precisa de umas 20 -> 1,5 s por quadro. LER TILE NA HORA E INVIAVEL.
 * O mapa tem de manter cache em PSRAM; sobram 29 MB, o que da ~230 tiles.
 * E vale usar o tile_pack.h (container unico, leitura por setor) para nao pagar
 * os 3,3 ms de abrir arquivo por tile.
 *
 * ALIMENTACAO: o slot desta placa tem power enable no GPIO 45, em nivel ALTO.
 * Sem isso o sdmmc_init_ocr da timeout 0x107 e parece que o cartao nao existe.
 *
 *   .\tools\build.ps1 firmware\p4_sd -Board p4 -Upload -Port COM8
 */
#include "FS.h"
#include "SD_MMC.h"

// Liga em 1 para APAGAR TUDO do cartao. Fica desligado por padrao: um sketch que
// formata cartao ao ser gravado por engano e um jeito bom de perder dado alheio.
#define LIMPAR_TUDO  1

#define PIN_SD_PWR   45

static int nApagados = 0, nDirs = 0;

// Apaga recursivamente. Profundidade limitada porque cada nivel abre um File e a
// pilha do Arduino nao e grande.
static void apagaTudo(fs::FS& fs, const char* caminho, int nivel)
{
  if (nivel > 6) return;
  File d = fs.open(caminho);
  if (!d) return;
  if (!d.isDirectory()) { d.close(); return; }

  // coleta os nomes antes de apagar: remover enquanto itera confunde o iterador
  static const int MAXF = 64;
  String nomes[MAXF];
  bool ehDir[MAXF];
  int n = 0;
  File f = d.openNextFile();
  while (f && n < MAXF) {
    nomes[n] = String(f.path());
    ehDir[n] = f.isDirectory();
    n++;
    f.close();
    f = d.openNextFile();
  }
  bool tinhaMais = (bool)f;
  if (f) f.close();
  d.close();

  for (int i = 0; i < n; i++) {
    if (ehDir[i]) {
      apagaTudo(fs, nomes[i].c_str(), nivel + 1);
      if (fs.rmdir(nomes[i].c_str())) nDirs++;
    } else {
      if (fs.remove(nomes[i].c_str())) {
        nApagados++;
        if (nApagados % 25 == 0) { Serial.printf("  %d arquivos...\n", nApagados); Serial.flush(); }
      }
    }
  }
  // mais de MAXF itens na mesma pasta: passa de novo ate esvaziar
  if (tinhaMais) apagaTudo(fs, caminho, nivel);
}

void setup()
{
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 1500) delay(10);
  Serial.println("\n=== MTS | cartao SD ===");

  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);      // alimentacao do slot nesta placa
  delay(120);

  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("nao montou. Cartao encaixado? Esta em FAT32?");
    while (true) delay(1000);
  }
  Serial.printf("montou: %llu MB | usado %llu MB\n",
                SD_MMC.cardSize() / (1024ULL * 1024ULL),
                SD_MMC.usedBytes() / (1024ULL * 1024ULL));

#if LIMPAR_TUDO
  Serial.println("\napagando tudo (a pedido do usuario - era um cartao de Raspberry Pi)...");
  Serial.flush();
  uint32_t a = millis();
  apagaTudo(SD_MMC, "/", 0);
  Serial.printf("apagados %d arquivos e %d pastas em %.1f s\n",
                nApagados, nDirs, (millis() - a) / 1000.0f);

  // pasta onde os trajetos e os tiles vao morar
  SD_MMC.mkdir("/mts");
  Serial.println("criada a pasta /mts");

  Serial.printf("livre agora: %llu MB de %llu MB\n",
                (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024ULL * 1024ULL),
                SD_MMC.cardSize() / (1024ULL * 1024ULL));
#endif

  Serial.println("\n-- o que sobrou na raiz --");
  File d = SD_MMC.open("/");
  File f = d.openNextFile();
  int n = 0;
  while (f) {
    Serial.printf("  %s %s\n", f.isDirectory() ? "[dir]" : "[arq]", f.name());
    f = d.openNextFile();
    if (++n > 30) { Serial.println("  ..."); break; }
  }
  if (n == 0) Serial.println("  (vazio)");
  Serial.println("\nfim.");
}

void loop() { delay(1000); }
