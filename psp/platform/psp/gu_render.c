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
static void draw_sky_discs(const frame_env_t *env) {
    static const struct { float x, y, r; float tint; unsigned alpha; } DISCS[] = {
        { 366.0f, 58.0f, 44.0f, 0.55f, 38u },
        { 128.0f, 36.0f, 13.0f, 0.75f, 30u },
    };
    const int segs = 20;
    for (unsigned d = 0; d < sizeof DISCS / sizeof DISCS[0]; d++) {
        unsigned color = tint_white(env->sky_bottom, DISCS[d].tint, DISCS[d].alpha);
        vtx2d_t *v = (vtx2d_t *)sceGuGetMemory((int)(3 * segs * sizeof(vtx2d_t)));
        if (!v) return;
        for (int i = 0; i < segs; i++) {
            float a0 = 6.2831853f * (float)i / (float)segs;
            float a1 = 6.2831853f * (float)(i + 1) / (float)segs;
            v[i * 3 + 0] = (vtx2d_t){ color, DISCS[d].x, DISCS[d].y, 0.0f };
            v[i * 3 + 1] = (vtx2d_t){ color, DISCS[d].x + cosf(a0) * DISCS[d].r,
                                      DISCS[d].y + sinf(a0) * DISCS[d].r, 0.0f };
            v[i * 3 + 2] = (vtx2d_t){ color, DISCS[d].x + cosf(a1) * DISCS[d].r,
                                      DISCS[d].y + sinf(a1) * DISCS[d].r, 0.0f };
        }
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                       3 * segs, 0, v);
        sceGuDisable(GU_BLEND);
    }
}

static void draw_sky(const frame_env_t *env) {
    vtx2d_t *v = (vtx2d_t *)sceGuGetMemory(6 * sizeof(vtx2d_t));
    if (!v) return;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_LIGHTING);
    const float x0 = 0.0f, x1 = (float)SCR_WIDTH, y0 = 0.0f, y1 = (float)SCR_HEIGHT;
    v[0] = (vtx2d_t){ env->sky_top, x0, y0, 0.0f };
    v[1] = (vtx2d_t){ env->sky_top, x1, y0, 0.0f };
    v[2] = (vtx2d_t){ env->sky_bottom, x1, y1, 0.0f };
    v[3] = (vtx2d_t){ env->sky_top, x0, y0, 0.0f };
    v[4] = (vtx2d_t){ env->sky_bottom, x1, y1, 0.0f };
    v[5] = (vtx2d_t){ env->sky_bottom, x0, y1, 0.0f };
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 6, 0, v);
    draw_sky_discs(env);
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
    draw_ghosts(f);             /* Око видно сквозь террасы — иначе теряется в изометрии */
    draw_desat(f->env.desat);   /* выцветание сцены до свечений: глаза остаются яркими */
    gu_sprite_draw(f);          /* аддитивные билборды: свечение, искры, пылинки */
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
