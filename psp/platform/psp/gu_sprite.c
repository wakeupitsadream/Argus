/* gu_sprite.c — аддитивные билборды: свечение глаз, искры, пылинки.
 *
 * Центр спрайта проецируем через cam_project (ровно та математика, что у камеры:
 * картинка и логика не расходятся), дальше рисуем 2D-спрайтами поверх сцены.
 * Текстура — процедурный атлас T8 64×256 из трёх радиальных заготовок 64×64
 * (GLOW, SPARK, DUST) плюс нулевая плитка-запас: степень двойки по обеим сторонам,
 * вид выбирается смещением v. Поэтому все спрайты кадра уходят одним draw-call. */
#include <pspgu.h>
#include <psputils.h>
#include <math.h>
#include <stddef.h>
#include "gu_sprite.h"
#include "camera.h"

#define SCR_W 480
#define SCR_H 272

#define TILE       64   /* сторона одной радиальной заготовки */
#define TEX_W      64
#define TEX_H      256  /* 4 плитки по 64: степень двойки обязательна (CLAUDE.md п.5) */
#define KIND_COUNT 3    /* GLOW, SPARK, DUST; четвёртая плитка — нули, запас */

#define CLUT_ENTRIES 256u /* T8: индексы 0..255 */
#define CLUT_BLOCK   8u   /* sceGuClutLoad принимает число блоков по 8 записей */

#define SPR_MIN_R 1.5f   /* пылинка не должна исчезать: диаметр ≥ 3 пикселей */
#define SPR_MAX_R 320.0f /* страховка от мусорного size: координаты остаются в short */

/* Плитка атласа выбирается по kind, поэтому порядок SPRITE_* зафиксирован. */
_Static_assert(SPRITE_GLOW == 0 && SPRITE_SPARK == 1 && SPRITE_DUST == 2,
               "порядок SPRITE_* задаёт нумерацию плиток атласа");

/* Вершина 2D-спрайта. Порядок полей задан GE: texcoord → color → position
 * (pspgu.h, sceGuDrawArray). Шаг вершины GE выравнивает по самому широкому полю
 * (color, 4 байта) → 16 байт, ровно sizeof структуры: хвостовой паддинг безопасен.
 * В GU_TRANSFORM_2D u, v — номера текселей, x, y — пиксели экрана (со знаком),
 * sceGuTexScale/sceGuTexOffset не применяются. */
typedef struct {
    unsigned short u, v;
    unsigned int color;
    short x, y, z;
} spr_vtx_t;

_Static_assert(sizeof(spr_vtx_t) == 16, "вершина спрайта: 16 байт (шаг вершины GE)");
_Static_assert(offsetof(spr_vtx_t, color) == 4, "color идёт после texcoord");
_Static_assert(offsetof(spr_vtx_t, x) == 8, "position идёт после color");

/* Профиль вида: форма пятна печётся в текстуру, размер — в вершины. */
typedef struct {
    float peak;  /* пик альфы 0..1 — «яркость» вида */
    float power; /* показатель спада (1 - r²)^power: больше — резче пятно */
    float core;  /* доля радиуса ярчайшего ядра */
    float scale; /* множитель пиксельного диаметра */
} spr_kind_t;

static const spr_kind_t s_kinds[KIND_COUNT] = {
    /* GLOW  — широкое мягкое свечение глаза */ { 1.00f, 2.0f, 0.22f, 1.0f },
    /* SPARK — короткая яркая искра          */ { 1.00f, 3.5f, 0.10f, 1.6f },
    /* DUST  — тусклая пылинка в воздухе     */ { 0.55f, 1.6f, 0.00f, 2.2f },
};

/* GE читает текстуру и CLUT из ОЗУ мимо кэша CPU — выравнивание 16 байт
 * обязательно (CLAUDE.md п.2), сброс кэша делает gu_sprite_init. */
static unsigned char __attribute__((aligned(16))) s_tex[TEX_W * TEX_H];
static unsigned int __attribute__((aligned(16))) s_clut[CLUT_ENTRIES];

static int s_ready;
static int s_count; /* спрайтов отправлено на прошлом кадре */
static int s_calls; /* вызовов отрисовки на прошлом кадре */

/* Заполняет плитку k радиальным градиентом по её профилю. */
static void build_tile(int k) {
    const spr_kind_t *p = &s_kinds[k];
    const float half = (float)TILE * 0.5f;
    const float core2 = p->core * p->core;
    unsigned char *tile = &s_tex[(unsigned)k * TILE * TEX_W];

    for (int y = 0; y < TILE; y++) {
        for (int x = 0; x < TILE; x++) {
            /* Координата центра текселя, нормированная на радиус плитки. */
            float dx = ((float)x + 0.5f - half) / half;
            float dy = ((float)y + 0.5f - half) / half;
            float r2 = dx * dx + dy * dy;
            float a;
            if (r2 >= 1.0f) {
                a = 0.0f;
            } else if (r2 <= core2) {
                a = 1.0f; /* ровное ядро: без него центр выглядит проваленным */
            } else {
                a = powf(1.0f - r2, p->power);
            }
            int val = (int)(a * p->peak * 255.0f + 0.5f);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            tile[y * TEX_W + x] = (unsigned char)val;
        }
    }
    /* Внешнее кольцо строго в нуль: GU_LINEAR у границы плитки подмешивает
     * соседний ряд атласа, нулевая кайма делает это подмешивание невидимым. */
    for (int x = 0; x < TILE; x++) {
        tile[x] = 0;
        tile[(TILE - 1) * TEX_W + x] = 0;
    }
    for (int y = 0; y < TILE; y++) {
        tile[y * TEX_W] = 0;
        tile[y * TEX_W + (TILE - 1)] = 0;
    }
}

int gu_sprite_init(void) {
    s_ready = 0;
    s_count = 0;
    s_calls = 0;

    for (int i = 0; i < TEX_W * TEX_H; i++) s_tex[i] = 0; /* плитка-запас остаётся нулевой */
    for (int k = 0; k < KIND_COUNT; k++) build_tile(k);

    /* Запись i: альфа = значение T8, RGB белый (0xAABBGGRR). Цвет спрайту даёт
     * вершина через GU_TFX_MODULATE: Cv = Ct · Cf, Av = At · Af. */
    for (unsigned i = 0; i < CLUT_ENTRIES; i++) s_clut[i] = (i << 24) | 0x00FFFFFFu;

    /* Без сброса кэша GE увидит в этой памяти мусор (CLAUDE.md п.2). */
    sceKernelDcacheWritebackRange(s_tex, (unsigned)sizeof(s_tex));
    sceKernelDcacheWritebackRange(s_clut, (unsigned)sizeof(s_clut));

    s_ready = 1;
    return 0;
}

/* Состояние прохода выставляем полностью сами (CLAUDE.md п.9). */
static void state_begin(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE); /* GU_TRUE = запись Z запрещена (pspgu.h) */
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE); /* у 2D-спрайтов обхода нет */

    sceGuEnable(GU_BLEND);
    /* Аддитив: Cdst = Csrc·As + Cdst·1. Множитель приёмника — GU_FIX, значение
     * берётся из destfix (0xffffffff = 1,0 по каналу); srcfix при GU_SRC_ALPHA
     * не используется. Семантика сверена с pspgu.h и samples/gu/blend. */
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);                  /* индекс = (T8 >> 0) & 0xff */
    sceGuClutLoad((int)(CLUT_ENTRIES / CLUT_BLOCK), s_clut);  /* 32 блока по 8 записей */
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);                         /* без мип-уровней, без свизла */
    sceGuTexImage(0, TEX_W, TEX_H, TEX_W, s_tex);             /* tbw = 64, кратно 16 */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);               /* RGBA: иначе альфа текстуры не читается */
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);                     /* мягкий край свечения */
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f); /* нейтрально: в GU_TRANSFORM_2D не влияет (pspgu.h) */
    sceGuTexOffset(0.0f, 0.0f);
    sceGuTexFlush();           /* текстура сменилась (CLAUDE.md п.5) */
}

/* Возвращаем состояние, пригодное для следующего кадра: тест глубины включён,
 * запись Z разрешена, смешивание и текстурирование выключены (CLAUDE.md п.9). */
static void state_end(void) {
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE); /* GU_FALSE = запись Z разрешена */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
}

void gu_sprite_draw(const frame_t *f) {
    s_count = 0;
    s_calls = 0;
    if (!s_ready || !f) return;

    int count = f->sprite_count;
    if (count > FRAME_MAX_SPRITES) count = FRAME_MAX_SPRITES; /* пул фиксирован, счётчику не доверяем */
    if (count <= 0) return;

    /* Пиксельный размер при ортокамере не зависит от глубины: ширина кадра —
     * 2·half_w мировых единиц на SCR_W пикселей, отсюда пикселей на единицу. */
    float half_w = f->cam.half_w;
    if (half_w < 0.01f) half_w = 0.01f;
    const float ppu = (float)SCR_W / (2.0f * half_w);

    /* Временные вершины — только через sceGuGetMemory: живут до sceGuFinish
     * (CLAUDE.md п.3). Выравнивание 16 байт вершинам GE не требуется. */
    spr_vtx_t *v = (spr_vtx_t *)sceGuGetMemory((int)(2u * (unsigned)count * sizeof(spr_vtx_t)));
    if (!v) return;

    int n = 0;
    for (int i = 0; i < count; i++) {
        const frame_sprite_t *s = &f->sprites[i];
        if ((s->color >> 24) == 0u) continue; /* прозрачный: аддитиву нечего добавить */

        int kind = (s->kind < KIND_COUNT) ? (int)s->kind : SPRITE_GLOW;
        const spr_kind_t *p = &s_kinds[kind];

        float cx = 0.0f, cy = 0.0f, depth = 0.0f;
        /* cam_project сам отсекает точки за камерой, за дальней плоскостью и вне
         * кадра — верим его вердикту, иначе проекция спрайтов разойдётся с
         * камерой. Крупный блик исчезает, как только центр покинул кадр. */
        if (!cam_project(&f->cam, s->pos, &cx, &cy, &depth)) continue;

        float r = s->size * ppu * p->scale * 0.5f;
        if (!(r > 0.0f)) r = SPR_MIN_R; /* заодно ловит NaN */
        if (r < SPR_MIN_R) r = SPR_MIN_R;
        if (r > SPR_MAX_R) r = SPR_MAX_R;

        float x0 = cx - r, y0 = cy - r, x1 = cx + r, y1 = cy + r;
        if (x1 <= 0.0f || y1 <= 0.0f || x0 >= (float)SCR_W || y0 >= (float)SCR_H) continue;

        /* В GU_TRANSFORM_2D координаты идут в растеризатор как есть, поэтому
         * обрезаем квад сами и тянем u, v пропорционально (у GU_SPRITES текстура
         * натянута на прямоугольник линейно, значит обрезка точная). */
        const float w = x1 - x0, h = y1 - y0;
        float u0 = 0.0f, u1 = (float)TILE;
        float v0 = (float)(kind * TILE), v1 = (float)(kind * TILE + TILE);
        if (x0 < 0.0f)         { u0 += (-x0) * (float)TILE / w;               x0 = 0.0f; }
        if (y0 < 0.0f)         { v0 += (-y0) * (float)TILE / h;               y0 = 0.0f; }
        if (x1 > (float)SCR_W) { u1 -= (x1 - (float)SCR_W) * (float)TILE / w; x1 = (float)SCR_W; }
        if (y1 > (float)SCR_H) { v1 -= (y1 - (float)SCR_H) * (float)TILE / h; y1 = (float)SCR_H; }

        int ix0 = (int)(x0 + 0.5f), iy0 = (int)(y0 + 0.5f);
        int ix1 = (int)(x1 + 0.5f), iy1 = (int)(y1 + 0.5f);
        if (ix1 <= ix0 || iy1 <= iy0) continue; /* после округления пятно схлопнулось */

        spr_vtx_t *a = &v[n * 2];
        spr_vtx_t *b = &v[n * 2 + 1];
        a->u = (unsigned short)(u0 + 0.5f);
        a->v = (unsigned short)(v0 + 0.5f);
        a->color = s->color;
        a->x = (short)ix0;
        a->y = (short)iy0;
        a->z = 0;
        b->u = (unsigned short)(u1 + 0.5f);
        b->v = (unsigned short)(v1 + 0.5f);
        b->color = s->color; /* цвет спрайта GE берёт со второй вершины — ставим обеим */
        b->x = (short)ix1;
        b->y = (short)iy1;
        b->z = 0;
        n++;
    }
    if (n == 0) return;

    /* Аддитив не зависит от порядка — сортировка не нужна, весь кадр одним вызовом. */
    state_begin();
    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   n * 2, 0, v);
    state_end();
    s_count = n;
    s_calls = 1; /* все спрайты уходят одним вызовом */
}

int gu_sprite_last_count(void) { return s_count; }

int gu_sprite_last_calls(void) { return s_calls; }
