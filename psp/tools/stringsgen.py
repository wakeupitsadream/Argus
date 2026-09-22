#!/usr/bin/env python3
"""stringsgen.py — assets/strings.csv (+ фрагменты assets/strings/*.csv) → strings_ru.bin, strings_en.bin (ASTR v1) и strings_ids.h.

Использование: python3 tools/stringsgen.py <каталог_вывода>

Формат ASTR v1 (docs/FORMATS.md): "ASTR", u32 версия, u32 count, u32 резерв,
далее u32 offsets[count] от начала файла, далее UTF-8 строки с завершающим нулём.
Порядок строк = порядок строк CSV = порядок STR_* в strings_ids.h.

Проверки (любая — ошибка с номером строки CSV и код возврата 1): уникальность и вид
идентификатора, непустые ru и en, одинаковая последовательность спецификаторов printf
в обеих колонках (иначе на приставке printf получит аргументы не того типа).
"""
import argparse
import csv
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "strings.csv"
# Фрагменты по регионам: каждый остров дописывает свои строки отдельным файлом,
# чтобы параллельная работа над контентом не сводилась к правкам одного CSV.
# Порядок: сначала базовый strings.csv, потом фрагменты по алфавиту имён файлов.
FRAGMENTS = ROOT / "assets" / "strings"

MAGIC = b"ASTR"
VERSION = 1
HDR_FMT = "<4sIII"  # magic, версия, count, резерв
HDR_SIZE = 16
LANGS = ("ru", "en")
COLUMNS = ("id",) + LANGS
U32_MAX = 0xFFFFFFFF

if struct.calcsize(HDR_FMT) != HDR_SIZE:
    raise SystemExit(f"stringsgen: заголовок {struct.calcsize(HDR_FMT)} байт, нужно {HDR_SIZE}")

ID_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
# Спецификатор printf: %% либо %[флаги][ширина][.точность][длина]конверсия.
SPEC_RE = re.compile(r"%(?:(%)|[-+ #0]*[0-9]*(?:\.[0-9]+)?(hh|h|ll|l|z|t|j|L)?([diouxXeEfgGcsp]))")


def fail(line, msg, src=None):
    """Ошибка с привязкой к строке CSV; sys.exit печатает в stderr и выходит с кодом 1."""
    sys.exit(f"{src or SRC}:{line}: {msg}")


def printf_specs(text):
    """Спецификаторы printf по порядку, нормализованные до «длина + конверсия» (%d, %lu).

    Ширина и точность отбрасываются: они не меняют тип аргумента, и '%d' против '%3d'
    в переводе законно. Незнакомый спецификатор (в том числе %n) — ValueError.
    """
    specs = []
    pos = 0
    while True:
        at = text.find("%", pos)
        if at < 0:
            return specs
        m = SPEC_RE.match(text, at)
        if not m:
            raise ValueError(f"непонятный спецификатор printf: {text[at:at + 8]!r}")
        pos = m.end()
        if m.group(1) is None:  # '%%' — это экранированный процент, не аргумент
            specs.append("%" + (m.group(2) or "") + m.group(3))


def read_file(path, rows, seen):
    """Дочитывает один CSV в общий список. seen — идентификаторы со ссылкой на источник."""
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f, restkey="_extra")
        if reader.fieldnames is None:
            sys.exit(f"stringsgen: {path} пуст")
        if tuple(reader.fieldnames) != COLUMNS:
            fail(1, f"колонки {reader.fieldnames}, нужно {list(COLUMNS)}", path)

        for row in reader:  # пустые строки csv пропускает сам
            line = reader.line_num
            if row.get("_extra"):
                fail(line, f"лишние колонки: {row['_extra']}", path)
            sid = (row.get("id") or "").strip()
            if not sid:
                fail(line, "пустой идентификатор", path)
            if not ID_RE.match(sid):
                fail(line, f"идентификатор {sid!r} не подходит под ^[A-Z][A-Z0-9_]*$", path)
            if sid in seen:
                where = seen[sid]
                fail(line, f"идентификатор {sid!r} уже был в {where[0]}:{where[1]}", path)
            seen[sid] = (path.name, line)

            values, specs = {}, {}
            for lang in LANGS:
                value = (row.get(lang) or "").strip()
                if not value:
                    fail(line, f"{sid}: пустая колонка {lang}", path)
                try:
                    specs[lang] = printf_specs(value)
                except ValueError as err:
                    fail(line, f"{sid}, колонка {lang}: {err}", path)
                values[lang] = value
            if specs["ru"] != specs["en"]:
                fail(line, f"{sid}: спецификаторы printf расходятся — "
                           f"ru {specs['ru'] or 'нет'}, en {specs['en'] or 'нет'}", path)
            rows.append((sid, values))


def read_rows():
    """Читает базовый CSV и фрагменты assets/strings/*.csv. Возвращает [(id, {ru, en})]."""
    if not SRC.exists():
        sys.exit(f"stringsgen: нет {SRC}")
    rows, seen = [], {}
    read_file(SRC, rows, seen)
    if FRAGMENTS.is_dir():
        for frag in sorted(FRAGMENTS.glob("*.csv")):
            read_file(frag, rows, seen)
    if not rows:
        sys.exit(f"stringsgen: в {SRC} нет ни одной строки")
    return rows


def build_blob(values):
    """Собирает файл ASTR из списка строк в порядке идентификаторов."""
    count = len(values)
    data_off = HDR_SIZE + 4 * count
    offsets, data = [], bytearray()
    for value in values:
        offsets.append(data_off + len(data))
        data += value.encode("utf-8") + b"\0"
    if offsets and offsets[-1] > U32_MAX:
        sys.exit("stringsgen: таблица строк больше 4 ГБ")
    blob = struct.pack(HDR_FMT, MAGIC, VERSION, count, 0)
    blob += struct.pack(f"<{count}I", *offsets)
    return blob + bytes(data)


def build_header(ids):
    """Текст strings_ids.h: enum STR_* в порядке CSV и финальный STR_COUNT."""
    width = max(len(sid) for sid in ids) + 5  # 'STR_' + идентификатор + пробел
    lines = [
        "/* strings_ids.h — сгенерировано tools/stringsgen.py из assets/strings.csv и фрагментов assets/strings.",
        " * НЕ РЕДАКТИРОВАТЬ: правки пропадут при следующем `make assets`. */",
        "#ifndef ARGUS_STRINGS_IDS_H",
        "#define ARGUS_STRINGS_IDS_H",
        "",
        "/* Порядок совпадает с порядком строк в strings_<lang>.bin. */",
        "enum {",
    ]
    for index, sid in enumerate(ids):
        lines.append(f"    {('STR_' + sid + ' ').ljust(width)}= {index},")
    lines.append(f"    {'STR_COUNT '.ljust(width)}= {len(ids)}")
    lines += ["};", "", "#endif", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="strings.csv → strings_<lang>.bin + strings_ids.h")
    parser.add_argument("out_dir", type=Path, help="каталог вывода (обычно build/assets)")
    args = parser.parse_args()
    out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = read_rows()
    ids = [sid for sid, _ in rows]

    sizes = []
    for lang in LANGS:
        blob = build_blob([values[lang] for _, values in rows])
        dst = out_dir / f"strings_{lang}.bin"
        dst.write_bytes(blob)
        sizes.append(f"{dst.name} {len(blob)} Б")

    header = out_dir / "strings_ids.h"
    header.write_text(build_header(ids), encoding="utf-8")
    sizes.append(f"{header.name} {len(ids)} идентификаторов")
    print(f"strings: {len(ids)} строк, " + ", ".join(sizes))


if __name__ == "__main__":
    main()
