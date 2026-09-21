#include "game.h"
#include "platform.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CAM_TURN_FRAMES 36
#define CAM_PITCH_DEG 33.0f
#define CAM_DIST 40.0f
#define CAM_HALF_W 12.0f

static void normalize3(float v[3]) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

int game_init(game_t *g) {
    memset(g, 0, sizeof *g);
    size_t len = 0;
    void *blob = plat_read_file("data/palettes.pal", &len);
    if (palette_set_load(&g->pals, blob, len) != 0) {
        plat_log("game: не удалось загрузить data/palettes.pal");
        if (blob) plat_free(blob);
        return -1;
    }
    g->pal = palette_find(&g->pals, "hub");
    if (!g->pal) { plat_log("game: нет палитры hub"); return -1; }

    blob = plat_read_file("data/hub.msh", &len);
    if (mesh_load(&g->island, blob, len) != 0) {
        plat_log("game: не удалось загрузить data/hub.msh");
        if (blob) plat_free(blob);
        return -1;
    }
    g->light.dir[0] = 0.45f; g->light.dir[1] = 1.0f; g->light.dir[2] = 0.3f;
    normalize3(g->light.dir);
    g->light.ambient = 0.58f;
    g->light.diffuse = 0.42f;
    mesh_recolor(&g->island, g->pal, &g->light);
    plat_log("game: остров %d треугольников", g->island.count / 3);

    tween_set(&g->cam_yaw, 45.0f);

    char *script = (char *)plat_read_file("autoplay.txt", &len);
    if (script) {
        int n = autoplay_parse(&g->ap, script);
        plat_log("game: autoplay %d событий", n);
        plat_free(script);
    }
    return 0;
}

void game_tick(game_t *g, const input_t *in_real) {
    input_t in = *in_real;
    if (g->ap.active) {
        char shot[AP_NAME_LEN] = {0};
        int flags = autoplay_step(&g->ap, g->frame, &in, shot);
        if (flags & AP_FLAG_SHOT) snprintf(g->pending_shot, sizeof g->pending_shot, "%s", shot);
        if (flags & AP_FLAG_QUIT) g->pending_quit = 1;
    }
    unsigned pressed = in.buttons & ~g->prev_buttons;
    g->prev_buttons = in.buttons;

    if ((pressed & BTN_L) && tween_done(&g->cam_yaw)) {
        g->cam_angle = (g->cam_angle + 3) % 4;
        tween_start(&g->cam_yaw, g->cam_yaw.value - 90.0f, CAM_TURN_FRAMES);
    } else if ((pressed & BTN_R) && tween_done(&g->cam_yaw)) {
        g->cam_angle = (g->cam_angle + 1) % 4;
        tween_start(&g->cam_yaw, g->cam_yaw.value + 90.0f, CAM_TURN_FRAMES);
    }
    if (pressed & BTN_START) g->pending_quit = 1;

    tween_update(&g->cam_yaw, ease_in_out_cubic);
    g->frame++;
}

void game_build_frame(game_t *g, frame_t *f) {
    memset(f, 0, sizeof *f);
    f->env.sky_top = g->pal->sky_top;
    f->env.sky_bottom = g->pal->sky_bottom;
    f->env.fog_color = g->pal->sky_bottom;
    /* Туман считается от глаза камеры, поэтому дистанции палитры — относительно плоскости цели. */
    f->env.fog_near = CAM_DIST + g->pal->fog_near;
    f->env.fog_far = CAM_DIST + g->pal->fog_far;

    f->cam.target[0] = 0.0f; f->cam.target[1] = 0.0f; f->cam.target[2] = 0.0f;
    f->cam.yaw_deg = g->cam_yaw.value;
    f->cam.pitch_deg = CAM_PITCH_DEG;
    f->cam.dist = CAM_DIST;
    f->cam.half_w = CAM_HALF_W;

    frame_mesh_t *m = &f->meshes[f->mesh_count++];
    m->mesh = &g->island;
    m->pos[0] = m->pos[1] = m->pos[2] = 0.0f;
    m->yaw_deg = 0.0f;

    if (g->pending_shot[0]) {
        snprintf(f->shot_name, sizeof f->shot_name, "%s", g->pending_shot);
        g->pending_shot[0] = 0;
    }
    f->quit = g->pending_quit;
    snprintf(f->dbg, sizeof f->dbg, "frame %d yaw %.0f angle %d", g->frame, (double)g->cam_yaw.value, g->cam_angle);
}

void game_shutdown(game_t *g) {
    mesh_free(&g->island);
    palette_set_free(&g->pals);
}
