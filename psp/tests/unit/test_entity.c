/* Тесты живых сущностей уровня: флаги мира при инициализации, связи, взаимодействие,
 * сбор коллекционных предметов и фирменная механика «спящих» (GDD §1.4). */
#include "minitest.h"
#include "tests.h"
#include "entity.h"
#include "entity_types.h"
#include "camera.h"
#include "math3.h"
#include "level.h"
#include "platform.h"
#include "world.h"
#include <math.h>
#include <string.h>

void tests_entity(void);

#define E_CELL 1.0f
#define E_STEP 0.5f
#define E_W 8
#define E_H 8
#define E_Y 1.0f      /* высота плато: 2 шага × 0,5 */
#define E_REACH 0.6f

/* ——— сборка синтетического ALVL (по контракту docs/FORMATS.md) ——— */

static void put_u16(unsigned char *p, unsigned v) { unsigned short t = (unsigned short)v; memcpy(p, &t, 2); }
static void put_u32(unsigned char *p, unsigned v) { memcpy(p, &v, 4); }
static void put_f32(unsigned char *p, float v) { memcpy(p, &v, 4); }

/* Центр клетки по одной оси: остров центрирован в начале координат. */
static float cell_c(int c, int n) { return -(float)n * E_CELL * 0.5f + ((float)c + 0.5f) * E_CELL; }

/* Плоский остров E_W×E_H высоты 2 с сущностями и связями. */
static void *build_entity_level(const level_entity_t *ents, int ent_count,
                                const level_link_t *links, int link_count, size_t *out_len) {
    size_t cells_bytes = (size_t)E_W * (size_t)E_H * 4u;
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
    memcpy(b + 8, "ents", 4);
    memcpy(b + 24, "hub", 3);
    put_u16(b + 40, E_W);
    put_u16(b + 42, E_H);
    put_f32(b + 44, E_CELL);
    put_f32(b + 48, E_STEP);
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

    for (size_t i = 0; i < cells_bytes; i += 4) {
        b[cells_off + i] = 2;                               /* высота */
        b[cells_off + i + 1] = CELL_EXISTS | CELL_WALK;      /* флаги */
    }
    if (ent_bytes) memcpy(b + ent_off, ents, ent_bytes);
    if (link_bytes) memcpy(b + link_off, links, link_bytes);
    if (out_len) *out_len = total;
    return b;
}

/* Сцена теста: рычаг → дверь, приёмник → большой глаз, коллекционные предметы,
 * два спящих объекта и заведомо битые связи (должны игнорироваться). */
enum { ID_LEVER = 1, ID_DOOR = 2, ID_EYE = 3, ID_BIG = 4, ID_RECV = 5,
       ID_SLEEP = 6, ID_SLEEP2 = 7, ID_FEATHER = 8 };
#define SCENE_ENTS 8

typedef struct {
    int id, type, cx, cz, p0, p1;
} scene_row_t;

static const scene_row_t SCENE[SCENE_ENTS] = {
    { ID_LEVER,   ENT_LEVER,     1, 1,  0,  0 },
    { ID_DOOR,    ENT_DOOR,      2, 1,  0,  0 },
    { ID_EYE,     ENT_SMALL_EYE, 3, 1,  0,  0 },
    { ID_BIG,     ENT_BIG_EYE,   4, 1,  0,  0 },
    { ID_RECV,    ENT_RECEIVER,  5, 1,  0,  0 },
    { ID_SLEEP,   ENT_SLEEPER,   3, 5, 20, 60 }, /* радиус 2,0 клетки, 60 град/с */
    { ID_SLEEP2,  ENT_SLEEPER,   6, 6,  0,  0 }, /* по умолчанию: 1,0 клетки, 30 град/с */
    { ID_FEATHER, ENT_FEATHER,   1, 6,  0,  0 },
};

static const level_link_t SCENE_LINKS[] = {
    { ID_LEVER, 0, 0, ID_DOOR, 0, 0 },  /* рычаг → дверь */
    { ID_RECV,  0, 0, ID_BIG,  0, 0 },  /* приёмник → большой глаз */
    { 99,       0, 0, ID_DOOR, 0, 0 },  /* неизвестный источник */
    { ID_LEVER, 0, 0, 98,      0, 0 },  /* неизвестный получатель */
    { ID_LEVER, 7, 0, ID_DOOR, 1, 0 },  /* выход за пределами ENT_OUT_BITS */
    { ID_LEVER, 0, 0, ID_DOOR, 9, 0 },  /* вход за пределами ENT_IN_BITS */
};
#define SCENE_LINK_COUNT ((int)(sizeof SCENE_LINKS / sizeof SCENE_LINKS[0]))

/* Загружает сцену; освобождать level_free. */
static int scene_load(level_t *l) {
    level_entity_t ents[SCENE_ENTS];
    memset(ents, 0, sizeof ents);
    for (int i = 0; i < SCENE_ENTS; i++) {
        ents[i].type = (unsigned short)SCENE[i].type;
        ents[i].id = (unsigned short)SCENE[i].id;
        ents[i].x = cell_c(SCENE[i].cx, E_W);
        ents[i].z = cell_c(SCENE[i].cz, E_H);
        ents[i].y = E_Y;
        ents[i].name_str_id = 0xFFFF;
        ents[i].params[0] = SCENE[i].p0;
        ents[i].params[1] = SCENE[i].p1;
    }
    size_t len = 0;
    void *blob = build_entity_level(ents, SCENE_ENTS, SCENE_LINKS, SCENE_LINK_COUNT, &len);
    return level_load(l, blob, len);
}

/* Мировые координаты сущности сцены по её id. */
static void scene_pos(int id, float *x, float *z) {
    *x = 0.0f;
    *z = 0.0f;
    for (int i = 0; i < SCENE_ENTS; i++) {
        if (SCENE[i].id != id) continue;
        *x = cell_c(SCENE[i].cx, E_W);
        *z = cell_c(SCENE[i].cz, E_H);
        return;
    }
}

/* Камера, нацеленная в точку (для проверки наблюдения). */
static void cam_at(frame_cam_t *out, float x, float y, float z) {
    float target[3] = { x, y, z };
    camera_t c;
    camera_init(&c, 0, 0, target);
    camera_fill(&c, out);
}

/* ——— тесты ——— */

TEST(test_entity_init_flags) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    entities_t es;

    /* Чистый мир: всё на месте, ничего не включено. */
    world_reset(&w);
    entities_init(&es, &l, &w);
    CHECK_EQ(es.count, SCENE_ENTS);
    CHECK_EQ(es.busy_frames, 0);
    CHECK_EQ(es.last_event, 0);
    CHECK_EQ(es.water_steps, 0); /* шлюзов на уровне нет */
    CHECK(es.level == &l);
    CHECK(es.world == &w);
    for (int i = 0; i < es.count; i++) {
        const entity_t *e = &es.items[i];
        CHECK(e->def != NULL);
        CHECK_EQ(e->active, 1);
        CHECK_EQ(e->state, 0);
        CHECK_EQ(e->outputs, 0);
        CHECK_EQ(e->inputs, 0);
        CHECK_EQ(e->watched, 0);
        CHECK_NEAR(e->phase, 0.0, 1e-6);
        CHECK(tween_done(&e->anim));
        CHECK_NEAR(e->x, e->def->x, 1e-6);
        CHECK_NEAR(e->y, e->def->y, 1e-6);
        CHECK_NEAR(e->z, e->def->z, 1e-6);
        CHECK_NEAR(e->yaw, e->def->yaw_deg, 1e-6);
    }
    CHECK_EQ(entities_eyes_open(&es), 0);

    /* Поиск по id. */
    const entity_t *lever = entities_by_id_const(&es, ID_LEVER);
    CHECK(lever != NULL);
    if (lever) CHECK_EQ(lever->def->type, ENT_LEVER);
    CHECK(entities_by_id(&es, 999) == NULL);
    CHECK(entities_by_id(&es, 0) == NULL);
    CHECK(entities_by_id(&es, -7) == NULL);
    CHECK(entities_by_id_const(&es, 999) == NULL);

    /* Мир с флагами: глаз собран, перо собрано, рычаг включён, большой глаз открыт. */
    world_reset(&w);
    world_set_flag(&w, ID_EYE, 1);
    world_set_flag(&w, ID_FEATHER, 1);
    world_set_flag(&w, ID_LEVER, 1);
    world_set_flag(&w, ID_BIG, 1);
    entities_init(&es, &l, &w);

    const entity_t *eye = entities_by_id_const(&es, ID_EYE);
    const entity_t *feather = entities_by_id_const(&es, ID_FEATHER);
    CHECK(eye != NULL && feather != NULL);
    if (eye) CHECK_EQ(eye->active, 0);       /* собранный глаз не появляется */
    if (feather) CHECK_EQ(feather->active, 0);

    lever = entities_by_id_const(&es, ID_LEVER);
    if (lever) {
        CHECK_EQ(lever->active, 1);
        CHECK_EQ(lever->state, 1);
        CHECK_EQ(lever->outputs, 1);
    }
    const entity_t *big = entities_by_id_const(&es, ID_BIG);
    if (big) {
        CHECK_EQ(big->state, 1);
        CHECK_EQ(big->outputs, 1);
    }
    CHECK_EQ(entities_eyes_open(&es), 1);
    /* Инициализация не начисляет счётчики (это дело world_set_counts) и не поднимает событий. */
    CHECK_EQ(w.eyes_opened, 0);
    CHECK_EQ(w.small_eyes, 0);
    CHECK_EQ(w.feathers, 0);
    CHECK_EQ(es.last_event, 0);
    /* Дверь под уже включённым рычагом открыта сразу после загрузки. */
    const entity_t *door = entities_by_id_const(&es, ID_DOOR);
    if (door) {
        CHECK_EQ(door->inputs, 1);
        CHECK_EQ(door->state, 1);
    }

    /* Собранный глаз не берётся повторно: взаимодействие в упор возвращает 0. */
    float ex = 0.0f, ez = 0.0f;
    scene_pos(ID_EYE, &ex, &ez);
    CHECK_EQ(entities_interact(&es, ex, ez, 1.0f, 0.0f, E_REACH), 0);
    CHECK_EQ(w.small_eyes, 0);

    level_free(&l);
}

TEST(test_entity_lever_door) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    float lx = 0.0f, lz = 0.0f;
    scene_pos(ID_LEVER, &lx, &lz);
    entity_t *door = entities_by_id(&es, ID_DOOR);
    CHECK(door != NULL);
    if (!door) { level_free(&l); return; }
    CHECK_EQ(door->state, 0);

    /* Дёргаем рычаг: выход 0 включается, дверь открывается после разноса сигналов. */
    CHECK_EQ(entities_interact(&es, lx + 0.4f, lz, 1.0f, 0.0f, E_REACH), ID_LEVER);
    entity_t *lever = entities_by_id(&es, ID_LEVER);
    CHECK(lever != NULL);
    if (lever) {
        CHECK_EQ(lever->state, 1);
        CHECK_EQ(lever->outputs, 1);
    }
    CHECK_EQ(es.last_event, ID_LEVER);
    CHECK_EQ(door->state, 0); /* до propagate дверь ещё закрыта */
    entities_propagate(&es);
    CHECK_EQ(door->inputs, 1); /* битые связи не добавили лишних бит */
    CHECK_EQ(door->state, 1);

    /* Повторное нажатие закрывает. */
    CHECK_EQ(entities_interact(&es, lx + 0.4f, lz, 1.0f, 0.0f, E_REACH), ID_LEVER);
    if (lever) {
        CHECK_EQ(lever->state, 0);
        CHECK_EQ(lever->outputs, 0);
    }
    entities_propagate(&es);
    CHECK_EQ(door->inputs, 0);
    CHECK_EQ(door->state, 0);

    /* Дверь сама по себе не интерактивна: в упор к двери ничего не происходит. */
    float dx = 0.0f, dz = 0.0f;
    scene_pos(ID_DOOR, &dx, &dz);
    CHECK_EQ(entities_interact(&es, dx, dz, 1.0f, 0.0f, 0.3f), 0);

    level_free(&l);
}

TEST(test_entity_interact_out_of_reach) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    float lx = 0.0f, lz = 0.0f;
    scene_pos(ID_LEVER, &lx, &lz);
    const entity_t *lever = entities_by_id_const(&es, ID_LEVER);
    CHECK(lever != NULL);

    /* Далеко от всего. */
    CHECK_EQ(entities_interact(&es, 100.0f, 100.0f, 1.0f, 0.0f, 1.0f), 0);
    /* Чуть дальше радиуса — тоже ничего. */
    CHECK_EQ(entities_interact(&es, lx + 0.61f, lz, 1.0f, 0.0f, E_REACH), 0);
    /* Нулевой и отрицательный радиус. */
    CHECK_EQ(entities_interact(&es, lx, lz, 1.0f, 0.0f, 0.0f), 0);
    CHECK_EQ(entities_interact(&es, lx, lz, 1.0f, 0.0f, -1.0f), 0);
    if (lever) {
        CHECK_EQ(lever->state, 0);
        CHECK_EQ(lever->outputs, 0);
    }
    CHECK_EQ(es.last_event, 0);

    /* На границе радиуса — строго меньше, поэтому внутри. */
    CHECK_EQ(entities_interact(&es, lx + 0.59f, lz, 1.0f, 0.0f, E_REACH), ID_LEVER);

    level_free(&l);
}

TEST(test_entity_collect) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    float ex = 0.0f, ez = 0.0f;
    scene_pos(ID_EYE, &ex, &ez);
    entity_t *eye = entities_by_id(&es, ID_EYE);
    CHECK(eye != NULL);
    CHECK_EQ(w.small_eyes, 0);
    CHECK_EQ(world_flag(&w, ID_EYE), 0);

    CHECK_EQ(entities_interact(&es, ex, ez, 0.0f, 1.0f, E_REACH), ID_EYE);
    if (eye) CHECK_EQ(eye->active, 0);
    CHECK_EQ(world_flag(&w, ID_EYE), 1);
    CHECK_EQ(w.small_eyes, 1);
    CHECK_EQ(es.last_event, ID_EYE);

    /* Повторный сбор невозможен: сущность неактивна, счётчик не растёт. */
    CHECK_EQ(entities_interact(&es, ex, ez, 0.0f, 1.0f, E_REACH), 0);
    CHECK_EQ(w.small_eyes, 1);

    /* Перо начисляется в свой счётчик. */
    float fx = 0.0f, fz = 0.0f;
    scene_pos(ID_FEATHER, &fx, &fz);
    CHECK_EQ(entities_interact(&es, fx, fz, 0.0f, 1.0f, E_REACH), ID_FEATHER);
    CHECK_EQ(w.feathers, 1);
    CHECK_EQ(w.small_eyes, 1);
    CHECK_EQ(world_flag(&w, ID_FEATHER), 1);

    /* Неактивные сущности не наблюдаются. */
    frame_cam_t cam;
    cam_at(&cam, ex, E_Y, ez);
    entities_tick(&es, &cam, 1);
    if (eye) CHECK_EQ(eye->watched, 0);

    level_free(&l);
}

TEST(test_entity_big_eye_opens_once) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    entity_t *recv = entities_by_id(&es, ID_RECV);
    entity_t *big = entities_by_id(&es, ID_BIG);
    CHECK(recv != NULL && big != NULL);
    if (!recv || !big) { level_free(&l); return; }
    CHECK_EQ(big->state, 0);
    CHECK_EQ(entities_eyes_open(&es), 0);

    /* Луч дошёл до приёмника (в игре это делает beam_trace). */
    recv->outputs = 1;
    entities_propagate(&es);
    CHECK_EQ(big->inputs, 1);
    CHECK_EQ(big->state, 1);
    CHECK_EQ(w.eyes_opened, 1);
    CHECK_EQ(world_flag(&w, ID_BIG), 1);
    CHECK_EQ(es.last_event, ID_BIG);
    CHECK_EQ(entities_eyes_open(&es), 1);

    /* Повторные разносы сигналов счётчик не двигают. */
    es.last_event = 0;
    entities_propagate(&es);
    entities_propagate(&es);
    CHECK_EQ(w.eyes_opened, 1);
    CHECK_EQ(es.last_event, 0);

    /* Луч ушёл: открытый глаз остаётся открытым, счётчик не растёт. */
    recv->outputs = 0;
    entities_propagate(&es);
    CHECK_EQ(big->inputs, 0);
    CHECK_EQ(big->state, 1);
    CHECK_EQ(w.eyes_opened, 1);
    recv->outputs = 1;
    entities_propagate(&es);
    CHECK_EQ(w.eyes_opened, 1);
    CHECK_EQ(entities_eyes_open(&es), 1);

    /* Большой глаз не интерактивен «крестом». */
    float bx = 0.0f, bz = 0.0f;
    scene_pos(ID_BIG, &bx, &bz);
    CHECK_EQ(entities_interact(&es, bx, bz, 1.0f, 0.0f, 0.3f), 0);

    level_free(&l);
}

/* Спящий объект: двигается только вне наблюдения (GDD §1.4). */
TEST(test_entity_sleeper_freeze) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    entities_t es;

    float sx = 0.0f, sz = 0.0f, s2x = 0.0f, s2z = 0.0f;
    scene_pos(ID_SLEEP, &sx, &sz);
    scene_pos(ID_SLEEP2, &s2x, &s2z);

    frame_cam_t near_cam, far_cam;
    cam_at(&near_cam, sx, E_Y, sz);
    cam_at(&far_cam, 200.0f, E_Y, 200.0f);

    /* Прогон А: взгляд включён и наведён на объект — он замирает на старте. */
    world_reset(&w);
    entities_init(&es, &l, &w);
    for (int i = 0; i < 60; i++) entities_tick(&es, &near_cam, 1);
    const entity_t *a = entities_by_id_const(&es, ID_SLEEP);
    CHECK(a != NULL);
    if (!a) { level_free(&l); return; }
    CHECK_EQ(a->watched, 1);
    CHECK_NEAR(a->x, sx, 1e-6);
    CHECK_NEAR(a->z, sz, 1e-6);
    CHECK_NEAR(a->phase, 0.0, 1e-6);
    float ax = a->x, az = a->z;

    /* Прогон Б: взгляд выключен — объект идёт по кругу радиуса 2,0 со скоростью 60 град/с. */
    world_reset(&w);
    entities_init(&es, &l, &w);
    for (int i = 0; i < 60; i++) entities_tick(&es, &near_cam, 0);
    const entity_t *b = entities_by_id_const(&es, ID_SLEEP);
    CHECK(b != NULL);
    if (!b) { level_free(&l); return; }
    CHECK_EQ(b->watched, 0);
    CHECK_NEAR(b->phase, 60.0, 1e-3);
    CHECK_NEAR(b->x, sx + 2.0 * cos(60.0 * M3_PI / 180.0), 1e-3);
    CHECK_NEAR(b->z, sz + 2.0 * sin(60.0 * M3_PI / 180.0), 1e-3);
    CHECK(fabs(b->x - ax) > 0.5); /* позиции двух прогонов заметно разошлись */
    CHECK(fabs(b->z - az) > 0.5);
    /* Параметры по умолчанию у второго спящего: радиус 1,0 клетки, 30 град/с. */
    const entity_t *b2 = entities_by_id_const(&es, ID_SLEEP2);
    CHECK(b2 != NULL);
    if (b2) {
        CHECK_NEAR(b2->phase, 30.0, 1e-3);
        CHECK_NEAR(b2->x, s2x + cos(30.0 * M3_PI / 180.0), 1e-3);
        CHECK_NEAR(b2->z, s2z + sin(30.0 * M3_PI / 180.0), 1e-3);
    }

    /* Прогон В: взгляд включён, но камера смотрит в другую сторону — объект движется. */
    world_reset(&w);
    entities_init(&es, &l, &w);
    for (int i = 0; i < 60; i++) entities_tick(&es, &far_cam, 1);
    const entity_t *c = entities_by_id_const(&es, ID_SLEEP);
    CHECK(c != NULL);
    if (c) {
        CHECK_EQ(c->watched, 0);
        CHECK_NEAR(c->x, b->x, 1e-6);
        CHECK_NEAR(c->z, b->z, 1e-6);
    }

    /* Прогон Г: 30 шагов свободы, потом взгляд — замирает ровно там, где был. */
    world_reset(&w);
    entities_init(&es, &l, &w);
    for (int i = 0; i < 30; i++) entities_tick(&es, &near_cam, 0);
    const entity_t *d = entities_by_id_const(&es, ID_SLEEP);
    CHECK(d != NULL);
    if (!d) { level_free(&l); return; }
    float fx = d->x, fz = d->z, fp = d->phase;
    CHECK(fabs(fx - sx) > 0.01 || fabs(fz - sz) > 0.01);
    for (int i = 0; i < 30; i++) entities_tick(&es, &near_cam, 1);
    CHECK_EQ(d->watched, 1);
    CHECK_NEAR(d->x, fx, 1e-6);
    CHECK_NEAR(d->z, fz, 1e-6);
    CHECK_NEAR(d->phase, fp, 1e-6);

    /* Клетка под спящим закрыта для ходьбы и открывается, когда он ушёл: ради этого
     * стражи и добавлены в ent_refresh_blockers — иначе взгляд ничего не решал бы. */
    world_reset(&w);
    entities_init(&es, &l, &w);
    entities_tick(&es, &near_cam, 1);              /* под взглядом страж стоит на месте */
    int cx = 0, cz = 0;
    CHECK(level_cell_at(&l, sx, sz, &cx, &cz));
    CHECK_EQ(level_cell_blocked(&l, cx, cz), 1);
    for (int i = 0; i < 60; i++) entities_tick(&es, &far_cam, 0);   /* ушёл по кругу */
    const entity_t *mv = entities_by_id_const(&es, ID_SLEEP);
    CHECK(mv != NULL);
    if (mv) {
        int mx = 0, mz = 0;
        CHECK(level_cell_at(&l, mv->x, mv->z, &mx, &mz));
        CHECK(mx != cx || mz != cz);
        CHECK_EQ(level_cell_blocked(&l, mx, mz), 1);
        CHECK_EQ(level_cell_blocked(&l, cx, cz), 0);
    }

    /* Прочие сущности парят: фаза растёт и остаётся в [0, 360). */
    const entity_t *lever = entities_by_id_const(&es, ID_LEVER);
    if (lever) {
        CHECK(lever->phase > 0.0f);
        CHECK(lever->phase < 360.0f);
    }
    for (int i = 0; i < 4000; i++) entities_tick(&es, &near_cam, 0);
    for (int i = 0; i < es.count; i++) {
        CHECK(es.items[i].phase >= 0.0f);
        CHECK(es.items[i].phase < 360.0f);
    }

    level_free(&l);
}

TEST(test_entity_busy_blocks_interact) {
    level_t l;
    CHECK_EQ(scene_load(&l), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    float lx = 0.0f, lz = 0.0f;
    scene_pos(ID_LEVER, &lx, &lz);
    frame_cam_t cam;
    cam_at(&cam, 0.0f, E_Y, 0.0f);

    es.busy_frames = 3;
    CHECK_EQ(entities_interact(&es, lx, lz, 1.0f, 0.0f, E_REACH), 0);
    const entity_t *lever = entities_by_id_const(&es, ID_LEVER);
    if (lever) CHECK_EQ(lever->state, 0);
    CHECK_EQ(es.last_event, 0);

    /* Каждый шаг логики уменьшает счётчик, ниже нуля не уходит. */
    entities_tick(&es, &cam, 0);
    CHECK_EQ(es.busy_frames, 2);
    entities_tick(&es, &cam, 0);
    entities_tick(&es, &cam, 0);
    CHECK_EQ(es.busy_frames, 0);
    entities_tick(&es, &cam, 0);
    CHECK_EQ(es.busy_frames, 0);

    CHECK_EQ(entities_interact(&es, lx, lz, 1.0f, 0.0f, E_REACH), ID_LEVER);
    if (lever) CHECK_EQ(lever->state, 1);

    level_free(&l);
}

/* Переполнение пула и пустой уровень: память не портится, состояние вменяемое. */
TEST(test_entity_pool_overflow) {
    static level_entity_t defs[ENT_RUNTIME_MAX + 8];
    memset(defs, 0, sizeof defs);
    int n = (int)(sizeof defs / sizeof defs[0]);
    for (int i = 0; i < n; i++) {
        defs[i].type = ENT_PLINTH;
        defs[i].id = (unsigned short)(i + 1);
        defs[i].x = (float)i;
        defs[i].z = (float)(-i);
        defs[i].name_str_id = 0xFFFF;
    }

    /* level_t собран руками: загрузчик ограничен 128 сущностями, а пул надо проверить. */
    level_t l;
    memset(&l, 0, sizeof l);
    memcpy(l.name, "overflow", 8);
    l.cells_x = 1;
    l.cells_z = 1;
    l.cell_size = 1.0f;
    l.step_y = 0.5f;
    l.entities = defs;
    l.entity_count = n;
    l.seg_states[0] = 3;
    l.seg_states[LEVEL_MAX_SEGMENTS - 1] = 2;

    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);
    CHECK_EQ(es.count, ENT_RUNTIME_MAX);
    CHECK(es.items[ENT_RUNTIME_MAX - 1].def == &defs[ENT_RUNTIME_MAX - 1]);
    CHECK_EQ(es.items[ENT_RUNTIME_MAX - 1].active, 1);
    /* Сущности сверх лимита просто не существуют. */
    CHECK(entities_by_id(&es, ENT_RUNTIME_MAX) != NULL);
    CHECK(entities_by_id(&es, ENT_RUNTIME_MAX + 1) == NULL);
    /* seg_states — состояние игры, сбрасывается при инициализации. */
    for (int i = 0; i < LEVEL_MAX_SEGMENTS; i++) CHECK_EQ(l.seg_states[i], 0);

    frame_cam_t cam;
    cam_at(&cam, 0.0f, 0.0f, 0.0f);
    entities_tick(&es, &cam, 1);
    entities_propagate(&es);
    CHECK_EQ(entities_interact(&es, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f), 0); /* постамент не интерактивен */
    CHECK_EQ(entities_eyes_open(&es), 0);

    /* Уровня нет: всё пусто и ничего не падает. */
    entities_init(&es, NULL, &w);
    CHECK_EQ(es.count, 0);
    CHECK(es.level == NULL);
    entities_tick(&es, NULL, 1);
    entities_propagate(&es);
    CHECK_EQ(entities_interact(&es, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f), 0);
    CHECK_EQ(entities_eyes_open(&es), 0);
    CHECK(entities_by_id(&es, 1) == NULL);
}

/* Типы, которые entity.c не обрабатывает сам: только last_event и возврат id,
 * плюс выбор ближайшей сущности и уровень воды из первого шлюза. */
TEST(test_entity_deferred_types) {
    static level_entity_t defs[4];
    memset(defs, 0, sizeof defs);
    defs[0].type = ENT_WATER_VALVE; defs[0].id = 1; defs[0].x = 0.0f; defs[0].z = 0.0f;
    defs[0].params[0] = 2; defs[0].params[1] = 6;   /* минимум 2 шага, максимум 6 */
    defs[1].type = ENT_ECHO;        defs[1].id = 2; defs[1].x = 4.0f; defs[1].z = 0.0f;
    defs[2].type = ENT_STONE_TEXT;  defs[2].id = 3; defs[2].x = 4.4f; defs[2].z = 0.0f;
    defs[3].type = ENT_WATER_VALVE; defs[3].id = 4; defs[3].x = 9.0f; defs[3].z = 0.0f;
    defs[3].params[0] = 5;                          /* второй шлюз на уровень воды не влияет */

    level_t l;
    memset(&l, 0, sizeof l);
    memcpy(l.name, "valve", 5);
    l.cells_x = l.cells_z = 8;
    l.cell_size = E_CELL;
    l.step_y = E_STEP;
    l.entities = defs;
    l.entity_count = 4;

    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);
    CHECK_EQ(es.count, 4);
    CHECK_EQ(es.water_steps, 2); /* минимум первого шлюза */

    /* Шлюз: entity.c только сообщает событие, воду двигает core/puzzles.c. */
    CHECK_EQ(entities_interact(&es, 0.2f, 0.0f, 1.0f, 0.0f, E_REACH), 1);
    CHECK_EQ(es.last_event, 1);
    CHECK_EQ(es.water_steps, 2);
    CHECK_EQ(entities_by_id(&es, 1)->state, 0);

    /* Из двух сущностей в радиусе выбирается ближайшая. */
    CHECK_EQ(entities_interact(&es, 4.5f, 0.0f, 1.0f, 0.0f, 1.0f), 3); /* камень ближе */
    CHECK_EQ(entities_interact(&es, 3.9f, 0.0f, 1.0f, 0.0f, 1.0f), 2); /* отголосок ближе */
    CHECK_EQ(es.last_event, 2);

    /* Отрицательный минимум не уводит воду ниже нуля. */
    defs[0].params[0] = -3;
    entities_init(&es, &l, &w);
    CHECK_EQ(es.water_steps, 0);
}

TEST(test_entity_mirror_state_from_yaw) {
    /* Автор уровня ставит зеркало углом в TOML; состояние 0..3 обязано совпасть,
     * иначе расстановка головоломки в данных расходится с тем, что видно в игре. */
    level_entity_t ents[4];
    memset(ents, 0, sizeof ents);
    static const float YAW[4] = { 0.0f, 90.0f, 270.0f, 180.0f };
    static const int TYPE[4] = { ENT_MIRROR, ENT_MIRROR, ENT_MIRROR, ENT_PRISM };
    for (int i = 0; i < 4; i++) {
        ents[i].type = (unsigned short)TYPE[i];
        ents[i].id = (unsigned short)(20 + i);
        ents[i].x = cell_c(1 + i, E_W);
        ents[i].z = cell_c(1, E_H);
        ents[i].y = E_Y;
        ents[i].yaw_deg = YAW[i];
        ents[i].name_str_id = 0xFFFF;
    }
    size_t len = 0;
    void *blob = build_entity_level(ents, 4, NULL, 0, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    CHECK_EQ(entities_by_id(&es, 20)->state, 0);
    CHECK_EQ(entities_by_id(&es, 21)->state, 1);
    CHECK_EQ(entities_by_id(&es, 22)->state, 3);
    CHECK_EQ(entities_by_id(&es, 23)->state, 2);
    level_free(&l);
}

TEST(test_entity_door_blocks_segment) {
    /* Дверь запирает проход через состояние своего сегмента: ходьба знает только
     * сетку, поэтому закрытая дверь обязана закрывать клетку, а не только гасить луч. */
    level_entity_t ents[2];
    memset(ents, 0, sizeof ents);
    ents[0].type = ENT_LEVER;
    ents[0].id = 30;
    ents[0].x = cell_c(1, E_W);
    ents[0].z = cell_c(1, E_H);
    ents[0].y = E_Y;
    ents[0].name_str_id = 0xFFFF;
    ents[1].type = ENT_DOOR;
    ents[1].id = 31;
    ents[1].x = cell_c(3, E_W);
    ents[1].z = cell_c(1, E_H);
    ents[1].y = E_Y;
    ents[1].name_str_id = 0xFFFF;
    ents[1].params[0] = 2;   /* номер сегмента двери */
    static const level_link_t LINKS[] = { { 30, 0, 0, 31, 0, 0 } };

    size_t len = 0;
    void *blob = build_entity_level(ents, 2, LINKS, 1, &len);
    level_t l;
    CHECK_EQ(level_load(&l, blob, len), 0);
    world_t w;
    world_reset(&w);
    entities_t es;
    entities_init(&es, &l, &w);

    CHECK_EQ(l.seg_states[2], 0);                 /* закрыта */
    CHECK_EQ(entities_by_id(&es, 31)->state, 0);

    float lx = cell_c(1, E_W), lz = cell_c(1, E_H);
    CHECK_EQ(entities_interact(&es, lx, lz, 1.0f, 0.0f, 0.8f), 30);
    entities_propagate(&es);
    CHECK_EQ(entities_by_id(&es, 31)->state, 1);
    CHECK_EQ(l.seg_states[2], 1);                 /* открылась — клетки сегмента пускают */

    CHECK_EQ(entities_interact(&es, lx, lz, 1.0f, 0.0f, 0.8f), 30);
    entities_propagate(&es);
    CHECK_EQ(l.seg_states[2], 0);                 /* и снова заперта */
    level_free(&l);
}

void tests_entity(void) {
    puts("entity tests");
    RUN(test_entity_init_flags);
    RUN(test_entity_lever_door);
    RUN(test_entity_interact_out_of_reach);
    RUN(test_entity_collect);
    RUN(test_entity_big_eye_opens_once);
    RUN(test_entity_sleeper_freeze);
    RUN(test_entity_busy_blocks_interact);
    RUN(test_entity_deferred_types);
    RUN(test_entity_pool_overflow);
    RUN(test_entity_mirror_state_from_yaw);
    RUN(test_entity_door_blocks_segment);
}
