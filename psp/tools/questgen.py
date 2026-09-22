#!/usr/bin/env python3
"""questgen.py — assets/quests.toml → quests_data.h (таблица QUESTS[] и QUEST_COUNT).

Использование: python3 tools/questgen.py <каталог_вывода>

Заголовок включает core/quest.h (quest_def_t) и strings_ids.h (STR_*), поэтому собирается
только с -Icore -I<каталог_вывода>. Состояние квестов в файле не хранится — оно выводится
из флагов мира (core/quest.c), здесь только неизменяемые определения.

Проверки (любая — ошибка с номером квеста и код возврата 1): набор полей без лишних ключей,
уникальность giver, диапазоны флагов (id сущностей 1..199, глобальные 204..255 в обход
занятых WFLAG_*), непересечение служебных флагов квестов между собой и с условием,
существование идентификаторов строк в assets/strings.csv.
"""
import argparse
import csv
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "quests.toml"
STRINGS = ROOT / "assets" / "strings.csv"

# Границы из core/world.h: 1..199 — флаги-«сделано» сущностей уровня, 200..255 — глобальные.
ENTITY_FLAG_MIN = 1
ENTITY_FLAG_MAX = 199
# 200..203 заняты WFLAG_ENDING_SEEN_A/B, WFLAG_BONUS_OPEN, WFLAG_INTRO_DONE — не трогаем.
GLOBAL_FLAG_MIN = 204
GLOBAL_FLAG_MAX = 255
# Шесть квестов по GDD; запас на случай бонусных просьб. Таблица статическая, влезает в ROM.
QUEST_MAX = 32

NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
STR_ID_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")

INT_FIELDS = ("giver", "need_flag", "accept_flag", "reward_flag")
STR_FIELDS = ("str_offer", "str_progress", "str_done")
FIELDS = ("name",) + INT_FIELDS + STR_FIELDS


def fail(msg):
    """Ошибка сборки: текст в stderr и код возврата 1."""
    sys.exit(f"questgen: {msg}")


def read_string_ids():
    """Идентификаторы строк из assets/strings.csv (колонка id) — для проверки реплик."""
    if not STRINGS.exists():
        fail(f"нет {STRINGS}")
    with STRINGS.open("r", encoding="utf-8-sig", newline="") as f:
        reader = csv.DictReader(f)
        if not reader.fieldnames or reader.fieldnames[0] != "id":
            fail(f"{STRINGS}: первая колонка должна называться id, а не {reader.fieldnames}")
        ids = {(row.get("id") or "").strip() for row in reader}
    ids.discard("")
    if not ids:
        fail(f"{STRINGS}: нет ни одного идентификатора строки")
    return ids


def read_int(quest, where, key):
    """Целое поле квеста: bool отсекаем отдельно — в TOML true тоже «целое» для isinstance."""
    value = quest[key]
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{where}: {key} должно быть целым, а не {value!r}")
    return value


def read_str(quest, where, key, string_ids):
    """Идентификатор строки без префикса STR_; должен существовать в strings.csv."""
    value = quest[key]
    if not isinstance(value, str):
        fail(f"{where}: {key} должно быть строкой-идентификатором, а не {value!r}")
    value = value.strip()
    if not STR_ID_RE.match(value):
        fail(f"{where}: {key} = {value!r} не подходит под ^[A-Z][A-Z0-9_]*$ "
             f"(идентификатор из assets/strings.csv без префикса STR_)")
    if value not in string_ids:
        fail(f"{where}: {key} = {value!r} — такой строки нет в {STRINGS}; "
             f"добавь её в strings.csv (колонки id, ru, en) или поправь квест")
    return value


def in_range(where, key, value, low, high, what):
    if not (low <= value <= high):
        fail(f"{where}: {key} = {value} вне диапазона {low}..{high} ({what})")


def read_quests():
    """Читает и проверяет assets/quests.toml. Возвращает список словарей в порядке файла."""
    if not SRC.exists():
        fail(f"нет {SRC}")
    try:
        data = tomllib.loads(SRC.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as err:
        fail(f"{SRC}: не разбирается как TOML: {err}")

    unknown_top = sorted(k for k in data if k != "quest")
    if unknown_top:
        fail(f"{SRC}: лишние ключи верхнего уровня: {', '.join(unknown_top)} (ожидается [[quest]])")
    quests = data.get("quest")
    if not isinstance(quests, list) or not quests:
        fail(f"{SRC}: нет ни одной таблицы [[quest]]")
    if len(quests) > QUEST_MAX:
        fail(f"{SRC}: {len(quests)} квестов, максимум {QUEST_MAX}")

    string_ids = read_string_ids()
    out = []
    seen_giver, seen_name, seen_flag = {}, {}, {}
    for index, quest in enumerate(quests):
        where = f"{SRC}: quest[{index}]"
        if not isinstance(quest, dict):
            fail(f"{where}: ожидается таблица [[quest]]")
        missing = [k for k in FIELDS if k not in quest]
        if missing:
            fail(f"{where}: нет обязательных полей: {', '.join(missing)}")
        extra = sorted(k for k in quest if k not in FIELDS)
        if extra:
            fail(f"{where}: лишние поля: {', '.join(extra)} (ожидаются {', '.join(FIELDS)})")

        name = quest["name"]
        if not isinstance(name, str) or not NAME_RE.match(name):
            fail(f"{where}: name = {name!r} не подходит под ^[a-z][a-z0-9_]*$")
        where = f"{SRC}: quest[{index}] {name}"
        if name in seen_name:
            fail(f"{where}: имя уже занято квестом [{seen_name[name]}]")
        seen_name[name] = index

        values = {"name": name}
        for key in INT_FIELDS:
            values[key] = read_int(quest, where, key)
        for key in STR_FIELDS:
            values[key] = read_str(quest, where, key, string_ids)

        in_range(where, "giver", values["giver"], ENTITY_FLAG_MIN, ENTITY_FLAG_MAX,
                 "id сущности-отголоска на уровне")
        in_range(where, "need_flag", values["need_flag"], ENTITY_FLAG_MIN, ENTITY_FLAG_MAX,
                 "id сущности, выполнение — её сбор или активация")
        for key in ("accept_flag", "reward_flag"):
            in_range(where, key, values[key], GLOBAL_FLAG_MIN, GLOBAL_FLAG_MAX,
                     "глобальный флаг мира; 200..203 заняты WFLAG_*")

        if values["giver"] in seen_giver:
            fail(f"{where}: giver = {values['giver']} уже у квеста "
                 f"[{seen_giver[values['giver']]}] — quest_find нашёл бы первый")
        seen_giver[values["giver"]] = index
        if values["need_flag"] == values["giver"]:
            fail(f"{where}: need_flag совпадает с giver ({values['giver']}) — "
                 f"условием не может быть сам отголосок")
        if values["accept_flag"] == values["reward_flag"]:
            fail(f"{where}: accept_flag и reward_flag равны ({values['accept_flag']}) — "
                 f"состояния ACTIVE и DONE стали бы неразличимы")
        for key in ("accept_flag", "reward_flag"):
            flag = values[key]
            if flag in seen_flag:
                owner_index, owner_key = seen_flag[flag]
                fail(f"{where}: {key} = {flag} уже занят как {owner_key} квеста [{owner_index}] — "
                     f"служебные флаги квестов должны быть свои")
            seen_flag[flag] = (index, key)

        out.append(values)
    return out


def build_header(quests):
    """Текст quests_data.h: таблица QUESTS[] в порядке файла и QUEST_COUNT."""
    lines = [
        "/* quests_data.h — сгенерировано tools/questgen.py из assets/quests.toml.",
        " * НЕ РЕДАКТИРОВАТЬ: правки пропадут при следующем `make assets`. */",
        "#ifndef ARGUS_QUESTS_DATA_H",
        "#define ARGUS_QUESTS_DATA_H",
        '#include "quest.h"',
        '#include "strings_ids.h"',
        "",
        f"#define QUEST_COUNT {len(quests)}",
        "",
        "/* Порядок совпадает с порядком [[quest]] в assets/quests.toml.",
        " * Состояние выводится из флагов мира — в таблице только неизменяемые определения. */",
        "static const quest_def_t QUESTS[QUEST_COUNT] = {",
    ]
    for quest in quests:
        lines.append(f"    {{ /* {quest['name']} */")
        lines.append(f"        .giver_id = {quest['giver']}, .need_flag = {quest['need_flag']},")
        lines.append(f"        .reward_flag = {quest['reward_flag']}, "
                     f".accept_flag = {quest['accept_flag']},")
        lines.append(f"        .str_offer = STR_{quest['str_offer']}, "
                     f".str_progress = STR_{quest['str_progress']},")
        lines.append(f"        .str_done = STR_{quest['str_done']},")
        lines.append("    },")
    lines += [
        "};",
        "",
        "/* Доступ к таблице одной строкой; заодно снимает -Wunused-const-variable",
        " * в единицах трансляции, которые заголовок включают, а QUESTS не трогают. */",
        "static inline const quest_def_t *quests_table(int *count) {",
        "    if (count) *count = QUEST_COUNT;",
        "    return QUESTS;",
        "}",
        "",
        "#endif /* ARGUS_QUESTS_DATA_H */",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="quests.toml → quests_data.h")
    parser.add_argument("out_dir", type=Path, help="каталог вывода (обычно build/assets)")
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    quests = read_quests()
    dst = args.out_dir / "quests_data.h"
    dst.write_text(build_header(quests), encoding="utf-8")
    print(f"quests: {len(quests)} квестов → {dst.name} "
          f"(отголоски: {', '.join(str(q['giver']) for q in quests)})")


if __name__ == "__main__":
    main()
