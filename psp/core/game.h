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
#include "screens.h"
#include "world.h"
#include "save.h"
#include "entity.h"
#include "puzzles.h"
#include "quest.h"
#include "audio.h"

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
    int level_index;
    mesh_t island;
    int island_ok;
    mesh_t objects[MESH_COUNT];
    int object_ok[MESH_COUNT];
    mesh_t ghost[3];            /* тело, голова, радужка Око плоским цветом */
    int ghost_ok[3];

    player_t player;
    camera_t cam;
    screens_t screens;
    world_t world;
    entities_t entities;
    beam_t beam;
    int msg_str;        /* строка, которую показываем внизу (реплика, надпись, подсказка) */
    int msg_frames;     /* сколько кадров ещё показывать */
    int prompt_id;      /* id сущности, с которой можно взаимодействовать прямо сейчас */
    int prompt_type;    /* её тип (ENT_*) — от него зависит текст подсказки */

    /* Переход между островами: свой занавес, независимый от смены экранов. */
    int travel_to;      /* индекс целевого уровня или -1 */
    int travel_entry;   /* id сущности-входа на нём */
    int travel_phase;   /* 0 — нет перехода, 1 — гаснет, 2 — разгорается */
    tween_t travel;     /* 0..1 — чернота перехода */
    int ending_started; /* финальный выбор уже показан */
    float title_yaw;   /* медленный облёт острова на заставке */
    particles_t particles;

    audio_t audio;          /* синтезатор: его читает поток звука, менять только через API */
    void *ambient_pcm;      /* текущая петля региона (владеем) */
    void *ambient_old;      /* прошлая петля: живёт ещё несколько кадров — её мог читать микшер */
    int ambient_free_in;    /* кадров до освобождения ambient_old */
    float step_dist;        /* пройденный путь: каждый шаг — тон */
    tween_t desat;                        /* выцветание сцены в режиме взгляда */
    float ent_vis[GAME_MAX_ENT_STATE];    /* визуальное состояние 0..1 (дверь, панель) */

    int frame;
    int show_debug;
    int hide_ui;        /* autoplay «ui 0»: кадр без интерфейса — для XMB и эталонов */
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
