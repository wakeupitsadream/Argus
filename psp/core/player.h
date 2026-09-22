/* player.h — Око: перемещение по сетке, направление, процедурная анимация (покачивание,
 * наклон в движении, моргание). Скелетной анимации нет. */
#ifndef ARGUS_PLAYER_H
#define ARGUS_PLAYER_H
#include "walk.h"
#include "input.h"
#include "frame.h"

typedef struct {
    walk_pos_t pos;
    float yaw_deg;      /* куда смотрит персонаж */
    float speed;        /* текущая скорость, единиц/с */
    float bob;          /* фаза покачивания */
    float lean;         /* наклон в движении */
    float blink;        /* фаза моргания */
    int look_active;
} player_t;

void player_init(player_t *p, const level_t *l);
/* Один шаг логики. cam_yaw_deg — текущий ракурс камеры: стик «вверх» уводит от камеры. */
void player_tick(player_t *p, const level_t *l, const input_t *in, float cam_yaw_deg);

/* Возвращает Око в законную точку, если мир изменился под ногами: поднявшаяся вода
 * или повернувшийся сегмент могут запереть его на месте намертво. 1 — пришлось сдвинуть. */
int player_unstick(player_t *p, const level_t *l);
/* Кладёт меши персонажа в кадр (тело, голова, радужка). */
void player_build(const player_t *p, frame_t *f, const mesh_t *body, const mesh_t *head, const mesh_t *iris);

#endif
