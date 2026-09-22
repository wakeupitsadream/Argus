/* palette.h — палитры регионов из build/assets/palettes.pal (формат APAL v1). */
#ifndef ARGUS_PALETTE_H
#define ARGUS_PALETTE_H
#include <stddef.h>

#define PAL_SLOTS 8
enum { SLOT_TOP = 0, SLOT_TOP_ALT, SLOT_WALL, SLOT_ACCENT, SLOT_SHADOW, SLOT_GLOW, SLOT_WATER, SLOT_EXTRA };

/* Раскладка совпадает с файлом: 72 байта, little-endian, все поля по 4 байта.
 * sun/sky — цвета двух источников: тёплое солнце и холодный небесный подсвет.
 * Цветной свет вместо серого — главный приём стилизованного рендера: материал
 * остаётся собой, а тень становится синей, а не просто тёмной. */
typedef struct {
    char name[16];
    unsigned sky_top, sky_bottom;   /* 0xAABBGGRR */
    float fog_near, fog_far;
    unsigned slots[PAL_SLOTS];
    unsigned sun, sky;              /* цвет прямого света и цвет заливки, 0xAABBGGRR */
} palette_t;

typedef struct {
    int count;
    const palette_t *items; /* указывает внутрь blob */
    void *blob;
} palette_set_t;

/* Принимает буфер файла во владение (освобождается в palette_set_free). 0 при успехе. */
int palette_set_load(palette_set_t *ps, void *blob, size_t len);
const palette_t *palette_find(const palette_set_t *ps, const char *name);
void palette_set_free(palette_set_t *ps);

#endif
