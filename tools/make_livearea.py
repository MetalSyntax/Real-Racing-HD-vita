#!/usr/bin/env python3
"""LiveArea from the game's own art into extras/livearea_game/ (gitignored: Firemint artwork never goes
in git; CMakeLists.txt uses it when present, else the extras/livearea/ placeholders).

  icon0.png   128x128  <- iTunesArtwork (512x512 JPEG next to Payload/)
  bg0.png     840x500  <- Default-Landscape.png (CgBI 1024x768 launch image)
  pic0.png    960x544  <- Default-Landscape.png
  startup.png 280x158  <- Default-Landscape.png
All saved as 8-bit indexed PNG (LiveArea requirement), validated by `psvita-toolkit livearea --validate`.

Uso: python3 -I tools/make_livearea.py
"""
import os, struct, sys, zlib
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'extras', 'livearea_game')


def cgbi_to_image(path):
    """Apple CgBI PNG: raw deflate, BGRA, premultiplied alpha."""
    d = open(path, 'rb').read()
    pos, idat, w, h = 8, b'', 0, 0
    while pos < len(d):
        n, t = struct.unpack('>I4s', d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b'IHDR':
            w, h = struct.unpack('>II', body[:8])
        elif t == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, out, prev = zlib.decompress(idat, -15), w * 4, bytearray(), bytearray(w * 4)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    for i in range(0, len(out), 4):  # BGRA -> RGBA (opaque launch image: premultiplication irrelevant)
        out[i], out[i + 2] = out[i + 2], out[i]
    return Image.frombytes('RGBA', (w, h), bytes(out)).convert('RGB')


def fit(img, size):
    """Crop to the target aspect (centered), then resize."""
    tw, th = size
    w, h = img.size
    if w * th > h * tw:
        nw = h * tw // th
        img = img.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    else:
        nh = w * th // tw
        img = img.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    return img.resize(size, Image.LANCZOS)


def main():
    app = os.path.join(ROOT, 'game_app')
    art = os.path.join(ROOT, 'realracinghd_extract', 'iTunesArtwork')
    splash = cgbi_to_image(os.path.join(app, 'Default-Landscape.png'))
    icon = Image.open(art).convert('RGB')
    os.makedirs(OUT, exist_ok=True)
    jobs = {'icon0.png': (icon, (128, 128)), 'bg0.png': (splash, (840, 500)),
            'pic0.png': (splash, (960, 544)), 'startup.png': (splash, (280, 158))}
    for name, (img, size) in jobs.items():
        out = os.path.join(OUT, name)
        # bg0.png must stay under 128 KB (vita-pack-vpk): the asphalt noise is smoothed until it fits.
        for blur in (0, 1, 2, 3, 5):
            im = fit(img, size)
            if blur:
                im = im.filter(ImageFilter.GaussianBlur(blur))
            im.quantize(64 if blur else 256, dither=Image.Dither.NONE).save(out, optimize=True)
            if name != 'bg0.png' or os.path.getsize(out) < 120 * 1024:
                break
        print('  %-12s %dx%d  blur %d, %d KB' % (name, *size, blur, os.path.getsize(out) // 1024))
    tpl = os.path.join(ROOT, 'extras', 'livearea', 'template.xml')
    open(os.path.join(OUT, 'template.xml'), 'w').write(open(tpl).read())
    print('[+] LiveArea ->', OUT)


if __name__ == '__main__':
    sys.exit(main())
