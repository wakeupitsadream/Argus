/* world.h — постоянное состояние мира: битовые флаги и счётчики прогресса.
 * Флаги 1..199 соответствуют id сущностей уровня («сделано»), 200..255 — глобальные. */
#ifndef ARGUS_WORLD_H
#define ARGUS_WORLD_H

#define WORLD_FLAG_COUNT 256
#define WORLD_ENTITY_FLAG_MAX 199

enum {
    WFLAG_ENDING_SEEN_A = 200, /* разбудил Аргуса */
    WFLAG_ENDING_SEEN_B = 201, /* отпустил мир */
    WFLAG_BONUS_OPEN = 202,    /* открыт бонусный остров */
    WFLAG_INTRO_DONE = 203
};

typedef struct {
    unsigned char bits[WORLD_FLAG_COUNT / 8];
    unsigned short eyes_opened;   /* больших глаз открыто (0..4) */
    unsigned short small_eyes;    /* малых глаз найдено */
    unsigned short feathers;      /* перьев получено */
} world_t;

void world_reset(world_t *w);
int world_flag(const world_t *w, int id);
void world_set_flag(world_t *w, int id, int value);
/* Счётчики пересчитываются по флагам сущностей уровня — вызывается после загрузки. */
/* Пересчёт счётчиков по флагам: eyes_opened, small_eyes, feathers задаются вызывающим. */
void world_set_counts(world_t *w, int eyes, int small_eyes, int feathers);

#endif
