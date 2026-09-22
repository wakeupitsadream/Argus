#!/usr/bin/env python3
"""mkxmb.py — картинки для меню XMB приставки: ICON0.PNG 144×82 и PIC1.PNG 480×272.

Использование: python3 tools/mkxmb.py build/assets/xmb

Если в assets/xmb лежат кадры игры (frame_title.png — заставка, frame_island.png —
остров), картинки собираются из них: это первое, что видит человек в меню приставки,
и нарисованная заглушка там выглядит именно заглушкой. Кадры снимаются прогоном
эмулятора (tests/autoplay/art.txt, tools/run_emu.py) и кладутся в assets/xmb руками.
Без кадров остаётся процедурный вариант в фирменной палитре.
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


SRC_DIR = ROOT / "assets" / "xmb"


def darken_bottom(img, height, strength=0.72):
    """Притемняет низ кадра: в XMB поверх PIC1 идёт системный текст."""
    w, h = img.size
    px = img.load()
    for y in range(h - height, h):
        k = 1.0 - strength * ((y - (h - height)) / max(1, height - 1))
        for x in range(w):
            r, g, b = px[x, y]
            px[x, y] = (int(r * k), int(g * k), int(b * k))
    return img


def from_frames(out):
    """Собирает ICON0 и PIC1 из настоящих кадров игры. None, если кадров нет."""
    title = SRC_DIR / "frame_title.png"
    island = SRC_DIR / "frame_island.png"
    if not title.exists():
        return None

    pic = Image.open(title).convert("RGB")
    if pic.size != (480, 272):
        pic = pic.resize((480, 272), Image.LANCZOS)
    darken_bottom(pic, 70)
    pic.save(out / "PIC1.PNG")

    # Иконка: остров целиком в пропорции 144×82. В меню приставки картинка размером
    # с ноготь, поэтому берём силуэт целиком, а не фрагмент — фрагмент читается как каша.
    src = Image.open(title).convert("RGB")
    w, h = src.size
    crop_w = int(w * 0.68)
    crop_h = int(crop_w * 82 / 144)
    left = max(0, min(w - crop_w, int(w * 0.30)))
    top_y = max(0, min(h - crop_h, int(h * 0.14)))
    icon = src.crop((left, top_y, left + crop_w, top_y + crop_h)).resize((144, 82), Image.LANCZOS)
    icon.save(out / "ICON0.PNG")
    print(f"xmb: из кадров игры — {out / 'ICON0.PNG'}, {out / 'PIC1.PNG'}")
    return True


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build" / "assets" / "xmb")
    out.mkdir(parents=True, exist_ok=True)
    if from_frames(out):
        return
    pal = tomllib.loads((ROOT / "assets" / "palettes.toml").read_text(encoding="utf-8"))["hub"]
    top, bottom = hex_rgb(pal["sky_top"]), hex_rgb(pal["sky_bottom"])

    icon = gradient((144, 82), top, bottom)
    draw_eye(icon, 72, 41, 44, pal)
    icon.save(out / "ICON0.PNG")

    pic = gradient((480, 272), top, bottom)
    draw_eye(pic, 360, 136, 150, pal)
    pic.save(out / "PIC1.PNG")
    print(f"xmb: процедурная заглушка — {out / 'ICON0.PNG'}, {out / 'PIC1.PNG'}")


if __name__ == "__main__":
    main()
