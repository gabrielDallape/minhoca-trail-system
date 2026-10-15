#!/bin/sh
# Compila um sketch para a P4 no Linux. /w = repositorio (so leitura), /libs = as
# bibliotecas DESTE PC (Documents/Arduino/libraries: a mesma LovyanGFX 1.2.21 com o
# PATCH REDEA e a RadioLib 7.6.0 - o binario sai igual ao do Windows), /out = saida.
set -e
export ARDUINO_DIRECTORIES_DATA=/ard/data ARDUINO_DIRECTORIES_DOWNLOADS=/ard/dl ARDUINO_DIRECTORIES_USER=/ard/user
command -v python3 >/dev/null || { apt-get update -qq && apt-get install -y -qq python3 >/dev/null; }
mkdir -p /ard/user/libraries
rm -rf /ard/user/libraries/LovyanGFX /ard/user/libraries/RadioLib
cp -r /libs/LovyanGFX /libs/RadioLib /ard/user/libraries/
FQBN="esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=default_8MB,USBMode=default,CDCOnBoot=default"
/ard/bin/arduino-cli compile --fqbn "$FQBN" --output-dir /out --build-path /tmp/b "/w/$1" 2>&1 | grep -v "Read-only file system" | tail -20
