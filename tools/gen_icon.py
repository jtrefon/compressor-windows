#!/usr/bin/env python3
"""Generate assets/CompressorWindows.ico (256x256, embedded PNG) without PIL.

Draws a rounded blue square with a white double-chevron (compression marks).
"""
import struct, zlib

SIZE = 256

def draw(size):
    # RGBA rows, top-down
    px = bytearray()
    R, G, B = 0x1B, 0x6F, 0xE0  # Windows accent blue
    for y in range(size):
        for x in range(size):
            # rounded corners (radius ~ 40)
            r = 40
            cr = 0
            if x < r and y < r:
                cr = 1 if (x - r) ** 2 + (y - r) ** 2 > r * r else 0
            elif x >= size - r and y < r:
                cr = 1 if (x - (size - r)) ** 2 + (y - r) ** 2 > r * r else 0
            elif x < r and y >= size - r:
                cr = 1 if (x - r) ** 2 + (y - (size - r)) ** 2 > r * r else 0
            elif x >= size - r and y >= size - r:
                cr = 1 if (x - (size - r)) ** 2 + (y - (size - r)) ** 2 > r * r else 0
            if cr:
                px += bytes((0, 0, 0, 0))
                continue
            # double chevron: two down-pointing triangles centered
            cx, cy = size // 2, size // 2
            h = int(size * 0.30)
            w = int(size * 0.52)
            t1y0 = cy - int(size * 0.10)
            t2y0 = cy + int(size * 0.10)
            white = False
            for t_y0 in (t1y0, t2y0):
                if t_y0 <= y < t_y0 + h:
                    half = int(w * (y - t_y0) / h)
                    if abs(x - cx) <= half:
                        white = True
                        break
            if white:
                px += bytes((255, 255, 255, 255))
            else:
                px += bytes((R, G, B, 255))
    return bytes(px)

def png_bytes(rgba, w, h):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))

def make_ico(path):
    png = png_bytes(draw(SIZE), SIZE, SIZE)
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, len(png), 22)
    with open(path, "wb") as f:
        f.write(header + entry + png)

if __name__ == "__main__":
    make_ico("app/assets/CompressorWindows.ico")
    print("wrote app/assets/CompressorWindows.ico")