/* walk.h — перемещение персонажа по сетке уровня: круг радиуса r, скольжение вдоль стен,
 * автоподъём на невысокие ступени, рампы-лестницы. Прыжка и физики нет. */
#ifndef ARGUS_WALK_H
#define ARGUS_WALK_H
#include "level.h"

typedef struct {
    float x, z; /* мировые координаты центра круга */
    float y;    /* высота пола под персонажем */
} walk_pos_t;

#define WALK_NO_FLOOR (-1.0e9f)

/* Высота пола в точке (с учётом рамп); WALK_NO_FLOOR, если стоять нельзя. */
float walk_floor_at(const level_t *l, float x, float z);
int walk_is_walkable(const level_t *l, int cx, int cz);

/* Сдвигает pos на (dx, dz) с раздельным резолвом по осям (скольжение вдоль стен).
 * step_max — максимальный подъём/спуск за шаг. Обновляет pos->y.
 * Возвращает 1, если позиция изменилась. */
int walk_move(const level_t *l, walk_pos_t *pos, float dx, float dz, float radius, float step_max);

/* Может ли круг радиуса radius стоять в точке (x, z): под всеми пробами есть пол и он
 * не выше шага от пола под центром. Высота стояния — в out_y. Нужна, чтобы поймать
 * ситуацию, когда мир изменился под ногами: поднявшаяся вода или повернувшийся
 * сегмент делают законную точку незаконной, и игрок перестаёт двигаться вообще. */
int walk_stand_ok(const level_t *l, float x, float z, float radius, float step_max, float *out_y);

/* Ставит персонажа в точку спавна уровня. */
void walk_spawn(const level_t *l, walk_pos_t *pos);

#endif
