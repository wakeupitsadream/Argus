/* gu_sprite.h — аддитивные билборды (свечение, искры, пылинки) из frame_t. */
#ifndef ARGUS_GU_SPRITE_H
#define ARGUS_GU_SPRITE_H
#include "frame.h"

/* Готовит процедурную радиальную текстуру. 0 при успехе. */
int gu_sprite_init(void);
/* Рисует f->sprites поверх сцены: проецирует через cam_project, аддитивное смешивание.
 * Вызывается внутри sceGuStart…sceGuFinish до вывода текста. */
void gu_sprite_draw(const frame_t *f);
int gu_sprite_last_count(void);

/* Число вызовов отрисовки на прошлом кадре (для бюджета draw-call). */
int gu_sprite_last_calls(void);

#endif
