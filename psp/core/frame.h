/* frame.h — граница core ↔ platform: плоский список команд рендера на один кадр.
 * core заполняет frame_t, платформа только рисует. */
#ifndef ARGUS_FRAME_H
#define ARGUS_FRAME_H
#include "mesh.h"

#define FRAME_MAX_MESHES 48
#define FRAME_MAX_SPRITES 128
#define FRAME_MAX_GHOSTS 8
#define FRAME_MAX_TEXTS 48
#define FRAME_TEXT_LEN 96
#define FRAME_NAME_LEN 32

/* Индексы гарнитур в font.bin: 0 — корпусная, 1 — заголовочная. */
enum { FONT_BODY = 0, FONT_TITLE = 1 };
enum { TEXT_LEFT = 0, TEXT_CENTER = 1, TEXT_RIGHT = 2 };
/* Виды аддитивных билбордов: свечение глаза, искра, пылинка. */
enum { SPRITE_GLOW = 0, SPRITE_SPARK = 1, SPRITE_DUST = 2 };

typedef struct {
    const mesh_t *mesh;
    float pos[3];
    float yaw_deg;
    float pitch_deg; /* наклон вокруг X — для покачивания и «дыхания» объектов */
    float scale;     /* 1.0 — как в меше; frame_push_mesh ставит 1.0 */
} frame_mesh_t;

/* Аддитивный билборд: проецируется рендером через cam_project, рисуется поверх сцены
 * без теста глубины (свечение «просвечивает» — сознательное решение арт-дирекшна). */
typedef struct {
    float pos[3];
    float size;     /* диаметр в мировых единицах */
    unsigned color; /* 0xAABBGGRR; альфа — яркость */
    unsigned char kind;
} frame_sprite_t;

typedef struct {
    char utf8[FRAME_TEXT_LEN];
    short x, y;          /* точка пера в пикселях экрана; y — базовая линия */
    unsigned color;      /* 0xAABBGGRR */
    unsigned char font;  /* FONT_BODY | FONT_TITLE */
    unsigned char align; /* TEXT_LEFT | TEXT_CENTER | TEXT_RIGHT */
} frame_text_t;

typedef struct {
    float target[3];
    float yaw_deg, pitch_deg; /* поворот вокруг цели и наклон камеры */
    float dist;               /* расстояние глаза от цели */
    float half_w;             /* полуширина орто-кадра в мировых единицах */
} frame_cam_t;

typedef struct {
    unsigned sky_top, sky_bottom, fog_color; /* 0xAABBGGRR */
    float fog_near, fog_far;
    float desat;   /* 0..1 — сила обесцвечивания в режиме взгляда (серый квад поверх сцены) */
    float vignette;/* 0..1 — затемнение к краям кадра, цвет берётся от sky_top */
    float curtain; /* 0..1 — чёрный занавес перехода между экранами, поверх всего */
} frame_env_t;

typedef struct {
    frame_env_t env;
    frame_cam_t cam;
    frame_mesh_t meshes[FRAME_MAX_MESHES];
    int mesh_count;
    frame_sprite_t sprites[FRAME_MAX_SPRITES];
    int sprite_count;
    /* Силуэты: те же меши, но рисуются только там, где объект закрыт геометрией
     * (обратный тест глубины). Так Око не теряется за террасами. */
    frame_mesh_t ghosts[FRAME_MAX_GHOSTS];
    int ghost_count;
    frame_text_t texts[FRAME_MAX_TEXTS];
    int text_count;
    char shot_name[FRAME_NAME_LEN]; /* непустое — сохранить кадр под этим именем */
    int quit;                       /* 1 — завершить игру после кадра */
} frame_t;

void frame_reset(frame_t *f);
/* Возвращает NULL при переполнении пула (тихо пропустить, не падать). */
frame_mesh_t *frame_push_mesh(frame_t *f, const mesh_t *m, float x, float y, float z, float yaw_deg);
#if defined(__GNUC__)
#define ARGUS_PRINTF(fmt_idx, arg_idx) __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define ARGUS_PRINTF(fmt_idx, arg_idx)
#endif

void frame_push_text(frame_t *f, int font, int align, int x, int y, unsigned color, const char *fmt, ...)
    ARGUS_PRINTF(7, 8);
/* То же, но с тёмной подложкой со сдвигом на пиксель: текст читается на любом фоне. */
void frame_push_text_shadow(frame_t *f, int font, int align, int x, int y, unsigned color, const char *fmt, ...)
    ARGUS_PRINTF(7, 8);
frame_sprite_t *frame_push_sprite(frame_t *f, int kind, const float pos[3], float size, unsigned color);
frame_mesh_t *frame_push_ghost(frame_t *f, const mesh_t *m, float x, float y, float z, float yaw_deg);

#endif
