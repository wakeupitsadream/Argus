/* frame.h — граница core ↔ platform: плоский список команд рендера на один кадр.
 * core заполняет frame_t, платформа только рисует. */
#ifndef ARGUS_FRAME_H
#define ARGUS_FRAME_H
#include "mesh.h"

#define FRAME_MAX_MESHES 64   /* остров + сущности + перья хвоста + Око */
#define FRAME_MAX_SPRITES 160  /* луч 60 + пыль 28 + порталы 18 + свечения ~30 + Око 2 */
#define FRAME_MAX_GHOSTS 8
#define FRAME_MAX_SHADOWS 24
#define FRAME_MAX_PANELS 8
#define FRAME_MAX_WATER 48
#define FRAME_MAX_TEXTS 48
#define FRAME_TEXT_LEN 96
#define FRAME_NAME_LEN 32

/* Индексы гарнитур в font.bin: 0 — корпусная, 1 — заголовочная, 2 — плакатная
 * (только капс и цифры: название игры, заголовки финалов). */
enum { FONT_BODY = 0, FONT_TITLE = 1, FONT_DISPLAY = 2 };
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

/* Плашка интерфейса: прямоугольник с вертикальным градиентом. Текст поверх плашки
 * читается на любом фоне, а сам интерфейс перестаёт выглядеть «наклеенным». */
typedef struct {
    short x, y, w, h;
    unsigned color_top, color_bottom; /* 0xAABBGGRR, альфа участвует */
    /* Куда плашка растворяется по горизонтали: PANEL_FADE_NONE — ровный
     * прямоугольник, PANEL_FADE_RIGHT — правый край уходит в прозрачность.
     * Жёсткая вертикальная граница подложки поперёк кадра — самая заметная
     * «самоделка» в интерфейсе: сцена делится на две половины разной яркости. */
    unsigned char fade;
} frame_panel_t;

enum { PANEL_FADE_NONE = 0, PANEL_FADE_RIGHT, PANEL_FADE_LEFT };

typedef struct {
    char utf8[FRAME_TEXT_LEN];
    short x, y;          /* точка пера в пикселях экрана; y — базовая линия */
    unsigned color;      /* 0xAABBGGRR */
    unsigned char font;  /* FONT_BODY | FONT_TITLE */
    unsigned char align; /* TEXT_LEFT | TEXT_CENTER | TEXT_RIGHT */
} frame_text_t;

/* Полоса водной глади: прямоугольник в плоскости XZ на высоте уровня воды.
 * Вода не запекается в геометрию острова, потому что её уровень меняется шлюзом:
 * в меше остаётся только дно, а гладь рисуется поверх полупрозрачным проходом. */
typedef struct {
    float x0, z0, x1, z1;
} frame_water_t;

/* Контактная тень: мягкий тёмный диск на полу под объектом. Без неё предметы в
 * изометрии «висят» — глазу не за что зацепить их высоту. Рисуется полупрозрачной
 * геометрией с тестом глубины, но без записи в Z. */
typedef struct {
    float pos[3];   /* центр диска: точка на полу */
    float radius;   /* в мировых единицах */
    unsigned char alpha;
} frame_shadow_t;

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
    unsigned shadow_color; /* цвет контактных теней: тон тени палитры */
    unsigned desat_color;  /* тон режима взгляда: мир уходит в него, свечения остаются */
    float time;    /* секунды с начала игры: небо и блики двигаются, сцена живёт */
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
    frame_shadow_t shadows[FRAME_MAX_SHADOWS];
    int shadow_count;
    frame_water_t water[FRAME_MAX_WATER];
    int water_count;
    float water_y;          /* высота глади в мировых единицах */
    unsigned water_color;   /* 0xAABBGGRR: альфа задаёт прозрачность */
    frame_panel_t panels[FRAME_MAX_PANELS];
    int panel_count;
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
/* Плашка под текст; NULL при переполнении пула. */
frame_panel_t *frame_push_panel(frame_t *f, int x, int y, int w, int h,
                                unsigned color_top, unsigned color_bottom);
/* То же, но с растворением края по горизонтали (PANEL_FADE_*). */
frame_panel_t *frame_push_panel_fade(frame_t *f, int x, int y, int w, int h,
                                     unsigned color_top, unsigned color_bottom, int fade);
/* Полоса водной глади; NULL при переполнении пула. */
frame_water_t *frame_push_water(frame_t *f, float x0, float z0, float x1, float z1);
/* Контактная тень под объектом; NULL при переполнении пула. */
frame_shadow_t *frame_push_shadow(frame_t *f, float x, float y, float z, float radius, unsigned char alpha);

#endif
