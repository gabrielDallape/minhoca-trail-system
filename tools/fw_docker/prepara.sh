#!/bin/sh
# Prepara arduino-cli + core esp32 3.3.11 no volume persistente /ard (docker volume mts-ard).
# Roda UMA vez (baixa ~2 GB); o build.ps1 chama sozinho se o volume estiver vazio.
set -e
export ARDUINO_DIRECTORIES_DATA=/ard/data ARDUINO_DIRECTORIES_DOWNLOADS=/ard/dl ARDUINO_DIRECTORIES_USER=/ard/user
apt-get update -qq && apt-get install -y -qq curl ca-certificates python3 >/dev/null
if [ ! -x /ard/bin/arduino-cli ]; then
  mkdir -p /ard/bin && curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=/ard/bin sh
fi
/ard/bin/arduino-cli config init --overwrite --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json >/dev/null
/ard/bin/arduino-cli core update-index
/ard/bin/arduino-cli core install esp32:esp32@3.3.11
