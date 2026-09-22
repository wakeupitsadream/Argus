#include "level.h"
#include "platform.h"
#include <string.h>

#define LVL_MAGIC "ALVL"
#define LVL_VERSION 1u
#define LVL_HEADER 96u
#define LVL_MAX_CELLS_SIDE 256
#define LVL_MAX_ENTITIES 128
#define LVL_MAX_LINKS 128
#define LVL_MAX_PORTALS 8

/* Чтение полей через memcpy: буфер 16-выровнен, но поля внутри файла
 * не обязаны быть выровнены под тип, а MIPS не прощает невыровненный доступ. */
static unsigned rd_u32(const unsigned char *p) {
    unsigned v;
    memcpy(&v, p, sizeof v);
    return v;
}
static unsigned short rd_u16(const unsigned char *p) {
    unsigned short v;
    memcpy(&v, p, sizeof v);
    return v;
}
static float rd_f32(const unsigned char *p) {
    float v;
    memcpy(&v, p, sizeof v);
    return v;
}
static void rd_name(char *dst, size_t dst_size, const unsigned char *src, size_t n) {
    size_t i = 0;
    for (; i < n && i + 1 < dst_size && src[i]; i++) dst[i] = (char)src[i];
    dst[i] = 0;
}

/* Проверка «блок [off, off + size) лежит внутри len» без переполнения. */
static int fits(size_t len, unsigned off, size_t size) {
    return (size_t)off <= len && size <= len - (size_t)off;
}

int level_load(level_t *l, void *blob, size_t len) {
    memset(l, 0, sizeof *l);
    if (!blob || len < LVL_HEADER) return -1;
    const unsigned char *b = (const unsigned char *)blob;
    if (memcmp(b, LVL_MAGIC, 4) != 0 || rd_u32(b + 4) != LVL_VERSION) return -1;

    rd_name(l->name, sizeof l->name, b + 8, 16);
    rd_name(l->palette, sizeof l->palette, b + 24, 16);
    int cx = rd_u16(b + 40), cz = rd_u16(b + 42);
    l->cell_size = rd_f32(b + 44);
    l->step_y = rd_f32(b + 48);
    l->spawn_x = rd_f32(b + 52);
    l->spawn_z = rd_f32(b + 56);
    l->spawn_yaw = rd_f32(b + 60);
    int ecount = rd_u16(b + 64), lcount = rd_u16(b + 66), pcount = rd_u16(b + 68);
    l->cam_angle = b[70] & 3;
    l->cam_lock = b[71] ? 1 : 0;
    unsigned cells_off = rd_u32(b + 72);
    unsigned ent_off = rd_u32(b + 76);
    unsigned link_off = rd_u32(b + 80);
    unsigned port_off = rd_u32(b + 84);
    l->name_str_id = rd_u32(b + 88);

    if (cx <= 0 || cz <= 0 || cx > LVL_MAX_CELLS_SIDE || cz > LVL_MAX_CELLS_SIDE) return -1;
    if (ecount > LVL_MAX_ENTITIES || lcount > LVL_MAX_LINKS || pcount > LVL_MAX_PORTALS) return -1;
    if (!(l->cell_size > 0.0f) || !(l->step_y > 0.0f)) return -1;
    if ((cells_off & 3u) || (ent_off & 3u) || (link_off & 3u) || (port_off & 3u)) return -1;
    if (!fits(len, cells_off, (size_t)cx * (size_t)cz * sizeof(level_cell_t))) return -1;
    if (ecount && !fits(len, ent_off, (size_t)ecount * sizeof(level_entity_t))) return -1;
    if (lcount && !fits(len, link_off, (size_t)lcount * sizeof(level_link_t))) return -1;
    if (pcount && !fits(len, port_off, (size_t)pcount * sizeof(level_portal_t))) return -1;

    l->cells_x = cx;
    l->cells_z = cz;
    l->entity_count = ecount;
    l->link_count = lcount;
    l->portal_count = pcount;
    l->cells = (const level_cell_t *)(b + cells_off);
    l->entities = ecount ? (const level_entity_t *)(b + ent_off) : NULL;
    l->links = lcount ? (const level_link_t *)(b + link_off) : NULL;
    l->portals = pcount ? (const level_portal_t *)(b + port_off) : NULL;
    l->blob = blob;
    return 0;
}

void level_free(level_t *l) {
    if (l->blob) plat_free(l->blob);
    memset(l, 0, sizeof *l);
}

const level_cell_t *level_cell(const level_t *l, int cx, int cz) {
    if (!l->cells || cx < 0 || cz < 0 || cx >= l->cells_x || cz >= l->cells_z) return NULL;
    return &l->cells[(size_t)cz * (size_t)l->cells_x + (size_t)cx];
}

float level_cell_top(const level_t *l, int cx, int cz) {
    const level_cell_t *c = level_cell(l, cx, cz);
    if (!c || !(c->flags & CELL_EXISTS)) return -1.0e9f;
    return (float)c->height * l->step_y;
}

void level_cell_center(const level_t *l, int cx, int cz, float *x, float *z) {
    float ox = -(float)l->cells_x * l->cell_size * 0.5f;
    float oz = -(float)l->cells_z * l->cell_size * 0.5f;
    if (x) *x = ox + ((float)cx + 0.5f) * l->cell_size;
    if (z) *z = oz + ((float)cz + 0.5f) * l->cell_size;
}

int level_cell_at(const level_t *l, float x, float z, int *cx, int *cz) {
    float ox = -(float)l->cells_x * l->cell_size * 0.5f;
    float oz = -(float)l->cells_z * l->cell_size * 0.5f;
    float fx = (x - ox) / l->cell_size;
    float fz = (z - oz) / l->cell_size;
    int ix = (int)((fx >= 0.0f) ? fx : fx - 1.0f); /* floor без math.h */
    int iz = (int)((fz >= 0.0f) ? fz : fz - 1.0f);
    if (cx) *cx = ix;
    if (cz) *cz = iz;
    return (ix >= 0 && iz >= 0 && ix < l->cells_x && iz < l->cells_z) ? 1 : 0;
}

const level_entity_t *level_entity_by_id(const level_t *l, int id) {
    if (!l->entities || id <= 0) return NULL;
    for (int i = 0; i < l->entity_count; i++) {
        if (l->entities[i].id == (unsigned short)id) return &l->entities[i];
    }
    return NULL;
}

const level_portal_t *level_portal_at(const level_t *l, float x, float z) {
    int cx = 0, cz = 0;
    if (!level_cell_at(l, x, z, &cx, &cz) || !l->portals) return NULL;
    for (int i = 0; i < l->portal_count; i++) {
        const level_portal_t *p = &l->portals[i];
        int w = p->w ? p->w : 1, h = p->h ? p->h : 1;
        if (cx >= (int)p->cx && cx < (int)p->cx + w && cz >= (int)p->cz && cz < (int)p->cz + h) return p;
    }
    return NULL;
}
