#!/usr/bin/env python3
"""run_emu.py — прогон EBOOT в PPSSPPHeadless со скриптом autoplay, сбор скриншотов в PNG.

Использование:
  python3 tools/run_emu.py --emu ~/ppsspp/PPSSPPHeadless --game build/ARGUS \
      --script tests/autoplay/smoke.txt --seconds 20 --out build/shots

Собирает структуру карты памяти build/memstick/PSP/GAME/ARGUS/, кладёт туда autoplay.txt,
запускает эмулятор (программный рендер, таймаут), конвертирует shots/*.bmp игры в PNG и
дополнительно сохраняет последний кадр средствами эмулятора (--screenshot-save).
"""
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emu", required=True)
    ap.add_argument("--game", required=True, help="каталог с EBOOT.PBP и data/")
    ap.add_argument("--script", default=None, help="autoplay.txt для прогона")
    ap.add_argument("--seconds", type=int, default=20)
    ap.add_argument("--out", default="build/shots")
    ap.add_argument("--memstick", default="build/memstick")
    ap.add_argument("--log", action="store_true", help="полный лог эмулятора (-l)")
    ap.add_argument("--extra", nargs=argparse.REMAINDER, default=[], help="доп. аргументы эмулятора")
    args = ap.parse_args()

    emu = Path(args.emu)
    if not emu.exists():
        sys.exit(f"эмулятор не найден: {emu} (собери: bash ci/build-ppsspp.sh)")
    game_dir = Path(args.memstick) / "PSP" / "GAME" / "ARGUS"
    if game_dir.exists():
        shutil.rmtree(game_dir)
    shutil.copytree(args.game, game_dir)
    (game_dir / "shots").mkdir(exist_ok=True)
    if args.script:
        shutil.copy(args.script, game_dir / "autoplay.txt")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    cmd = [str(emu), str(game_dir.resolve() / "EBOOT.PBP"), "--graphics=software",
           f"--timeout={args.seconds}", f"--screenshot-save={out.resolve() / 'emu_last.png'}",
           f"--memstick={Path(args.memstick).resolve()}"]
    if args.log:
        cmd.append("-l")
    cmd += args.extra
    print("$", " ".join(cmd))
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.time() - t0
    print(f"--- эмулятор завершился с кодом {proc.returncode} за {dt:.1f} с ---")
    if proc.stdout.strip():
        print(proc.stdout.rstrip())
    if proc.stderr.strip():
        print("stderr:", proc.stderr.rstrip()[-4000:])

    converted = 0
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for bmp in sorted((game_dir / "shots").glob("*.bmp")):
        dst = out / (bmp.stem + ".png")
        if Image:
            Image.open(bmp).convert("RGB").save(dst)
        else:
            shutil.copy(bmp, out / bmp.name)
        converted += 1
    print(f"скриншотов игры: {converted} -> {out}/")
    for p in sorted(out.glob("*.png")):
        print("  ", p, p.stat().st_size, "байт")
    for logf in (game_dir / "argus.log",):
        if logf.exists():
            print(f"--- {logf} ---")
            print(logf.read_text(errors="replace")[-3000:])
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
