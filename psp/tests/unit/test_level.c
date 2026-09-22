/* Тесты загрузчика уровней (ALVL) и перемещения по сетке. */
#include "minitest.h"
#include "tests.h"
#include "level.h"
#include "walk.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

#define CELL 1.0f
#define STEP 0.5f

/* Сборка корректного ALVL-блоба из ASCII-карт (по контракту docs/FORMATS.md).
 * map: '.' — пусто, '0'-'9' — высота в шагах. stairs/water — необязательные слои с 'x'. */
typedef struct {
    int w, h;
    const char *const *map;
    const char *const *stairs;
    const char *const *water;
    const level_entity_t *ents;
    int ent_count;
    const level_portal_t *portals;
    int portal_count;
    float spawn_x, spawn_z;
} lvl_spec_t;

static void put_u16(unsigned char *p, unsigned v) { unsigned short t = (unsigned short)v; memcpy(p, &t, 2); }
static void put_u32(unsigned char *p, unsigned v) { memcpy(p, &v, 4); }
static void put_f32(unsigned char *p, float v) { memcpy(p, &v, 4); }

static void *build_level(const lvl_spec_t *s, size_t *out_len) {
    size_t cells_bytes = (size_t)s->w * (size_t)s->h * 4u;
    size_t ent_bytes = (size_t)s->ent_count * sizeof(level_entity_t);
    size_t port_bytes = (size_t)s->portal_count * sizeof(level_portal_t);
    unsigned cells_off = 96;
    unsigned ent_off = (unsigned)(cells_off + cells_bytes);
    unsigned link_off = (unsigned)(ent_off + ent_bytes);
    unsigned port_off = link_off;
    size_t total = (size_t)port_off + port_bytes;

    unsigned char *b = (unsigned char *)plat_alloc16(total + 16);
    memset(b, 0, total + 16);
    memcpy(b, "ALVL", 4);
    put_u32(b + 4, 1);
    memcpy(b + 8, "test", 4);
    memcpy(b + 24, "hub", 3);
    put_u16(b + 40, (unsigned)s->w);
    put_u16(b + 42, (unsigned)s->h);
    put_f32(b + 44, CELL);
    put_f32(b + 48, STEP);
    put_f32(b + 52, s->spawn_x);
    put_f32(b + 56, s->spawn_z);
    put_f32(b + 60, 180.0f);
    put_u16(b + 64, (unsigned)s->ent_count);
    put_u16(b + 66, 0);
    put_u16(b + 68, (unsigned)s->portal_count);
    b[70] = 0;
    b[71] = 0;
    put_u32(b + 72, cells_off);
    put_u32(b + 76, ent_off);
    put_u32(b + 80, link_off);
    put_u32(b + 84, port_off);
    put_u32(b + 88, 0xFFFFFFFFu);

    for (int z = 0; z < s->h; z++) {
        for (int x = 0; x < s->w; x++) {
            unsigned char *c = b + cells_off + ((size_t)z * (size_t)s->w + (size_t)x) * 4u;
            char ch = s->map[z][x];
            if (ch == '.' || ch == ' ') continue;
            c[0] = (unsigned char)(ch - '0');
            c[1] = CELL_EXISTS | CELL_WALK;
            if (s->stairs && s->stairs[z][x] == 'x') c[1] |= CELL_STAIR;
            if (s->water && s->water[z][x] == 'x') { c[1] |= CELL_WATER; c[1] &= (unsigned char)~CELL_WALK; }
        }
    }
    if (s->ent_count) memcpy(b + ent_off, s->ents, ent_bytes);
    if (s->portal_count) memcpy(b + port_off, s->portals, port_bytes);
    if (out_len) *out_len = total;
    return b;
}

/* Тестовый остров 6×5: плато высоты 2, рампа в (2,1) с 2 на 4, резкая ступень в (3,2),
 * дыра в (3,3) и край острова по контуру. */
#define MAP_W 6
#define MAP_H 5
static const char *const MAP5[] = {
    "222222",
    "223444", /* x=2 — рампа: запад 2, восток 4 */
    "222444", /* x=3 — ступень без рампы: перепад 1,0 */
    "222.44", /* x=3 — дыра */
    "222222",
};
static const char *const STAIRS5[] = {
    "......",
    "..x...",
    "......",
    "......",
    "......",
};

TEST(test_level_load_ok) {
    lvl_spec_t spec = {0};
    spec.w = MAP_W; spec.h = MAP_H; spec.map = MAP5; spec.stairs = STAIRS5;
    spec.spawn_x = -1.5f; spec.spawn_z = 0.0f;
    size_t len = 0;
    void *blob = build_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    CHECK_EQ(l.cells_x, MAP_W);
    CHECK_EQ(l.cells_z, MAP_H);
    CHECK_STR(l.name, "test");
    CHECK_STR(l.palette, "hub");
    CHECK_NEAR(l.cell_size, CELL, 1e-6);
    CHECK_NEAR(l.step_y, STEP, 1e-6);
    CHECK_EQ(l.entity_count, 0);

    /* Соответствие клетка ↔ мировые координаты в обе стороны. */
    for (int z = 0; z < l.cells_z; z++) {
        for (int x = 0; x < l.cells_x; x++) {
            float wx = 0.0f, wz = 0.0f;
            level_cell_center(&l, x, z, &wx, &wz);
            int bx = -1, bz = -1;
            CHECK_EQ(level_cell_at(&l, wx, wz, &bx, &bz), 1);
            CHECK_EQ(bx, x);
            CHECK_EQ(bz, z);
        }
    }
    /* Остров центрирован: левый верхний угол — в минус половину размера сетки. */
    float cx0 = 0.0f, cz0 = 0.0f;
    level_cell_center(&l, 0, 0, &cx0, &cz0);
    CHECK_NEAR(cx0, -MAP_W * CELL / 2.0 + 0.5 * CELL, 1e-6);
    CHECK_NEAR(cz0, -MAP_H * CELL / 2.0 + 0.5 * CELL, 1e-6);

    CHECK_NEAR(level_cell_top(&l, 0, 0), 2 * STEP, 1e-6);
    CHECK_NEAR(level_cell_top(&l, 4, 2), 4 * STEP, 1e-6);
    CHECK(level_cell_top(&l, 3, 3) < -1.0e8f); /* дыра */
    CHECK(level_cell(&l, -1, 0) == NULL);
    CHECK(level_cell(&l, MAP_W, 0) == NULL);

    int out_x = 0, out_z = 0;
    CHECK_EQ(level_cell_at(&l, -99.0f, 0.0f, &out_x, &out_z), 0);
    CHECK(out_x < 0);
    level_free(&l);
}

TEST(test_level_load_bad) {
    lvl_spec_t spec = {0};
    spec.w = MAP_W; spec.h = MAP_H; spec.map = MAP5;
    size_t len = 0;
    level_t l;

    void *blob = build_level(&spec, &len);
    memcpy(blob, "XLVL", 4);
    CHECK_EQ(level_load(&l, blob, len), -1);
    plat_free(blob);

    blob = build_level(&spec, &len);
    ((unsigned char *)blob)[4] = 9; /* версия */
    CHECK_EQ(level_load(&l, blob, len), -1);
    plat_free(blob);

    blob = build_level(&spec, &len);
    CHECK_EQ(level_load(&l, blob, 95), -1); /* обрезанный заголовок */
    CHECK_EQ(level_load(&l, blob, len - 4), -1); /* обрезанные клетки */
    plat_free(blob);

    blob = build_level(&spec, &len);
    put_u32((unsigned char *)blob + 72, 97); /* cells_off не кратен 4 */
    CHECK_EQ(level_load(&l, blob, len), -1);
    plat_free(blob);

    blob = build_level(&spec, &len);
    put_u32((unsigned char *)blob + 72, 0xFFFFFFF0u); /* смещение за пределами файла */
    CHECK_EQ(level_load(&l, blob, len), -1);
    plat_free(blob);

    blob = build_level(&spec, &len);
    put_u16((unsigned char *)blob + 64, 999); /* слишком много сущностей */
    CHECK_EQ(level_load(&l, blob, len), -1);
    plat_free(blob);

    blob = build_level(&spec, &len);
    put_f32((unsigned char *)blob + 44, 0.0f); /* нулевой размер клетки */
    CHECK_EQ(level_load(&l, blob, len), -1);
    plat_free(blob);

    CHECK_EQ(level_load(&l, NULL, 0), -1);
}

TEST(test_level_entities_portals) {
    level_entity_t ents[2];
    memset(ents, 0, sizeof ents);
    ents[0].type = 3; ents[0].id = 7; ents[0].x = 1.5f; ents[0].params[0] = 42;
    ents[1].type = 4; ents[1].id = 9; ents[1].z = -2.0f;
    level_portal_t ports[1];
    memset(ports, 0, sizeof ports);
    ports[0].cx = 0; ports[0].cz = 0; ports[0].w = 2; ports[0].h = 1;
    ports[0].target_level = 5; ports[0].target_entry = 9;

    lvl_spec_t spec = {0};
    spec.w = MAP_W; spec.h = MAP_H; spec.map = MAP5;
    spec.ents = ents; spec.ent_count = 2;
    spec.portals = ports; spec.portal_count = 1;
    size_t len = 0;
    void *blob = build_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    CHECK_EQ(l.entity_count, 2);
    CHECK_EQ(l.portal_count, 1);

    const level_entity_t *e = level_entity_by_id(&l, 7);
    CHECK(e != NULL);
    if (e) {
        CHECK_EQ(e->type, 3);
        CHECK_EQ(e->params[0], 42);
        CHECK_NEAR(e->x, 1.5, 1e-6);
    }
    CHECK(level_entity_by_id(&l, 8) == NULL);
    CHECK(level_entity_by_id(&l, 0) == NULL);

    float px = 0.0f, pz = 0.0f;
    level_cell_center(&l, 1, 0, &px, &pz);
    const level_portal_t *p = level_portal_at(&l, px, pz);
    CHECK(p != NULL);
    if (p) CHECK_EQ(p->target_level, 5);
    level_cell_center(&l, 3, 3, &px, &pz);
    CHECK(level_portal_at(&l, px, pz) == NULL);
    level_free(&l);
}

TEST(test_walk_floor_and_ramp) {
    lvl_spec_t spec = {0};
    spec.w = MAP_W; spec.h = MAP_H; spec.map = MAP5; spec.stairs = STAIRS5;
    size_t len = 0;
    void *blob = build_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);

    float x = 0.0f, z = 0.0f;
    level_cell_center(&l, 0, 0, &x, &z);
    CHECK_NEAR(walk_floor_at(&l, x, z), 2 * STEP, 1e-6);
    CHECK_EQ(walk_is_walkable(&l, 0, 0), 1);
    CHECK_EQ(walk_is_walkable(&l, 3, 3), 0); /* дыра */
    CHECK_EQ(walk_is_walkable(&l, -1, -1), 0);

    /* Рампа в (2,1): запад 2 шага, восток 4 шага — высота растёт вдоль x. */
    float ox = -(float)l.cells_x * l.cell_size * 0.5f;
    float oz = -(float)l.cells_z * l.cell_size * 0.5f;
    float left = walk_floor_at(&l, ox + 2.02f, oz + 1.5f);
    float mid = walk_floor_at(&l, ox + 2.5f, oz + 1.5f);
    float right = walk_floor_at(&l, ox + 2.98f, oz + 1.5f);
    CHECK(left < mid);
    CHECK(mid < right);
    CHECK_NEAR(left, 2 * STEP, 0.05);
    CHECK_NEAR(mid, 3 * STEP, 0.05);
    CHECK_NEAR(right, 4 * STEP, 0.05);

    CHECK(walk_floor_at(&l, 1000.0f, 0.0f) <= WALK_NO_FLOOR * 0.5f);
    level_free(&l);
}

TEST(test_walk_move) {
    lvl_spec_t spec = {0};
    spec.w = MAP_W; spec.h = MAP_H; spec.map = MAP5; spec.stairs = STAIRS5;
    spec.spawn_x = -1.5f; spec.spawn_z = 0.0f;
    size_t len = 0;
    void *blob = build_level(&spec, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);

    walk_pos_t p;
    level_cell_center(&l, 1, 2, &p.x, &p.z);
    p.y = walk_floor_at(&l, p.x, p.z);
    CHECK_NEAR(p.y, 2 * STEP, 1e-6);

    /* Шаг внутри плато проходит. */
    float before_x = p.x;
    CHECK_EQ(walk_move(&l, &p, 0.2f, 0.0f, 0.3f, 0.6f), 1);
    CHECK(p.x > before_x);

    /* За край острова не пускает: идём на запад до упора. */
    for (int i = 0; i < 60; i++) walk_move(&l, &p, -0.2f, 0.0f, 0.3f, 0.6f);
    CHECK(p.x >= -(float)l.cells_x * l.cell_size * 0.5f);
    CHECK(walk_floor_at(&l, p.x, p.z) > -1.0e8f);
    float wall_x = p.x;
    CHECK_EQ(walk_move(&l, &p, -0.2f, 0.0f, 0.3f, 0.6f), 0); /* дальше некуда */
    CHECK_NEAR(p.x, wall_x, 1e-6);

    /* Скольжение вдоль стены: движение по диагонали в стену сдвигает только по свободной оси. */
    walk_pos_t s;
    level_cell_center(&l, 0, 2, &s.x, &s.z);
    s.y = walk_floor_at(&l, s.x, s.z);
    float sx = s.x, sz = s.z;
    CHECK_EQ(walk_move(&l, &s, -0.5f, 0.2f, 0.3f, 0.6f), 1);
    CHECK_NEAR(s.x, sx, 1e-6); /* на запад — стена */
    CHECK(s.z > sz);           /* на юг — прошло */

    /* Резкая ступень: перепад 1,0 больше обычного шага, но проходим при большом step_max. */
    walk_pos_t hi;
    level_cell_center(&l, 2, 2, &hi.x, &hi.z);
    hi.y = walk_floor_at(&l, hi.x, hi.z);
    CHECK_NEAR(hi.y, 2 * STEP, 1e-6);
    float hx = hi.x;
    CHECK_EQ(walk_move(&l, &hi, 0.6f, 0.0f, 0.3f, 0.6f), 0); /* перепад 1,0 > step_max 0,6 */
    CHECK_NEAR(hi.x, hx, 1e-6);
    CHECK_EQ(walk_move(&l, &hi, 0.6f, 0.0f, 0.3f, 1.2f), 1); /* с большим шагом поднялся */
    CHECK_NEAR(hi.y, 4 * STEP, 1e-6);

    /* В дыру не пускает: от (2,3) на восток. */
    walk_pos_t hole;
    level_cell_center(&l, 2, 3, &hole.x, &hole.z);
    hole.y = walk_floor_at(&l, hole.x, hole.z);
    float hole_x = hole.x;
    CHECK_EQ(walk_move(&l, &hole, 0.6f, 0.0f, 0.3f, 1.2f), 0);
    CHECK_NEAR(hole.x, hole_x, 1e-6);

    /* walk_spawn ставит на пол. */
    walk_pos_t sp;
    walk_spawn(&l, &sp);
    CHECK_NEAR(sp.x, -1.5, 1e-6);
    CHECK_NEAR(sp.z, 0.0, 1e-6);
    CHECK_NEAR(sp.y, 2 * STEP, 1e-6);
    level_free(&l);
}

void tests_level(void) {
    puts("level/walk tests");
    RUN(test_level_load_ok);
    RUN(test_level_load_bad);
    RUN(test_level_entities_portals);
    RUN(test_walk_floor_and_ramp);
    RUN(test_walk_move);
}
