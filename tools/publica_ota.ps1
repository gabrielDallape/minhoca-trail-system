# Publica uma versao nova do mts_p4 para a FROTA (servidor mts-ota na Vercel).
#
# Uso:
#   1. suba o MTS_VERSAO em firmware\mts_p4\hardware.h (fonte unica da versao)
#   2. .\tools\publica_ota.ps1
#
# O que acontece do outro lado: toda tela ligada, conectada em QUALQUER WiFi e
# parada na tela inicial checa o manifesto a cada 15 min; ao ver versao maior,
# baixa, confere o MD5, grava no segundo slot e reinicia sozinha. Na trilha a
# checagem nao roda (de proposito - ver otaNuvemChecar em wifi_ota.h).
$ErrorActionPreference = 'Stop'

$repo = Split-Path $PSScriptRoot -Parent
$hw   = Get-Content "$repo\firmware\mts_p4\hardware.h" -Raw
if ($hw -notmatch '#define\s+MTS_VERSAO\s+(\d+)') { throw "MTS_VERSAO nao encontrado no hardware.h" }
$v = [int]$Matches[1]
Write-Host "publicando firmware v$v" -ForegroundColor Cyan

$CLI  = "C:\Users\gadal\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
# MESMO FQBN do build.ps1 (p4). Se mudar la, mude aqui.
$FQBN = "esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=default_8MB,USBMode=default,CDCOnBoot=default"

# compila para fora da pasta do servidor (o que esta em ota_server sobe TODO no deploy)
$saida = Join-Path $env:TEMP "mts_ota_build"
& $CLI compile --fqbn $FQBN --output-dir $saida "$repo\firmware\mts_p4" | Select-String "Sketch uses"
if ($LASTEXITCODE -ne 0) { throw "compilacao falhou" }

$bin = Join-Path $saida "mts_p4.ino.bin"
$md5 = (Get-FileHash $bin -Algorithm MD5).Hash.ToLower()
$tam = (Get-Item $bin).Length

$srv = "$repo\tools\ota_server"
Get-ChildItem "$srv\mts_p4_v*.bin" -ErrorAction SilentlyContinue | Remove-Item   # so a corrente fica no ar
Copy-Item $bin "$srv\mts_p4_v$v.bin"

# nome do binario COM a versao: o CDN da Vercel pode guardar cache por caminho,
# e um caminho novo por versao nunca serve binario velho com manifesto novo
@"
{
  "versao": $v,
  "arq": "/mts_p4_v$v.bin",
  "md5": "$md5",
  "bytes": $tam,
  "publicado": "$(Get-Date -Format s)"
}
"@ | Set-Content "$srv\version.json" -Encoding ascii

Push-Location $srv
try { vercel deploy --prod --yes | Select-Object -Last 2 } finally { Pop-Location }

Write-Host "`nfirmware v$v no ar: https://mts-ota.vercel.app/version.json" -ForegroundColor Green
Write-Host "telas na tela inicial + WiFi atualizam sozinhas em ate 15 min (boot: 40 s)"
