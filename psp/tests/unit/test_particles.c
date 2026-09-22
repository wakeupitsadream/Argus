/* test_particles.c — частицы: детерминизм, инварианты пула, затухание, сборка кадра. */
#include "minitest.h"
#include "tests.h"
#include "particles.h"

/* Крупные структуры держим в BSS, чтобы не раздувать стек. */
static particles_t g_a, g_b;
static frame_t g_frame;

static int count_kind(const particles_t *p, int kind) {
    int n = 0;
    for (int i = 0; i < p->count; i++) {
        if (p->items[i].kind == kind) n++;
    }
    return n;
}

/* Максимальное удаление пылинки от центра объёма. */
static float max_dust_dist(const particles_t *p) {
    float best = 0.0f;
    for (int i = 0; i < p->count; i++) {
        if (p->items[i].kind != PARTICLE_DUST) continue;
        float dx = p->items[i].pos[0] - p->ambient_center[0];
        float dy = p->items[i].pos[1] - p->ambient_center[1];
        float dz = p->items[i].pos[2] - p->ambient_center[2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if (d > best) best = d;
    }
    return best;
}

/* Один и тот же сценарий вызовов — единственный источник различий это seed. */
static void run_scene(particles_t *p, unsigned seed, int frames) {
    const float center[3] = { 1.5f, 2.0f, -0.5f };
    const float burst[3] = { 0.0f, 1.0f, 0.0f };
    particles_init(p, seed);
    particles_set_ambient(p, center, 5.0f, 24, 0x70E8D8B0u);
    for (int i = 0; i < frames; i++) {
        if (i == 40 || i == 120) particles_emit_burst(p, burst, 12, 0xFFFFC080u, 0.09f);
        particles_tick(p);
    }
}

static int same_state(const particles_t *a, const particles_t *b) {
    if (a->count != b->count || a->rng != b->rng) return 0;
    for (int i = 0; i < a->count; i++) {
        const particle_t *x = &a->items[i], *y = &b->items[i];
        if (x->kind != y->kind || x->life != y->life || x->life_max != y->life_max) return 0;
        if (x->color != y->color || x->alpha_max != y->alpha_max) return 0;
        if (x->phase != y->phase) return 0;
        for (int k = 0; k < 3; k++) {
            if (x->pos[k] != y->pos[k] || x->vel[k] != y->vel[k]) return 0;
        }
    }
    return 1;
}

TEST(particles_determinism) {
    run_scene(&g_a, 0x1234ABCDu, 200);
    run_scene(&g_b, 0x1234ABCDu, 200);
    CHECK(g_a.count > 0);
    CHECK(same_state(&g_a, &g_b));

    /* Другой seed — другая картина. */
    run_scene(&g_b, 0x0BADF00Du, 200);
    CHECK(!same_state(&g_a, &g_b));
}

TEST(particles_ambient_invariants) {
    const float center[3] = { 2.0f, 1.0f, -3.0f };
    const float radius = 4.0f;
    particles_init(&g_a, 0x51F0A17Du);
    particles_set_ambient(&g_a, center, radius, 30, 0x60FFF3C9u);
    CHECK_EQ(count_kind(&g_a, PARTICLE_DUST), 30);
    CHECK(max_dust_dist(&g_a) <= radius);

    int count_ok = 1, radius_ok = 1;
    for (int i = 0; i < 400; i++) {
        particles_tick(&g_a);
        if (count_kind(&g_a, PARTICLE_DUST) != 30) count_ok = 0;
        /* требование: не выходить за радиус больше чем на 10% */
        if (max_dust_dist(&g_a) > radius * 1.10f) radius_ok = 0;
    }
    CHECK(count_ok);
    CHECK(radius_ok);
    /* по факту логика возрождает пылинку сразу, так что запаса 10% и не нужно */
    CHECK(max_dust_dist(&g_a) <= radius * 1.001f);

    /* Уменьшили количество — лишние снимаются. */
    particles_set_ambient(&g_a, center, radius, 6, 0x60FFF3C9u);
    particles_tick(&g_a);
    CHECK_EQ(count_kind(&g_a, PARTICLE_DUST), 6);

    /* Запрос сверх предела зажимается, место под искры остаётся. */
    particles_set_ambient(&g_a, center, radius, 1000, 0x60FFF3C9u);
    CHECK_EQ(g_a.ambient_count, PARTICLES_DUST_MAX);
    CHECK_EQ(count_kind(&g_a, PARTICLE_DUST), PARTICLES_DUST_MAX);
    CHECK(PARTICLES_DUST_MAX < PARTICLES_MAX);
}

TEST(particles_burst_exact) {
    const float pos[3] = { 1.0f, 2.0f, 3.0f };
    particles_init(&g_a, 0x77777777u);

    particles_emit_burst(&g_a, pos, 10, 0xFF80C0FFu, 0.1f);
    CHECK_EQ(g_a.count, 10);
    CHECK_EQ(count_kind(&g_a, PARTICLE_SPARK), 10);
    int at_origin = 1;
    for (int i = 0; i < g_a.count; i++) {
        if (g_a.items[i].pos[0] != pos[0] || g_a.items[i].pos[1] != pos[1] ||
            g_a.items[i].pos[2] != pos[2]) at_origin = 0;
        /* альфа при рождении — из цвета эмиссии */
        CHECK_EQ((g_a.items[i].color >> 24) & 0xFFu, 0xFFu);
        CHECK_EQ(g_a.items[i].color & 0x00FFFFFFu, 0x0080C0FFu);
    }
    CHECK(at_origin);

    /* Просят больше, чем есть свободных слотов — добавится ровно min(count, свободные). */
    int before = g_a.count;
    particles_emit_burst(&g_a, pos, 200, 0xFF80C0FFu, 0.1f);
    CHECK_EQ(g_a.count, PARTICLES_MAX);
    CHECK_EQ(g_a.count - before, PARTICLES_MAX - before);

    /* Пул полон — ничего не добавляется и ничего не портится. */
    particles_emit_burst(&g_a, pos, 5, 0xFF80C0FFu, 0.1f);
    CHECK_EQ(g_a.count, PARTICLES_MAX);

    /* Отрицательное и нулевое количество — не падаем. */
    particles_init(&g_a, 0x77777777u);
    particles_emit_burst(&g_a, pos, 0, 0xFF80C0FFu, 0.1f);
    particles_emit_burst(&g_a, pos, -7, 0xFF80C0FFu, 0.1f);
    CHECK_EQ(g_a.count, 0);
}

TEST(particles_spark_lifetime) {
    const float pos[3] = { 0.0f, 0.5f, 0.0f };
    particles_init(&g_a, 0x2B2B2B2Bu);
    particles_emit_burst(&g_a, pos, 20, 0xFFFFE0A0u, 0.12f);

    int life_ok = 1;
    for (int i = 0; i < g_a.count; i++) {
        /* 0.6..1.2 с при шаге 1/60 */
        if (g_a.items[i].life_max < 36 || g_a.items[i].life_max > 72) life_ok = 0;
    }
    CHECK(life_ok);

    for (int i = 0; i < 35; i++) particles_tick(&g_a);
    CHECK_EQ(g_a.count, 20); /* раньше 0.6 с не гаснут */
    for (int i = 0; i < 37; i++) particles_tick(&g_a);
    CHECK_EQ(g_a.count, 0); /* к 1.2 с мертвы все */
}

TEST(particles_spark_alpha_fade) {
    const float pos[3] = { 0.0f, 1.0f, 0.0f };
    particles_init(&g_a, 0x13571357u);
    particles_emit_burst(&g_a, pos, 1, 0xFF3366CCu, 0.1f);

    unsigned prev = 0x100u, last = 0x100u;
    int monotonic = 1, rgb_kept = 1, steps = 0;
    while (g_a.count > 0 && steps < 200) {
        unsigned a = (g_a.items[0].color >> 24) & 0xFFu;
        if (a > prev) monotonic = 0;
        if ((g_a.items[0].color & 0x00FFFFFFu) != 0x003366CCu) rgb_kept = 0;
        prev = a;
        last = a;
        particles_tick(&g_a);
        steps++;
    }
    CHECK(monotonic);
    CHECK(rgb_kept);
    CHECK(steps >= 36 && steps <= 72);
    CHECK(last < 48u); /* к концу жизни искра почти невидима */
}

TEST(particles_dust_alpha_smooth) {
    const float center[3] = { 0.0f, 1.0f, 0.0f };
    particles_init(&g_a, 0x9E3779B9u);
    particles_set_ambient(&g_a, center, 3.0f, 8, 0x60FFF3C9u);

    unsigned alpha[PARTICLES_MAX];
    int life[PARTICLES_MAX];
    for (int i = 0; i < g_a.count; i++) {
        alpha[i] = (g_a.items[i].color >> 24) & 0xFFu;
        life[i] = g_a.items[i].life;
    }

    int smooth = 1, capped = 1, rgb_kept = 1;
    for (int f = 0; f < 600; f++) {
        particles_tick(&g_a);
        for (int i = 0; i < g_a.count; i++) {
            unsigned a = (g_a.items[i].color >> 24) & 0xFFu;
            if (a > 0x60u) capped = 0;
            if ((g_a.items[i].color & 0x00FFFFFFu) != 0x00FFF3C9u) rgb_kept = 0;
            /* жизнь выросла — пылинка возродилась, разрыв яркости здесь законен */
            if (g_a.items[i].life <= life[i]) {
                int d = (int)a - (int)alpha[i];
                if (d < 0) d = -d;
                if (d > 5) smooth = 0;
            }
            alpha[i] = a;
            life[i] = g_a.items[i].life;
        }
    }
    CHECK(smooth);
    CHECK(capped);
    CHECK(rgb_kept);
}

TEST(particles_pool_overflow_safe) {
    const float center[3] = { 0.0f, 1.0f, 0.0f };
    const float pos[3] = { 0.5f, 0.5f, 0.5f };
    particles_init(&g_a, 0xFEEDBEEFu);
    particles_set_ambient(&g_a, center, 2.5f, 40, 0x50FFF3C9u);

    int bounded = 1;
    for (int f = 0; f < 60; f++) {
        particles_emit_burst(&g_a, pos, 30, 0xFFFFA040u, 0.15f);
        particles_tick(&g_a);
        if (g_a.count < 0 || g_a.count > PARTICLES_MAX) bounded = 0;
    }
    CHECK(bounded);
    /* Сразу после вспышки пул забит до предела. */
    particles_emit_burst(&g_a, pos, 30, 0xFFFFA040u, 0.15f);
    CHECK_EQ(g_a.count, PARTICLES_MAX);

    /* Перестали сыпать искры — состав возвращается к одним пылинкам. */
    for (int f = 0; f < 200; f++) particles_tick(&g_a);
    CHECK_EQ(count_kind(&g_a, PARTICLE_SPARK), 0);
    CHECK_EQ(count_kind(&g_a, PARTICLE_DUST), 40);
    CHECK_EQ(g_a.count, 40);
}

TEST(particles_build_frame) {
    const float center[3] = { 0.0f, 1.0f, 0.0f };
    const float pos[3] = { 0.0f, 0.5f, 0.0f };
    particles_init(&g_a, 0x0C0FFEE1u);
    particles_set_ambient(&g_a, center, 3.0f, 12, 0x60FFF3C9u);
    particles_emit_burst(&g_a, pos, 5, 0xFFFFA040u, 0.1f);
    particles_tick(&g_a);

    frame_reset(&g_frame);
    particles_build(&g_a, &g_frame);
    CHECK_EQ(g_frame.sprite_count, g_a.count);
    CHECK_EQ(g_frame.sprite_count, 17);

    int dust = 0, spark = 0, kinds_ok = 1, sizes_ok = 1;
    for (int i = 0; i < g_frame.sprite_count; i++) {
        const frame_sprite_t *s = &g_frame.sprites[i];
        if (s->kind == SPRITE_DUST) dust++;
        else if (s->kind == SPRITE_SPARK) spark++;
        else kinds_ok = 0;
        if (g_a.items[i].kind == PARTICLE_DUST && s->kind != SPRITE_DUST) kinds_ok = 0;
        if (g_a.items[i].kind == PARTICLE_SPARK && s->kind != SPRITE_SPARK) kinds_ok = 0;
        if (!(s->size > 0.0f) || s->size > 1.0f) sizes_ok = 0;
        if (s->color != g_a.items[i].color) kinds_ok = 0;
    }
    CHECK(kinds_ok);
    CHECK(sizes_ok);
    CHECK_EQ(dust, 12);
    CHECK_EQ(spark, 5);
    /* SPRITE_GLOW частицы не используют */
    CHECK_EQ(dust + spark, g_frame.sprite_count);

    /* Кадр почти полон — build не выходит за FRAME_MAX_SPRITES. */
    frame_reset(&g_frame);
    for (int i = 0; i < FRAME_MAX_SPRITES - 5; i++) {
        frame_push_sprite(&g_frame, SPRITE_GLOW, pos, 0.5f, 0xFFFFFFFFu);
    }
    particles_build(&g_a, &g_frame);
    CHECK_EQ(g_frame.sprite_count, FRAME_MAX_SPRITES);

    /* Полный пул частиц тоже влезает в кадр без обрезки. */
    particles_init(&g_a, 0x0C0FFEE1u);
    particles_emit_burst(&g_a, pos, PARTICLES_MAX, 0xFFFFA040u, 0.1f);
    frame_reset(&g_frame);
    particles_build(&g_a, &g_frame);
    CHECK_EQ(g_frame.sprite_count, PARTICLES_MAX);
    CHECK(g_frame.sprite_count <= FRAME_MAX_SPRITES);
}

void tests_particles(void) {
    puts("particles tests");
    RUN(particles_determinism);
    RUN(particles_ambient_invariants);
    RUN(particles_burst_exact);
    RUN(particles_spark_lifetime);
    RUN(particles_spark_alpha_fade);
    RUN(particles_dust_alpha_smooth);
    RUN(particles_pool_overflow_safe);
    RUN(particles_build_frame);
}
