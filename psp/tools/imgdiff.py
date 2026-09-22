#!/usr/bin/env python3
"""imgdiff.py — сравнение кадра с эталоном: защита от незаметной порчи картинки.

    python3 tools/imgdiff.py build/shots/hub_start.png tests/golden/hub_start.png
    python3 tools/imgdiff.py --check build/shots tests/golden      # все эталоны разом
    python3 tools/imgdiff.py --update build/shots tests/golden     # обновить эталоны

Порог (докуметирован в docs/TECH.md §2.7): кадр считается изменившимся, если больше
DIFF_SHARE пикселей отличаются хотя бы на DIFF_LEVEL уровней по любому каналу.
Мелкая разница в один-два уровня — это округление растеризатора, её терпим;
уехавшая камера, потерянная палитра или пропавший объект дают сотни процентов порога.

Код возврата: 0 — совпало, 1 — расхождение или нет файла (для CI и make golden-check).
"""
import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pillow ставит ci/session-start.sh
    sys.exit("imgdiff: нужен pillow (pip install pillow)")

DIFF_LEVEL = 8      # на сколько уровней канала пиксель должен отличаться, чтобы считаться иным
DIFF_SHARE = 0.005  # доля таких пикселей, выше которой кадр считается изменившимся


def load(path):
    img = Image.open(path)
    return img.convert("RGB")


def compare(a_path, b_path, level=DIFF_LEVEL):
    """(доля различий, ширина, высота) либо None, если размеры не совпали."""
    a, b = load(a_path), load(b_path)
    if a.size != b.size:
        return None, a.size, b.size
    pa, pb = a.load(), b.load()
    w, h = a.size
    bad = 0
    for y in range(h):
        for x in range(w):
            ra, ga, ba = pa[x, y]
            rb, gb, bb = pb[x, y]
            if abs(ra - rb) >= level or abs(ga - gb) >= level or abs(ba - bb) >= level:
                bad += 1
    return bad / float(w * h), a.size, b.size


def check_dir(shots, golden, level, share, update):
    shots, golden = Path(shots), Path(golden)
    golden.mkdir(parents=True, exist_ok=True)
    if update:
        n = 0
        for src in sorted(shots.glob("*.png")):
            if src.name == "emu_last.png":
                continue  # служебный кадр эмулятора, не эталон
            (golden / src.name).write_bytes(src.read_bytes())
            n += 1
        print(f"эталонов обновлено: {n} → {golden}")
        return 0

    refs = sorted(golden.glob("*.png"))
    if not refs:
        print(f"imgdiff: в {golden} нет эталонов — нечего проверять")
        return 0
    bad = 0
    for ref in refs:
        shot = shots / ref.name
        if not shot.exists():
            print(f"НЕТ КАДРА {shot} (эталон {ref.name})")
            bad += 1
            continue
        diff, sa, sb = compare(shot, ref, level)
        if diff is None:
            print(f"РАЗМЕР {ref.name}: кадр {sa}, эталон {sb}")
            bad += 1
            continue
        mark = "ok" if diff <= share else "РАСХОЖДЕНИЕ"
        print(f"{ref.name:<24} {diff * 100:6.2f}% {mark}")
        if diff > share:
            bad += 1
    print(f"эталонов {len(refs)}, расхождений {bad}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description="сравнение кадров с эталонами")
    ap.add_argument("a", help="кадр или каталог кадров")
    ap.add_argument("b", help="эталон или каталог эталонов")
    ap.add_argument("--check", action="store_true", help="сравнить каталоги")
    ap.add_argument("--update", action="store_true", help="переписать эталоны кадрами")
    ap.add_argument("--level", type=int, default=DIFF_LEVEL)
    ap.add_argument("--share", type=float, default=DIFF_SHARE)
    args = ap.parse_args()

    if args.check or args.update:
        return check_dir(args.a, args.b, args.level, args.share, args.update)

    for path in (args.a, args.b):
        if not Path(path).exists():
            print(f"imgdiff: нет {path}")
            return 1
    diff, sa, sb = compare(args.a, args.b, args.level)
    if diff is None:
        print(f"размеры разные: {sa} и {sb}")
        return 1
    print(f"{diff * 100:.2f}% пикселей отличаются больше чем на {args.level} уровней")
    return 0 if diff <= args.share else 1


if __name__ == "__main__":
    raise SystemExit(main())
