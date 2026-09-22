#!/usr/bin/env python3
"""meshpreview.py — визуальная приёмка мешей объектов: все mesh_*.msh каталога одним PNG.

Использование:
    python3 tools/meshpreview.py <каталог> [--out <png>] [--palette hub] [--cols 5] [--tile 300]

Камера — как в игре (core/camera.c): ортографическая изометрия, yaw 45°, наклон 33°.
Заливка плоская, по нормали грани: один направленный свет плюс ambient, те же числа, что
в core/game.c, и та же формула, что в mesh_shade_color (цвет слота · ao · (ambient + diffuse·n·l)).
Видимость — отсечение задних граней по нормали плюс сортировка по глубине (алгоритм художника).

Под каждым объектом подписаны имя, габариты и число треугольников; в масштаб кадра всегда
включён квадрат 1×1 — клетка уровня, по нему читается размер объекта. Крестик — начало
координат меша (точка, в которую объект ставит игра).
Только Pillow, сторонних библиотек нет.
"""
import argparse
import math
import sys
import tomllib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from amsh import read_mesh  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PALETTES = ROOT / "assets" / "palettes.toml"
FONT_FILE = ROOT / "assets" / "font" / "Inter-SemiBold.ttf"

CAM_YAW_DEG = 45.0    # ракурс 0 камеры (camera.c: 45 + 90·angle)
CAM_PITCH_DEG = 33.0  # CAM_PITCH_DEG в camera.c
LIGHT_DIR = (0.45, 1.0, 0.3)  # game.c: направление НА источник
LIGHT_AMBIENT = 0.58
LIGHT_DIFFUSE = 0.42
SS = 2  # сглаживание: рисуем в двойном размере и уменьшаем

BG = (26, 29, 35)
TILE_BG = (31, 35, 42)
TILE_EDGE = (44, 50, 60)
FLOOR_FILL = (37, 42, 51)
FLOOR_EDGE = (62, 70, 85)
FLOOR_FRONT = (104, 118, 140)
ORIGIN_MARK = (150, 120, 90)
TEXT = (233, 237, 243)
TEXT_DIM = (141, 151, 166)

# Палитра на случай, когда assets/palettes.toml недоступен (слоты hub).
FALLBACK_SLOTS = ["#ece1cb", "#dccfb6", "#b39b7c", "#f0b35a",
                  "#5c6b8f", "#fff3c9", "#6fa3c9", "#d9707f"]


def load_slots(name):
    """Цвета слотов палитры региона из assets/palettes.toml."""
    try:
        with PALETTES.open("rb") as f:
            data = tomllib.load(f)
        raw = data[name]["slots"]
    except (OSError, KeyError, tomllib.TOMLDecodeError):
        raw = FALLBACK_SLOTS
    out = []
    for s in raw[:8]:
        s = s.lstrip("#")
        out.append((int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)))
    while len(out) < 8:
        out.append((200, 200, 200))
    return out


def view_basis():
    """Оси камеры: xc — вправо по экрану, yc — вверх, zc — от цели к глазу."""
    yaw, pitch = math.radians(CAM_YAW_DEG), math.radians(CAM_PITCH_DEG)
    zc = (math.cos(pitch) * math.sin(yaw), math.sin(pitch), math.cos(pitch) * math.cos(yaw))
    xl = math.hypot(zc[2], zc[0])
    xc = (zc[2] / xl, 0.0, -zc[0] / xl)            # up × zc, нормировано
    yc = (zc[1] * xc[2] - zc[2] * xc[1],
          zc[2] * xc[0] - zc[0] * xc[2],
          zc[0] * xc[1] - zc[1] * xc[0])           # zc × xc
    return xc, yc, zc


XC, YC, ZC = view_basis()


def project(p):
    """Мировая точка → (экранный x, экранный y вниз, глубина: больше — ближе к камере)."""
    return (p[0] * XC[0] + p[1] * XC[1] + p[2] * XC[2],
            -(p[0] * YC[0] + p[1] * YC[1] + p[2] * YC[2]),
            p[0] * ZC[0] + p[1] * ZC[1] + p[2] * ZC[2])


def light_dir():
    d = LIGHT_DIR
    n = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    return (d[0] / n, d[1] / n, d[2] / n)


LDIR = light_dir()


def shade(rgb, ao, n):
    """Та же формула, что mesh_shade_color в core/mesh.c."""
    ndl = n[0] * LDIR[0] + n[1] * LDIR[1] + n[2] * LDIR[2]
    if ndl < 0.0:
        ndl = 0.0
    k = (ao / 255.0) * (LIGHT_AMBIENT + LIGHT_DIFFUSE * ndl)
    return tuple(min(255, int(c * k + 0.5)) for c in rgb)


def load_tris(path):
    """Грани файла: (глубина, точки экрана, нормаль, слот, ao вершин). Задние грани отброшены."""
    hdr, verts = read_mesh(path)
    tris = []
    for t in range(hdr["count"] // 3):
        v = verts[t * 3:t * 3 + 3]
        n = (v[0][3], v[0][4], v[0][5])
        if n[0] * ZC[0] + n[1] * ZC[1] + n[2] * ZC[2] <= 0.0:
            continue  # грань смотрит от камеры (GU_CULL)
        pts = [project((a[0], a[1], a[2])) for a in v]
        tris.append((sum(p[2] for p in pts) / 3.0, [(p[0], p[1]) for p in pts], n, v[0][6],
                     (v[0][7], v[1][7], v[2][7])))
    tris.sort(key=lambda e: e[0])  # дальние раньше — алгоритм художника
    return hdr, verts, tris


def ao_patches(pts, ao, steps):
    """Дробит грань на подтреугольники с интерполированным ao: GPU растит ao по вершинам,
    одна плоская заливка на грань дала бы ложные полосы по диагоналям квадов."""
    if max(ao) - min(ao) <= 5 or steps <= 1:
        yield pts, sum(ao) / 3.0
        return
    (x0, y0), (x1, y1), (x2, y2) = pts

    def at(i, j):
        w1, w2 = i / steps, j / steps
        w0 = 1.0 - w1 - w2
        return (x0 * w0 + x1 * w1 + x2 * w2, y0 * w0 + y1 * w1 + y2 * w2,
                ao[0] * w0 + ao[1] * w1 + ao[2] * w2)

    for i in range(steps):
        for j in range(steps - i):
            a, b, c = at(i, j), at(i + 1, j), at(i, j + 1)
            yield [(a[0], a[1]), (b[0], b[1]), (c[0], c[1])], (a[2] + b[2] + c[2]) / 3.0
            if i + j < steps - 1:
                d = at(i + 1, j + 1)
                yield ([(b[0], b[1]), (d[0], d[1]), (c[0], c[1])],
                       (b[2] + d[2] + c[2]) / 3.0)


def floor_square(y):
    """Клетка уровня 1×1 под объектом: контур и передняя (+Z) сторона."""
    c = [(-0.5, y, -0.5), (0.5, y, -0.5), (0.5, y, 0.5), (-0.5, y, 0.5)]
    return [project(p) for p in c]


def pick_font(size):
    try:
        return ImageFont.truetype(str(FONT_FILE), size)
    except OSError:
        try:
            return ImageFont.load_default(size=size)
        except TypeError:
            return ImageFont.load_default()


def main(argv=None):
    ap = argparse.ArgumentParser(description="Изометрическое превью мешей объектов ARGUS")
    ap.add_argument("indir", help="каталог с mesh_*.msh")
    ap.add_argument("--out", default=None, help="PNG (по умолчанию <каталог>/mesh_preview.png)")
    ap.add_argument("--palette", default="hub", help="палитра из assets/palettes.toml")
    ap.add_argument("--cols", type=int, default=5, help="объектов в ряду")
    ap.add_argument("--tile", type=int, default=300, help="сторона клетки превью, пикселей")
    ap.add_argument("--ao-steps", type=int, default=4,
                    help="дробление грани для градиента ao (1 — плоская заливка)")
    args = ap.parse_args(argv)

    indir = Path(args.indir)
    files = sorted(indir.glob("mesh_*.msh"))
    if not files:
        # Превью — диагностика, а не ассет игры: не валим сборку из-за пустого каталога.
        print(f"meshpreview: в {indir} нет файлов mesh_*.msh — нечего рисовать")
        return 0
    slots = load_slots(args.palette)
    out_path = Path(args.out) if args.out else indir / "mesh_preview.png"

    cols = max(1, min(args.cols, len(files)))
    rows = (len(files) + cols - 1) // cols
    tw = th = max(120, args.tile)
    cap_h = 46
    head_h = 54
    pad = 26
    width, height = cols * tw, head_h + rows * (th + cap_h)

    img = Image.new("RGB", (width * SS, height * SS), BG)
    dr = ImageDraw.Draw(img)
    report = []

    for idx, path in enumerate(files):
        col, row = idx % cols, idx // cols
        x0, y0 = col * tw, head_h + row * (th + cap_h)
        dr.rectangle([x0 * SS, y0 * SS, (x0 + tw) * SS - 1, (y0 + th + cap_h) * SS - 1],
                     fill=TILE_BG, outline=TILE_EDGE, width=SS)

        hdr, verts, tris = load_tris(path)
        lo, hi = hdr["bbox"][:3], hdr["bbox"][3:]
        size = (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])
        floor = floor_square(lo[1])
        pts = [project((v[0], v[1], v[2])) for v in verts] + floor + [project((0.0, 0.0, 0.0))]
        sx = [p[0] for p in pts]
        sy = [p[1] for p in pts]
        span_x = max(1e-6, max(sx) - min(sx))
        span_y = max(1e-6, max(sy) - min(sy))
        scale = min((tw - 2 * pad) / span_x, (th - 2 * pad) / span_y)
        mid_x, mid_y = (max(sx) + min(sx)) / 2.0, (max(sy) + min(sy)) / 2.0
        cx, cy = x0 + tw / 2.0, y0 + th / 2.0

        def to_px(p):
            return ((cx + (p[0] - mid_x) * scale) * SS, (cy + (p[1] - mid_y) * scale) * SS)

        fl = [to_px(p) for p in floor]
        dr.polygon(fl, fill=FLOOR_FILL)
        dr.line(fl + [fl[0]], fill=FLOOR_EDGE, width=SS)
        dr.line([fl[2], fl[3]], fill=FLOOR_FRONT, width=SS)  # сторона +Z — «лицо» объекта

        for _, tri, n, slot, ao in tris:
            base = slots[slot if slot < 8 else 0]
            for patch, ao_mid in ao_patches([to_px(p) for p in tri], ao, args.ao_steps):
                col_rgb = shade(base, ao_mid, n)
                dr.polygon(patch, fill=col_rgb, outline=col_rgb)

        ox, oy = to_px(project((0.0, 0.0, 0.0)))
        m = 3 * SS
        dr.line([(ox - m, oy), (ox + m, oy)], fill=ORIGIN_MARK, width=SS)
        dr.line([(ox, oy - m), (ox, oy + m)], fill=ORIGIN_MARK, width=SS)

        report.append((path.stem[len("mesh_"):], size, len(verts) // 3, x0, y0))

    img = img.resize((width, height), Image.LANCZOS)
    dr = ImageDraw.Draw(img)
    f_head = pick_font(17)
    f_name = pick_font(16)
    f_dim = pick_font(12)
    dr.text((22, 16), "ARGUS — меши объектов", font=f_head, fill=TEXT)
    dr.text((22, 35), f"орто-изометрия yaw {CAM_YAW_DEG:.0f}°, наклон {CAM_PITCH_DEG:.0f}°; "
                      f"палитра «{args.palette}»; свет {LIGHT_AMBIENT:.2f} ambient + "
                      f"{LIGHT_DIFFUSE:.2f} diffuse; сетка — клетка уровня 1×1, "
                      f"крестик — начало координат, светлая сторона клетки — лицо (+Z)",
            font=f_dim, fill=TEXT_DIM)

    for name, size, tris, x0, y0 in report:
        ty = y0 + th + 6
        dims = f"{size[0]:.2f}×{size[1]:.2f}×{size[2]:.2f} · {tris} тр."
        for text, font, fill, dy in ((name, f_name, TEXT, 0), (dims, f_dim, TEXT_DIM, 20)):
            w = dr.textlength(text, font=font)
            dr.text((x0 + (tw - w) / 2.0, ty + dy), text, font=font, fill=fill)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(out_path)
    for name, size, tris, _, _ in report:
        print(f"{name:16s} {size[0]:.3f}×{size[1]:.3f}×{size[2]:.3f}  {tris:3d} тр.")
    print(f"{len(report)} мешей → {out_path} ({width}×{height})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
