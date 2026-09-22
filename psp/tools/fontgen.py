#!/usr/bin/env python3
"""fontgen.py — собирает шрифтовой атлас AFNT v1 (docs/FORMATS.md) из одного ttf.

Использование:
    python3 tools/fontgen.py <путь/font.bin>     собрать атлас + превью рядом
    python3 tools/fontgen.py --verify <font.bin> разобрать готовый файл и проверить

Зависимости: только pillow и stdlib (tomllib). fontTools НЕ используется: в CI-контейнере
его нет, а вывод должен быть детерминированным на любой машине.

Две гарнитуры из одного ttf: индекс 0 — корпусная, индекс 1 — заголовочная.
Атлас — 512x512, один канал (альфа), упаковка полками с отступом 1 px.
"""
import argparse
import math
import struct
import sys
import tomllib
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT_TTF = ROOT / "assets" / "font" / "Inter-SemiBold.ttf"
KERNING_TOML = ROOT / "assets" / "font" / "kerning.toml"

MAGIC = b"AFNT"
VERSION = 1
TEX_W = 512
TEX_H = 512
PAD = 1              # отступ между глифами и от краёв атласа
ALIGN = 16           # выравнивание пикселей (и таблиц) — правило 2 из CLAUDE.md

BODY_SIZE = 15       # корпусный кегль, гарнитура 0
TITLE_SIZE = 30      # заголовочный кегль, гарнитура 1; уменьшается, если не влез
TITLE_MIN = 20

# Ключи гарнитур в kerning.toml — в порядке индексов гарнитур.
FACE_KEYS = ("body", "title")

# Кодпоинт-зонд из плоскости 15 (PUA): его нет ни в одном шрифте. Если глиф символа
# рисуется так же, как зонд, значит шрифт отдал .notdef, то есть символа в шрифте нет.
NOTDEF_PROBE = 0x0F0000

# Записи формата. Размеры проверяются ниже: они — часть контракта.
HDR_FMT = "<4sIIIIIII"      # magic, version, tex_w, tex_h, face_count, px_off, px_size, резерв
FACE_FMT = "<HHhHIHHI"      # px_size, line_height, baseline, glyph_count, glyph_off,
                            # kern_count, pad, kern_off
GLYPH_FMT = "<IHHBBbbBB2x"  # codepoint, u, v, w, h, xoff, yoff, advance, pad + 2 байта до 16
KERN_FMT = "<HHb3x"         # first, second, dx, pad[3]
HDR_SIZE, FACE_SIZE, GLYPH_SIZE, KERN_SIZE = 32, 20, 16, 8

_SIZES = ((HDR_FMT, HDR_SIZE), (FACE_FMT, FACE_SIZE), (GLYPH_FMT, GLYPH_SIZE),
          (KERN_FMT, KERN_SIZE))
for _fmt, _want in _SIZES:
    if struct.calcsize(_fmt) != _want:
        raise SystemExit(f"fontgen: запись {_fmt} = {struct.calcsize(_fmt)} байт, нужно {_want}")

I8_MIN, I8_MAX, U8_MAX = -128, 127, 255


def build_charset():
    """Набор символов атласа (см. задание): ASCII, кириллица, типографские знаки."""
    cps = list(range(0x20, 0x7F))        # ASCII 0x20..0x7E
    cps += list(range(0x410, 0x450))     # А..я
    cps += [0x401, 0x451]                # Ё, ё
    cps += [0x00AB, 0x00BB,              # «, »
            0x2013, 0x2014,              # –, —
            0x2026,                      # …
            0x2018, 0x2019,              # ‘, ’
            0x201C, 0x201D,              # “, ”
            0x00B7, 0x00B0,              # ·, °
            0x2116,                      # №
            0xFFFD]                      # символ-заменитель
    return sorted(set(cps))


# --- рендер одного глифа -------------------------------------------------------

class FaceRender:
    """Обёртка над ImageFont: рендер глифа в плотную маску "L" и метрики."""

    def __init__(self, ttf, px_size):
        self.px_size = px_size
        # BASIC — без libraqm, чтобы раскладка не зависела от окружения.
        self.font = ImageFont.truetype(str(ttf), px_size, layout_engine=ImageFont.Layout.BASIC)
        self.ascent, self.descent = self.font.getmetrics()
        self.line_height = math.ceil(self.ascent + self.descent)
        self.baseline = self.ascent
        self._probe = self._raw(chr(NOTDEF_PROBE))

    def _raw(self, text):
        """Маска "L" и её смещение от точки пера; None, если пикселей нет.

        Точка пера — на базовой линии слева. Рисуем с якорем по умолчанию ("la",
        верх линии ascent), поэтому из y вычитаем ascent. Маска обрезается по
        непрозрачным пикселям, так что w/h — реальные размеры чернил.
        """
        m = self.px_size + 8  # запас под свисающие влево/вправо элементы
        w = m * 2 + int(math.ceil(self.font.getlength(text))) + self.px_size * 2
        h = m * 2 + self.ascent + self.descent
        img = Image.new("L", (w, h), 0)
        draw = ImageDraw.Draw(img)
        # На изображении "L" ImageDraw рисует сглаженной маской режима "L".
        draw.text((m, m), text, font=self.font, fill=255)
        ink = img.getbbox()
        if ink is None:
            return None
        if ink[0] == 0 or ink[1] == 0 or ink[2] == w or ink[3] == h:
            raise SystemExit(f"fontgen: глиф {text!r} ({self.px_size} px) обрезан холстом "
                             f"{w}x{h}, чернила {ink} — увеличить запас в _raw()")
        return img.crop(ink), ink[0] - m, ink[1] - m - self.ascent

    def glyph(self, cp):
        """(маска|None, xoff, yoff, advance, missing) для кодпоинта."""
        raw = self._raw(chr(cp))
        advance = int(round(self.font.getlength(chr(cp))))
        missing = self._same_as_probe(raw)
        if missing or raw is None:
            return None, 0, 0, advance, missing
        return raw[0], raw[1], raw[2], advance, False

    def _same_as_probe(self, raw):
        if self._probe is None or raw is None:
            return False
        a, b = raw, self._probe
        return (a[1], a[2]) == (b[1], b[2]) and a[0].tobytes() == b[0].tobytes()

    def box_glyph(self):
        """Рамка-прямоугольник вместо отсутствующего 0xFFFD.

        Высота — по прописным, ширина — 0.6 от высоты, толщина линии растёт с кеглем.
        """
        h = max(5, round(self.px_size * 0.72))
        w = max(4, round(h * 0.6))
        line = max(1, round(self.px_size / 16))
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).rectangle((0, 0, w - 1, h - 1), outline=255, width=line)
        return img, 1, -h, w + 2


# --- упаковка полками ----------------------------------------------------------

def pack_shelves(items, tex_w, tex_h):
    """Раскладывает (key, w, h) по полкам сверху вниз. -> {key: (u, v)} или None.

    Глифы сортируются по убыванию высоты, чтобы полки были плотными. Между глифами
    и от краёв — PAD пикселей, иначе линейная фильтрация тянет соседние чернила.
    """
    order = sorted(items, key=lambda it: (-it[2], -it[1], it[0]))
    pos = {}
    x, y, shelf_h = PAD, PAD, 0
    for key, w, h in order:
        if x + w + PAD > tex_w:      # полка кончилась — следующая
            x, y, shelf_h = PAD, y + shelf_h + PAD, 0
        if y + h + PAD > tex_h:
            return None
        pos[key] = (x, y)
        x += w + PAD
        shelf_h = max(shelf_h, h)
    return pos


# --- сборка гарнитур -----------------------------------------------------------

def clamp(val, lo, hi, warns, what):
    if val < lo or val > hi:
        warns.append(f"{what}: {val} вне [{lo}, {hi}], ограничено")
        return max(lo, min(hi, val))
    return val


def build_face(ttf, px_size, charset, warns, face_name):
    """Список глифов гарнитуры (отсортирован по кодпоинту) + метрики."""
    fr = FaceRender(ttf, px_size)
    glyphs, synth = [], []
    for cp in charset:
        mask, xoff, yoff, advance, missing = fr.glyph(cp)
        if missing:
            if cp != 0xFFFD:
                warns.append(f"{face_name}: в шрифте нет U+{cp:04X}, глиф пустой")
                mask, xoff, yoff, advance = None, 0, 0, advance or 1
            else:
                mask, xoff, yoff, advance = fr.box_glyph()
                synth.append(cp)
        w, h = (mask.size if mask is not None else (0, 0))
        if w > U8_MAX or h > U8_MAX:
            raise SystemExit(f"{face_name}: глиф U+{cp:04X} {w}x{h} не влезает в u8")
        glyphs.append({
            "cp": cp, "mask": mask, "w": w, "h": h,
            "xoff": clamp(xoff, I8_MIN, I8_MAX, warns, f"{face_name} U+{cp:04X} xoff"),
            "yoff": clamp(yoff, I8_MIN, I8_MAX, warns, f"{face_name} U+{cp:04X} yoff"),
            "advance": clamp(advance, 0, U8_MAX, warns, f"{face_name} U+{cp:04X} advance"),
        })
    glyphs.sort(key=lambda g: g["cp"])
    return {"name": face_name, "px_size": px_size, "line_height": fr.line_height,
            "baseline": fr.baseline, "glyphs": glyphs, "kerns": [], "synth": synth,
            "render": fr}


def load_kerning(faces, warns):
    """Читает kerning.toml и превращает пары символов в пары индексов глифов."""
    if not KERNING_TOML.exists():
        warns.append(f"нет {KERNING_TOML.name}, кернинг пуст")
        return
    data = tomllib.loads(KERNING_TOML.read_text(encoding="utf-8"))
    for idx, face in enumerate(faces):
        key = FACE_KEYS[idx] if idx < len(FACE_KEYS) else face["name"]
        pairs = data.get(key, {}).get("pairs", [])
        index = {g["cp"]: i for i, g in enumerate(face["glyphs"])}
        table = {}
        for pair in pairs:
            if len(pair) != 3:
                warns.append(f"kerning.toml [{key}]: пара {pair!r} — нужно [a, b, dx]")
                continue
            a, b, dx = pair
            ca, cb = ord(a), ord(b)
            if ca not in index or cb not in index:
                warns.append(f"kerning.toml [{key}]: пара {a!r}{b!r} вне набора символов")
                continue
            ia, ib = index[ca], index[cb]
            if (ia << 16) | ib in table:
                warns.append(f"kerning.toml [{key}]: пара {a!r}{b!r} задана дважды, взята последняя")
            table[(ia << 16) | ib] = (ia, ib,
                                      clamp(int(dx), I8_MIN, I8_MAX, warns,
                                            f"kerning [{key}] {a!r}{b!r}"))
        face["kerns"] = [table[k] for k in sorted(table)]


# --- запись файла --------------------------------------------------------------

def align_up(val, step=ALIGN):
    return (val + step - 1) // step * step


def render_atlas(faces):
    """Пакует глифы всех гарнитур в один атлас; проставляет u/v. -> (Image, ink_px)."""
    items = []
    for fi, face in enumerate(faces):
        for gi, g in enumerate(face["glyphs"]):
            if g["w"] and g["h"]:
                items.append(((fi, gi), g["w"], g["h"]))
    pos = pack_shelves(items, TEX_W, TEX_H)
    if pos is None:
        return None, 0
    atlas = Image.new("L", (TEX_W, TEX_H), 0)
    ink = 0
    for fi, face in enumerate(faces):
        for gi, g in enumerate(face["glyphs"]):
            if not (g["w"] and g["h"]):
                g["u"], g["v"] = 0, 0
                continue
            u, v = pos[(fi, gi)]
            atlas.paste(g["mask"], (u, v))
            g["u"], g["v"] = u, v
            ink += g["w"] * g["h"]
    return atlas, ink


def pack_afnt(faces, atlas):
    """Собирает байты файла AFNT v1."""
    off = HDR_SIZE + FACE_SIZE * len(faces)
    for face in faces:
        off = align_up(off)
        face["glyph_off"] = off
        off += GLYPH_SIZE * len(face["glyphs"])
        off = align_up(off)
        face["kern_off"] = off if face["kerns"] else 0
        off += KERN_SIZE * len(face["kerns"])
    pixels_off = align_up(off)
    pixels = atlas.tobytes()
    assert len(pixels) == TEX_W * TEX_H

    blob = bytearray(pixels_off)
    struct.pack_into(HDR_FMT, blob, 0, MAGIC, VERSION, TEX_W, TEX_H, len(faces),
                     pixels_off, len(pixels), 0)
    for i, face in enumerate(faces):
        struct.pack_into(FACE_FMT, blob, HDR_SIZE + FACE_SIZE * i,
                         face["px_size"], face["line_height"], face["baseline"],
                         len(face["glyphs"]), face["glyph_off"],
                         len(face["kerns"]), 0, face["kern_off"])
        at = face["glyph_off"]
        for g in face["glyphs"]:
            struct.pack_into(GLYPH_FMT, blob, at, g["cp"], g["u"], g["v"], g["w"], g["h"],
                             g["xoff"], g["yoff"], g["advance"], 0)
            at += GLYPH_SIZE
        at = face["kern_off"]
        for first, second, dx in face["kerns"]:
            struct.pack_into(KERN_FMT, blob, at, first, second, dx)
            at += KERN_SIZE
    return bytes(blob) + pixels


# --- превью --------------------------------------------------------------------

SAMPLES = ("АРГУС — Argus 0123", "«Ёж», №1 · 25° … –")


def layout_from_atlas(face, text):
    """Повторяет font_layout из core/font.h: квады глифов и итоговая ширина."""
    index = {g["cp"]: i for i, g in enumerate(face["glyphs"])}
    kern = {(f << 16) | s: dx for f, s, dx in face["kerns"]}
    quads, pen, prev = [], 0, None
    for ch in text:
        gi = index.get(ord(ch), index.get(0xFFFD))
        if gi is None:
            continue
        g = face["glyphs"][gi]
        if prev is not None:
            pen += kern.get((prev << 16) | gi, 0)
        if g["w"] and g["h"]:
            quads.append((pen + g["xoff"], g["yoff"], g["w"], g["h"], g["u"], g["v"]))
        pen += g["advance"]
        prev = gi
    return quads, pen


def write_preview(path, faces, atlas):
    """Превью: строки-образцы, разложенные по атласу, и сам атлас серым."""
    margin = 8
    rows = [(face, s) for face in faces for s in SAMPLES]
    head_h = margin
    for face, _ in rows:
        head_h += face["line_height"] + 4
    img = Image.new("L", (TEX_W, head_h + margin + TEX_H), 24)
    y = margin
    for face, text in rows:
        quads, _ = layout_from_atlas(face, text)
        base = y + face["baseline"]
        for qx, qy, qw, qh, u, v in quads:
            glyph = atlas.crop((u, v, u + qw, v + qh))
            img.paste(255, (margin + qx, base + qy), glyph)
        y += face["line_height"] + 4
    img.paste(atlas, (0, head_h + margin))
    img.save(path)


# --- независимый читатель для --verify -----------------------------------------

def verify(path):
    """Разбирает готовый font.bin с нуля и проверяет инварианты. -> список ошибок."""
    blob = Path(path).read_bytes()
    errs = []

    def bad(msg):
        errs.append(msg)

    if len(blob) < HDR_SIZE:
        return [f"файл короче заголовка: {len(blob)} байт"]
    magic, version, tex_w, tex_h, face_count, px_off, px_size, reserved = \
        struct.unpack_from(HDR_FMT, blob, 0)
    if magic != MAGIC:
        bad(f"магия {magic!r}, ожидалось {MAGIC!r}")
    if version != VERSION:
        bad(f"версия {version}, ожидалась {VERSION}")
    if (tex_w, tex_h) != (TEX_W, TEX_H):
        bad(f"размер атласа {tex_w}x{tex_h}, ожидался {TEX_W}x{TEX_H}")
    if face_count < 1 or face_count > 4:
        return errs + [f"face_count = {face_count}"]
    if reserved != 0:
        bad(f"резерв заголовка = {reserved}, ожидался 0")
    if px_off % ALIGN:
        bad(f"pixels_offset = {px_off}, не кратен {ALIGN}")
    if px_size != tex_w * tex_h:
        bad(f"pixels_size = {px_size}, ожидался {tex_w * tex_h}")
    if px_off + px_size != len(blob):
        bad(f"pixels_offset + pixels_size = {px_off + px_size}, размер файла {len(blob)}")

    need = {0x41: "'A'", 0x416: "'Ж'", 0x20: "' '", 0xFFFD: "U+FFFD"}
    for fi in range(face_count):
        fo = HDR_SIZE + FACE_SIZE * fi
        if fo + FACE_SIZE > len(blob):
            bad(f"гарнитура {fi}: запись за концом файла")
            continue
        px, lh, base, gcount, goff, kcount, pad, koff = struct.unpack_from(FACE_FMT, blob, fo)
        tag = f"гарнитура {fi} ({px} px)"
        if pad != 0:
            bad(f"{tag}: pad = {pad}, ожидался 0")
        if px <= 0 or lh <= 0:
            bad(f"{tag}: px_size = {px}, line_height = {lh}")
        if base <= 0 or base > lh:
            bad(f"{tag}: baseline = {base} вне (0, {lh}]")
        if goff % 4:
            bad(f"{tag}: glyph_offset = {goff}, не кратен 4")
        if goff + GLYPH_SIZE * gcount > px_off:
            bad(f"{tag}: таблица глифов заходит в пиксели")
            continue
        prev_cp, seen = -1, {}
        for gi in range(gcount):
            cp, u, v, w, h, xoff, yoff, adv, gpad = \
                struct.unpack_from(GLYPH_FMT, blob, goff + GLYPH_SIZE * gi)
            if cp <= prev_cp:
                bad(f"{tag}: глиф {gi} U+{cp:04X} нарушает сортировку по кодпоинту")
            prev_cp = cp
            if gpad != 0:
                bad(f"{tag}: U+{cp:04X}: pad = {gpad}, ожидался 0")
            if w and h:
                if u + w > tex_w or v + h > tex_h:
                    bad(f"{tag}: U+{cp:04X}: прямоугольник {u},{v} {w}x{h} вне атласа")
                if adv <= 0:
                    bad(f"{tag}: U+{cp:04X}: непустой глиф с advance = {adv}")
            seen[cp] = (u, v, w, h)
        for cp, label in need.items():
            if cp not in seen:
                bad(f"{tag}: нет глифа {label}")
        if kcount:
            if koff % 4:
                bad(f"{tag}: kern_offset = {koff}, не кратен 4")
            if koff + KERN_SIZE * kcount > px_off:
                bad(f"{tag}: таблица кернинга заходит в пиксели")
                continue
            prev_key = -1
            for ki in range(kcount):
                first, second, dx = struct.unpack_from(KERN_FMT, blob, koff + KERN_SIZE * ki)
                key = (first << 16) | second
                if key <= prev_key:
                    bad(f"{tag}: кернинг {ki} ({first}, {second}) нарушает сортировку")
                prev_key = key
                if first >= gcount or second >= gcount:
                    bad(f"{tag}: кернинг {ki}: индекс глифа вне таблицы")
                if dx == 0:
                    bad(f"{tag}: кернинг {ki}: dx = 0, запись бесполезна")

    # Таблицы не должны пересекаться между собой.
    spans = []
    for fi in range(face_count):
        px, lh, base, gcount, goff, kcount, pad, koff = \
            struct.unpack_from(FACE_FMT, blob, HDR_SIZE + FACE_SIZE * fi)
        if gcount:
            spans.append((goff, goff + GLYPH_SIZE * gcount, f"глифы {fi}"))
        if kcount:
            spans.append((koff, koff + KERN_SIZE * kcount, f"кернинг {fi}"))
    spans.sort()
    head_end = HDR_SIZE + FACE_SIZE * face_count
    for a0, _a1, an in spans:
        if a0 < head_end:
            bad(f"таблица {an} начинается в {a0}, заходит в записи гарнитур (до {head_end})")
    for (a0, a1, an), (b0, b1, bn) in zip(spans, spans[1:]):
        if b0 < a1:
            bad(f"таблицы пересекаются: {an} [{a0}, {a1}) и {bn} [{b0}, {b1})")
    return errs


# --- точка входа ---------------------------------------------------------------

def generate(out_path):
    if not FONT_TTF.exists():
        raise SystemExit(f"fontgen: нет шрифта {FONT_TTF}")
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    charset = build_charset()
    notes = []  # предупреждения об уменьшении кегля — живут между попытками

    title_size = TITLE_SIZE
    while True:
        warns = []  # предупреждения по глифам: только от удачной попытки
        faces = [build_face(FONT_TTF, BODY_SIZE, charset, warns, "body"),
                 build_face(FONT_TTF, title_size, charset, warns, "title")]
        atlas, ink = render_atlas(faces)
        if atlas is not None:
            break
        if title_size <= TITLE_MIN:
            raise SystemExit(f"fontgen: атлас {TEX_W}x{TEX_H} мал даже для заголовка {TITLE_MIN} px")
        title_size -= 1
        notes.append(f"заголовочный кегль уменьшен до {title_size} px: атлас переполнен")
    warns = notes + warns

    load_kerning(faces, warns)
    blob = pack_afnt(faces, atlas)
    out_path.write_bytes(blob)
    preview = out_path.parent / "font_preview.png"
    write_preview(preview, faces, atlas)

    print(f"{out_path.name}: AFNT v1, атлас {TEX_W}x{TEX_H}, шрифт {FONT_TTF.name}")
    for i, face in enumerate(faces):
        print(f"  гарнитура {i} ({face['name']}): {face['px_size']} px, "
              f"{len(face['glyphs'])} глифов, line_height {face['line_height']}, "
              f"baseline {face['baseline']}, кернинг {len(face['kerns'])} пар"
              + (f", нарисовано вручную: {len(face['synth'])}" if face["synth"] else ""))
    print(f"  занятость атласа: {100.0 * ink / (TEX_W * TEX_H):.1f}% "
          f"({ink} из {TEX_W * TEX_H} пикселей)")
    print(f"  размер файла: {len(blob)} байт, пиксели с {struct.unpack_from(HDR_FMT, blob, 0)[5]}")
    print(f"  превью: {preview}")
    for w in warns:
        print(f"  ВНИМАНИЕ: {w}")
    return out_path


def main(argv=None):
    ap = argparse.ArgumentParser(description="сборка шрифтового атласа AFNT v1")
    ap.add_argument("out", help="путь к font.bin")
    ap.add_argument("--verify", action="store_true",
                    help="не собирать, а разобрать готовый файл и проверить")
    args = ap.parse_args(argv)

    if args.verify:
        errs = verify(args.out)
    else:
        generate(args.out)
        errs = verify(args.out)  # собранный файл всегда проверяем
    if errs:
        print(f"--verify {args.out}: {len(errs)} ошибок")
        for e in errs:
            print(f"  ОШИБКА: {e}")
        return 1
    print(f"--verify {args.out}: формат AFNT v1 в порядке")
    return 0


if __name__ == "__main__":
    sys.exit(main())
