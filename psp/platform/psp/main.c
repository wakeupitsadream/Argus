/* main.c — точка входа PSP: коллбэки, файлы, ввод, рендер, игровой цикл. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>
#include "sys_psp.h"
#include "fs_psp.h"
#include "input_psp.h"
#include "gu_render.h"
#include "game.h"
#include "platform.h"

PSP_MODULE_INFO("ARGUS", 0, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024); /* вся память минус 1 МБ под системные нужды */

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
    fs_mkdir("shots");

    while (sys_running()) {
        input_t in;
        input_poll(&in);
        game_tick(&s_game, &in);
        game_build_frame(&s_game, &s_frame);
        r_draw_frame(&s_frame);
        if (s_frame.shot_name[0]) {
            char rel[64];
            snprintf(rel, sizeof rel, "shots/%s.bmp", s_frame.shot_name);
            int rc = r_screenshot_bmp(rel);
            plat_log("argus: скриншот %s -> %d (%s)", rel, rc, s_frame.dbg);
        }
        if (s_frame.quit) break;
    }
    plat_log("argus: выход на кадре %d", s_game.frame);
    game_shutdown(&s_game);
    r_term();
    sceKernelExitGame();
    return 0;
}
