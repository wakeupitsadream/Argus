/* main.c — точка входа PSP: коллбэки, файлы, ввод, рендер, текст, игровой цикл. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspsysmem.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <string.h>
#include "sys_psp.h"
#include "fs_psp.h"
#include "input_psp.h"
#include "gu_render.h"
#include "gu_text.h"
#include "game.h"
#include "platform.h"

PSP_MODULE_INFO("ARGUS", 0, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(16384); /* 16 МБ кучи: помещается и на PSP-1000 (24 МБ пользователю) */

static game_t s_game;
static frame_t s_frame;

int main(int argc, char *argv[]) {
    sys_setup_callbacks();
    fs_init(argc > 0 ? argv[0] : NULL);
    input_init();
    r_init();
    plat_log("argus: старт, каталог '%s'", fs_base());

    if (game_init(&s_game) != 0) {
        plat_log("argus: ошибка инициализации, выход");
        r_term();
        sceKernelExitGame();
        return 0;
    }
    if (s_game.font_ok && gu_text_init(&s_game.font) != 0) {
        plat_log("argus: текст отключён (gu_text_init)");
        s_game.font_ok = 0;
    }
    fs_mkdir("shots");

    plat_stats_t stats;
    memset(&stats, 0, sizeof stats);
    SceInt64 prev = sceKernelGetSystemTimeWide();

    while (sys_running()) {
        input_t in;
        input_poll(&in);
        game_tick(&s_game, &in, &stats);
        game_build_frame(&s_game, &s_frame);
        r_draw_frame(&s_frame, &stats);

        SceInt64 now = sceKernelGetSystemTimeWide();
        unsigned dt = (unsigned)(now - prev);
        prev = now;
        stats.frame_us = dt;
        stats.fps = dt ? 1000000.0f / (float)dt : 0.0f;
        stats.heap_free = (unsigned)sceKernelTotalFreeMemSize();
        stats.heap_max = (unsigned)sceKernelMaxFreeMemSize();

        if (s_frame.shot_name[0]) {
            char rel[64];
            snprintf(rel, sizeof rel, "shots/%s.bmp", s_frame.shot_name);
            int rc = r_screenshot_bmp(rel);
            plat_log("argus: скриншот %s -> %d (кадр %d, %u тр., cpu %u мкс, куча %u)",
                     rel, rc, s_game.frame, stats.tris, stats.cpu_us, stats.heap_free);
        }
        if (s_frame.quit) break;
    }
    plat_log("argus: выход на кадре %d", s_game.frame);
    if (s_game.assert_pass || s_game.assert_fail) {
        plat_log("AUTOPLAY ИТОГ: пройдено %d, провалено %d", s_game.assert_pass, s_game.assert_fail);
    }
    game_shutdown(&s_game);
    r_term();
    sceKernelExitGame();
    return 0;
}
