/* puzzle_beam.c — семейство «свет» (GDD §1.3, Сад Зеркал): луч, зеркала, призмы,
 * приёмники и спящие стены. Реализует часть core/puzzles.h: beam_trace и mirror_rotate;
 * остальные семейства живут в core/puzzle_mech.c.
 *
 * Всё детерминировано: ни времени, ни rand, ни malloc (CLAUDE.md, п.10 и 12). Луч идёт
 * по клеткам сетки в четырёх направлениях; состояние объектов читается из entities_t. */
#include "puzzles.h"
#include "entity_types.h"
#include "level.h"
#include <math.h>

/* Направления марша. Порядок задан контрактом: 0 = +X, 1 = +Z, 2 = -X, 3 = -Z. */
enum { BEAM_E = 0, BEAM_S = 1, BEAM_W = 2, BEAM_N = 3 };

static const int BEAM_DX[4] = { 1, 0, -1, 0 };
static const int BEAM_DZ[4] = { 0, 1, 0, -1 };

#define BEAM_MAX_STEPS 48       /* клеток на один луч — заодно защита от петли из зеркал */
#define BEAM_MAX_RAYS 4         /* лучей одновременно на один излучатель (ветвление призмой) */
#define BEAM_CLEAR_Y 0.25f      /* насколько верх клетки может быть выше луча */
#define BEAM_NO_CELL (-1.0e8f)  /* порог «клетки нет»: level_cell_top даёт -1e9 */
#define BEAM_TURN_FRAMES 15     /* 0,25 с при шаге логики 1/60 */

/* Зеркало — односторонний уголок: состояние задаёт, откуда зеркало принимает луч,
 * и поворачивает его на 90°. Индексы: [состояние 0..3][направление входа].
 * -1 — такой вход не принимается, луч гаснет. Четыре состояния осмысленны:
 * 0 и 2 — две стороны диагонали «/», 1 и 3 — две стороны диагонали «\». */
static const signed char BEAM_MIRROR_OUT[4][4] = {
    /* 0: восток→север, север→восток */ { BEAM_N, -1,     -1,     BEAM_E },
    /* 1: восток→юг,    юг→восток    */ { BEAM_S, BEAM_E, -1,     -1     },
    /* 2: запад→юг,     юг→запад     */ { -1,     BEAM_W, BEAM_S, -1     },
    /* 3: запад→север,  север→запад  */ { -1,     -1,     BEAM_N, BEAM_W },
};

/* yaw излучателя → направление. По контракту 0° = +Z (юг), 90° = +X (восток). */
static const signed char BEAM_YAW_DIR[4] = { BEAM_S, BEAM_E, BEAM_N, BEAM_W };

/* Один луч в работе: где находится, куда идёт и откуда начался текущий отрезок. */
typedef struct {
    int cx, cz;   /* клетка, в которой луч сейчас */
    int dir;      /* BEAM_E … BEAM_N */
    int sx, sz;   /* клетка начала текущего отрезка */
} beam_ray_t;

/* Предпросчитанные клетки объектов, влияющих на луч: без него каждый шаг перебирал бы
 * все сущности уровня. Ничего не выделяем — массив живёт на стеке beam_trace. */
typedef struct {
    short cx, cz, idx;
} beam_obst_t;

typedef struct {
    beam_obst_t items[ENT_RUNTIME_MAX];
    int count;
} beam_index_t;

/* ——— мелкие помощники ——— */

static int beam_type(const entity_t *e) { return e->def ? (int)e->def->type : ENT_NONE; }

/* Влияет ли тип на луч (эмиттеры прозрачны — луч свободно идёт через свою клетку). */
static int beam_is_obstacle(int type) {
    switch (type) {
    case ENT_MIRROR:
    case ENT_PRISM:
    case ENT_RECEIVER:
    case ENT_SLEEP_WALL:
    case ENT_DOOR:
    case ENT_BLOCK:
    case ENT_FLOAT_BLOCK:
        return 1;
    default:
        return 0;
    }
}

/* Округление угла до ближайшей из четырёх осей. */
static int beam_dir_from_yaw(float yaw) {
    int k = (int)floorf(yaw / 90.0f + 0.5f) % 4;
    if (k < 0) k += 4;
    return BEAM_YAW_DIR[k];
}

/* Поворот на 90° влево (против часовой при взгляде сверху, север вверху). */
static int beam_dir_left(int dir) { return (dir + 3) & 3; }

static void beam_index_build(const entities_t *es, beam_index_t *ix) {
    ix->count = 0;
    for (int i = 0; i < es->count; i++) {
        const entity_t *e = &es->items[i];
        if (!e->active || !beam_is_obstacle(beam_type(e))) continue;
        int cx = 0, cz = 0;
        if (!level_cell_at(es->level, e->x, e->z, &cx, &cz)) continue;
        beam_obst_t *o = &ix->items[ix->count++];
        o->cx = (short)cx;
        o->cz = (short)cz;
        o->idx = (short)i;
    }
}

/* Первый по порядку уровня объект в клетке (порядок задаёт данные — он детерминирован). */
static entity_t *beam_obstacle_at(entities_t *es, const beam_index_t *ix, int cx, int cz) {
    for (int i = 0; i < ix->count; i++) {
        if (ix->items[i].cx == (short)cx && ix->items[i].cz == (short)cz) {
            return &es->items[ix->items[i].idx];
        }
    }
    return NULL;
}

/* Отрезок на непрерывный участок одного направления; нулевые отрезки не пишем. */
static void beam_push_seg(const level_t *l, beam_t *out,
                          int x0, int z0, int x1, int z1, float y) {
    if (x0 == x1 && z0 == z1) return;
    if (out->count >= BEAM_MAX_SEGMENTS) return;
    beam_seg_t *s = &out->segs[out->count++];
    level_cell_center(l, x0, z0, &s->x0, &s->z0);
    level_cell_center(l, x1, z1, &s->x1, &s->z1);
    s->y = y;
}

/* ——— трассировка одного излучателя ——— */

/* Очередь лучей фиксированной длины: луч 0 — от излучателя, остальные слоты отдаём
 * призмам. Обработка строго по порядку — результат не зависит от компилятора. */
static void beam_run(entities_t *es, const beam_index_t *ix, beam_t *out,
                     int cx, int cz, int dir, float y) {
    const level_t *l = es->level;
    beam_ray_t q[BEAM_MAX_RAYS];
    int qn = 0;

    q[qn].cx = cx; q[qn].cz = cz; q[qn].dir = dir; q[qn].sx = cx; q[qn].sz = cz;
    qn++;

    for (int r = 0; r < qn; r++) {
        beam_ray_t ray = q[r];
        int steps = 0;
        while (steps++ < BEAM_MAX_STEPS) {
            int nx = ray.cx + BEAM_DX[ray.dir];
            int nz = ray.cz + BEAM_DZ[ray.dir];
            float top = level_cell_top(l, nx, nz);
            if (top < BEAM_NO_CELL) break;        /* клетки нет — луч уходит в пустоту */
            if (top > y + BEAM_CLEAR_Y) break;    /* стена выше луча */
            ray.cx = nx;
            ray.cz = nz;

            entity_t *e = beam_obstacle_at(es, ix, nx, nz);
            if (!e) continue;

            int type = beam_type(e);
            if (type == ENT_MIRROR) {
                int nd = BEAM_MIRROR_OUT[e->state & 3][ray.dir];
                if (nd < 0) break; /* вход не с той стороны — зеркало гасит луч */
                beam_push_seg(l, out, ray.sx, ray.sz, ray.cx, ray.cz, y);
                ray.dir = nd;
                ray.sx = ray.cx;
                ray.sz = ray.cz;
                continue;
            }
            if (type == ENT_PRISM) {
                /* Насквозь плюс копия, повёрнутая влево, — если есть свободный слот. */
                if (qn < BEAM_MAX_RAYS) {
                    q[qn].cx = ray.cx;
                    q[qn].cz = ray.cz;
                    q[qn].dir = beam_dir_left(ray.dir);
                    q[qn].sx = ray.cx;
                    q[qn].sz = ray.cz;
                    qn++;
                }
                continue;
            }
            if (type == ENT_RECEIVER) {
                e->outputs = (unsigned char)(e->outputs | 1u);
                out->hit_receiver_id = (int)e->def->id;
                break; /* луч заканчивается в приёмнике */
            }
            if (type == ENT_SLEEP_WALL) {
                if (!e->watched) break; /* прозрачна только под взглядом (GDD §1.4) */
                continue;
            }
            if (type == ENT_DOOR) {
                if (e->state == 0) break; /* закрытая дверь гасит */
                continue;
            }
            break; /* блок и плавучий блок гасят всегда */
        }
        beam_push_seg(l, out, ray.sx, ray.sz, ray.cx, ray.cz, y);
    }
}

/* ——— контракт ——— */

void beam_trace(entities_t *es, beam_t *out) {
    if (!out) return;
    out->count = 0;
    out->hit_receiver_id = 0;
    if (!es || !es->level) return;

    /* Приёмники пересчитываются с нуля каждый вызов: непопавший луч гасит выход. */
    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        if (beam_type(e) == ENT_RECEIVER) e->outputs = (unsigned char)(e->outputs & ~1u);
    }

    beam_index_t ix;
    beam_index_build(es, &ix);

    for (int i = 0; i < es->count; i++) {
        entity_t *e = &es->items[i];
        if (!e->active || beam_type(e) != ENT_EMITTER) continue;
        int cx = 0, cz = 0;
        if (!level_cell_at(es->level, e->x, e->z, &cx, &cz)) continue;
        beam_run(es, &ix, out, cx, cz, beam_dir_from_yaw(e->yaw), e->y);
    }
}

int mirror_rotate(entities_t *es, int entity_id) {
    if (!es) return 0;
    entity_t *e = entities_by_id(es, entity_id);
    if (!e || !e->active) return 0;

    int type = beam_type(e);
    if (type != ENT_MIRROR && type != ENT_PRISM) return 0;
    if (es->busy_frames > 0 || !tween_done(&e->anim)) return 0; /* идёт анимация */

    e->state = (unsigned char)((e->state + 1u) & 3u);

    /* Угол ведёт tween (CLAUDE.md, п.10): anim.value — то, что рисуем в эти 15 кадров.
     * anim после entities_init стоит на нуле, поэтому привязываем его к текущему углу.
     * e->yaw сразу принимает конечное значение, чтобы следующий поворот считался от него
     * даже если кадровый код не переносит anim.value в yaw. */
    tween_set(&e->anim, e->yaw);
    tween_start(&e->anim, e->yaw + 90.0f, BEAM_TURN_FRAMES);
    e->yaw = e->anim.to;
    es->busy_frames = BEAM_TURN_FRAMES;
    return 1;
}
