/* world.c — постоянное состояние мира: битсет 256 флагов и счётчики прогресса.
 * Без выделений памяти и без побочных эффектов: всё лежит в world_t у вызывающего. */
#include "world.h"
#include <string.h>

/* Счётчики хранятся в unsigned short — шире не запишешь. */
#define WORLD_COUNT_MAX 65535
/* Больших глаз в игре четыре (GDD §1.3), see world.h. */
#define WORLD_EYES_MAX 4

static unsigned short clamp_count(int v, int max) {
    if (v < 0) return 0;
    if (v > max) return (unsigned short)max;
    return (unsigned short)v;
}

void world_reset(world_t *w) {
    if (!w) return;
    memset(w, 0, sizeof *w);
}

int world_flag(const world_t *w, int id) {
    if (!w || id < 0 || id >= WORLD_FLAG_COUNT) return 0;
    return (w->bits[id >> 3] >> (id & 7)) & 1;
}

void world_set_flag(world_t *w, int id, int value) {
    /* id вне диапазона — тихо игнорируем: данные уровня не должны ронять игру. */
    if (!w || id < 0 || id >= WORLD_FLAG_COUNT) return;
    unsigned char mask = (unsigned char)(1u << (id & 7));
    if (value) w->bits[id >> 3] |= mask;
    else w->bits[id >> 3] = (unsigned char)(w->bits[id >> 3] & (unsigned char)~mask);
}

void world_set_counts(world_t *w, int eyes, int small_eyes, int feathers) {
    if (!w) return;
    w->eyes_opened = clamp_count(eyes, WORLD_EYES_MAX);
    w->small_eyes = clamp_count(small_eyes, WORLD_COUNT_MAX);
    w->feathers = clamp_count(feathers, WORLD_COUNT_MAX);
}
