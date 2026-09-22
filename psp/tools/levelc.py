#!/usr/bin/env python3
"""levelc.py — компилятор уровней: levels/*.toml → <имя>.msh (AMSH) + <имя>.lvl (ALVL).

Использование:
  python3 tools/levelc.py levels/hub.toml build/assets   # геометрия острова + данные уровня
  python3 tools/levelc.py --headers build/assets         # entity_types.h и level_ids.h
  python3 tools/levelc.py --verify build/assets/hub.lvl  # независимая проверка формата

Форматы описаны в docs/FORMATS.md (разделы AMSH, ALVL, «Исходник уровня»).
Геометрия строится общим построителем tools/amsh.py; цвета в файл не попадают —
только слот палитры и запечённый AO.
"""
import argparse
import csv
import math
import re
import struct
import sys
from collections import Counter
from pathlib import Path

if sys.version_info < (3, 11):  # tomllib появился в 3.11
    sys.exit("levelc: нужен Python 3.11+ (tomllib)")
import tomllib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from amsh import (MeshBuilder, SLOT_ACCENT, SLOT_TOP, SLOT_TOP_ALT, SLOT_WALL,  # noqa: E402
                  SLOT_WATER)

ROOT = Path(__file__).resolve().parent.parent

# --- формат ALVL ---
LVL_MAGIC = b"ALVL"
LVL_VERSION = 1
LVL_HEADER = 96
CELL_SIZE = 4
# заголовок 96 байт по таблице docs/FORMATS.md: магия, версия, имя, палитра, размеры,
# cell/step, спавн, счётчики, камера, четыре смещения, идентификатор подписи, резерв
HEADER_FMT = "<4sI16s16sHHfffffHHHBBIIIIII"
ENTITY_FMT = "<HH4f6iHH"
LINK_FMT = "<HBBHBB"
PORTAL_FMT = "<HHBBHHHI"
ENTITY_SIZE = struct.calcsize(ENTITY_FMT)
LINK_SIZE = struct.calcsize(LINK_FMT)
PORTAL_SIZE = struct.calcsize(PORTAL_FMT)
assert (ENTITY_SIZE, LINK_SIZE, PORTAL_SIZE) == (48, 8, 16)
assert struct.calcsize(HEADER_FMT) == LVL_HEADER

MAX_SIDE = 256        # core/level.c: LVL_MAX_CELLS_SIDE
MAX_ENTITIES = 128
MAX_LINKS = 128
MAX_PORTALS = 8
MAX_PARAMS = 6        # LEVEL_ENTITY_PARAMS
MAX_HEIGHT = 15       # символы карты 0-9, a-f
MAX_SEGMENT = 63      # CELL_SEG_ID — младшие 6 бит байта segment

# Биты level_cell_t.flags (core/level.h)
CELL_EXISTS, CELL_WALK, CELL_STAIR, CELL_WATER, CELL_FLOAT, CELL_SEG = 1, 2, 4, 8, 16, 32

NO_STR16 = 0xFFFF
NO_STR32 = 0xFFFFFFFF

TRI_BUDGET = 4000     # docs/TECH.md §2.4: остров 2500–4000 треугольников

# --- запечённое затенение ---
# Всё освещение острова живёт в цвете вершин: шейдеров на PSP нет, а per-pixel
# ничего посчитать нельзя. Поэтому здесь печётся три вещи сразу: контактное
# затенение в углах, падающая тень от солнца и карниз — светлая полоса по верху
# каждой стены. Именно карниз и падающая тень отличают «дорогую» изометрию от
# набора кубиков: у объёма появляется кромка, у сцены — направление света.
AO_TOP_STEP = 46       # за каждого соседа выше у угла верхней грани
AO_TOP_MIN = 84
AO_WALL_TOP = 255      # верх стены (под карнизом)
AO_WALL_VOID = 118     # низ стены в пустоту: тёмный, но не чёрный — иначе киль выглядит дырой
AO_WALL_STEP = 168     # низ стены до соседней клетки
AO_WATER = 235         # водная поверхность затеняется слабее

# Направление НА солнце для запечённой тени. По XZ совпадает с направлением света
# в core/game.c, но ниже по высоте: высокое солнце даёт короткие тени, а длинная
# тень — главный признак объёма в изометрии. Тень строго техническая: она только
# темнит верхние грани, геометрии не добавляет.
SHADOW_DIR = (0.62, 0.46, 0.34)
SHADOW_MAX_CELLS = 14  # дальше не маршируем: тень всё равно теряется в тумане
SHADOW_CORE = 0.64     # во сколько раз темнее верх клетки в полной тени
SHADOW_EDGE = 0.84     # полутень: клетка, куда тень только дотянулась

# Карниз: узкая светлая полоса по верху стены. Высота в мировых единицах;
# если стена ниже, карниз занимает её целиком.
CORNICE_H = 0.09
CORNICE_AO = 255       # сам карниз — самое светлое место стены
WALL_BODY_TOP = 196    # тело стены начинается заметно темнее карниза: видна кромка

# Кладка: верхние грани получают разброс яркости по блокам 2×2 клетки. Без него
# большая плита читается как один кусок пластика; с ним — как набранная плитами.
SLAB_JITTER = 8        # ± единиц AO поверх шахматки плит
# Вынос под верхней гранью там, где стена уходит в пустоту: узкая полка снаружи.
# Она даёт кромке толщину и тёмную линию под собой — самый дешёвый «дорогой» приём.
LEDGE_OUT = 0.11       # насколько выступает наружу
LEDGE_H = 0.13         # высота полки
LEDGE_DROP = 0.02      # на сколько ниже верхней грани начинается полка

# Киль: стена, уходящая в пустоту, заваливается внутрь. Отвесная стена читается как
# срез коробки, а заваленная — как днище парящей скалы. Доля от глубины подошвы.
KEEL_INSET = 0.26

EPS = 1.0e-4
ID_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

_warnings = 0


def warn(msg):
    global _warnings
    _warnings += 1
    print(f"levelc: предупреждение: {msg}", file=sys.stderr)


def fail(msg):
    sys.exit(f"levelc: ошибка: {msg}")


# ---------------------------------------------------------------- таблицы кодов

def load_entity_types():
    """assets/entities.toml → список имён типов; индекс = код ENT_*."""
    path = ROOT / "assets" / "entities.toml"
    if not path.exists():
        fail(f"нет {path}")
    data = tomllib.loads(path.read_text(encoding="utf-8"))
    types = data.get("types")
    if not isinstance(types, list) or not types:
        fail(f"{path}: нужен непустой массив types = [...]")
    seen = set()
    for name in types:
        if not isinstance(name, str) or not ID_RE.match(name):
            fail(f"{path}: имя типа {name!r} не подходит под ^[A-Z][A-Z0-9_]*$")
        if name in seen:
            fail(f"{path}: тип {name!r} повторяется")
        seen.add(name)
    if types[0] != "NONE":
        fail(f"{path}: types[0] должен быть \"NONE\" (код 0 — пустая сущность)")
    if len(types) > 0xFFFF:
        fail(f"{path}: слишком много типов")
    return types


def load_level_names():
    """Имена уровней в алфавитном порядке — порядок задаёт коды LVL_*."""
    return sorted(p.stem for p in (ROOT / "levels").glob("*.toml"))


def load_strings():
    """Идентификаторы строк → индексы STR_*; None, если базового файла нет.

    Порядок ровно тот же, что у tools/stringsgen.py: сначала assets/strings.csv,
    затем фрагменты assets/strings/*.csv по алфавиту. Разойдись порядок — уровни
    ссылались бы не на те строки."""
    base = ROOT / "assets" / "strings.csv"
    if not base.exists():
        warn(f"нет {base} — идентификаторы строк будут пустыми (0xFFFF)")
        return None
    sources = [base] + sorted((ROOT / "assets" / "strings").glob("*.csv"))
    ids = {}
    index = 0
    for path in sources:
        with path.open(newline="", encoding="utf-8") as f:
            rows = list(csv.reader(f))
        if not rows or rows[0][:1] != ["id"]:
            warn(f"{path}: первая строка не заголовок 'id,...' — файл пропущен")
            continue
        for row in rows[1:]:
            if not row:
                continue
            sid = row[0].strip()
            if sid and sid not in ids:
                ids[sid] = index
            index += 1
    return ids


def resolve_str(strings, name, where, wide):
    """Идентификатор строки → индекс. Нет файла или нет идентификатора → пусто + предупреждение."""
    empty = NO_STR32 if wide else NO_STR16
    if name is None:
        return empty
    if not isinstance(name, str) or not ID_RE.match(name):
        fail(f"{where}: идентификатор строки {name!r} не подходит под ^[A-Z][A-Z0-9_]*$")
    if strings is None:
        return empty
    if name not in strings:
        warn(f"{where}: строки {name} нет в assets/strings.csv — записан пустой идентификатор")
        return empty
    return strings[name]


# ------------------------------------------------------------------ сетка клеток

def parse_height(ch, where):
    if ch in ". ":
        return None
    if ch.isdigit():
        return int(ch)
    if "a" <= ch <= "f":
        return 10 + ord(ch) - ord("a")
    fail(f"{where}: недопустимый символ карты {ch!r} (нужно '.', 0-9 или a-f)")


def split_rows(text, name, where):
    if text is None:
        return []
    if not isinstance(text, str):
        fail(f"{where}: слой {name} должен быть строкой")
    return [r.rstrip("\r") for r in text.strip("\n").splitlines()]


class Grid:
    """Слои уровня: высоты и признаки клеток. Мировые координаты как в core/level.c."""

    def __init__(self, level, where):
        rows = split_rows(level.get("map"), "map", where)
        if not rows:
            fail(f"{where}: пустой или отсутствующий слой map")
        self.w = max(len(r) for r in rows)
        self.h = len(rows)
        if self.w < 1 or self.w > MAX_SIDE or self.h > MAX_SIDE:
            fail(f"{where}: размер карты {self.w}x{self.h} вне 1..{MAX_SIDE}")
        self.cell = float(level.get("cell", 1.0))
        self.step = float(level.get("step", 0.5))
        self.base_depth = float(level.get("base_depth", 3.0))
        if self.cell <= 0.0 or self.step <= 0.0:
            fail(f"{where}: cell и step должны быть положительными")
        self._shadow = None
        # Высота декора над клеткой: колонны и башни кладут тень наравне с рельефом.
        self.occluder = [[0.0] * self.w for _ in range(self.h)]
        self.ox = -self.w * self.cell / 2.0
        self.oz = -self.h * self.cell / 2.0

        self.height = [[None] * self.w for _ in range(self.h)]
        for z, row in enumerate(rows):
            for x, ch in enumerate(row.ljust(self.w, ".")):
                hv = parse_height(ch, f"{where}: map, строка {z + 1}")
                if hv is not None and hv > MAX_HEIGHT:
                    fail(f"{where}: высота {hv} больше {MAX_HEIGHT}")
                self.height[z][x] = hv

        self.stair = self._mask(level, "stairs", where)
        self.water = self._mask(level, "water", where)
        self.afloat = self._mask(level, "floats", where)
        self.segment = self._segments(level, where)
        self.seg_state = self._seg_states(level, where)

        for z in range(self.h):
            for x in range(self.w):
                if self.height[z][x] is None:
                    for mask, name in ((self.stair, "stairs"), (self.water, "water"),
                                       (self.afloat, "floats")):
                        if mask[z][x]:
                            fail(f"{where}: слой {name} помечает пустую клетку ({x}, {z})")
                    if self.segment[z][x]:
                        fail(f"{where}: слой segments помечает пустую клетку ({x}, {z})")
                if self.stair[z][x] and self.water[z][x]:
                    fail(f"{where}: клетка ({x}, {z}) одновременно рампа и вода")

    def _layer_rows(self, level, name, where):
        rows = split_rows(level.get(name), name, where)
        if len(rows) > self.h:
            fail(f"{where}: слой {name}: {len(rows)} строк, в map {self.h}")
        for i, r in enumerate(rows):
            if len(r) > self.w:
                fail(f"{where}: слой {name}, строка {i + 1}: {len(r)} символов, в map {self.w}")
        rows = [r.ljust(self.w, ".") for r in rows]
        rows += ["." * self.w] * (self.h - len(rows))
        return rows

    def _mask(self, level, name, where):
        rows = self._layer_rows(level, name, where)
        out = [[False] * self.w for _ in range(self.h)]
        for z, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch in "xX":
                    out[z][x] = True
                elif ch not in ". ":
                    fail(f"{where}: слой {name}, строка {z + 1}: символ {ch!r} (нужно 'x' или '.')")
        return out

    def _segments(self, level, where):
        rows = self._layer_rows(level, "segments", where)
        out = [[0] * self.w for _ in range(self.h)]
        for z, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch in ". ":
                    continue
                if not ("1" <= ch <= "9"):
                    fail(f"{where}: слой segments, строка {z + 1}: символ {ch!r} (нужно 1-9 или '.')")
                out[z][x] = int(ch)
                if out[z][x] > MAX_SEGMENT:
                    fail(f"{where}: номер сегмента {out[z][x]} больше {MAX_SEGMENT}")
        return out

    def _seg_states(self, level, where):
        """Необязательный слой seg_states: 0-3 — состояние сегмента, в котором клетка
        проходима (старшие два бита байта segment, см. CELL_SEG_STATE в core/level.h)."""
        rows = self._layer_rows(level, "seg_states", where)
        out = [[0] * self.w for _ in range(self.h)]
        for z, row in enumerate(rows):
            for x, ch in enumerate(row):
                if ch in ". ":
                    continue
                if not ("0" <= ch <= "3"):
                    fail(f"{where}: слой seg_states, строка {z + 1}: символ {ch!r} (нужно 0-3 или '.')")
                if not self.segment[z][x]:
                    fail(f"{where}: seg_states помечает клетку ({x}, {z}) без сегмента")
                out[z][x] = int(ch)
        return out

    def seg_byte(self, x, z):
        """Байт segment: номер в младших 6 битах, требуемое состояние — в старших 2."""
        return self.segment[z][x] | (self.seg_state[z][x] << 6)

    # --- запросы ---

    def exists(self, x, z):
        return 0 <= x < self.w and 0 <= z < self.h and self.height[z][x] is not None

    def top(self, x, z):
        """Мировая высота верха клетки или None — как level_cell_top()."""
        if not self.exists(x, z):
            return None
        return self.height[z][x] * self.step

    def walkable(self, x, z):
        """Проходимость: вода непроходима, если на ней нет плавучей клетки."""
        if not self.exists(x, z):
            return False
        return not (self.water[z][x] and not self.afloat[z][x])

    def flags(self, x, z):
        if not self.exists(x, z):
            return 0
        f = CELL_EXISTS
        if self.walkable(x, z):
            f |= CELL_WALK
        if self.stair[z][x]:
            f |= CELL_STAIR
        if self.water[z][x]:
            f |= CELL_WATER
        if self.afloat[z][x]:
            f |= CELL_FLOAT
        if self.segment[z][x]:
            f |= CELL_SEG
        return f

    def ramp(self, x, z):
        """Ось и края рампы строго по core/walk.c cell_floor().

        Возвращает (ось, y0, y1): ось 'x' — высота идёт от западного края к восточному,
        'z' — от северного к южному, None — клетка плоская."""
        if not self.exists(x, z) or not self.stair[z][x]:
            return (None, 0.0, 0.0)
        base = self.height[z][x] * self.step
        west, east = self.top(x - 1, z), self.top(x + 1, z)
        north, south = self.top(x, z - 1), self.top(x, z + 1)
        dx = (east - west) if (west is not None and east is not None) else 0.0
        dz = (south - north) if (north is not None and south is not None) else 0.0
        adx, adz = abs(dx), abs(dz)
        if adx < 0.001 and adz < 0.001:
            return (None, base, base)
        if adx >= adz:  # тот же порядок сравнения, что в cell_floor()
            return ("x", west, west + dx)
        return ("z", north, north + dz)

    def edge(self, x, z, side):
        """Высоты поверхности на концах ребра клетки: side — 'e', 'w', 's', 'n'.
        Порядок концов: для 'e'/'w' — от z0 к z1, для 's'/'n' — от x0 к x1."""
        y00, y10, y11, y01 = self.corners(x, z)
        return {"e": (y10, y11), "w": (y00, y01), "s": (y01, y11), "n": (y00, y10)}[side]

    def corners(self, x, z):
        """Высоты четырёх углов верхней грани: (y00, y10, y11, y01) для (x0z0, x1z0, x1z1, x0z1)."""
        base = self.height[z][x] * self.step
        if self.water[z][x]:
            return (base, base, base, base)  # водная поверхность всегда горизонтальна
        axis, y0, y1 = self.ramp(x, z)
        if axis == "x":
            return (y0, y1, y1, y0)
        if axis == "z":
            return (y0, y0, y1, y1)
        return (base, base, base, base)

    def add_occluder(self, fx, fz, height):
        """Регистрирует декор как преграду для солнца: колонна должна класть тень."""
        x, z = int(fx // 1), int(fz // 1)
        if not self.exists(x, z) or height <= 0.0:
            return
        if self.occluder[z][x] < height:
            self.occluder[z][x] = height

    def shadow_map(self):
        """Доля света на верхней грани каждой клетки: 1 — открыто солнцу, меньше — тень.

        Маршируем от клетки в сторону солнца и смотрим, перекрывает ли кто-то луч.
        Луч поднимается на (L.y / |L.xz|) мировых единиц за клетку, поэтому высокая
        стена рядом даёт короткую тень, а далёкая башня — длинную. Полутень на
        последней клетке даёт мягкий край вместо ступеньки.
        """
        if self._shadow is not None:
            return self._shadow
        lx, ly, lz = SHADOW_DIR
        flat = math.hypot(lx, lz)
        if flat < 1e-6:
            self._shadow = [[1.0] * self.w for _ in range(self.h)]
            return self._shadow
        dx, dz = lx / flat, lz / flat
        rise = (ly / flat) * self.cell          # на сколько луч поднимается за клетку
        shadow = [[1.0] * self.w for _ in range(self.h)]
        for z in range(self.h):
            for x in range(self.w):
                if self.height[z][x] is None:
                    continue
                y = self.top(x, z)
                blocked = 0.0
                for i in range(1, SHADOW_MAX_CELLS + 1):
                    nx = int(math.floor(x + 0.5 + dx * i))
                    nz = int(math.floor(z + 0.5 + dz * i))
                    if not (0 <= nx < self.w and 0 <= nz < self.h):
                        break
                    n = self.height[nz][nx]
                    if n is None:
                        continue
                    occ = self.top(nx, nz) + self.occluder[nz][nx]
                    if occ > y + rise * i + EPS:
                        blocked = SHADOW_CORE if i > 1 else SHADOW_EDGE
                        # чем ближе преграда, тем плотнее тень
                        if i > 2:
                            blocked = SHADOW_CORE + (SHADOW_EDGE - SHADOW_CORE) * min(1.0, (i - 2) / 6.0)
                        break
                shadow[z][x] = blocked if blocked else 1.0
        self._shadow = shadow
        return shadow

    def surface_y(self, fx, fz):
        """Высота поверхности в дробных координатах клеток (для посадки сущностей)."""
        x, z = int(fx // 1), int(fz // 1)
        if not self.exists(x, z):
            return None
        y00, y10, y11, y01 = self.corners(x, z)
        lx, lz = min(max(fx - x, 0.0), 1.0), min(max(fz - z, 0.0), 1.0)
        return (y00 * (1.0 - lx) * (1.0 - lz) + y10 * lx * (1.0 - lz)
                + y11 * lx * lz + y01 * (1.0 - lx) * lz)

    def world(self, fx, fz):
        """Координаты клеток (дробные) → мировые."""
        return (self.ox + fx * self.cell, self.oz + fz * self.cell)


# -------------------------------------------------------------------- геометрия

def _wall(mb, pa, pb, ya_bot, yb_bot, normal, slot, ao_bottom, sun=255, inset=0.0):
    """Стена между верхним ребром (pa→pb) и нижним — теми же (x, z) на высотах ya_bot/yb_bot.
    Низ — поверхность соседа, поэтому у рампы боковина выходит треугольником и щелей нет.

    Стена режется на карниз (светлая полоса CORNICE_H по верху) и тело с градиентом
    вниз. Ради этой полосы всё и затевалось: без неё стык верхней грани со стеной —
    просто смена цвета, а с ней у плиты появляется толщина и кромка, ловящая свет."""
    da, db = pa[1] - ya_bot, pb[1] - yb_bot
    if da <= EPS and db <= EPS:
        return
    # Нижнее ребро уводится внутрь вдоль нормали: получается завал стены (киль).
    pa_b = (pa[0] - normal[0] * inset, ya_bot, pa[2] - normal[2] * inset)
    pb_b = (pb[0] - normal[0] * inset, yb_bot, pb[2] - normal[2] * inset)
    if da > EPS and db > EPS:
        cut = min(CORNICE_H, da, db)
        if cut > EPS * 10.0:
            # карниз не заваливаем, завал начинается под ним
            k = (cut / da) if da > EPS else 0.0
            pa_c = (pa[0] - normal[0] * inset * k, pa[1] - cut, pa[2] - normal[2] * inset * k)
            k = (cut / db) if db > EPS else 0.0
            pb_c = (pb[0] - normal[0] * inset * k, pb[1] - cut, pb[2] - normal[2] * inset * k)
            mb.quad(pa, pb, pb_c, pa_c, slot,
                    (CORNICE_AO, CORNICE_AO, CORNICE_AO, CORNICE_AO), normal, sun)
            mb.quad(pa_c, pb_c, pb_b, pa_b, slot,
                    (WALL_BODY_TOP, WALL_BODY_TOP, ao_bottom, ao_bottom), normal, sun)
        else:
            mb.quad(pa, pb, pb_b, pa_b, slot,
                    (AO_WALL_TOP, AO_WALL_TOP, ao_bottom, ao_bottom), normal, sun)
        return
    # верх и низ пересекаются внутри ребра — делим его в точке совпадения
    t = da / (da - db)
    mid = (pa[0] + (pb[0] - pa[0]) * t, pa[1] + (pb[1] - pa[1]) * t, pa[2] + (pb[2] - pa[2]) * t)
    if da > EPS:
        mb.tri(pa, mid, pa_b, slot, (AO_WALL_TOP, ao_bottom, ao_bottom), normal, sun)
    else:
        mb.tri(mid, pb, pb_b, slot, (ao_bottom, AO_WALL_TOP, ao_bottom), normal, sun)


def _slab_delta(x, z):
    """Разброс яркости плиты по блокам 2×2: детерминированный, без шума по клеткам."""
    h = ((x >> 1) * 73856093) ^ ((z >> 1) * 19349663)
    return SLAB_JITTER if (h >> 5) & 1 else -SLAB_JITTER


def _ledge(mb, x0, x1, z0, z1, y_top, side, slot, sun):
    """Полка-вынос снаружи стены, уходящей в пустоту. side — 'e', 'w', 'n', 's'."""
    top = y_top - LEDGE_DROP
    bot = top - LEDGE_H
    if side == "e":
        a0, a1 = (x1, top, z0), (x1 + LEDGE_OUT, top, z0)
        b0, b1 = (x1, top, z1), (x1 + LEDGE_OUT, top, z1)
    elif side == "w":
        a0, a1 = (x0 - LEDGE_OUT, top, z0), (x0, top, z0)
        b0, b1 = (x0 - LEDGE_OUT, top, z1), (x0, top, z1)
    elif side == "s":
        a0, a1 = (x0, top, z1), (x0, top, z1 + LEDGE_OUT)
        b0, b1 = (x1, top, z1), (x1, top, z1 + LEDGE_OUT)
    else:
        a0, a1 = (x0, top, z0 - LEDGE_OUT), (x0, top, z0)
        b0, b1 = (x1, top, z0 - LEDGE_OUT), (x1, top, z0)

    # верх полки — светлый, лицевая кромка чуть темнее, низ почти чёрный
    mb.quad(a0, a1, b1, b0, slot, CORNICE_AO, (0, 1, 0), sun)
    if side in ("e", "w"):
        fx = (x1 + LEDGE_OUT) if side == "e" else (x0 - LEDGE_OUT)
        nx = 1 if side == "e" else -1
        mb.quad((fx, top, z0), (fx, top, z1), (fx, bot, z1), (fx, bot, z0), slot,
                (WALL_BODY_TOP, WALL_BODY_TOP, AO_WALL_VOID, AO_WALL_VOID), (nx, 0, 0), sun)
    else:
        fz = (z1 + LEDGE_OUT) if side == "s" else (z0 - LEDGE_OUT)
        nz = 1 if side == "s" else -1
        mb.quad((x0, top, fz), (x1, top, fz), (x1, bot, fz), (x0, bot, fz), slot,
                (WALL_BODY_TOP, WALL_BODY_TOP, AO_WALL_VOID, AO_WALL_VOID), (0, 0, nz), sun)


def build_mesh(g):
    """Геометрия острова: верхние грани клеток (включая рампы и воду) и стены вниз.

    Клетки сегментов уходят в отдельные построители — по одному на пару
    (номер сегмента, состояние, в котором клетка проходима). В игре такая плита
    поднимается, когда её состояние стало текущим, и уходит вниз, когда нет.
    Иначе рычаг менял бы только проходимость, а камень стоял бы на месте: механика
    Часовых Террас и все ворота существовали бы только в коде.
    Возвращает (построитель острова, {(номер, состояние): построитель})."""
    mb = MeshBuilder()
    segs = {}
    base_y = -g.base_depth
    heights = [g.height[z][x] for z in range(g.h) for x in range(g.w) if g.height[z][x] is not None]
    if not heights:
        fail("в карте нет ни одной клетки")
    common = Counter(heights).most_common(1)[0][0]  # «плоскость» острова
    shadow = g.shadow_map()

    for z in range(g.h):
        for x in range(g.w):
            hc = g.height[z][x]
            if hc is None:
                continue
            seg_id = g.segment[z][x]
            out = segs.setdefault((seg_id, g.seg_state[z][x]), MeshBuilder()) if seg_id else mb
            x0, x1 = g.ox + x * g.cell, g.ox + (x + 1) * g.cell
            z0, z1 = g.oz + z * g.cell, g.oz + (z + 1) * g.cell
            y00, y10, y11, y01 = g.corners(x, z)

            # AO угла: сколько соседей вокруг него выше этой клетки
            def corner_ao(dx, dz):
                higher = 0
                for nx, nz in ((x + dx, z), (x, z + dz), (x + dx, z + dz)):
                    n = g.height[nz][nx] if g.exists(nx, nz) else None
                    if n is not None and n > hc:
                        higher += 1
                return max(AO_TOP_MIN, 255 - AO_TOP_STEP * higher)

            # Падающая тень от солнца едет отдельным байтом вершины: в игре она
            # гасит только прямой свет, а не весь цвет, поэтому тень остаётся
            # цветной (небесный подсвет), а не превращается в серое пятно.
            sun_top = max(0, min(255, int(shadow[z][x] * 255.0 + 0.5)))

            if g.water[z][x] and not g.afloat[z][x]:
                slot_top, ao_top = SLOT_WATER, (AO_WATER,) * 4
            else:
                if g.segment[z][x]:
                    slot_top = SLOT_ACCENT          # вращающиеся части читаются акцентом
                elif hc != common:
                    slot_top = SLOT_TOP_ALT         # уступы выделяются вариантом верха
                else:
                    # Главная плита выкладывается плитами 2×2: шахматка из двух
                    # оттенков верха. Ровная заливка на пол-экрана — первое, из-за
                    # чего картинка выглядит дешёвой; плитка даёт масштаб и материал.
                    slot_top = SLOT_TOP if ((x >> 1) + (z >> 1)) & 1 else SLOT_TOP_ALT
                slab = _slab_delta(x, z)
                ao_top = tuple(max(24, min(255, corner_ao(dx, dz) + slab))
                               for dx, dz in ((-1, -1), (1, -1), (1, 1), (-1, 1)))

            # нормаль верхней грани: у рампы наклонная, иначе строго вверх
            axis, ry0, ry1 = g.ramp(x, z)
            if axis == "x":
                normal_top = (-(ry1 - ry0) / g.cell, 1.0, 0.0)
            elif axis == "z":
                normal_top = (0.0, 1.0, -(ry1 - ry0) / g.cell)
            else:
                normal_top = (0.0, 1.0, 0.0)
            out.quad((x0, y00, z0), (x1, y10, z0), (x1, y11, z1), (x0, y01, z1),
                     slot_top, ao_top, normal_top, sun_top)

            # стены: к соседям ниже и в пустоту; низ ребра — поверхность соседа
            for dx, dz, opp, normal, pa, pb in (
                (1, 0, "w", (1, 0, 0), (x1, y10, z0), (x1, y11, z1)),     # восток
                (-1, 0, "e", (-1, 0, 0), (x0, y00, z0), (x0, y01, z1)),   # запад
                (0, 1, "n", (0, 0, 1), (x0, y01, z1), (x1, y11, z1)),     # юг
                (0, -1, "s", (0, 0, -1), (x0, y00, z0), (x1, y10, z0)),   # север
            ):
                if g.exists(x + dx, z + dz):
                    ya, yb = g.edge(x + dx, z + dz, opp)
                    ao_bottom = AO_WALL_STEP
                    inset = 0.0
                else:
                    ya, yb = base_y, base_y
                    ao_bottom = AO_WALL_VOID
                    # Край острова: под верхней гранью полка-вынос, а сама стена
                    # заваливается внутрь — снизу остров сходится клином.
                    side = {"w": "e", "e": "w", "n": "s", "s": "n"}[opp]
                    _ledge(out, x0, x1, z0, z1, min(pa[1], pb[1]), side, slot_top, sun_top)
                    inset = g.base_depth * KEEL_INSET
                _wall(out, pa, pb, ya, yb, normal, SLOT_WALL, ao_bottom, sun_top, inset)
    return mb, segs


# ----------------------------------------------------------------- данные ALVL

def pack_name(value, field, limit=16):
    data = str(value).encode("utf-8")
    if len(data) >= limit:
        fail(f"{field}: {value!r} длиннее {limit - 1} байт")
    return data + b"\0" * (limit - len(data))


def as_int(value, field, lo, hi):
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{field}: нужно целое, получено {value!r}")
    if not lo <= value <= hi:
        fail(f"{field}: {value} вне диапазона {lo}..{hi}")
    return value


def as_pair(value, field):
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        fail(f"{field}: нужен массив из двух чисел, получено {value!r}")
    out = []
    for v in value:
        if isinstance(v, bool) or not isinstance(v, (int, float)):
            fail(f"{field}: нужны числа, получено {value!r}")
        out.append(float(v))
    return out


# ------------------------------------------------------------------- декор

# Декор — чистая геометрия: ни сущностей, ни логики, ни флагов. Именно он отличает
# «выдавленную карту высот» от построенного места: арка над мостом задаёт вход,
# колонна держит взгляд, полотнище даёт вертикаль и единственное пятно цвета.
PROP_TYPES = ("column", "arch", "banner", "obelisk", "tower")


def _prop_column(mb, height, slot, accent):
    """Колонна: база, ствол с сужением, капитель."""
    r = 0.17
    mb.box((0.0, 0.07, 0.0), (0.52, 0.14, 0.52), slot, ao_top=250, ao_side=215, ao_bottom=150)
    mb.prism((0.0, 0.14 + height * 0.5, 0.0), r, height, 8, slot,
             ao_top=245, ao_side=225, ao_bottom=170)
    mb.box((0.0, 0.14 + height + 0.06, 0.0), (0.46, 0.12, 0.46), slot,
           ao_top=255, ao_side=230, ao_bottom=160)
    mb.box((0.0, 0.14 + height + 0.15, 0.0), (0.30, 0.06, 0.30), accent,
           ao_top=255, ao_side=235, ao_bottom=180)


def _prop_arch(mb, height, slot, accent):
    """Арка со ступенчатым сводом: проём вдоль оси X.

    Свод набирается из четырёх уменьшающихся блоков с каждой стороны — прямая
    перемычка читается как балка, а ступенчатый свод как арка, и это ровно та
    разница, из-за которой сцена выглядит построенной, а не собранной из кубиков."""
    span = 1.05
    for sx in (-span, span):
        mb.box((sx, height * 0.5, 0.0), (0.34, height, 0.40), slot,
               ao_top=248, ao_side=218, ao_bottom=150)

    # ступени свода: снизу вверх сужаются к центру
    steps = ((0.86, 0.17, 0.30), (0.62, 0.15, 0.30), (0.40, 0.13, 0.28))
    y = height
    for dx, h, d in steps:
        y += h * 0.5
        for sx in (-dx, dx):
            mb.box((sx, y, 0.0), (0.34, h, d), slot, ao_top=252, ao_side=222, ao_bottom=166)
        y += h * 0.5
    # замок свода и золотая метка на нём
    mb.box((0.0, y + 0.10, 0.0), (0.70, 0.20, 0.34), slot, ao_top=255, ao_side=230, ao_bottom=172)
    mb.box((0.0, y + 0.26, 0.0), (0.22, 0.10, 0.22), accent, ao_top=255, ao_side=242, ao_bottom=196)


def _prop_tower(mb, height, slot, accent):
    """Башня: три сужающихся яруса и светлая верхушка. Даёт вертикаль силуэту,
    из-за которой остров перестаёт быть плоской лепёшкой."""
    h1 = height * 0.5
    h2 = height * 0.32
    h3 = height * 0.18
    mb.box((0.0, 0.10, 0.0), (1.05, 0.20, 1.05), slot, ao_top=250, ao_side=214, ao_bottom=150)
    mb.box((0.0, 0.20 + h1 * 0.5, 0.0), (0.82, h1, 0.82), slot,
           ao_top=246, ao_side=220, ao_bottom=168)
    mb.box((0.0, 0.20 + h1 + 0.06, 0.0), (0.94, 0.12, 0.94), slot,
           ao_top=255, ao_side=232, ao_bottom=176)
    mb.box((0.0, 0.32 + h1 + h2 * 0.5, 0.0), (0.62, h2, 0.62), slot,
           ao_top=246, ao_side=224, ao_bottom=172)
    mb.box((0.0, 0.32 + h1 + h2 + 0.05, 0.0), (0.72, 0.10, 0.72), slot,
           ao_top=255, ao_side=234, ao_bottom=180)
    mb.prism((0.0, 0.42 + h1 + h2 + h3 * 0.5, 0.0), 0.24, h3, 8, accent,
             ao_top=255, ao_side=238, ao_bottom=190)


def _prop_banner(mb, height, slot, accent):
    """Полотнище на шесте: вертикаль и единственное пятно цвета.

    Полотно — коробки, а не плоские четырёхугольники: плоскость в изометрии
    поворачивается ребром к камере и превращается в золотую щепку поперёк кадра.
    Тонкая коробка всегда показывает грань, и флаг читается флагом под любым ракурсом."""
    mb.prism((0.0, height * 0.5, 0.0), 0.06, height, 6, slot, ao_top=240, ao_side=210)
    mb.box((0.18, height - 0.06, 0.0), (0.40, 0.08, 0.10), slot, ao_top=250, ao_side=220)
    mb.box((0.24, height - 0.37, 0.0), (0.34, 0.50, 0.09), accent,
           ao_top=250, ao_side=225, ao_bottom=190)
    mb.box((0.20, height - 0.80, 0.0), (0.22, 0.38, 0.09), accent,
           ao_top=225, ao_side=200, ao_bottom=165)


def _prop_obelisk(mb, height, slot, accent):
    """Обелиск: сужающийся столб с золотой верхушкой — вертикальный акцент издалека."""
    mb.box((0.0, 0.09, 0.0), (0.66, 0.18, 0.66), slot, ao_top=250, ao_side=215, ao_bottom=150)
    mb.prism((0.0, 0.18 + height * 0.5, 0.0), 0.22, height, 4, slot,
             ao_top=245, ao_side=222, ao_bottom=165)
    mb.prism((0.0, 0.18 + height + 0.16, 0.0), 0.16, 0.32, 4, accent,
             ao_top=255, ao_side=238, ao_bottom=190)


_PROP_BUILDERS = {
    "column": _prop_column,
    "arch": _prop_arch,
    "banner": _prop_banner,
    "obelisk": _prop_obelisk,
    "tower": _prop_tower,
}


# Какая доля высоты декора реально перекрывает солнце. Колонна тонкая, но в
# изометрии её тень всё равно ложится на клетку целиком — это читается как объём,
# а не как ошибка; полотнище и арка перекрывают меньше.
PROP_OCCLUSION = {"column": 0.95, "arch": 0.75, "banner": 0.60, "obelisk": 0.90, "tower": 1.0}


def register_prop_shadows(level, g, where):
    """Заносит декор в карту преград ДО расчёта теней: иначе колонны висят без тени."""
    items = level.get("prop", [])
    if not isinstance(items, list):
        fail(f"{where}: [[prop]] должен быть массивом таблиц")
    for i, p in enumerate(items):
        kind = p.get("type")
        if kind not in _PROP_BUILDERS:
            continue          # тип проверит build_props — там же и сообщение об ошибке
        cell = p.get("cell")
        if not isinstance(cell, (list, tuple)) or len(cell) != 2:
            continue
        try:
            fx, fz = float(cell[0]), float(cell[1])
        except (TypeError, ValueError):
            continue
        h = float(p.get("height", 1.6)) * float(p.get("scale", 1.0))
        g.add_occluder(fx, fz, h * PROP_OCCLUSION.get(kind, 0.9))


def build_props(mb, level, g, where):
    """Ставит декор из [[prop]] на поверхность острова."""
    items = level.get("prop", [])
    if not isinstance(items, list):
        fail(f"{where}: [[prop]] должен быть массивом таблиц")
    for i, p in enumerate(items):
        tag = f"{where}: prop #{i + 1}"
        kind = p.get("type")
        if kind not in _PROP_BUILDERS:
            fail(f"{tag}: type = {kind!r}, известны {', '.join(PROP_TYPES)}")
        fx, fz = as_pair(p.get("cell"), f"{tag}: cell")
        y = g.surface_y(fx, fz)
        if y is None:
            fail(f"{tag}: клетка ({fx}, {fz}) пуста — декору не на чем стоять")
        yaw = float(p.get("yaw", 0.0))
        scale = float(p.get("scale", 1.0))
        if not (0.2 <= scale <= 4.0):
            fail(f"{tag}: scale = {scale}, допустимо 0,2..4,0")
        height = float(p.get("height", 1.6))
        if not (0.3 <= height <= 12.0):
            fail(f"{tag}: height = {height}, допустимо 0,3..12,0")
        wx, wz = g.world(fx, fz)
        slot = SLOT_TOP_ALT if p.get("dark") else SLOT_TOP
        mb.push((wx, y, wz), yaw_deg=yaw, scale=scale)
        _PROP_BUILDERS[kind](mb, height, slot, SLOT_ACCENT)
        mb.pop()
    return len(items)


def build_cells(g):
    out = bytearray()
    for z in range(g.h):
        for x in range(g.w):
            if g.height[z][x] is None:
                out += b"\0\0\0\0"
            else:
                out += struct.pack("<BBBB", g.height[z][x], g.flags(x, z), g.seg_byte(x, z), 0)
    return bytes(out)


def build_entities(level, g, types, strings, where):
    items = level.get("entity", [])
    if not isinstance(items, list):
        fail(f"{where}: [[entity]] должен быть массивом таблиц")
    if len(items) > MAX_ENTITIES:
        fail(f"{where}: {len(items)} сущностей, лимит {MAX_ENTITIES}")
    out = bytearray()
    ids = {}
    for i, e in enumerate(items):
        tag = f"{where}: entity #{i + 1}"
        type_name = e.get("type")
        if type_name not in types:
            fail(f"{tag}: неизвестный тип {type_name!r} (нет в assets/entities.toml)")
        eid = as_int(e.get("id"), f"{tag}: id", 1, 0xFFFF)
        if eid in ids:
            fail(f"{tag}: id {eid} уже занят сущностью #{ids[eid] + 1}")
        ids[eid] = i
        fx, fz = as_pair(e.get("cell"), f"{tag}: cell")
        if not (0.0 <= fx <= g.w and 0.0 <= fz <= g.h):
            fail(f"{tag}: cell [{fx}, {fz}] вне сетки {g.w}x{g.h}")
        wx, wz = g.world(fx, fz)
        y = e.get("y")
        if y is None:
            y = g.surface_y(fx, fz)
            if y is None:
                warn(f"{tag}: клетка [{fx}, {fz}] пустая — высота 0")
                y = 0.0
        else:
            y = float(y)
        yaw = float(e.get("yaw", 0.0))
        flags = as_int(e.get("flags", 0), f"{tag}: flags", 0, 0xFFFF)
        params = e.get("params", [])
        if not isinstance(params, list) or len(params) > MAX_PARAMS:
            fail(f"{tag}: params — массив не длиннее {MAX_PARAMS}")
        vals = [as_int(p, f"{tag}: params", -(1 << 31), (1 << 31) - 1) for p in params]
        vals += [0] * (MAX_PARAMS - len(vals))
        name_id = resolve_str(strings, e.get("name"), f"{tag}: name", wide=False)
        out += struct.pack(ENTITY_FMT, types.index(type_name), flags,
                           wx, y, wz, yaw, *vals, name_id, eid)
    return bytes(out), len(items), set(ids)


def build_links(level, ids, where):
    items = level.get("link", [])
    if not isinstance(items, list):
        fail(f"{where}: [[link]] должен быть массивом таблиц")
    if len(items) > MAX_LINKS:
        fail(f"{where}: {len(items)} связей, лимит {MAX_LINKS}")
    out = bytearray()
    for i, lk in enumerate(items):
        tag = f"{where}: link #{i + 1}"
        src = lk.get("from")
        dst = lk.get("to")
        for what, pair in (("from", src), ("to", dst)):
            if not isinstance(pair, (list, tuple)) or len(pair) != 2:
                fail(f"{tag}: {what} — массив [id, номер]")
        src_id = as_int(src[0], f"{tag}: from[0]", 1, 0xFFFF)
        # Рантайм держит по четыре выхода и входа (ENT_OUT_BITS/ENT_IN_BITS в core/entity.h)
        # и молча отбрасывает связь с большим номером — ловим это здесь, а не в игре.
        src_out = as_int(src[1], f"{tag}: from[1]", 0, 3)
        dst_id = as_int(dst[0], f"{tag}: to[0]", 1, 0xFFFF)
        dst_in = as_int(dst[1], f"{tag}: to[1]", 0, 3)
        for what, eid in (("from", src_id), ("to", dst_id)):
            if eid not in ids:
                fail(f"{tag}: {what} ссылается на несуществующий id {eid}")
        out += struct.pack(LINK_FMT, src_id, src_out, 0, dst_id, dst_in, 0)
    return bytes(out), len(items)


def build_portals(level, g, level_names, where):
    items = level.get("portal", [])
    if not isinstance(items, list):
        fail(f"{where}: [[portal]] должен быть массивом таблиц")
    if len(items) > MAX_PORTALS:
        fail(f"{where}: {len(items)} порталов, лимит {MAX_PORTALS}")
    out = bytearray()
    for i, p in enumerate(items):
        tag = f"{where}: portal #{i + 1}"
        cell = p.get("cell")
        if not isinstance(cell, (list, tuple)) or len(cell) != 2:
            fail(f"{tag}: cell — массив [cx, cz] целых")
        cx = as_int(cell[0], f"{tag}: cell[0]", 0, MAX_SIDE - 1)
        cz = as_int(cell[1], f"{tag}: cell[1]", 0, MAX_SIDE - 1)
        size = p.get("size", [1, 1])
        if not isinstance(size, (list, tuple)) or len(size) != 2:
            fail(f"{tag}: size — массив [w, h] целых")
        pw = as_int(size[0], f"{tag}: size[0]", 1, 255)
        ph = as_int(size[1], f"{tag}: size[1]", 1, 255)
        if cx + pw > g.w or cz + ph > g.h:
            fail(f"{tag}: прямоугольник [{cx}, {cz}] {pw}x{ph} выходит за сетку {g.w}x{g.h}")
        # Портал должен лежать на существующих клетках: иначе он недостижим,
        # а игрок увидит «мост в никуда».
        empty = [(x, z) for z in range(cz, cz + ph) for x in range(cx, cx + pw)
                 if g.height[z][x] is None]
        if len(empty) == pw * ph:
            fail(f"{tag}: прямоугольник [{cx}, {cz}] {pw}x{ph} целиком в пустоте")
        if empty:
            warn(f"{tag}: {len(empty)} из {pw * ph} клеток портала пусты")
        target = p.get("to")
        if target not in level_names:
            fail(f"{tag}: to = {target!r} — нет файла levels/{target}.toml")
        entry = as_int(p.get("entry", 0), f"{tag}: entry", 0, 0xFFFF)
        out += struct.pack(PORTAL_FMT, cx, cz, pw, ph, level_names.index(target), entry, 0, 0)
    return bytes(out), len(items)


LEVEL_KEYS = {"name", "palette", "cell", "step", "base_depth", "cam_angle", "cam_lock",
              "spawn", "spawn_yaw", "caption", "map", "stairs", "water", "floats",
              "segments", "seg_states", "entity", "link", "portal", "prop"}

SEG_MESH_MAX = 16     # столько плит сегментов грузит игра (GAME_SEG_MAX в core/game.c)


def segment_anchors(g, segs):
    """Центр клеток каждой плиты: в игре оттуда бьёт пыль, когда плита встаёт на место.
    Пишется в заголовок AMSH (версия 2), отдельного места в .lvl не занимает."""
    out = {}
    for key in segs:
        sid, state = key
        cells = [(x, z) for z in range(g.h) for x in range(g.w)
                 if g.segment[z][x] == sid and g.seg_state[z][x] == state]
        fx = sum(x for x, _ in cells) / len(cells) + 0.5
        fz = sum(z for _, z in cells) / len(cells) + 0.5
        wx, wz = g.world(fx, fz)
        out[key] = (wx, g.surface_y(fx, fz) or 0.0, wz)
    return out


def compile_level(src, out_dir):
    where = src.name
    level = tomllib.loads(src.read_text(encoding="utf-8"))
    # Опечатка в имени ключа раньше проходила молча: слой seg_state вместо seg_states
    # просто не применялся, а дверь оставалась заперта навсегда.
    unknown = sorted(set(level) - LEVEL_KEYS)
    if unknown:
        fail(f"{where}: неизвестные ключи: {', '.join(unknown)}")
    types = load_entity_types()
    level_names = load_level_names()
    strings = load_strings()

    name = level.get("name", src.stem)
    if name != src.stem:
        warn(f"{where}: name = {name!r} не совпадает с именем файла {src.stem!r}")
    g = Grid(level, where)

    spawn = as_pair(level.get("spawn", [g.w / 2.0, g.h / 2.0]), f"{where}: spawn")
    if not g.walkable(int(spawn[0] // 1), int(spawn[1] // 1)):
        fail(f"{where}: spawn {spawn} не на проходимой клетке")
    spawn_x, spawn_z = g.world(spawn[0], spawn[1])
    spawn_yaw = float(level.get("spawn_yaw", 0.0))
    cam_angle = as_int(level.get("cam_angle", 0), f"{where}: cam_angle", 0, 3)
    cam_lock = 1 if level.get("cam_lock", False) else 0
    caption = resolve_str(strings, level.get("caption"), f"{where}: caption", wide=True)

    cells = build_cells(g)
    entities, ent_count, ids = build_entities(level, g, types, strings, where)
    links, link_count = build_links(level, ids, where)
    portals, portal_count = build_portals(level, g, level_names, where)

    cells_off = LVL_HEADER
    ent_off = cells_off + len(cells)
    link_off = ent_off + len(entities)
    portal_off = link_off + len(links)
    for off in (cells_off, ent_off, link_off, portal_off):
        assert off % 4 == 0, off

    header = struct.pack(HEADER_FMT, LVL_MAGIC, LVL_VERSION,
                         pack_name(name, f"{where}: name"),
                         pack_name(level.get("palette", name), f"{where}: palette"),
                         g.w, g.h, g.cell, g.step, spawn_x, spawn_z, spawn_yaw,
                         ent_count, link_count, portal_count, cam_angle, cam_lock,
                         cells_off, ent_off, link_off, portal_off, caption, 0)
    assert len(header) == LVL_HEADER, len(header)

    out_dir.mkdir(parents=True, exist_ok=True)
    lvl_path = out_dir / (src.stem + ".lvl")
    lvl_path.write_bytes(header + cells + entities + links + portals)

    register_prop_shadows(level, g, where)   # до build_mesh: там печётся карта теней
    mb, segs = build_mesh(g)
    prop_count = build_props(mb, level, g, where)
    msh_path = out_dir / (src.stem + ".msh")
    mb.write(msh_path)
    anchors = segment_anchors(g, segs)
    seg_tris = 0
    if len(segs) > SEG_MESH_MAX:
        fail(f"{where}: {len(segs)} плит сегментов, предел {SEG_MESH_MAX} (см. core/game.c)")
    for key in sorted(segs):
        sid, state = key
        segs[key].write(out_dir / f"{src.stem}_seg{sid}_{state}.msh", anchors[key])
        seg_tris += len(segs[key]) // 3
    tris = len(mb) // 3 + seg_tris
    if tris > TRI_BUDGET:
        warn(f"{where}: {tris} треугольников, бюджет острова {TRI_BUDGET} (docs/TECH.md §2.4)")

    walk_cells = sum(1 for z in range(g.h) for x in range(g.w) if g.flags(x, z) & CELL_WALK)
    print(f"{src.stem}: {g.w}x{g.h} клеток ({walk_cells} проходимых), "
          f"{ent_count} сущностей, {link_count} связей, {portal_count} порталов "
          f"-> {lvl_path.name} ({lvl_path.stat().st_size} Б)")
    seg_note = f", сегментов {len(segs)} ({seg_tris} тр.)" if segs else ""
    print(f"{src.stem}: {len(mb)} вершин, {tris} треугольников, {prop_count} декора{seg_note}"
          f" -> {msh_path.name} "
          f"({msh_path.stat().st_size} Б)"
          + (f", предупреждений: {_warnings}" if _warnings else ""))
    return lvl_path, msh_path


# ------------------------------------------------------------------ заголовки

def gen_header(guard, comment, enum_prefix, names, tables):
    """Общий каркас заголовка: enum по именам + функции-таблицы (таблица внутри функции,
    иначе -Wunused-const-variable в TU, где она не нужна)."""
    width = max((len(enum_prefix + n) for n in names), default=len(enum_prefix + "COUNT")) + 1
    lines = [f"/* {comment} */", f"#ifndef {guard}", f"#define {guard}", "", "enum {"]
    for i, n in enumerate(names):
        lines.append(f"    {(enum_prefix + n + ' ').ljust(width)}= {i},")
    lines.append(f"    {(enum_prefix + 'COUNT ').ljust(width)}= {len(names)}")
    lines.append("};")
    for func, doc, values in tables:
        lines += ["", f"/* {doc} */", f"static inline const char *{func}(unsigned index) {{"]
        if names:
            lines.append(f"    static const char *const table[{enum_prefix}COUNT] = {{")
            for v in values:
                lines.append(f"        \"{v}\",")
            lines.append("    };")
            lines.append(f"    return index < {enum_prefix}COUNT ? table[index] : 0;")
        else:
            lines.append("    (void)index;")
            lines.append("    return 0;")
        lines.append("}")
    lines += ["", f"#endif /* {guard} */", ""]
    return "\n".join(lines)


def write_headers(out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    types = load_entity_types()
    ent_h = out_dir / "entity_types.h"
    ent_h.write_text(gen_header(
        "ARGUS_ENTITY_TYPES_H",
        "entity_types.h — сгенерировано tools/levelc.py из assets/entities.toml.\n"
        " * Не править вручную: порядок задаёт коды в .lvl, добавлять типы только в конец.",
        "ENT_", types,
        [("entity_type_name", "Имя типа сущности для отладочного вывода; 0 вне диапазона.", types)],
    ), encoding="utf-8")

    names = load_level_names()
    lvl_h = out_dir / "level_ids.h"
    lvl_h.write_text(gen_header(
        "ARGUS_LEVEL_IDS_H",
        "level_ids.h — сгенерировано tools/levelc.py по файлам levels (алфавитный порядок).\n"
        " * Не править вручную: LVL_* хранятся в порталах .lvl.",
        "LVL_", [n.upper() for n in names],
        [("level_data_path", "Путь к данным уровня относительно EBOOT (fs_path).",
          [f"data/{n}.lvl" for n in names]),
         ("level_mesh_path", "Путь к геометрии острова относительно EBOOT.",
          [f"data/{n}.msh" for n in names]),
         ("level_name", "Внутреннее имя уровня (совпадает с levels/<имя>.toml).", names)],
    ), encoding="utf-8")

    print(f"entity_types.h: {len(types)} типов -> {ent_h}")
    print(f"level_ids.h: {len(names)} уровней ({', '.join(names) or '—'}) -> {lvl_h}")


# -------------------------------------------------------------------- проверка

def verify(path):
    """Независимый разбор .lvl строго по docs/FORMATS.md с проверкой инвариантов."""
    errors = []

    def check(cond, msg):
        if not cond:
            errors.append(msg)
        return cond

    data = path.read_bytes()
    size = len(data)
    if not check(size >= LVL_HEADER, f"файл {size} Б, заголовок {LVL_HEADER} Б"):
        return report(path, errors)

    u8 = lambda off: data[off]                                    # noqa: E731
    u16 = lambda off: struct.unpack_from("<H", data, off)[0]      # noqa: E731
    u32 = lambda off: struct.unpack_from("<I", data, off)[0]      # noqa: E731
    f32 = lambda off: struct.unpack_from("<f", data, off)[0]      # noqa: E731

    check(data[0:4] == LVL_MAGIC, f"магия {data[0:4]!r}, нужно {LVL_MAGIC!r}")
    check(u32(4) == LVL_VERSION, f"версия {u32(4)}, нужно {LVL_VERSION}")
    for off, field in ((8, "name"), (24, "palette")):
        raw = data[off:off + 16]
        check(b"\0" in raw, f"{field} без завершающего нуля")
        check(all(32 <= b < 127 for b in raw[:raw.find(b"\0")]),
              f"{field} содержит непечатные байты")
    cells_x, cells_z = u16(40), u16(42)
    check(1 <= cells_x <= MAX_SIDE, f"cells_x = {cells_x}")
    check(1 <= cells_z <= MAX_SIDE, f"cells_z = {cells_z}")
    cell_size, step_y = f32(44), f32(48)
    check(cell_size > 0.0, f"cell_size = {cell_size}")
    check(step_y > 0.0, f"step_y = {step_y}")
    ent_count, link_count, portal_count = u16(64), u16(66), u16(68)
    check(ent_count <= MAX_ENTITIES, f"entity_count = {ent_count} > {MAX_ENTITIES}")
    check(link_count <= MAX_LINKS, f"link_count = {link_count} > {MAX_LINKS}")
    check(portal_count <= MAX_PORTALS, f"portal_count = {portal_count} > {MAX_PORTALS}")
    check(u8(70) <= 3, f"cam_angle = {u8(70)}")
    check(u8(71) <= 1, f"cam_lock = {u8(71)}")
    check(u32(92) == 0, f"резерв в 92 = {u32(92)}, нужно 0")

    tables = (("cells", u32(72), cells_x * cells_z * CELL_SIZE),
              ("entities", u32(76), ent_count * ENTITY_SIZE),
              ("links", u32(80), link_count * LINK_SIZE),
              ("portals", u32(84), portal_count * PORTAL_SIZE))
    for name, off, need in tables:
        check(off % 4 == 0, f"{name}_offset = {off} не кратно 4")
        check(off >= LVL_HEADER, f"{name}_offset = {off} внутри заголовка")
        check(off <= size and need <= size - off,
              f"{name}: [{off}, {off + need}) не влезает в {size} Б")
    if errors:
        return report(path, errors)

    # клетки
    cells_off = u32(72)
    walk = 0
    for i in range(cells_x * cells_z):
        h, flags, seg, pad = struct.unpack_from("<BBBB", data, cells_off + i * CELL_SIZE)
        cx, cz = i % cells_x, i // cells_x
        if not check(pad == 0, f"клетка ({cx}, {cz}): pad = {pad}"):
            break
        if flags & CELL_WALK:
            walk += 1
            check(flags & CELL_EXISTS, f"клетка ({cx}, {cz}): WALK без EXISTS")
        if flags & ~(CELL_EXISTS | CELL_WALK | CELL_STAIR | CELL_WATER | CELL_FLOAT | CELL_SEG):
            errors.append(f"клетка ({cx}, {cz}): неизвестные биты флагов {flags:#x}")
        if not (flags & CELL_EXISTS):
            check(flags == 0 and h == 0 and seg == 0,
                  f"клетка ({cx}, {cz}) без EXISTS, но h={h} flags={flags:#x} seg={seg}")
        seg_id = seg & 0x3F  # CELL_SEG_ID, старшие два бита — требуемое состояние
        check(bool(seg_id) == bool(flags & CELL_SEG),
              f"клетка ({cx}, {cz}): номер сегмента {seg_id}, бит SEG = {bool(flags & CELL_SEG)}")
        if seg_id == 0:
            check(seg == 0, f"клетка ({cx}, {cz}): состояние сегмента без номера (segment = {seg})")
        if flags & CELL_WATER and not (flags & CELL_FLOAT):
            check(not (flags & CELL_WALK), f"клетка ({cx}, {cz}): вода помечена проходимой")

    # сущности
    ent_off = u32(76)
    ids = set()
    type_count = len(load_entity_types())
    for i in range(ent_count):
        fields = struct.unpack_from(ENTITY_FMT, data, ent_off + i * ENTITY_SIZE)
        etype, _flags = fields[0], fields[1]
        eid = fields[-1]
        check(etype < type_count, f"сущность #{i + 1}: тип {etype} >= ENT_COUNT ({type_count})")
        check(eid != 0, f"сущность #{i + 1}: id = 0")
        check(eid not in ids, f"сущность #{i + 1}: id {eid} повторяется")
        ids.add(eid)

    # связи
    link_off = u32(80)
    for i in range(link_count):
        src_id, _so, pad, dst_id, _di, pad2 = struct.unpack_from(LINK_FMT, data, link_off + i * LINK_SIZE)
        check(pad == 0 and pad2 == 0, f"связь #{i + 1}: pad не нулевой")
        check(src_id in ids, f"связь #{i + 1}: src_id {src_id} — нет такой сущности")
        check(dst_id in ids, f"связь #{i + 1}: dst_id {dst_id} — нет такой сущности")

    # порталы
    port_off = u32(84)
    level_count = len(load_level_names())
    for i in range(portal_count):
        cx, cz, pw, ph, target, entry, pad, pad2 = struct.unpack_from(PORTAL_FMT, data, port_off + i * PORTAL_SIZE)
        check(pad == 0 and pad2 == 0, f"портал #{i + 1}: pad не нулевой")
        check(pw >= 1 and ph >= 1, f"портал #{i + 1}: размер {pw}x{ph}")
        check(cx + max(pw, 1) <= cells_x and cz + max(ph, 1) <= cells_z,
              f"портал #{i + 1}: [{cx}, {cz}] {pw}x{ph} вне сетки {cells_x}x{cells_z}")
        check(target < level_count, f"портал #{i + 1}: target_level {target} >= LVL_COUNT ({level_count})")
        check(entry == 0 or entry <= 0xFFFF, f"портал #{i + 1}: entry {entry}")

    # спавн стоит на проходимой клетке
    spawn_x, spawn_z = f32(52), f32(56)
    sx = int((spawn_x + cells_x * cell_size / 2.0) // cell_size)
    sz = int((spawn_z + cells_z * cell_size / 2.0) // cell_size)
    if check(0 <= sx < cells_x and 0 <= sz < cells_z,
             f"спавн ({spawn_x:.2f}, {spawn_z:.2f}) вне сетки"):
        flags = data[cells_off + (sz * cells_x + sx) * CELL_SIZE + 1]
        check(flags & CELL_WALK, f"спавн в клетке ({sx}, {sz}) без WALK (флаги {flags:#x})")

    if not errors:
        print(f"{path.name}: ok — {cells_x}x{cells_z} клеток ({walk} проходимых), "
              f"{ent_count} сущностей, {link_count} связей, {portal_count} порталов, {size} Б")
    return report(path, errors)


def report(path, errors):
    for e in errors:
        print(f"{path.name}: {e}", file=sys.stderr)
    if errors:
        print(f"{path.name}: проверка не прошла ({len(errors)} замечаний)", file=sys.stderr)
        return 1
    return 0


# ------------------------------------------------------------------------ CLI

def main():
    ap = argparse.ArgumentParser(description="компилятор уровней ARGUS", add_help=True)
    ap.add_argument("--headers", metavar="КАТАЛОГ",
                    help="записать entity_types.h и level_ids.h")
    ap.add_argument("--verify", metavar="ФАЙЛ.lvl", help="проверить готовый .lvl")
    ap.add_argument("args", nargs="*", metavar="levels/имя.toml КАТАЛОГ")
    opts = ap.parse_args()

    if opts.verify:
        path = Path(opts.verify)
        if not path.is_file():
            fail(f"нет файла {path}")
        return verify(path)
    if opts.headers:
        write_headers(Path(opts.headers))
        return 0
    if len(opts.args) != 2:
        ap.print_usage(sys.stderr)
        fail("нужно: levelc.py <levels/имя.toml> <каталог_вывода>")
    src, out_dir = Path(opts.args[0]), Path(opts.args[1])
    if not src.is_file():
        fail(f"нет файла {src}")
    if out_dir.exists() and not out_dir.is_dir():
        fail(f"{out_dir} — не каталог")
    compile_level(src, out_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
