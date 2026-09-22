#!/usr/bin/env python3
"""amsh.py — построитель мешей и запись формата AMSH v1 (см. docs/FORMATS.md).

Общий модуль для tools/levelc.py (геометрия островов) и tools/meshgen.py (примитивы объектов).
Вершины хранят слот палитры и запечённый AO, цвета считаются в игре при загрузке.
Обход граней — против часовой стрелки при взгляде снаружи (рендер включает GU_CCW).

Слоты палитры (индексы в palettes.toml):
  0 верх, 1 верх-вариант, 2 стены, 3 акцент, 4 тень, 5 свечение, 6 вода, 7 запас.
"""
import math
import struct
from pathlib import Path

MAGIC = b"AMSH"
VERSION = 1
FMT_STATIC = 1
HEADER_SIZE = 64
VERTEX_FMT = "<3f3fBBH"
VERTEX_SIZE = struct.calcsize(VERTEX_FMT)
assert VERTEX_SIZE == 28, VERTEX_SIZE

SLOT_TOP, SLOT_TOP_ALT, SLOT_WALL, SLOT_ACCENT, SLOT_SHADOW, SLOT_GLOW, SLOT_WATER, SLOT_EXTRA = range(8)


def _norm(v):
    length = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    if length < 1e-9:
        return (0.0, 1.0, 0.0)
    return (v[0] / length, v[1] / length, v[2] / length)


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class MeshBuilder:
    """Накапливает треугольники. Поддерживает стек преобразований (сдвиг, поворот вокруг Y, масштаб)."""

    def __init__(self):
        self.verts = []  # (x, y, z, nx, ny, nz, slot, ao)
        self._stack = [(0.0, 0.0, 0.0, 0.0, 1.0)]  # tx, ty, tz, yaw_deg, scale

    # --- преобразования ---

    def push(self, translate=(0.0, 0.0, 0.0), yaw_deg=0.0, scale=1.0):
        tx, ty, tz, yaw, sc = self._stack[-1]
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        dx, dy, dz = translate
        # локальный сдвиг поворачивается и масштабируется текущим преобразованием
        wx = tx + sc * (dx * c + dz * s)
        wy = ty + sc * dy
        wz = tz + sc * (-dx * s + dz * c)
        self._stack.append((wx, wy, wz, yaw + yaw_deg, sc * scale))
        return self

    def pop(self):
        if len(self._stack) > 1:
            self._stack.pop()
        return self

    def _apply_point(self, p):
        tx, ty, tz, yaw, sc = self._stack[-1]
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        x, y, z = p[0] * sc, p[1] * sc, p[2] * sc
        return (tx + x * c + z * s, ty + y, tz - x * s + z * c)

    def _apply_dir(self, d):
        _, _, _, yaw, _ = self._stack[-1]
        c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        return _norm((d[0] * c + d[2] * s, d[1], -d[0] * s + d[2] * c))

    # --- базовые грани ---

    def tri(self, a, b, c, slot, ao=(255, 255, 255), normal=None):
        """Треугольник; порядок вершин исправляется так, чтобы нормаль совпала с normal
        (по умолчанию — геометрическая нормаль обхода a→b→c)."""
        if isinstance(ao, int):
            ao = (ao, ao, ao)
        wa, wb, wc = self._apply_point(a), self._apply_point(b), self._apply_point(c)
        geo = _cross(_sub(wb, wa), _sub(wc, wa))
        n = self._apply_dir(normal) if normal else _norm(geo)
        if _dot(geo, n) < 0.0:
            wb, wc = wc, wb
            ao = (ao[0], ao[2], ao[1])
        for p, k in zip((wa, wb, wc), ao):
            self.verts.append((p[0], p[1], p[2], n[0], n[1], n[2], int(slot), int(k)))

    def quad(self, p0, p1, p2, p3, slot, ao=(255, 255, 255, 255), normal=None):
        """Четырёхугольник по контуру p0→p1→p2→p3."""
        if isinstance(ao, int):
            ao = (ao,) * 4
        self.tri(p0, p1, p2, slot, (ao[0], ao[1], ao[2]), normal)
        self.tri(p0, p2, p3, slot, (ao[0], ao[2], ao[3]), normal)

    # --- примитивы ---

    def box(self, center, size, slot, ao_top=255, ao_side=225, ao_bottom=170, slot_side=None):
        """Параллелепипед. center — центр, size — полные размеры (sx, sy, sz)."""
        cx, cy, cz = center
        hx, hy, hz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0
        x0, x1 = cx - hx, cx + hx
        y0, y1 = cy - hy, cy + hy
        z0, z1 = cz - hz, cz + hz
        side = slot if slot_side is None else slot_side
        self.quad((x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1), slot, ao_top, (0, 1, 0))
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0), side, ao_bottom, (0, -1, 0))
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y0, z1), (x0, y0, z1), side, (ao_side, ao_side, ao_bottom, ao_bottom), (0, 0, 1))
        self.quad((x1, y1, z0), (x0, y1, z0), (x0, y0, z0), (x1, y0, z0), side, (ao_side, ao_side, ao_bottom, ao_bottom), (0, 0, -1))
        self.quad((x1, y1, z1), (x1, y1, z0), (x1, y0, z0), (x1, y0, z1), side, (ao_side, ao_side, ao_bottom, ao_bottom), (1, 0, 0))
        self.quad((x0, y1, z0), (x0, y1, z1), (x0, y0, z1), (x0, y0, z0), side, (ao_side, ao_side, ao_bottom, ao_bottom), (-1, 0, 0))

    def prism(self, center, radius, height, sides, slot, ao_top=255, ao_side=220, ao_bottom=165,
              slot_side=None, cap_top=True, cap_bottom=True, radius_top=None, phase_deg=0.0):
        """Вертикальная N-угольная призма (цилиндр при больших sides, конус при radius_top=0).
        center — центр нижнего основания."""
        cx, cy, cz = center
        rt = radius if radius_top is None else radius_top
        side = slot if slot_side is None else slot_side
        ring_b, ring_t = [], []
        for i in range(sides):
            a = math.radians(phase_deg) + 2.0 * math.pi * i / sides
            ring_b.append((cx + radius * math.cos(a), cy, cz + radius * math.sin(a)))
            ring_t.append((cx + rt * math.cos(a), cy + height, cz + rt * math.sin(a)))
        for i in range(sides):
            j = (i + 1) % sides
            nx = (ring_b[i][0] + ring_b[j][0]) / 2.0 - cx
            nz = (ring_b[i][2] + ring_b[j][2]) / 2.0 - cz
            n = _norm((nx, 0.0, nz))
            if rt <= 1e-6:
                self.tri(ring_b[i], ring_b[j], ring_t[i], side, (ao_bottom, ao_bottom, ao_top), n)
            else:
                self.quad(ring_t[i], ring_t[j], ring_b[j], ring_b[i], side,
                          (ao_side, ao_side, ao_bottom, ao_bottom), n)
        if cap_top and rt > 1e-6:
            for i in range(1, sides - 1):
                self.tri(ring_t[0], ring_t[i], ring_t[i + 1], slot, ao_top, (0, 1, 0))
        if cap_bottom:
            for i in range(1, sides - 1):
                self.tri(ring_b[0], ring_b[i + 1], ring_b[i], side, ao_bottom, (0, -1, 0))

    def disc(self, center, radius, sides, slot, ao=255, up=True, phase_deg=0.0):
        """Плоский диск в плоскости XZ."""
        cx, cy, cz = center
        n = (0, 1, 0) if up else (0, -1, 0)
        ring = []
        for i in range(sides):
            a = math.radians(phase_deg) + 2.0 * math.pi * i / sides
            ring.append((cx + radius * math.cos(a), cy, cz + radius * math.sin(a)))
        for i in range(1, sides - 1):
            self.tri(ring[0], ring[i], ring[i + 1], slot, ao, n)

    def sphere(self, center, radius, segs, rings, slot, ao_top=255, ao_bottom=180, squash=1.0):
        """Сфера (squash < 1 — приплюснутая). Нормали — от центра, заливка получается плоской
        по граням, потому что цвет всё равно считается по нормали грани."""
        cx, cy, cz = center
        grid = []
        for r in range(rings + 1):
            phi = math.pi * r / rings
            y = math.cos(phi) * radius * squash
            rad = math.sin(phi) * radius
            row = []
            for s in range(segs):
                th = 2.0 * math.pi * s / segs
                row.append((cx + rad * math.cos(th), cy + y, cz + rad * math.sin(th)))
            grid.append(row)
        for r in range(rings):
            for s in range(segs):
                s2 = (s + 1) % segs
                a, b = grid[r][s], grid[r][s2]
                c, d = grid[r + 1][s2], grid[r + 1][s]
                t = 1.0 - r / float(rings)
                ao = int(ao_bottom + (ao_top - ao_bottom) * t)
                if r == 0:
                    self.tri(a, c, d, slot, ao)
                elif r == rings - 1:
                    self.tri(a, b, c, slot, ao)
                else:
                    self.quad(a, b, c, d, slot, ao)

    def tube(self, center, r_outer, r_inner, height, sides, slot, ao_top=255, ao_side=210):
        """Кольцо-труба: внешняя и внутренняя стенки плюс верхнее кольцо."""
        cx, cy, cz = center
        for i in range(sides):
            a0 = 2.0 * math.pi * i / sides
            a1 = 2.0 * math.pi * (i + 1) / sides
            o0 = (cx + r_outer * math.cos(a0), cz + r_outer * math.sin(a0))
            o1 = (cx + r_outer * math.cos(a1), cz + r_outer * math.sin(a1))
            i0 = (cx + r_inner * math.cos(a0), cz + r_inner * math.sin(a0))
            i1 = (cx + r_inner * math.cos(a1), cz + r_inner * math.sin(a1))
            self.quad((o0[0], cy + height, o0[1]), (o1[0], cy + height, o1[1]),
                      (o1[0], cy, o1[1]), (o0[0], cy, o0[1]), slot, ao_side,
                      _norm((math.cos((a0 + a1) / 2), 0.0, math.sin((a0 + a1) / 2))))
            self.quad((i1[0], cy + height, i1[1]), (i0[0], cy + height, i0[1]),
                      (i0[0], cy, i0[1]), (i1[0], cy, i1[1]), slot, ao_side,
                      _norm((-math.cos((a0 + a1) / 2), 0.0, -math.sin((a0 + a1) / 2))))
            self.quad((i0[0], cy + height, i0[1]), (i1[0], cy + height, i1[1]),
                      (o1[0], cy + height, o1[1]), (o0[0], cy + height, o0[1]), slot, ao_top, (0, 1, 0))

    # --- вывод ---

    def bbox(self):
        if not self.verts:
            return (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)
        xs = [v[0] for v in self.verts]
        ys = [v[1] for v in self.verts]
        zs = [v[2] for v in self.verts]
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))

    def __len__(self):
        return len(self.verts)

    def write(self, path):
        lo, hi = self.bbox()
        header = struct.pack("<4sIII6f", MAGIC, VERSION, FMT_STATIC, len(self.verts),
                             lo[0], lo[1], lo[2], hi[0], hi[1], hi[2])
        header += b"\0" * (HEADER_SIZE - len(header))
        body = b"".join(struct.pack(VERTEX_FMT, *v[:6], v[6], v[7], 0) for v in self.verts)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_bytes(header + body)
        return len(self.verts)


def read_mesh(path):
    """Независимый читатель для самопроверок: возвращает (заголовок, список вершин)."""
    data = Path(path).read_bytes()
    magic, version, fmt, count = struct.unpack_from("<4sIII", data, 0)
    bbox = struct.unpack_from("<6f", data, 16)
    assert magic == MAGIC and version == VERSION and fmt == FMT_STATIC, (magic, version, fmt)
    assert len(data) == HEADER_SIZE + count * VERTEX_SIZE, (len(data), count)
    verts = [struct.unpack_from(VERTEX_FMT, data, HEADER_SIZE + i * VERTEX_SIZE) for i in range(count)]
    return {"count": count, "bbox": bbox}, verts
