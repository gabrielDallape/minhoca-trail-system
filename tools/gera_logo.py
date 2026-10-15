# Converte a arte do MTS para o formato do painel: paleta de 16 cores em RGB565
# + indices de 4 bits empacotados. Escolha medida, nao chutada:
#   cru RGB565 .... 1519 KB   (fonte C gigante, compilacao lenta)
#   RLE ........... 894 KB    (so 1,7x - a arte tem textura e gradiente)
#   paleta 16 ..... 380 KB    <- e visualmente identico ao original
import sys
from PIL import Image

SRC = r"C:\Users\gadal\.claude\uploads\0131dfe1-400c-4992-a89a-466a01071eb4\2886d1fc-20D94FF2D3464881BDBF51D3954709DB.png"
DST = r"C:\Users\gadal\Desktop\gps\firmware\mts_p4\logo_mts.h"
W, H = 1080, 720          # altura cheia da tela deitada; sobra 100 px de cada lado

im = Image.open(SRC).convert("RGB").resize((W, H), Image.LANCZOS)
q = im.quantize(colors=16, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)

pal = q.getpalette()[:16 * 3]
pal565 = []
for i in range(16):
    r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
    pal565.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

idx = list(q.getdata())
packed = bytearray()
for y in range(H):
    row = idx[y * W:(y + 1) * W]
    for x in range(0, W, 2):
        packed.append((row[x] << 4) | row[x + 1])

# cor do fundo da arte, para as laterais casarem sem emenda visivel
bg = im.getpixel((4, 4))
bg565 = ((bg[0] >> 3) << 11) | ((bg[1] >> 2) << 5) | (bg[2] >> 3)

with open(DST, "w", encoding="utf-8") as f:
    f.write("// GERADO por tools/gera_logo.py - nao editar a mao.\n")
    f.write("// Arte do MTS em paleta de 16 cores (RGB565) + indices de 4 bits.\n")
    f.write("// %d x %d, %d bytes de pixel + 32 de paleta.\n" % (W, H, len(packed)))
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write("#define MTS_LOGO_W %d\n#define MTS_LOGO_H %d\n" % (W, H))
    f.write("#define MTS_LOGO_BG 0x%04X   // fundo da arte, para as laterais\n\n" % bg565)
    f.write("static const uint16_t mtsLogoPal[16] = {\n  ")
    f.write(", ".join("0x%04X" % c for c in pal565))
    f.write("\n};\n\n")
    f.write("static const uint8_t mtsLogoPix[%d] = {\n" % len(packed))
    for i in range(0, len(packed), 32):
        f.write("  " + ",".join("0x%02X" % b for b in packed[i:i + 32]) + ",\n")
    f.write("};\n")

print("gerado:", DST)
print("pixels: %d bytes (%.0f KB)" % (len(packed), len(packed) / 1024))
print("fundo : 0x%04X  rgb%s" % (bg565, bg))
