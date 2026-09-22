/* test_font.c — юнит-тесты core/font.c. Основной blob формата AFNT собирается прямо здесь,
 * поэтому тесты не зависят от генератора шрифта. Запуск: make test. */
#include "minitest.h"
#include "font.h"
#include "frame.h"
#include "platform.h"
#include "tests.h"
#include <string.h>

/* --- Раскладка синтетического файла (все таблицы выровнены, пиксели кратны 16) --- */
#define FB_FACE_REC     32u   /* первая запись гарнитуры */
#define FB_G0           96u   /* глифы гарнитуры 0: 6 × 16 */
#define FB_K0           192u  /* пары кернинга гарнитуры 0: 2 × 8 */
#define FB_G1           208u  /* глифы гарнитуры 1: 3 × 16 */
#define FB_G2           260u  /* глифы гарнитуры 2: 1 × 16, нарочно кратно 4, но не 16 */
#define FB_PIXELS       288u
#define FB_TEX_W        64u
#define FB_TEX_H        64u
#define FB_LEN          (size_t)(FB_PIXELS + FB_TEX_W * FB_TEX_H)
#define FB_FACES        3u

/* Индексы глифов гарнитуры 0 (таблица отсортирована по codepoint). */
enum { G_SPACE = 0, G_QUEST = 1, G_A = 2, G_V = 3, G_BE = 4, G_REPL = 5 };

static void put_u16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void put_u32(unsigned char *p, unsigned v) {
    put_u16(p, v & 0xFFFFu);
    put_u16(p + 2, (v >> 16) & 0xFFFFu);
}

typedef struct {
    unsigned cp, u, v, w, h, adv;
    int xoff, yoff;
} gspec_t;

static void put_glyphs(unsigned char *b, size_t off, const gspec_t *g, int n) {
    for (int i = 0; i < n; i++) {
        unsigned char *p = b + off + (size_t)i * 16u;
        put_u32(p + 0, g[i].cp);
        put_u16(p + 4, g[i].u);
        put_u16(p + 6, g[i].v);
        p[8] = (unsigned char)g[i].w;
        p[9] = (unsigned char)g[i].h;
        p[10] = (unsigned char)(signed char)g[i].xoff;
        p[11] = (unsigned char)(signed char)g[i].yoff;
        p[12] = (unsigned char)g[i].adv;
        p[13] = p[14] = p[15] = 0; /* pad */
    }
}

static void put_kern(unsigned char *p, unsigned first, unsigned second, int dx) {
    put_u16(p + 0, first);
    put_u16(p + 2, second);
    p[4] = (unsigned char)(signed char)dx;
    p[5] = p[6] = p[7] = 0;
}

static void put_face(unsigned char *p, unsigned px, unsigned lh, int baseline,
                     unsigned gcount, unsigned goff, unsigned kcount, unsigned koff) {
    put_u16(p + 0, px);
    put_u16(p + 2, lh);
    put_u16(p + 4, (unsigned)baseline & 0xFFFFu);
    put_u16(p + 6, gcount);
    put_u32(p + 8, goff);
    put_u16(p + 12, kcount);
    put_u16(p + 14, 0);
    put_u32(p + 16, koff);
}

/* Корректный файл AFNT v1: 3 гарнитуры. 0 — полная (с пробелом, '?' и U+FFFD),
 * 1 — без U+FFFD (проверка отката на '?'), 2 — только 'A' (откат в NULL). */
static unsigned char *synth_font(void) {
    static const gspec_t g0[] = {
        { 0x20u,   0,  0,  0, 0,  4,  0,   0 },  /* пробел: пикселей нет */
        { 0x3Fu,   8,  0,  5, 7,  6,  0,  -7 },  /* '?' */
        { 0x41u,  16,  0,  6, 7,  8,  1,  -7 },  /* 'A' */
        { 0x56u,  24,  0,  6, 7,  8,  0,  -7 },  /* 'V' */
        { 0x411u, 32,  8,  5, 7,  7,  1,  -7 },  /* 'Б' */
        { 0xFFFDu, 40, 8,  7, 7,  9,  0,  -7 },  /* U+FFFD */
    };
    static const gspec_t g1[] = {
        { 0x3Fu,   0, 16, 12, 14, 13, 0, -14 },  /* '?' */
        { 0x41u,  16, 16, 12, 14, 14, 0, -14 },  /* 'A' */
        { 0x42u,  32, 16, 12, 14, 15, 1, -14 },  /* 'B' */
    };
    static const gspec_t g2[] = {
        { 0x41u,  48, 16,  4,  4,  5, 0,  -4 },  /* 'A' */
    };

    unsigned char *b = (unsigned char *)plat_alloc16(FB_LEN);
    if (!b) return NULL;
    memset(b, 0, FB_LEN);

    memcpy(b, "AFNT", 4);
    put_u32(b + 4, 1u);           /* версия */
    put_u32(b + 8, FB_TEX_W);
    put_u32(b + 12, FB_TEX_H);
    put_u32(b + 16, FB_FACES);
    put_u32(b + 20, FB_PIXELS);
    put_u32(b + 24, FB_TEX_W * FB_TEX_H);
    put_u32(b + 28, 0u);          /* резерв */

    put_face(b + FB_FACE_REC + 0,  8, 10,  8, 6, FB_G0, 2, FB_K0);
    put_face(b + FB_FACE_REC + 20, 16, 20, 16, 3, FB_G1, 0, 0);
    put_face(b + FB_FACE_REC + 40, 8, 10,  8, 1, FB_G2, 0, 0);

    put_glyphs(b, FB_G0, g0, 6);
    put_glyphs(b, FB_G1, g1, 3);
    put_glyphs(b, FB_G2, g2, 1);
    put_kern(b + FB_K0 + 0, G_A, G_V, -2);  /* ключ 0x00020003 */
    put_kern(b + FB_K0 + 8, G_V, G_A, -1);  /* ключ 0x00030002 */

    for (unsigned i = 0; i < FB_TEX_W * FB_TEX_H; i++) b[FB_PIXELS + i] = (unsigned char)(i & 0xFFu);
    return b;
}

/* Копия первых n байт корректного файла — для тестов обрезки (ASan ловит чтение за краем). */
static unsigned char *synth_trunc(size_t n) {
    unsigned char *full = synth_font();
    unsigned char *cut = (unsigned char *)plat_alloc16(n);
    if (full && cut) memcpy(cut, full, n);
    plat_free(full);
    return cut;
}

/* Ожидание отказа: blob остаётся за вызывающим, освобождаем его сами. */
#define REJECT(blob_expr, len_expr) do { \
    unsigned char *_b = (blob_expr); \
    font_t _f; \
    CHECK_EQ(font_load(&_f, _b, (size_t)(len_expr)), -1); \
    CHECK(_f.blob == NULL); \
    CHECK_EQ(_f.face_count, 0); \
    plat_free(_b); \
} while (0)

TEST(test_font_load_header) {
    font_t f;
    unsigned char *blob = synth_font();
    CHECK(blob != NULL);
    if (!blob) return;
    CHECK_EQ(font_load(&f, blob, FB_LEN), 0);
    CHECK_EQ(f.tex_w, (int)FB_TEX_W);
    CHECK_EQ(f.tex_h, (int)FB_TEX_H);
    CHECK_EQ(f.face_count, (int)FB_FACES);
    CHECK(f.blob == blob);
    CHECK(f.pixels == blob + FB_PIXELS);
    CHECK_EQ(((unsigned long)(size_t)f.pixels) & 15UL, 0); /* пиксели для GPU 16-выровнены */
    CHECK_EQ(f.pixels[0], 0);
    CHECK_EQ(f.pixels[5], 5);
    CHECK_EQ(f.pixels[FB_TEX_W * FB_TEX_H - 1], 255);

    const font_face_t *body = font_face(&f, FONT_BODY);
    const font_face_t *title = font_face(&f, FONT_TITLE);
    CHECK(body != NULL);
    CHECK(title != NULL);
    if (body) {
        CHECK_EQ(body->px_size, 8);
        CHECK_EQ(body->line_height, 10);
        CHECK_EQ(body->baseline, 8);
        CHECK_EQ(body->glyph_count, 6);
        CHECK_EQ(body->kern_count, 2);
        CHECK_EQ(body->glyphs[G_A].codepoint, 0x41u);
        CHECK_EQ(body->glyphs[G_A].u, 16);
        CHECK_EQ(body->glyphs[G_A].advance, 8);
        CHECK_EQ(body->glyphs[G_A].xoff, 1);
        CHECK_EQ(body->glyphs[G_A].yoff, -7);
        CHECK_EQ(body->glyphs[G_BE].codepoint, 0x411u);
        CHECK_EQ(body->glyphs[G_BE].v, 8);
    }
    if (title) {
        CHECK_EQ(title->px_size, 16);
        CHECK_EQ(title->glyph_count, 3);
        CHECK_EQ(title->kern_count, 0);
        CHECK(title->kerns == NULL);
    }
    /* таблица, выровненная только на 4 байта, читается на месте корректно */
    const font_face_t *f2 = font_face(&f, 2);
    CHECK(f2 != NULL);
    if (f2) {
        CHECK_EQ(((unsigned long)(size_t)f2->glyphs) & 3UL, 0);
        CHECK(((unsigned long)(size_t)f2->glyphs) & 15UL);
        CHECK_EQ(f2->glyphs[0].codepoint, 0x41u);
        CHECK_EQ(f2->glyphs[0].u, 48);
        CHECK_EQ(f2->glyphs[0].advance, 5);
        CHECK_EQ(f2->glyphs[0].yoff, -4);
    }
    /* за пределами face_count — NULL, без падения */
    CHECK(font_face(&f, -1) == NULL);
    CHECK(font_face(&f, (int)FB_FACES) == NULL);
    CHECK(font_face(&f, 999) == NULL);
    CHECK(font_face(NULL, 0) == NULL);

    font_free(&f);
    CHECK(f.blob == NULL);
    CHECK_EQ(f.face_count, 0);
    font_free(&f);   /* повторный вызов безопасен */
    font_free(NULL);
}

TEST(test_font_glyph_lookup) {
    font_t f;
    unsigned char *blob = synth_font();
    CHECK(blob != NULL);
    if (!blob) return;
    CHECK_EQ(font_load(&f, blob, FB_LEN), 0);
    const font_face_t *f0 = font_face(&f, 0);
    const font_face_t *f1 = font_face(&f, 1);
    const font_face_t *f2 = font_face(&f, 2);
    CHECK(f0 && f1 && f2);
    if (!f0 || !f1 || !f2) { font_free(&f); return; }

    /* точные попадания на всех концах таблицы */
    const font_glyph_t *g = font_glyph(f0, 0x20u);
    CHECK(g == &f0->glyphs[G_SPACE]);
    CHECK(font_glyph(f0, 0x41u) == &f0->glyphs[G_A]);
    CHECK(font_glyph(f0, 0x56u) == &f0->glyphs[G_V]);
    CHECK(font_glyph(f0, 0x411u) == &f0->glyphs[G_BE]);
    CHECK(font_glyph(f0, FONT_REPLACEMENT) == &f0->glyphs[G_REPL]);
    CHECK(font_glyph(f0, (unsigned)'?') == &f0->glyphs[G_QUEST]);

    /* промах: сначала U+FFFD */
    CHECK(font_glyph(f0, (unsigned)'z') == &f0->glyphs[G_REPL]);
    CHECK(font_glyph(f0, 0x19u) == &f0->glyphs[G_REPL]);      /* меньше первого */
    CHECK(font_glyph(f0, 0x10000u) == &f0->glyphs[G_REPL]);    /* больше последнего */
    /* промах без U+FFFD: откат на '?' */
    CHECK(font_glyph(f1, (unsigned)'z') == &f1->glyphs[0]);
    CHECK(font_glyph(f1, FONT_REPLACEMENT) == &f1->glyphs[0]);
    CHECK(font_glyph(f1, (unsigned)'B') == &f1->glyphs[2]);
    /* нет ни U+FFFD, ни '?' — NULL */
    CHECK(font_glyph(f2, (unsigned)'z') == NULL);
    CHECK(font_glyph(f2, (unsigned)'?') == NULL);
    CHECK(font_glyph(f2, FONT_REPLACEMENT) == NULL);
    CHECK(font_glyph(f2, (unsigned)'A') == &f2->glyphs[0]);
    CHECK(font_glyph(NULL, (unsigned)'A') == NULL);

    font_free(&f);
}

TEST(test_font_kern_lookup) {
    font_t f;
    unsigned char *blob = synth_font();
    CHECK(blob != NULL);
    if (!blob) return;
    CHECK_EQ(font_load(&f, blob, FB_LEN), 0);
    const font_face_t *f0 = font_face(&f, 0);
    const font_face_t *f1 = font_face(&f, 1);
    CHECK(f0 && f1);
    if (!f0 || !f1) { font_free(&f); return; }

    CHECK_EQ(font_kern(f0, G_A, G_V), -2);
    CHECK_EQ(font_kern(f0, G_V, G_A), -1);
    CHECK_EQ(font_kern(f0, G_A, G_A), 0);        /* пары нет */
    CHECK_EQ(font_kern(f0, G_SPACE, G_A), 0);
    CHECK_EQ(font_kern(f0, G_REPL, G_REPL), 0);
    CHECK_EQ(font_kern(f0, -1, G_A), 0);         /* мусорные индексы */
    CHECK_EQ(font_kern(f0, G_A, 70000), 0);
    CHECK_EQ(font_kern(f1, 0, 1), 0);            /* kern_count == 0 */
    CHECK_EQ(font_kern(NULL, 0, 1), 0);

    font_free(&f);
}

TEST(test_font_measure) {
    font_t f;
    unsigned char *blob = synth_font();
    CHECK(blob != NULL);
    if (!blob) return;
    CHECK_EQ(font_load(&f, blob, FB_LEN), 0);

    CHECK_EQ(font_measure(&f, 0, "A"), 8);
    CHECK_EQ(font_measure(&f, 0, "?"), 6);
    CHECK_EQ(font_measure(&f, 0, "AV"), 14);          /* 8 - 2 + 8: кернинг */
    CHECK_EQ(font_measure(&f, 0, "VA"), 15);          /* 8 - 1 + 8 */
    CHECK_EQ(font_measure(&f, 0, "A V"), 20);         /* 8 + 4 + 8, пар нет */
    CHECK_EQ(font_measure(&f, 0, "AVA"), 8 - 2 + 8 - 1 + 8);
    CHECK_EQ(font_measure(&f, 0, "\xD0\x91"), 7);     /* 'Б' */
    CHECK_EQ(font_measure(&f, 0, "z"), 9);            /* откат на U+FFFD */
    CHECK_EQ(font_measure(&f, 0, "\xFF"), 9);         /* битый байт → U+FFFD */
    CHECK_EQ(font_measure(&f, 1, "A"), 14);
    CHECK_EQ(font_measure(&f, 2, "AB"), 5);           /* 'B' без отката — пропущен */

    /* пограничные случаи: 0 без падения */
    CHECK_EQ(font_measure(&f, 0, ""), 0);
    CHECK_EQ(font_measure(&f, 0, NULL), 0);
    CHECK_EQ(font_measure(&f, -1, "A"), 0);
    CHECK_EQ(font_measure(&f, 3, "A"), 0);
    CHECK_EQ(font_measure(&f, 99, "A"), 0);
    CHECK_EQ(font_measure(NULL, 0, "A"), 0);

    font_free(&f);
}

TEST(test_font_layout) {
    font_t f;
    font_quad_t q[8];
    unsigned char *blob = synth_font();
    CHECK(blob != NULL);
    if (!blob) return;
    CHECK_EQ(font_load(&f, blob, FB_LEN), 0);

    /* "AV" от пера (10, 100): второй глиф сдвинут кернингом на -2 */
    memset(q, 0x7F, sizeof q);
    CHECK_EQ(font_layout(&f, 0, "AV", 10, 100, q, 8), 2);
    CHECK_EQ(q[0].x, 11);   /* 10 + xoff 1 */
    CHECK_EQ(q[0].y, 93);   /* 100 + yoff -7 */
    CHECK_EQ(q[0].w, 6);
    CHECK_EQ(q[0].h, 7);
    CHECK_EQ(q[0].u, 16);
    CHECK_EQ(q[0].v, 0);
    CHECK_EQ(q[1].x, 16);   /* 10 + 8 - 2 + xoff 0 */
    CHECK_EQ(q[1].y, 93);
    CHECK_EQ(q[1].u, 24);
    /* конец строки совпадает с font_measure */
    CHECK_EQ(q[1].x + font_glyph(font_face(&f, 0), 'V')->advance, 10 + font_measure(&f, 0, "AV"));

    /* пробел квада не даёт, но перо двигает */
    memset(q, 0x7F, sizeof q);
    CHECK_EQ(font_layout(&f, 0, "A V", 0, 50, q, 8), 2);
    CHECK_EQ(q[0].x, 1);
    CHECK_EQ(q[0].y, 43);
    CHECK_EQ(q[1].x, 12);   /* 8 (A) + 4 (пробел) + xoff 0 */
    CHECK_EQ(q[1].u, 24);

    /* откат на U+FFFD для неизвестного символа */
    memset(q, 0x7F, sizeof q);
    CHECK_EQ(font_layout(&f, 0, "z", 5, 20, q, 8), 1);
    CHECK_EQ(q[0].x, 5);
    CHECK_EQ(q[0].y, 13);
    CHECK_EQ(q[0].w, 7);
    CHECK_EQ(q[0].u, 40);
    CHECK_EQ(q[0].v, 8);

    /* max не превышаем, перо продолжает считать */
    memset(q, 0x7F, sizeof q);
    CHECK_EQ(font_layout(&f, 0, "AVA", 10, 100, q, 1), 1);
    CHECK_EQ(q[0].x, 11);
    CHECK_EQ(q[1].x, 0x7F7F);  /* хвост не тронут */
    memset(q, 0x7F, sizeof q);
    CHECK_EQ(font_layout(&f, 0, "AVA", 10, 100, q, 2), 2);
    CHECK_EQ(q[1].x, 16);
    CHECK_EQ(q[2].x, 0x7F7F);

    /* отрицательные координаты пера допустимы */
    CHECK_EQ(font_layout(&f, 0, "A", -20, -5, q, 8), 1);
    CHECK_EQ(q[0].x, -19);
    CHECK_EQ(q[0].y, -12);

    /* пограничные случаи: 0 без падения */
    CHECK_EQ(font_layout(&f, 0, "", 0, 0, q, 8), 0);
    CHECK_EQ(font_layout(&f, 0, NULL, 0, 0, q, 8), 0);
    CHECK_EQ(font_layout(&f, 0, "AV", 0, 0, NULL, 8), 0);
    CHECK_EQ(font_layout(&f, 0, "AV", 0, 0, q, 0), 0);
    CHECK_EQ(font_layout(&f, 0, "AV", 0, 0, q, -3), 0);
    CHECK_EQ(font_layout(&f, 7, "AV", 0, 0, q, 8), 0);
    CHECK_EQ(font_layout(&f, -1, "AV", 0, 0, q, 8), 0);
    CHECK_EQ(font_layout(NULL, 0, "AV", 0, 0, q, 8), 0);
    /* только пробелы — квадов нет */
    CHECK_EQ(font_layout(&f, 0, "   ", 0, 0, q, 8), 0);
    /* глиф без отката пропускается целиком */
    CHECK_EQ(font_layout(&f, 2, "zAz", 0, 0, q, 8), 1);
    CHECK_EQ(q[0].x, 0);

    font_free(&f);
}

TEST(test_utf8_valid) {
    const char *p = "A";
    const char *start = p;
    CHECK_EQ(utf8_next(&p), 0x41);
    CHECK_EQ(p - start, 1);
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - start, 1);          /* в конце строки указатель не двигаем */
    CHECK_EQ(utf8_next(&p), -1);     /* повторно — тоже -1 */

    p = start = "";
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - start, 0);

    /* латиница по кодпоинтам */
    p = start = "Hi!";
    CHECK_EQ(utf8_next(&p), 'H');
    CHECK_EQ(utf8_next(&p), 'i');
    CHECK_EQ(utf8_next(&p), '!');
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - start, 3);

    /* 2 байта: кириллица "Привет" */
    p = start = "\xD0\x9F\xD1\x80\xD0\xB8";
    CHECK_EQ(utf8_next(&p), 0x41F);  /* П */
    CHECK_EQ(p - start, 2);
    CHECK_EQ(utf8_next(&p), 0x440);  /* р */
    CHECK_EQ(utf8_next(&p), 0x438);  /* и */
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - start, 6);

    /* границы диапазонов */
    p = "\xC2\x80";                  /* U+0080 — минимум для 2 байт */
    CHECK_EQ(utf8_next(&p), 0x80);
    p = "\xDF\xBF";                  /* U+07FF — максимум */
    CHECK_EQ(utf8_next(&p), 0x7FF);

    /* 3 байта */
    p = start = "\xE2\x82\xAC";      /* € U+20AC */
    CHECK_EQ(utf8_next(&p), 0x20AC);
    CHECK_EQ(p - start, 3);
    p = "\xE0\xA0\x80";              /* U+0800 — минимум */
    CHECK_EQ(utf8_next(&p), 0x800);
    p = "\xEF\xBF\xBD";              /* U+FFFD */
    CHECK_EQ(utf8_next(&p), (int)FONT_REPLACEMENT);
    p = "\xEF\xBF\xBF";              /* U+FFFF */
    CHECK_EQ(utf8_next(&p), 0xFFFF);

    /* 4 байта */
    p = start = "\xF0\x9F\x98\x80";  /* U+1F600 */
    CHECK_EQ(utf8_next(&p), 0x1F600);
    CHECK_EQ(p - start, 4);
    p = "\xF0\x90\x80\x80";          /* U+10000 — минимум */
    CHECK_EQ(utf8_next(&p), 0x10000);
    p = "\xF4\x8F\xBF\xBF";          /* U+10FFFF — максимум Unicode */
    CHECK_EQ(utf8_next(&p), 0x10FFFF);

    /* смешанная строка целиком */
    p = start = "a\xD0\x91\xE2\x82\xAC\xF0\x9F\x98\x80";
    CHECK_EQ(utf8_next(&p), 'a');
    CHECK_EQ(utf8_next(&p), 0x411);
    CHECK_EQ(utf8_next(&p), 0x20AC);
    CHECK_EQ(utf8_next(&p), 0x1F600);
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - start, 10);

    CHECK_EQ(utf8_next(NULL), -1);
}

/* Битый вход: FONT_REPLACEMENT и продвижение ровно на 1 байт. */
#define BAD_ONE(str) do { \
    const char *_p = (str), *_s = _p; \
    CHECK_EQ(utf8_next(&_p), (int)FONT_REPLACEMENT); \
    CHECK_EQ(_p - _s, 1); \
} while (0)

TEST(test_utf8_invalid) {
    BAD_ONE("\x80");                  /* одиночный продолжающий байт */
    BAD_ONE("\xBF");
    BAD_ONE("\xFF");                  /* не бывает в UTF-8 */
    BAD_ONE("\xFE\xBF");
    BAD_ONE("\xF8\x88\x80\x80\x80");  /* 5-байтовая последовательность */
    BAD_ONE("\xD0");                  /* обрубленная 2-байтовая */
    BAD_ONE("\xD0" "A");              /* продолжения нет */
    BAD_ONE("\xE2\x82");              /* обрубленная 3-байтовая */
    BAD_ONE("\xE2");
    BAD_ONE("\xF0\x9F\x98");          /* обрубленная 4-байтовая */
    BAD_ONE("\xC0\x80");              /* overlong U+0000 */
    BAD_ONE("\xC1\xBF");              /* overlong U+007F */
    BAD_ONE("\xE0\x80\x80");          /* overlong 3 байта */
    BAD_ONE("\xF0\x80\x80\x80");      /* overlong 4 байта */
    BAD_ONE("\xED\xA0\x80");          /* суррогат U+D800 */
    BAD_ONE("\xED\xBF\xBF");          /* суррогат U+DFFF */
    BAD_ONE("\xF4\x90\x80\x80");      /* U+110000 — за пределом Unicode */
    BAD_ONE("\xF5\x80\x80\x80");
    BAD_ONE("\xF7\xBF\xBF\xBF");

    /* после битого байта разбор продолжается со следующего */
    const char *p = "\xD0" "A", *s = p;
    CHECK_EQ(utf8_next(&p), (int)FONT_REPLACEMENT);
    CHECK_EQ(utf8_next(&p), 'A');
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - s, 2);

    /* хвост обрубленной 3-байтовой разбирается байт за байтом */
    p = s = "\xE2\x82";
    CHECK_EQ(utf8_next(&p), (int)FONT_REPLACEMENT);
    CHECK_EQ(utf8_next(&p), (int)FONT_REPLACEMENT);
    CHECK_EQ(utf8_next(&p), -1);
    CHECK_EQ(p - s, 2);
}

TEST(test_font_load_bad) {
    font_t f;
    /* пустой вход */
    CHECK_EQ(font_load(&f, NULL, FB_LEN), -1);
    CHECK_EQ(font_load(NULL, NULL, 0), -1);

    /* мусор вместо файла */
    unsigned char *junk = (unsigned char *)plat_alloc16(FB_LEN);
    if (junk) { memset(junk, 0xA5, FB_LEN); memcpy(junk, "JUNK", 4); }
    REJECT(junk, FB_LEN);

    /* нули */
    unsigned char *zeros = (unsigned char *)plat_alloc16(FB_LEN);
    if (zeros) memset(zeros, 0, FB_LEN);
    REJECT(zeros, FB_LEN);

    /* короче заголовка */
    REJECT(synth_trunc(16), 16);
    REJECT(synth_font(), 31);
    REJECT(synth_font(), 0);

    /* обрезанный файл: заголовок цел, таблиц и пикселей нет */
    REJECT(synth_trunc(96), 96);
    REJECT(synth_trunc(208), 208);
    REJECT(synth_font(), FB_LEN - 16);          /* пиксели не влезают */
    REJECT(synth_font(), FB_PIXELS);

    /* битая версия */
    unsigned char *b = synth_font();
    if (b) put_u32(b + 4, 2u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 4, 0u);
    REJECT(b, FB_LEN);

    /* невыровненный pixels_offset (влезает, но не кратен 16) */
    b = synth_font();
    if (b) put_u32(b + 20, FB_PIXELS - 8u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 20, FB_PIXELS + 1u);
    REJECT(b, FB_LEN);

    /* face_count вне диапазона */
    b = synth_font();
    if (b) put_u32(b + 16, (unsigned)FONT_MAX_FACES + 1u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 16, 0u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 16, 0xFFFFFFFFu);
    REJECT(b, FB_LEN);

    /* размер атласа не степень двойки / больше предела PSP */
    b = synth_font();
    if (b) put_u32(b + 8, 63u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 12, 1024u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 12, 0u);
    REJECT(b, FB_LEN);

    /* pixels_size меньше атласа и с переполнением при сложении */
    b = synth_font();
    if (b) put_u32(b + 24, FB_TEX_W * FB_TEX_H - 1u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 24, 0xFFFFFFF0u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + 20, 0xFFFFFFF0u);
    REJECT(b, FB_LEN);

    /* glyph_offset: не кратен 4, за краем, с переполнением */
    b = synth_font();
    if (b) put_u32(b + FB_FACE_REC + 8, FB_G0 + 1u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + FB_FACE_REC + 8, FB_G0 + 2u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + FB_FACE_REC + 8, (unsigned)FB_LEN);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + FB_FACE_REC + 8, 0xFFFFFFF0u);
    REJECT(b, FB_LEN);
    /* glyph_count больше, чем влезает в файл */
    b = synth_font();
    if (b) put_u16(b + FB_FACE_REC + 6, 0xFFFFu);
    REJECT(b, FB_LEN);

    /* kern_offset за краем / невыровнен */
    b = synth_font();
    if (b) put_u32(b + FB_FACE_REC + 16, (unsigned)FB_LEN - 8u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u32(b + FB_FACE_REC + 16, FB_K0 + 1u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u16(b + FB_FACE_REC + 12, 0xFFFFu);
    REJECT(b, FB_LEN);

    /* нулевые метрики гарнитуры (в том числе у последней) */
    b = synth_font();
    if (b) put_u16(b + FB_FACE_REC + 0, 0u);
    REJECT(b, FB_LEN);
    b = synth_font();
    if (b) put_u16(b + FB_FACE_REC + 40 + 2, 0u);
    REJECT(b, FB_LEN);

    /* записи гарнитур не влезают в файл */
    b = synth_trunc(48);
    REJECT(b, 48);

    /* буфер не 16-выровнен: такие пиксели нельзя отдать GPU */
    unsigned char *base = (unsigned char *)plat_alloc16(FB_LEN + 16u);
    unsigned char *src = synth_font();
    if (base && src) {
        memcpy(base + 8, src, FB_LEN);
        font_t off;
        CHECK_EQ(font_load(&off, base + 8, FB_LEN), -1);
        CHECK(off.blob == NULL);
    }
    plat_free(src);
    plat_free(base);
}

/* Реальный файл делает другой инструмент; если его ещё нет — тест молча проходит. */
TEST(test_font_real_file) {
    size_t len = 0;
    void *blob = plat_read_file("data/font.bin", &len);
    if (!blob) {
        printf("\n    data/font.bin отсутствует — проверка пропущена\n");
        return;
    }
    font_t f;
    CHECK_EQ(font_load(&f, blob, len), 0);
    if (!f.blob) { plat_free(blob); return; }

    CHECK(f.tex_w > 0 && f.tex_w <= 512);
    CHECK(f.tex_h > 0 && f.tex_h <= 512);
    CHECK_EQ(((unsigned long)(size_t)f.pixels) & 15UL, 0);
    CHECK(f.face_count >= 1 && f.face_count <= FONT_MAX_FACES);

    for (int i = 0; i < f.face_count; i++) {
        const font_face_t *fa = font_face(&f, i);
        CHECK(fa != NULL);
        if (!fa) continue;
        CHECK(fa->px_size > 0);
        CHECK(fa->line_height >= fa->px_size);
        CHECK(fa->baseline > 0 && fa->baseline <= fa->line_height);
        CHECK(fa->glyph_count > 0);
        int unsorted = 0, outside = 0;
        for (int j = 0; j < fa->glyph_count; j++) {
            const font_glyph_t *g = &fa->glyphs[j];
            if (j > 0 && fa->glyphs[j - 1].codepoint >= g->codepoint) unsorted++;
            if ((int)g->u + (int)g->w > f.tex_w || (int)g->v + (int)g->h > f.tex_h) outside++;
        }
        CHECK_EQ(unsorted, 0);
        CHECK_EQ(outside, 0);
        int bad_pair = 0;
        unsigned prev_key = 0;
        for (int j = 0; j < fa->kern_count; j++) {
            const font_kern_t *k = &fa->kerns[j];
            unsigned key = ((unsigned)k->first << 16) | (unsigned)k->second;
            if ((int)k->first >= fa->glyph_count || (int)k->second >= fa->glyph_count) bad_pair++;
            if (j > 0 && key <= prev_key) bad_pair++;
            prev_key = key;
        }
        CHECK_EQ(bad_pair, 0);
        /* латиница и кириллица должны находиться (хотя бы через откат) */
        CHECK(font_glyph(fa, (unsigned)'A') != NULL);
        CHECK(font_glyph(fa, 0x410u) != NULL);
    }

    /* раскладка реальной строки не выходит за атлас */
    font_quad_t q[FRAME_TEXT_LEN];
    const char *sample = "ARGUS — Аргус 0123";
    int w = font_measure(&f, FONT_BODY, sample);
    CHECK(w > 0);
    int n = font_layout(&f, FONT_BODY, sample, 8, 200, q, FRAME_TEXT_LEN);
    CHECK(n > 0);
    int off_atlas = 0, off_pen = 0;
    for (int i = 0; i < n; i++) {
        if (q[i].u + q[i].w > f.tex_w || q[i].v + q[i].h > f.tex_h) off_atlas++;
        if (q[i].w <= 0 || q[i].h <= 0) off_atlas++;
        /* квады идут слева направо внутри измеренной ширины (с запасом на xoff) */
        if (q[i].x < 8 - 16 || q[i].x > 8 + w + 16) off_pen++;
        if (i > 0 && q[i].x < q[i - 1].x) off_pen++;
    }
    CHECK_EQ(off_atlas, 0);
    CHECK_EQ(off_pen, 0);

    font_free(&f);
    CHECK(f.blob == NULL);
}

void tests_font(void) {
    puts("font tests");
    RUN(test_font_load_header);
    RUN(test_font_glyph_lookup);
    RUN(test_font_kern_lookup);
    RUN(test_font_measure);
    RUN(test_font_layout);
    RUN(test_utf8_valid);
    RUN(test_utf8_invalid);
    RUN(test_font_load_bad);
    RUN(test_font_real_file);
}
