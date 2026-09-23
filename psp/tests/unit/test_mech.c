/* Тесты механических головоломок (core/puzzle_mech.c): сокобан-блоки и плиты, рычаги,
 * уровень воды и плавучие блоки, панели памяти, вращающиеся сегменты (GDD §1.4).
 * Уровни синтетические — собираются здесь же по контракту docs/FORMATS.md. */
#include "minitest.h"
#include "tests.h"
#include "entity.h"
#include "entity_types.h"
#include "frame.h"
#include "level.h"
#include "platform.h"
#include "player.h"
#include "puzzles.h"
#include "walk.h"
#include "world.h"
#include <string.h>

void tests_mech(void);

#define M_CELL 1.0f
#define M_STEP 0.5f
#define M_W 8
#define M_H 8
#define M_EPS 1e-6

/* ——— сборка синтетического ALVL ——— */

static void put_u16(unsigned char *p, unsigned v) { unsigned short t = (unsigned short)v; memcpy(p, &t, 2); }
static void put_u32(unsigned char *p, unsigned v) { memcpy(p, &v, 4); }
static void put_f32(unsigned char *p, float v) { memcpy(p, &v, 4); }

/* Центр клетки по одной оси: остров центрирован в начале координат. */
static float cell_c(int c, int n) { return -(float)n * M_CELL * 0.5f + ((float)c + 0.5f) * M_CELL; }

typedef struct {
    const char *const *map;   /* высоты: '.' — пусто, '0'-'9' — шаги */
    const char *const *segs;  /* '1'-'9' — номер вращающегося сегмента */
    const char *const *segst; /* '0'-'3' — состояние сегмента, при котором клетка проходима */
    const char *const *floats; /* 'x' — плавучая клетка: пол идёт за уровнем воды */
    const level_entity_t *ents;
    int ent_count;
    const level_link_t *links;
    int link_count;
} mech_spec_t;

static void *build_mech_level(const mech_spec_t *s, size_t *out_len) {
    size_t cells_bytes = (size_t)M_W * (size_t)M_H * 4u;
    size_t ent_bytes = (size_t)s->ent_count * sizeof(level_entity_t);
    size_t link_bytes = (size_t)s->link_count * sizeof(level_link_t);
    unsigned cells_off = 96;
    unsigned ent_off = (unsigned)(cells_off + cells_bytes);
    unsigned link_off = (unsigned)(ent_off + ent_bytes);
    size_t total = (size_t)link_off + link_bytes;

    unsigned char *b = (unsigned char *)plat_alloc16(total + 16);
    memset(b, 0, total + 16);
    memcpy(b, "ALVL", 4);
    put_u32(b + 4, 1);
    memcpy(b + 8, "mech", 4);
    memcpy(b + 24, "hub", 3);
    put_u16(b + 40, M_W);
    put_u16(b + 42, M_H);
    put_f32(b + 44, M_CELL);
    put_f32(b + 48, M_STEP);
    put_f32(b + 52, 0.0f);
    put_f32(b + 56, 0.0f);
    put_f32(b + 60, 0.0f);
    put_u16(b + 64, (unsigned)s->ent_count);
    put_u16(b + 66, (unsigned)s->link_count);
    put_u16(b + 68, 0);
    put_u32(b + 72, cells_off);
    put_u32(b + 76, ent_off);
    put_u32(b + 80, link_off);
    put_u32(b + 84, link_off);
    put_u32(b + 88, 0xFFFFFFFFu);

    for (int z = 0; z < M_H; z++) {
        for (int x = 0; x < M_W; x++) {
            unsigned char *c = b + cells_off + ((size_t)z * (size_t)M_W + (size_t)x) * 4u;
            char ch = s->map[z][x];
            if (ch == '.' || ch == ' ') continue;
            c[0] = (unsigned char)(ch - '0');
            c[1] = CELL_EXISTS | CELL_WALK;
            if (s->floats && s->floats[z][x] == 'x') c[1] |= CELL_FLOAT;
            char seg = s->segs ? s->segs[z][x] : '.';
            if (seg >= '1' && seg <= '9') {
                char st = s->segst ? s->segst[z][x] : '0';
                unsigned need = (st >= '0' && st <= '3') ? (unsigned)(st - '0') : 0u;
                c[1] |= CELL_SEG;
                c[2] = (unsigned char)(((unsigned)(seg - '0') & 0x3Fu) | (need << 6));
            }
        }
    }
    if (ent_bytes) memcpy(b + ent_off, s->ents, ent_bytes);
    if (link_bytes) memcpy(b + link_off, s->links, link_bytes);
    if (out_len) *out_len = total;
    return b;
}

/* Описание сущности в клетках — разворачивается в level_entity_t. */
typedef struct {
    int id, type, cx, cz;
    int p0, p1, p2, p3, p4;
} row_t;

static void fill_ents(const row_t *rows, int n, const char *const *map, level_entity_t *out) {
    memset(out, 0, (size_t)n * sizeof(level_entity_t));
    for (int i = 0; i < n; i++) {
        char ch = map[rows[i].cz][rows[i].cx];
        out[i].type = (unsigned short)rows[i].type;
        out[i].id = (unsigned short)rows[i].id;
        out[i].x = cell_c(rows[i].cx, M_W);
        out[i].z = cell_c(rows[i].cz, M_H);
        out[i].y = (ch == '.' || ch == ' ') ? 0.0f : (float)(ch - '0') * M_STEP;
        out[i].name_str_id = 0xFFFF;
        out[i].params[0] = rows[i].p0;
        out[i].params[1] = rows[i].p1;
        out[i].params[2] = rows[i].p2;
        out[i].params[3] = rows[i].p3;
        out[i].params[4] = rows[i].p4;
    }
}

/* Кадров логики: сдвигает твины и busy_frames. Камера нулевая — взгляд выключен. */
static void tick_n(entities_t *es, int n) {
    frame_cam_t cam;
    memset(&cam, 0, sizeof cam);
    for (int i = 0; i < n; i++) entities_tick(es, &cam, 0);
}

/* ——— блоки ——— */

/* (3,1) выше на шаг, (5,1) выше на два шага, (5,3) — дыра. */
static const char *const MAP_BLOCK[M_H] = {
    "22222222",
    "22232422",
    "22222222",
    "22222.22",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
};

enum { B_FREE = 1, B_EDGE = 2, B_HOLE = 3, B_STEP = 4, B_PAIR_A = 5, B_PAIR_B = 6, B_LEVER = 7 };
static const row_t BLOCK_ROWS[] = {
    { B_FREE,   ENT_BLOCK, 1, 6, 0, 0, 0, 0, 0 },
    { B_EDGE,   ENT_BLOCK, 0, 4, 0, 0, 0, 0, 0 },
    { B_HOLE,   ENT_BLOCK, 4, 3, 0, 0, 0, 0, 0 },
    { B_STEP,   ENT_BLOCK, 4, 1, 0, 0, 0, 0, 0 },
    { B_PAIR_A, ENT_BLOCK, 1, 5, 0, 0, 0, 0, 0 },
    { B_PAIR_B, ENT_BLOCK, 2, 5, 0, 0, 0, 0, 0 },
    { B_LEVER,  ENT_LEVER, 7, 7, 0, 0, 0, 0, 0 },
};
#define BLOCK_ROW_COUNT ((int)(sizeof BLOCK_ROWS / sizeof BLOCK_ROWS[0]))

TEST(test_mech_block_push) {
    level_entity_t ents[BLOCK_ROW_COUNT];
    fill_ents(BLOCK_ROWS, BLOCK_ROW_COUNT, MAP_BLOCK, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_BLOCK;
    spec.ents = ents;
    spec.ent_count = BLOCK_ROW_COUNT;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    /* Направление — ровно одна ось на одну клетку. */
    CHECK_EQ(block_push(&es, B_FREE, 1, 1), 0);
    CHECK_EQ(block_push(&es, B_FREE, 0, 0), 0);
    CHECK_EQ(block_push(&es, B_FREE, 2, 0), 0);
    CHECK_EQ(block_push(&es, B_FREE, -1, -1), 0);
    /* Не блок и несуществующая сущность. */
    CHECK_EQ(block_push(&es, B_LEVER, 1, 0), 0);
    CHECK_EQ(block_push(&es, 99, 1, 0), 0);

    /* Стена (край острова), дыра, слишком большой перепад, другой блок. */
    CHECK_EQ(block_push(&es, B_EDGE, -1, 0), 0);
    CHECK_EQ(block_push(&es, B_HOLE, 1, 0), 0);
    CHECK_EQ(block_push(&es, B_STEP, 1, 0), 0);
    CHECK_EQ(block_push(&es, B_PAIR_A, 1, 0), 0);
    CHECK_EQ(es.busy_frames, 0); /* ни один отказ не занял ввод */

    entity_t *b = entities_by_id(&es, B_FREE);
    CHECK(b != NULL);
    if (!b) { level_free(&l); return; }
    float start_x = b->x;

    /* Толчок на восток: блок встаёт в центр соседней клетки, анимация 0,3 с. */
    CHECK_EQ(block_push(&es, B_FREE, 1, 0), 1);
    CHECK_NEAR(b->x, cell_c(2, M_W), M_EPS);
    CHECK_NEAR(b->z, cell_c(6, M_H), M_EPS);
    CHECK_NEAR(b->y, 2 * M_STEP, M_EPS);
    CHECK_EQ(es.busy_frames, 18);
    CHECK(!tween_done(&b->anim));
    CHECK_NEAR(b->anim.value, start_x, M_EPS); /* визуал стартует со старой клетки */

    /* Во время анимации новые толчки запрещены — ни этим блоком, ни другим. */
    CHECK_EQ(block_push(&es, B_FREE, 1, 0), 0);
    CHECK_EQ(block_push(&es, B_PAIR_B, 1, 0), 0);
    CHECK_NEAR(b->x, cell_c(2, M_W), M_EPS);

    tick_n(&es, 18);
    CHECK_EQ(es.busy_frames, 0);
    CHECK(tween_done(&b->anim));
    CHECK_NEAR(b->anim.value, cell_c(2, M_W), M_EPS);

    /* Подъём ровно на один шаг разрешён: (4,1) → (3,1). */
    entity_t *s = entities_by_id(&es, B_STEP);
    CHECK(s != NULL);
    if (s) {
        CHECK_EQ(block_push(&es, B_STEP, -1, 0), 1);
        CHECK_NEAR(s->x, cell_c(3, M_W), M_EPS);
        CHECK_NEAR(s->y, 3 * M_STEP, M_EPS);
        CHECK_NEAR(s->yaw, 180.0, M_EPS);
    }
    tick_n(&es, 18);

    /* Толчок на юг: анимируется ось z. */
    CHECK_EQ(block_push(&es, B_FREE, 0, 1), 1);
    CHECK_NEAR(b->z, cell_c(7, M_H), M_EPS);
    CHECK_NEAR(b->yaw, 90.0, M_EPS);
    CHECK_NEAR(b->anim.value, cell_c(6, M_H), M_EPS);
    tick_n(&es, 18);
    CHECK_NEAR(b->anim.value, cell_c(7, M_H), M_EPS);

    /* С юга дальше — край острова. */
    CHECK_EQ(block_push(&es, B_FREE, 0, 1), 0);
    level_free(&l);
}

/* ——— рычаг и дверь ——— */

static const char *const MAP_FLAT[M_H] = {
    "22222222",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
};

enum { L_LEVER = 1, L_DOOR = 2 };
static const row_t LEVER_ROWS[] = {
    { L_LEVER, ENT_LEVER, 1, 1, 0, 0, 0, 0, 0 },
    { L_DOOR,  ENT_DOOR,  2, 1, 0, 0, 0, 0, 0 },
};
static const level_link_t LEVER_LINKS[] = { { L_LEVER, 0, 0, L_DOOR, 0, 0 } };

TEST(test_mech_lever) {
    level_entity_t ents[2];
    fill_ents(LEVER_ROWS, 2, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 2;
    spec.links = LEVER_LINKS;
    spec.link_count = 1;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    entity_t *lever = entities_by_id(&es, L_LEVER);
    entity_t *door = entities_by_id(&es, L_DOOR);
    CHECK(lever != NULL && door != NULL);
    if (!lever || !door) { level_free(&l); return; }
    CHECK_EQ(door->state, 0);

    /* Переключение: состояние и выход 0, дверь открывается после разноса сигналов. */
    CHECK_EQ(lever_toggle(&es, L_LEVER), 1);
    CHECK_EQ(lever->state, 1);
    CHECK_EQ(lever->outputs & 1, 1);
    CHECK_EQ(es.busy_frames, 0); /* рычаг ввод не блокирует */
    CHECK(!tween_done(&lever->anim));
    CHECK_EQ(door->state, 0); /* до entities_propagate дверь ещё закрыта */
    entities_propagate(&es);
    CHECK_EQ(door->inputs & 1, 1);
    CHECK_EQ(door->state, 1);

    tick_n(&es, 12); /* 0,2 с */
    CHECK(tween_done(&lever->anim));
    CHECK_NEAR(lever->anim.value, 1.0, M_EPS);

    /* Обратное переключение закрывает дверь. */
    CHECK_EQ(lever_toggle(&es, L_LEVER), 1);
    CHECK_EQ(lever->state, 0);
    CHECK_EQ(lever->outputs & 1, 0);
    entities_propagate(&es);
    CHECK_EQ(door->state, 0);
    tick_n(&es, 12);
    CHECK_NEAR(lever->anim.value, 0.0, M_EPS);

    /* Не рычаг, несуществующий id и заблокированный ввод. */
    CHECK_EQ(lever_toggle(&es, L_DOOR), 0);
    CHECK_EQ(lever_toggle(&es, 99), 0);
    es.busy_frames = 5;
    CHECK_EQ(lever_toggle(&es, L_LEVER), 0);
    CHECK_EQ(lever->state, 0);
    es.busy_frames = 0;
    level_free(&l);
}

/* ——— плиты ——— */

enum { P_PLATE = 1, P_DOOR = 2, P_BLOCK = 3 };
static const row_t PLATE_ROWS[] = {
    { P_PLATE, ENT_PLATE, 2, 2, 0, 0, 0, 0, 0 },
    { P_DOOR,  ENT_DOOR,  5, 5, 0, 0, 0, 0, 0 },
    { P_BLOCK, ENT_BLOCK, 3, 2, 0, 0, 0, 0, 0 },
};
static const level_link_t PLATE_LINKS[] = { { P_PLATE, 0, 0, P_DOOR, 0, 0 } };

TEST(test_mech_plates) {
    level_entity_t ents[3];
    fill_ents(PLATE_ROWS, 3, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 3;
    spec.links = PLATE_LINKS;
    spec.link_count = 1;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    entity_t *plate = entities_by_id(&es, P_PLATE);
    entity_t *door = entities_by_id(&es, P_DOOR);
    CHECK(plate != NULL && door != NULL);
    if (!plate || !door) { level_free(&l); return; }

    float far_x = cell_c(7, M_W), far_z = cell_c(7, M_H);
    float on_x = cell_c(2, M_W), on_z = cell_c(2, M_H);

    plates_update(&es, far_x, far_z);
    entities_propagate(&es);
    CHECK_EQ(plate->outputs & 1, 0);
    CHECK_EQ(door->state, 0);

    /* Игрок встал на плиту (сравнение по клетке, не по расстоянию). */
    plates_update(&es, on_x + 0.45f, on_z - 0.45f);
    entities_propagate(&es);
    CHECK_EQ(plate->outputs & 1, 1);
    CHECK_EQ(plate->state, 1);
    CHECK(!tween_done(&plate->anim));
    CHECK_EQ(door->state, 1);

    /* Повторный пересчёт без изменений не перезапускает анимацию. */
    tick_n(&es, 6);
    CHECK(tween_done(&plate->anim));
    plates_update(&es, on_x, on_z);
    CHECK(tween_done(&plate->anim));
    CHECK_EQ(plate->outputs & 1, 1);

    /* Игрок ушёл — плита отпущена. */
    plates_update(&es, far_x, far_z);
    entities_propagate(&es);
    CHECK_EQ(plate->outputs & 1, 0);
    CHECK_EQ(door->state, 0);

    /* Плиту держит блок, задвинутый в её клетку. */
    CHECK_EQ(block_push(&es, P_BLOCK, -1, 0), 1);
    plates_update(&es, far_x, far_z);
    entities_propagate(&es);
    CHECK_EQ(plate->outputs & 1, 1);
    CHECK_EQ(door->state, 1);
    tick_n(&es, 18);

    /* Ушли оба — плита отпускается. */
    CHECK_EQ(block_push(&es, P_BLOCK, 1, 0), 1);
    plates_update(&es, far_x, far_z);
    entities_propagate(&es);
    CHECK_EQ(plate->outputs & 1, 0);
    CHECK_EQ(door->state, 0);
    tick_n(&es, 18);

    /* Игрок и блок на плите одновременно: уход одного плиту не отпускает. */
    CHECK_EQ(block_push(&es, P_BLOCK, -1, 0), 1);
    plates_update(&es, on_x, on_z);
    CHECK_EQ(plate->outputs & 1, 1);
    plates_update(&es, far_x, far_z);
    CHECK_EQ(plate->outputs & 1, 1); /* блок остался */
    level_free(&l);
}

/* ——— вода, шлюз и плавучие блоки ——— */

/* (2,2) — яма высоты 0, (3,3) — столб высоты 4 (выше максимума воды). */
static const char *const MAP_WATER[M_H] = {
    "22222222",
    "22222222",
    "22022222",
    "22242222",
    "22222222",
    "22222222",
    "22222222",
    "22222222",
};

enum { W_VALVE = 1, W_LOW = 2, W_HIGH = 3, W_LEVER = 4 };
static const row_t WATER_ROWS[] = {
    { W_VALVE, ENT_WATER_VALVE, 1, 1, 1, 3, 0, 0, 0 }, /* уровни 1..3 */
    { W_LOW,   ENT_FLOAT_BLOCK, 2, 2, 0, 0, 0, 0, 0 },
    { W_HIGH,  ENT_FLOAT_BLOCK, 3, 3, 0, 0, 0, 0, 0 },
    { W_LEVER, ENT_LEVER,       5, 5, 0, 0, 0, 0, 0 },
};

TEST(test_mech_water) {
    level_entity_t ents[4];
    fill_ents(WATER_ROWS, 4, MAP_WATER, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_WATER;
    spec.ents = ents;
    spec.ent_count = 4;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);
    CHECK_EQ(es.water_steps, 1); /* entities_init ставит воду на минимум шлюза */

    entity_t *low = entities_by_id(&es, W_LOW);
    entity_t *high = entities_by_id(&es, W_HIGH);
    CHECK(low != NULL && high != NULL);
    if (!low || !high) { level_free(&l); return; }
    /* entities_init уже применил стартовый уровень воды: плот в яме всплыл на него,
     * причём без анимации — загрузка уровня не игровое событие. */
    CHECK_NEAR(low->y, 1 * M_STEP, M_EPS);
    CHECK(tween_done(&low->anim));
    CHECK_NEAR(high->y, 4 * M_STEP, M_EPS);

    /* Подъём воды: блок в яме всплывает, блок на столбе остаётся на своей высоте. */
    water_set_level(&es, 2);
    CHECK_EQ(es.water_steps, 2);
    CHECK_NEAR(low->y, 2 * M_STEP, M_EPS);
    CHECK(!tween_done(&low->anim));
    CHECK_NEAR(low->anim.value, 1 * M_STEP, M_EPS);
    CHECK_NEAR(high->y, 4 * M_STEP, M_EPS);
    CHECK(tween_done(&high->anim));
    CHECK_NEAR(high->anim.value, 4 * M_STEP, M_EPS); /* визуал привязан к логике */

    tick_n(&es, 24); /* 0,4 с */
    CHECK(tween_done(&low->anim));
    CHECK_NEAR(low->anim.value, 2 * M_STEP, M_EPS);

    /* Спуск и защита от мусора на входе. */
    water_set_level(&es, -3);
    CHECK_EQ(es.water_steps, 0);
    CHECK_NEAR(low->y, 0.0, M_EPS);
    tick_n(&es, 24);

    /* Шлюз перебирает уровни по кругу в границах params[0]..params[1]. */
    CHECK_EQ(water_valve_use(&es, W_VALVE), 1);
    CHECK_EQ(es.water_steps, 1);
    CHECK_EQ(es.last_event, W_VALVE);
    CHECK_EQ(water_valve_use(&es, W_VALVE), 1);
    CHECK_EQ(es.water_steps, 2);
    CHECK_EQ(water_valve_use(&es, W_VALVE), 1);
    CHECK_EQ(es.water_steps, 3);
    CHECK_NEAR(low->y, 3 * M_STEP, M_EPS);
    CHECK_NEAR(high->y, 4 * M_STEP, M_EPS); /* вода не дотягивается до столба */
    CHECK_EQ(water_valve_use(&es, W_VALVE), 1);
    CHECK_EQ(es.water_steps, 1); /* после максимума — снова минимум */
    CHECK_EQ(water_valve_use(&es, W_VALVE), 1);
    CHECK_EQ(es.water_steps, 2);

    /* Не шлюз, несуществующий id и заблокированный ввод. */
    CHECK_EQ(water_valve_use(&es, W_LEVER), 0);
    CHECK_EQ(water_valve_use(&es, 99), 0);
    es.busy_frames = 4;
    CHECK_EQ(water_valve_use(&es, W_VALVE), 0);
    CHECK_EQ(es.water_steps, 2);
    es.busy_frames = 0;

    tick_n(&es, 24);
    CHECK_NEAR(low->anim.value, 2 * M_STEP, M_EPS);
    level_free(&l);
}

/* Плот из плавучих клеток: (4,2) и (4,3) поднимаются и опускаются вместе с водой. */
static const char *const FLOATS_WATER[M_H] = {
    "........",
    "........",
    "....x...",
    "....x...",
    "........",
    "........",
    "........",
    "........",
};

TEST(test_mech_water_walk) {
    level_entity_t ents[4];
    fill_ents(WATER_ROWS, 4, MAP_WATER, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_WATER;
    spec.floats = FLOATS_WATER;
    spec.ents = ents;
    spec.ent_count = 4;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);
    CHECK_EQ(es.water_steps, 1); /* минимум шлюза */

    /* Пол плота — уровень воды, а не высота из карты. */
    float fx = cell_c(4, M_W), fz = cell_c(2, M_H);
    CHECK_NEAR(walk_floor_at(&l, fx, fz), 1 * M_STEP, M_EPS);
    CHECK_EQ(walk_is_walkable(&l, 4, 2), 1);

    /* Подняли воду — плот всплыл; соседняя клетка карты осталась на своей высоте. */
    water_set_level(&es, 3);
    CHECK_NEAR(walk_floor_at(&l, fx, fz), 3 * M_STEP, M_EPS);
    CHECK_NEAR(walk_floor_at(&l, cell_c(5, M_W), fz), 2 * M_STEP, M_EPS);

    /* Шаг с берега (высота 2) на плот: при воде 3 перепад 0,5 берётся,
     * при воде 0 — перепад 1,0 уже нет. Это и есть головоломка со шлюзом. */
    walk_pos_t p = { cell_c(5, M_W), fz, 2 * M_STEP };
    CHECK_EQ(walk_move(&l, &p, -M_CELL, 0.0f, 0.3f, 0.55f), 1);
    CHECK_NEAR(p.y, 3 * M_STEP, M_EPS);

    water_set_level(&es, 0);
    walk_pos_t q = { cell_c(5, M_W), fz, 2 * M_STEP };
    CHECK_EQ(walk_move(&l, &q, -M_CELL, 0.0f, 0.3f, 0.55f), 0);

    /* Западня шлюза: Око стоит у самой кромки, вода поднимает плот на две ступени —
     * и проба круга с его стороны упирается в стену при любом направлении движения.
     * player_unstick обязан вернуть Око в центр клетки, иначе игра встаёт намертво. */
    water_set_level(&es, 1);
    player_t pl;
    memset(&pl, 0, sizeof pl);
    pl.pos.x = cell_c(5, M_W) - M_CELL * 0.3f;   /* вплотную к плоту: проба круга уже на нём */
    pl.pos.z = fz;
    pl.pos.y = 2 * M_STEP;
    CHECK(walk_stand_ok(&l, pl.pos.x, pl.pos.z, 0.3f, 0.55f, NULL));
    water_set_level(&es, 4);   /* плот на ступень выше шага персонажа */
    CHECK_EQ(walk_stand_ok(&l, pl.pos.x, pl.pos.z, 0.3f, 0.55f, NULL), 0);
    CHECK_EQ(player_unstick(&pl, &l), 1);
    CHECK(walk_stand_ok(&l, pl.pos.x, pl.pos.z, 0.3f, 0.55f, NULL));
    /* и после сдвига Око снова может уйти от воды */
    walk_pos_t r = pl.pos;
    CHECK_EQ(walk_move(&l, &r, M_CELL * 0.2f, 0.0f, 0.3f, 0.55f), 1);

    level_free(&l);
}

/* ——— панель памяти ——— */

enum { MEM_SEQ = 1, MEM_DOOR = 2, MEM_LEN = 3, MEM_EMPTY = 4, MEM_LEVER = 5 };
static const row_t MEM_ROWS[] = {
    { MEM_SEQ,   ENT_MEMORY_PANEL, 1, 1, 2, 3, 1, 0, 0 }, /* длина по ненулевым: 3 */
    { MEM_DOOR,  ENT_DOOR,         2, 1, 0, 0, 0, 0, 0 },
    { MEM_LEN,   ENT_MEMORY_PANEL, 3, 1, 1, 2, 0, 0, 2 }, /* длина задана явно: 2 */
    { MEM_EMPTY, ENT_MEMORY_PANEL, 4, 1, 0, 0, 0, 0, 0 }, /* последовательности нет */
    { MEM_LEVER, ENT_LEVER,        5, 1, 0, 0, 0, 0, 0 },
};
static const level_link_t MEM_LINKS[] = { { MEM_SEQ, 0, 0, MEM_DOOR, 0, 0 } };

TEST(test_mech_memory) {
    level_entity_t ents[5];
    fill_ents(MEM_ROWS, 5, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 5;
    spec.links = MEM_LINKS;
    spec.link_count = 1;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    entity_t *panel = entities_by_id(&es, MEM_SEQ);
    entity_t *door = entities_by_id(&es, MEM_DOOR);
    CHECK(panel != NULL && door != NULL);
    if (!panel || !door) { level_free(&l); return; }

    /* Сразу неверное значение — прогресс остаётся нулевым. */
    CHECK_EQ(memory_input(&es, MEM_SEQ, 1), 0);
    CHECK_EQ(panel->state, 0);

    /* Ошибка в середине сбрасывает прогресс. */
    CHECK_EQ(memory_input(&es, MEM_SEQ, 2), 1);
    CHECK_EQ(panel->state, 1);
    CHECK_EQ(memory_input(&es, MEM_SEQ, 9), 0);
    CHECK_EQ(panel->state, 0);
    CHECK_EQ(panel->outputs & 1, 0);

    /* Верная последовательность: 2, 3, 1 — последнее значение включает выход. */
    CHECK_EQ(memory_input(&es, MEM_SEQ, 2), 1);
    CHECK_EQ(memory_input(&es, MEM_SEQ, 3), 1);
    CHECK_EQ(door->state, 0);
    CHECK_EQ(memory_input(&es, MEM_SEQ, 1), 2);
    CHECK_EQ(panel->outputs & 1, 1);
    entities_propagate(&es);
    CHECK_EQ(door->state, 1);

    /* После завершения повторный ввод ничего не меняет. */
    unsigned char done_state = panel->state;
    CHECK_EQ(memory_input(&es, MEM_SEQ, 4), 2);
    CHECK_EQ(memory_input(&es, MEM_SEQ, 2), 2);
    CHECK_EQ(panel->state, done_state);
    CHECK_EQ(panel->outputs & 1, 1);
    entities_propagate(&es);
    CHECK_EQ(door->state, 1);

    /* Явная длина params[4]: хватает двух значений. */
    entity_t *short_panel = entities_by_id(&es, MEM_LEN);
    CHECK(short_panel != NULL);
    if (short_panel) {
        CHECK_EQ(memory_input(&es, MEM_LEN, 1), 1);
        CHECK_EQ(memory_input(&es, MEM_LEN, 2), 2);
        CHECK_EQ(short_panel->outputs & 1, 1);
    }

    /* Пустая панель, не панель и несуществующий id. */
    CHECK_EQ(memory_input(&es, MEM_EMPTY, 1), 0);
    CHECK_EQ(memory_input(&es, MEM_LEVER, 1), 0);
    CHECK_EQ(memory_input(&es, 99, 1), 0);

    /* Собранная панель помнится миром: после перезагрузки острова она собрана,
     * а дверь за ней открыта. Ради этого случая — возврат в читальню Библиотеки
     * с соседнего острова — флаг и заведён. */
    CHECK_EQ(world_flag(&w, MEM_SEQ), 1);
    CHECK_EQ(world_flag(&w, MEM_LEN), 1);
    CHECK_EQ(world_flag(&w, MEM_EMPTY), 0);
    entities_init(&es, &l, &w);
    panel = entities_by_id(&es, MEM_SEQ);
    door = entities_by_id(&es, MEM_DOOR);
    CHECK(panel != NULL && door != NULL);
    if (panel && door) {
        CHECK_EQ(panel->outputs & 1, 1);
        CHECK_EQ(panel->state, 3);
        CHECK_EQ(memory_input(&es, MEM_SEQ, 7), 2); /* уже собрана — ввод не ломает */
        entities_propagate(&es);
        CHECK_EQ(door->state, 1);
    }
    level_free(&l);
}

/* ——— вращающиеся сегменты ——— */

static const char *const SEGS_MAP[M_H] = {
    "........",
    "........",
    "........",
    "........",
    "....1...",
    "....1...",
    "........",
    "........",
};
static const char *const SEGS_STATE[M_H] = {
    "........",
    "........",
    "........",
    "........",
    "....1...", /* (4,4) проходима при состоянии 1 */
    "....0...", /* (4,5) проходима при состоянии 0 */
    "........",
    "........",
};

enum { S_SEG = 1, S_BLOCK = 2, S_LEVER = 3 };
static const row_t SEG_ROWS[] = {
    { S_SEG,   ENT_SEGMENT, 4, 4, 1, 0, 0, 0, 0 }, /* params[0] — номер сегмента */
    { S_BLOCK, ENT_BLOCK,   1, 1, 0, 0, 0, 0, 0 },
    { S_LEVER, ENT_LEVER,   2, 2, 0, 0, 0, 0, 0 },
};

TEST(test_mech_segment) {
    level_entity_t ents[3];
    fill_ents(SEG_ROWS, 3, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.segs = SEGS_MAP;
    spec.segst = SEGS_STATE;
    spec.ents = ents;
    spec.ent_count = 3;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    entity_t *seg = entities_by_id(&es, S_SEG);
    CHECK(seg != NULL);
    if (!seg) { level_free(&l); return; }

    /* Состояние 0: проходима нижняя клетка сегмента, верхняя — нет. */
    CHECK_EQ(l.seg_states[1], 0);
    CHECK_EQ(walk_is_walkable(&l, 4, 4), 0);
    CHECK_EQ(walk_is_walkable(&l, 4, 5), 1);

    /* Поворот: проходимость меняется сразу, анимация — только визуал. */
    CHECK_EQ(segment_rotate(&es, 1, 1), 1);
    CHECK_EQ(l.seg_states[1], 1);
    CHECK_EQ(walk_is_walkable(&l, 4, 4), 1);
    CHECK_EQ(walk_is_walkable(&l, 4, 5), 0);
    CHECK_EQ(es.busy_frames, 36); /* 0,6 с */
    CHECK_EQ(seg->state, 1);
    CHECK_NEAR(seg->yaw, 90.0, M_EPS);
    CHECK(!tween_done(&seg->anim));

    /* Пока крутится — ввод заблокирован. */
    CHECK_EQ(segment_rotate(&es, 1, 1), 0);
    CHECK_EQ(block_push(&es, S_BLOCK, 1, 0), 0);
    CHECK_EQ(lever_toggle(&es, S_LEVER), 0);
    CHECK_EQ(l.seg_states[1], 1);

    tick_n(&es, 36);
    CHECK_EQ(es.busy_frames, 0);
    CHECK(tween_done(&seg->anim));
    CHECK_NEAR(seg->anim.value, 90.0, M_EPS);

    /* Ещё три поворота: состояние по кругу 2 → 3 → 0, доворот копится. */
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(segment_rotate(&es, 1, 1), 1);
        tick_n(&es, 36);
    }
    CHECK_EQ(l.seg_states[1], 0);
    CHECK_NEAR(seg->anim.value, 360.0, M_EPS);
    CHECK_EQ(walk_is_walkable(&l, 4, 4), 0);
    CHECK_EQ(walk_is_walkable(&l, 4, 5), 1);

    /* Обратный поворот: состояние 0 → 3, визуально крутимся назад. */
    CHECK_EQ(segment_rotate(&es, 1, -1), 1);
    CHECK_EQ(l.seg_states[1], 3);
    CHECK_NEAR(seg->anim.value, 360.0, M_EPS);
    tick_n(&es, 36);
    CHECK_NEAR(seg->anim.value, 270.0, M_EPS);
    CHECK_EQ(seg->state, 3);

    /* Неверные аргументы. */
    CHECK_EQ(segment_rotate(&es, 0, 1), 0);
    CHECK_EQ(segment_rotate(&es, -1, 1), 0);
    CHECK_EQ(segment_rotate(&es, LEVEL_MAX_SEGMENTS, 1), 0);
    CHECK_EQ(segment_rotate(&es, 1, 0), 0);
    CHECK_EQ(segment_rotate(&es, 1, 4), 0);
    CHECK_EQ(l.seg_states[1], 3);

    /* Сегмент без клеток и маркера крутится молча (им может управлять связь). */
    CHECK_EQ(segment_rotate(&es, 2, 1), 1);
    CHECK_EQ(l.seg_states[2], 1);
    CHECK_EQ(seg->state, 3); /* чужой маркер не трогаем */
    tick_n(&es, 36);
    level_free(&l);
}

/* ——— условие «всё решено» ——— */

enum { A_RECV = 1, A_BIG = 2, A_LEVER = 3, A_BIG2 = 4 };
static const row_t SOLVED_ROWS[] = {
    { A_RECV,  ENT_RECEIVER, 1, 1, 0, 0, 0, 0, 0 },
    { A_BIG,   ENT_BIG_EYE,  2, 1, 0, 0, 0, 0, 0 },
    { A_LEVER, ENT_LEVER,    3, 1, 0, 0, 0, 0, 0 },
    { A_BIG2,  ENT_BIG_EYE,  4, 1, 0, 0, 0, 0, 0 },
};
static const level_link_t SOLVED_LINKS[] = {
    { A_RECV, 0, 0, A_BIG, 0, 0 },
    { A_RECV, 0, 0, A_BIG2, 0, 0 },
};
static const row_t PLAIN_ROWS[] = { { A_LEVER, ENT_LEVER, 3, 1, 0, 0, 0, 0, 0 } };

TEST(test_mech_all_solved) {
    level_entity_t ents[4];
    fill_ents(SOLVED_ROWS, 4, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 4;
    spec.links = SOLVED_LINKS;
    spec.link_count = 2;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    CHECK_EQ(puzzles_all_solved(NULL), 0);
    CHECK_EQ(puzzles_all_solved(&es), 0); /* приёмник пуст, глаза закрыты */

    /* Луч пришёл в приёмник (beam_trace — другой модуль, здесь ставим выход руками). */
    entity_t *recv = entities_by_id(&es, A_RECV);
    CHECK(recv != NULL);
    if (!recv) { level_free(&l); return; }
    recv->outputs |= 1u;
    CHECK_EQ(puzzles_all_solved(&es), 0); /* до разноса сигналов глаза ещё закрыты */

    entities_propagate(&es);
    CHECK_EQ(entities_eyes_open(&es), 2);
    CHECK_EQ(puzzles_all_solved(&es), 1);

    /* Закрытый глаз снова делает уровень нерешённым. */
    entity_t *big = entities_by_id(&es, A_BIG);
    if (big) {
        big->state = 0;
        CHECK_EQ(puzzles_all_solved(&es), 0);
        big->state = 1;
    }
    /* Погасший приёмник — тоже. */
    recv->outputs = 0;
    recv->inputs = 0;
    CHECK_EQ(puzzles_all_solved(&es), 0);
    recv->inputs = 1; /* сигнал по связи засчитывается наравне с лучом */
    CHECK_EQ(puzzles_all_solved(&es), 1);
    level_free(&l);

    /* Уровень без приёмников и больших глаз считается решённым. */
    level_entity_t plain[1];
    fill_ents(PLAIN_ROWS, 1, MAP_FLAT, plain);
    mech_spec_t plain_spec = {0};
    plain_spec.map = MAP_FLAT;
    plain_spec.ents = plain;
    plain_spec.ent_count = 1;
    void *plain_blob = build_mech_level(&plain_spec, &len);
    level_t pl;
    CHECK_EQ(level_load(&pl, plain_blob, len), 0);
    world_reset(&w);
    entities_init(&es, &pl, &w);
    CHECK_EQ(puzzles_all_solved(&es), 1);
    level_free(&pl);
}

TEST(test_mech_segment_trap) {
    /* Поворот сегмента из-под собственных ног запрещён: иначе клетка под игроком
     * становится непроходимой и выбраться с неё уже нельзя. */
    level_entity_t ents[3];
    fill_ents(SEG_ROWS, 3, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.segs = SEGS_MAP;
    spec.segst = SEGS_STATE;
    spec.ents = ents;
    spec.ent_count = 3;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    float sx = cell_c(4, M_W), sz5 = cell_c(5, M_H), sz4 = cell_c(4, M_H);
    /* Состояние 0: игрок стоит на (4,5) — после поворота она закроется. */
    CHECK_EQ(segment_would_trap(&es, 1, 1, sx, sz5), 1);
    /* На (4,4) он стоять сейчас не может, но после поворота она откроется — не ловушка. */
    CHECK_EQ(segment_would_trap(&es, 1, 1, sx, sz4), 0);
    /* Вне сегмента поворот всегда разрешён. */
    CHECK_EQ(segment_would_trap(&es, 1, 1, cell_c(1, M_W), cell_c(1, M_H)), 0);
    /* Поворот на четыре шага возвращает то же состояние — ловушки нет. */
    CHECK_EQ(segment_would_trap(&es, 1, 4, sx, sz5), 0);

    level_free(&l);
}

TEST(test_mech_float_on_plate) {
    /* Плот, приведённый на плиту, давит на неё так же, как обычный блок:
     * на этом построена головоломка «подвести плавучий блок». */
    enum { P_PLATE = 1, P_FLOAT = 2, P_DOOR = 3 };
    static const row_t ROWS[] = {
        { P_PLATE, ENT_PLATE,       4, 2, 0, 0, 0, 0, 0 },
        { P_FLOAT, ENT_FLOAT_BLOCK, 4, 2, 0, 0, 0, 0, 0 },
        { P_DOOR,  ENT_DOOR,        6, 6, 0, 0, 0, 0, 0 },
    };
    static const level_link_t LINKS[] = { { P_PLATE, 0, 0, P_DOOR, 0, 0 } };
    level_entity_t ents[3];
    fill_ents(ROWS, 3, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 3;
    spec.links = LINKS;
    spec.link_count = 1;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    /* Игрок далеко: плиту давит только плот. */
    plates_update(&es, cell_c(0, M_W), cell_c(0, M_H));
    entities_propagate(&es);
    const entity_t *plate = entities_by_id_const(&es, P_PLATE);
    const entity_t *door = entities_by_id_const(&es, P_DOOR);
    CHECK(plate != NULL && door != NULL);
    if (plate && door) {
        CHECK_EQ(plate->state, 1);
        CHECK_EQ(door->state, 1); /* дверь открыта сигналом плиты */
    }

    level_free(&l);
}

TEST(test_mech_block_solid) {
    /* Блок занимает клетку: сквозь него нельзя пройти, а после толчка освобождённая
     * клетка снова проходима. Без этого сокобан не работает — игрок проходит насквозь. */
    enum { B_BLOCK = 1 };
    static const row_t ROWS[] = { { B_BLOCK, ENT_BLOCK, 4, 4, 0, 0, 0, 0, 0 } };
    level_entity_t ents[1];
    fill_ents(ROWS, 1, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 1;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    CHECK_EQ(walk_is_walkable(&l, 4, 4), 0);   /* клетка блока занята */
    CHECK_EQ(walk_is_walkable(&l, 5, 4), 1);

    /* Игрок упирается в блок и не проходит его насквозь. */
    walk_pos_t p = { cell_c(3, M_W), cell_c(4, M_H), 2 * M_STEP };
    for (int i = 0; i < 30; i++) walk_move(&l, &p, 0.1f, 0.0f, 0.3f, 0.55f);
    CHECK(p.x < cell_c(4, M_W) - 0.2f);

    /* Толкаем блок на восток — клетка (4,4) освобождается. */
    CHECK_EQ(block_push(&es, B_BLOCK, 1, 0), 1);
    tick_n(&es, 1);
    CHECK_EQ(walk_is_walkable(&l, 4, 4), 1);
    CHECK_EQ(walk_is_walkable(&l, 5, 4), 0);
    level_free(&l);
}

TEST(test_mech_memory_by_triggers) {
    /* Последовательность вводится кнопками-постаментами: у каждой своё значение
     * (params[0]) и id панели (params[1]). Игра вызывает memory_input с этими
     * значениями — проверяем ровно эту связку, включая сброс при ошибке. */
    enum { T_PANEL = 1, T_A = 2, T_B = 3, T_C = 4, T_DOOR = 5 };
    static const row_t ROWS[] = {
        { T_PANEL, ENT_MEMORY_PANEL, 4, 4, 3, 1, 2, 0, 3 },  /* ждёт 3,1,2 */
        { T_A,     ENT_TRIGGER,      2, 4, 1, T_PANEL, 0, 0, 0 },
        { T_B,     ENT_TRIGGER,      3, 4, 2, T_PANEL, 0, 0, 0 },
        { T_C,     ENT_TRIGGER,      5, 4, 3, T_PANEL, 0, 0, 0 },
        { T_DOOR,  ENT_DOOR,         6, 6, 0, 0, 0, 0, 0 },
    };
    static const level_link_t LINKS[] = { { T_PANEL, 0, 0, T_DOOR, 0, 0 } };
    level_entity_t ents[5];
    fill_ents(ROWS, 5, MAP_FLAT, ents);
    mech_spec_t spec = {0};
    spec.map = MAP_FLAT;
    spec.ents = ents;
    spec.ent_count = 5;
    spec.links = LINKS;
    spec.link_count = 1;

    size_t len = 0;
    void *blob = build_mech_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    /* Неверный первый ввод сбрасывает прогресс. */
    CHECK_EQ(memory_input(&es, T_PANEL, 1), 0);
    CHECK_EQ(entities_by_id(&es, T_PANEL)->state, 0);

    /* Верный порядок 3,1,2 собирает последовательность и открывает дверь. */
    CHECK_EQ(memory_input(&es, T_PANEL, 3), 1);
    CHECK_EQ(memory_input(&es, T_PANEL, 1), 1);
    CHECK_EQ(memory_input(&es, T_PANEL, 2), 2);
    entities_propagate(&es);
    CHECK_EQ(entities_by_id(&es, T_DOOR)->state, 1);

    /* Повторный ввод ничего не ломает. */
    CHECK_EQ(memory_input(&es, T_PANEL, 1), 2);
    entities_propagate(&es);
    CHECK_EQ(entities_by_id(&es, T_DOOR)->state, 1);

    /* Кнопка-постамент интерактивна: игра найдёт её «крестом». */
    CHECK_EQ(entity_can_interact(ENT_TRIGGER), 1);
    level_free(&l);
}

void tests_mech(void) {
    puts("mech puzzle tests");
    RUN(test_mech_block_push);
    RUN(test_mech_block_solid);
    RUN(test_mech_lever);
    RUN(test_mech_plates);
    RUN(test_mech_water);
    RUN(test_mech_water_walk);
    RUN(test_mech_memory);
    RUN(test_mech_memory_by_triggers);
    RUN(test_mech_segment);
    RUN(test_mech_segment_trap);
    RUN(test_mech_float_on_plate);
    RUN(test_mech_all_solved);
}
