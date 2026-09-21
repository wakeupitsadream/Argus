/* fs_psp.h — пути относительно каталога EBOOT, чтение/запись через sceIo. */
#ifndef ARGUS_FS_PSP_H
#define ARGUS_FS_PSP_H
#include <stddef.h>
void fs_init(const char *argv0);
const char *fs_base(void);
/* Собирает полный путь в buf; возвращает buf. */
const char *fs_path(char *buf, size_t n, const char *rel);
int fs_mkdir(const char *rel);
#endif
