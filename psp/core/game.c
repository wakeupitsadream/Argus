#include "game.h"
#include "platform.h"
#include "strings_ids.h"
#include "screens.h"
#include "entity_types.h"
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
#define EYES_TOTAL 4        /* больших глаз в игре */
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

/* Светятся ли глаза и перья — для аддитивных билбордов. */
static int entity_glow(int type, float *size) {
    switch (type) {
    case ENT_SMALL_EYE: *size = 0.9f; return 1;
    case ENT_BIG_EYE: *size = 2.2f; return 1;
    case ENT_FEATHER: *size = 0.7f; return 1;
    case ENT_RECEIVER: *size = 0.8f; return 1;
    case ENT_PEACOCK_TAIL: *size = 3.0f; return 1;
    default: return 0;
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

int game_init(game_t *g) {
    memset(g, 0, sizeof *g);
    size_t len = 0;
    void *blob = plat_read_file("data/palettes.pal", &len);
    if (palette_set_load(&g->pals, blob, len) != 0) {
        plat_log("game: не удалось загрузить data/palettes.pal");
        if (blob) plat_free(blob);
        return -1;
    }

    g->light.dir[0] = 0.45f; g->light.dir[1] = 1.0f; g->light.dir[2] = 0.3f;
    normalize3(g->light.dir);
    g->light.ambient = 0.58f;
    g->light.diffuse = 0.42f;

    /* Уровень: данные и геометрия. Палитра берётся из уровня. */
    blob = plat_read_file("data/hub.lvl", &len);
    if (blob && level_load(&g->level, blob, len) == 0) {
        g->level_ok = 1;
        g->pal = palette_find(&g->pals, g->level.palette);
    } else {
        if (blob) plat_free(blob);
        plat_log("game: нет data/hub.lvl — только геометрия");
    }
    if (!g->pal) g->pal = palette_find(&g->pals, "hub");
    if (!g->pal) { plat_log("game: нет палитры"); return -1; }

    blob = plat_read_file("data/hub.msh", &len);
    if (blob && mesh_load(&g->island, blob, len) == 0) {
        mesh_recolor(&g->island, g->pal, &g->light);
        g->island_ok = 1;
        plat_log("game: остров %d треугольников", g->island.count / 3);
    } else {
        if (blob) plat_free(blob);
        plat_log("game: не удалось загрузить data/hub.msh");
        return -1;
    }

    load_meshes(g);
    load_ghost_meshes(g);
    load_text_resources(g);

    if (g->level_ok) {
        player_init(&g->player, &g->level);
    } else {
        memset(&g->player, 0, sizeof g->player);
    }
    float target[3] = { g->player.pos.x, g->player.pos.y + PLAYER_EYE_H, g->player.pos.z };
    camera_init(&g->cam, g->level_ok ? g->level.cam_angle : 0, g->level_ok ? g->level.cam_lock : 0, target);
    tween_set(&g->desat, 0.0f);

    particles_init(&g->particles, 0x51F0A17Du);
    float dust_center[3] = { target[0], target[1] + 1.5f, target[2] };
    particles_set_ambient(&g->particles, dust_center, 7.5f, 34, 0x88FFF0C4u);

    world_reset(&g->world);
    save_data_t sd;
    int has_save = (save_read_file(&sd) == 0);
    if (has_save) g->world = sd.world;
    screens_init(&g->screens, SCR_TITLE, has_save);
    g->title_yaw = 45.0f;

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
        if (flags & AP_FLAG_QUIT) g->pending_quit = 1;
    }
    unsigned pressed = in.buttons & ~g->prev_buttons;
    g->prev_buttons = in.buttons;

    if (pressed & BTN_SELECT) g->show_debug = !g->show_debug;

    int act = screens_tick(&g->screens, pressed);
    switch (act) {
    case ACT_NEW:
        world_reset(&g->world);
        if (g->level_ok) player_init(&g->player, &g->level);
        screens_goto(&g->screens, SCR_GAME);
        break;
    case ACT_CONTINUE: {
        save_data_t sd;
        if (save_read_file(&sd) == 0) {
            g->world = sd.world;
            set_lang(g, sd.lang);
            g->player.pos.x = sd.px;
            g->player.pos.y = sd.py;
            g->player.pos.z = sd.pz;
            g->player.yaw_deg = sd.pyaw;
        }
        screens_goto(&g->screens, SCR_GAME);
        break;
    }
    case ACT_LANG: set_lang(g, (g->lang + 1) % LANG_COUNT); break;
    case ACT_QUIT: g->pending_quit = 1; break;
    case ACT_RESUME: screens_goto(&g->screens, SCR_GAME); break;
    case ACT_TO_TITLE: screens_goto(&g->screens, SCR_TITLE); break;
    case ACT_ENDING_DONE: screens_goto(&g->screens, SCR_CREDITS); break;
    default: break;
    }

    int playing = (g->screens.current == SCR_GAME) && !screens_busy(&g->screens);
    if (playing) {
        if (pressed & BTN_L) camera_rotate(&g->cam, -1);
        if (pressed & BTN_R) camera_rotate(&g->cam, 1);
        if (pressed & BTN_SQUARE) set_lang(g, (g->lang + 1) % LANG_COUNT);
        if (pressed & BTN_START) screens_goto(&g->screens, SCR_PAUSE);
        if (g->level_ok) player_tick(&g->player, &g->level, &in, g->cam.yaw.value);
        else g->player.look_active = (in.buttons & BTN_CIRCLE) ? 1 : 0;
    } else {
        g->player.look_active = 0;
        g->player.speed = 0.0f;
    }

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
        float center[3] = { 0.0f, 2.0f, 0.0f };
        camera_update(&g->cam, center);
    } else {
        float target[3] = { g->player.pos.x, g->player.pos.y + PLAYER_EYE_H, g->player.pos.z };
        camera_update(&g->cam, target);
    }

    /* Спящие сущности двигаются только вне наблюдения — фирменная механика (GDD §1.4). */
    if (g->level_ok) {
        frame_cam_t fc;
        camera_fill(&g->cam, &fc);
        int n = g->level.entity_count < GAME_MAX_ENT_STATE ? g->level.entity_count : GAME_MAX_ENT_STATE;
        for (int i = 0; i < n; i++) {
            const level_entity_t *e = &g->level.entities[i];
            float pos[3] = { e->x, e->y + 0.4f, e->z };
            int watched = observe_is_watched(&fc, pos, 0.5f, g->player.look_active);
            g->ent_watched[i] = (unsigned char)watched;
            if (e->type == ENT_SLEEPER) {
                if (!watched) g->ent_phase[i] += SLEEPER_RATE * DT;
            } else {
                g->ent_phase[i] += 12.0f * DT; /* лёгкое парение у остальных */
            }
            if (g->ent_phase[i] > 360.0f) g->ent_phase[i] -= 360.0f;
        }
    }

    particles_tick(&g->particles);
    g->frame++;
}

static void build_world(game_t *g, frame_t *f) {
    if (g->island_ok) frame_push_mesh(f, &g->island, 0.0f, 0.0f, 0.0f, 0.0f);

    if (g->level_ok) {
        int n = g->level.entity_count;
        for (int i = 0; i < n; i++) {
            const level_entity_t *e = &g->level.entities[i];
            int mi = mesh_for_entity(e->type);
            float phase = (i < GAME_MAX_ENT_STATE) ? g->ent_phase[i] : 0.0f;
            float hover = sinf(phase * M3_DEG2RAD) * 0.06f;
            if (mi >= 0 && mi < MESH_COUNT && g->object_ok[mi]) {
                float yaw = e->yaw_deg;
                float y = e->y;
                if (e->type == ENT_ECHO || e->type == ENT_SLEEPER) { y += 0.35f + hover; yaw += phase; }
                frame_mesh_t *m = frame_push_mesh(f, &g->objects[mi], e->x, y, e->z, yaw);
                if (m && (e->type == ENT_SMALL_EYE || e->type == ENT_BIG_EYE)) {
                    m->scale = 1.0f + sinf(phase * M3_DEG2RAD) * 0.04f;
                }
            }
            float glow_size = 0.0f;
            if (entity_glow(e->type, &glow_size)) {
                float pos[3] = { e->x, e->y + 0.35f, e->z };
                float pulse = 0.75f + 0.25f * sinf(phase * M3_DEG2RAD);
                unsigned alpha = (unsigned)(pulse * 190.0f);
                unsigned color = (alpha << 24) | (g->pal->slots[SLOT_GLOW] & 0x00FFFFFFu);
                frame_push_sprite(f, SPRITE_GLOW, pos, glow_size, color);
            }
        }
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

static void build_hud(game_t *g, frame_t *f) {
    if (!g->font_ok || !g->strings_ok) return;
    unsigned accent = g->pal->slots[SLOT_ACCENT];
    unsigned dim = g->pal->slots[SLOT_TOP_ALT];

    /* Подпись локации: строка берётся из данных уровня. Заголовочный кегль оставлен
     * для экранов (веха 5) — в игре он перекрывает сцену. */
    if (g->level_ok && g->level.name_str_id != 0xFFFFFFFFu) {
        frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 20, 26, accent, "%s",
                               i18n_str((int)g->level.name_str_id));
    }
    frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 20, SCR_H - 14, dim, "%s", STR(STR_HINT_CAMERA));
    frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 20, SCR_H - 30, dim, "%s", STR(STR_HINT_OBSERVE));
    frame_push_text_shadow(f, FONT_BODY, TEXT_RIGHT, SCR_W - 20, 26, dim,
                           "%s", g->lang == LANG_RU ? "RU" : "EN");

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
    f->env.fog_near = g->cam.dist + g->pal->fog_near;
    f->env.fog_far = g->cam.dist + g->pal->fog_far;
    f->env.desat = g->desat.value;

    f->env.curtain = screens_curtain(&g->screens);
    camera_fill(&g->cam, &f->cam);
    build_world(g, f);
    if (g->screens.current == SCR_GAME) build_hud(g, f);
    else if (g->screens.current == SCR_PAUSE) {
        build_hud(g, f);
        f->env.desat = 0.85f; /* сцена уходит на задний план под меню паузы */
    }
    if (g->font_ok && g->strings_ok) {
        screens_build(&g->screens, f, g->pal, g->lang, (int)g->world.eyes_opened, EYES_TOTAL);
    }

    if (g->pending_shot[0]) {
        snprintf(f->shot_name, sizeof f->shot_name, "%s", g->pending_shot);
        g->pending_shot[0] = 0;
    }
    f->quit = g->pending_quit;
}

void game_shutdown(game_t *g) {
    if (g->font_ok) font_free(&g->font);
    for (int i = 0; i < LANG_COUNT; i++) i18n_free(&g->strings[i]);
    for (int i = 0; i < MESH_COUNT; i++) if (g->object_ok[i]) mesh_free(&g->objects[i]);
    for (int i = 0; i < 3; i++) if (g->ghost_ok[i]) mesh_free(&g->ghost[i]);
    if (g->island_ok) mesh_free(&g->island);
    if (g->level_ok) level_free(&g->level);
    palette_set_free(&g->pals);
}
