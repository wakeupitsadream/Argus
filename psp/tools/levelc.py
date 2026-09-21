#!/usr/bin/env python3
"""levelc.py — компилирует остров из levels/*.toml в бинарный меш .msh (формат AMSH v1).

Использование: python3 tools/levelc.py levels/hub.toml build/assets/hub.msh

Формат .msh (little-endian):
  заголовок 64 байта: 4s magic "AMSH", u32 version=1, u32 format=1 (static),
                      u32 vertex_count, f32 bbox_min[3], f32 bbox_max[3], padding
  вершины: N × { f32 x,y,z; f32 nx,ny,nz; u8 slot; u8 ao; u16 pad } = 28 байт
Цвета в файле нет — только слот палитры и AO; RGB резолвится при загрузке.
Обход граней — против часовой стрелки при взгляде снаружи (правило правой руки).
"""
import struct
import sys
import tomllib
from pathlib import Path

MAGIC = b"AMSH"
VERSION = 1
FMT_STATIC = 1
HEADER_SIZE = 64
VERTEX_FMT = "<3f3fBBH"

SLOT_TOP, SLOT_TOP_ALT, SLOT_WALL, SLOT_ACCENT, SLOT_SHADOW = 0, 1, 2, 3, 4

AO_TOP_STEP = 38      # затемнение угла за каждого соседа выше
AO_TOP_MIN = 110
AO_WALL_TOP = 255
AO_WALL_BOTTOM = 140  # низ стены, уходящей в пустоту
AO_WALL_STEP_BOTTOM = 190  # низ невысокой ступени


def parse_height(ch):
    if ch in ". ":
        return None
    if ch.isdigit():
        return int(ch)
    if "a" <= ch <= "f":
        return 10 + ord(ch) - ord("a")
    raise ValueError(f"недопустимый символ карты: {ch!r}")


class MeshBuilder:
    def __init__(self):
        self.verts = []  # (x,y,z, nx,ny,nz, slot, ao)

    def tri(self, a, b, c, normal, slot, ao):
        """a,b,c — (x,y,z); ao — кортеж из трёх значений. Порядок исправляется под normal."""
        ax, ay, az = a
        bx, by, bz = b
        cx, cy, cz = c
        ux, uy, uz = bx - ax, by - ay, bz - az
        vx, vy, vz = cx - ax, cy - ay, cz - az
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        if nx * normal[0] + ny * normal[1] + nz * normal[2] < 0:
            b, c = c, b
            ao = (ao[0], ao[2], ao[1])
        for p, k in zip((a, b, c), ao):
            self.verts.append((*p, *normal, slot, k))

    def quad(self, p0, p1, p2, p3, normal, slot, ao):
        """Четыре точки по контуру, ao — четыре значения."""
        self.tri(p0, p1, p2, normal, slot, (ao[0], ao[1], ao[2]))
        self.tri(p0, p2, p3, normal, slot, (ao[0], ao[2], ao[3]))

    def write(self, path):
        xs = [v[0] for v in self.verts] or [0.0]
        ys = [v[1] for v in self.verts] or [0.0]
        zs = [v[2] for v in self.verts] or [0.0]
        header = struct.pack("<4sIII6f", MAGIC, VERSION, FMT_STATIC, len(self.verts),
                             min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))
        header += b"\0" * (HEADER_SIZE - len(header))
        body = b"".join(struct.pack(VERTEX_FMT, *v[:6], v[6], v[7], 0) for v in self.verts)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_bytes(header + body)


def build_island(level):
    rows = [r for r in level["map"].strip("\n").splitlines()]
    width = max(len(r) for r in rows)
    rows = [r.ljust(width, ".") for r in rows]
    height = len(rows)
    cell = float(level.get("cell", 1.0))
    step = float(level.get("step", 0.5))
    base_y = -float(level.get("base_depth", 3.0))
    grid = [[parse_height(ch) for ch in r] for r in rows]

    def h(r, c):
        if 0 <= r < height and 0 <= c < width:
            return grid[r][c]
        return None

    ox, oz = -width * cell / 2.0, -height * cell / 2.0
    mb = MeshBuilder()
    for r in range(height):
        for c in range(width):
            hc = grid[r][c]
            if hc is None:
                continue
            y = hc * step
            x0, x1 = ox + c * cell, ox + (c + 1) * cell
            z0, z1 = oz + r * cell, oz + (r + 1) * cell

            # AO углов верхней грани: соседи выше вокруг каждого угла
            def corner_ao(dr, dc):
                higher = 0
                for rr, cc in ((r + dr, c), (r, c + dc), (r + dr, c + dc)):
                    n = h(rr, cc)
                    if n is not None and n > hc:
                        higher += 1
                return max(AO_TOP_MIN, 255 - AO_TOP_STEP * higher)

            ao = (corner_ao(-1, -1), corner_ao(-1, 1), corner_ao(1, 1), corner_ao(1, -1))
            mb.quad((x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1), (0, 1, 0), SLOT_TOP, ao)

            # Стены к соседям ниже или в пустоту
            for (dr, dc, normal, pa, pb) in (
                (0, 1, (1, 0, 0), (x1, z0), (x1, z1)),    # восток
                (0, -1, (-1, 0, 0), (x0, z1), (x0, z0)),  # запад
                (1, 0, (0, 0, 1), (x1, z1), (x0, z1)),    # юг
                (-1, 0, (0, 0, -1), (x0, z0), (x1, z0)),  # север
            ):
                n = h(r + dr, c + dc)
                if n is None:
                    yb, ao_b = base_y, AO_WALL_BOTTOM
                elif n < hc:
                    yb, ao_b = n * step, AO_WALL_STEP_BOTTOM
                else:
                    continue
                (ax, az), (bx, bz) = pa, pb
                mb.quad((ax, y, az), (bx, y, bz), (bx, yb, bz), (ax, yb, az), normal, SLOT_WALL,
                        (AO_WALL_TOP, AO_WALL_TOP, ao_b, ao_b))
    return mb


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    level = tomllib.loads(src.read_text(encoding="utf-8"))
    mb = build_island(level)
    mb.write(dst)
    print(f"{src.name}: {len(mb.verts)} вершин, {len(mb.verts) // 3} треугольников -> {dst}")


if __name__ == "__main__":
    main()
