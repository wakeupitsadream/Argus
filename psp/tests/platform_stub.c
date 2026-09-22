/* platform_stub.c — реализация platform.h для хост-тестов. Каталог ассетов — переменная ARGUS_ASSETS
 * (build/assets); пути вида "data/x" маппятся туда, остальные — в build/host. */
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void resolve(const char *rel, char *out, size_t n) {
    const char *assets = getenv("ARGUS_ASSETS");
    if (strncmp(rel, "data/", 5) == 0 && assets) snprintf(out, n, "%s/%s", assets, rel + 5);
    else snprintf(out, n, "build/host/%s", rel);
}

void *plat_alloc16(size_t bytes) {
    size_t rounded = (bytes + 15) & ~(size_t)15;
    return aligned_alloc(16, rounded ? rounded : 16);
}

void plat_free(void *p) { free(p); }

void *plat_read_file(const char *rel_path, size_t *out_len) {
    char path[512];
    resolve(rel_path, path, sizeof path);
    if (out_len) *out_len = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    unsigned char *buf = (unsigned char *)plat_alloc16((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return NULL; }
    buf[size] = 0;
    if (out_len) *out_len = (size_t)size;
    return buf;
}

int plat_write_file(const char *rel_path, const void *data, size_t len) {
    char path[512];
    resolve(rel_path, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len ? 0 : -1;
}

void plat_gpu_writeback(const void *p, size_t bytes) {
    (void)p; (void)bytes; /* на хосте кэша GPU нет */
}

void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "    [log] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
