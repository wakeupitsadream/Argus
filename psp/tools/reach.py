#!/usr/bin/env python3
"""reach.py — проверка достижимости: обход острова по тем же правилам, что core/walk.c.

Запуск: python3 tools/reach.py levels/terraces_1.toml [...]

Считает, до каких клеток и сущностей Око дойдёт от точки входа, и печатает тех,
кто остался за бортом. Доказывает не решаемость головоломки (двери и сегменты
меняют проходимость), а базовое: остров не разваливается на несвязные куски и
до рычагов есть дорога. Именно это ломается чаще всего — уступ в полшага,
на который нельзя забраться, выглядит в TOML как обычная клетка.
"""
import sys
from collections import deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import levelc  # noqa: E402

# Декор и плавучие блоки стоят вне досягаемости по замыслу: постамент с хвостом —
# памятник, а плот приплывает сам, когда поднимется вода.
DECOR = {"PLINTH", "PEACOCK_TAIL", "PEACOCK_FEATHER", "FLOAT_BLOCK"}

STEP_LIMIT = 0.55  # PLAYER_STEP_MAX в core/player.c: одна ступень берётся, две — нет


def walk_graph(g):
    """Множество проходимых клеток и переходы между ними."""
    ok = set()
    for z in range(g.h):
        for x in range(g.w):
            if g.walkable(x, z):
                ok.add((x, z))
    return ok


def edge_gap(g, x, z, dx, dz):
    """Разница высот на общем ребре двух клеток; None — соседа нет."""
    nx, nz = x + dx, z + dz
    if not g.exists(nx, nz):
        return None
    side = {(1, 0): "e", (-1, 0): "w", (0, 1): "s", (0, -1): "n"}[(dx, dz)]
    opp = {"e": "w", "w": "e", "s": "n", "n": "s"}[side]
    a = g.edge(x, z, side)
    b = g.edge(nx, nz, opp)
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def reachable(g, start):
    ok = walk_graph(g)
    if start not in ok:
        return set(), ok
    seen = {start}
    q = deque([start])
    while q:
        x, z = q.popleft()
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, z + dz)
            if n in seen or n not in ok:
                continue
            gap = edge_gap(g, x, z, dx, dz)
            if gap is None or gap > STEP_LIMIT + 1e-6:
                continue
            seen.add(n)
            q.append(n)
    return seen, ok


def check(path):
    import tomllib
    level = tomllib.loads(Path(path).read_text(encoding="utf-8"))
    where = Path(path).name
    g = levelc.Grid(level, where)
    spawn = level.get("spawn", [g.w / 2.0, g.h / 2.0])
    start = (int(spawn[0] // 1), int(spawn[1] // 1))
    seen, ok = reachable(g, start)
    lost_cells = sorted(ok - seen)
    bad = []
    for e in level.get("entity", []):
        cell = e.get("cell")
        if not cell or e.get("type") in DECOR:
            continue
        cx, cz = int(cell[0] // 1), int(cell[1] // 1)
        # до сущности достаточно дойти до соседней клетки: рука дотягивается
        near = [(cx, cz)] + [(cx + dx, cz + dz) for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1))]
        if not any(c in seen for c in near):
            bad.append((e.get("type"), e.get("id"), cx, cz))
    print(f"{where}: достижимо {len(seen)} из {len(ok)} проходимых клеток")
    if lost_cells:
        print(f"  недостижимых клеток: {len(lost_cells)}, например {lost_cells[:8]}")
    for t, i, cx, cz in bad:
        print(f"  НЕ ДОЙТИ: {t} id={i} в клетке ({cx}, {cz})")
    return 1 if bad else 0


if __name__ == "__main__":
    rc = 0
    for arg in sys.argv[1:] or sorted(str(p) for p in Path("levels").glob("*.toml")):
        rc |= check(arg)
    sys.exit(rc)
