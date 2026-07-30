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
```

`CDCOnBoot=cdc` manda o `Serial` pela USB nativa e **libera GPIO 43/44** (UART0)
para uso como GPIO. O cabecalho de `grupo_ws.ino:7` diz `CDCOnBoot=default`, o
`README.md:111` diz `cdc` — o `cdc` e o correto quando 43/44 estao no LoRa.

Nao usar `EraseFlash`: preserva a particao `nvs` (nome da tela, tema, sala).
