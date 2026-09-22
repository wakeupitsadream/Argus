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
    levelc = ROOT / "tools" / "levelc.py"
    amsh = ROOT / "tools" / "amsh.py"
    tool_mtime = max(levelc.stat().st_mtime, amsh.stat().st_mtime)
    for src in sorted((ROOT / "levels").glob("*.toml")):
        msh = out_dir / (src.stem + ".msh")
        lvl = out_dir / (src.stem + ".lvl")
        if msh.exists() and lvl.exists() and min(msh.stat().st_mtime, lvl.stat().st_mtime) >= max(src.stat().st_mtime, tool_mtime):
            print(f"{src.stem}: актуален")
            continue
        subprocess.run([sys.executable, str(levelc), str(src), str(out_dir)], check=True)
    subprocess.run([sys.executable, str(levelc), "--headers", str(out_dir)], check=True)


def build_meshes(out_dir):
    gen = ROOT / "tools" / "meshgen.py"
    if not gen.exists():
        print("meshgen.py отсутствует — меши объектов не пересобираются")
        return
    # Меши объектов не блокируют сборку: если генератор сломан, остаются прежние файлы,
    # и игра запускается (проверяется отдельно в make test и в прогоне эмулятора).
    r = subprocess.run([sys.executable, str(gen), str(out_dir)])
    if r.returncode != 0:
        print(f"ВНИМАНИЕ: meshgen.py упал (код {r.returncode}) — используются ранее собранные меши")
    preview = ROOT / "tools" / "meshpreview.py"
    if preview.exists():
        subprocess.run([sys.executable, str(preview), str(out_dir)], check=True)


def build_font(out_dir):
    subprocess.run([sys.executable, str(ROOT / "tools" / "fontgen.py"), str(out_dir / "font.bin")], check=True)


def build_strings(out_dir):
    subprocess.run([sys.executable, str(ROOT / "tools" / "stringsgen.py"), str(out_dir)], check=True)


def build_quests(out_dir):
    gen = ROOT / "tools" / "questgen.py"
    if not gen.exists():
        print("questgen.py отсутствует — таблица квестов не пересобирается")
        return
    subprocess.run([sys.executable, str(gen), str(out_dir)], check=True)


def build_xmb(out_dir):
    subprocess.run([sys.executable, str(ROOT / "tools" / "mkxmb.py"), str(out_dir / "xmb")], check=True)


def main():
    out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "build" / "assets")
    out_dir.mkdir(parents=True, exist_ok=True)
    build_palettes(out_dir)
    build_levels(out_dir)
    build_meshes(out_dir)
    build_font(out_dir)
    build_strings(out_dir)
    build_quests(out_dir)
    build_xmb(out_dir)


if __name__ == "__main__":
    main()
