# Mapa offline — preparação dos tiles

Pipeline que roda **no PC** para transformar tiles de mapa num container que o
aparelho lê direto do cartão, por setor, sem filesystem.

```
tiles do servidor  --download-->  {z}/{x}/{y}.png  --pack-->  trilha.img  --dd-->  microSD
                                                                    |
                                              firmware/tile_pack.h <-+ (lê por setor)
```

## Por que não usar arquivos num cartão FAT

O aparelho fica num carro em trilha e **perde energia sem aviso** (chave do carro).
Três razões para abandonar o filesystem:

1. **Nada de metadados para corromper.** Sem FAT não há tabela de alocação nem
   diretório. O pior caso passa a ser um tile individual errado, em vez de um
   cartão que não monta.
2. **CRC32 por tile torna a degradação detectável.** O container guarda o CRC de
   cada tile no índice; o firmware devolve `TILEPACK_BADCRC` e pinta o tile de
   cinza em vez de desenhar lixo colorido na tela. O contador `crcErrors` subindo
   com o tempo é o sinal de que o cartão está morrendo — troque antes de falhar
   na trilha.
3. **Mais rápido no caminho do frame.** O endereço do tile é aritmética
   (`slot = indexOffset + (y-ymin)*w + (x-xmin)`), sem travessia de diretório nem
   cache de FAT.

Vale saber: **montar o filesystem como read-only não protege o cartão.** Alguns
cartões fazem wear leveling em background *disparado por leitura*, e perder
energia nesse instante corrompe dados mesmo sem nenhuma escrita do host. Por isso
a escolha do cartão importa (ver `docs/` e o plano) — prefira **microSD industrial
pSLC, -40/+85 °C, com power-fail protection declarada**. Cartão "High Endurance"
de dashcam vende endurance de *escrita*, que aqui não se usa.

## Uso

```powershell
# 1) tiles sinteticos - valida o pipeline sem baixar nada nem depender de licenca
python tiles.py synth --out synth\ --zoom 15-16 --bbox=-23.56,-46.66,-23.54,-46.64

# 2) empacota em RGB565 + indice + CRC32
python tiles.py pack --in synth\ --out trilha.img

# 3) confere
python tiles.py info   --img trilha.img
python tiles.py verify --img trilha.img

# 4) tira um tile de volta para PNG (conferencia visual)
python tiles.py extract --img trilha.img --zoom 15 --x 12137 --y 18590 --out t.png

# 5) conformidade entre este gravador (Python) e o leitor (C, tile_pack.h)
python verify_reader.py --img trilha.img
```

> **Latitude/longitude no Brasil são negativas** e o argparse trata `-23.5` como
> se fosse uma opção. Use sempre `--bbox=-23.56,...` **com o sinal de igual**.

Para mapa real, o download exige um `--url-template` seu:

```powershell
python tiles.py download --out real\ --zoom 13-16 `
  --bbox=-23.60,-46.70,-23.50,-46.60 `
  --url-template "https://tile.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey=SUA_KEY"
```

### Licença dos tiles — leia antes

- **`tile.openstreetmap.org` proíbe bulk download.** A política deles bloqueia, e
  não é para ser usado como fonte de mapa offline.
- **Thunderforest / MapTiler**: use com a **sua** chave e dentro dos termos.
- Para **distribuir** aparelhos com mapa pré-carregado, o caminho limpo é
  renderizar de um extract da **Geofabrik** (dados OSM, ODbL) ou usar um provedor
  cuja licença permita redistribuição.

O `download` tem trava de segurança (`--max-tiles`, padrão 5000), `--delay` entre
requests e cache local (não rebaixa o que já existe). Se o servidor responder 403
ou 429, aumente o `--delay`.

## Dimensionamento

| Zoom | Metros por tile (lat. −23,5°) | Uso |
|---|---|---|
| z13 | ~4 500 m | visão geral da região |
| z14 | ~2 240 m | aproximação |
| z15 | ~1 120 m | trilha |
| z16 | ~560 m | detalhe, até ruela |

Tile RGB565 de 256×256 = **131 072 bytes fixos** (não comprime). Uma área de
20×20 km em z13–z16 dá cerca de **200 MB** — num cartão de 32 GB cabem dezenas de
trilhas. O `download` imprime a estimativa em MB antes de começar.

## Formato do container

```
setor 0            cabecalho (36 B) + tabela de niveis (20 B cada, ate 16)
setor indexSector  indice: por slot {setor u32, crc32 u32}   (setor 0 = tile ausente)
setor dataSector.. tiles, cada um ocupando tileSectors setores (256 para 256x256)
```

Cabeçalho, little-endian:

| Offset | Campo | Tipo |
|--:|---|---|
| 0 | magic `"TRILHAMP"` | 8 bytes |
| 8 | version (=1) | u16 |
| 10 | tilePx (=256) | u16 |
| 12 | pixFmt (0 = RGB565) | u8 |
| 13 | nLevels | u8 |
| 16 | indexSector | u32 |
| 20 | dataSector | u32 |
| 24 | tileSectors | u32 |
| 28 | nTiles | u32 |
| 36 + i·20 | nível i: zoom u8, xmin u32, ymin u32, w u16, h u16, indexOffset u32 | 20 bytes |

**Se mudar o formato, mude nos dois lados** — `tools/tiles/tiles.py` e
`firmware/tile_pack.h`. O `verify_reader.py` existe justamente para pegar
divergência entre eles (reimplementa o CRC32 e o Mercator do lado C e compara com
o Python); rode-o depois de qualquer mudança.

## Gravar no cartão

O `.img` é uma imagem **de disco inteiro**, não um arquivo para copiar. No Windows,
com [dd for Windows](http://www.chrysocome.net/dd) ou Rufus/BalenaEtcher:

```powershell
Get-Disk                                    # descubra o numero do disco do cartao
dd if=trilha.img of=\\.\PhysicalDriveN bs=1M
```

**Confirme o número do disco.** Escrever no disco errado destrói o conteúdo dele.

Dica: mantenha **dois cartões idênticos** com a mesma imagem, um de reserva. É a
mitigação mais barata que existe para falha de cartão em campo.

## Testes

Nenhum dos dois exige hardware:

- `verify_reader.py` — conformidade entre gravador e leitor: CRC32 (inclusive num
  payload de 128 KB), fórmula de Mercator em 8 pontos × 5 zooms, tamanhos de
  struct, parsing do cabeçalho e cálculo de slot.
- `firmware/tile_selftest/` — auto-teste do `tile_pack.h` que monta um container
  na RAM (tiles de 16 px) e exercita o parser, incluindo os caminhos de erro
  (magic inválido, versão, tile ausente, CRC ruim, falha de I/O). Roda em qualquer
  ESP32, sem cartão.
