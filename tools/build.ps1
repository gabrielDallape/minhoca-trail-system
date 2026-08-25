# Build/upload dos sketches deste repo sem depender do PATH.
#
# Uso:
#   .\tools\build.ps1 firmware\grupo_ws
#   .\tools\build.ps1 firmware\e22_ping -Upload -Port COM7
#   .\tools\build.ps1 firmware\e22_ping -Board devkit
#
# Por que existe: o arduino-cli vive dentro da instalacao da Arduino IDE e nao
# esta no PATH; e as portas COM da Waveshare reenumeram (localizar por VID 303A).

param(
  [Parameter(Mandatory = $true, Position = 0)]
  [string]$Sketch,

  # ws7b   = Waveshare ESP32-S3-Touch-LCD-7B (telas em producao, PSRAM OPI 16M)
  # devkit = ESP32-S3-DevKitC-1 (bancada de radio/TDMA, sem display)
  # p4     = Waveshare ESP32-P4-WIFI6-Touch-LCD-5/7/7B (alvo da fase 4)
  [ValidateSet('ws7b', 'devkit', 'p4')]
  [string]$Board = 'ws7b',

  [switch]$Upload,
  [string]$Port,
  [switch]$ListPorts
)

$ErrorActionPreference = 'Stop'

$CLI = "C:\Users\gadal\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
if (-not (Test-Path $CLI)) { throw "arduino-cli nao encontrado em $CLI" }

# CDCOnBoot=cdc: o Serial sai pela USB nativa, liberando GPIO43/44 (UART0) como GPIO.
# EraseFlash NAO e usado: preserva a particao nvs (nome da tela, tema, sala).
$FQBN = switch ($Board) {
  'ws7b'   { "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc" }
  'devkit' { "esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=huge_app,CDCOnBoot=cdc" }
  # P4: a PSRAM e INTERNA ao encapsulamento (datasheet sec. 2.7, "not pinned out"),
  # ao contrario do S3 onde a PSRAM octal externa custava 12 GPIOs. MAS ser interna
  # NAO quer dizer que venha ligada: o padrao do core Arduino e PSRAM=disabled, e
  # sem PSRAM=enabled o ESP.getPsramSize() devolve 0. Descoberto gravando o
  # p4_hello, que compara o medido com o que a pesquisa dizia. Flash externa de
  # 32M (GD25Q256 nas placas Waveshare).
  #
  # CDCOnBoot=default (desligado) e proposital: manda o Serial para o UART0, que na
  # placa vai para a ponte CH343P e aparece como "USB-Enhanced-SERIAL CH343" (VID
  # 1A86). E por ali que a placa enumera quando voce liga na USB-C de gravacao.
  # Se trocar para CDCOnBoot=cdc, o log passa a sair pelo USB Serial/JTAG nativo
  # (GPIO24/25) e a COM da CH343 fica MUDA - o sintoma e "gravou e nao imprime nada".
  # PartitionScheme=default_8MB: DOIS slots de app de 3,3 MB (OTA pelo WiFi).
  # O huge_app antigo tinha um slot so e o binario usa ~1 MB - sobra 3x. A NVS
  # fica no MESMO deslocamento (0x9000) nos dois esquemas, entao a migracao NAO
  # apaga nome/slot/potencia/wifi. A primeira gravacao apos a troca e por cabo;
  # depois: -Upload -Port <IP da tela> (a tela mostra o IP na linha WIFI).
  'p4'     { "esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=default_8MB,USBMode=default,CDCOnBoot=default" }
}

if ($ListPorts) {
  # A Waveshare expoe duas USB-C: VID 303A = USB nativa (gravar), 1A86 = CH343 (usar em campo).
  & $CLI board list
  Write-Host "`nVID 303A = USB nativa (gravar aqui) | VID 1A86 = CH343 (campo, so energia)" -ForegroundColor Cyan
  return
}

$sketchPath = Resolve-Path $Sketch
Write-Host "sketch : $sketchPath"
Write-Host "board  : $Board"
Write-Host "fqbn   : $FQBN`n"

$out = & $CLI compile --fqbn $FQBN $sketchPath 2>&1
$out | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw "compilacao falhou" }

# Tamanho do binario: numero que precisamos acompanhar para decidir particionamento
# (huge_app tem UM slot de app; migrar para OTA exige dois).
$m = $out | Select-String -Pattern 'Sketch uses (\d+) bytes' | Select-Object -First 1
if ($m) {
  $bytes = [int]$m.Matches[0].Groups[1].Value
  $kb = [math]::Round($bytes / 1024, 1)
  Write-Host "`nbinario: $kb KB ($bytes bytes)" -ForegroundColor Green
}

if ($Upload) {
  if (-not $Port) { throw "-Upload exige -Port (use -ListPorts para achar a VID 303A)" }
  if ($Port -match '^\d+\.\d+\.\d+\.\d+$') {
    # OTA pelo WiFi: o Port e o IP da tela (aparece na linha WIFI da configuracao
    # e no serial). Senha fixa "mts" - so evita gravacao acidental.
    & $CLI upload -p $Port --fqbn $FQBN --upload-field password=mts $sketchPath
  } else {
    & $CLI upload -p $Port --fqbn $FQBN $sketchPath
  }
  if ($LASTEXITCODE -ne 0) { throw "upload falhou" }
  Write-Host "gravado em $Port" -ForegroundColor Green
}
