/* platform.h — интерфейс платформы для core/. Реализации: platform/psp/ и tests/platform_stub.c. */
#ifndef ARGUS_PLATFORM_H
#define ARGUS_PLATFORM_H
#include <stddef.h>

/* Читает файл по пути относительно каталога игры (где лежит EBOOT.PBP).
 * Возвращает 16-выровненный буфер с завершающим нулём (для текста) или NULL. Освобождать plat_free. */
void *plat_read_file(const char *rel_path, size_t *out_len);
/* Пишет файл по пути относительно каталога игры. 0 при успехе. */
int plat_write_file(const char *rel_path, const void *data, size_t len);
/* Память с выравниванием 16 байт (требование GPU). */
void *plat_alloc16(size_t bytes);
void plat_free(void *p);
/* Отладочный лог (stdout в эмуляторе, файл на приставке в debug-сборке). */
void plat_log(const char *fmt, ...);

/* Замеры платформы за предыдущий кадр — для debug-оверлея. Заполняет платформа. */
typedef struct {
    float fps;          /* 1 / период кадра */
    unsigned frame_us;  /* период кадра, мкс */
    unsigned cpu_us;    /* построение кадра + отправка дисплей-листа */
    unsigned gpu_us;    /* ожидание sceGuSync */
    unsigned tris;      /* треугольников отправлено */
    unsigned draws;     /* вызовов отрисовки */
    unsigned heap_free; /* свободно в куче, байт */
    unsigned heap_max;  /* крупнейший свободный блок, байт */
} plat_stats_t;

#endif
