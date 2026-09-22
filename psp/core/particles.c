#include "particles.h"
#include "ease.h"
#include <string.h>

#define DUST_LIFE 420.0f    /* кадров: 7 секунд */
#define SPARK_LIFE_MIN 36.0f
#define SPARK_LIFE_MAX 72.0f
#define DUST_RISE 0.0035f
#define DUST_DRIFT 0.0022f
#define SPARK_DRAG 0.94f
#define SPARK_GRAVITY 0.0016f
#define DUST_SIZE 0.11f
#define SPARK_SIZE 0.22f

/* xorshift32: детерминированный и достаточный для частиц. */
static unsigned rnd(particles_t *p) {
    unsigned x = p->rng ? p->rng : 0x1234567u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->rng = x;
    return x;
}

/* Случайное в [-1, 1]. */
static float rnd_sym(particles_t *p) {
    return (float)(rnd(p) >> 8) / 8388608.0f - 1.0f;
}

/* Случайное в [0, 1]. */
static float rnd_unit(particles_t *p) {
    return (float)(rnd(p) >> 8) / 16777216.0f;
}

static particle_t *alloc_particle(particles_t *p) {
    for (int i = 0; i < PARTICLES_MAX; i++) {
        if (p->items[i].kind == PT_FREE) {
            if (i + 1 > p->count) p->count = i + 1;
            return &p->items[i];
        }
    }
    return NULL; /* пул полон — новые частицы просто не появляются */
}

void particles_init(particles_t *p, unsigned seed) {
    memset(p, 0, sizeof *p);
    p->rng = seed ? seed : 0xA5A5A5A5u;
}

static void spawn_dust(particles_t *p, int seeded) {
    particle_t *it = alloc_particle(p);
    if (!it) return;
    float r = p->ambient_radius * (0.25f + 0.75f * rnd_unit(p));
    float a = rnd_unit(p) * 6.28318f;
    it->kind = PT_DUST;
    it->x = p->ambient_center[0] + r * (a < 3.14159f ? 1.0f : -1.0f) * rnd_sym(p);
    it->z = p->ambient_center[2] + r * rnd_sym(p);
    it->y = p->ambient_center[1] + p->ambient_radius * 0.5f * rnd_sym(p);
    it->vx = 0.0f; it->vy = DUST_RISE * (0.5f + rnd_unit(p)); it->vz = 0.0f;
    it->phase = rnd_unit(p) * 6.28318f;
    it->life_max = DUST_LIFE * (0.6f + 0.8f * rnd_unit(p));
    it->life = seeded ? it->life_max * rnd_unit(p) : it->life_max;
    it->color = p->ambient_color;
}

void particles_set_ambient(particles_t *p, const float center[3], float radius, int count, unsigned color) {
    if (!center) return;
    memcpy(p->ambient_center, center, sizeof p->ambient_center);
    p->ambient_radius = radius > 0.1f ? radius : 0.1f;
    p->ambient_count = count < 0 ? 0 : (count > PARTICLES_MAX / 2 ? PARTICLES_MAX / 2 : count);
    p->ambient_color = color;
    /* Первый досев — с разбросом по времени жизни, иначе все мигнут разом. */
    int live = 0;
    for (int i = 0; i < PARTICLES_MAX; i++) if (p->items[i].kind == PT_DUST) live++;
    for (int i = live; i < p->ambient_count; i++) spawn_dust(p, 1);
}

void particles_emit_burst(particles_t *p, const float pos[3], int count, unsigned color, float speed) {
    if (!pos) return;
    for (int i = 0; i < count; i++) {
        particle_t *it = alloc_particle(p);
        if (!it) return;
        it->kind = PT_SPARK;
        it->x = pos[0]; it->y = pos[1]; it->z = pos[2];
        float hx = rnd_sym(p), hz = rnd_sym(p);
        it->vx = hx * speed;
        it->vz = hz * speed;
        it->vy = speed * (0.6f + 0.7f * rnd_unit(p));
        it->phase = 0.0f;
        it->life_max = SPARK_LIFE_MIN + (SPARK_LIFE_MAX - SPARK_LIFE_MIN) * rnd_unit(p);
        it->life = it->life_max;
        it->color = color;
    }
}

void particles_tick(particles_t *p) {
    int dust_live = 0;
    for (int i = 0; i < PARTICLES_MAX; i++) {
        particle_t *it = &p->items[i];
        if (it->kind == PT_FREE) continue;
        it->life -= 1.0f;
        if (it->life <= 0.0f) {
            it->kind = PT_FREE;
            continue;
        }
        if (it->kind == PT_DUST) {
            it->phase += 0.021f;
            float drift = DUST_DRIFT;
            /* лёгкий боковой дрейф: синус приближаем через фазу, чтобы не звать sinf на каждую частицу */
            float s = it->phase;
            while (s > 6.28318f) s -= 6.28318f;
            float approx = (s < 3.14159f) ? (s * (3.14159f - s) * 0.4053f) : (-(s - 3.14159f) * (6.28318f - s) * 0.4053f);
            it->x += approx * drift;
            it->z += approx * drift * 0.7f;
            it->y += it->vy;
            float dy = it->y - p->ambient_center[1];
            if (dy > p->ambient_radius * 0.6f) it->y = p->ambient_center[1] - p->ambient_radius * 0.6f;
            dust_live++;
        } else {
            it->vx *= SPARK_DRAG;
            it->vz *= SPARK_DRAG;
            it->vy = it->vy * SPARK_DRAG - SPARK_GRAVITY;
            it->x += it->vx;
            it->y += it->vy;
            it->z += it->vz;
        }
    }
    /* Досев пылинок до заданного количества. */
    for (int i = dust_live; i < p->ambient_count; i++) spawn_dust(p, 0);
}

void particles_build(const particles_t *p, frame_t *f) {
    for (int i = 0; i < PARTICLES_MAX; i++) {
        const particle_t *it = &p->items[i];
        if (it->kind == PT_FREE) continue;
        float t = it->life_max > 0.0f ? it->life / it->life_max : 0.0f;
        /* Яркость: плавный вход и выход — пылинки не мигают, искры гаснут. */
        float k = (it->kind == PT_DUST)
                      ? (t > 0.75f ? ease_out_cubic((1.0f - t) * 4.0f) : ease_out_cubic(t / 0.75f))
                      : ease_out_cubic(t);
        unsigned base_a = (it->color >> 24) & 0xFFu;
        unsigned a = (unsigned)((float)base_a * k);
        unsigned color = (a << 24) | (it->color & 0x00FFFFFFu);
        float pos[3] = { it->x, it->y, it->z };
        frame_push_sprite(f, it->kind == PT_DUST ? SPRITE_DUST : SPRITE_SPARK, pos,
                          it->kind == PT_DUST ? DUST_SIZE : SPARK_SIZE, color);
    }
}
