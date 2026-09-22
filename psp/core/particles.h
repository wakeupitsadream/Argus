/* particles.h — частицы: парящие пылинки (атмосфера) и искры (события).
 * Полностью детерминированы: свой ГПСЧ с явным seed, никакого времени и rand. */
#ifndef ARGUS_PARTICLES_H
#define ARGUS_PARTICLES_H
#include "frame.h"

#define PARTICLES_MAX 96

enum { PT_FREE = 0, PT_DUST = 1, PT_SPARK = 2 };

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float life, life_max; /* в кадрах */
    float phase;          /* для бокового дрейфа пылинок */
    unsigned color;       /* 0xAABBGGRR; альфа пересчитывается при затухании */
    unsigned char kind;
} particle_t;

typedef struct {
    particle_t items[PARTICLES_MAX];
    int count;
    unsigned rng;
    float ambient_center[3];
    float ambient_radius;
    int ambient_count;
    unsigned ambient_color;
} particles_t;

void particles_init(particles_t *p, unsigned seed);
void particles_set_ambient(particles_t *p, const float center[3], float radius, int count, unsigned color);
void particles_emit_burst(particles_t *p, const float pos[3], int count, unsigned color, float speed);
void particles_tick(particles_t *p);
void particles_build(const particles_t *p, frame_t *f);

#endif
