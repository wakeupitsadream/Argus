/* particles.h — частицы: парящие пылинки (атмосфера) и искры (события).
 * Полностью детерминированы: свой ГПСЧ с явным seed в состоянии particles_t,
 * никакого rand(), time() и глобальных переменных — иначе поедут золотые кадры.
 * Все скорости и времена — в мировых единицах за кадр и в кадрах (шаг 1/60 с). */
#ifndef ARGUS_PARTICLES_H
#define ARGUS_PARTICLES_H
#include "frame.h"

#define PARTICLES_MAX 96
/* Верхний предел пылинок: остаток пула зарезервирован под искры,
 * чтобы атмосфера никогда не вытесняла реакцию на событие. */
#define PARTICLES_DUST_MAX 64

enum { PARTICLE_FREE = 0, PARTICLE_DUST = 1, PARTICLE_SPARK = 2 };

typedef struct {
    float pos[3];
    float vel[3];
    float phase;          /* фаза бокового дрейфа пылинки, 0..2π */
    int life;             /* сколько кадров осталось жить */
    int life_max;         /* полная длительность жизни, кадров */
    unsigned color;       /* 0xAABBGGRR; particles_tick меняет только альфу */
    unsigned char alpha_max; /* альфа цвета эмиссии — потолок яркости */
    unsigned char kind;      /* PARTICLE_DUST | PARTICLE_SPARK */
} particle_t;

/* Пул плотный: живые частицы всегда занимают items[0 .. count-1]. */
typedef struct {
    particle_t items[PARTICLES_MAX];
    int count;
    unsigned rng;        /* состояние детерминированного ГПСЧ (xorshift32) */
    float ambient_center[3];
    float ambient_radius;
    int ambient_count;
    unsigned ambient_color;
} particles_t;

void particles_init(particles_t *p, unsigned seed);
/* Пылинки, медленно парящие в шаре радиуса radius вокруг center: поддерживаются
 * в постоянном количестве. count зажимается в 0..PARTICLES_DUST_MAX. */
void particles_set_ambient(particles_t *p, const float center[3], float radius, int count, unsigned color);
/* Вспышка: count искр из точки вверх-в-стороны со скоростью speed (единиц за кадр).
 * Если в пуле меньше свободных слотов, лишние искры молча не появляются. */
void particles_emit_burst(particles_t *p, const float pos[3], int count, unsigned color, float speed);
/* Один шаг логики (1/60 с): движение, затухание, снятие мёртвых, досев пылинок. */
void particles_tick(particles_t *p);
/* Кладёт живые частицы в кадр через frame_push_sprite (SPRITE_DUST / SPRITE_SPARK).
 * При переполнении пула спрайтов кадра лишние молча отбрасываются. */
void particles_build(const particles_t *p, frame_t *f);

#endif
