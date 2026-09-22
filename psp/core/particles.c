/* particles.c — логика частиц. Без malloc, без глобального состояния,
 * без обращений к времени: всё поведение выводится из particles_t. */
#include "particles.h"
#include "ease.h"
#include <math.h>
#include <string.h>

#define P_PI 3.14159265f
#define P_TWO_PI 6.28318531f

/* Пылинки: очень медленный подъём и лёгкий боковой дрейф. */
#define DUST_LIFE_MIN 300      /* 5 с */
#define DUST_LIFE_SPAN 241     /* до 9 с */
#define DUST_RISE 0.0060f      /* ≈0.36 единицы в секунду */
#define DUST_DRIFT 0.0034f
#define DUST_PHASE_STEP 0.019f
#define DUST_FADE_EDGE 0.28f   /* доля жизни на вход и на выход яркости */
#define DUST_SIZE 0.085f

/* Искры: 36..72 кадра — 0.6..1.2 с. */
#define SPARK_LIFE_MIN 36
#define SPARK_LIFE_SPAN 37
#define SPARK_DRAG 0.93f
#define SPARK_GRAVITY 0.0018f
#define SPARK_SIZE 0.26f

/* ---- ГПСЧ ---- */

/* xorshift32: детерминированный, состояние целиком в particles_t. */
static unsigned p_rand(particles_t *p) {
    unsigned x = p->rng;
    if (x == 0u) x = 0x9E3779B9u; /* защита от нулевого состояния */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->rng = x;
    return x;
}

/* Случайное в [0, 1). */
static float p_unit(particles_t *p) {
    return (float)(p_rand(p) >> 8) * (1.0f / 16777216.0f);
}

/* Случайное в [-1, 1). */
static float p_sym(particles_t *p) {
    return p_unit(p) * 2.0f - 1.0f;
}

/* ---- вспомогательное ---- */

/* Синус без libm: приближение Бхаскары, погрешность < 0.002.
 * Свой, чтобы результат был побитово одинаков на хосте и на PSP. */
static float sin_wave(float x) {
    int neg = 0;
    while (x >= P_TWO_PI) x -= P_TWO_PI;
    while (x < 0.0f) x += P_TWO_PI;
    if (x >= P_PI) { x -= P_PI; neg = 1; }
    float d = x * (P_PI - x);
    float s = 16.0f * d / (5.0f * P_PI * P_PI - 4.0f * d);
    return neg ? -s : s;
}

/* Меняет только альфу, RGB остаётся как у цвета эмиссии. */
static void set_alpha(particle_t *it, float k) {
    unsigned a = (unsigned)((float)it->alpha_max * clamp01(k) + 0.5f);
    if (a > 255u) a = 255u;
    it->color = (a << 24) | (it->color & 0x00FFFFFFu);
}

/* Огибающая яркости пылинки: плавный вход и выход, без резкого мигания. */
static float dust_envelope(float age) {
    if (age < DUST_FADE_EDGE) return ease_in_out_cubic(age / DUST_FADE_EDGE);
    if (age > 1.0f - DUST_FADE_EDGE) return ease_in_out_cubic((1.0f - age) / DUST_FADE_EDGE);
    return 1.0f;
}

static float life_age(const particle_t *it) {
    if (it->life_max <= 0) return 1.0f;
    return 1.0f - (float)it->life / (float)it->life_max;
}

/* ---- пылинки ---- */

/* Рождение внутри шара. Радиус берём не больше 0.95·R, направление — случайное:
 * распределение чуть смещено к центру, зато пылинка гарантированно в объёме. */
static void spawn_dust(particles_t *p, particle_t *it, int staggered) {
    float dir[3] = { p_sym(p), p_sym(p), p_sym(p) };
    float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (len < 1e-4f) {
        dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f;
    } else {
        dir[0] /= len; dir[1] /= len; dir[2] /= len;
    }
    float r = p->ambient_radius * (0.15f + 0.80f * p_unit(p));

    memset(it, 0, sizeof *it);
    it->kind = PARTICLE_DUST;
    it->pos[0] = p->ambient_center[0] + dir[0] * r;
    it->pos[1] = p->ambient_center[1] + dir[1] * r;
    it->pos[2] = p->ambient_center[2] + dir[2] * r;
    it->vel[1] = DUST_RISE * (0.55f + 0.90f * p_unit(p));
    it->phase = p_unit(p) * P_TWO_PI;
    it->life_max = DUST_LIFE_MIN + (int)((float)DUST_LIFE_SPAN * p_unit(p));
    /* Первый досев — с разбросом по возрасту, иначе весь объём мигнёт разом. */
    it->life = staggered ? 1 + (int)((float)(it->life_max - 1) * p_unit(p)) : it->life_max;
    it->color = p->ambient_color;
    it->alpha_max = (unsigned char)((p->ambient_color >> 24) & 0xFFu);
    set_alpha(it, dust_envelope(life_age(it)));
}

static int dust_outside(const particles_t *p, const particle_t *it) {
    float dx = it->pos[0] - p->ambient_center[0];
    float dy = it->pos[1] - p->ambient_center[1];
    float dz = it->pos[2] - p->ambient_center[2];
    return dx * dx + dy * dy + dz * dz > p->ambient_radius * p->ambient_radius;
}

static void dust_step(particles_t *p, particle_t *it) {
    it->phase += DUST_PHASE_STEP;
    if (it->phase >= P_TWO_PI) it->phase -= P_TWO_PI;
    /* боковой дрейф — синус от собственной фазы, по z с другим периодом */
    it->pos[0] += sin_wave(it->phase) * DUST_DRIFT;
    it->pos[2] += sin_wave(it->phase * 0.73f + 1.3f) * DUST_DRIFT * 0.8f;
    it->pos[1] += it->vel[1];
    it->life--;
    /* Вышла из объёма или дожила свой срок — рождается заново внутри шара,
     * поэтому снаружи позиции пылинок никогда не выходят за ambient_radius. */
    if (it->life <= 0 || dust_outside(p, it)) {
        spawn_dust(p, it, 0);
        return;
    }
    set_alpha(it, dust_envelope(life_age(it)));
}

/* Держит число пылинок равным ambient_count: лишние снимает, недостающие досевает. */
static void refill_dust(particles_t *p, int staggered) {
    int live = 0;
    for (int i = 0; i < p->count; i++) {
        if (p->items[i].kind == PARTICLE_DUST) live++;
    }
    for (int i = p->count - 1; i >= 0 && live > p->ambient_count; i--) {
        if (p->items[i].kind != PARTICLE_DUST) continue;
        p->items[i] = p->items[--p->count];
        live--;
    }
    while (live < p->ambient_count && p->count < PARTICLES_MAX) {
        spawn_dust(p, &p->items[p->count++], staggered);
        live++;
    }
}

/* ---- искры ---- */

/* 0 — искра умерла и слот надо освободить. */
static int spark_step(particle_t *it) {
    it->vel[0] *= SPARK_DRAG;
    it->vel[1] = it->vel[1] * SPARK_DRAG - SPARK_GRAVITY;
    it->vel[2] *= SPARK_DRAG;
    it->pos[0] += it->vel[0];
    it->pos[1] += it->vel[1];
    it->pos[2] += it->vel[2];
    it->life--;
    if (it->life <= 0) return 0;
    set_alpha(it, ease_out_cubic((float)it->life / (float)it->life_max));
    return 1;
}

/* ---- публичное API ---- */

void particles_init(particles_t *p, unsigned seed) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->rng = seed ? seed : 0xA5A5A5A5u;
    p->ambient_radius = 1.0f;
}

void particles_set_ambient(particles_t *p, const float center[3], float radius, int count, unsigned color) {
    if (!p || !center) return;
    p->ambient_center[0] = center[0];
    p->ambient_center[1] = center[1];
    p->ambient_center[2] = center[2];
    p->ambient_radius = radius > 0.05f ? radius : 0.05f;
    p->ambient_count = count < 0 ? 0 : (count > PARTICLES_DUST_MAX ? PARTICLES_DUST_MAX : count);
    p->ambient_color = color;
    refill_dust(p, 1);
}

void particles_emit_burst(particles_t *p, const float pos[3], int count, unsigned color, float speed) {
    if (!p || !pos || count <= 0) return;
    for (int i = 0; i < count && p->count < PARTICLES_MAX; i++) {
        particle_t *it = &p->items[p->count++];
        /* направление: разброс по горизонтали, всегда с составляющей вверх */
        float dir[3] = { p_sym(p), 0.55f + 0.85f * p_unit(p), p_sym(p) };
        float len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (len < 1e-4f) {
            dir[0] = 0.0f; dir[1] = 1.0f; dir[2] = 0.0f;
        } else {
            dir[0] /= len; dir[1] /= len; dir[2] /= len;
        }
        float v = speed * (0.75f + 0.50f * p_unit(p));

        memset(it, 0, sizeof *it);
        it->kind = PARTICLE_SPARK;
        it->pos[0] = pos[0]; it->pos[1] = pos[1]; it->pos[2] = pos[2];
        it->vel[0] = dir[0] * v; it->vel[1] = dir[1] * v; it->vel[2] = dir[2] * v;
        it->life_max = SPARK_LIFE_MIN + (int)((float)SPARK_LIFE_SPAN * p_unit(p));
        it->life = it->life_max;
        it->color = color;
        it->alpha_max = (unsigned char)((color >> 24) & 0xFFu);
        set_alpha(it, 1.0f);
    }
}

void particles_tick(particles_t *p) {
    if (!p) return;
    int i = 0;
    while (i < p->count) {
        particle_t *it = &p->items[i];
        if (it->kind == PARTICLE_DUST) {
            dust_step(p, it);
            i++;
        } else if (spark_step(it)) {
            i++;
        } else {
            /* пул плотный: на место мёртвой кладём последнюю живую */
            p->items[i] = p->items[--p->count];
        }
    }
    refill_dust(p, 0);
}

void particles_build(const particles_t *p, frame_t *f) {
    if (!p || !f) return;
    for (int i = 0; i < p->count; i++) {
        const particle_t *it = &p->items[i];
        int dust = (it->kind == PARTICLE_DUST);
        frame_push_sprite(f, dust ? SPRITE_DUST : SPRITE_SPARK, it->pos,
                          dust ? DUST_SIZE : SPARK_SIZE, it->color);
    }
}
