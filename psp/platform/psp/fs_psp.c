#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psputils.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fs_psp.h"
#include "platform.h"

static char s_base[256] = "";


void fs_init(const char *argv0) {
    s_base[0] = 0;
    if (!argv0 || !argv0[0]) return;
    const char *slash = strrchr(argv0, '/');
    if (!slash) return;
    size_t n = (size_t)(slash - argv0) + 1;
    if (n >= sizeof s_base) n = sizeof s_base - 1;
    memcpy(s_base, argv0, n);
    s_base[n] = 0;
}

const char *fs_base(void) { return s_base; }

const char *fs_path(char *buf, size_t n, const char *rel) {
    snprintf(buf, n, "%s%s", s_base, rel);
    return buf;
}

int fs_mkdir(const char *rel) {
    char path[320];
    fs_path(path, sizeof path, rel);
    int r = sceIoMkdir(path, 0777);
    return r < 0 ? -1 : 0; /* уже существует — тоже < 0, вызывающий игнорирует */
}

void *plat_alloc16(size_t bytes) { return memalign(16, bytes ? bytes : 16); }
void plat_free(void *p) { free(p); }

void plat_gpu_writeback(const void *p, size_t bytes) {
    if (!p || !bytes) return;
    sceKernelDcacheWritebackRange(p, (unsigned)bytes);
}

void *plat_read_file(const char *rel_path, size_t *out_len) {
    char path[320];
    fs_path(path, sizeof path, rel_path);
    if (out_len) *out_len = 0;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return NULL;
    SceOff size = sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    if (size < 0 || size > 32 * 1024 * 1024) { sceIoClose(fd); return NULL; }
    unsigned char *buf = (unsigned char *)memalign(16, (size_t)size + 1);
    if (!buf) { sceIoClose(fd); return NULL; }
    size_t got = 0;
    while (got < (size_t)size) {
        int r = sceIoRead(fd, buf + got, (unsigned)((size_t)size - got));
        if (r <= 0) break;
        got += (size_t)r;
    }
    sceIoClose(fd);
    if (got != (size_t)size) { free(buf); return NULL; }
    buf[size] = 0;
    if (out_len) *out_len = (size_t)size;
    return buf;
}

int plat_write_file(const char *rel_path, const void *data, size_t len) {
    char path[320];
    fs_path(path, sizeof path, rel_path);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return -1;
    const unsigned char *p = (const unsigned char *)data;
    size_t done = 0;
    while (done < len) {
        int w = sceIoWrite(fd, p + done, (unsigned)(len - done));
        if (w <= 0) { sceIoClose(fd); return -1; }
        done += (size_t)w;
    }
    sceIoClose(fd);
    return 0;
}

void plat_log(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    printf("%s\n", line); /* в PPSSPP уходит в консоль */
#ifdef ARGUS_DEBUG
    char path[320];
    fs_path(path, sizeof path, "argus.log");
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, line, (unsigned)strlen(line));
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
#endif
}
