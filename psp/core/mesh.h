/* mesh.h — статические меши (.msh, формат AMSH v1) и их раскраска палитрой.
 * Формат вершины для GPU: цвет 0xAABBGGRR, затем позиция float x,y,z —
 * ровно GU_COLOR_8888 | GU_VERTEX_32BITF (порядок полей: color, position). */
#ifndef ARGUS_MESH_H
#define ARGUS_MESH_H
#include <stddef.h>
#include "palette.h"

typedef struct {
    unsigned color;
    float x, y, z;
} vtx_static_t; /* 16 байт */

typedef struct {
    float x, y, z, nx, ny, nz;
    unsigned char slot, ao;
    unsigned char sun;  /* видимость солнца 0..255: запечённая падающая тень */
    unsigned char pad;
} mesh_src_vertex_t; /* 28 байт, как в файле */

typedef struct {
    int count;                       /* число вершин (кратно 3) */
    vtx_static_t *verts;             /* 16-выровнены, для GPU */
    const mesh_src_vertex_t *src;    /* исходные вершины внутри blob */
    float bbox_min[3], bbox_max[3];
    void *blob;
} mesh_t;

typedef struct {
    float dir[3];      /* направление НА источник света, нормализовано */
    float ambient;     /* 0..1 */
    float diffuse;     /* 0..1 */
    /* Доля небесного подсвета в неосвещённой части: цвет там уходит к тону тени
     * палитры, а не просто темнеет. Именно из-за этого тень читается как тень,
     * а не как выцветший серый — приём стилизованного рендера, дешёвый и заметный. */
    float sky_mix;
} light_t;

/* Принимает буфер файла во владение. 0 при успехе. Цвета не резолвит — вызови mesh_recolor. */
int mesh_load(mesh_t *m, void *blob, size_t len);
void mesh_recolor(mesh_t *m, const palette_t *pal, const light_t *light);
/* Заливает все вершины одним цветом — для силуэтов. */
void mesh_recolor_flat(mesh_t *m, unsigned color);
void mesh_free(mesh_t *m);

/* Цвет одной вершины. Прямой свет гасится запечённой тенью (sun), неосвещённая часть
 * подмешивает тон тени палитры (light->sky_mix), всё вместе умножается на AO. */
unsigned mesh_shade_color(const palette_t *pal, unsigned slot, unsigned char ao,
                          unsigned char sun, const float n[3], const light_t *light);

#endif
