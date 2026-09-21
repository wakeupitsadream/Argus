/* game.h — состояние игры и цикл: game_init → (game_tick, game_build_frame)* → game_shutdown. */
#ifndef ARGUS_GAME_H
#define ARGUS_GAME_H
#include "input.h"
#include "frame.h"
#include "palette.h"
#include "mesh.h"
#include "ease.h"
#include "autoplay.h"

typedef struct {
    palette_set_t pals;
    const palette_t *pal;
    mesh_t island;
    light_t light;
    int frame;
    int cam_angle;       /* 0..3, ракурс */
    tween_t cam_yaw;     /* градусы */
    unsigned prev_buttons;
    autoplay_t ap;
    int pending_quit;
    char pending_shot[AP_NAME_LEN];
} game_t;

int game_init(game_t *g);
void game_tick(game_t *g, const input_t *in_real);
void game_build_frame(game_t *g, frame_t *f);
void game_shutdown(game_t *g);

#endif
