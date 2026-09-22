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

unsigned mesh_shade_color(unsigned pal_color, unsigned char ao, const float n[3], const light_t *light) {
    float ndl = n[0] * light->dir[0] + n[1] * light->dir[1] + n[2] * light->dir[2];
    if (ndl < 0.0f) ndl = 0.0f;
    float k = ((float)ao / 255.0f) * (light->ambient + light->diffuse * ndl);
    unsigned r = scale_channel(pal_color & 0xFFu, k);
    unsigned g = scale_channel((pal_color >> 8) & 0xFFu, k);
    unsigned b = scale_channel((pal_color >> 16) & 0xFFu, k);
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

void mesh_recolor(mesh_t *m, const palette_t *pal, const light_t *light) {
    if (!m->verts || !pal || !light) return;
    for (int i = 0; i < m->count; i++) {
        const mesh_src_vertex_t *s = &m->src[i];
        float n[3] = { s->nx, s->ny, s->nz };
        unsigned slot = s->slot < PAL_SLOTS ? s->slot : 0u;
        m->verts[i].color = mesh_shade_color(pal->slots[slot], s->ao, n, light);
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
