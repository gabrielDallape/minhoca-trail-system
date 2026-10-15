# Gera a placa do radio v4 (fabrica) do zero: monta, roteia, DRC e arquivos da JLCPCB.
#   .\hardware\placa_radio_smt\gera.ps1
# Tudo roda no Docker (KiCad 9 + Freerouting 1.9): o Smart App Control deste PC
# barra binario nao assinado, e o KiCad nao esta instalado aqui.
$ErrorActionPreference = "Stop"
$aqui = $PSScriptRoot
docker image inspect kicad-fr:9 *> $null
if ($LASTEXITCODE -ne 0) { docker build -t kicad-fr:9 (Join-Path $aqui "docker") }
docker run --rm -v "${aqui}:/w" kicad-fr:9 bash /w/gera.sh 2>&1 | Where-Object { $_ -notmatch "assert" }
Get-Content (Join-Path $aqui "saida\drc.txt") | Select-String "Found"
