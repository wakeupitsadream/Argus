/* entity.c — живое состояние сущностей уровня: сигналы, связи, взаимодействие, анимация.
 * Определения приходят из level_t (данные файла), здесь только то, что меняется в игре.
 * Поведение строго детерминированное: ни времени, ни rand, ни malloc (CLAUDE.md, п.10 и 12). */
#include "entity.h"
#include "entity_types.h"
#include "math3.h"
#include "observe.h"
#include "platform.h"
#include "puzzles.h"
#include <math.h>
#include <string.h>

#define ENT_DT (1.0f / 60.0f)    /* фиксированный шаг логики */
#define ENT_HOVER_RATE 12.0f     /* град/с — общая фаза парения */
#define ENT_SLEEPER_RATE 30.0f   /* град/с — скорость спящего по умолчанию */
#define ENT_SLEEPER_RADIUS 1.0f  /* клеток — радиус обхода по умолчанию */
#define ENT_SLEEPER_DIV 10.0f    /* params[0] задаёт радиус в десятых клетки */
#define ENT_WATCH_RADIUS 0.5f    /* радиус сферы для проверки наблюдения */
#define ENT_WATCH_Y 0.4f         /* центр объекта над его опорной точкой */
#define ENT_DIR_EPS 1.0e-4f

/* ——— мелкие помощники ——— */

static int ent_type(const entity_t *e) { return e->def ? (int)e->def->type : ENT_NONE; }
static int ent_id(const entity_t *e) { return e->def ? (int)e->def->id : 0; }

static float wrap_deg(float a) {
    a = fmodf(a, 360.0f);
    return a < 0.0f ? a + 360.0f : a;
}

/* Флаги мира 1..199 соответствуют id сущностей (world.h); мир может быть не задан. */
static int ent_flag_get(const entities_t *es, int id) {
    if (!es->world || id <= 0 || id > WORLD_ENTITY_FLAG_MAX) return 0;
    return world_flag(es->world, id) ? 1 : 0;
}

static void ent_flag_set(entities_t *es, int id) {
    if (!es->world || id <= 0 || id > WORLD_ENTITY_FLAG_MAX) return;
    world_set_flag(es->world, id, 1);
}

/* seg_states лежит внутри level_t, но это состояние игры, а не данные файла
 * (docs/FORMATS.md, «Вращающиеся сегменты»). Контракт даёт нам const level_t *,
 * поэтому const снимается только ради этого поля. */
static void ent_seg_reset(entities_t *es) {
    if (!es->level) return;
    level_t *mut = (level_t *)(void *)es->level;
    memset(mut->seg_states, 0, sizeof mut->seg_states);
}

/* Поддерживает ли тип взаимодействие «крестом». */
int entity_can_interact(int type) {
    switch (type) {
    case ENT_LEVER:
    case ENT_MIRROR:
    case ENT_PRISM:
    case ENT_BLOCK:
    case ENT_SMALL_EYE:
    case ENT_FEATHER:
    case ENT_WATER_VALVE:
    case ENT_MEMORY_PANEL:
    case ENT_SEGMENT:
    case ENT_ECHO:
    case ENT_STONE_TEXT:
        return 1;
    default:
        return 0;
    }
}

/* Округляет направление взгляда до одной из четырёх осей. Если направление нулевое,
 * толкаем от игрока — как обещает комментарий в entity.h. */
static void ent_push_dir(const entity_t *e, float px, float pz,
                         float dir_x, float dir_z, int *out_x, int *out_z) {
    float ax = dir_x, az = dir_z;
    *out_x = 0;
    *out_z = 0;
    if (fabsf(ax) < ENT_DIR_EPS && fabsf(az) < ENT_DIR_EPS) {
        ax = e->x - px;
        az = e->z - pz;
    }
    if (fabsf(ax) < ENT_DIR_EPS && fabsf(az) < ENT_DIR_EPS) return;
    if (fabsf(ax) >= fabsf(az)) *out_x = ax > 0.0f ? 1 : -1;
    else *out_z = az > 0.0f ? 1 : -1;
}

/* ——— поиск ——— */

entity_t *entities_by_id(entities_t *es, int id) {
    if (!es || id <= 0) return NULL;
    for (int i = 0; i < es->count; i++) {
        if (ent_id(&es->items[i]) == id) return &es->items[i];
    }
    return NULL;
}

const entity_t *entities_by_id_const(const entities_t *es, int id) {
    if (!es || id <= 0) return NULL;
    for (int i = 0; i < es->count; i++) {
        if (ent_id(&es->items[i]) == id) return &es->items[i];
    }
    return NULL;
}

int entities_eyes_open(const entities_t *es) {
    int n = 0;
    for (int i = 0; i < es->count; i++) {
        if (ent_type(&es->items[i]) == ENT_BIG_EYE && es->items[i].state) n++;
    }
    return n;
}

/* ——— связи ——— */

void entities_propagate(entities_t *es) {
    for (int i = 0; i < es->count; i++) es->items[i].inputs = 0;

    const level_t *l = es->level;
    if (l && l->links) {
        /* Один проход за вызов: длинные цепочки догоняют за несколько кадров,
         * зато циклы в данных не могут подвесить игру. */
        for (int i = 0; i < l->link_count; i++) {
            const level_link_t *k = &l->links[i];
            if (k->src_out >= ENT_OUT_BITS || k->dst_in >= ENT_IN_BITS) continue;
            entity_t *src = entities_by_id(es, k->src_id);
            entity_t *dst = entities_by_id(es, k->dst_id);
            if (!src || !dst || !src->active) continue; /* неизвестные id — пропускаем */
            if (src->outputs & (unsigned char)(1u << k->src_out)) {
                dst->inputs |= (unsigned char)(1u << k->dst_in);
            }
        }
    }

    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        switch (ent_type(e)) {
        case ENT_DOOR: {
            e->state = e->inputs ? 1 : 0; /* открыта, пока активен любой вход */
            /* Ходьба знает только сетку, поэтому дверь физически запирает проход
             * через состояние сегмента: её клетки помечены CELL_SEG с номером
             * params[0] и требуемым состоянием 1 (docs/FORMATS.md, «Двери»). */
            int seg = e->def ? e->def->params[0] : 0;
            if (seg > 0 && seg < LEVEL_MAX_SEGMENTS && es->level) {
                level_t *mut = (level_t *)(void *)es->level;
                mut->seg_states[seg] = e->state;
            }
            break;
        }
        case ENT_BIG_EYE:
            if ((e->inputs & 1u) && !e->state) {
                int id = ent_id(e);
                e->state = 1; /* открытый глаз больше не закрывается — это прогресс */
                if (!ent_flag_get(es, id)) {
                    ent_flag_set(es, id);
                    /* Счётчик через world_set_counts: там же зажимаются пределы,
                     * иначе испорченные данные уровня раздули бы его за границу. */
                    if (es->world) {
                        world_set_counts(es->world, (int)es->world->eyes_opened + 1,
                                         (int)es->world->small_eyes, (int)es->world->feathers);
                    }
                }
                es->last_event = id; /* игра по событию играет арпеджио и сохраняется */
            }
            break;
        default:
            break;
        }
    }
}

/* ——— инициализация ——— */

void entities_init(entities_t *es, const level_t *l, world_t *w) {
    memset(es, 0, sizeof *es);
    es->level = l;
    es->world = w;
    if (!l) return;

    int skipped = 0;
    for (int i = 0; i < l->entity_count && l->entities; i++) {
        const level_entity_t *def = &l->entities[i];
        if (es->count >= ENT_RUNTIME_MAX) { skipped++; continue; }

        entity_t *e = &es->items[es->count++];
        e->def = def;
        e->x = def->x;
        e->y = def->y;
        e->z = def->z;
        e->yaw = def->yaw_deg;
        /* Зеркало и призма: состояние 0..3 задаёт автор уровня углом в TOML.
         * Иначе любое зеркало стартовало бы в состоянии 0, и авторская расстановка
         * («этот уголок уже повёрнут») молча терялась бы. */
        e->phase = 0.0f;
        tween_set(&e->anim, 0.0f);
        e->state = 0;
        if (def->type == ENT_MIRROR || def->type == ENT_PRISM) {
            int q = (int)((def->yaw_deg < 0.0f ? def->yaw_deg + 1440.0f : def->yaw_deg)
                          / 90.0f + 0.5f);
            e->state = (unsigned char)(q & 3);
        }
        e->outputs = 0;
        e->inputs = 0;
        e->watched = 0;
        e->active = 1;

        if (!ent_flag_get(es, def->id)) continue;
        switch (def->type) {
        case ENT_SMALL_EYE:
        case ENT_FEATHER:
            e->active = 0; /* уже собрано в прошлый раз */
            break;
        case ENT_LEVER:
        case ENT_BIG_EYE:
            e->state = 1;  /* уже переключено / открыто */
            e->outputs = 1;
            break;
        default:
            break;
        }
    }
    if (skipped) {
        plat_log("entity: уровень \"%s\": %d сущностей сверх лимита %d — пропущены",
                 l->name, skipped, ENT_RUNTIME_MAX);
    }

    /* Состояния сегментов в файле не хранятся — начинаем с нуля. */
    ent_seg_reset(es);

    /* Вода стоит на минимуме первого шлюза уровня. */
    es->water_steps = 0;
    for (int i = 0; i < es->count; i++) {
        if (ent_type(&es->items[i]) != ENT_WATER_VALVE) continue;
        int min_steps = es->items[i].def->params[0];
        es->water_steps = min_steps > 0 ? min_steps : 0;
        break;
    }
    /* Уровень воды нужен и walk.c (пол плавучих клеток), поэтому через water_set_level:
     * заодно поднимет плавучие блоки на стартовый уровень. Загрузка — не игровое
     * событие, поэтому анимацию всплытия сразу гасим: плоты уже на месте. */
    water_set_level(es, es->water_steps);
    for (int i = 0; i < es->count; i++) {
        if (ent_type(&es->items[i]) == ENT_FLOAT_BLOCK) tween_set(&es->items[i].anim, es->items[i].y);
    }
    es->busy_frames = 0;

    /* Приводим входы в согласие с восстановленными выходами, чтобы дверь под
     * включённым рычагом была открыта сразу. Загрузка — не игровое событие. */
    entities_propagate(es);
    es->last_event = 0;
}

/* ——— шаг логики ——— */

void entities_tick(entities_t *es, const frame_cam_t *cam, int look_active) {
    if (es->busy_frames > 0) es->busy_frames--;

    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        tween_update(&e->anim, ease_in_out_cubic);
        if (!e->active) { e->watched = 0; continue; }

        float pos[3] = { e->x, e->y + ENT_WATCH_Y, e->z };
        e->watched = (unsigned char)(observe_is_watched(cam, pos, ENT_WATCH_RADIUS, look_active) ? 1 : 0);

        if (ent_type(e) != ENT_SLEEPER) {
            e->phase = wrap_deg(e->phase + ENT_HOVER_RATE * ENT_DT);
            continue;
        }

        /* Фирменная механика (GDD §1.4): под взглядом спящий замирает ровно там, где был. */
        if (e->watched) continue;
        float rate = e->def->params[1] != 0 ? (float)e->def->params[1] : ENT_SLEEPER_RATE;
        e->phase = wrap_deg(e->phase + rate * ENT_DT);

        float cells = e->def->params[0] > 0
                          ? (float)e->def->params[0] / ENT_SLEEPER_DIV
                          : ENT_SLEEPER_RADIUS;
        float r = cells * (es->level ? es->level->cell_size : 1.0f);
        float a = e->phase * M3_DEG2RAD;
        e->x = e->def->x + r * cosf(a); /* круг вокруг стартовой точки определения */
        e->z = e->def->z + r * sinf(a);
    }
}

/* ——— взаимодействие ——— */

int entities_interact(entities_t *es, float px, float pz, float dir_x, float dir_z, float reach) {
    if (es->busy_frames > 0) return 0; /* идёт анимация — ввод заблокирован */

    entity_t *best = NULL;
    float best_d2 = reach > 0.0f ? reach * reach : 0.0f;
    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        if (!e->active || !entity_can_interact(ent_type(e))) continue;
        float dx = e->x - px, dz = e->z - pz;
        float d2 = dx * dx + dz * dz;
        if (d2 < best_d2) { best_d2 = d2; best = e; }
    }
    if (!best) return 0;

    int id = ent_id(best);
    int type = ent_type(best);
    switch (type) {
    case ENT_LEVER:
        best->state = (unsigned char)(best->state ^ 1u);
        best->outputs = (unsigned char)((best->outputs & ~1u) | best->state);
        break;

    case ENT_MIRROR:
    case ENT_PRISM:
        mirror_rotate(es, id); /* состояние, анимация и busy_frames — на core/puzzles.c */
        break;

    case ENT_BLOCK: {
        int dx = 0, dz = 0;
        ent_push_dir(best, px, pz, dir_x, dir_z, &dx, &dz);
        block_push(es, id, dx, dz);
        break;
    }

    case ENT_SMALL_EYE:
    case ENT_FEATHER:
        best->active = 0;
        if (!ent_flag_get(es, id)) {
            ent_flag_set(es, id);
            if (es->world) {
                int se = (int)es->world->small_eyes + (type == ENT_SMALL_EYE ? 1 : 0);
                int fe = (int)es->world->feathers + (type == ENT_SMALL_EYE ? 0 : 1);
                world_set_counts(es->world, (int)es->world->eyes_opened, se, fe);
            }
        }
        break;

    default:
        /* Шлюз, панель памяти, сегмент, отголосок, камень с надписью: решают
         * игра и модули головоломок, увидев last_event. */
        break;
    }

    es->last_event = id;
    return id;
}
