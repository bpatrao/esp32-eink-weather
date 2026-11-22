"""Convert FRAME dumps from the weather_preview build into PNGs.

Usage: python3 frames_to_png.py serial.log out_dir
Writes view_<n>.png per frame (2x scale) and sheet.png with all of them.
Pure Python (zlib only), no Pillow needed.
"""
import os, re, struct, sys, zlib

W, H, STRIDE, SCALE = 250, 122, 16, 2

def frames(text):
    for m in re.finditer(r"FRAME (\d+)\s+([0-9A-F\s]+?)\s*END", text):
        data = bytes.fromhex("".join(m.group(2).split()))
        if len(data) == STRIDE * 250:
            yield int(m.group(1)), data

def to_pixels(buf):
    # Rotation 1: logical (x, y) -> native (121 - y, x); bit set = white
    px = []
    for y in range(H):
        row = []
        for x in range(W):
            nx, ny = 121 - y, x
            white = buf[ny * STRIDE + nx // 8] & (0x80 >> (nx % 8))
            row.append(255 if white else 0)
        px.append(row)
    return px

def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def scale(rows, s):
    return [[v for v in r for _ in range(s)] for r in rows for _ in range(s)]

def main(log, out):
    os.makedirs(out, exist_ok=True)
    got = dict(frames(open(log, encoding="latin-1").read()))
    imgs = []
    for n in sorted(got):
        rows = scale(to_pixels(got[n]), SCALE)
        write_png(os.path.join(out, f"view_{n}.png"), rows)
        imgs.append(rows)
    if imgs:  # 2-column sheet with grey gutters
        cols, gw = 2, 8
        iw, ih = len(imgs[0][0]), len(imgs[0])
        nrows = (len(imgs) + cols - 1) // cols
        sheet = [[128] * (cols * iw + (cols + 1) * gw) for _ in range(nrows * ih + (nrows + 1) * gw)]
        for i, im in enumerate(imgs):
            ox, oy = gw + (i % cols) * (iw + gw), gw + (i // cols) * (ih + gw)
            for y, r in enumerate(im):
                sheet[oy + y][ox:ox + iw] = r
        write_png(os.path.join(out, "sheet.png"), sheet)
    print(f"{len(imgs)} frames -> {out}")

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
