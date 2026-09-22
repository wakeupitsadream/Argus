/* font.h — шрифтовой атлас (font.bin, формат AFNT v1 из docs/FORMATS.md): загрузка,
 * поиск глифов, кернинг, декодирование UTF-8, раскладка строки в квады.
 * Раскладка живёт в core (тестируется на хосте), платформа только рисует квады. */
#ifndef ARGUS_FONT_H
#define ARGUS_FONT_H
#include <stddef.h>

#define FONT_MAX_FACES 4
#define FONT_REPLACEMENT 0xFFFDu

/* Структуры повторяют раскладку файла — читаются на месте, без копирования. */
typedef struct {
    unsigned codepoint;
    unsigned short u, v;
    unsigned char w, h;
    signed char xoff, yoff;
    unsigned char advance, pad;
} font_glyph_t; /* 16 байт */

typedef struct {
    unsigned short first, second; /* индексы глифов в таблице гарнитуры */
    signed char dx;
    unsigned char pad[3];
} font_kern_t; /* 8 байт */

typedef struct {
    int px_size, line_height, baseline;
    int glyph_count, kern_count;
    const font_glyph_t *glyphs; /* отсортированы по codepoint */
    const font_kern_t *kerns;   /* отсортированы по (first << 16 | second) */
} font_face_t;

typedef struct {
    int tex_w, tex_h, face_count;
    const unsigned char *pixels; /* T8-альфа, 16-выровнена */
    font_face_t faces[FONT_MAX_FACES];
    void *blob;                  /* буфер файла во владении шрифта */
} font_t;

/* Квад одного глифа: экранный прямоугольник и левый верхний угол в атласе. */
typedef struct {
    short x, y, w, h;
    unsigned short u, v;
} font_quad_t;

/* Принимает буфер файла во владение. 0 при успехе, -1 при неверном формате. */
int font_load(font_t *f, void *blob, size_t len);
void font_free(font_t *f);

const font_face_t *font_face(const font_t *f, int face_idx);
/* Глиф по кодпоинту; при отсутствии — глиф FONT_REPLACEMENT, иначе '?', иначе NULL. */
const font_glyph_t *font_glyph(const font_face_t *face, unsigned codepoint);
/* Кернинг между глифами по их индексам в таблице гарнитуры; 0, если пары нет. */
int font_kern(const font_face_t *face, int glyph_a, int glyph_b);

/* Следующий кодпоинт UTF-8; продвигает *p. Возвращает -1 в конце строки,
 * FONT_REPLACEMENT на некорректной последовательности (продвинув на 1 байт). */
int utf8_next(const char **p);

/* Ширина строки в пикселях (с кернингом), 0 при ошибке. */
int font_measure(const font_t *f, int face_idx, const char *utf8);

/* Раскладывает строку в квады. (x, y) — точка пера: x слева, y — базовая линия.
 * Возвращает число заполненных квадов (≤ max); пробелы и глифы без пикселей пропускаются. */
int font_layout(const font_t *f, int face_idx, const char *utf8, int x, int y,
                font_quad_t *out, int max);

#endif
