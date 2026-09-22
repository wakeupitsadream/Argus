#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspiofilemgr.h>
#include <math.h>
#include <string.h>
#include "gu_render.h"
#include "gu_text.h"
#include "gu_sprite.h"
#include "camera.h"
#include "fs_psp.h"
#include "platform.h"

#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define DEG2RAD (3.14159265358979f / 180.0f)

static unsigned int __attribute__((aligned(16))) s_list[262144];
static void *s_fb[2];   /* смещения в VRAM (как принимает sceGuDrawBuffer) */
static void *s_zb;
static int s_draw = 0;  /* индекс буфера, в который рисуется текущий кадр */
static int s_shown = 1; /* индекс буфера, показанного после последнего swap */
static unsigned s_tris, s_draws;

typedef struct {
    unsigned color;
    float x, y, z;
} vtx2d_t;

int r_init(void) {
    s_fb[0] = guGetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_8888);
    s_fb[1] = guGetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_8888);
    s_zb = guGetStaticVramBuffer(BUF_WIDTH, SCR_HEIGHT, GU_PSM_4444);

    sceGuInit();
    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBuffer(GU_PSM_8888, s_fb[0], BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, s_fb[1], BUF_WIDTH);
    sceGuDepthBuffer(s_zb, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CCW);      /* levelc.py генерирует обход против часовой снаружи */
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    s_draw = 0;
    s_shown = 1;

    /* Атлас билбордов строится один раз. Без этого gu_sprite_draw молча ничего не
     * рисует: пропадают свечение глаз, искры, пылинки и весь луч — проход мёртв. */
    if (gu_sprite_init() != 0) plat_log("render: билборды отключены (gu_sprite_init)");
    return 0;
}

/* Смешивает цвет с белым: k = 0 — как есть, 1 — белый. Формат 0xAABBGGRR. */
static unsigned tint_white(unsigned c, float k, unsigned alpha) {
    unsigned r = c & 0xFFu, g = (c >> 8) & 0xFFu, b = (c >> 16) & 0xFFu;
    r = (unsigned)((float)r + (255.0f - (float)r) * k);
    g = (unsigned)((float)g + (255.0f - (float)g) * k);
    b = (unsigned)((float)b + (255.0f - (float)b) * k);
    return (alpha << 24) | (b << 16) | (g << 8) | r;
}

/* Плоские диски-луны на небе: дают кадру композиционный якорь и «нарисованность».
 * Цвет берётся из палитры региона, поэтому небо каждого региона своё. */
/* ——— небо ———
 * Небо занимает половину кадра, поэтому от него зависит, выглядит ли сцена дорогой.
 * Рисуется целиком гуро-геометрией, без текстур: полосатый градиент с нелинейным
 * профилем, тёплая дымка у горизонта, солнце с ореолом и несколько мягких облачных
 * полос. Всё — 2D, глубина выключена, порядок сверху вниз. */

#define SKY_BANDS 6       /* полос градиента: больше — меньше ступенек на 8888 */
#define SUN_X 0.255f      /* положение солнца в долях экрана: слева, вдали от HUD */
#define SUN_Y 0.165f
#define SUN_R 26.0f
#define SUN_RINGS 6

/* Линейная смесь двух цветов 0xAABBGGRR по k = 0..1 (альфа берётся из a). */
static unsigned mix_rgb(unsigned c0, unsigned c1, float k, unsigned a) {
    if (k < 0.0f) k = 0.0f;
    if (k > 1.0f) k = 1.0f;
    unsigned out = a << 24;
    for (int i = 0; i < 3; i++) {
        float v0 = (float)((c0 >> (8 * i)) & 0xFFu), v1 = (float)((c1 >> (8 * i)) & 0xFFu);
        unsigned v = (unsigned)(v0 + (v1 - v0) * k + 0.5f);
        if (v > 255u) v = 255u;
        out |= v << (8 * i);
    }
    return out;
}

/* Полоса-капсула с мягкими краями: по краям альфа нулевая, в середине — full.
 * Из таких полос собираются облака и дымка у горизонта. */
static void sky_band(float cx, float cy, float half_w, float half_h, unsigned color, unsigned alpha) {
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory(18 * sizeof(vtx2d_t));
    if (!v) return;
    unsigned mid = (alpha << 24) | (color & 0x00FFFFFFu);
    unsigned edge = color & 0x00FFFFFFu;  /* альфа 0 */
    float x0 = cx - half_w, x1 = cx - half_w * 0.35f, x2 = cx + half_w * 0.35f, x3 = cx + half_w;
    float y0 = cy - half_h, y1 = cy + half_h;
    const float xs[4] = { x0, x1, x2, x3 };
    const unsigned cs[4] = { edge, mid, mid, edge };
    int n = 0;
    for (int i = 0; i < 3; i++) {
        v[n++] = (vtx2d_t){ cs[i], xs[i], y0, 0.0f };
        v[n++] = (vtx2d_t){ cs[i + 1], xs[i + 1], y0, 0.0f };
        v[n++] = (vtx2d_t){ cs[i + 1], xs[i + 1], y1, 0.0f };
        v[n++] = (vtx2d_t){ cs[i], xs[i], y0, 0.0f };
        v[n++] = (vtx2d_t){ cs[i + 1], xs[i + 1], y1, 0.0f };
        v[n++] = (vtx2d_t){ cs[i], xs[i], y1, 0.0f };
    }
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, v);
}

/* Диск из треугольного веера: солнце и его ореол. */
static void sky_disc(float cx, float cy, float r, unsigned color, unsigned a_center, unsigned a_edge) {
    const int segs = 24;
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory((int)(3 * segs * sizeof(vtx2d_t)));
    if (!v) return;
    unsigned c_in = (a_center << 24) | (color & 0x00FFFFFFu);
    unsigned c_out = (a_edge << 24) | (color & 0x00FFFFFFu);
    for (int i = 0; i < segs; i++) {
        float a0 = 6.2831853f * (float)i / (float)segs;
        float a1 = 6.2831853f * (float)(i + 1) / (float)segs;
        v[i * 3 + 0] = (vtx2d_t){ c_in, cx, cy, 0.0f };
        v[i * 3 + 1] = (vtx2d_t){ c_out, cx + cosf(a0) * r, cy + sinf(a0) * r, 0.0f };
        v[i * 3 + 2] = (vtx2d_t){ c_out, cx + cosf(a1) * r, cy + sinf(a1) * r, 0.0f };
    }
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 3 * segs, 0, v);
}

/* Медленный дрейф облака: возвращает x, уезжающий вправо и заходящий слева.
 * Полоса шире экрана, поэтому подмены никто не видит. */
static float drift(float base, float time, float speed, float width) {
    float span = width * 2.0f;
    float x = base + time * speed;
    x = fmodf(x, span);
    if (x < 0.0f) x += span;
    return x - width * 0.5f;
}

static void draw_sky(const frame_env_t *env) {
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);

    /* 1. Градиент полосами с нелинейным профилем: тёмная верхушка держится дольше,
     *    у горизонта цвет разгоняется — так небо читается глубоким, а не плоским. */
    const float H = (float)SCR_HEIGHT, W = (float)SCR_WIDTH;
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory(6 * SKY_BANDS * sizeof(vtx2d_t));
    if (!v) return;
    int n = 0;
    for (int i = 0; i < SKY_BANDS; i++) {
        float t0 = (float)i / (float)SKY_BANDS, t1 = (float)(i + 1) / (float)SKY_BANDS;
        float y0 = t0 * H, y1 = t1 * H;
        unsigned c0 = mix_rgb(env->sky_top, env->sky_bottom, t0 * t0 * (3.0f - 2.0f * t0), 255u);
        unsigned c1 = mix_rgb(env->sky_top, env->sky_bottom, t1 * t1 * (3.0f - 2.0f * t1), 255u);
        v[n++] = (vtx2d_t){ c0, 0.0f, y0, 0.0f };
        v[n++] = (vtx2d_t){ c0, W, y0, 0.0f };
        v[n++] = (vtx2d_t){ c1, W, y1, 0.0f };
        v[n++] = (vtx2d_t){ c0, 0.0f, y0, 0.0f };
        v[n++] = (vtx2d_t){ c1, W, y1, 0.0f };
        v[n++] = (vtx2d_t){ c1, 0.0f, y1, 0.0f };
    }
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, v);

    /* 2. Всё остальное — полупрозрачными наложениями. */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    unsigned warm = tint_white(env->sky_bottom, 0.45f, 255u);
    /* Ореол вокруг солнца: три кольца с падающей альфой. */
    float sx = W * SUN_X, sy = H * SUN_Y;
    for (int i = SUN_RINGS; i >= 1; i--) {
        float r = SUN_R * (0.9f + 1.15f * (float)i);
        unsigned a = (unsigned)(26.0f / (float)i);   /* внешние кольца почти незаметны */
        sky_disc(sx, sy, r, warm, a, 0u);
    }
    /* Само солнце: ядро с мягким спадом до нуля — резкий край читался бы как ошибка. */
    sky_disc(sx, sy, SUN_R, tint_white(env->sky_bottom, 0.92f, 255u), 150u, 0u);
    sky_disc(sx, sy, SUN_R * 0.45f, tint_white(env->sky_bottom, 0.98f, 255u), 190u, 20u);

    /* 3. Облака: широкие мягкие полосы двумя слоями, медленно плывущие поперёк кадра.
     *    Тонкая полоса читалась бы как линия-артефакт, поэтому высота заметная,
     *    а альфа маленькая. Разные скорости дают слабый параллакс. */
    float t = env->time;
    sky_band(drift(W * 0.30f, t, 2.6f, W), H * 0.29f, W * 0.34f, 13.0f, warm, 18u);
    sky_band(drift(W * 0.66f, t, 1.7f, W), H * 0.38f, W * 0.30f, 16.0f, warm, 22u);
    sky_band(drift(W * 0.44f, t, 3.4f, W), H * 0.36f, W * 0.46f, 9.0f, warm, 14u);
    sky_band(drift(W * 0.58f, t, 1.1f, W), H * 0.49f, W * 0.40f, 18.0f, warm, 26u);

    /* 4. Дымка у горизонта: воздух между камерой и островом, дальний план отходит. */
    sky_band(W * 0.5f, H * 0.68f, W * 0.80f, 30.0f, warm, 40u);

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

/* Полноэкранный серый квад: обесцвечивание сцены в режиме взгляда. Шейдеров на PSP нет,
 * поэтому «выцветание» делается наложением с альфой — дешёво и предсказуемо. */
static void draw_desat(float amount) {
    if (amount <= 0.002f) return;
    if (amount > 1.0f) amount = 1.0f;
    unsigned alpha = (unsigned)(amount * 110.0f);
    unsigned color = (alpha << 24) | 0x00808080u;
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory(6 * sizeof(vtx2d_t));
    if (!v) return;
    const float x0 = 0.0f, x1 = (float)SCR_WIDTH, y0 = 0.0f, y1 = (float)SCR_HEIGHT;
    v[0] = (vtx2d_t){ color, x0, y0, 0.0f };
    v[1] = (vtx2d_t){ color, x1, y0, 0.0f };
    v[2] = (vtx2d_t){ color, x1, y1, 0.0f };
    v[3] = (vtx2d_t){ color, x0, y0, 0.0f };
    v[4] = (vtx2d_t){ color, x1, y1, 0.0f };
    v[5] = (vtx2d_t){ color, x0, y1, 0.0f };

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

/* Плашки интерфейса: прямоугольники с вертикальным градиентом под текстом.
 * Рисуются после сцены и виньетки, но до текста. */
static void draw_panels(const frame_t *f) {
    int count = f->panel_count;
    if (count <= 0) return;
    if (count > FRAME_MAX_PANELS) count = FRAME_MAX_PANELS;
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory((int)(6 * (unsigned)count * sizeof(vtx2d_t)));
    if (!v) return;
    int n = 0;
    for (int i = 0; i < count; i++) {
        const frame_panel_t *p = &f->panels[i];
        float x0 = (float)p->x, y0 = (float)p->y;
        float x1 = x0 + (float)p->w, y1 = y0 + (float)p->h;
        v[n++] = (vtx2d_t){ p->color_top, x0, y0, 0.0f };
        v[n++] = (vtx2d_t){ p->color_top, x1, y0, 0.0f };
        v[n++] = (vtx2d_t){ p->color_bottom, x1, y1, 0.0f };
        v[n++] = (vtx2d_t){ p->color_top, x0, y0, 0.0f };
        v[n++] = (vtx2d_t){ p->color_bottom, x1, y1, 0.0f };
        v[n++] = (vtx2d_t){ p->color_bottom, x0, y1, 0.0f };
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

/* Виньетка: мягкое затемнение к краям кадра. Делается кольцом гуро-треугольников
 * между внутренним эллипсом (прозрачным) и границей экрана с запасом (тёмной) —
 * без текстуры и без второго прохода, один draw-call на 2·VIGNETTE_SEGS треугольников.
 * Это половина «дорогого» кадра: взгляд перестаёт уезжать в угол. */
#define VIGNETTE_SEGS 24
#define VIGNETTE_IN_X 250.0f   /* полуоси прозрачного центра, пиксели */
#define VIGNETTE_IN_Y 150.0f
#define VIGNETTE_OUT  1.12f    /* насколько внешний контур выходит за экран */

static void draw_vignette(float amount, unsigned color_rgb) {
    if (amount <= 0.002f) return;
    if (amount > 1.0f) amount = 1.0f;
    const float cx = (float)SCR_WIDTH * 0.5f, cy = (float)SCR_HEIGHT * 0.5f;
    unsigned alpha = (unsigned)(amount * 255.0f);
    unsigned dark = (alpha << 24) | (color_rgb & 0x00FFFFFFu);
    unsigned clear = color_rgb & 0x00FFFFFFu; /* альфа 0: центр не трогаем */

    int n = 6 * VIGNETTE_SEGS;
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory((int)((unsigned)n * sizeof(vtx2d_t)));
    if (!v) return;
    for (int i = 0; i < VIGNETTE_SEGS; i++) {
        float a0 = 6.2831853f * (float)i / (float)VIGNETTE_SEGS;
        float a1 = 6.2831853f * (float)(i + 1) / (float)VIGNETTE_SEGS;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        /* Внешняя точка — на границе экрана в этом направлении, с запасом:
         * так угол кадра получает полную густоту, а не остаток градиента. */
        float t0 = fminf(fabsf(c0) > 1e-4f ? cx / fabsf(c0) : 1.0e6f,
                         fabsf(s0) > 1e-4f ? cy / fabsf(s0) : 1.0e6f) * VIGNETTE_OUT;
        float t1 = fminf(fabsf(c1) > 1e-4f ? cx / fabsf(c1) : 1.0e6f,
                         fabsf(s1) > 1e-4f ? cy / fabsf(s1) : 1.0e6f) * VIGNETTE_OUT;
        vtx2d_t in0 = { clear, cx + c0 * VIGNETTE_IN_X, cy + s0 * VIGNETTE_IN_Y, 0.0f };
        vtx2d_t in1 = { clear, cx + c1 * VIGNETTE_IN_X, cy + s1 * VIGNETTE_IN_Y, 0.0f };
        vtx2d_t out0 = { dark, cx + c0 * t0, cy + s0 * t0, 0.0f };
        vtx2d_t out1 = { dark, cx + c1 * t1, cy + s1 * t1, 0.0f };
        v[i * 6 + 0] = in0;  v[i * 6 + 1] = out0; v[i * 6 + 2] = out1;
        v[i * 6 + 3] = in0;  v[i * 6 + 4] = out1; v[i * 6 + 5] = in1;
    }

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

/* Чёрный занавес перехода — поверх всего, включая текст. */
static void draw_curtain(float amount) {
    if (amount <= 0.002f) return;
    if (amount > 1.0f) amount = 1.0f;
    unsigned alpha = (unsigned)(amount * 255.0f);
    unsigned color = (alpha << 24);
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory(6 * sizeof(vtx2d_t));
    if (!v) return;
    const float x1 = (float)SCR_WIDTH, y1 = (float)SCR_HEIGHT;
    v[0] = (vtx2d_t){ color, 0.0f, 0.0f, 0.0f };
    v[1] = (vtx2d_t){ color, x1, 0.0f, 0.0f };
    v[2] = (vtx2d_t){ color, x1, y1, 0.0f };
    v[3] = (vtx2d_t){ color, 0.0f, 0.0f, 0.0f };
    v[4] = (vtx2d_t){ color, x1, y1, 0.0f };
    v[5] = (vtx2d_t){ color, 0.0f, y1, 0.0f };
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

static void setup_camera(const frame_cam_t *cam) {
    float eye_v[3];
    cam_eye(cam, eye_v); /* та же функция, что и в cam_project: картинка и проекция не расходятся */
    ScePspFVector3 center = { cam->target[0], cam->target[1], cam->target[2] };
    ScePspFVector3 eye = { eye_v[0], eye_v[1], eye_v[2] };
    ScePspFVector3 up = { 0.0f, 1.0f, 0.0f };
    float hw = cam->half_w, hh = cam->half_w * ((float)SCR_HEIGHT / (float)SCR_WIDTH);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(-hw, hw, -hh, hh, 1.0f, cam->dist * 2.5f);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumLookAt(&eye, &center, &up);
}

static void draw_meshes(const frame_t *f) {
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_CULL_FACE);
    sceGuFog(f->env.fog_near, f->env.fog_far, f->env.fog_color);
    sceGuEnable(GU_FOG);
    for (int i = 0; i < f->mesh_count; i++) {
        const frame_mesh_t *cmd = &f->meshes[i];
        if (!cmd->mesh || !cmd->mesh->verts) continue;
        ScePspFVector3 pos = { cmd->pos[0], cmd->pos[1], cmd->pos[2] };
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumTranslate(&pos);
        if (cmd->yaw_deg != 0.0f || cmd->pitch_deg != 0.0f) {
            ScePspFVector3 rot = { cmd->pitch_deg * DEG2RAD, cmd->yaw_deg * DEG2RAD, 0.0f };
            sceGumRotateXYZ(&rot);
        }
        if (cmd->scale != 1.0f && cmd->scale > 0.0f) {
            ScePspFVector3 sc = { cmd->scale, cmd->scale, cmd->scale };
            sceGumScale(&sc);
        }
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        cmd->mesh->count, 0, cmd->mesh->verts);
        s_tris += (unsigned)cmd->mesh->count / 3u;
        s_draws++;
    }
    sceGuDisable(GU_FOG);
}

/* Водная гладь: полупрозрачные прямоугольники на высоте уровня воды. Рисуются после
 * непрозрачной геометрии с тестом глубины, но без записи в Z — под водой видно дно,
 * а объекты на берегу воду не перекрашивают. Бликов не рисуем: их даёт проход спрайтов. */
static void draw_water(const frame_t *f) {
    int count = f->water_count;
    if (count <= 0) return;
    if (count > FRAME_MAX_WATER) count = FRAME_MAX_WATER;

    vtx_static_t *v = (vtx_static_t *)sceGuGetMemory((int)(6u * (unsigned)count * sizeof(vtx_static_t)));
    if (!v) return;
    unsigned c = f->water_color;
    float y = f->water_y;
    int n = 0;
    for (int i = 0; i < count; i++) {
        const frame_water_t *w = &f->water[i];
        v[n++] = (vtx_static_t){ c, w->x0, y, w->z0 };
        v[n++] = (vtx_static_t){ c, w->x1, y, w->z0 };
        v[n++] = (vtx_static_t){ c, w->x1, y, w->z1 };
        v[n++] = (vtx_static_t){ c, w->x0, y, w->z0 };
        v[n++] = (vtx_static_t){ c, w->x1, y, w->z1 };
        v[n++] = (vtx_static_t){ c, w->x0, y, w->z1 };
    }

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);        /* GU_TRUE = запись Z запрещена */
    sceGuDisable(GU_CULL_FACE);     /* гладь видна и снизу, если камера ушла под берег */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFog(f->env.fog_near, f->env.fog_far, f->env.fog_color);
    sceGuEnable(GU_FOG);
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, n, 0, v);
    s_tris += (unsigned)n / 3u;
    s_draws++;

    sceGuDisable(GU_FOG);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
}

/* Контактные тени: мягкие диски на полу под объектами. Геометрия строится на месте
 * (веер из SHADOW_SEGS треугольников), цвет — тон тени палитры с альфой в центре и
 * нулём по краю. Тест глубины включён, запись Z выключена: тень ложится на пол,
 * но не мешает рисовать то, что стоит на ней. */
#define SHADOW_SEGS 12
#define SHADOW_LIFT 0.02f   /* приподнимаем над полом, иначе Z-конфликт с верхней гранью */

static void draw_shadows(const frame_t *f) {
    int count = f->shadow_count;
    if (count <= 0) return;
    if (count > FRAME_MAX_SHADOWS) count = FRAME_MAX_SHADOWS;

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);        /* диск виден с любой стороны */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);           /* GU_TRUE = запись Z запрещена */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuShadeModel(GU_SMOOTH);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    unsigned rgb = f->env.shadow_color & 0x00FFFFFFu;
    for (int i = 0; i < count; i++) {
        const frame_shadow_t *sh = &f->shadows[i];
        vtx_static_t *v = (vtx_static_t *)sceGuGetMemory((int)(3 * SHADOW_SEGS * sizeof(vtx_static_t)));
        if (!v) break;
        unsigned c_in = ((unsigned)sh->alpha << 24) | rgb;
        unsigned c_out = rgb;  /* альфа 0 */
        float y = sh->pos[1] + SHADOW_LIFT;
        for (int k = 0; k < SHADOW_SEGS; k++) {
            float a0 = 6.2831853f * (float)k / (float)SHADOW_SEGS;
            float a1 = 6.2831853f * (float)(k + 1) / (float)SHADOW_SEGS;
            v[k * 3 + 0] = (vtx_static_t){ c_in, sh->pos[0], y, sh->pos[2] };
            v[k * 3 + 1] = (vtx_static_t){ c_out, sh->pos[0] + cosf(a0) * sh->radius, y,
                                           sh->pos[2] + sinf(a0) * sh->radius };
            v[k * 3 + 2] = (vtx_static_t){ c_out, sh->pos[0] + cosf(a1) * sh->radius, y,
                                           sh->pos[2] + sinf(a1) * sh->radius };
        }
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        3 * SHADOW_SEGS, 0, v);
        s_tris += SHADOW_SEGS;
        s_draws++;
    }

    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
}

/* Силуэты: рисуем только там, где пиксель уже закрыт более близкой геометрией.
 * Глубина у нас перевёрнута (GU_GEQUAL + DepthRange(65535, 0)), поэтому «дальше» — это GU_LESS. */
static void draw_ghosts(const frame_t *f) {
    if (f->ghost_count <= 0) return;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_FOG);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_LESS);
    sceGuDepthMask(GU_TRUE); /* в буфер глубины не пишем */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    for (int i = 0; i < f->ghost_count; i++) {
        const frame_mesh_t *cmd = &f->ghosts[i];
        if (!cmd->mesh || !cmd->mesh->verts) continue;
        ScePspFVector3 pos = { cmd->pos[0], cmd->pos[1], cmd->pos[2] };
        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();
        sceGumTranslate(&pos);
        if (cmd->yaw_deg != 0.0f) {
            ScePspFVector3 rot = { 0.0f, cmd->yaw_deg * DEG2RAD, 0.0f };
            sceGumRotateXYZ(&rot);
        }
        if (cmd->scale != 1.0f && cmd->scale > 0.0f) {
            ScePspFVector3 sc = { cmd->scale, cmd->scale, cmd->scale };
            sceGumScale(&sc);
        }
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        cmd->mesh->count, 0, cmd->mesh->verts);
        s_tris += (unsigned)cmd->mesh->count / 3u;
        s_draws++;
    }

    sceGuDisable(GU_BLEND);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(GU_FALSE);
}

void r_draw_frame(const frame_t *f, plat_stats_t *stats) {
    SceInt64 t0 = sceKernelGetSystemTimeWide();
    s_tris = 0;
    s_draws = 0;

    sceGuStart(GU_DIRECT, s_list);
    sceGuClearColor(f->env.sky_bottom);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    draw_sky(&f->env);
    setup_camera(&f->cam);
    draw_meshes(f);
    draw_water(f);              /* гладь поверх дна: её высота меняется шлюзом */
    draw_shadows(f);            /* контактные тени ложатся на пол под объектами */
    draw_ghosts(f);             /* Око видно сквозь террасы — иначе теряется в изометрии */
    draw_desat(f->env.desat);   /* выцветание сцены до свечений: глаза остаются яркими */
    gu_sprite_draw(f);          /* аддитивные билборды: свечение, искры, пылинки */
    /* Виньетка — по сцене и свечениям, но до текста: подписи должны остаться чистыми. */
    draw_vignette(f->env.vignette, f->env.sky_top);
    draw_panels(f);             /* плашки под текст: интерфейс лежит на подложке */
    gu_text_draw(f);            /* 2D-наложение поверх сцены */
    draw_curtain(f->env.curtain); /* занавес перехода — поверх всего, включая текст */
    sceGuFinish();

    SceInt64 t1 = sceKernelGetSystemTimeWide();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    SceInt64 t2 = sceKernelGetSystemTimeWide();

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    s_shown = s_draw;
    s_draw ^= 1;

    if (stats) {
        stats->cpu_us = (unsigned)(t1 - t0);
        stats->gpu_us = (unsigned)(t2 - t1);
        /* Треугольники и вызовы считаем в своих единицах: у текста и спрайтов
         * каждый квад — два треугольника, но уходят они одним вызовом на строку. */
        stats->tris = s_tris + 2u * (unsigned)gu_text_last_quads() + 2u * (unsigned)gu_sprite_last_count();
        stats->draws = s_draws + (unsigned)gu_text_last_calls() + (unsigned)gu_sprite_last_calls();
    }
}

int r_screenshot_bmp(const char *rel_path) {
    /* Читаем показанный буфер по некэшированному адресу VRAM. */
    const unsigned char *vram = (const unsigned char *)(((unsigned)sceGeEdramGetAddr() | 0x40000000u) + (unsigned)s_fb[s_shown]);
    const unsigned row_bytes = SCR_WIDTH * 3;
    const unsigned data_bytes = row_bytes * SCR_HEIGHT; /* 1440 — кратно 4, без выравнивания строк */
    unsigned char *bmp = (unsigned char *)plat_alloc16(54 + data_bytes);
    if (!bmp) return -1;
    memset(bmp, 0, 54);
    bmp[0] = 'B'; bmp[1] = 'M';
    unsigned file_size = 54 + data_bytes;
    memcpy(bmp + 2, &file_size, 4);
    unsigned off = 54; memcpy(bmp + 10, &off, 4);
    unsigned dib = 40; memcpy(bmp + 14, &dib, 4);
    int w = SCR_WIDTH, h = SCR_HEIGHT; memcpy(bmp + 18, &w, 4); memcpy(bmp + 22, &h, 4);
    unsigned short planes = 1, bpp = 24; memcpy(bmp + 26, &planes, 2); memcpy(bmp + 28, &bpp, 2);
    memcpy(bmp + 34, &data_bytes, 4);
    for (int y = 0; y < SCR_HEIGHT; y++) {
        const unsigned *src = (const unsigned *)(vram + (unsigned)y * BUF_WIDTH * 4);
        unsigned char *dst = bmp + 54 + (unsigned)(SCR_HEIGHT - 1 - y) * row_bytes;
        for (int x = 0; x < SCR_WIDTH; x++) {
            unsigned c = src[x];            /* 0xAABBGGRR */
            dst[x * 3 + 0] = (unsigned char)((c >> 16) & 0xFF); /* B */
            dst[x * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);  /* G */
            dst[x * 3 + 2] = (unsigned char)(c & 0xFF);         /* R */
        }
    }
    int rc = plat_write_file(rel_path, bmp, 54 + data_bytes);
    plat_free(bmp);
    return rc;
}

void r_term(void) { sceGuTerm(); }
