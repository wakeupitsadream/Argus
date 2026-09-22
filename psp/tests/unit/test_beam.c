/* Тесты семейства «свет» (core/puzzle_beam.c): марш луча по сетке, зеркала-уголки,
 * призмы, приёмники, спящие стены, двери и поворот зеркала с анимацией. */
#include "minitest.h"
#include "tests.h"
#include "entity.h"
#include "entity_types.h"
#include "level.h"
#include "platform.h"
#include "puzzles.h"
#include <string.h>

void tests_beam(void);

#define B_CELL 1.0f
#define B_STEP 0.5f
#define B_W 8
#define B_H 8
#define B_Y 1.0f       /* высота плато: 2 шага × 0,5 — на ней же идёт луч */
#define B_MAX_ENTS 128
#define B_TURN_FRAMES 15 /* 0,25 с при шаге 1/60 */

/* Направления: те же индексы, что в контракте (0 = +X, 1 = +Z, 2 = -X, 3 = -Z). */
enum { D_E = 0, D_S = 1, D_W = 2, D_N = 3 };
static const int DIR_DX[4] = { 1, 0, -1, 0 };
static const int DIR_DZ[4] = { 0, 1, 0, -1 };
/* yaw излучателя, дающий движение в этом направлении (0° = +Z, 90° = +X). */
static const float DIR_YAW[4] = { 90.0f, 0.0f, 270.0f, 180.0f };

/* ——— сборка синтетического ALVL (по контракту docs/FORMATS.md) ——— */

static void put_u16(unsigned char *p, unsigned v) { unsigned short t = (unsigned short)v; memcpy(p, &t, 2); }
static void put_u32(unsigned char *p, unsigned v) { memcpy(p, &v, 4); }
static void put_f32(unsigned char *p, float v) { memcpy(p, &v, 4); }

/* Центр клетки: остров центрирован в начале координат. */
static float cell_cx(int c) { return -(float)B_W * B_CELL * 0.5f + ((float)c + 0.5f) * B_CELL; }
static float cell_cz(int c) { return -(float)B_H * B_CELL * 0.5f + ((float)c + 0.5f) * B_CELL; }

/* Строка описания сущности: state ставится уже в живом состоянии (в файле его нет). */
typedef struct {
    int id, type, cx, cz, state;
    float yaw;
} brow_t;

typedef struct {
    level_t level;
    entities_t es;
} bscene_t;

/* map — B_H строк по B_W символов: '.' пусто, '0'..'9' высота в шагах; NULL — плато 2. */
static void *build_beam_level(const char *const *map,
                              const level_entity_t *ents, int ent_count,
                              const level_link_t *links, int link_count, size_t *out_len) {
    size_t cells_bytes = (size_t)B_W * (size_t)B_H * 4u;
    size_t ent_bytes = (size_t)ent_count * sizeof(level_entity_t);
    size_t link_bytes = (size_t)link_count * sizeof(level_link_t);
    unsigned cells_off = 96;
    unsigned ent_off = (unsigned)(cells_off + cells_bytes);
    unsigned link_off = (unsigned)(ent_off + ent_bytes);
    size_t total = (size_t)link_off + link_bytes;

    unsigned char *b = (unsigned char *)plat_alloc16(total + 16);
    memset(b, 0, total + 16);
    memcpy(b, "ALVL", 4);
    put_u32(b + 4, 1);
    memcpy(b + 8, "beam", 4);
    memcpy(b + 24, "hub", 3);
    put_u16(b + 40, B_W);
    put_u16(b + 42, B_H);
    put_f32(b + 44, B_CELL);
    put_f32(b + 48, B_STEP);
    put_f32(b + 52, 0.0f);
    put_f32(b + 56, 0.0f);
    put_f32(b + 60, 0.0f);
    put_u16(b + 64, (unsigned)ent_count);
    put_u16(b + 66, (unsigned)link_count);
    put_u16(b + 68, 0);
    put_u32(b + 72, cells_off);
    put_u32(b + 76, ent_off);
    put_u32(b + 80, link_off);
    put_u32(b + 84, link_off);
    put_u32(b + 88, 0xFFFFFFFFu);

    for (int z = 0; z < B_H; z++) {
        for (int x = 0; x < B_W; x++) {
            unsigned char *c = b + cells_off + ((size_t)z * (size_t)B_W + (size_t)x) * 4u;
            char ch = map ? map[z][x] : '2';
            if (ch == '.' || ch == ' ') continue;
            c[0] = (unsigned char)(ch - '0');
            c[1] = CELL_EXISTS | CELL_WALK;
        }
    }
    if (ent_bytes) memcpy(b + ent_off, ents, ent_bytes);
    if (link_bytes) memcpy(b + link_off, links, link_bytes);
    if (out_len) *out_len = total;
    return b;
}

/* Собирает уровень и живое состояние сущностей; освобождать scene_free. */
static int scene_make(bscene_t *sc, const char *const *map,
                      const brow_t *rows, int row_count,
                      const level_link_t *links, int link_count) {
    level_entity_t ents[B_MAX_ENTS];
    memset(ents, 0, sizeof ents);
    if (row_count > B_MAX_ENTS) return -1;
    for (int i = 0; i < row_count; i++) {
        ents[i].type = (unsigned short)rows[i].type;
        ents[i].id = (unsigned short)rows[i].id;
        ents[i].x = cell_cx(rows[i].cx);
        ents[i].z = cell_cz(rows[i].cz);
        ents[i].y = B_Y;
        ents[i].yaw_deg = rows[i].yaw;
        ents[i].name_str_id = 0xFFFF;
    }
    size_t len = 0;
    void *blob = build_beam_level(map, ents, row_count, links, link_count, &len);
    if (level_load(&sc->level, blob, len) != 0) { plat_free(blob); return -1; }
    entities_init(&sc->es, &sc->level, NULL);
    for (int i = 0; i < row_count; i++) {
        entity_t *e = entities_by_id(&sc->es, rows[i].id);
        if (e) e->state = (unsigned char)rows[i].state;
    }
    return 0;
}

static void scene_free(bscene_t *sc) { level_free(&sc->level); }

/* ——— проверки ——— */

static void check_seg(const beam_seg_t *s, int x0, int z0, int x1, int z1) {
    CHECK_NEAR(s->x0, cell_cx(x0), 1e-5);
    CHECK_NEAR(s->z0, cell_cz(z0), 1e-5);
    CHECK_NEAR(s->x1, cell_cx(x1), 1e-5);
    CHECK_NEAR(s->z1, cell_cz(z1), 1e-5);
    CHECK_NEAR(s->y, B_Y, 1e-5);
}

/* Последняя клетка острова в направлении dir от (cx, cz). */
static void edge_cell(int cx, int cz, int dir, int *ex, int *ez) {
    int x = cx, z = cz;
    while (x + DIR_DX[dir] >= 0 && x + DIR_DX[dir] < B_W &&
           z + DIR_DZ[dir] >= 0 && z + DIR_DZ[dir] < B_H) {
        x += DIR_DX[dir];
        z += DIR_DZ[dir];
    }
    *ex = x;
    *ez = z;
}

static int recv_on(const bscene_t *sc, int id) {
    const entity_t *e = entities_by_id_const(&sc->es, id);
    return e && (e->outputs & 1u) ? 1 : 0;
}

/* ——— прямой луч и приёмники ——— */

enum { ID_EMIT = 1, ID_RECV = 2, ID_RECV2 = 3, ID_MIRROR = 4, ID_PRISM = 5,
       ID_WALL = 6, ID_DOOR = 7, ID_LEVER = 8, ID_BLOCK = 9 };

TEST(test_beam_straight) {
    static const brow_t rows[] = {
        { ID_EMIT,  ENT_EMITTER,  1, 3, 0, 90.0f }, /* на восток */
        { ID_RECV,  ENT_RECEIVER, 6, 3, 0, 0.0f },
        { ID_RECV2, ENT_RECEIVER, 6, 6, 0, 0.0f }, /* в стороне — должен остаться погашен */
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 3, NULL, 0), 0);

    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 1);
    if (beam.count == 1) check_seg(&beam.segs[0], 1, 3, 6, 3);
    CHECK_EQ(beam.hit_receiver_id, ID_RECV);
    CHECK_EQ(recv_on(&sc, ID_RECV), 1);
    CHECK_EQ(recv_on(&sc, ID_RECV2), 0);

    /* Повторный вызов не накапливает отрезки и даёт тот же результат. */
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 1);
    CHECK_EQ(recv_on(&sc, ID_RECV), 1);

    /* Без излучателя приёмник гаснет, отрезков нет. */
    entities_by_id(&sc.es, ID_EMIT)->active = 0;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 0);
    CHECK_EQ(beam.hit_receiver_id, 0);
    CHECK_EQ(recv_on(&sc, ID_RECV), 0);
    scene_free(&sc);
}

TEST(test_beam_no_emitters) {
    static const brow_t rows[] = {
        { ID_MIRROR, ENT_MIRROR,   3, 3, 0, 0.0f },
        { ID_RECV,   ENT_RECEIVER, 6, 3, 0, 0.0f },
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 2, NULL, 0), 0);

    /* Приёмник «залип» включённым с прошлого кадра — трассировка обязана погасить. */
    entities_by_id(&sc.es, ID_RECV)->outputs = 1;
    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 0);
    CHECK_EQ(beam.hit_receiver_id, 0);
    CHECK_EQ(recv_on(&sc, ID_RECV), 0);
    scene_free(&sc);
}

/* ——— таблица зеркала: 4 состояния × 4 входа ——— */

/* Ожидаемая таблица из ТЗ: [состояние][направление входа] → выход, -1 — луч гаснет.
 * Записана независимо от реализации — это и есть спецификация. */
static const int EXPECT_OUT[4][4] = {
    /* 0: восток→север, север→восток */ { D_N, -1,  -1,  D_E },
    /* 1: восток→юг,    юг→восток    */ { D_S, D_E, -1,  -1  },
    /* 2: запад→юг,     юг→запад     */ { -1,  D_W, D_S, -1  },
    /* 3: запад→север,  север→запад  */ { -1,  -1,  D_N, D_W },
};

TEST(test_beam_mirror_table) {
    const int mx = 3, mz = 3;
    for (int state = 0; state < 4; state++) {
        for (int in_dir = 0; in_dir < 4; in_dir++) {
            /* Излучатель в двух клетках «до» зеркала, чтобы луч пришёл нужной стороной. */
            int ex = mx - 2 * DIR_DX[in_dir];
            int ez = mz - 2 * DIR_DZ[in_dir];
            brow_t rows[2] = {
                { ID_EMIT,   ENT_EMITTER, ex, ez, 0,     DIR_YAW[in_dir] },
                { ID_MIRROR, ENT_MIRROR,  mx, mz, state, 0.0f },
            };
            bscene_t sc;
            CHECK_EQ(scene_make(&sc, NULL, rows, 2, NULL, 0), 0);

            beam_t beam;
            beam_trace(&sc.es, &beam);

            int out = EXPECT_OUT[state][in_dir];
            if (out < 0) {
                /* Зеркало гасит: один отрезок, упирающийся в клетку зеркала. */
                CHECK_EQ(beam.count, 1);
                if (beam.count == 1) check_seg(&beam.segs[0], ex, ez, mx, mz);
            } else {
                CHECK_EQ(beam.count, 2);
                if (beam.count == 2) {
                    int tx = 0, tz = 0;
                    edge_cell(mx, mz, out, &tx, &tz);
                    check_seg(&beam.segs[0], ex, ez, mx, mz);
                    check_seg(&beam.segs[1], mx, mz, tx, tz);
                }
            }
            CHECK_EQ(beam.hit_receiver_id, 0);
            scene_free(&sc);
        }
    }
}

TEST(test_beam_mirror_to_receiver) {
    /* Луч на восток, зеркало в состоянии 0 (восток→север) заводит его за угол. */
    static const brow_t rows[] = {
        { ID_EMIT,   ENT_EMITTER,  1, 3, 0, 90.0f },
        { ID_MIRROR, ENT_MIRROR,   3, 3, 0, 0.0f },
        { ID_RECV,   ENT_RECEIVER, 3, 1, 0, 0.0f }, /* на север от зеркала */
        { ID_RECV2,  ENT_RECEIVER, 3, 5, 0, 0.0f }, /* на юг от зеркала */
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 4, NULL, 0), 0);

    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 2);
    if (beam.count == 2) {
        check_seg(&beam.segs[0], 1, 3, 3, 3);
        check_seg(&beam.segs[1], 3, 3, 3, 1);
    }
    CHECK_EQ(recv_on(&sc, ID_RECV), 1);
    CHECK_EQ(recv_on(&sc, ID_RECV2), 0);
    CHECK_EQ(beam.hit_receiver_id, ID_RECV);

    /* Поворот в состояние 1 (восток→юг) переводит луч на второй приёмник. */
    CHECK_EQ(mirror_rotate(&sc.es, ID_MIRROR), 1);
    CHECK_EQ(entities_by_id(&sc.es, ID_MIRROR)->state, 1);
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 2);
    if (beam.count == 2) check_seg(&beam.segs[1], 3, 3, 3, 5);
    CHECK_EQ(recv_on(&sc, ID_RECV), 0);
    CHECK_EQ(recv_on(&sc, ID_RECV2), 1);
    CHECK_EQ(beam.hit_receiver_id, ID_RECV2);
    scene_free(&sc);
}

/* ——— призма ——— */

TEST(test_beam_prism_splits) {
    /* Призма пропускает луч насквозь и рождает копию с поворотом влево (восток → север). */
    static const brow_t rows[] = {
        { ID_EMIT,  ENT_EMITTER,  1, 3, 0, 90.0f },
        { ID_PRISM, ENT_PRISM,    3, 3, 0, 0.0f },
        { ID_RECV,  ENT_RECEIVER, 6, 3, 0, 0.0f }, /* прямой луч */
        { ID_RECV2, ENT_RECEIVER, 3, 1, 0, 0.0f }, /* ответвление */
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 4, NULL, 0), 0);

    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 2);
    if (beam.count == 2) {
        check_seg(&beam.segs[0], 1, 3, 6, 3); /* насквозь до дальнего приёмника */
        check_seg(&beam.segs[1], 3, 3, 3, 1); /* копия — влево от направления */
    }
    CHECK_EQ(recv_on(&sc, ID_RECV), 1);
    CHECK_EQ(recv_on(&sc, ID_RECV2), 1);
    scene_free(&sc);
}

TEST(test_beam_ray_limit) {
    /* Четыре призмы подряд: лучей одновременно не больше четырёх, четвёртая не делит. */
    static const brow_t rows[] = {
        { ID_EMIT, ENT_EMITTER, 0, 3, 0, 90.0f },
        { 10,      ENT_PRISM,   2, 3, 0, 0.0f },
        { 11,      ENT_PRISM,   3, 3, 0, 0.0f },
        { 12,      ENT_PRISM,   4, 3, 0, 0.0f },
        { 13,      ENT_PRISM,   5, 3, 0, 0.0f },
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 5, NULL, 0), 0);

    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 4); /* прямой + три ответвления; призма в (5,3) уже без слота */
    if (beam.count == 4) {
        check_seg(&beam.segs[0], 0, 3, 7, 3);
        check_seg(&beam.segs[1], 2, 3, 2, 0);
        check_seg(&beam.segs[2], 3, 3, 3, 0);
        check_seg(&beam.segs[3], 4, 3, 4, 0);
    }
    scene_free(&sc);
}

/* ——— спящая стена, дверь, блок ——— */

TEST(test_beam_sleep_wall) {
    static const brow_t rows[] = {
        { ID_EMIT, ENT_EMITTER,    1, 3, 0, 90.0f },
        { ID_WALL, ENT_SLEEP_WALL, 4, 3, 0, 0.0f },
        { ID_RECV, ENT_RECEIVER,   6, 3, 0, 0.0f },
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 3, NULL, 0), 0);
    entity_t *wall = entities_by_id(&sc.es, ID_WALL);

    /* На стену не смотрят — луч гаснет в её клетке. */
    beam_t beam;
    wall->watched = 0;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 1);
    if (beam.count == 1) check_seg(&beam.segs[0], 1, 3, 4, 3);
    CHECK_EQ(recv_on(&sc, ID_RECV), 0);

    /* Под взглядом стена прозрачна — луч доходит до приёмника одним отрезком. */
    wall->watched = 1;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 1);
    if (beam.count == 1) check_seg(&beam.segs[0], 1, 3, 6, 3);
    CHECK_EQ(recv_on(&sc, ID_RECV), 1);
    CHECK_EQ(beam.hit_receiver_id, ID_RECV);
    scene_free(&sc);
}

TEST(test_beam_door_and_block) {
    static const brow_t rows[] = {
        { ID_EMIT,  ENT_EMITTER,  1, 3, 0, 90.0f },
        { ID_DOOR,  ENT_DOOR,     4, 3, 0, 0.0f },
        { ID_RECV,  ENT_RECEIVER, 6, 3, 0, 0.0f },
        { ID_LEVER, ENT_LEVER,    1, 6, 0, 0.0f },
        { ID_BLOCK, ENT_BLOCK,    4, 5, 0, 0.0f },
        { 14,       ENT_EMITTER,  1, 5, 0, 90.0f },
        { 15,       ENT_RECEIVER, 6, 5, 0, 0.0f },
    };
    static const level_link_t links[] = {
        { ID_LEVER, 0, 0, ID_DOOR, 0, 0 },
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 7, links, 1), 0);

    /* Закрытая дверь гасит; блок гасит всегда. */
    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(entities_by_id(&sc.es, ID_DOOR)->state, 0);
    CHECK_EQ(beam.count, 2);
    if (beam.count == 2) {
        check_seg(&beam.segs[0], 1, 3, 4, 3);
        check_seg(&beam.segs[1], 1, 5, 4, 5);
    }
    CHECK_EQ(recv_on(&sc, ID_RECV), 0);
    CHECK_EQ(recv_on(&sc, 15), 0);

    /* Рычаг открывает дверь (связь разносит entities_propagate) — луч проходит. */
    entity_t *lever = entities_by_id(&sc.es, ID_LEVER);
    lever->state = 1;
    lever->outputs = 1;
    entities_propagate(&sc.es);
    CHECK_EQ(entities_by_id(&sc.es, ID_DOOR)->state, 1);

    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, 2);
    if (beam.count == 2) check_seg(&beam.segs[0], 1, 3, 6, 3);
    CHECK_EQ(recv_on(&sc, ID_RECV), 1);
    CHECK_EQ(recv_on(&sc, 15), 0); /* блок по-прежнему держит второй луч */
    scene_free(&sc);
}

/* ——— геометрия ——— */

static const char *const MAP_WALL[B_H] = {
    "22222222",
    "22222222",
    "22222222",
    "22223222", /* (4,3) — на шаг выше луча */
    "22222222",
    "22222222",
    "22222222",
    "22222222",
};

static const char *const MAP_HOLE[B_H] = {
    "22222222",
    "22222222",
    "22222222",
    "2222.222", /* (4,3) — дыра */
    "22222222",
    "22222222",
    "22222222",
    "22222222",
};

static const char *const MAP_LOW[B_H] = {
    "22222222",
    "22222222",
    "22222222",
    "22221222", /* (4,3) ниже луча — не мешает */
    "22222222",
    "22222222",
    "22222222",
    "22222222",
};

TEST(test_beam_geometry) {
    static const brow_t rows[] = {
        { ID_EMIT, ENT_EMITTER,  1, 3, 0, 90.0f },
        { ID_RECV, ENT_RECEIVER, 6, 3, 0, 0.0f },
    };
    beam_t beam;

    /* Высокая клетка гасит луч: отрезок кончается в последней пройденной клетке. */
    bscene_t hi;
    CHECK_EQ(scene_make(&hi, MAP_WALL, rows, 2, NULL, 0), 0);
    beam_trace(&hi.es, &beam);
    CHECK_EQ(beam.count, 1);
    if (beam.count == 1) check_seg(&beam.segs[0], 1, 3, 3, 3);
    CHECK_EQ(recv_on(&hi, ID_RECV), 0);
    scene_free(&hi);

    /* Пустота: луч уходит в неё и заканчивается. */
    bscene_t hole;
    CHECK_EQ(scene_make(&hole, MAP_HOLE, rows, 2, NULL, 0), 0);
    beam_trace(&hole.es, &beam);
    CHECK_EQ(beam.count, 1);
    if (beam.count == 1) check_seg(&beam.segs[0], 1, 3, 3, 3);
    CHECK_EQ(recv_on(&hole, ID_RECV), 0);
    scene_free(&hole);

    /* Клетка ниже луча помехой не считается. */
    bscene_t low;
    CHECK_EQ(scene_make(&low, MAP_LOW, rows, 2, NULL, 0), 0);
    beam_trace(&low.es, &beam);
    CHECK_EQ(beam.count, 1);
    if (beam.count == 1) check_seg(&beam.segs[0], 1, 3, 6, 3);
    CHECK_EQ(recv_on(&low, ID_RECV), 1);
    scene_free(&low);
}

/* ——— пределы ——— */

TEST(test_beam_mirror_loop) {
    /* Замкнутая петля из четырёх зеркал: (2,2)→юг→(2,4)→восток→(4,4)→север→(4,2)→запад→…
     * Излучатель стоит в (3,2) — его клетка для луча прозрачна, через неё петля замыкается.
     * Трассировка обязана завершиться на ограничении шагов и не переполнить массив. */
    static const brow_t rows[] = {
        { ID_EMIT, ENT_EMITTER, 3, 2, 0, 270.0f }, /* на запад, в первое зеркало */
        { 20,      ENT_MIRROR,  2, 2, 2, 0.0f },   /* запад→юг */
        { 21,      ENT_MIRROR,  2, 4, 1, 0.0f },   /* юг→восток */
        { 22,      ENT_MIRROR,  4, 4, 0, 0.0f },   /* восток→север */
        { 23,      ENT_MIRROR,  4, 2, 3, 0.0f },   /* север→запад */
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 5, NULL, 0), 0);

    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK(beam.count > 4);                      /* петля прокрутилась несколько раз */
    CHECK(beam.count <= BEAM_MAX_SEGMENTS);     /* и не вышла за массив */
    CHECK_EQ(beam.hit_receiver_id, 0);
    if (beam.count > 0) check_seg(&beam.segs[0], 3, 2, 2, 2);
    scene_free(&sc);
}

TEST(test_beam_segment_cap) {
    /* Излучателей заведомо больше, чем отрезков в beam_t: лишние молча отбрасываются. */
    brow_t rows[56];
    int n = 0;
    for (int z = 0; z < B_H; z++) {
        for (int x = 0; x < 7; x++) {
            rows[n].id = n + 1;
            rows[n].type = ENT_EMITTER;
            rows[n].cx = x;
            rows[n].cz = z;
            rows[n].state = 0;
            rows[n].yaw = 90.0f;
            n++;
        }
    }
    CHECK_EQ(n, 56);
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, n, NULL, 0), 0);

    beam_t beam;
    beam_trace(&sc.es, &beam);
    CHECK_EQ(beam.count, BEAM_MAX_SEGMENTS);
    scene_free(&sc);
}

/* ——— поворот зеркала ——— */

TEST(test_beam_mirror_rotate) {
    static const brow_t rows[] = {
        { ID_MIRROR, ENT_MIRROR,   3, 3, 0, 45.0f },
        { ID_PRISM,  ENT_PRISM,    5, 3, 0, 0.0f },
        { ID_RECV,   ENT_RECEIVER, 6, 6, 0, 0.0f },
    };
    bscene_t sc;
    CHECK_EQ(scene_make(&sc, NULL, rows, 3, NULL, 0), 0);
    entity_t *m = entities_by_id(&sc.es, ID_MIRROR);
    float yaw0 = m->yaw;
    CHECK_NEAR(yaw0, 45.0, 1e-5);

    /* Полный круг 0 → 1 → 2 → 3 → 0, каждый шаг — 0,25 с анимации. */
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(mirror_rotate(&sc.es, ID_MIRROR), 1);
        CHECK_EQ(m->state, (i + 1) & 3);
        CHECK_EQ(sc.es.busy_frames, B_TURN_FRAMES);
        CHECK(!tween_done(&m->anim));
        CHECK_NEAR(m->anim.from, yaw0 + 90.0 * i, 1e-4);
        CHECK_NEAR(m->anim.to, yaw0 + 90.0 * (i + 1), 1e-4);

        /* Во время анимации повторный поворот не срабатывает. */
        CHECK_EQ(mirror_rotate(&sc.es, ID_MIRROR), 0);
        CHECK_EQ(m->state, (i + 1) & 3);
        /* И соседнее зеркало тоже заблокировано — крутится что-то одно. */
        CHECK_EQ(mirror_rotate(&sc.es, ID_PRISM), 0);

        for (int f = 0; f < B_TURN_FRAMES; f++) entities_tick(&sc.es, NULL, 0);
        CHECK_EQ(sc.es.busy_frames, 0);
        CHECK(tween_done(&m->anim));
        CHECK_NEAR(m->anim.value, yaw0 + 90.0 * (i + 1), 1e-3);
    }
    CHECK_EQ(m->state, 0);
    CHECK_NEAR(m->yaw, yaw0 + 360.0, 1e-3);

    /* Призма крутится теми же правилами. */
    CHECK_EQ(mirror_rotate(&sc.es, ID_PRISM), 1);
    CHECK_EQ(entities_by_id(&sc.es, ID_PRISM)->state, 1);
    for (int f = 0; f < B_TURN_FRAMES; f++) entities_tick(&sc.es, NULL, 0);

    /* Не зеркало, неизвестный id и неактивная сущность — отказ. */
    CHECK_EQ(mirror_rotate(&sc.es, ID_RECV), 0);
    CHECK_EQ(mirror_rotate(&sc.es, 999), 0);
    CHECK_EQ(mirror_rotate(&sc.es, 0), 0);
    CHECK_EQ(mirror_rotate(NULL, ID_MIRROR), 0);
    m->active = 0;
    CHECK_EQ(mirror_rotate(&sc.es, ID_MIRROR), 0);
    scene_free(&sc);
}

void tests_beam(void) {
    puts("beam tests");
    RUN(test_beam_straight);
    RUN(test_beam_no_emitters);
    RUN(test_beam_mirror_table);
    RUN(test_beam_mirror_to_receiver);
    RUN(test_beam_prism_splits);
    RUN(test_beam_ray_limit);
    RUN(test_beam_sleep_wall);
    RUN(test_beam_door_and_block);
    RUN(test_beam_geometry);
    RUN(test_beam_mirror_loop);
    RUN(test_beam_segment_cap);
    RUN(test_beam_mirror_rotate);
}
