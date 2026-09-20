#!/usr/bin/env python3
"""Write src/easyamp.ico in the only form Windows 98 and XP can read.

Pillow's .ico writer stores PNG-compressed images, which Windows only learned
to read in Vista: on 98 and XP the program, its taskbar button and every
shortcut fall back to the generic icon. This writes classic BMP-format entries
by hand, each size twice: 8-bit with a transparency mask (Windows 98 has no
alpha-blended icons) and 32-bit BGRA (XP uses the alpha channel).
"""
import os
import struct
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "assets", "icons", "easyamp-256.png")
OUT = os.path.join(HERE, "..", "src", "easyamp.ico")


def and_mask(alpha, size):
    row = ((size + 31) // 32) * 4
    out = bytearray()
    for y in range(size - 1, -1, -1):                      # bottom-up
        bits = bytearray(row)
        for x in range(size):
            if alpha.getpixel((x, y)) < 128:
                bits[x // 8] |= 0x80 >> (x % 8)
        out += bits
    return bytes(out)


def header(size, bpp, colors=0):
    return struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, bpp, 0, 0, 0, 0, colors, 0)


def entry_32(im, size):
    rgba = im.resize((size, size), Image.LANCZOS)
    px = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            r, g, b, a = rgba.getpixel((x, y))
            px += bytes((b, g, r, a))
    return header(size, 32) + bytes(px) + and_mask(rgba.getchannel("A"), size)


def entry_8(im, size):
    rgba = im.resize((size, size), Image.LANCZOS)
    alpha = rgba.getchannel("A")
    flat = Image.new("RGB", (size, size), (0, 0, 0))
    flat.paste(rgba, mask=alpha)
    pal = flat.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    table = pal.getpalette()[:768] + [0] * (768 - len(pal.getpalette()[:768]))
    colors = b"".join(bytes((table[i * 3 + 2], table[i * 3 + 1], table[i * 3], 0)) for i in range(256))
    row = ((size + 3) // 4) * 4
    px = bytearray()
    for y in range(size - 1, -1, -1):
        line = bytearray(row)
        for x in range(size):
            line[x] = 0 if alpha.getpixel((x, y)) < 128 else pal.getpixel((x, y))
        px += line
    return header(size, 8, 256) + colors + bytes(px) + and_mask(alpha, size)


def main():
    im = Image.open(SRC).convert("RGBA")
    images = []
    for size in (16, 32, 48):
        images.append((size, 8, entry_8(im, size)))
        images.append((size, 32, entry_32(im, size)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, bpp, data in images:
        out += struct.pack("<BBBBHHII", size, size, 0, 0, 1, bpp, len(data), offset)
        offset += len(data)
    for _, _, data in images:
        out += data
    open(OUT, "wb").write(out)
    print(f"{os.path.relpath(OUT)}: {len(images)} images, {len(out)} bytes, BMP format")


if __name__ == "__main__":
    main()
