# COMPILA FIRMWARE DA P4 NO DOCKER - porque o Smart App Control deste Windows
# passou a bloquear o linker (riscv32-esp-elf-ld.exe: "uma politica de Controle de
# Aplicativo bloqueou este arquivo", 2026-10-08; o mesmo arquivo linkava em 10-01).
# Nao desligue o SAC: so volta reinstalando o Windows.
#
#   .\tools\fw_docker\build.ps1                  -> firmware\mts, binarios em tools\fw_docker\out
#   .\tools\fw_docker\build.ps1 firmware\mts -Porta COM9   -> compila e grava SO o app
#
# Gravar: so o app em 0x10000 e a otadata limpa (a tela parte do app0, como no upload
# do Arduino). NAO grava o merged.bin: ele tem 32 MB e apagaria a NVS (nome, slot, pwr).
param([string]$Sketch = "firmware\mts", [string]$Porta = "")
$ErrorActionPreference = 'Stop'
$raiz = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$aqui = $PSScriptRoot
$out = Join-Path $aqui "out"
New-Item -ItemType Directory -Force $out | Out-Null
$libs = Join-Path $env:USERPROFILE "Documents\Arduino\libraries"
docker volume create mts-ard | Out-Null
docker run --rm -v mts-ard:/ard debian:bookworm-slim test -x /ard/bin/arduino-cli
if ($LASTEXITCODE -ne 0) {
  Write-Host "primeira vez: baixando arduino-cli e o core esp32 3.3.11 (~2 GB)..."
  docker run --rm -v mts-ard:/ard -v "${aqui}:/s" debian:bookworm-slim sh /s/prepara.sh
  if ($LASTEXITCODE -ne 0) { throw "preparo do container falhou" }
}
$sk = $Sketch.Replace([char]92, [char]47)   # barra invertida -> barra (caminho dentro do Linux)
docker run --rm -v mts-ard:/ard -v "${raiz}:/w:ro" -v "${libs}:/libs:ro" -v "${aqui}:/s" -v "${out}:/out" debian:bookworm-slim sh /s/compila.sh $sk
if ($LASTEXITCODE -ne 0) { throw "compilacao falhou" }
$bin = Get-ChildItem $out -Filter *.ino.bin | Where-Object { $_.Name -notmatch 'bootloader|partitions|merged' } | Select-Object -First 1
Write-Host "binario: $($bin.FullName) ($($bin.Length) B)"
if ($Porta) {
  python -I -m esptool --chip esp32p4 -p $Porta -b 921600 --after no-reset erase-region 0xe000 0x2000
  if ($LASTEXITCODE -ne 0) { throw "falhou ao limpar a otadata" }
  python -I -m esptool --chip esp32p4 -p $Porta -b 921600 write-flash 0x10000 $bin.FullName
  if ($LASTEXITCODE -ne 0) { throw "gravacao falhou" }
}
