#include "mesh.h"
#include "platform.h"
#include <string.h>

#define MSH_MAGIC "AMSH"
#define MSH_VERSION 1u
#define MSH_FMT_STATIC 1u
#define MSH_HEADER 64u

int mesh_load(mesh_t *m, void *blob, size_t len) {
    memset(m, 0, sizeof *m);
    if (!blob || len < MSH_HEADER || memcmp(blob, MSH_MAGIC, 4) != 0) return -1;
    const unsigned *hdr = (const unsigned *)blob;
    unsigned version = hdr[1], format = hdr[2], count = hdr[3];
    if (version != MSH_VERSION || format != MSH_FMT_STATIC) return -1;
    if (count == 0 || count % 3 != 0 || count > 200000) return -1;
    if (len < MSH_HEADER + (size_t)count * sizeof(mesh_src_vertex_t)) return -1;
    const float *bb = (const float *)(hdr + 4);
    memcpy(m->bbox_min, bb, sizeof m->bbox_min);
    memcpy(m->bbox_max, bb + 3, sizeof m->bbox_max);
    m->count = (int)count;
    m->src = (const mesh_src_vertex_t *)((const char *)blob + MSH_HEADER);
    m->verts = (vtx_static_t *)plat_alloc16((size_t)count * sizeof(vtx_static_t));
    if (!m->verts) return -1;
    for (unsigned i = 0; i < count; i++) {
        m->verts[i].color = 0xFFFFFFFFu;
        m->verts[i].x = m->src[i].x;
        m->verts[i].y = m->src[i].y;
        m->verts[i].z = m->src[i].z;
    }
    m->blob = blob;
    /* GE читает вершины напрямую из памяти — без сброса кэша увидит мусор (CLAUDE.md п.2). */
    plat_gpu_writeback(m->verts, (size_t)count * sizeof(vtx_static_t));
    return 0;
}

static unsigned char scale_channel(unsigned c, float k) {
    float v = (float)c * k + 0.5f;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (unsigned char)v;
}

unsigned mesh_shade_color(const palette_t *pal, unsigned slot, unsigned char ao,
                          unsigned char sun, const float n[3], const light_t *light) {
    if (!pal || !light) return 0xFFFFFFFFu;
    unsigned pal_color = pal->slots[slot < PAL_SLOTS ? slot : 0];
    unsigned shadow_color = pal->slots[SLOT_SHADOW];

    float ndl = n[0] * light->dir[0] + n[1] * light->dir[1] + n[2] * light->dir[2];
    if (ndl < 0.0f) ndl = 0.0f;
    float sun01 = (float)sun / 255.0f;
    float direct = light->diffuse * ndl * sun01;      /* прямой свет с учётом падающей тени */
    float amb = light->ambient;
    /* Чем меньше прямого света, тем сильнее цвет уводится в тон тени палитры. */
    float cool = light->sky_mix * (1.0f - ndl * sun01);
    if (cool < 0.0f) cool = 0.0f;
    if (cool > 1.0f) cool = 1.0f;
    float ao01 = (float)ao / 255.0f;

    unsigned out[3];
    for (int i = 0; i < 3; i++) {
        float pc = (float)((pal_color >> (8 * i)) & 0xFFu);
        float sc = (float)((shadow_color >> (8 * i)) & 0xFFu);
        float v = ao01 * ((pc * (amb + direct)) * (1.0f - cool) + (sc * amb) * cool);
        out[i] = scale_channel((unsigned)(v + 0.5f), 1.0f);
    }
    return 0xFF000000u | (out[2] << 16) | (out[1] << 8) | out[0];
}

void mesh_recolor(mesh_t *m, const palette_t *pal, const light_t *light) {
    if (!m->verts || !pal || !light) return;
    for (int i = 0; i < m->count; i++) {
        const mesh_src_vertex_t *s = &m->src[i];
        float n[3] = { s->nx, s->ny, s->nz };
        m->verts[i].color = mesh_shade_color(pal, s->slot, s->ao, s->sun, n, light);
    }
    plat_gpu_writeback(m->verts, (size_t)m->count * sizeof(vtx_static_t));
}

void mesh_recolor_flat(mesh_t *m, unsigned color) {
    if (!m->verts) return;
    for (int i = 0; i < m->count; i++) m->verts[i].color = color;
    plat_gpu_writeback(m->verts, (size_t)m->count * sizeof(vtx_static_t));
}

void mesh_free(mesh_t *m) {
    if (m->verts) plat_free(m->verts);
    if (m->blob) plat_free(m->blob);
    memset(m, 0, sizeof *m);
}
