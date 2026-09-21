#!/usr/bin/env python3
"""mkxmb.py — картинки для меню XMB приставки: ICON0.PNG 144×82 и PIC1.PNG 480×272.

Использование: python3 tools/mkxmb.py build/assets/xmb
Стиль: градиент неба хаба, один большой глаз из плоских дисков. Пока — процедурная заглушка
в фирменной палитре; заменяется финальными кадрами игры на этапе 3.
"""
import sys
import tomllib
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent


def hex_rgb(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(size, top, bottom):
    img = Image.new("RGB", size)
    px = img.load()
    w, h = size
    for y in range(h):
        c = lerp(top, bottom, y / max(1, h - 1))
        for x in range(w):
            px[x, y] = c
    return img


def draw_eye(img, cx, cy, rx, pal):
    d = ImageDraw.Draw(img)
    ry = rx * 0.55
    d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=hex_rgb(pal["slots"][0]))
    ir = ry * 0.82
    d.ellipse((cx - ir, cy - ir, cx + ir, cy + ir), fill=hex_rgb(pal["slots"][3]))
    pr = ir * 0.42
    d.ellipse((cx - pr, cy - pr, cx + pr, cy + pr), fill=hex_rgb(pal["sky_top"]))
    hr = pr * 0.35
    d.ellipse((cx - pr * 0.5 - hr, cy - pr * 0.5 - hr, cx - pr * 0.5 + hr, cy - pr * 0.5 + hr),
              fill=hex_rgb(pal["slots"][5]))


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build" / "assets" / "xmb")
    out.mkdir(parents=True, exist_ok=True)
    pal = tomllib.loads((ROOT / "assets" / "palettes.toml").read_text(encoding="utf-8"))["hub"]
    top, bottom = hex_rgb(pal["sky_top"]), hex_rgb(pal["sky_bottom"])

    icon = gradient((144, 82), top, bottom)
    draw_eye(icon, 72, 41, 44, pal)
    icon.save(out / "ICON0.PNG")

    pic = gradient((480, 272), top, bottom)
    draw_eye(pic, 360, 136, 150, pal)
    pic.save(out / "PIC1.PNG")
    print(f"xmb: {out / 'ICON0.PNG'}, {out / 'PIC1.PNG'}")


if __name__ == "__main__":
    main()
