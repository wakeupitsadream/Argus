#include "palette.h"
#include "platform.h"
#include <string.h>

#define PAL_MAGIC "APAL"
#define PAL_VERSION 1u

int palette_set_load(palette_set_t *ps, void *blob, size_t len) {
    memset(ps, 0, sizeof *ps);
    if (!blob || len < 12 || memcmp(blob, PAL_MAGIC, 4) != 0) return -1;
    const unsigned *hdr = (const unsigned *)blob;
    unsigned version = hdr[1], count = hdr[2];
    if (version != PAL_VERSION || count == 0 || count > 64) return -1;
    if (len < 12 + (size_t)count * sizeof(palette_t)) return -1;
    ps->count = (int)count;
    ps->items = (const palette_t *)((const char *)blob + 12);
    ps->blob = blob;
    return 0;
}

const palette_t *palette_find(const palette_set_t *ps, const char *name) {
    for (int i = 0; i < ps->count; i++) {
        if (strncmp(ps->items[i].name, name, sizeof ps->items[i].name) == 0) return &ps->items[i];
    }
    return NULL;
}

void palette_set_free(palette_set_t *ps) {
    if (ps->blob) plat_free(ps->blob);
    memset(ps, 0, sizeof *ps);
}
