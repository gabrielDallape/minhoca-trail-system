# SIMULADOR: o firmware/mts de verdade, 2-3 telas conversando, no PC.
#
#   .\tools\sim\build.ps1                    -> compila e roda TODOS os cenarios
#   .\tools\sim\build.ps1 grupo_lista        -> so um cenario
#
# Roda no Docker (imagem gcc:14) porque o Smart App Control deste Windows bloqueia
# qualquer compilador nao assinado: o MinGW saia com 0xC0E90002 sem mensagem. O
# Docker Desktop precisa estar aberto. Saida: tools\sim\out\<cenario>\<tela>\
# (console.txt + log\*.csv no formato do cartao). Retorna 0 se tudo passou.
param([string]$Cenario = "todos")
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
docker info --format '{{.ServerVersion}}' 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
  $d = "C:\Program Files\Docker\Docker\Docker Desktop.exe"
  if (-not (Test-Path $d)) { throw "Docker Desktop nao encontrado" }
  Write-Host "abrindo o Docker Desktop..."
  Start-Process $d
  $fim = (Get-Date).AddSeconds(240)
  do { Start-Sleep -Seconds 5; docker info 2>$null | Out-Null } while ($LASTEXITCODE -ne 0 -and (Get-Date) -lt $fim)
  if ($LASTEXITCODE -ne 0) { throw "o Docker nao ficou pronto" }
}
docker run --rm -v "${raiz}:/w" -w /w gcc:14 sh tools/sim/build.sh $Cenario
exit $LASTEXITCODE
