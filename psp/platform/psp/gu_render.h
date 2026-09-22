/* gu_render.h — исполнение frame_t на GPU PSP (sceGu/sceGum). */
#ifndef ARGUS_GU_RENDER_H
#define ARGUS_GU_RENDER_H
#include "frame.h"
#include "platform.h"
int r_init(void);
/* Рисует кадр и заполняет в stats поля cpu_us, gpu_us, tris, draws. */
void r_draw_frame(const frame_t *f, plat_stats_t *stats);
/* Сохраняет последний показанный кадр в 24-битный BMP по пути относительно каталога игры. */
int r_screenshot_bmp(const char *rel_path);
void r_term(void);
#endif
