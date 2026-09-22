/* font.c — шрифтовой атлас AFNT v1: загрузка на месте, поиск глифов, кернинг,
 * декодирование UTF-8 и раскладка строки в квады. Без malloc: font_load только
 * раскладывает указатели внутрь blob, память освобождает font_free. */
#include "font.h"
#include "platform.h"
#include <stdint.h>
#include <string.h>

#define FNT_MAGIC    "AFNT"
#define FNT_VERSION  1u
#define FNT_HEADER   32u  /* размер заголовка файла */
#define FNT_FACE_REC 20u  /* размер записи гарнитуры */
#define FNT_TEX_MAX  512u /* предел размера текстуры на PSP */

/* Структуры повторяют раскладку файла — иначе чтение на месте неверно. */
_Static_assert(sizeof(font_glyph_t) == 16, "font_glyph_t: 16 байт");
_Static_assert(sizeof(font_kern_t) == 8, "font_kern_t: 8 байт");
_Static_assert(offsetof(font_glyph_t, u) == 4, "font_glyph_t: u по смещению 4");
_Static_assert(offsetof(font_glyph_t, w) == 8, "font_glyph_t: w по смещению 8");
_Static_assert(offsetof(font_glyph_t, advance) == 12, "font_glyph_t: advance по смещению 12");
_Static_assert(offsetof(font_kern_t, dx) == 4, "font_kern_t: dx по смещению 4");

/* Чтение little-endian побайтово: смещения в файле не выровнены (запись гарнитуры
 * 20 байт), а на MIPS невыровненный доступ к u32/u16 приводит к исключению. */
static unsigned rd_u16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned rd_u32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static int rd_i16(const unsigned char *p) {
    unsigned v = rd_u16(p);
    return v >= 0x8000u ? (int)v - 0x10000 : (int)v;
}

/* Лежит ли [off, off + size) внутри len. Считаем в u32 — ширине смещений в файле,
 * поэтому на 32-битном PSP и на хосте результат один и тот же. Вычитание, а не
 * сложение: off + size переполнило бы u32 и пропустило битый файл. */
static int fits(size_t len, unsigned off, unsigned size) {
    if (len > (size_t)0xFFFFFFFFu) len = (size_t)0xFFFFFFFFu; /* на PSP не срабатывает */
    unsigned cap = (unsigned)len;
    return off <= cap && size <= cap - off;
}

static int is_pot(unsigned v) {
    return v != 0u && (v & (v - 1u)) == 0u;
}

/* Разбирает запись гарнитуры r (20 байт) и проверяет её таблицы. 0 при успехе. */
static int read_face(font_face_t *fa, const unsigned char *base, size_t len, const unsigned char *r) {
    unsigned glyph_count = rd_u16(r + 6);
    unsigned glyph_offset = rd_u32(r + 8);
    unsigned kern_count = rd_u16(r + 12);
    unsigned kern_offset = rd_u32(r + 16);

    fa->px_size = (int)rd_u16(r + 0);
    fa->line_height = (int)rd_u16(r + 2);
    fa->baseline = rd_i16(r + 4);
    if (fa->px_size <= 0 || fa->line_height <= 0) return -1;

    /* Чтение на месте законно только при выравнивании таблицы: 4 байта для глифов, 2 для пар.
     * Размеры таблиц в u32 не переполняются: счётчики читаются из u16. */
    if (glyph_count > 0u) {
        if (glyph_offset % _Alignof(font_glyph_t) != 0u) return -1;
        if (!fits(len, glyph_offset, glyph_count * (unsigned)sizeof(font_glyph_t))) return -1;
        fa->glyphs = (const font_glyph_t *)(const void *)(base + glyph_offset);
    }
    fa->glyph_count = (int)glyph_count;

    if (kern_count > 0u) {
        if (kern_offset % _Alignof(font_kern_t) != 0u) return -1;
        if (!fits(len, kern_offset, kern_count * (unsigned)sizeof(font_kern_t))) return -1;
        fa->kerns = (const font_kern_t *)(const void *)(base + kern_offset);
    }
    fa->kern_count = (int)kern_count;
    return 0;
}

int font_load(font_t *f, void *blob, size_t len) {
    if (!f) return -1;
    memset(f, 0, sizeof *f);
    if (!blob || len < FNT_HEADER) return -1;
    /* Пиксели идут в GPU, поэтому весь буфер обязан быть 16-выровнен (plat_alloc16). */
    if (((uintptr_t)blob & 15u) != 0u) return -1;

    const unsigned char *b = (const unsigned char *)blob;
    if (memcmp(b, FNT_MAGIC, 4) != 0) return -1;
    if (rd_u32(b + 4) != FNT_VERSION) return -1;

    unsigned tex_w = rd_u32(b + 8), tex_h = rd_u32(b + 12);
    unsigned face_count = rd_u32(b + 16);
    unsigned pixels_offset = rd_u32(b + 20), pixels_size = rd_u32(b + 24);

    if (face_count == 0u || face_count > (unsigned)FONT_MAX_FACES) return -1;
    if (!is_pot(tex_w) || !is_pot(tex_h) || tex_w > FNT_TEX_MAX || tex_h > FNT_TEX_MAX) return -1;
    if ((pixels_offset & 15u) != 0u) return -1;
    if (!fits(len, pixels_offset, pixels_size)) return -1;
    if (pixels_size < (size_t)tex_w * (size_t)tex_h) return -1;
    if (!fits(len, FNT_HEADER, face_count * FNT_FACE_REC)) return -1;

    /* Заполняем копию: при отказе *f остаётся нулевым, blob — за вызывающим. */
    font_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (unsigned i = 0; i < face_count; i++) {
        if (read_face(&tmp.faces[i], b, len, b + FNT_HEADER + i * FNT_FACE_REC) != 0) return -1;
    }
    tmp.tex_w = (int)tex_w;
    tmp.tex_h = (int)tex_h;
    tmp.face_count = (int)face_count;
    tmp.pixels = b + pixels_offset;
    tmp.blob = blob;
    *f = tmp;
    return 0;
}

void font_free(font_t *f) {
    if (!f) return;
    if (f->blob) plat_free(f->blob);
    memset(f, 0, sizeof *f);
}

const font_face_t *font_face(const font_t *f, int face_idx) {
    if (!f || face_idx < 0 || face_idx >= f->face_count || face_idx >= FONT_MAX_FACES) return NULL;
    return &f->faces[face_idx];
}

/* Двоичный поиск по codepoint; NULL, если точного глифа нет. */
static const font_glyph_t *find_glyph(const font_face_t *face, unsigned codepoint) {
    int lo = 0, hi = face->glyph_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        unsigned c = face->glyphs[mid].codepoint;
        if (c < codepoint) lo = mid + 1;
        else if (c > codepoint) hi = mid - 1;
        else return &face->glyphs[mid];
    }
    return NULL;
}

const font_glyph_t *font_glyph(const font_face_t *face, unsigned codepoint) {
    if (!face || !face->glyphs || face->glyph_count <= 0) return NULL;
    const font_glyph_t *g = find_glyph(face, codepoint);
    if (g) return g;
    if (codepoint != FONT_REPLACEMENT) {
        g = find_glyph(face, FONT_REPLACEMENT);
        if (g) return g;
    }
    if (codepoint != (unsigned)'?') {
        g = find_glyph(face, (unsigned)'?');
        if (g) return g;
    }
    return NULL;
}

int font_kern(const font_face_t *face, int glyph_a, int glyph_b) {
    if (!face || !face->kerns || face->kern_count <= 0) return 0;
    if (glyph_a < 0 || glyph_b < 0 || glyph_a > 0xFFFF || glyph_b > 0xFFFF) return 0;
    unsigned key = ((unsigned)glyph_a << 16) | (unsigned)glyph_b;
    int lo = 0, hi = face->kern_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const font_kern_t *k = &face->kerns[mid];
        unsigned k_key = ((unsigned)k->first << 16) | (unsigned)k->second;
        if (k_key < key) lo = mid + 1;
        else if (k_key > key) hi = mid - 1;
        else return k->dx;
    }
    return 0;
}

/* Некорректная последовательность: продвигаем ровно на байт и отдаём заменяющий символ. */
static int utf8_bad(const char **p, const unsigned char *s) {
    *p = (const char *)(s + 1);
    return (int)FONT_REPLACEMENT;
}

int utf8_next(const char **p) {
    if (!p || !*p) return -1;
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = s[0];
    if (c == 0u) return -1; /* конец строки: указатель не двигаем */
    if (c < 0x80u) {
        *p = (const char *)(s + 1);
        return (int)c;
    }
    int tail;
    unsigned cp, lowest;
    if (c >= 0xC0u && c <= 0xDFu)      { tail = 1; cp = c & 0x1Fu; lowest = 0x80u; }
    else if (c >= 0xE0u && c <= 0xEFu) { tail = 2; cp = c & 0x0Fu; lowest = 0x800u; }
    else if (c >= 0xF0u && c <= 0xF7u) { tail = 3; cp = c & 0x07u; lowest = 0x10000u; }
    else return utf8_bad(p, s); /* одиночный продолжающий байт или 0xF8…0xFF */

    for (int i = 1; i <= tail; i++) {
        unsigned cc = s[i]; /* на нуле выйдем здесь же: за конец строки не читаем */
        if ((cc & 0xC0u) != 0x80u) return utf8_bad(p, s);
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if (cp < lowest) return utf8_bad(p, s);                   /* переслишком длинная запись */
    if (cp >= 0xD800u && cp <= 0xDFFFu) return utf8_bad(p, s); /* половина суррогатной пары */
    if (cp > 0x10FFFFu) return utf8_bad(p, s);
    *p = (const char *)(s + tail + 1);
    return (int)cp;
}

int font_measure(const font_t *f, int face_idx, const char *utf8) {
    const font_face_t *face = font_face(f, face_idx);
    if (!face || !utf8) return 0;
    int pen = 0, prev = -1;
    const char *p = utf8;
    for (;;) {
        int cp = utf8_next(&p);
        if (cp < 0) break;
        const font_glyph_t *g = font_glyph(face, (unsigned)cp);
        if (!g) { prev = -1; continue; } /* нечего рисовать и нечем двигать перо */
        int idx = (int)(g - face->glyphs);
        if (prev >= 0) pen += font_kern(face, prev, idx);
        pen += g->advance;
        prev = idx;
    }
    return pen;
}

int font_layout(const font_t *f, int face_idx, const char *utf8, int x, int y,
                font_quad_t *out, int max) {
    const font_face_t *face = font_face(f, face_idx);
    if (!face || !utf8 || !out || max <= 0) return 0;
    int pen = x, count = 0, prev = -1;
    const char *p = utf8;
    for (;;) {
        int cp = utf8_next(&p);
        if (cp < 0) break;
        const font_glyph_t *g = font_glyph(face, (unsigned)cp);
        if (!g) { prev = -1; continue; }
        int idx = (int)(g - face->glyphs);
        if (prev >= 0) pen += font_kern(face, prev, idx);
        /* Пустые глифы (пробел) двигают перо, но квада не дают; за max не выходим. */
        if (g->w != 0u && g->h != 0u && count < max) {
            font_quad_t *q = &out[count++];
            q->x = (short)(pen + g->xoff);
            q->y = (short)(y + g->yoff);
            q->w = (short)g->w;
            q->h = (short)g->h;
            q->u = g->u;
            q->v = g->v;
        }
        pen += g->advance;
        prev = idx;
    }
    return count;
}
