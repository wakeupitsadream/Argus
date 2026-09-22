/* gu_text.h — вывод текста из frame_t на GPU PSP: T8-атлас + CLUT + 2D-спрайты. */
#ifndef ARGUS_GU_TEXT_H
#define ARGUS_GU_TEXT_H
#include "font.h"
#include "frame.h"

/* Запоминает шрифт (не копирует) и готовит CLUT. 0 при успехе. */
int gu_text_init(const font_t *font);
/* Рисует все f->texts. Вызывается внутри sceGuStart…sceGuFinish последним проходом.
 * Сам выставляет всё нужное состояние и возвращает его к непрозрачному проходу. */
void gu_text_draw(const frame_t *f);
/* Число квадов, отправленных на прошлом кадре (для debug-оверлея). */
int gu_text_last_quads(void);

/* Число вызовов отрисовки на прошлом кадре (для бюджета draw-call). */
int gu_text_last_calls(void);

#endif
