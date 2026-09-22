/* puzzle_mech.c — механические семейства головоломок (GDD §1.4): сокобан-блоки и плиты,
 * рычаги, уровень воды с плавучими блоками, панели памяти, вращающиеся сегменты.
 * Луч, зеркала и призмы — в core/puzzle_beam.c.
 * Поведение строго детерминированное: ни времени, ни rand, ни malloc (CLAUDE.md, п.10 и 12).
 *
 * ——— соглашение по анимациям (важно для рендера и для отсутствия провалов) ———
 * entities_tick продвигает единственный твин сущности (e->anim) и НЕ переносит его значение
 * в позицию. Поэтому здесь принят один порядок: логика (позиция, состояние, выходы,
 * проходимость) применяется СРАЗУ, а e->anim хранит ту же величину «догоняющей» — только
 * для отрисовки. Иначе игрок успел бы провалиться в клетку сегмента посреди анимации,
 * а плита не досчитала бы блок, который «ещё едет».
 * Что лежит в e->anim по типам:
 *   ENT_BLOCK       — мировая координата вдоль оси толчка; ось и знак задаёт e->yaw
 *                     (0 → +x, 90 → +z, 180 → -x, 270 → -z);
 *   ENT_FLOAT_BLOCK — мировая высота y;
 *   ENT_LEVER       — положение рукоятки 0..1 (равно state);
 *   ENT_PLATE       — утопленность 0..1;
 *   ENT_WATER_VALVE — уровень воды в шагах (положение колеса);
 *   ENT_MEMORY_PANEL— прогресс 0..1;
 *   ENT_SEGMENT     — накопленный доворот в градусах относительно def->yaw_deg
 *                     (растёт и убывает монотонно, поэтому нет прыжка при 3 → 0).
 */
#include "puzzles.h"
#include "entity_types.h"
#include "walk.h"

/* Длительности в кадрах при фиксированном шаге логики 1/60 с. */
#define MECH_BLOCK_FRAMES 18 /* 0,3 с — толчок блока */
#define MECH_LEVER_FRAMES 12 /* 0,2 с — рукоятка рычага, панель памяти */
#define MECH_PLATE_FRAMES 6  /* 0,1 с — плита */
#define MECH_WATER_FRAMES 24 /* 0,4 с — вода и плавучие блоки */
#define MECH_SEG_FRAMES 36   /* 0,6 с — поворот сегмента */

#define MECH_SEG_STEP_DEG 90.0f
#define MECH_MEM_MAX 4      /* params[0..3] — ожидаемая последовательность */
#define MECH_MEM_LEN_IDX 4  /* params[4] — длина (0 — считать по ненулевым) */
#define MECH_WATER_MAX 255  /* высота клетки хранится в байте */
#define MECH_NO_FLOOR (-1.0e8f)

/* ——— мелкие помощники ——— */

static int mech_type(const entity_t *e) { return e->def ? (int)e->def->type : ENT_NONE; }
static int mech_id(const entity_t *e) { return e->def ? (int)e->def->id : 0; }

/* Клетка под сущностью; 0 — вне сетки. */
static int mech_cell_of(const level_t *l, const entity_t *e, int *cx, int *cz) {
    return level_cell_at(l, e->x, e->z, cx, cz);
}

/* Готовы ли принять ввод: есть уровень и не идёт анимация. */
static int mech_ready(const entities_t *es) {
    return es && es->level && es->busy_frames <= 0;
}

/* Мешает ли сущность встать блоку в её клетку. Открытая дверь — проход. */
static int mech_is_obstacle(int type, int state) {
    switch (type) {
    case ENT_DOOR:
        return state ? 0 : 1;
    case ENT_BLOCK:
    case ENT_FLOAT_BLOCK:
    case ENT_MIRROR:
    case ENT_PRISM:
    case ENT_EMITTER:
    case ENT_RECEIVER:
    case ENT_LEVER:
    case ENT_MEMORY_PANEL:
    case ENT_WATER_VALVE:
    case ENT_SLEEP_WALL:
    case ENT_PLINTH:
    case ENT_STONE_TEXT:
    case ENT_BIG_EYE:
    case ENT_PEACOCK_TAIL:
    case ENT_ECHO:
        return 1;
    default:
        /* Спавн, плита, триггер, малый глаз, перо, спящий, маркер сегмента — не мешают. */
        return 0;
    }
}

/* Занята ли клетка препятствием (сущность skip_id не учитывается). */
static int mech_cell_occupied(const entities_t *es, int cx, int cz, int skip_id) {
    for (int i = 0; i < es->count; i++) {
        const entity_t *e = &es->items[i];
        if (!e->active || mech_id(e) == skip_id) continue;
        if (!mech_is_obstacle(mech_type(e), e->state)) continue;
        int ex = 0, ez = 0;
        if (!level_cell_at(es->level, e->x, e->z, &ex, &ez)) continue;
        if (ex == cx && ez == cz) return 1;
    }
    return 0;
}

/* Стоит ли сущность типа type в клетке (cx, cz). */
static int mech_type_in_cell(const entities_t *es, int type, int cx, int cz) {
    for (int i = 0; i < es->count; i++) {
        const entity_t *e = &es->items[i];
        if (!e->active || mech_type(e) != type) continue;
        int ex = 0, ez = 0;
        if (!level_cell_at(es->level, e->x, e->z, &ex, &ez)) continue;
        if (ex == cx && ez == cz) return 1;
    }
    return 0;
}

/* Ставит выход 0 сущности; возвращает 1, если значение изменилось. */
static int mech_set_out0(entity_t *e, int on) {
    int was = (e->outputs & 1u) ? 1 : 0;
    if (was == (on ? 1 : 0)) return 0;
    e->outputs = (unsigned char)((e->outputs & ~1u) | (on ? 1u : 0u));
    return 1;
}

/* Привязывает визуальный твин к логическому значению, если он в покое.
 * Нужно после entities_init (там anim = 0, а позиция взята из данных уровня). */
static void mech_anim_bind(entity_t *e, float logical) {
    if (tween_done(&e->anim)) tween_set(&e->anim, logical);
}

/* ——— блоки (сокобан) ——— */

int block_push(entities_t *es, int entity_id, int dx, int dz) {
    if (!mech_ready(es)) return 0; /* идёт анимация — новые толчки запрещены */
    if (dx < -1 || dx > 1 || dz < -1 || dz > 1) return 0;
    if ((dx != 0) == (dz != 0)) return 0; /* ровно одна ось: ни по диагонали, ни на месте */

    entity_t *e = entities_by_id(es, entity_id);
    if (!e || !e->active || mech_type(e) != ENT_BLOCK) return 0;

    const level_t *l = es->level;
    int cx = 0, cz = 0;
    if (!mech_cell_of(l, e, &cx, &cz)) return 0;
    int tx = cx + dx, tz = cz + dz;
    if (!walk_is_walkable(l, tx, tz)) return 0; /* стена, дыра, вода, закрытый сегмент */

    const level_cell_t *from = level_cell(l, cx, cz);
    const level_cell_t *to = level_cell(l, tx, tz);
    if (!from || !to) return 0;
    int dh = (int)to->height - (int)from->height;
    if (dh < -1 || dh > 1) return 0; /* перепад больше одного шага step_y */
    if (mech_cell_occupied(es, tx, tz, entity_id)) return 0;

    float nx = 0.0f, nz = 0.0f;
    level_cell_center(l, tx, tz, &nx, &nz);
    float from_axis = (dx != 0) ? e->x : e->z; /* анимируем ту ось, по которой идём */
    float to_axis = (dx != 0) ? nx : nz;

    /* Логика — сразу и строго в центр клетки: плиты и трассировка луча видят блок там же. */
    e->x = nx;
    e->z = nz;
    e->y = (float)to->height * l->step_y;
    e->yaw = (dx > 0) ? 0.0f : (dx < 0 ? 180.0f : (dz > 0 ? 90.0f : 270.0f));

    tween_set(&e->anim, from_axis);
    tween_start(&e->anim, to_axis, MECH_BLOCK_FRAMES);
    es->busy_frames = MECH_BLOCK_FRAMES;
    return 1;
}

/* ——— рычаги ——— */

int lever_toggle(entities_t *es, int entity_id) {
    if (!mech_ready(es)) return 0;
    entity_t *e = entities_by_id(es, entity_id);
    if (!e || !e->active || mech_type(e) != ENT_LEVER) return 0;

    e->state = (unsigned char)(e->state ^ 1u);
    mech_set_out0(e, e->state);
    mech_anim_bind(e, (float)(e->state ^ 1u));
    tween_start(&e->anim, (float)e->state, MECH_LEVER_FRAMES);
    /* Ввод не блокируем — рукоятка доводится только визуально.
     * Разнос сигналов делает вызывающий (entities_propagate), как обещает entity.h. */
    return 1;
}

/* ——— плиты ——— */

void plates_update(entities_t *es, float px, float pz) {
    if (!es || !es->level) return;
    const level_t *l = es->level;
    int pcx = 0, pcz = 0;
    int player_in = level_cell_at(l, px, pz, &pcx, &pcz);

    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        if (!e->active || mech_type(e) != ENT_PLATE) continue;
        int cx = 0, cz = 0;
        if (!mech_cell_of(l, e, &cx, &cz)) continue;

        /* Сравниваем по клетке, а не по расстоянию: блок всегда в центре клетки. */
        int pressed = (player_in && pcx == cx && pcz == cz) ? 1 : 0;
        if (!pressed) pressed = mech_type_in_cell(es, ENT_BLOCK, cx, cz);

        if (!mech_set_out0(e, pressed)) continue; /* без изменений — не дёргаем анимацию */
        e->state = (unsigned char)pressed;
        mech_anim_bind(e, (float)(pressed ^ 1));
        tween_start(&e->anim, (float)pressed, MECH_PLATE_FRAMES);
    }
}

/* ——— вода ——— */

void water_set_level(entities_t *es, int steps) {
    if (!es || !es->level) return;
    if (steps < 0) steps = 0;
    if (steps > MECH_WATER_MAX) steps = MECH_WATER_MAX;

    const level_t *l = es->level;
    es->water_steps = steps;
    float water_y = (float)steps * l->step_y;

    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        if (!e->active || mech_type(e) != ENT_FLOAT_BLOCK) continue;

        int cx = 0, cz = 0;
        float floor_y = 0.0f;
        if (mech_cell_of(l, e, &cx, &cz)) {
            float top = level_cell_top(l, cx, cz);
            if (top > MECH_NO_FLOOR) floor_y = top;
        }
        /* Всплыл до уровня воды, но не ниже собственной клетки. */
        float target = water_y > floor_y ? water_y : floor_y;
        mech_anim_bind(e, e->y);
        if (target == e->y) continue;
        e->y = target;
        tween_start(&e->anim, target, MECH_WATER_FRAMES);
    }
}

int water_valve_use(entities_t *es, int entity_id) {
    if (!mech_ready(es)) return 0;
    entity_t *e = entities_by_id(es, entity_id);
    if (!e || !e->active || mech_type(e) != ENT_WATER_VALVE) return 0;

    int lo = e->def->params[0], hi = e->def->params[1];
    if (lo < 0) lo = 0;
    if (hi < lo) hi = lo; /* испорченные данные: шлюз с единственным уровнем */

    int cur = es->water_steps;
    int next = (cur < lo || cur >= hi) ? lo : cur + 1; /* после максимума — назад к минимуму */

    water_set_level(es, next);
    e->state = (unsigned char)next;
    mech_anim_bind(e, (float)cur);
    tween_start(&e->anim, (float)next, MECH_WATER_FRAMES);
    es->last_event = entity_id;
    return 1;
}

/* ——— панель памяти ——— */

int memory_input(entities_t *es, int entity_id, int value) {
    /* Занятость намеренно не проверяем: 0 здесь означает «ошибка, прогресс сброшен»,
     * и возвращать его из-за идущей анимации нельзя. Ввод гасит вызывающий. */
    if (!es || !es->level) return 0;
    entity_t *e = entities_by_id(es, entity_id);
    if (!e || !e->active || mech_type(e) != ENT_MEMORY_PANEL) return 0;

    const int *p = e->def->params;
    int len = p[MECH_MEM_LEN_IDX];
    if (len <= 0) {
        /* Длина не задана — считаем ненулевые params[0..3] (нуль закрывает список). */
        len = 0;
        while (len < MECH_MEM_MAX && p[len] != 0) len++;
    }
    if (len > MECH_MEM_MAX) len = MECH_MEM_MAX;
    if (len <= 0) return 0; /* последовательности нет — вводить нечего */

    if (e->outputs & 1u) return 2; /* уже собрана: повторный ввод ничего не меняет */

    int step = e->state < (unsigned char)len ? (int)e->state : 0;
    if (value != p[step]) {
        e->state = 0; /* ошибка — вводить заново с начала */
        mech_anim_bind(e, (float)step / (float)len);
        tween_start(&e->anim, 0.0f, MECH_LEVER_FRAMES);
        return 0;
    }

    step++;
    e->state = (unsigned char)step;
    mech_anim_bind(e, (float)(step - 1) / (float)len);
    tween_start(&e->anim, (float)step / (float)len, MECH_LEVER_FRAMES);
    if (step < len) return 1;
    mech_set_out0(e, 1); /* последнее верное значение включает выход панели */
    return 2;
}

/* ——— вращающиеся сегменты ——— */

int segment_rotate(entities_t *es, int segment_id, int dir) {
    if (!mech_ready(es)) return 0;
    if (segment_id <= 0 || segment_id >= LEVEL_MAX_SEGMENTS) return 0; /* 0 — статика */

    int turn = dir % 4; /* знак сохраняем: визуально крутим в сторону dir */
    if (turn == 0) return 0;
    int step = (turn + 4) % 4;

    /* seg_states лежит внутри level_t, но это состояние игры, а не данные файла
     * (docs/FORMATS.md, «Вращающиеся сегменты»). Контракт даёт const level_t *,
     * поэтому const снимается только ради этого поля — как в core/entity.c. */
    level_t *mut = (level_t *)(void *)es->level;
    int prev = mut->seg_states[segment_id] & 3;

    /* ВАЖНО: состояние применяем сразу, до анимации. Проходимость клеток сегмента
     * (walk_is_walkable) меняется мгновенно, анимация — только визуал: иначе игрок
     * успел бы провалиться в клетку, которая «ещё поворачивается». Ввод на эти 0,6 с
     * всё равно закрыт через busy_frames, так что рассинхрона не видно. */
    mut->seg_states[segment_id] = (unsigned char)((prev + step) & 3);
    es->busy_frames = MECH_SEG_FRAMES;

    /* Маркер ENT_SEGMENT необязателен: сегмент может крутить рычаг по связи.
     * Номер сегмента — params[0], а при нуле — сам id сущности. */
    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        if (!e->active || mech_type(e) != ENT_SEGMENT) continue;
        int owned = e->def->params[0] > 0 ? e->def->params[0] : mech_id(e);
        if (owned != segment_id) continue;
        e->state = mut->seg_states[segment_id];
        e->yaw = e->def->yaw_deg + MECH_SEG_STEP_DEG * (float)e->state;
        tween_start(&e->anim, e->anim.value + MECH_SEG_STEP_DEG * (float)turn, MECH_SEG_FRAMES);
    }
    return 1;
}

/* ——— условие «уровень пройден» ——— */

int puzzles_all_solved(const entities_t *es) {
    if (!es) return 0;
    for (int i = 0; i < es->count; i++) {
        const entity_t *e = &es->items[i];
        if (!e->active) continue;
        switch (mech_type(e)) {
        case ENT_RECEIVER:
            /* Приёмник засчитан, если луч пришёл (выход) или сигнал дошёл по связи (вход). */
            if (!(e->outputs & 1u) && !(e->inputs & 1u)) return 0;
            break;
        case ENT_BIG_EYE:
            if (!e->state) return 0;
            break;
        default:
            break;
        }
    }
    return 1; /* нет ни приёмников, ни больших глаз — решать нечего */
}
