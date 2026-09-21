#!/usr/bin/env python3
"""mkassets.py — собирает все ассеты в build/assets: уровни (.msh), палитры (.pal), XMB-картинки.

Использование: python3 tools/mkassets.py build/assets
"""
import struct
import subprocess
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAL_MAGIC = b"APAL"
PAL_VERSION = 1


def color_abgr(hex_str):
    """'#rrggbb' -> u32 0xAABBGGRR (формат цвета PSP)."""
    s = hex_str.lstrip("#")
    r, g, b = int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    return 0xFF000000 | (b << 16) | (g << 8) | r


def build_palettes(out_dir):
    src = ROOT / "assets" / "palettes.toml"
    data = tomllib.loads(src.read_text(encoding="utf-8"))
    blob = struct.pack("<4sII", PAL_MAGIC, PAL_VERSION, len(data))
    for name, p in data.items():
        slots = [color_abgr(c) for c in p["slots"]]
        if len(slots) != 8:
            sys.exit(f"палитра {name}: нужно ровно 8 слотов")
        blob += struct.pack("<16sIIff8I", name.encode("utf-8"), color_abgr(p["sky_top"]),
                            color_abgr(p["sky_bottom"]), float(p["fog_near"]),
                            float(p["fog_far"]), *slots)
    (out_dir / "palettes.pal").write_bytes(blob)
    print(f"palettes.pal: {len(data)} палитр ({', '.join(data)})")


def build_levels(out_dir):
    for src in sorted((ROOT / "levels").glob("*.toml")):
        dst = out_dir / (src.stem + ".msh")
        if dst.exists() and dst.stat().st_mtime >= src.stat().st_mtime \
                and dst.stat().st_mtime >= (ROOT / "tools" / "levelc.py").stat().st_mtime:
            print(f"{dst.name}: актуален")
            continue
        subprocess.run([sys.executable, str(ROOT / "tools" / "levelc.py"), str(src), str(dst)], check=True)


def build_xmb(out_dir):
    subprocess.run([sys.executable, str(ROOT / "tools" / "mkxmb.py"), str(out_dir / "xmb")], check=True)


def main():
    out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build" / "assets")
    out_dir.mkdir(parents=True, exist_ok=True)
    build_palettes(out_dir)
    build_levels(out_dir)
    build_xmb(out_dir)


if __name__ == "__main__":
    main()
