# Changelog

Histórico das fases do projeto. As datas são aproximadas (marcos, não releases formais).

## Fase 3 — Duas telas Waveshare (atual)

Arquitetura final: duas telas **Waveshare ESP32-S3-Touch-LCD-7B** idênticas rodando o mesmo firmware (`firmware/grupo_ws/`), papel escolhido por toque (CRIAR SALA = líder, ENTRAR = seguidor).

- **Protocolo P2P** ponto-a-ponto (0x11 seguidor→líder com ACK, 0x12 líder→seguidor com bloco de trajeto).
- **Catch-up**: ao reconectar, o líder reenvia o trecho perdido em blocos até o seguidor alcançar — preenche o vão sem linha reta.
- **Bordas de aviso piscando**: vermelho (alerta), laranja (perda de sinal, com "REDUZA"/"SINAL PERDIDO"), amarelo ("FORA DO TRAJETO").
- **Alerta** pinta o trecho do caminho entre os dois carros de vermelho.
- **Trajeto real** (breadcrumb) em vez de linha reta; cores fixas roxo (a percorrer) / azul (percorrido).
- Buffer de trajeto ampliado para **~6 km** (PSRAM); busca do ponto mais próximo otimizada.
- Bring-up completo da Waveshare: GT911 (touch), CH32V003 (expansor IO), LoRaMESH, GPS.
- Reorganização do repositório em `firmware/`, `tools/`, `legacy/`, `docs/`.

## Fase 2 — Seguidor por LoRa (líder → seguidor)

- Líder (CYD) transmite posição + histórico; seguidor (GIGA) plota mapa estilo Waze.
- Núcleo compartilhado `trilha_core.h` (protocolo, temas, estado, geo).
- Modo Grupo no GIGA com temas (Rally/Tático/HUD), roster, distância por carro.
- Tentativa no Arduino GIGA descontinuada (módulo LoRa do GIGA falhou → migração para Waveshare).

## Fase 1 — Odômetro

- Projeto original de odômetro (`legacy/odometro/`), origem do sistema.
