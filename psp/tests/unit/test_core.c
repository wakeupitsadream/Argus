/* test_core.c — юнит-тесты core/. Запуск: make test (ARGUS_ASSETS=build/assets). */
#include "minitest.h"
#include "ease.h"
#include "autoplay.h"
#include "palette.h"
#include "mesh.h"
#include "game.h"
#include "platform.h"
#include "tests.h"
#include <stdlib.h>

TEST(test_ease_bounds) {
    CHECK_NEAR(ease_in_out_cubic(0.0f), 0.0, 1e-6);
    CHECK_NEAR(ease_in_out_cubic(1.0f), 1.0, 1e-6);
    CHECK_NEAR(ease_in_out_cubic(0.5f), 0.5, 1e-6);
    CHECK_NEAR(ease_out_expo(1.0f), 1.0, 1e-6);
    CHECK_NEAR(ease_out_back(1.0f), 1.0, 1e-5);
    CHECK_NEAR(ease_in_out_cubic(2.0f), 1.0, 1e-6); /* clamp */
    float prev = 0.0f;
    for (int i = 1; i <= 20; i++) {
        float v = ease_in_out_cubic((float)i / 20.0f);
        CHECK(v >= prev);
        prev = v;
    }
}

TEST(test_tween) {
    tween_t tw;
    tween_set(&tw, 0.0f);
    CHECK(tween_done(&tw));
    tween_start(&tw, 90.0f, 10);
    CHECK(!tween_done(&tw));
    float last = 0.0f;
    for (int i = 0; i < 10; i++) {
        float v = tween_update(&tw, ease_in_out_cubic);
        CHECK(v >= last);
        last = v;
    }
    CHECK(tween_done(&tw));
    CHECK_NEAR(tw.value, 90.0, 1e-4);
    CHECK_NEAR(tween_update(&tw, ease_in_out_cubic), 90.0, 1e-4);
}

TEST(test_autoplay_parse_and_step) {
    autoplay_t ap;
    const char *script =
        "# smoke\n"
        "@10 press L\n"
        "@12 release L\n"
        "@20 stick -1.0 0.5\n"
        "@30 shot hub_0\n"
        "@40 quit\n";
    CHECK_EQ(autoplay_parse(&ap, script), 5);
    CHECK(ap.active);
    input_t in;
    char shot[AP_NAME_LEN] = {0};
    CHECK_EQ(autoplay_step(&ap, 9, &in, shot), 0);
    CHECK_EQ(in.buttons, 0);
    CHECK_EQ(autoplay_step(&ap, 10, &in, shot), 0);
    CHECK_EQ(in.buttons, BTN_L);
    CHECK_EQ(autoplay_step(&ap, 12, &in, shot), 0);
    CHECK_EQ(in.buttons, 0);
    autoplay_step(&ap, 20, &in, shot);
    CHECK_NEAR(in.lx, -1.0, 1e-6);
    CHECK_NEAR(in.ly, 0.5, 1e-6);
    CHECK_EQ(autoplay_step(&ap, 30, &in, shot), AP_FLAG_SHOT);
    CHECK_STR(shot, "hub_0");
    CHECK_EQ(autoplay_step(&ap, 35, &in, shot), 0);
    CHECK_EQ(autoplay_step(&ap, 40, &in, shot), AP_FLAG_QUIT);
    /* пропущенные кадры применяются все сразу */
    autoplay_t ap2;
    CHECK_EQ(autoplay_parse(&ap2, "@1 press R\n@2 shot a\n@3 quit\n"), 3);
    CHECK_EQ(autoplay_step(&ap2, 100, &in, shot), AP_FLAG_SHOT | AP_FLAG_QUIT);
    CHECK_EQ(in.buttons, BTN_R);
}

TEST(test_autoplay_long_lines) {
    autoplay_t ap;
    /* Комментарий длиннее буфера разбора не должен ломать следующие строки. */
    char script[2048];
    size_t pos = 0;
    script[pos++] = '#';
    for (int i = 0; i < 700; i++) script[pos++] = 'x';
    script[pos++] = '\n';
    const char *tail = "@10 press L\n@20 assert fps>=58\n@30 quit\n";
    memcpy(script + pos, tail, strlen(tail) + 1);
    CHECK_EQ(autoplay_parse(&ap, script), 3);
    CHECK_EQ(ap.ev[0].kind, AP_PRESS);
    CHECK_EQ(ap.ev[1].kind, AP_ASSERT);
    CHECK_STR(ap.ev[1].name, "fps>=58");
    CHECK_EQ(ap.ev[2].kind, AP_QUIT);

    /* Проверки попадают в очередь кадра и видны вызывающему. */
    input_t in;
    char shot[AP_NAME_LEN] = {0};
    CHECK_EQ(autoplay_step(&ap, 20, &in, shot) & AP_FLAG_ASSERT, AP_FLAG_ASSERT);
    CHECK_EQ(ap.assert_count, 1);
    CHECK_STR(ap.asserts[0], "fps>=58");
    /* На следующем кадре очередь пуста. */
    autoplay_step(&ap, 21, &in, shot);
    CHECK_EQ(ap.assert_count, 0);

    /* Длинное имя кадра обрезается, но не портит память. */
    autoplay_t ap2;
    CHECK_EQ(autoplay_parse(&ap2, "@1 shot aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"), 1);
    char shot2[AP_NAME_LEN] = {0};
    autoplay_step(&ap2, 1, &in, shot2);
    CHECK_EQ((int)strlen(shot2), AP_NAME_LEN - 1);
}

TEST(test_autoplay_errors) {
    autoplay_t ap;
    CHECK_EQ(autoplay_parse(&ap, "@5 press nosuch\n"), -1);
    CHECK_EQ(autoplay_parse(&ap, "press L\n"), -1);
    CHECK_EQ(autoplay_parse(&ap, "@5 dance\n"), -1);
    CHECK_EQ(autoplay_parse(&ap, ""), 0);
    CHECK(!ap.active);
    CHECK_EQ(autoplay_parse(&ap, NULL), 0);
}

TEST(test_palette_file) {
    size_t len = 0;
    void *blob = plat_read_file("data/palettes.pal", &len);
    CHECK(blob != NULL);
    if (!blob) return;
    palette_set_t ps;
    CHECK_EQ(palette_set_load(&ps, blob, len), 0);
    CHECK(ps.count >= 2);
    const palette_t *hub = palette_find(&ps, "hub");
    CHECK(hub != NULL);
    if (hub) {
        CHECK_EQ(hub->sky_top >> 24, 0xFF);
        CHECK(hub->fog_far > hub->fog_near);
        CHECK(hub->slots[SLOT_TOP] != hub->slots[SLOT_WALL]);
    }
    CHECK(palette_find(&ps, "nope") == NULL);
    palette_set_free(&ps);

    palette_set_t bad;
    char junk[64] = "JUNK";
    CHECK_EQ(palette_set_load(&bad, junk, sizeof junk), -1);
}

TEST(test_mesh_shading) {
    light_t light = { { 0.0f, 1.0f, 0.0f }, 0.5f, 0.5f };
    float up[3] = { 0.0f, 1.0f, 0.0f }, side[3] = { 1.0f, 0.0f, 0.0f };
    unsigned white = 0xFFFFFFFFu;
    CHECK_EQ(mesh_shade_color(white, 255, up, &light), 0xFFFFFFFFu);      /* полный свет */
    CHECK_EQ(mesh_shade_color(white, 255, side, &light), 0xFF808080u);    /* только ambient */
    CHECK_EQ(mesh_shade_color(0xFF0000FFu, 255, up, &light) & 0xFFFFFFu, 0x0000FFu); /* красный */
    CHECK_EQ(mesh_shade_color(white, 0, up, &light), 0xFF000000u);        /* ao = 0 */
}

TEST(test_mesh_file) {
    size_t len = 0;
    void *blob = plat_read_file("data/hub.msh", &len);
    CHECK(blob != NULL);
    if (!blob) return;
    mesh_t m;
    CHECK_EQ(mesh_load(&m, blob, len), 0);
    CHECK(m.count > 0);
    CHECK_EQ(m.count % 3, 0);
    CHECK(m.bbox_max[1] > m.bbox_min[1]);
    CHECK_EQ(((unsigned long)(size_t)m.verts) & 15UL, 0);
    for (int i = 0; i < m.count; i++) {
        CHECK(m.src[i].slot < PAL_SLOTS);
        CHECK_NEAR(m.verts[i].x, m.src[i].x, 1e-6);
    }
    /* нормали единичные, обход против часовой снаружи (нормаль грани сонаправлена с записанной) */
    int bad_winding = 0;
    for (int i = 0; i + 2 < m.count; i += 3) {
        const mesh_src_vertex_t *a = &m.src[i], *b = &m.src[i + 1], *c = &m.src[i + 2];
        float ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
        float vx = c->x - a->x, vy = c->y - a->y, vz = c->z - a->z;
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (nx * a->nx + ny * a->ny + nz * a->nz <= 0.0f) bad_winding++;
    }
    CHECK_EQ(bad_winding, 0);
    mesh_free(&m);

    mesh_t bad;
    char junk[80] = "JUNK";
    CHECK_EQ(mesh_load(&bad, junk, sizeof junk), -1);
}

/* Один шаг игры с нажатием кнопок в этом кадре. */
static void tick_btn(game_t *g, unsigned buttons) {
    input_t in;
    memset(&in, 0, sizeof in);
    in.buttons = buttons;
    game_tick(g, &in, NULL);
    /* Отпускаем, иначе следующий кадр не увидит новое нажатие. */
    memset(&in, 0, sizeof in);
    game_tick(g, &in, NULL);
}

static void tick_idle(game_t *g, int frames) {
    input_t in;
    memset(&in, 0, sizeof in);
    for (int i = 0; i < frames; i++) game_tick(g, &in, NULL);
}

TEST(test_game_loop) {
    game_t *g = calloc(1, sizeof *g);
    CHECK_EQ(game_init(g), 0);
    frame_t f;

    /* Игра начинается с заставки: мир рисуется фоном, игровой HUD — нет. */
    CHECK_EQ(g->screens.current, SCR_TITLE);
    game_build_frame(g, &f);
    CHECK(f.mesh_count >= 1);
    CHECK(f.meshes[0].mesh == &g->island);
    CHECK(f.mesh_count <= FRAME_MAX_MESHES);

    /* Выбираем «Новая игра» и ждём, пока закроется и откроется занавес. */
    int guard = 0;
    while (g->screens.items[g->screens.index] != ACT_NEW && guard++ < 8) tick_btn(g, BTN_DOWN);
    CHECK_EQ(g->screens.items[g->screens.index], ACT_NEW);
    tick_btn(g, BTN_CROSS);
    tick_idle(g, 80);
    CHECK_EQ(g->screens.current, SCR_GAME);
    CHECK_EQ(screens_busy(&g->screens), 0);

    /* В игре работает поворот камеры. */
    int angle_before = g->cam.angle;
    tick_btn(g, BTN_R);
    tick_idle(g, 60);
    CHECK_EQ(g->cam.angle, (angle_before + 1) % 4);

    /* Режим взгляда выцвечивает сцену. */
    input_t look;
    memset(&look, 0, sizeof look);
    look.buttons = BTN_CIRCLE;
    for (int i = 0; i < 30; i++) game_tick(g, &look, NULL);
    game_build_frame(g, &f);
    CHECK(f.env.desat > 0.5f);
    CHECK_EQ(g->player.look_active, 1);
    tick_idle(g, 30);
    game_build_frame(g, &f);
    CHECK(f.env.desat < 0.5f);

    /* Start открывает паузу, а не выходит из игры. */
    tick_btn(g, BTN_START);
    tick_idle(g, 80);
    CHECK_EQ(g->screens.current, SCR_PAUSE);
    CHECK(!f.quit);

    /* В паузе движение выключено. */
    input_t walk;
    memset(&walk, 0, sizeof walk);
    walk.ly = -1.0f;
    float px = g->player.pos.x, pz = g->player.pos.z;
    for (int i = 0; i < 30; i++) game_tick(g, &walk, NULL);
    CHECK_NEAR(g->player.pos.x, px, 1e-5);
    CHECK_NEAR(g->player.pos.z, pz, 1e-5);

    /* Возврат в игру из паузы. */
    guard = 0;
    while (g->screens.items[g->screens.index] != ACT_RESUME && guard++ < 8) tick_btn(g, BTN_DOWN);
    tick_btn(g, BTN_CROSS);
    tick_idle(g, 80);
    CHECK_EQ(g->screens.current, SCR_GAME);

    /* Выход: пауза → в меню → «Выход». */
    tick_btn(g, BTN_START);
    tick_idle(g, 80);
    guard = 0;
    while (g->screens.items[g->screens.index] != ACT_TO_TITLE && guard++ < 8) tick_btn(g, BTN_DOWN);
    tick_btn(g, BTN_CROSS);
    tick_idle(g, 80);
    CHECK_EQ(g->screens.current, SCR_TITLE);
    guard = 0;
    while (g->screens.items[g->screens.index] != ACT_QUIT && guard++ < 8) tick_btn(g, BTN_DOWN);
    tick_btn(g, BTN_CROSS);
    game_build_frame(g, &f);
    CHECK(f.quit);

    game_shutdown(g);
    free(g);
}

void tests_core(void) {
    puts("core tests");
    RUN(test_ease_bounds);
    RUN(test_tween);
    RUN(test_autoplay_parse_and_step);
    RUN(test_autoplay_long_lines);
    RUN(test_autoplay_errors);
    RUN(test_palette_file);
    RUN(test_mesh_shading);
    RUN(test_mesh_file);
    RUN(test_game_loop);
}
