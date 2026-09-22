/* gu_text.c — вывод текста из frame_t на GPU: атлас T8 + CLUT на 256 записей +
 * 2D-спрайты. Раскладка строки живёт в core (font_layout), здесь только
 * состояние GE, отсечение по экрану и вершины. */
#include <pspgu.h>
#include <psputils.h>
#include <stddef.h>
#include "gu_text.h"

#define SCR_W 480
#define SCR_H 272

#define CLUT_ENTRIES 256u /* T8: индексы 0..255 */
#define CLUT_BLOCK   8u   /* sceGuClutLoad принимает число блоков по 8 записей */

#define QUADS_LINE  128 /* квадов на строку; FRAME_TEXT_LEN = 96 байт UTF-8 — с запасом */
#define QUADS_FRAME 1024 /* бюджет квадов на кадр, сверх него тихо обрезаем */

#define TEX_MIN 16  /* tbw для T8 выровнен по блокам в 16 пикселей */
#define TEX_MAX 512 /* предел размера текстуры на PSP */

/* Вершина 2D-спрайта. Порядок полей задан GE: texcoord → color → position
 * (pspgu.h: «Data members inside a vertex are laid out in the following order»).
 * Шаг вершины GE выравнивает по самому широкому полю (color, 4 байта) → 16 байт,
 * ровно sizeof этой структуры, поэтому паддинг в хвосте безопасен.
 * В GU_TRANSFORM_2D u, v — номера текселей (беззнаковые), x, y — пиксели экрана
 * (со знаком), масштаб текстуры не применяется; так же сделано в
 * pspsdk/src/samples/gu/doublelist и .../speed. */
typedef struct {
    unsigned short u, v;
    unsigned int color;
    short x, y, z;
} text_vtx_t;

_Static_assert(sizeof(text_vtx_t) == 16, "вершина текста: 16 байт (шаг вершины GE)");
_Static_assert(offsetof(text_vtx_t, color) == 4, "color идёт после texcoord");
_Static_assert(offsetof(text_vtx_t, x) == 8, "position идёт после color");

static const font_t *s_font;
static int s_quads; /* квадов отправлено на прошлом кадре */
static int s_calls; /* вызовов sceGuDrawArray на прошлом кадре */
/* CLUT читает GE напрямую — выравнивание 16 байт обязательно (sceGuClutLoad). */
static unsigned int __attribute__((aligned(16))) s_clut[CLUT_ENTRIES];

/* Размер текстуры: степень двойки в [TEX_MIN, TEX_MAX]. */
static int tex_dim_ok(int v) {
    return v >= TEX_MIN && v <= TEX_MAX && (v & (v - 1)) == 0;
}

int gu_text_init(const font_t *font) {
    s_font = NULL;
    s_quads = 0;
    s_calls = 0;
    if (!font || !font->pixels) return -1;
    if (!tex_dim_ok(font->tex_w) || !tex_dim_ok(font->tex_h)) return -1;
    /* sceGuTexImage: «Data must be aligned to 1 quad word (16 bytes)». */
    if ((((unsigned)(size_t)font->pixels) & 15u) != 0u) return -1;

    /* Запись i: альфа = значение T8, RGB белый (0xAABBGGRR). Цвет тексту даёт
     * вершина через GU_TFX_MODULATE: Cv = Ct * Cf, Av = At * Af. */
    for (unsigned i = 0; i < CLUT_ENTRIES; i++) {
        s_clut[i] = (i << 24) | 0x00FFFFFFu;
    }
    /* GE читает CLUT и атлас из ОЗУ мимо кэша CPU: без сброса увидит мусор.
     * Атлас пришёл из файла (тоже записан CPU), поэтому сбрасываем и его. */
    sceKernelDcacheWritebackRange(s_clut, (unsigned)sizeof(s_clut));
    sceKernelDcacheWritebackRange(font->pixels, (unsigned)font->tex_w * (unsigned)font->tex_h);

    s_font = font;
    return 0;
}

/* Состояние прохода выставляем полностью сами (CLAUDE.md п.9). */
static void state_begin(const font_t *font) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE); /* GU_TRUE = запись Z запрещена: наложение 2D */
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE); /* у спрайтов обхода нет */
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

    sceGuEnable(GU_TEXTURE_2D);
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);                     /* индекс = (T8 >> 0) & 0xff */
    sceGuClutLoad((int)(CLUT_ENTRIES / CLUT_BLOCK), s_clut);    /* 32 блока по 8 записей */
    sceGuTexMode(GU_PSM_T8, 0, 0, 0);                           /* без мип-уровней, без свизла */
    sceGuTexImage(0, font->tex_w, font->tex_h, font->tex_w, font->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST); /* текст рисуется 1:1, фильтр размыл бы его */
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f); /* нейтрально: в GU_TRANSFORM_2D не влияет (pspgu.h) */
    sceGuTexOffset(0.0f, 0.0f);
    /* Сменили страницу текстур — сбрасываем кэш текстур GE (pspgu.h: «Flush
     * texture page-cache»), иначе останутся тексели предыдущего прохода. */
    sceGuTexFlush();
}

/* Возврат к непрозрачному проходу следующего кадра. */
static void state_end(void) {
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE); /* GU_FALSE = запись Z разрешена */
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
}

/* Рисует одну строку; не более limit квадов. Возвращает число отправленных. */
static int draw_line(const frame_text_t *t, int limit) {
    font_quad_t quads[QUADS_LINE];

    if (limit <= 0 || t->utf8[0] == '\0') return 0;

    int pen_x = (int)t->x;
    if (t->align == TEXT_CENTER || t->align == TEXT_RIGHT) {
        int w = font_measure(s_font, (int)t->font, t->utf8);
        pen_x -= (t->align == TEXT_CENTER) ? w / 2 : w;
    }

    int n = font_layout(s_font, (int)t->font, t->utf8, pen_x, (int)t->y, quads, QUADS_LINE);
    if (n <= 0) return 0;
    if (n > limit) n = limit;

    /* Временные вершины — только через sceGuGetMemory: живут до sceGuFinish
     * (CLAUDE.md п.3). Выравнивание 16 байт вершинам GE не требуется. */
    text_vtx_t *v = (text_vtx_t *)sceGuGetMemory((int)(2u * (unsigned)n * sizeof(text_vtx_t)));
    if (!v) return 0;

    int out = 0;
    for (int i = 0; i < n; i++) {
        int x0 = (int)quads[i].x, y0 = (int)quads[i].y;
        int x1 = x0 + (int)quads[i].w, y1 = y0 + (int)quads[i].h;
        int u0 = (int)quads[i].u, v0 = (int)quads[i].v;

        if (x1 <= x0 || y1 <= y0) continue;                          /* пустой квад */
        if (x1 <= 0 || y1 <= 0 || x0 >= SCR_W || y0 >= SCR_H) continue; /* целиком вне экрана */

        /* Частично видимый квад обрезаем сами: в GU_TRANSFORM_2D координаты идут
         * в растеризатор как есть, отрицательные лучше не отдавать. Текст 1:1 с
         * атласом, поэтому сдвиг экранной координаты — тот же сдвиг текселя. */
        if (x0 < 0) { u0 -= x0; x0 = 0; }
        if (y0 < 0) { v0 -= y0; y0 = 0; }
        if (x1 > SCR_W) x1 = SCR_W;
        if (y1 > SCR_H) y1 = SCR_H;

        text_vtx_t *a = &v[out * 2];
        text_vtx_t *b = &v[out * 2 + 1];
        a->u = (unsigned short)u0;
        a->v = (unsigned short)v0;
        a->color = t->color;
        a->x = (short)x0;
        a->y = (short)y0;
        a->z = 0;
        b->u = (unsigned short)(u0 + (x1 - x0));
        b->v = (unsigned short)(v0 + (y1 - y0));
        b->color = t->color; /* цвет спрайта GE берёт со второй вершины — ставим обеим */
        b->x = (short)x1;
        b->y = (short)y1;
        b->z = 0;
        out++;
    }
    if (out == 0) return 0;

    sceGuDrawArray(GU_SPRITES,
                   GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   out * 2, 0, v);
    return out;
}

void gu_text_draw(const frame_t *f) {
    s_quads = 0;
    s_calls = 0;
    if (!s_font || !f || f->text_count <= 0) return;

    int count = f->text_count;
    if (count > FRAME_MAX_TEXTS) count = FRAME_MAX_TEXTS; /* пул фиксирован, не доверяем счётчику */

    state_begin(s_font);
    int budget = QUADS_FRAME;
    for (int i = 0; i < count && budget > 0; i++) {
        int drawn = draw_line(&f->texts[i], budget);
        budget -= drawn;
        s_quads += drawn;
        if (drawn > 0) s_calls++;
    }
    state_end();
}

int gu_text_last_quads(void) {
    return s_quads;
}

int gu_text_last_calls(void) { return s_calls; }
