/* game.h — состояние игры и цикл: game_init → (game_tick, game_build_frame)* → game_shutdown. */
#ifndef ARGUS_GAME_H
#define ARGUS_GAME_H
#include "input.h"
#include "frame.h"
#include "palette.h"
#include "mesh.h"
#include "font.h"
#include "i18n.h"
#include "ease.h"
#include "autoplay.h"
#include "platform.h"
#include "level.h"
#include "walk.h"
#include "camera.h"
#include "observe.h"
#include "player.h"
#include "particles.h"

/* Меши объектов (файлы data/mesh_<имя>.msh, порядок — как в MESH_FILES в game.c). */
enum {
    MESH_EYE_BODY = 0, MESH_EYE_HEAD, MESH_EYE_IRIS, MESH_ECHO, MESH_MIRROR, MESH_PRISM,
    MESH_RECEIVER, MESH_LEVER, MESH_PLATE, MESH_BLOCK, MESH_DOOR, MESH_EYE_SMALL,
    MESH_EYE_BIG, MESH_STONE, MESH_PLINTH, MESH_PEACOCK_TAIL, MESH_PEACOCK_FEATHER,
    MESH_FEATHER, MESH_COUNT
};

#define GAME_MAX_ENT_STATE 128

typedef struct {
    palette_set_t pals;
    const palette_t *pal;
    light_t light;

    font_t font;
    int font_ok;
    i18n_t strings[LANG_COUNT];
    int strings_ok;
    int lang;

    level_t level;
    int level_ok;
    mesh_t island;
    int island_ok;
    mesh_t objects[MESH_COUNT];
    int object_ok[MESH_COUNT];
    mesh_t ghost[3];            /* тело, голова, радужка Око плоским цветом */
    int ghost_ok[3];

    player_t player;
    camera_t cam;
    particles_t particles;
    tween_t desat;                        /* выцветание сцены в режиме взгляда */
    float ent_phase[GAME_MAX_ENT_STATE];  /* фаза анимации сущности */
    unsigned char ent_watched[GAME_MAX_ENT_STATE];

    int frame;
    int show_debug;
    unsigned prev_buttons;

    autoplay_t ap;
    int assert_pass, assert_fail;
    int pending_quit;
    char pending_shot[AP_NAME_LEN];
    plat_stats_t stats;
} game_t;

int game_init(game_t *g);
void game_tick(game_t *g, const input_t *in_real, const plat_stats_t *stats);
void game_build_frame(game_t *g, frame_t *f);
void game_shutdown(game_t *g);

#endif
