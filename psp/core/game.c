#include "game.h"
#include "platform.h"
#include "strings_ids.h"
#include "screens.h"
#include "entity_types.h"
#include "level_ids.h"
#include "quests_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCR_W 480
#define SCR_H 272
#define DESAT_FRAMES 15
#define PLAYER_EYE_H 0.5f   /* камера смотрит чуть выше пола */
#define SLEEPER_RATE 26.0f  /* градусов в секунду у спящих объектов */
#define TITLE_ORBIT 6.0f    /* градусов в секунду: медленный облёт на заставке */
#define TITLE_SHIFT 5.2f    /* на сколько остров уезжает вправо под плашку названия */
#define EYES_TOTAL 4        /* больших глаз в игре */
#define BONUS_FEATHERS 3    /* перьев за просьбы отголосков, чтобы открылся бонусный остров */
#define PORTAL_MARKS_MAX 6  /* столбов света: пул спрайтов кадра делится с лучом и пылью */
#define CAPTION_HOLD 200    /* кадров: карточка с названием острова держится ~3,3 с */
#define HINT_HOLD 900       /* кадров: подсказки по управлению живут 15 с на острове */
#define HUD_FADE 60         /* кадр затухания — 1 с */
#define INTERACT_REACH 1.1f /* на каком расстоянии Око достаёт до механизма */
#define MSG_FRAMES 210      /* 3,5 с на реплику */
#define BEAM_STEP 0.20f      /* шаг спрайтов вдоль луча: реже — и луч рассыпается в пунктир */
#define BEAM_SPRITES_MAX 60  /* бюджет спрайтов на луч: остальное — пылинки и свечения */
#define TRAVEL_FRAMES 20     /* затемнение при переходе между островами */
#define DOOR_DROP 1.5f       /* насколько открытая дверь уходит в пол */
#define ENT_VIS_RATE 0.18f   /* догоняющее сглаживание двери и панели (как у камеры) */
#define DUST_COUNT 28        /* пылинок в воздухе: остаток пула нужен лучу и свечениям */
#define VIGNETTE_BASE 0.40f  /* затемнение краёв кадра; в режиме взгляда чуть сильнее */
#define TAIL_FEATHERS_MAX 12 /* перьев на хвосте: больше не влезает в пул мешей кадра */
#define TAIL_FAN_DEG 150.0f  /* раскрытие веера */
#define TAIL_TILT_DEG 26.0f  /* наклон пера наружу */
#define DT (1.0f / 60.0f)

static const char *const MESH_FILES[MESH_COUNT] = {
    "data/mesh_eye_body.msh", "data/mesh_eye_head.msh", "data/mesh_eye_iris.msh",
    "data/mesh_echo.msh", "data/mesh_mirror.msh", "data/mesh_prism.msh",
    "data/mesh_receiver.msh", "data/mesh_lever.msh", "data/mesh_plate.msh",
    "data/mesh_block.msh", "data/mesh_door.msh", "data/mesh_eye_small.msh",
    "data/mesh_eye_big.msh", "data/mesh_stone.msh", "data/mesh_plinth.msh",
    "data/mesh_peacock_tail.msh", "data/mesh_peacock_feather.msh", "data/mesh_feather.msh"
};

/* Какой меш рисовать для типа сущности; -1 — не рисуется. */
static int mesh_for_entity(int type) {
    switch (type) {
    case ENT_ECHO: return MESH_ECHO;
    case ENT_SMALL_EYE: return MESH_EYE_SMALL;
    case ENT_BIG_EYE: return MESH_EYE_BIG;
    case ENT_STONE_TEXT: return MESH_STONE;
    case ENT_MIRROR: return MESH_MIRROR;
    case ENT_PRISM: return MESH_PRISM;
    case ENT_RECEIVER: return MESH_RECEIVER;
    case ENT_EMITTER: return MESH_RECEIVER;
    case ENT_LEVER: return MESH_LEVER;
    case ENT_PLATE: return MESH_PLATE;
    case ENT_BLOCK: return MESH_BLOCK;
    case ENT_FLOAT_BLOCK: return MESH_BLOCK;
    case ENT_DOOR: return MESH_DOOR;
    case ENT_PLINTH: return MESH_PLINTH;
    case ENT_PEACOCK_TAIL: return MESH_PEACOCK_TAIL;
    case ENT_FEATHER: return MESH_FEATHER;
    case ENT_SLEEPER: return MESH_ECHO;
    default: return -1;
    }
}

static void normalize3(float v[3]) {
    float len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

static void *load_optional(const char *rel, size_t *len, const char *what) {
    void *blob = plat_read_file(rel, len);
    if (!blob) plat_log("game: нет %s (%s) — работаем без него", rel, what);
    return blob;
}

static void load_text_resources(game_t *g) {
    size_t len = 0;
    void *blob = load_optional("data/font.bin", &len, "шрифт");
    if (blob) {
        if (font_load(&g->font, blob, len) == 0) {
            g->font_ok = 1;
            plat_log("game: шрифт %dx%d, гарнитур %d", g->font.tex_w, g->font.tex_h, g->font.face_count);
        } else {
            plat_log("game: битый data/font.bin");
            plat_free(blob);
        }
    }
    static const char *const files[LANG_COUNT] = { "data/strings_ru.bin", "data/strings_en.bin" };
    int loaded = 0;
    for (int i = 0; i < LANG_COUNT; i++) {
        blob = load_optional(files[i], &len, "строки");
        if (!blob) continue;
        if (i18n_load(&g->strings[i], blob, len) != 0) {
            plat_log("game: битый %s", files[i]);
            plat_free(blob);
        } else if (g->strings[i].count != STR_COUNT) {
            /* Устаревший файл сдвинул бы все STR_* — лучше вообще без текста. */
            plat_log("game: %s — %d строк, ожидается %d", files[i], g->strings[i].count, STR_COUNT);
            i18n_free(&g->strings[i]);
        } else {
            loaded++;
        }
    }
    if (loaded == LANG_COUNT) {
        g->strings_ok = 1;
        i18n_use(&g->strings[g->lang]);
    }
}

static void load_meshes(game_t *g) {
    for (int i = 0; i < MESH_COUNT; i++) {
        size_t len = 0;
        void *blob = plat_read_file(MESH_FILES[i], &len);
        if (!blob) continue;
        if (mesh_load(&g->objects[i], blob, len) == 0) {
            mesh_recolor(&g->objects[i], g->pal, &g->light);
            g->object_ok[i] = 1;
        } else {
            plat_log("game: битый %s", MESH_FILES[i]);
            plat_free(blob);
        }
    }
}

/* Силуэт Око: те же меши, залитые цветом свечения — видно сквозь геометрию. */
static void load_ghost_meshes(game_t *g) {
    static const int src[3] = { MESH_EYE_BODY, MESH_EYE_HEAD, MESH_EYE_IRIS };
    unsigned color = (0xD0u << 24) | (g->pal->slots[SLOT_GLOW] & 0x00FFFFFFu);
    for (int i = 0; i < 3; i++) {
        size_t len = 0;
        void *blob = plat_read_file(MESH_FILES[src[i]], &len);
        if (!blob) continue;
        if (mesh_load(&g->ghost[i], blob, len) == 0) {
            mesh_recolor_flat(&g->ghost[i], color);
            g->ghost_ok[i] = 1;
        } else {
            plat_free(blob);
        }
    }
}

/* Тональность региона (GDD §1.6): каждое взаимодействие звучит нотой своего аккорда.
 * Ноты — номера MIDI: 57 — ля большой октавы. Имя региона совпадает с именем палитры. */
typedef struct {
    const char *region;
    int root_midi;
    int chord[4];
} region_chord_t;

static const region_chord_t REGION_CHORDS[] = {
    { "hub",      57, { 0, 7, 12, 16 } },  /* ля: открытая квинта, спокойствие */
    { "mirrors",  60, { 0, 4, 7, 11 } },   /* до-мажор с большой септимой: свет */
    { "terraces", 55, { 0, 5, 7, 10 } },   /* соль-сус: механика, без наклонения */
    { "water",    62, { 0, 3, 7, 10 } },   /* ре-минор с септимой: вода */
    { "library",  53, { 0, 7, 14, 19 } },  /* фа: пустые квинты, тишина зала */
    { "gray",     50, { 0, 5, 10, 12 } },  /* ре: серый финал */
};

static void audio_set_region(game_t *g, const char *region) {
    const region_chord_t *pick = &REGION_CHORDS[0];
    for (unsigned i = 0; i < sizeof REGION_CHORDS / sizeof REGION_CHORDS[0]; i++) {
        if (strncmp(REGION_CHORDS[i].region, region, 16) == 0) { pick = &REGION_CHORDS[i]; break; }
    }
    audio_set_chord(&g->audio, pick->root_midi, pick->chord, 4);
}

/* Подключает эмбиент-петлю региона. Прошлый буфер не освобождается сразу: его мог
 * читать поток звука — он отпустит его к следующему вызову микшера (правило 14). */
static void audio_set_region_ambient(game_t *g, const char *region) {
    char rel[40];
    snprintf(rel, sizeof rel, "data/amb_%.16s.pcm", region);
    size_t len = 0;
    void *pcm = plat_read_file(rel, &len);
    if (!pcm) {
        plat_log("game: нет %s — регион без эмбиента", rel);
        audio_set_ambient(&g->audio, NULL, 0, 0.0f);
    } else {
        audio_set_ambient(&g->audio, (const short *)pcm, (int)(len / sizeof(short)), 0.7f);
    }
    if (g->ambient_old) plat_free(g->ambient_old); /* прошлый уже пережил свои кадры */
    g->ambient_old = g->ambient_pcm;
    g->ambient_free_in = 12;  /* 0,2 с — заведомо больше буфера pspaudiolib (≈23 мс) */
    g->ambient_pcm = pcm;
}

/* Пере-раскрашивает предметы и силуэты под текущую палитру: цвет живёт в палитре,
 * а не в ассете, поэтому смена региона — это только повторный резолв (TECH.md §2.4). */
static void recolor_objects(game_t *g) {
    for (int i = 0; i < MESH_COUNT; i++) {
        if (g->object_ok[i]) mesh_recolor(&g->objects[i], g->pal, &g->light);
    }
    unsigned color = (0xD0u << 24) | (g->pal->slots[SLOT_GLOW] & 0x00FFFFFFu);
    for (int i = 0; i < 3; i++) {
        if (g->ghost_ok[i]) mesh_recolor_flat(&g->ghost[i], color);
    }
}

/* Загружает остров и ставит на него Око. entry_id — id сущности-входа (0 — спавн уровня).
 * Старое освобождается только после удачной загрузки нового: неудача оставляет игру живой. */
static int load_level(game_t *g, int index, int entry_id) {
    const char *lvl_path = level_data_path((unsigned)index);
    const char *msh_path = level_mesh_path((unsigned)index);
    if (!lvl_path || !msh_path) { plat_log("game: нет уровня %d", index); return -1; }

    size_t len = 0;
    void *blob = plat_read_file(lvl_path, &len);
    level_t lv;
    if (!blob || level_load(&lv, blob, len) != 0) {
        if (blob) plat_free(blob);
        plat_log("game: не читается %s", lvl_path);
        return -1;
    }
    size_t mlen = 0;
    void *mblob = plat_read_file(msh_path, &mlen);
    mesh_t msh;
    if (!mblob || mesh_load(&msh, mblob, mlen) != 0) {
        if (mblob) plat_free(mblob);
        level_free(&lv);
        plat_log("game: не читается %s", msh_path);
        return -1;
    }

    if (g->level_ok) level_free(&g->level);
    if (g->island_ok) mesh_free(&g->island);
    g->level = lv;
    g->island = msh;
    g->level_ok = 1;
    g->island_ok = 1;
    g->level_index = index;

    const palette_t *pal = palette_find(&g->pals, g->level.palette);
    if (pal && pal != g->pal) { g->pal = pal; recolor_objects(g); }
    mesh_recolor(&g->island, g->pal, &g->light);

    player_init(&g->player, &g->level);
    const level_entity_t *entry = entry_id > 0 ? level_entity_by_id(&g->level, entry_id) : NULL;
    if (entry) {
        float y = walk_floor_at(&g->level, entry->x, entry->z);
        if (y > WALK_NO_FLOOR) {
            g->player.pos.x = entry->x;
            g->player.pos.z = entry->z;
            g->player.pos.y = y;
            g->player.yaw_deg = entry->yaw_deg;
        }
    }

    /* Водная гладь: клетки CELL_WATER сливаются в горизонтальные полосы, чтобы
     * прямоугольников было в разы меньше, чем клеток (пул кадра невелик). */
    g->water_count = 0;
    for (int cz = 0; cz < g->level.cells_z && g->water_count < FRAME_MAX_WATER; cz++) {
        int run_start = -1;
        for (int cx = 0; cx <= g->level.cells_x; cx++) {
            const level_cell_t *c = (cx < g->level.cells_x) ? level_cell(&g->level, cx, cz) : NULL;
            int is_water = (c && (c->flags & CELL_EXISTS) && (c->flags & CELL_WATER)) ? 1 : 0;
            if (is_water && run_start < 0) run_start = cx;
            if (!is_water && run_start >= 0) {
                float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
                level_cell_center(&g->level, run_start, cz, &x0, &z0);
                level_cell_center(&g->level, cx - 1, cz, &x1, &z1);
                float half = g->level.cell_size * 0.5f;
                if (g->water_count < FRAME_MAX_WATER) {
                    frame_water_t *w = &g->water[g->water_count++];
                    w->x0 = x0 - half; w->z0 = z0 - half;
                    w->x1 = x1 + half; w->z1 = z1 + half;
                }
                run_start = -1;
            }
        }
    }

    entities_init(&g->entities, &g->level, &g->world);
    entities_propagate(&g->entities);
    memset(&g->beam, 0, sizeof g->beam);
    /* Двери, открытые сохранённым рычагом, не должны выезжать на глазах у игрока. */
    memset(g->ent_vis, 0, sizeof g->ent_vis);
    for (int i = 0; i < g->entities.count && i < GAME_MAX_ENT_STATE; i++) {
        g->ent_vis[i] = g->entities.items[i].state ? 1.0f : 0.0f;
    }

    float target[3] = { g->player.pos.x, g->player.pos.y + PLAYER_EYE_H, g->player.pos.z };
    camera_init(&g->cam, g->level.cam_angle, g->level.cam_lock, target);
    /* Прибытие: кадр начинается шире и сходится к игровому масштабу — остров
     * успевает показать себя целиком, прежде чем камера возьмёт Око. */
    camera_arrive(&g->cam);
    g->level_frames = 0;
    particles_init(&g->particles, 0x51F0A17Du + (unsigned)index * 7919u);
    float dust[3] = { target[0], target[1] + 1.5f, target[2] };
    particles_set_ambient(&g->particles, dust, 8.5f, DUST_COUNT, 0xA0C8E8FFu);

    audio_set_region(g, g->pal->name);
    audio_set_region_ambient(g, g->pal->name);

    plat_log("game: остров \"%s\": %d треугольников, %d сущностей, %d связей",
             g->level.name, g->island.count / 3, g->entities.count, g->level.link_count);
    return 0;
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

    /* Направление НА солнце. По горизонтали совпадает с SHADOW_DIR из tools/levelc.py
     * (там печётся падающая тень), по высоте выше: тени должны быть длинными, а верхние
     * грани — светлыми. Ambient меньше, diffuse больше, чем «безопасные» 0,6/0,4:
     * грани, смотрящие в разные стороны, обязаны отличаться, иначе объём читается как
     * плоская заливка одного тона — ровно то, от чего кадр выглядит дёшево. */
    g->light.dir[0] = 0.62f; g->light.dir[1] = 0.80f; g->light.dir[2] = 0.34f;
    normalize3(g->light.dir);
    g->light.ambient = 0.44f;
    g->light.diffuse = 0.56f;
    g->light.sky_mix = 0.50f;   /* половина неосвещённого цвета — тон тени палитры */

    g->pal = palette_find(&g->pals, "hub");
    if (!g->pal) { plat_log("game: нет палитры hub"); return -1; }

    load_meshes(g);
    load_ghost_meshes(g);
    load_text_resources(g);

    tween_set(&g->desat, 0.0f);
    tween_set(&g->travel, 0.0f);
    g->travel_to = -1;
    audio_init(&g->audio, 0x7F4A7C15u);

    world_reset(&g->world);
    save_data_t sd;
    int has_save = (save_read_file(&sd) == 0);
    if (has_save) g->world = sd.world;
    screens_init(&g->screens, SCR_TITLE, has_save);
    g->title_yaw = 45.0f;

    int start_level = (has_save && sd.level_index < LVL_COUNT) ? (int)sd.level_index : LVL_HUB;
    if (load_level(g, start_level, 0) != 0 && load_level(g, LVL_HUB, 0) != 0) {
        plat_log("game: нет стартового уровня");
        return -1;
    }

    char *script = (char *)plat_read_file("autoplay.txt", &len);
    if (script) {
        int n = autoplay_parse(&g->ap, script);
        plat_log("game: autoplay %d событий", n);
        plat_free(script);
    }
    return 0;
}

/* Значение метрики по имени для проверок autoplay. NaN — имя неизвестно. */
static float metric_value(const game_t *g, const char *name) {
    const plat_stats_t *s = &g->stats;
    if (strcmp(name, "fps") == 0) return s->fps;
    if (strcmp(name, "frame_us") == 0) return (float)s->frame_us;
    if (strcmp(name, "cpu_us") == 0) return (float)s->cpu_us;
    if (strcmp(name, "gpu_us") == 0) return (float)s->gpu_us;
    if (strcmp(name, "tris") == 0) return (float)s->tris;
    if (strcmp(name, "draws") == 0) return (float)s->draws;
    if (strcmp(name, "heap_free") == 0) return (float)s->heap_free;
    if (strcmp(name, "heap_max") == 0) return (float)s->heap_max;
    if (strcmp(name, "frame") == 0) return (float)g->frame;
    if (strcmp(name, "lang") == 0) return (float)g->lang;
    if (strcmp(name, "font_ok") == 0) return (float)g->font_ok;
    if (strcmp(name, "level_ok") == 0) return (float)g->level_ok;
    if (strcmp(name, "strings_ok") == 0) return (float)g->strings_ok;
    if (strcmp(name, "px") == 0) return g->player.pos.x;
    if (strcmp(name, "py") == 0) return g->player.pos.y;
    if (strcmp(name, "pz") == 0) return g->player.pos.z;
    if (strcmp(name, "cam_angle") == 0) return (float)g->cam.angle;
    if (strcmp(name, "look") == 0) return (float)g->player.look_active;
    if (strcmp(name, "desat") == 0) return g->desat.value;
    if (strcmp(name, "eyes") == 0) return (float)g->world.eyes_opened;
    if (strcmp(name, "small_eyes") == 0) return (float)g->world.small_eyes;
    if (strcmp(name, "feathers") == 0) return (float)g->world.feathers;
    if (strcmp(name, "screen") == 0) return (float)g->screens.current;
    if (strcmp(name, "level") == 0) return (float)g->level_index;
    if (strcmp(name, "prompt") == 0) return (float)g->prompt_id;
    if (strcmp(name, "msg") == 0) return (float)(g->msg_frames > 0 ? g->msg_str : -1);
    if (strcmp(name, "beam") == 0) return (float)g->beam.count;
    if (strcmp(name, "receiver") == 0) return (float)g->beam.hit_receiver_id;
    if (strcmp(name, "ents") == 0) return (float)g->entities.count;
    /* "flagN" — флаг мира N (например flag30=1: малый глаз собран). */
    if (strncmp(name, "flag", 4) == 0 && name[4]) {
        int id = atoi(name + 4);
        if (id > 0 && id < WORLD_FLAG_COUNT) return (float)world_flag(&g->world, id);
    }
    /* "entN" — состояние сущности N (рычаг включён, дверь открыта, глаз открыт). */
    if (strncmp(name, "ent", 3) == 0 && name[3] >= '0' && name[3] <= '9') {
        const entity_t *e = entities_by_id_const(&g->entities, atoi(name + 3));
        if (e) return (float)e->state;
    }
    return 0.0f / 0.0f;
}

/* Проверка вида "<метрика><оператор><число>", операторы: >= <= = > <. */
static void run_assert(game_t *g, const char *expr) {
    char name[32] = {0};
    const char *op = NULL;
    size_t i = 0;
    while (expr[i] && (size_t)i + 1 < sizeof name &&
           expr[i] != '>' && expr[i] != '<' && expr[i] != '=' && expr[i] != '!') {
        name[i] = expr[i];
        i++;
    }
    name[i] = 0;
    op = expr + i;
    if (!*op) { plat_log("AUTOPLAY FAIL: не разобрал проверку '%s'", expr); g->assert_fail++; return; }

    int len_op = (op[1] == '=' && (op[0] == '>' || op[0] == '<' || op[0] == '!')) ? 2 : 1;
    float want = (float)atof(op + len_op);
    float got = metric_value(g, name);
    int ok;
    if (got != got) { /* NaN — неизвестная метрика */
        plat_log("AUTOPLAY FAIL: неизвестная метрика '%s'", name);
        g->assert_fail++;
        return;
    }
    if (len_op == 2 && op[0] == '>') ok = got >= want;
    else if (len_op == 2 && op[0] == '<') ok = got <= want;
    else if (len_op == 2 && op[0] == '!') ok = got != want;
    else if (op[0] == '>') ok = got > want;
    else if (op[0] == '<') ok = got < want;
    else ok = (got == want) || (got > want - 0.001f && got < want + 0.001f);

    if (ok) {
        g->assert_pass++;
        plat_log("AUTOPLAY OK: %s (кадр %d, значение %.3f)", expr, g->frame, (double)got);
    } else {
        g->assert_fail++;
        plat_log("AUTOPLAY FAIL: %s (кадр %d, значение %.3f)", expr, g->frame, (double)got);
    }
}

/* Сохранение текущего состояния: вызывается на событиях прогресса, не каждый кадр. */
static void autosave(game_t *g) {
    save_data_t sd;
    memset(&sd, 0, sizeof sd);
    sd.version = SAVE_VERSION;
    sd.lang = g->lang;
    sd.level_index = g->level_index;
    sd.px = g->player.pos.x;
    sd.py = g->player.pos.y;
    sd.pz = g->player.pos.z;
    sd.pyaw = g->player.yaw_deg;
    sd.cam_angle = g->cam.angle;
    sd.world = g->world;
    if (save_write_file(&sd) == 0) {
        g->screens.has_save = 1;
        plat_log("game: сохранено (остров %d, глаз %d)", g->level_index, (int)g->world.eyes_opened);
    }
}

/* Начинает переход на другой остров: занавес перехода отдельный от занавеса экранов,
 * чтобы пауза и меню поверх смены острова не спорили за одну шкалу. */
static void travel_begin(game_t *g, int level_index, int entry_id) {
    if (g->travel_phase != 0 || level_index < 0 || level_index >= LVL_COUNT) return;
    g->travel_to = level_index;
    g->travel_entry = entry_id;
    g->travel_phase = 1;
    tween_start(&g->travel, 1.0f, TRAVEL_FRAMES);
}

static void travel_tick(game_t *g) {
    if (g->travel_phase == 0) return;
    tween_update(&g->travel, ease_in_out_cubic);
    if (!tween_done(&g->travel)) return;
    if (g->travel_phase == 1) {
        if (g->travel_to >= 0 && load_level(g, g->travel_to, g->travel_entry) != 0) {
            plat_log("game: переход на уровень %d не удался", g->travel_to);
        }
        g->travel_to = -1;
        g->travel_phase = 2;
        tween_start(&g->travel, 0.0f, TRAVEL_FRAMES);
    } else {
        g->travel_phase = 0;
    }
}

/* Подсказка под тип сущности: игрок должен понимать действие до нажатия. */
static int prompt_str_for(int type) {
    switch (type) {
    case ENT_ECHO: return STR_PROMPT_TALK;
    case ENT_STONE_TEXT: return STR_PROMPT_READ;
    case ENT_SMALL_EYE:
    case ENT_FEATHER: return STR_PROMPT_TAKE;
    case ENT_MIRROR:
    case ENT_PRISM:
    case ENT_SEGMENT: return STR_PROMPT_TURN;
    case ENT_BLOCK: return STR_PROMPT_PUSH;
    case ENT_LEVER: return STR_PROMPT_PULL;
    default: return STR_PROMPT_USE;
    }
}

/* Короткая реплика внизу экрана: надпись на камне, слова отголоска, отказ механизма. */
static void show_msg(game_t *g, int str_id) {
    if (str_id < 0 || str_id >= STR_COUNT) return;
    g->msg_str = str_id;
    g->msg_frames = MSG_FRAMES;
}

/* Взаимодействие с тем, что рядом. Типы, которые entity.c намеренно не обрабатывает сам,
 * разбираются здесь: вентиль воды, панель памяти, сегмент, отголосок, надпись. */
static void do_interact(game_t *g) {
    if (!g->level_ok) return;
    float dirx = sinf(g->player.yaw_deg * M3_DEG2RAD);
    float dirz = cosf(g->player.yaw_deg * M3_DEG2RAD);
    int id = entities_interact(&g->entities, g->player.pos.x, g->player.pos.z, dirx, dirz,
                              INTERACT_REACH);
    if (!id) return;
    const entity_t *e = entities_by_id_const(&g->entities, id);
    if (!e || !e->def) return;

    /* Тон события: у каждого механизма свой голос из аккорда региона. */
    switch (e->def->type) {
    case ENT_LEVER: audio_play(&g->audio, SFX_LEVER); break;
    case ENT_MIRROR:
    case ENT_PRISM: audio_play(&g->audio, SFX_MIRROR); break;
    case ENT_BLOCK: audio_play(&g->audio, SFX_BLOCK); break;
    case ENT_SMALL_EYE: audio_play(&g->audio, SFX_EYE_SMALL); break;
    case ENT_FEATHER: audio_play(&g->audio, SFX_FEATHER); break;
    default: audio_play(&g->audio, SFX_INTERACT); break;
    }

    switch (e->def->type) {
    case ENT_WATER_VALVE:
        water_valve_use(&g->entities, id);
        break;
    case ENT_MEMORY_PANEL:
        /* Панель вводит следующее значение по порядку: подробный ввод — дело головоломки. */
        memory_input(&g->entities, id, (int)e->state + 1);
        break;
    case ENT_SEGMENT:
        segment_rotate(&g->entities, e->def->params[0], 1);
        break;
    case ENT_STONE_TEXT:
        if (e->def->name_str_id != 0xFFFFu) show_msg(g, (int)e->def->name_str_id);
        break;
    case ENT_SMALL_EYE:
        show_msg(g, STR_SMALL_EYE_FOUND);
        break;
    case ENT_FEATHER:
        show_msg(g, STR_FEATHER_FOUND);
        break;
    case ENT_ECHO: {
        const quest_def_t *q = quest_find(QUESTS, QUEST_COUNT, id);
        if (q) {
            quest_talk(&g->world, q);
            show_msg(g, quest_line_str(&g->world, q));
        } else if (e->def->name_str_id != 0xFFFFu) {
            show_msg(g, (int)e->def->name_str_id);
        }
        break;
    }
    default:
        break;
    }
    entities_propagate(&g->entities);
}

static void set_lang(game_t *g, int lang) {
    if (!g->strings_ok || lang < 0 || lang >= LANG_COUNT) return;
    g->lang = lang;
    i18n_use(&g->strings[lang]);
}

void game_tick(game_t *g, const input_t *in_real, const plat_stats_t *stats) {
    if (stats) g->stats = *stats;

    input_t in = *in_real;
    if (g->ap.active) {
        char shot[AP_NAME_LEN] = {0};
        int flags = autoplay_step(&g->ap, g->frame, &in, shot);
        if (flags & AP_FLAG_SHOT) snprintf(g->pending_shot, sizeof g->pending_shot, "%s", shot);
        if (flags & AP_FLAG_ASSERT) {
            for (int i = 0; i < g->ap.assert_count; i++) run_assert(g, g->ap.asserts[i]);
        }
        if (flags & AP_FLAG_LEVEL) {
            /* Отладочный переход: тест начинает сразу на нужном острове. */
            int index = -1;
            for (int i = 0; i < LVL_COUNT; i++) {
                const char *n = level_name((unsigned)i);
                if (n && strcmp(n, g->ap.level_name) == 0) { index = i; break; }
            }
            if (index < 0) plat_log("autoplay: нет уровня '%s'", g->ap.level_name);
            else if (load_level(g, index, g->ap.level_entry) == 0) {
                if (g->screens.current != SCR_GAME) screens_goto(&g->screens, SCR_GAME);
            }
        }
        if (flags & AP_FLAG_SET) {
            /* Отладка: выставить счётчик прогресса, не проходя игру. */
            int value = g->ap.set_value < 0 ? 0 : g->ap.set_value;
            if (strcmp(g->ap.set_name, "eyes") == 0) g->world.eyes_opened = (unsigned short)value;
            else if (strcmp(g->ap.set_name, "small_eyes") == 0) g->world.small_eyes = (unsigned short)value;
            else if (strcmp(g->ap.set_name, "feathers") == 0) g->world.feathers = (unsigned short)value;
            else plat_log("autoplay: неизвестный счётчик '%s'", g->ap.set_name);
        }
        if (flags & AP_FLAG_UI) g->hide_ui = g->ap.ui_visible ? 0 : 1;
        if (flags & AP_FLAG_QUIT) g->pending_quit = 1;
    }
    unsigned pressed = in.buttons & ~g->prev_buttons;
    g->prev_buttons = in.buttons;

    if (pressed & BTN_SELECT) g->show_debug = !g->show_debug;

    int act = screens_tick(&g->screens, pressed);
    if (g->screens.item_count > 0 && !screens_busy(&g->screens)) {
        if (pressed & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) audio_play(&g->audio, SFX_UI_MOVE);
    }
    if (act != ACT_NONE) audio_play(&g->audio, SFX_UI_OK);
    switch (act) {
    case ACT_NEW:
        world_reset(&g->world);
        g->ending_started = 0;
        load_level(g, LVL_HUB, 0);
        screens_goto(&g->screens, SCR_GAME);
        break;
    case ACT_CONTINUE: {
        save_data_t sd;
        if (save_read_file(&sd) == 0) {
            g->world = sd.world;
            set_lang(g, sd.lang);
            if (sd.level_index < LVL_COUNT) load_level(g, (int)sd.level_index, 0);
            /* Позиция ставится после загрузки: load_level ставит Око в точку спавна. */
            float y = walk_floor_at(&g->level, sd.px, sd.pz);
            if (y > WALK_NO_FLOOR) {
                g->player.pos.x = sd.px;
                g->player.pos.y = y;
                g->player.pos.z = sd.pz;
                g->player.yaw_deg = sd.pyaw;
            }
            g->ending_started = ((int)g->world.eyes_opened >= EYES_TOTAL);
        }
        screens_goto(&g->screens, SCR_GAME);
        break;
    }
    case ACT_ENDING_A:
    case ACT_ENDING_B: {
        int variant = (act == ACT_ENDING_B) ? 1 : 0;
        g->screens.ending_variant = variant;
        world_set_flag(&g->world, variant ? WFLAG_ENDING_SEEN_B : WFLAG_ENDING_SEEN_A, 1);
        /* Финал меняет мир, а не только текст: разбуженный Аргус приводит рассвет,
         * отпущенный — серость. Цвет живёт в палитре, поэтому достаточно повторного
         * резолва (TECH.md §2.4): ни одного ассета не трогаем. */
        const palette_t *end_pal = palette_find(&g->pals, variant ? "gray" : "dawn");
        if (end_pal) {
            g->pal = end_pal;
            recolor_objects(g);
            if (g->island_ok) mesh_recolor(&g->island, g->pal, &g->light);
        }
        autosave(g);
        screens_goto(&g->screens, SCR_ENDING);
        break;
    }
    case ACT_LANG: set_lang(g, (g->lang + 1) % LANG_COUNT); break;
    case ACT_QUIT: g->pending_quit = 1; break;
    case ACT_RESUME: screens_goto(&g->screens, SCR_GAME); break;
    case ACT_TO_TITLE:
        /* Из титров возвращаемся к обычному виду мира: палитра уровня и его геометрия. */
        if (g->level_ok) {
            const palette_t *pal = palette_find(&g->pals, g->level.palette);
            if (pal && pal != g->pal) {
                g->pal = pal;
                recolor_objects(g);
                if (g->island_ok) mesh_recolor(&g->island, g->pal, &g->light);
            }
        }
        screens_goto(&g->screens, SCR_TITLE);
        break;
    case ACT_ENDING_DONE: screens_goto(&g->screens, SCR_CREDITS); break;
    default: break;
    }

    int playing = (g->screens.current == SCR_GAME) && !screens_busy(&g->screens) && g->travel_phase == 0;
    if (playing) {
        if (pressed & BTN_L) camera_rotate(&g->cam, -1);
        if (pressed & BTN_R) camera_rotate(&g->cam, 1);
        if (pressed & BTN_SQUARE) set_lang(g, (g->lang + 1) % LANG_COUNT);
        if (pressed & BTN_START) screens_goto(&g->screens, SCR_PAUSE);
        if (g->level_ok) player_tick(&g->player, &g->level, &in, g->cam.yaw.value);
        else g->player.look_active = (in.buttons & BTN_CIRCLE) ? 1 : 0;
        if ((pressed & BTN_CROSS) && g->entities.busy_frames <= 0) do_interact(g);

        /* Портал: мост уводит на соседний остров. Портал «сам в себя» — это ещё не
         * собранный регион: игра честно говорит, что ход закрыт, вместо перезагрузки. */
        if (g->level_ok) {
            const level_portal_t *p = level_portal_at(&g->level, g->player.pos.x, g->player.pos.z);
            if (p) {
                int target = (int)p->target_level;
                if (target == g->level_index && p->target_entry == 0) {
                    if (g->msg_frames <= 0) { show_msg(g, STR_GATE_LOCKED); audio_play(&g->audio, SFX_DENY); }
                } else if (target == LVL_BONUS && (int)g->world.feathers < BONUS_FEATHERS) {
                    /* Бонусный остров — награда за просьбы отголосков, а не просто ещё
                     * один мост: без перьев ход закрыт, и игра говорит об этом прямо. */
                    if (g->msg_frames <= 0) { show_msg(g, STR_BONUS_LOCKED); audio_play(&g->audio, SFX_DENY); }
                } else {
                    travel_begin(g, target, (int)p->target_entry);
                }
            }
        }
    } else {
        g->player.look_active = 0;
        g->player.speed = 0.0f;
    }

    /* Вне игры кадр отъезжает: заставка, финал и титры показывают остров целиком. */
    camera_set_wide(&g->cam, g->screens.current != SCR_GAME && g->screens.current != SCR_PAUSE);

    /* Режим взгляда: наезд камеры и выцветание мира. */
    camera_set_look(&g->cam, g->player.look_active);
    float want_desat = g->player.look_active ? 1.0f : 0.0f;
    if (g->desat.to != want_desat) tween_start(&g->desat, want_desat, DESAT_FRAMES);
    tween_update(&g->desat, ease_out_cubic);

    if (g->screens.current == SCR_TITLE) {
        /* Заставка: медленный облёт центра острова — кадр живёт сам по себе. */
        g->title_yaw += TITLE_ORBIT * DT;
        if (g->title_yaw > 360.0f) g->title_yaw -= 360.0f;
        tween_set(&g->cam.yaw, g->title_yaw);
        /* Остров уводится вправо: левую треть кадра занимает плашка с названием.
         * Смещение считается в экранных осях камеры, поэтому при облёте композиция
         * не разъезжается. */
        frame_cam_t fc;
        camera_fill(&g->cam, &fc);
        float fx = 0.0f, fz = 0.0f;
        cam_forward_xz(&fc, &fx, &fz);
        float rx = -fz, rz = fx;   /* вектор «вправо по экрану» в мировых координатах */
        float center[3] = { -rx * TITLE_SHIFT, 2.0f, -rz * TITLE_SHIFT };
        camera_update(&g->cam, center);
    } else {
        float target[3] = { g->player.pos.x, g->player.pos.y + PLAYER_EYE_H, g->player.pos.z };
        camera_update(&g->cam, target);
    }

    /* Сущности: анимации, спящие объекты (двигаются только вне наблюдения), плиты, луч. */
    if (g->level_ok) {
        frame_cam_t fc;
        camera_fill(&g->cam, &fc);
        int eyes_before = (int)g->world.eyes_opened;
        entities_tick(&g->entities, &fc, g->player.look_active);
        plates_update(&g->entities, g->player.pos.x, g->player.pos.z);
        beam_trace(&g->entities, &g->beam);
        entities_propagate(&g->entities);

        /* Открытие большого глаза — событие: сообщение, искры и автосохранение. */
        if ((int)g->world.eyes_opened > eyes_before) {
            show_msg(g, STR_EYE_OPENED);
            const entity_t *eye = entities_by_id_const(&g->entities, g->entities.last_event);
            if (eye) {
                float pos[3] = { eye->x, eye->y + 0.6f, eye->z };
                particles_emit_burst(&g->particles, pos, 14, g->pal->slots[SLOT_GLOW], 1.6f);
            }
            audio_play(&g->audio, SFX_EYE_BIG);
            autosave(g);
        }

        /* Подсказка «можно нажать крест»: ближайшая сущность, с которой есть что делать. */
        g->prompt_id = 0;
        g->prompt_type = 0;
        float best = INTERACT_REACH * INTERACT_REACH;
        for (int i = 0; i < g->entities.count; i++) {
            const entity_t *e = &g->entities.items[i];
            if (!e->active || !e->def || !entity_can_interact((int)e->def->type)) continue;
            float dx = e->x - g->player.pos.x, dz = e->z - g->player.pos.z;
            float d2 = dx * dx + dz * dz;
            if (d2 < best) { best = d2; g->prompt_id = e->def->id; g->prompt_type = (int)e->def->type; }
        }

        /* Все большие глаза открыты — финальный выбор (GDD §1.1). */
        if (!g->ending_started && (int)g->world.eyes_opened >= EYES_TOTAL &&
            g->screens.current == SCR_GAME && !screens_busy(&g->screens)) {
            g->ending_started = 1;
            screens_goto(&g->screens, SCR_CHOICE);
        }

        /* Визуальное состояние дверей и панелей догоняет логическое (как цель камеры). */
        for (int i = 0; i < g->entities.count && i < GAME_MAX_ENT_STATE; i++) {
            float want = g->entities.items[i].state ? 1.0f : 0.0f;
            g->ent_vis[i] += (want - g->ent_vis[i]) * ENT_VIS_RATE;
        }
    }
    travel_tick(g);
    if (g->msg_frames > 0) g->msg_frames--;
    g->level_frames++;

    /* Шаги: тон через каждые 0,95 единицы пути — ровно, без привязки к частоте кадров. */
    if (playing && g->player.speed > 0.05f) {
        g->step_dist += g->player.speed * DT;
        if (g->step_dist >= 0.95f) { g->step_dist = 0.0f; audio_play(&g->audio, SFX_STEP); }
    } else {
        g->step_dist = 0.0f;
    }

    /* Прошлая эмбиент-петля отпускается, когда микшер точно ушёл со старого буфера. */
    if (g->ambient_free_in > 0 && --g->ambient_free_in == 0 && g->ambient_old) {
        plat_free(g->ambient_old);
        g->ambient_old = NULL;
    }

    particles_tick(&g->particles);
    g->frame++;
}

/* Пока твин идёт, рисуем догоняющее значение; когда он закончился — логическое.
 * Логика применяет позицию сразу (puzzle_mech.c), твин живёт только ради картинки. */
static float anim_or(const entity_t *e, float logical) {
    return tween_done(&e->anim) ? logical : e->anim.value;
}

/* Радиус контактной тени под сущностью в мировых единицах; 0 — тени нет
 * (плита, дверь и прочее, что само лежит на полу). */
static float entity_shadow_radius(int type) {
    switch (type) {
    case ENT_ECHO:
    case ENT_SLEEPER: return 0.42f;
    case ENT_BLOCK:
    case ENT_FLOAT_BLOCK: return 0.52f;
    case ENT_MIRROR:
    case ENT_PRISM:
    case ENT_RECEIVER:
    case ENT_EMITTER:
    case ENT_LEVER:
    case ENT_MEMORY_PANEL:
    case ENT_WATER_VALVE: return 0.34f;
    case ENT_STONE_TEXT: return 0.40f;
    case ENT_PLINTH: return 0.95f;
    case ENT_BIG_EYE: return 0.70f;
    case ENT_SMALL_EYE:
    case ENT_FEATHER: return 0.26f;
    default: return 0.0f;
    }
}

/* Свечение сущности: размер и яркость 0..1. 0 — не светится. */
static float entity_glow_size(const entity_t *e, float *bright) {
    int type = e->def ? (int)e->def->type : 0;
    switch (type) {
    case ENT_SMALL_EYE: *bright = 0.70f; return 0.75f;
    case ENT_FEATHER: *bright = 0.65f; return 0.6f;
    case ENT_PEACOCK_TAIL: *bright = 0.60f; return 2.2f;
    case ENT_BIG_EYE:
        /* Закрытый глаз только тлеет — открытый горит: это и есть индикатор прогресса. */
        *bright = e->state ? 0.95f : 0.22f;
        return e->state ? 1.9f : 1.2f;
    case ENT_EMITTER: *bright = 0.85f; return 0.7f;
    case ENT_RECEIVER: *bright = e->inputs ? 0.95f : 0.18f; return 0.7f;
    case ENT_MEMORY_PANEL: *bright = 0.3f + 0.7f * anim_or(e, 0.0f); return 0.6f;
    case ENT_DOOR: *bright = 0.0f; return 0.0f;
    default: return 0.0f;
    }
}

/* Веер перьев на хвосте. Раскрывается по мере прогресса: большие глаза дают по перу
 * с запасом, малые — по одному на четыре. Перья кладутся от краёв к центру, поэтому
 * веер растёт симметрично, а не отращивает одну сторону. */
static void build_tail_feathers(game_t *g, frame_t *f, float x, float y, float z,
                                float yaw, float hover) {
    if (!g->object_ok[MESH_PEACOCK_FEATHER]) return;
    int n = (int)g->world.eyes_opened * 2 + (int)(g->world.small_eyes / 4);
    if (n > TAIL_FEATHERS_MAX) n = TAIL_FEATHERS_MAX;
    if (n <= 0) return;
    for (int k = 0; k < n; k++) {
        /* Позиция пера в веере: 0,5 — середина, края — 0 и 1. */
        float t = (n == 1) ? 0.5f : (float)k / (float)(n - 1);
        float ang = yaw + (t - 0.5f) * TAIL_FAN_DEG;
        float lean = TAIL_TILT_DEG * (0.6f + 0.4f * fabsf(t - 0.5f) * 2.0f);
        frame_mesh_t *m = frame_push_mesh(f, &g->objects[MESH_PEACOCK_FEATHER],
                                          x, y + 0.15f + hover * 0.04f, z, ang);
        if (!m) return;
        m->pitch_deg = lean;
        m->scale = 0.85f + 0.15f * (1.0f - fabsf(t - 0.5f) * 2.0f);
    }
}

/* Сущности рисуются по живому состоянию (entities_t), а не по данным уровня:
 * иначе блок стоял бы на месте, дверь не открывалась, а собранный глаз не исчезал. */
static void build_entities(game_t *g, frame_t *f) {
    const entities_t *es = &g->entities;
    /* Счётчику не доверяем: пул фиксирован, а испорченные данные не должны выводить
     * индекс за массив (CLAUDE.md, п.12). */
    int count = es->count < ENT_RUNTIME_MAX ? es->count : ENT_RUNTIME_MAX;
    for (int i = 0; i < count; i++) {
        const entity_t *e = &es->items[i];
        if (!e->def || !e->active) continue;
        int type = (int)e->def->type;

        float x = e->x, y = e->y, z = e->z, yaw = e->yaw;
        float pitch = 0.0f, scale = 1.0f;
        float hover = sinf(e->phase * M3_DEG2RAD);
        float vis = (i < GAME_MAX_ENT_STATE) ? g->ent_vis[i] : (e->state ? 1.0f : 0.0f);

        switch (type) {
        case ENT_BLOCK: {
            /* anim — координата вдоль оси толчка; ось задаёт yaw (0 → +x, 90 → +z). */
            int q = ((int)(yaw / 90.0f + 0.5f)) & 3;
            if (!tween_done(&e->anim)) { if (q == 0 || q == 2) x = e->anim.value; else z = e->anim.value; }
            yaw = 0.0f; /* куб; поворот по направлению толчка выглядел бы дрожанием */
            break;
        }
        case ENT_FLOAT_BLOCK:
            y = anim_or(e, e->y);
            break;
        case ENT_LEVER:
            pitch = -26.0f + 52.0f * anim_or(e, (float)e->state);
            break;
        case ENT_PLATE:
            y -= 0.07f * anim_or(e, (float)e->state);
            break;
        case ENT_SEGMENT:
            yaw = tween_done(&e->anim) ? e->yaw : e->def->yaw_deg + e->anim.value;
            break;
        case ENT_WATER_VALVE:
            yaw = e->def->yaw_deg + 60.0f * anim_or(e, (float)es->water_steps);
            break;
        case ENT_MEMORY_PANEL:
            scale = 1.0f + 0.05f * anim_or(e, 0.0f);
            break;
        case ENT_DOOR:
            y -= DOOR_DROP * vis; /* открытая дверь уезжает в пол */
            if (vis > 0.985f) continue;
            break;
        case ENT_ECHO:
        case ENT_SLEEPER:
            y += 0.35f + hover * 0.06f;
            yaw += e->phase;
            break;
        case ENT_SMALL_EYE:
        case ENT_BIG_EYE:
            scale = 1.0f + hover * 0.04f;
            break;
        default:
            break;
        }

        /* Тень под объектом: у парящих (отголосок, глаз) она меньше и слабее —
         * так видно, что предмет висит, а не стоит. */
        float sh_r = entity_shadow_radius(type);
        if (sh_r > 0.0f) {
            int floating = (type == ENT_ECHO || type == ENT_SLEEPER ||
                            type == ENT_SMALL_EYE || type == ENT_BIG_EYE || type == ENT_FEATHER);
            frame_push_shadow(f, x, e->y, z, floating ? sh_r * 0.8f : sh_r,
                              (unsigned char)(floating ? 60 : 96));
        }

        int mi = mesh_for_entity(type);
        if (mi >= 0 && mi < MESH_COUNT && g->object_ok[mi]) {
            frame_mesh_t *m = frame_push_mesh(f, &g->objects[mi], x, y, z, yaw);
            if (m) { m->scale = scale; m->pitch_deg = pitch; }
        }

        /* Павлиний хвост — счётчик прогресса в мире, а не в интерфейсе (GDD §1.1):
         * каждое перо соответствует открытому глазу, малые глаза добавляют пух. */
        if (type == ENT_PEACOCK_TAIL) build_tail_feathers(g, f, x, y, z, yaw, hover);

        float bright = 0.0f;
        float size = entity_glow_size(e, &bright);
        if (size > 0.0f && bright > 0.0f) {
            /* Свет рисуется двумя билбордами: широкий тёплый ореол цветом акцента
             * и маленькое яркое ядро. Один большой белый круг на аддитиве просто
             * выжигает кадр в белое пятно — именно так «дёшево» и выглядит. */
            float pulse = 0.80f + 0.20f * hover;
            float pos[3] = { x, y + 0.35f, z };
            unsigned halo_a = (unsigned)(bright * pulse * 96.0f);
            unsigned core_a = (unsigned)(bright * pulse * 168.0f);
            if (halo_a > 255u) halo_a = 255u;
            if (core_a > 255u) core_a = 255u;
            frame_push_sprite(f, SPRITE_GLOW, pos, size,
                              (halo_a << 24) | (g->pal->slots[SLOT_ACCENT] & 0x00FFFFFFu));
            frame_push_sprite(f, SPRITE_SPARK, pos, size * 0.42f,
                              (core_a << 24) | (g->pal->slots[SLOT_GLOW] & 0x00FFFFFFu));
        }
    }
}

/* Луч — цепочка аддитивных искр вдоль отрезков трассировки: отдельной геометрии нет,
 * бюджет спрайтов ограничен, чтобы пылинки и свечения не вытеснялись (CLAUDE.md, п.17). */
static void build_beam(game_t *g, frame_t *f) {
    const beam_t *b = &g->beam;
    if (b->count <= 0) return;
    unsigned rgb = g->pal->slots[SLOT_GLOW] & 0x00FFFFFFu;
    int budget = BEAM_SPRITES_MAX;
    for (int i = 0; i < b->count && budget > 0; i++) {
        const beam_seg_t *seg = &b->segs[i];
        float dx = seg->x1 - seg->x0, dz = seg->z1 - seg->z0;
        float len = sqrtf(dx * dx + dz * dz);
        int steps = (int)(len / BEAM_STEP);
        if (steps < 1) steps = 1;
        for (int k = 0; k <= steps && budget > 0; k++) {
            float t = (float)k / (float)steps;
            float pos[3] = { seg->x0 + dx * t, seg->y + 0.45f, seg->z0 + dz * t };
            /* Бегущая волна вдоль луча: фаза зависит и от кадра, и от точки. */
            float wave = sinf((float)(g->frame * 7 + k * 52) * M3_DEG2RAD);
            unsigned alpha = (unsigned)(120.0f + 45.0f * wave);
            frame_push_sprite(f, SPRITE_SPARK, pos, 0.34f, (alpha << 24) | rgb);
            budget--;
        }
    }
}

/* Столб света над порталом: выход с острова иначе ничем не отмечен, и игрок
 * ищет его наугад. Три билборда друг над другом с падающей яркостью — дёшево
 * и читается издалека. */
static void build_portal_marks(game_t *g, frame_t *f) {
    const level_t *l = &g->level;
    unsigned rgb = g->pal->slots[SLOT_GLOW] & 0x00FFFFFFu;
    int count = l->portal_count < PORTAL_MARKS_MAX ? l->portal_count : PORTAL_MARKS_MAX;
    for (int i = 0; i < count; i++) {
        const level_portal_t *p = &l->portals[i];
        int cx = (int)p->cx + (p->w ? p->w / 2 : 0);
        int cz = (int)p->cz + (p->h ? p->h / 2 : 0);
        float x = 0.0f, z = 0.0f;
        level_cell_center(l, cx, cz, &x, &z);
        float top = level_cell_top(l, cx, cz);
        if (top < -1.0e8f) continue;
        for (int k = 0; k < 3; k++) {
            float phase = (float)(g->frame * 3 + k * 70 + i * 40) * M3_DEG2RAD;
            float pos[3] = { x, top + 0.35f + (float)k * 0.42f, z };
            unsigned alpha = (unsigned)((70.0f - (float)k * 16.0f) * (0.75f + 0.25f * sinf(phase)));
            frame_push_sprite(f, SPRITE_GLOW, pos, 0.85f - (float)k * 0.14f, (alpha << 24) | rgb);
        }
    }
}

static void build_world(game_t *g, frame_t *f) {
    if (g->island_ok) frame_push_mesh(f, &g->island, 0.0f, 0.0f, 0.0f, 0.0f);
    if (g->level_ok) {
        build_entities(g, f);
        build_portal_marks(g, f);
        build_beam(g, f);
    }

    /* Тень Око: слегка сжимается на бегу — глаз цепляется за неё и видит высоту. */
    if (g->level_ok) {
        float squash = 1.0f - 0.12f * (g->player.speed / 3.4f);
        frame_push_shadow(f, g->player.pos.x, g->player.pos.y, g->player.pos.z,
                          0.34f * squash, 118u);
    }

    if (g->object_ok[MESH_EYE_BODY] && g->object_ok[MESH_EYE_HEAD] && g->object_ok[MESH_EYE_IRIS]) {
        player_build(&g->player, f, &g->objects[MESH_EYE_BODY], &g->objects[MESH_EYE_HEAD],
                     &g->objects[MESH_EYE_IRIS]);
        /* Те же позиции — в список силуэтов: Око не теряется за террасами. */
        int base = f->mesh_count - 3;
        if (base >= 0) {
            for (int i = 0; i < 3; i++) {
                if (!g->ghost_ok[i]) continue;
                const frame_mesh_t *m = &f->meshes[base + i];
                frame_mesh_t *gh = frame_push_ghost(f, &g->ghost[i], m->pos[0], m->pos[1], m->pos[2],
                                                    m->yaw_deg);
                if (gh) gh->scale = m->scale;
            }
        }
    }
    particles_build(&g->particles, f);
}

/* Затухание по кадрам: 1 до hold, дальше плавно к нулю за fade кадров. */
static float fade_out(int frames, int hold, int fade) {
    if (frames <= hold) return 1.0f;
    if (frames >= hold + fade) return 0.0f;
    float t = (float)(frames - hold) / (float)fade;
    return 1.0f - ease_in_out_cubic(t);
}

static void build_hud(game_t *g, frame_t *f) {
    if (!g->font_ok || !g->strings_ok) return;
    unsigned accent = g->pal->slots[SLOT_ACCENT];
    unsigned dim = g->pal->slots[SLOT_TOP_ALT];

    /* Подложки под текст: сверху и снизу узкие полосы, растворяющиеся в сцену.
     * Цвет — тон тени палитры, поэтому интерфейс принадлежит миру, а не наклеен. */
    unsigned shade = g->pal->slots[SLOT_SHADOW] & 0x00FFFFFFu;
    frame_push_panel(f, 0, 0, SCR_W, 46, (150u << 24) | shade, shade);
    frame_push_panel(f, 0, SCR_H - 64, SCR_W, 64, shade, (170u << 24) | shade);

    /* Подпись острова — карточка на входе: держится несколько секунд и гаснет.
     * Постоянная надпись поверх сцены — первый признак отладочного интерфейса. */
    float caption_a = fade_out(g->level_frames, CAPTION_HOLD, HUD_FADE);
    if (caption_a > 0.0f && g->level_ok && g->level.name_str_id != 0xFFFFFFFFu) {
        unsigned a = (unsigned)(caption_a * 255.0f);
        frame_push_text_shadow(f, FONT_TITLE, TEXT_LEFT, 20, 40, (a << 24) | (accent & 0x00FFFFFFu),
                               "%s", i18n_str((int)g->level.name_str_id));
        frame_push_panel(f, 20, 48, 96, 2, ((unsigned)(caption_a * 200.0f) << 24) | (accent & 0x00FFFFFFu),
                         ((unsigned)(caption_a * 40.0f) << 24) | (accent & 0x00FFFFFFu));
    }

    /* Подсказки по управлению живут первую минуту на острове, потом уходят. */
    float hint_a = fade_out(g->level_frames, HINT_HOLD, HUD_FADE);
    if (hint_a > 0.0f) {
        unsigned a = (unsigned)(hint_a * 255.0f);
        unsigned c = (a << 24) | (dim & 0x00FFFFFFu);
        frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 20, SCR_H - 14, c, "%s", STR(STR_HINT_CAMERA));
        frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 20, SCR_H - 30, c, "%s", STR(STR_HINT_OBSERVE));
    }
    /* Язык и счёт глаз — в правом нижнем углу: наверху их перекрывала бы карточка
     * с названием острова, а внизу справа пусто всегда. */
    frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 20, SCR_H - 14, dim,
                           "%s", g->lang == LANG_RU ? "RU" : "EN");
    /* Формат счёта берётся из строки (STR_EYE_COUNT_OF = «Глаза: %d из %d»): число
     * и порядок спецификаторов задаёт assets/strings.csv, а не код. */
    frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 20, SCR_H - 30, accent,
                           i18n_str(STR_EYE_COUNT_OF), (int)g->world.eyes_opened, EYES_TOTAL);

    /* Реплика важнее подсказки: она появляется в ответ на действие игрока. */
    if (g->msg_frames > 0 && g->msg_str >= 0 && g->msg_str < STR_COUNT) {
        unsigned a = 255u;
        if (g->msg_frames < 30) a = (unsigned)(g->msg_frames * 255 / 30);
        unsigned col = (a << 24) | (g->pal->slots[SLOT_TOP] & 0x00FFFFFFu);
        frame_push_text_shadow(f, FONT_BODY, TEXT_CENTER, SCR_W / 2, SCR_H - 52, col,
                               "%s", i18n_str(g->msg_str));
    } else if (g->prompt_id) {
        frame_push_text_shadow(f, FONT_BODY, TEXT_CENTER, SCR_W / 2, SCR_H - 52, accent,
                               "%s", i18n_str(prompt_str_for(g->prompt_type)));
    }

    if (g->show_debug) {
        const plat_stats_t *s = &g->stats;
        int y = 110, step = 15;
        unsigned c = 0xFFE8E8E8u;
        frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 10, y, c,
                        "fps %.1f (%u мкс)", (double)s->fps, s->frame_us);
        frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 10, y += step, c,
                        "cpu %u / gpu %u мкс", s->cpu_us, s->gpu_us);
        frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 10, y += step, c,
                        "тр. %u, вызовов %u", s->tris, s->draws);
        frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 10, y += step, c,
                        "куча %u КБ (блок %u КБ)", s->heap_free >> 10, s->heap_max >> 10);
        frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 10, y += step, c,
                        "кадр %d, ракурс %d", g->frame, g->cam.angle);
        frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 10, y += step, c,
                        "Око %.1f %.1f %.1f", (double)g->player.pos.x, (double)g->player.pos.y,
                        (double)g->player.pos.z);
    }
}

void game_build_frame(game_t *g, frame_t *f) {
    frame_reset(f);
    f->env.sky_top = g->pal->sky_top;
    f->env.sky_bottom = g->pal->sky_bottom;
    f->env.fog_color = g->pal->sky_bottom;
    f->env.shadow_color = g->pal->slots[SLOT_SHADOW];
    /* Гладь стоит на текущем уровне воды и чуть дышит: ровная плоскость выглядит
     * стеклом, а не водой. Амплитуда меньше миллиметра в масштабе клетки. */
    f->water_color = (170u << 24) | (g->pal->slots[SLOT_WATER] & 0x00FFFFFFu);
    f->water_y = (float)g->entities.water_steps * g->level.step_y
                 + 0.02f * sinf((float)g->frame * 1.4f * M3_DEG2RAD);
    for (int i = 0; i < g->water_count; i++) {
        frame_push_water(f, g->water[i].x0, g->water[i].z0, g->water[i].x1, g->water[i].z1);
    }
    f->env.fog_near = g->cam.dist + g->pal->fog_near;
    f->env.fog_far = g->cam.dist + g->pal->fog_far;
    f->env.desat = g->desat.value;
    /* Виньетка — постоянная часть кадра; в режиме взгляда мир сужается ещё немного. */
    f->env.vignette = VIGNETTE_BASE + 0.18f * g->desat.value;
    f->env.time = (float)g->frame * DT;

    float curtain = screens_curtain(&g->screens);
    if (g->travel.value > curtain) curtain = g->travel.value;
    f->env.curtain = curtain;
    camera_fill(&g->cam, &f->cam);
    build_world(g, f);
    if (g->hide_ui) { /* чистый кадр: ни HUD, ни экранов */ }
    else if (g->screens.current == SCR_GAME) build_hud(g, f);
    else if (g->screens.current == SCR_PAUSE || g->screens.current == SCR_CHOICE) {
        f->env.desat = 0.85f; /* сцена уходит на задний план под меню */
    }
    if (g->font_ok && g->strings_ok && !g->hide_ui) {
        screens_build(&g->screens, f, g->pal, g->lang, (int)g->world.eyes_opened, EYES_TOTAL);
    }

    if (g->pending_shot[0]) {
        snprintf(f->shot_name, sizeof f->shot_name, "%s", g->pending_shot);
        g->pending_shot[0] = 0;
    }
    f->quit = g->pending_quit;
}

void game_shutdown(game_t *g) {
    /* Звук останавливает платформа до этого вызова (main.c), поэтому буферы отпускаем. */
    audio_set_ambient(&g->audio, NULL, 0, 0.0f);
    if (g->ambient_pcm) { plat_free(g->ambient_pcm); g->ambient_pcm = NULL; }
    if (g->ambient_old) { plat_free(g->ambient_old); g->ambient_old = NULL; }
    if (g->font_ok) font_free(&g->font);
    for (int i = 0; i < LANG_COUNT; i++) i18n_free(&g->strings[i]);
    for (int i = 0; i < MESH_COUNT; i++) if (g->object_ok[i]) mesh_free(&g->objects[i]);
    for (int i = 0; i < 3; i++) if (g->ghost_ok[i]) mesh_free(&g->ghost[i]);
    if (g->island_ok) mesh_free(&g->island);
    if (g->level_ok) level_free(&g->level);
    palette_set_free(&g->pals);
}
