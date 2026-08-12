# Ambiente de build

> Estado verificado em **2026-07-30** na maquina de desenvolvimento (Windows 11).
> Este arquivo existe porque o ambiente foi encontrado **vazio** (sem o core ESP32
> e sem 3 das 4 bibliotecas) e nada compilava. Registrar as versoes evita repetir
> o diagnostico.

## Ferramenta

`arduino-cli` **1.4.1** vive dentro da instalacao da Arduino IDE e **nao esta no PATH**:

```
C:\Users\gadal\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
```

Use `tools\build.ps1`, que ja carrega esse caminho e o FQBN:

```powershell
.\tools\build.ps1 firmware\grupo_ws                              # compila
.\tools\build.ps1 firmware\e22_ping -Board devkit                # outra placa
.\tools\build.ps1 firmware\grupo_ws -Upload -Port COM7           # grava
.\tools\build.ps1 x -ListPorts                                   # acha a porta
```

## Cores

| Core | Versao |
|---|---|
| `esp32:esp32` | **3.3.11** |
| `arduino:megaavr` | 1.8.8 (legado GIGA/UNO) |
| `arduino:renesas_uno` | 1.5.3 (legado) |

Instalado com o indice adicional da Espressif:

```powershell
& $cli core install esp32:esp32 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

## Bibliotecas

| Biblioteca | Versao | Origem | Usada por |
|---|---|---|---|
| LovyanGFX | **1.2.21** | Library Manager | `grupo_ws` (painel RGB + sprite) |
| TinyGPSPlus | **1.0.3** | Library Manager | `grupo_ws` (NMEA) |
| RadioLib | **7.6.0** | Library Manager | `e22_ping` e a pilha TDMA (SX1262) |
| LoRaMESH | header-only | [Radioenge/LoRaMESH](https://github.com/Radioenge/LoRaMESH) (`master.zip`) | `grupo_ws` e todo o `legacy/` |

**LoRaMESH nao esta no Library Manager** — baixar o zip do GitHub e extrair em
`Documents\Arduino\libraries\LoRaMESH\`. O header oficial da Radioenge tem todas
as 16 funcoes que este repo usa (`localread`, `config_bps`, `read_config_bps`,
`setnetworkId`, `get_gpio_status`, `PrepareFrameCommand`, ...) — nao e preciso o
fork do elcereza.

**Nota sobre LovyanGFX:** o comentario em `firmware/grupo_ws/LGFX_WS7B.h:56-59`
fala da versao 1.2.24 (timings oficiais do 7B estourando `int8_t`). O Library
Manager entrega **1.2.21**, e `grupo_ws` compila normalmente nela. Se um dia
subir de versao, revalidar aqueles timings.

## Tamanho dos binarios

Medido com `PartitionScheme=huge_app` (limite de 3.145.728 bytes):

| Sketch | Flash | % de 3 MB | RAM global |
|---|---|---|---|
| `firmware/grupo_ws` | **449.111 B** (439 KB) | 14 % | 46.164 B (14 %) |
| `firmware/e22_ping` | **353.287 B** (345 KB) | 11 % | 24.524 B (7 %) |
| `firmware/p4_hello` (P4) | **328.198 B** (321 KB) | 10 % | 23.592 B (7 %) |
| `firmware/tdma_selftest` (P4) | **332.918 B** (325 KB) | 10 % | 23.600 B (7 %) |

**Consequencia para OTA (futuro):** o app esta muito longe do limite, entao **nao
e preciso `partitions.csv` customizado** — qualquer esquema com dois slots serve
com folga. `app3M_fat9M_16MB` (3 MB/slot) da ~7x de margem; ate `min_spiffs`
(1,875 MB/slot) daria 4x. Trocar o esquema ainda exige **uma gravacao por cabo**,
porque a tabela de particoes fica em 0x8000, fora de qualquer slot de app.

## FQBN

```
# telas em producao (Waveshare ESP32-S3-Touch-LCD-7B, N16R8)
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc

# bancada de radio (ESP32-S3-DevKitC-1, N8R8)
esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=huge_app,CDCOnBoot=cdc

# telas da fase 4 (Waveshare ESP32-P4-WIFI6-Touch-LCD-5/7/7B, ESP32-P4NRW32)
esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=huge_app,USBMode=default,CDCOnBoot=default
```

### P4: duas armadilhas, as duas medidas em placa

**`PSRAM=enabled` nao e opcional.** A PSRAM do P4 e interna ao encapsulamento
(datasheet §2.7: *"PSRAM is not pinned out"*) — o que **nao** significa que venha
ligada. O padrao do core e `PSRAM=disabled`, e sem a opcao o
`ESP.getPsramSize()` devolve **0** e o mapa nao tem onde caber. Nada avisa: nao ha
erro de compilacao nem de boot. Descoberto gravando o `p4_hello`, que compara o
medido com o que o datasheet promete.

**`CDCOnBoot=default` (desligado) e proposital aqui**, ao contrario do S3. Manda o
`Serial` para o UART0, que na placa vai para a ponte CH343P e aparece como
`USB-Enhanced-SERIAL CH343` (VID **1A86**). Com `CDCOnBoot=cdc` o log sai pelo USB
Serial/JTAG nativo (GPIO 24/25) e **a COM da CH343 fica muda** — o sintoma e
"gravou, verificou o hash e nao imprime nada".

Medido na placa de 5" (2026-08-12): ESP32-P4 rev **103** (v1.3), 2 nucleos a
360 MHz, SDK v5.5.5, PSRAM 32 MB, flash 32 MB, `SOC_GPIO_PIN_COUNT` = 55, nenhum
GPIO so-entrada, `esp_timer` com passo observado de 2 us.

`CDCOnBoot=cdc` manda o `Serial` pela USB nativa e **libera GPIO 43/44** (UART0)
para uso como GPIO. O cabecalho de `grupo_ws.ino:7` diz `CDCOnBoot=default`, o
`README.md:111` diz `cdc` — o `cdc` e o correto quando 43/44 estao no LoRa.

Nao usar `EraseFlash`: preserva a particao `nvs` (nome da tela, tema, sala).
