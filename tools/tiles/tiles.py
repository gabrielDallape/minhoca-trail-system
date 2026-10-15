#!/usr/bin/env python3
r"""
Pipeline de mapa offline: baixa tiles, converte para RGB565 e empacota numa
imagem RAW com indice proprio + CRC32 por tile.

Por que RAW e nao um filesystem: o aparelho vive num carro em trilha, onde a
energia cai sem aviso (chave do carro). Sem FAT nao ha tabela de alocacao nem
diretorio para corromper - o pior caso e um tile individual sair errado, e o
CRC32 deixa isso DETECTAVEL (desenha tile cinza em vez de lixo na tela). Ler
tambem fica mais rapido: endereco do tile e aritmetica, sem travessia de
diretorio nem cache de FAT.

Subcomandos:
  synth     gera tiles sinteticos (nao baixa nada) - valida o pipeline e da para
            testar no device sem mapa real
  download  baixa tiles de um template de URL, por bbox e zooms
  pack      converte PNG/JPG -> RGB565 e monta o .img
  info      mostra o cabecalho de um .img
  verify    confere o CRC32 de todos os tiles
  extract   tira um tile do .img de volta para PNG (conferencia visual)

ATENCAO na bbox: latitude/longitude do Brasil sao NEGATIVAS, e o argparse trata
um valor que comeca com '-' como se fosse outra opcao. Use SEMPRE com '=':
  --bbox=-23.60,-46.70,-23.50,-46.60      correto
  --bbox -23.60,-46.70,-23.50,-46.60      da erro "expected one argument"

Exemplos:
  python tiles.py synth  --out synth/ --zoom 15 --bbox=-23.60,-46.70,-23.50,-46.60
  python tiles.py pack   --in synth/ --out trilha.img
  python tiles.py info   --img trilha.img
  python tiles.py verify --img trilha.img
  python tiles.py extract --img trilha.img --zoom 15 --x 12055 --y 17557 --out t.png

  # gravar no cartao (Windows, PowerShell como admin - CUIDADO com o disco certo):
  #   Get-Disk                                  # descobrir o numero do disco
  #   dd if=trilha.img of=\\.\PhysicalDriveN    # com dd for Windows
"""

import argparse
import math
import os
import struct
import sys
import time
import zlib

# ----------------------------------------------------------------- formato
MAGIC = b"TRILHAMP"
VERSION = 1
SECTOR = 512          # setor logico do SD
TILE_PX = 256         # tile slippy-map padrao
PIXFMT_RGB565 = 0
MAX_LEVELS = 16

HDR_FMT = "<8sHHBBHIIIII"     # magic, ver, tilePx, pixFmt, nLevels, pad, idxSec, dataSec, tileSecs, nTiles, pad2
HDR_SIZE = struct.calcsize(HDR_FMT)
LVL_FMT = "<BBHIIHHI"         # zoom, pad, pad2, xmin, ymin, w, h, indexOffset
LVL_SIZE = struct.calcsize(LVL_FMT)   # 20
IDX_FMT = "<II"               # sector (0 = tile ausente), crc32
IDX_SIZE = struct.calcsize(IDX_FMT)

TILE_BYTES = TILE_PX * TILE_PX * 2
TILE_SECTORS = TILE_BYTES // SECTOR          # 256

assert HDR_SIZE + MAX_LEVELS * LVL_SIZE <= SECTOR, "cabecalho nao cabe no setor 0"


# --------------------------------------------------------- slippy map math
def deg2tile(lat, lon, z):
    """lat/lon -> (x, y) do tile no zoom z (padrao slippy map / Web Mercator)."""
    n = 2 ** z
    x = int((lon + 180.0) / 360.0 * n)
    lat = max(min(lat, 85.05112878), -85.05112878)   # limite do Mercator
    lat_rad = math.radians(lat)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    return max(0, min(x, n - 1)), max(0, min(y, n - 1))


def tile2deg(x, y, z):
    """Canto NO (noroeste) do tile, em graus."""
    n = 2 ** z
    lon = x / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y / n))))
    return lat, lon


def tile_span_m(lat, z):
    """Lado do tile em metros, na latitude dada."""
    return 156543.03392 * math.cos(math.radians(lat)) / (2 ** z) * TILE_PX


def bbox_tiles(bbox, z):
    """bbox = (lat_sul, lon_oeste, lat_norte, lon_leste) -> (xmin, ymin, w, h)."""
    s, w_, n_, e = bbox
    x0, y0 = deg2tile(n_, w_, z)      # canto NO
    x1, y1 = deg2tile(s, e, z)        # canto SE
    xmin, xmax = min(x0, x1), max(x0, x1)
    ymin, ymax = min(y0, y1), max(y0, y1)
    return xmin, ymin, xmax - xmin + 1, ymax - ymin + 1


def parse_bbox(s):
    p = [float(v) for v in s.split(",")]
    if len(p) != 4:
        raise argparse.ArgumentTypeError("bbox = lat_sul,lon_oeste,lat_norte,lon_leste")
    if p[0] > p[2]:
        p[0], p[2] = p[2], p[0]
    if p[1] > p[3]:
        p[1], p[3] = p[3], p[1]
    return tuple(p)


def parse_zooms(s):
    """'13-16' ou '13,14,16' ou '15'."""
    out = set()
    for part in s.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            out.update(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return sorted(out)


# ------------------------------------------------------------------- synth
def cmd_synth(args):
    """Tiles sinteticos: valida o pipeline inteiro sem baixar nada e sem
    depender de politica de uso de servidor de mapa."""
    from PIL import Image, ImageDraw

    zooms = parse_zooms(args.zoom)
    total = 0
    for z in zooms:
        xmin, ymin, w, h = bbox_tiles(args.bbox, z)
        print(f"z{z}: {w}x{h} = {w*h} tiles (x {xmin}..{xmin+w-1}, y {ymin}..{ymin+h-1})")
        for dy in range(h):
            for dx in range(w):
                x, y = xmin + dx, ymin + dy
                img = Image.new("RGB", (TILE_PX, TILE_PX), (18, 24, 20))
                d = ImageDraw.Draw(img)
                # grade + rotulo: da para ver na tela se o tile certo foi carregado
                for g in range(0, TILE_PX, 32):
                    d.line([(g, 0), (g, TILE_PX)], fill=(32, 44, 36))
                    d.line([(0, g), (TILE_PX, g)], fill=(32, 44, 36))
                d.rectangle([0, 0, TILE_PX - 1, TILE_PX - 1], outline=(120, 200, 140))
                lat, lon = tile2deg(x, y, z)
                d.text((10, 10), f"z{z}", fill=(230, 255, 230))
                d.text((10, 30), f"x{x}", fill=(180, 220, 190))
                d.text((10, 46), f"y{y}", fill=(180, 220, 190))
                d.text((10, 70), f"{lat:.4f}", fill=(150, 190, 160))
                d.text((10, 86), f"{lon:.4f}", fill=(150, 190, 160))
                # diagonal com cor por paridade: facilita ver tile trocado de lugar
                c = (200, 120, 60) if (x + y) % 2 else (60, 120, 200)
                d.line([(0, 0), (TILE_PX, TILE_PX)], fill=c, width=2)
                p = os.path.join(args.out, str(z), str(x))
                os.makedirs(p, exist_ok=True)
                img.save(os.path.join(p, f"{y}.png"))
                total += 1
    print(f"\n{total} tiles sinteticos em {args.out}")


# ---------------------------------------------------------------- download
def cmd_download(args):
    import requests

    if not args.url_template:
        print("ERRO: --url-template e obrigatorio.", file=sys.stderr)
        print("\nO servidor de tiles e uma escolha SUA, com implicacoes de licenca:", file=sys.stderr)
        print("  - tile.openstreetmap.org PROIBE bulk download (a politica deles", file=sys.stderr)
        print("    bloqueia; nao use para montar mapa offline).", file=sys.stderr)
        print("  - Thunderforest / MapTiler: use com a SUA chave e respeite os termos.", file=sys.stderr)
        print("  - Melhor caminho para distribuir: renderizar de um extract Geofabrik.", file=sys.stderr)
        print("\nExemplo: --url-template 'https://tile.thunderforest.com/outdoors/{z}/{x}/{y}.png?apikey=SUA_KEY'", file=sys.stderr)
        return 2

    zooms = parse_zooms(args.zoom)
    plan = []
    for z in zooms:
        xmin, ymin, w, h = bbox_tiles(args.bbox, z)
        plan.append((z, xmin, ymin, w, h))
        lat_mid = (args.bbox[0] + args.bbox[2]) / 2
        print(f"z{z}: {w}x{h} = {w*h} tiles  (~{tile_span_m(lat_mid, z):.0f} m por tile)")
    total = sum(w * h for _, _, _, w, h in plan)
    mb = total * TILE_BYTES / 1024 / 1024
    print(f"\ntotal: {total} tiles -> {mb:.1f} MB depois de convertidos para RGB565")
    if args.max_tiles and total > args.max_tiles:
        print(f"ERRO: {total} tiles passa do limite --max-tiles={args.max_tiles}.", file=sys.stderr)
        print("Reduza a bbox/zooms ou aumente o limite de proposito.", file=sys.stderr)
        return 2
    if not args.yes:
        if input("baixar? [s/N] ").strip().lower() not in ("s", "y"):
            return 1

    sess = requests.Session()
    sess.headers["User-Agent"] = args.user_agent
    got = skip = fail = 0
    for z, xmin, ymin, w, h in plan:
        for dy in range(h):
            for dx in range(w):
                x, y = xmin + dx, ymin + dy
                d = os.path.join(args.out, str(z), str(x))
                os.makedirs(d, exist_ok=True)
                dest = os.path.join(d, f"{y}.png")
                if os.path.exists(dest) and os.path.getsize(dest) > 0:
                    skip += 1
                    continue
                url = args.url_template.format(z=z, x=x, y=y)
                ok = False
                for attempt in range(args.retries):
                    try:
                        r = sess.get(url, timeout=30)
                        if r.status_code == 200 and r.content:
                            with open(dest, "wb") as f:
                                f.write(r.content)
                            ok = True
                            break
                        if r.status_code in (403, 429):
                            print(f"\n  {r.status_code} em z{z}/{x}/{y} - servidor recusando "
                                  f"(rate limit ou politica). Aumente --delay.", file=sys.stderr)
                            time.sleep(args.delay * 10)
                        else:
                            time.sleep(args.delay * 2)
                    except Exception as e:
                        print(f"\n  erro {e.__class__.__name__} em z{z}/{x}/{y}", file=sys.stderr)
                        time.sleep(args.delay * 2)
                if ok:
                    got += 1
                else:
                    fail += 1
                time.sleep(args.delay)      # rate limit: seja educado com o servidor
                done = got + skip + fail
                if done % 20 == 0:
                    print(f"\r  {done}/{total}  ok={got} cache={skip} falha={fail}", end="")
    print(f"\n\nbaixados={got} do_cache={skip} falharam={fail}")
    return 0 if fail == 0 else 1


# -------------------------------------------------------------------- pack
def to_rgb565(img):
    """PIL Image -> bytes RGB565 little-endian (o formato que o LovyanGFX/LVGL
    consomem direto, sem decodificar PNG no device)."""
    if img.mode != "RGB":
        img = img.convert("RGB")
    if img.size != (TILE_PX, TILE_PX):
        img = img.resize((TILE_PX, TILE_PX))
    out = bytearray(TILE_BYTES)
    px = img.load()
    i = 0
    for y in range(TILE_PX):
        for x in range(TILE_PX):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[i] = v & 0xFF
            out[i + 1] = (v >> 8) & 0xFF
            i += 2
    return bytes(out)


def scan_source(src):
    """Varre {zoom}/{x}/{y}.png e devolve {z: {(x,y): caminho}}."""
    levels = {}
    for zname in sorted(os.listdir(src), key=lambda s: (not s.isdigit(), s)):
        zpath = os.path.join(src, zname)
        if not (zname.isdigit() and os.path.isdir(zpath)):
            continue
        z = int(zname)
        tiles = {}
        for xname in os.listdir(zpath):
            xpath = os.path.join(zpath, xname)
            if not (xname.isdigit() and os.path.isdir(xpath)):
                continue
            x = int(xname)
            for fname in os.listdir(xpath):
                base, ext = os.path.splitext(fname)
                if ext.lower() not in (".png", ".jpg", ".jpeg", ".webp") or not base.isdigit():
                    continue
                tiles[(x, int(base))] = os.path.join(xpath, fname)
        if tiles:
            levels[z] = tiles
    return levels


def cmd_pack(args):
    from PIL import Image

    levels = scan_source(args.inp)
    if not levels:
        print(f"ERRO: nenhum tile em {args.inp} (esperado {{zoom}}/{{x}}/{{y}}.png)", file=sys.stderr)
        return 2
    if len(levels) > MAX_LEVELS:
        print(f"ERRO: {len(levels)} niveis passa do maximo {MAX_LEVELS}", file=sys.stderr)
        return 2

    # geometria de cada nivel: o retangulo que cobre os tiles presentes
    meta, idx_count = [], 0
    for z in sorted(levels):
        xs = [x for x, _ in levels[z]]
        ys = [y for _, y in levels[z]]
        xmin, ymin = min(xs), min(ys)
        w, h = max(xs) - xmin + 1, max(ys) - ymin + 1
        if w > 0xFFFF or h > 0xFFFF:
            print(f"ERRO: z{z} tem {w}x{h} tiles, nao cabe em uint16", file=sys.stderr)
            return 2
        meta.append(dict(zoom=z, xmin=xmin, ymin=ymin, w=w, h=h, off=idx_count))
        idx_count += w * h
        print(f"z{z}: grade {w}x{h} ({w*h} posicoes, {len(levels[z])} tiles presentes)")

    idx_bytes = idx_count * IDX_SIZE
    idx_sectors = (idx_bytes + SECTOR - 1) // SECTOR
    index_sector = 1
    data_sector = index_sector + idx_sectors

    index = [(0, 0)] * idx_count       # (setor, crc32); setor 0 = ausente
    next_sector = data_sector
    written = 0

    with open(args.out, "wb") as f:
        f.truncate(data_sector * SECTOR)      # reserva header + indice
        for m in meta:
            z = m["zoom"]
            for (x, y), path in sorted(levels[z].items()):
                try:
                    with Image.open(path) as im:
                        raw = to_rgb565(im)
                except Exception as e:
                    print(f"  aviso: {path} ilegivel ({e.__class__.__name__}), tratado como ausente")
                    continue
                slot = m["off"] + (y - m["ymin"]) * m["w"] + (x - m["xmin"])
                f.seek(next_sector * SECTOR)
                f.write(raw)
                index[slot] = (next_sector, zlib.crc32(raw) & 0xFFFFFFFF)
                next_sector += TILE_SECTORS
                written += 1
                if written % 50 == 0:
                    print(f"\r  {written} tiles convertidos", end="")

        # indice
        f.seek(index_sector * SECTOR)
        for sec, crc in index:
            f.write(struct.pack(IDX_FMT, sec, crc))

        # header no setor 0
        f.seek(0)
        f.write(struct.pack(HDR_FMT, MAGIC, VERSION, TILE_PX, PIXFMT_RGB565,
                            len(meta), 0, index_sector, data_sector,
                            TILE_SECTORS, written, 0))
        for m in meta:
            f.write(struct.pack(LVL_FMT, m["zoom"], 0, 0, m["xmin"], m["ymin"],
                                m["w"], m["h"], m["off"]))
        # zera o resto do setor 0 (deixa deterministico)
        pad = SECTOR - (HDR_SIZE + len(meta) * LVL_SIZE)
        f.write(b"\x00" * pad)

    size_mb = os.path.getsize(args.out) / 1024 / 1024
    print(f"\n\n{args.out}: {written} tiles, {size_mb:.1f} MB")
    print(f"  setor do indice: {index_sector} ({idx_sectors} setores)")
    print(f"  setor dos dados: {data_sector}")
    print(f"  setores por tile: {TILE_SECTORS} ({TILE_BYTES} bytes)")
    return 0


# ------------------------------------------------------------- info/verify
def read_header(f):
    f.seek(0)
    hdr = struct.unpack(HDR_FMT, f.read(HDR_SIZE))
    (magic, ver, tilepx, pixfmt, nlev, _pad, idx_sec, data_sec, tile_secs, ntiles, _p2) = hdr
    if magic != MAGIC:
        raise ValueError(f"magic invalido: {magic!r} (esperado {MAGIC!r})")
    levels = []
    for _ in range(nlev):
        z, _p, _p2b, xmin, ymin, w, h, off = struct.unpack(LVL_FMT, f.read(LVL_SIZE))
        levels.append(dict(zoom=z, xmin=xmin, ymin=ymin, w=w, h=h, off=off))
    return dict(version=ver, tile_px=tilepx, pixfmt=pixfmt, index_sector=idx_sec,
                data_sector=data_sec, tile_sectors=tile_secs, n_tiles=ntiles,
                levels=levels)


def read_index(f, hdr, slot):
    f.seek(hdr["index_sector"] * SECTOR + slot * IDX_SIZE)
    return struct.unpack(IDX_FMT, f.read(IDX_SIZE))


def find_slot(hdr, z, x, y):
    for m in hdr["levels"]:
        if m["zoom"] != z:
            continue
        dx, dy = x - m["xmin"], y - m["ymin"]
        if 0 <= dx < m["w"] and 0 <= dy < m["h"]:
            return m["off"] + dy * m["w"] + dx
        return None
    return None


def cmd_info(args):
    with open(args.img, "rb") as f:
        hdr = read_header(f)
        print(f"versao          : {hdr['version']}")
        print(f"tile            : {hdr['tile_px']}x{hdr['tile_px']} "
              f"{'RGB565' if hdr['pixfmt'] == PIXFMT_RGB565 else '?'} "
              f"({hdr['tile_sectors']} setores)")
        print(f"setor do indice : {hdr['index_sector']}")
        print(f"setor dos dados : {hdr['data_sector']}")
        print(f"tiles gravados  : {hdr['n_tiles']}")
        print(f"tamanho         : {os.path.getsize(args.img)/1024/1024:.1f} MB")
        print("\nniveis:")
        for m in hdr["levels"]:
            lat_n, lon_w = tile2deg(m["xmin"], m["ymin"], m["zoom"])
            lat_s, lon_e = tile2deg(m["xmin"] + m["w"], m["ymin"] + m["h"], m["zoom"])
            present = sum(1 for s in range(m["off"], m["off"] + m["w"] * m["h"])
                          if read_index(f, hdr, s)[0] != 0)
            print(f"  z{m['zoom']}: grade {m['w']}x{m['h']} em x{m['xmin']} y{m['ymin']}"
                  f" | {present}/{m['w']*m['h']} presentes")
            print(f"       bbox lat {lat_s:.5f}..{lat_n:.5f}  lon {lon_w:.5f}..{lon_e:.5f}"
                  f"  ({tile_span_m((lat_n+lat_s)/2, m['zoom']):.0f} m/tile)")
    return 0


def cmd_verify(args):
    bad = missing = ok = 0
    with open(args.img, "rb") as f:
        hdr = read_header(f)
        for m in hdr["levels"]:
            for s in range(m["off"], m["off"] + m["w"] * m["h"]):
                sec, crc = read_index(f, hdr, s)
                if sec == 0:
                    missing += 1
                    continue
                f.seek(sec * SECTOR)
                raw = f.read(TILE_BYTES)
                if len(raw) != TILE_BYTES:
                    print(f"  tile truncado no setor {sec}")
                    bad += 1
                elif (zlib.crc32(raw) & 0xFFFFFFFF) != crc:
                    print(f"  CRC ERRADO no setor {sec}")
                    bad += 1
                else:
                    ok += 1
    print(f"ok={ok} ausentes={missing} corrompidos={bad}")
    return 0 if bad == 0 else 1


def cmd_extract(args):
    from PIL import Image

    with open(args.img, "rb") as f:
        hdr = read_header(f)
        slot = find_slot(hdr, args.zoom, args.x, args.y)
        if slot is None:
            zooms = [m["zoom"] for m in hdr["levels"]]
            if args.zoom not in zooms:
                print(f"ERRO: este .img nao tem o zoom {args.zoom} (tem: {zooms})", file=sys.stderr)
            else:
                m = next(m for m in hdr["levels"] if m["zoom"] == args.zoom)
                print(f"ERRO: x{args.x} y{args.y} fora da grade de z{args.zoom} "
                      f"(x {m['xmin']}..{m['xmin']+m['w']-1}, "
                      f"y {m['ymin']}..{m['ymin']+m['h']-1})", file=sys.stderr)
            return 2
        sec, crc = read_index(f, hdr, slot)
        if sec == 0:
            print("ERRO: tile ausente (nao foi gravado)", file=sys.stderr)
            return 2
        f.seek(sec * SECTOR)
        raw = f.read(TILE_BYTES)
    calc = zlib.crc32(raw) & 0xFFFFFFFF
    print(f"setor={sec} crc_indice={crc:08x} crc_calculado={calc:08x} "
          f"{'OK' if calc == crc else 'DIVERGENTE'}")
    img = Image.new("RGB", (TILE_PX, TILE_PX))
    px = img.load()
    for i in range(TILE_PX * TILE_PX):
        v = raw[i * 2] | (raw[i * 2 + 1] << 8)
        r = ((v >> 11) & 0x1F) << 3
        g = ((v >> 5) & 0x3F) << 2
        b = (v & 0x1F) << 3
        px[i % TILE_PX, i // TILE_PX] = (r, g, b)
    img.save(args.out)
    print(f"gravado {args.out}")
    return 0


# -------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("synth", help="gera tiles sinteticos (nao baixa nada)")
    s.add_argument("--out", required=True)
    s.add_argument("--zoom", required=True, help="ex: 15 ou 13-16")
    s.add_argument("--bbox", required=True, type=parse_bbox,
                   help="lat_sul,lon_oeste,lat_norte,lon_leste")
    s.set_defaults(func=cmd_synth)

    s = sub.add_parser("download", help="baixa tiles de um template de URL")
    s.add_argument("--out", required=True)
    s.add_argument("--zoom", required=True)
    s.add_argument("--bbox", required=True, type=parse_bbox)
    s.add_argument("--url-template", help="ex: https://.../{z}/{x}/{y}.png?apikey=KEY")
    s.add_argument("--delay", type=float, default=0.25, help="segundos entre requests")
    s.add_argument("--retries", type=int, default=3)
    s.add_argument("--max-tiles", type=int, default=5000, help="trava de seguranca")
    s.add_argument("--user-agent", default="outdoor-trail-follow-me/1.0 (offline map prep)")
    s.add_argument("--yes", action="store_true")
    s.set_defaults(func=cmd_download)

    s = sub.add_parser("pack", help="converte para RGB565 e monta o .img")
    s.add_argument("--in", dest="inp", required=True, help="pasta {zoom}/{x}/{y}.png")
    s.add_argument("--out", required=True)
    s.set_defaults(func=cmd_pack)

    s = sub.add_parser("info", help="mostra o cabecalho")
    s.add_argument("--img", required=True)
    s.set_defaults(func=cmd_info)

    s = sub.add_parser("verify", help="confere o CRC32 de todos os tiles")
    s.add_argument("--img", required=True)
    s.set_defaults(func=cmd_verify)

    s = sub.add_parser("extract", help="tira um tile de volta para PNG")
    s.add_argument("--img", required=True)
    s.add_argument("--zoom", type=int, required=True)
    s.add_argument("--x", type=int, required=True)
    s.add_argument("--y", type=int, required=True)
    s.add_argument("--out", required=True)
    s.set_defaults(func=cmd_extract)

    args = ap.parse_args()
    sys.exit(args.func(args) or 0)


if __name__ == "__main__":
    main()
