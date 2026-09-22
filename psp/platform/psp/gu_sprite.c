/* gu_sprite.c — аддитивные билборды: свечение, искры, пылинки. Проецируем центр через
 * cam_project (та же математика, что у камеры) и рисуем 2D-спрайтами поверх сцены. */
#include <pspgu.h>
#include <pspkernel.h>
#include <stddef.h>
#include "gu_sprite.h"
#include "camera.h"

#define TEX_SIZE 64
#define CLUT_ENTRIES 256u
#define CLUT_BLOCK 8u
#define SCR_W 480
#define SCR_H 272

typedef struct {
    unsigned short u, v;
    unsigned int color;
    short x, y, z;
} spr_vtx_t;

_Static_assert(sizeof(spr_vtx_t) == 16, "вершина спрайта: 16 байт");

static unsigned char __attribute__((aligned(16))) s_tex[TEX_SIZE * TEX_SIZE];
static unsigned int __attribute__((aligned(16))) s_clut[CLUT_ENTRIES];
static int s_ready;
static int s_count;

int gu_sprite_init(void) {
    /* Радиальный градиент: (1 - r)^2 — мягкое свечение без резкого края. */
    const float c = (float)(TEX_SIZE - 1) * 0.5f;
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            float dx = ((float)x - c) / c, dy = ((float)y - c) / c;
            float r2 = dx * dx + dy * dy;
            float v = r2 >= 1.0f ? 0.0f : (1.0f - r2) * (1.0f - r2);
            /* небольшое яркое ядро в центре */
            if (r2 < 0.05f) v = 1.0f;
            int a = (int)(v * 255.0f + 0.5f);
            s_tex[y * TEX_SIZE + x] = (unsigned char)(a < 0 ? 0 : (a > 255 ? 255 : a));
        }
    }
    for (unsigned i = 0; i < CLUT_ENTRIES; i++) s_clut[i] = (i << 24) | 0x00FFFFFFu;
    /* GE читает текстуру и CLUT напрямую из ОЗУ — без сброса кэша увидит мусор. */
    sceKernelDcacheWritebackRange(s_tex, sizeof s_tex);
    sceKernelDcacheWritebackRange(s_clut, sizeof s_clut);
    s_ready = 1;
    s_count = 0;
    return 0;
}

static void state_begin(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);        /* запись Z запрещена */
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    /* Аддитив: dst += src·alpha. GU_FIX с белым фиксированным множителем для dst. */
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFFu);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad((int)(CLUT_ENTRIES / CLUT_BLOCK), s_clut);
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);
    sceGuTexImage(0, TEX_SIZE, TEX_SIZE, TEX_SIZE, s_tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR); /* мягкие края свечения */
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexFlush();
}

static void state_end(void) {
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
}

void gu_sprite_draw(const frame_t *f) {
    s_count = 0;
    if (!s_ready || !f || f->sprite_count <= 0) return;

    /* Пикселей на мировую единицу: ортокамера даёт постоянный масштаб. */
    float half_w = f->cam.half_w > 0.001f ? f->cam.half_w : 0.001f;
    float ppu = (float)SCR_W / (2.0f * half_w);

    spr_vtx_t *v = (spr_vtx_t *)sceGuGetMemory((int)(2u * (unsigned)f->sprite_count * sizeof(spr_vtx_t)));
    if (!v) return;

    int n = 0;
    for (int i = 0; i < f->sprite_count; i++) {
        const frame_sprite_t *s = &f->sprites[i];
        float sx = 0.0f, sy = 0.0f, depth = 0.0f;
        cam_project(&f->cam, s->pos, &sx, &sy, &depth);
        if (depth <= 0.0f) continue;
        float r = s->size * ppu * 0.5f;
        if (r < 1.0f) r = 1.0f;
        if (sx + r < 0.0f || sx - r > (float)SCR_W || sy + r < 0.0f || sy - r > (float)SCR_H) continue;
        if ((s->color >> 24) == 0u) continue;

        spr_vtx_t *a = &v[n * 2], *b = &v[n * 2 + 1];
        a->u = 0; a->v = 0;
        a->color = s->color;
        a->x = (short)(sx - r); a->y = (short)(sy - r); a->z = 0;
        b->u = TEX_SIZE; b->v = TEX_SIZE;
        b->color = s->color;
        b->x = (short)(sx + r); b->y = (short)(sy + r); b->z = 0;
        n++;
    }
    if (n == 0) return;

    state_begin();
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   n * 2, 0, v);
    state_end();
    s_count = n;
}

int gu_sprite_last_count(void) { return s_count; }
