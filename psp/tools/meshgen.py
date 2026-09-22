#!/usr/bin/env python3
"""meshgen.py — процедурная геометрия объектов ARGUS: assets/meshes/objects.toml → *.msh.

Использование:
    python3 tools/meshgen.py <каталог_вывода> [--spec assets/meshes/objects.toml] [--check]

Для каждого объекта пишется <каталог>/mesh_<имя>.msh (формат AMSH v1, см. docs/FORMATS.md)
и печатается число треугольников. Запись и примитивы — общий tools/amsh.py, здесь только
построение; все габариты, сегменты и слоты живут в TOML.

--check: независимая перечитка файлов (amsh.read_mesh) и проверка того, что
  * записанная нормаль каждой грани совпадает с геометрической нормалью её обхода;
  * каждая замкнутая часть меша ориентирована согласованно (у любого ребра ровно одна пара)
    и имеет положительный объём, то есть обход граней против часовой стрелки снаружи;
  * bbox совпадает с заявленным в expect, а число треугольников — в бюджете budget.

Типы частей (part.type) и параметры (в скобках — значение по умолчанию):
  box          center, size, slot, slot_side(slot), pitch_deg(0), yaw_deg(0),
               ao_top(255), ao_side(228), ao_bottom(175)
  chamfer_box  center, size, chamfer, slot, slot_side(slot), ao_top, ao_side, ao_bottom
  prism        center (центр нижнего основания), radius, radius_top(radius), height, sides,
               phase_deg(0), slot, slot_side(slot), ao_top, ao_side, ao_bottom
  sphere       center (центр), radius, segs, rings, squash(1), slot, ao_top, ao_bottom
  lens         center (центр), face (ось лица), along (ось длины), r_len, r_wid, thick, segs,
               slot, slot_tip(slot), ao(255), ao_tip(ao)
  cyl          from, to, radius, sides, phase_deg(0), slot, ao_side(228), ao_cap(250)
  ring         center, face, r_inner, r_outer, thick, segs, slot, ao_face(250), ao_wall(225)
  gem          center (низ), height, radius, sides, rings [[доля высоты, доля радиуса], …],
               phase_deg(0), slot, ao_top(255), ao_bottom(185)
  fan          center (ступица), count, arc_deg, inner_r, length, half_wid, thick, segs,
               bow(0), slot, slot_tip(slot), ao, ao_tip
Оси задаются строкой "x"/"-x"/"y"/"-y"/"z"/"-z" или вектором [x, y, z].
"""
import argparse
import math
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from amsh import MeshBuilder, read_mesh  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SPEC = ROOT / "assets" / "meshes" / "objects.toml"
DEFAULT_BUDGET = 400
DEFAULT_TOL = 0.12

AXES = {
    "x": (1.0, 0.0, 0.0), "-x": (-1.0, 0.0, 0.0),
    "y": (0.0, 1.0, 0.0), "-y": (0.0, -1.0, 0.0),
    "z": (0.0, 0.0, 1.0), "-z": (0.0, 0.0, -1.0),
}


# --- вектора ---

def v_add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_mul(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def v_dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def v_norm(a):
    d = math.sqrt(v_dot(a, a))
    if d < 1e-12:
        raise ValueError("нулевой вектор")
    return (a[0] / d, a[1] / d, a[2] / d)


def axis(value, default=(0.0, 0.0, 1.0)):
    """Ось из строки ("z", "-x") или вектора."""
    if value is None:
        return default
    if isinstance(value, str):
        key = value.strip().lower()
        if key not in AXES:
            raise SystemExit(f"meshgen: неизвестная ось {value!r}")
        return AXES[key]
    return v_norm(tuple(float(x) for x in value))


def plane_basis(w):
    """Правая тройка (u, v, w): w — заданная ось, обход u→v виден против часовой стрелки с +w."""
    w = v_norm(w)
    up = (0.0, 1.0, 0.0)
    if abs(v_dot(w, up)) > 0.95:
        up = (0.0, 0.0, -1.0)
    u = v_norm(v_cross(up, w))
    return u, v_cross(w, u), w


def frame_axes(pitch_deg, yaw_deg):
    """Оси локальной системы: наклон вокруг X, затем поворот вокруг Y (как в amsh)."""
    p, q = math.radians(pitch_deg), math.radians(yaw_deg)
    sp, cp, sq, cq = math.sin(p), math.cos(p), math.sin(q), math.cos(q)
    return (cq, 0.0, -sq), (sp * sq, cp, sp * cq), (cp * sq, -sp, cp * cq)


# --- чтение параметров ---

def cfg_vec(c, key, default=(0.0, 0.0, 0.0), y_off=0.0):
    raw = c.get(key, default)
    p = tuple(float(x) for x in raw)
    return (p[0], p[1] + y_off, p[2])


def cfg_num(c, key, default=None):
    if key not in c:
        if default is None:
            raise SystemExit(f"meshgen: не задан параметр {key!r}")
        return float(default)
    return float(c[key])


def cfg_int(c, key, default=None):
    if key not in c:
        if default is None:
            raise SystemExit(f"meshgen: не задан параметр {key!r}")
        return int(default)
    return int(c[key])


def cfg_slot(c, key="slot"):
    if key not in c:
        raise SystemExit(f"meshgen: не задан слот {key!r}")
    return int(c[key])


def cfg_ao3(c):
    return cfg_int(c, "ao_top", 255), cfg_int(c, "ao_side", 228), cfg_int(c, "ao_bottom", 175)


# --- построители частей ---

def part_box(b, c, y_off):
    center = cfg_vec(c, "center", y_off=y_off)
    size = cfg_vec(c, "size", (1.0, 1.0, 1.0))
    slot = cfg_slot(c)
    side = int(c.get("slot_side", slot))
    ao_top, ao_side, ao_bottom = cfg_ao3(c)
    pitch, yaw = cfg_num(c, "pitch_deg", 0.0), cfg_num(c, "yaw_deg", 0.0)
    if pitch == 0.0 and yaw == 0.0:
        b.box(center, size, slot, ao_top, ao_side, ao_bottom, side)
        return
    ex, ey, ez = frame_axes(pitch, yaw)
    hx, hy, hz = size[0] / 2.0, size[1] / 2.0, size[2] / 2.0

    def pt(sx, sy, sz):
        return v_add(center, v_add(v_add(v_mul(ex, sx * hx), v_mul(ey, sy * hy)), v_mul(ez, sz * hz)))

    ao_wall = (ao_side, ao_side, ao_bottom, ao_bottom)
    b.quad(pt(-1, 1, -1), pt(1, 1, -1), pt(1, 1, 1), pt(-1, 1, 1), slot, ao_top, ey)
    b.quad(pt(-1, -1, 1), pt(1, -1, 1), pt(1, -1, -1), pt(-1, -1, -1), side, ao_bottom, v_mul(ey, -1))
    b.quad(pt(-1, 1, 1), pt(1, 1, 1), pt(1, -1, 1), pt(-1, -1, 1), side, ao_wall, ez)
    b.quad(pt(1, 1, -1), pt(-1, 1, -1), pt(-1, -1, -1), pt(1, -1, -1), side, ao_wall, v_mul(ez, -1))
    b.quad(pt(1, 1, 1), pt(1, 1, -1), pt(1, -1, -1), pt(1, -1, 1), side, ao_wall, ex)
    b.quad(pt(-1, 1, -1), pt(-1, 1, 1), pt(-1, -1, 1), pt(-1, -1, -1), side, ao_wall, v_mul(ex, -1))


def part_chamfer_box(b, c, y_off):
    """Бокс со скошенными верхними рёбрами: низ, четыре стенки, четыре фаски (45°), верх."""
    cx, cy, cz = cfg_vec(c, "center", y_off=y_off)
    sx, sy, sz = cfg_vec(c, "size", (1.0, 1.0, 1.0))
    ch = cfg_num(c, "chamfer", 0.0)
    slot = cfg_slot(c)
    side = int(c.get("slot_side", slot))
    ao_top, ao_side, ao_bottom = cfg_ao3(c)
    hx, hz = sx / 2.0, sz / 2.0
    y0, y1 = cy - sy / 2.0, cy + sy / 2.0
    ym = y1 - ch  # уровень, с которого начинается фаска
    b.quad((cx - hx, y0, cz + hz), (cx + hx, y0, cz + hz), (cx + hx, y0, cz - hz),
           (cx - hx, y0, cz - hz), side, ao_bottom, (0, -1, 0))
    b.quad((cx - hx + ch, y1, cz - hz + ch), (cx + hx - ch, y1, cz - hz + ch),
           (cx + hx - ch, y1, cz + hz - ch), (cx - hx + ch, y1, cz + hz - ch),
           slot, ao_top, (0, 1, 0))
    for n in ((0.0, 0.0, 1.0), (1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (-1.0, 0.0, 0.0)):
        t = (n[2], 0.0, -n[0])            # касательная вдоль грани
        hn = hz if n[2] else hx           # вынос грани по нормали
        ht = hx if n[2] else hz           # полуширина грани
        edge, low, top = [], [], []
        for s in (1.0, -1.0):
            edge.append((cx + n[0] * hn + t[0] * s * ht, ym, cz + n[2] * hn + t[2] * s * ht))
            low.append((cx + n[0] * hn + t[0] * s * ht, y0, cz + n[2] * hn + t[2] * s * ht))
            top.append((cx + n[0] * (hn - ch) + t[0] * s * (ht - ch), y1,
                        cz + n[2] * (hn - ch) + t[2] * s * (ht - ch)))
        b.quad(edge[0], edge[1], low[1], low[0], side,
               (ao_side, ao_side, ao_bottom, ao_bottom), n)
        b.quad(edge[1], edge[0], top[0], top[1], slot,
               (ao_side, ao_side, ao_top, ao_top), v_norm(v_add(n, (0.0, 1.0, 0.0))))


def part_prism(b, c, y_off):
    radius = cfg_num(c, "radius")
    slot = cfg_slot(c)
    ao_top, ao_side, ao_bottom = cfg_ao3(c)
    b.prism(cfg_vec(c, "center", y_off=y_off), radius, cfg_num(c, "height"), cfg_int(c, "sides"),
            slot, ao_top, ao_side, ao_bottom, int(c.get("slot_side", slot)), True, True,
            cfg_num(c, "radius_top", radius), cfg_num(c, "phase_deg", 0.0))


def part_sphere(b, c, y_off):
    b.sphere(cfg_vec(c, "center", y_off=y_off), cfg_num(c, "radius"), cfg_int(c, "segs"),
             cfg_int(c, "rings"), cfg_slot(c), cfg_int(c, "ao_top", 255),
             cfg_int(c, "ao_bottom", 185), cfg_num(c, "squash", 1.0))


def emit_lens(b, center, face, along, r_tip, r_root, r_wid, half_thick, segs, slot, slot_tip,
              ao, ao_tip):
    """Линза: кольцо из segs точек и две вершины по оси лица. Кончик — со стороны +along.
    r_tip ≠ r_root даёт профиль пера: длинный сбег к корню и тупой «глаз» у кончика."""
    w = v_norm(face)
    u = v_norm(v_sub(along, v_mul(w, v_dot(along, w))))
    v = v_cross(w, u)
    ring = []
    for i in range(segs):
        a = 2.0 * math.pi * i / segs
        c = math.cos(a)
        ring.append(v_add(center, v_add(v_mul(u, (r_tip if c >= 0.0 else r_root) * c),
                                        v_mul(v, r_wid * math.sin(a)))))
    front = v_add(center, v_mul(w, half_thick))
    back = v_sub(center, v_mul(w, half_thick))
    for i in range(segs):
        j = (i + 1) % segs
        tip = i == 0 or j == 0
        s = slot_tip if tip else slot
        k = ao_tip if tip else ao
        b.tri(front, ring[i], ring[j], s, k)
        b.tri(back, ring[j], ring[i], s, k)


def part_lens(b, c, y_off):
    slot = cfg_slot(c)
    ao = cfg_int(c, "ao", 255)
    r_len = cfg_num(c, "r_len")
    emit_lens(b, cfg_vec(c, "center", y_off=y_off), axis(c.get("face")), axis(c.get("along"), (0, 1, 0)),
              cfg_num(c, "r_tip", r_len), cfg_num(c, "r_root", r_len), cfg_num(c, "r_wid", r_len),
              cfg_num(c, "thick") / 2.0, cfg_int(c, "segs"), slot, int(c.get("slot_tip", slot)),
              ao, cfg_int(c, "ao_tip", ao))


def part_cyl(b, c, y_off):
    p0 = cfg_vec(c, "from", y_off=y_off)
    p1 = cfg_vec(c, "to", y_off=y_off)
    radius, sides = cfg_num(c, "radius"), cfg_int(c, "sides")
    slot = cfg_slot(c)
    ao_side, ao_cap = cfg_int(c, "ao_side", 228), cfg_int(c, "ao_cap", 250)
    phase = math.radians(cfg_num(c, "phase_deg", 0.0))
    u, v, w = plane_basis(v_sub(p1, p0))
    lo, hi = [], []
    for i in range(sides):
        a = phase + 2.0 * math.pi * i / sides
        off = v_add(v_mul(u, radius * math.cos(a)), v_mul(v, radius * math.sin(a)))
        lo.append(v_add(p0, off))
        hi.append(v_add(p1, off))
    for i in range(sides):
        j = (i + 1) % sides
        a = phase + 2.0 * math.pi * (i + 0.5) / sides
        n = v_add(v_mul(u, math.cos(a)), v_mul(v, math.sin(a)))
        b.quad(hi[i], hi[j], lo[j], lo[i], slot, ao_side, n)
    for i in range(1, sides - 1):
        b.tri(hi[0], hi[i], hi[i + 1], slot, ao_cap, w)
        b.tri(lo[0], lo[i], lo[i + 1], slot, ao_cap, v_mul(w, -1))


def part_ring(b, c, y_off):
    center = cfg_vec(c, "center", y_off=y_off)
    r_in, r_out = cfg_num(c, "r_inner"), cfg_num(c, "r_outer")
    half = cfg_num(c, "thick") / 2.0
    segs = cfg_int(c, "segs")
    slot = cfg_slot(c)
    ao_face, ao_wall = cfg_int(c, "ao_face", 250), cfg_int(c, "ao_wall", 225)
    u, v, w = plane_basis(axis(c.get("face")))
    fr, bk = v_mul(w, half), v_mul(w, -half)
    ring = []
    for i in range(segs):
        a = 2.0 * math.pi * i / segs
        d = v_add(v_mul(u, math.cos(a)), v_mul(v, math.sin(a)))
        ring.append((v_add(center, v_mul(d, r_out)), v_add(center, v_mul(d, r_in))))
    for i in range(segs):
        j = (i + 1) % segs
        (o_i, i_i), (o_j, i_j) = ring[i], ring[j]
        a = 2.0 * math.pi * (i + 0.5) / segs
        n = v_add(v_mul(u, math.cos(a)), v_mul(v, math.sin(a)))
        b.quad(v_add(o_i, fr), v_add(o_j, fr), v_add(i_j, fr), v_add(i_i, fr), slot, ao_face, w)
        b.quad(v_add(o_i, bk), v_add(o_j, bk), v_add(i_j, bk), v_add(i_i, bk), slot, ao_face, v_mul(w, -1))
        b.quad(v_add(o_i, fr), v_add(o_j, fr), v_add(o_j, bk), v_add(o_i, bk), slot, ao_wall, n)
        b.quad(v_add(i_i, fr), v_add(i_j, fr), v_add(i_j, bk), v_add(i_i, bk), slot, ao_wall, v_mul(n, -1))


def part_gem(b, c, y_off):
    """Огранённая форма: нижняя вершина, пояса, верхняя вершина. Нормали — по граням."""
    cx, cy, cz = cfg_vec(c, "center", y_off=y_off)
    height, radius = cfg_num(c, "height"), cfg_num(c, "radius")
    sides = cfg_int(c, "sides")
    slot = cfg_slot(c)
    ao_top, ao_bottom = cfg_int(c, "ao_top", 255), cfg_int(c, "ao_bottom", 185)
    phase = math.radians(cfg_num(c, "phase_deg", 0.0))
    rows = sorted((float(yf), float(rf)) for yf, rf in c.get("rings", [[0.5, 1.0]]))
    rings = []
    for yf, rf in rows:
        pts = []
        for i in range(sides):
            a = phase + 2.0 * math.pi * i / sides
            pts.append((cx + radius * rf * math.sin(a), cy + height * yf, cz + radius * rf * math.cos(a)))
        rings.append(pts)
    apex_lo, apex_hi = (cx, cy, cz), (cx, cy + height, cz)

    def ao_at(yf):
        return int(ao_bottom + (ao_top - ao_bottom) * max(0.0, min(1.0, yf)))

    for i in range(sides):
        j = (i + 1) % sides
        b.tri(apex_lo, rings[0][j], rings[0][i], slot, ao_bottom)
        b.tri(apex_hi, rings[-1][i], rings[-1][j], slot, ao_top)
    for r in range(len(rings) - 1):
        lo, hi = rings[r], rings[r + 1]
        ao = (ao_at(rows[r + 1][0]), ao_at(rows[r][0]), ao_at(rows[r][0]), ao_at(rows[r + 1][0]))
        for i in range(sides):
            j = (i + 1) % sides
            b.quad(hi[i], lo[i], lo[j], hi[j], slot, ao)


def part_fan(b, c, y_off):
    """Веер перьев: перо i повёрнуто в плоскости XY на a_i, прогиб bow выносит края в +Z."""
    hub = cfg_vec(c, "center", y_off=y_off)
    count, arc = cfg_int(c, "count"), cfg_num(c, "arc_deg")
    inner, length = cfg_num(c, "inner_r"), cfg_num(c, "length")
    half_wid, thick = cfg_num(c, "half_wid"), cfg_num(c, "thick")
    segs, bow = cfg_int(c, "segs"), cfg_num(c, "bow", 0.0)
    tip = cfg_num(c, "tip_frac", 0.5)  # доля длины от самого широкого места до кончика
    slot = cfg_slot(c)
    ao = cfg_int(c, "ao", 240)
    slot_tip, ao_tip = int(c.get("slot_tip", slot)), cfg_int(c, "ao_tip", ao)
    r_tip, r_root = length * tip, length * (1.0 - tip)
    for i in range(count):
        t = 0.0 if count < 2 else 2.0 * i / (count - 1) - 1.0
        a = math.radians(arc * 0.5 * t)
        d = v_norm((math.sin(a), math.cos(a), bow * t * t))
        side = (math.cos(a), -math.sin(a), 0.0)
        face = v_cross(side, d)
        center = v_add(hub, v_mul(d, inner + r_root))
        emit_lens(b, center, face, d, r_tip, r_root, half_wid, thick / 2.0, segs,
                  slot, slot_tip, ao, ao_tip)


PARTS = {
    "box": part_box, "chamfer_box": part_chamfer_box, "prism": part_prism, "sphere": part_sphere,
    "lens": part_lens, "cyl": part_cyl, "ring": part_ring, "gem": part_gem, "fan": part_fan,
}


# --- сборка объекта ---

def merge_part(raw, shapes):
    """Часть = именованная форма из [shape.*] плюс переопределения на месте."""
    out = {}
    if "use" in raw:
        name = raw["use"]
        if name not in shapes:
            raise SystemExit(f"meshgen: нет формы [shape.{name}]")
        out.update(shapes[name])
    out.update({k: v for k, v in raw.items() if k != "use"})
    return out


def build_object(spec, shapes):
    """Два прохода: второй прижимает низ bbox к lift (если ground не выключен)."""
    parts = [merge_part(p, shapes) for p in spec.get("part", [])]
    if not parts:
        raise SystemExit(f"meshgen: у объекта {spec.get('name')!r} нет частей")

    def emit(y_off):
        b = MeshBuilder()
        for c in parts:
            kind = c.get("type")
            if kind not in PARTS:
                raise SystemExit(f"meshgen: неизвестный тип части {kind!r} в {spec.get('name')!r}")
            PARTS[kind](b, c, y_off)
        return b

    mesh = emit(0.0)
    if spec.get("ground", True):
        lo, _ = mesh.bbox()
        mesh = emit(float(spec.get("lift", 0.0)) - lo[1])
    return mesh


# --- самопроверка ---

def tri_normal(a, b, c):
    n = v_cross(v_sub(b, a), v_sub(c, a))
    return n, math.sqrt(v_dot(n, n))


class Union:
    def __init__(self, n):
        self.p = list(range(n))

    def find(self, x):
        while self.p[x] != x:
            self.p[x] = self.p[self.p[x]]
            x = self.p[x]
        return x

    def join(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.p[rb] = ra


def check_mesh(path, spec):
    """Возвращает (список проблем, строка-сводка)."""
    hdr, verts = read_mesh(path)
    bad = []
    tris = hdr["count"] // 3
    pos = [(v[0], v[1], v[2]) for v in verts]
    key = [(round(p[0], 5), round(p[1], 5), round(p[2], 5)) for p in pos]
    ids, uniq = [], {}
    for k in key:
        ids.append(uniq.setdefault(k, len(uniq)))

    # 1. записанная нормаль сонаправлена с геометрической нормалью обхода (обход — снаружи).
    # Полного равенства не требуем: у сбегающих призм amsh пишет горизонтальную радиальную
    # нормаль, и это осознанный выбор общего построителя.
    worst = 0.0
    for t in range(tris):
        i = t * 3
        n, ln = tri_normal(pos[i], pos[i + 1], pos[i + 2])
        if ln < 1e-10:
            bad.append(f"грань {t}: вырожденная")
            continue
        n = v_mul(n, 1.0 / ln)
        for j in range(3):
            s = (verts[i + j][3], verts[i + j][4], verts[i + j][5])
            if abs(v_dot(s, s) - 1.0) > 1e-3:
                bad.append(f"грань {t}: нормаль не единичной длины")
                break
            cos = max(-1.0, min(1.0, v_dot(n, s)))
            if cos < 0.05:
                bad.append(f"грань {t}: нормаль не сонаправлена с обходом (cos {cos:+.3f})")
                break
            worst = max(worst, math.degrees(math.acos(cos)))

    # 2. связные части: согласованная ориентация и положительный объём
    uf = Union(tris)
    owner = {}
    for t in range(tris):
        for j in range(3):
            v = ids[t * 3 + j]
            if v in owner:
                uf.join(owner[v], t)
            else:
                owner[v] = t
    groups = {}
    for t in range(tris):
        groups.setdefault(uf.find(t), []).append(t)
    closed = 0
    for root, group in sorted(groups.items()):
        edges = {}
        for t in group:
            i = t * 3
            tri = (ids[i], ids[i + 1], ids[i + 2])
            for e in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                edges[e] = edges.get(e, 0) + 1
        dup = [e for e, n in edges.items() if n != 1]
        open_e = [e for e in edges if (e[1], e[0]) not in edges]
        if dup:
            bad.append(f"часть {root}: {len(dup)} рёбер повторяются (ориентация не согласована)")
        if open_e:
            bad.append(f"часть {root}: {len(open_e)} рёбер без пары (часть не замкнута)")
        if dup or open_e:
            continue
        closed += 1
        cen = (sum(pos[t * 3][0] for t in group) / len(group),
               sum(pos[t * 3][1] for t in group) / len(group),
               sum(pos[t * 3][2] for t in group) / len(group))
        vol = 0.0
        for t in group:
            i = t * 3
            a, b, c = (v_sub(pos[i + k], cen) for k in range(3))
            vol += v_dot(a, v_cross(b, c)) / 6.0
        if vol <= 1e-9:
            bad.append(f"часть {root}: объём {vol:+.6f} — обход не против часовой снаружи")

    # 3. габариты и бюджет
    lo, hi = hdr["bbox"][:3], hdr["bbox"][3:]
    size = (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2])
    expect = spec.get("expect", {})
    tol = float(expect.get("tol", DEFAULT_TOL))
    for i, k in enumerate(("width", "height", "depth")):
        if k in expect:
            want = float(expect[k])
            if abs(size[i] - want) > max(tol * want, 1e-4):
                bad.append(f"габарит {k}: {size[i]:.3f} вместо {want:.3f} (допуск {tol:.0%})")
    budget = int(spec.get("budget", DEFAULT_BUDGET))
    if tris > budget:
        bad.append(f"бюджет: {tris} треугольников больше {budget}")
    info = (f"{tris:3d} тр./{budget}, частей {len(groups)} (замкнуто {closed}), "
            f"bbox {size[0]:.3f}×{size[1]:.3f}×{size[2]:.3f}, низ y {lo[1]:+.3f}, "
            f"нормаль откл. ≤{worst:.1f}°")
    return bad, info


# --- main ---

def main(argv=None):
    ap = argparse.ArgumentParser(description="Меши объектов ARGUS из assets/meshes/objects.toml")
    ap.add_argument("outdir", help="каталог для mesh_<имя>.msh")
    ap.add_argument("--spec", default=str(DEFAULT_SPEC), help="файл параметров (TOML)")
    ap.add_argument("--check", action="store_true", help="самопроверка обхода, габаритов и бюджета")
    args = ap.parse_args(argv)

    spec_path = Path(args.spec)
    try:
        with spec_path.open("rb") as f:
            data = tomllib.load(f)
    except OSError as e:
        raise SystemExit(f"meshgen: не читается {spec_path}: {e}")
    except tomllib.TOMLDecodeError as e:
        raise SystemExit(f"meshgen: ошибка в {spec_path}: {e}")
    shapes = data.get("shape", {})
    objects = data.get("object", [])
    if not objects:
        raise SystemExit(f"meshgen: в {spec_path} нет объектов")

    out = Path(args.outdir)
    out.mkdir(parents=True, exist_ok=True)
    total, problems = 0, 0
    for spec in objects:
        name = spec.get("name")
        if not name:
            raise SystemExit("meshgen: у объекта нет имени")
        mesh = build_object(spec, shapes)
        path = out / f"mesh_{name}.msh"
        count = mesh.write(path)
        tris = count // 3
        total += tris
        if args.check:
            bad, info = check_mesh(path, spec)
            print(f"{name:16s} {info}")
            for line in bad:
                print(f"{'':16s}   ! {line}")
            problems += len(bad)
        else:
            print(f"{name:16s} {tris:3d} тр.")
    print(f"итого {len(objects)} мешей, {total} треугольников → {out}")
    if args.check:
        print("самопроверка: замечаний нет" if problems == 0 else f"самопроверка: замечаний {problems}")
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
