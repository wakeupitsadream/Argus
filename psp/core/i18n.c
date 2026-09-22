/* i18n.c — таблица строк ASTR v1 (docs/FORMATS.md): проверка файла, чтение на месте,
 * активный набор для STR(). Строки не копируются: i18n_t указывает внутрь blob и владеет им.
 * i18n_load при отказе ничего не освобождает — буфер остаётся на вызывающем. */
#include "i18n.h"
#include "platform.h"
#include <stdint.h>
#include <string.h>

#define STR_MAGIC   "ASTR"
#define STR_VERSION 1u
#define STR_HEADER  16u /* magic, версия, count, резерв */

/* Таблица offsets читается на месте как u32 — ширина типа часть контракта i18n_t. */
_Static_assert(sizeof(unsigned) == 4, "offsets в файле — u32");

/* Заглушка вместо отсутствующей строки: рисуется на экране, но не ломает printf. */
static const char k_missing[] = "?";

/* Активный набор для i18n_str/STR(); меняется при смене языка. */
static const i18n_t *g_active;

/* Резерв (смещение 12) не проверяем: он оставлен на будущее расширение формата. */
static unsigned rd_u32(const unsigned char *p) {
    unsigned v;
    memcpy(&v, p, sizeof v); /* поля файла не обязаны быть выровнены под тип */
    return v;
}

int i18n_load(i18n_t *s, void *blob, size_t len) {
    if (!s) return -1;
    memset(s, 0, sizeof *s);
    if (!blob || len < STR_HEADER) return -1;
    /* Таблица offsets отдаётся наружу как const unsigned *: на MIPS невыровненное
     * чтение u32 — исключение, поэтому требуем кратный 4 адрес (plat_read_file даёт 16). */
    if (((uintptr_t)blob & 3u) != 0u) return -1;

    const unsigned char *b = (const unsigned char *)blob;
    if (memcmp(b, STR_MAGIC, 4) != 0 || rd_u32(b + 4) != STR_VERSION) return -1;

    /* Считаем в u32 — ширине смещений в файле; вычитание вместо сложения, иначе
     * переполнение пропустило бы битый файл. Верхняя граница count ≈ 1.07e9 < INT_MAX,
     * так что приведение к int ниже безопасно. */
    unsigned count = rd_u32(b + 8);
    if (count > (0xFFFFFFFFu - STR_HEADER) / 4u) return -1;
    unsigned data_off = STR_HEADER + 4u * count;
    if ((size_t)data_off > len) return -1;

    if (count > 0u) {
        /* Последний байт файла обязан быть нулём: тогда любая строка внутри файла
         * заведомо завершается до конца буфера. */
        if (b[len - 1] != 0) return -1;
        const unsigned char *table = b + STR_HEADER;
        for (unsigned i = 0; i < count; i++) {
            unsigned off = rd_u32(table + 4u * i);
            /* Строки лежат после таблицы смещений и целиком внутри файла. */
            if (off < data_off || (size_t)off >= len) return -1;
            if (memchr(b + off, 0, len - (size_t)off) == NULL) return -1;
        }
    }

    s->count = (int)count;
    s->offsets = (const unsigned *)(const void *)(b + STR_HEADER);
    s->base = (const char *)b;
    s->blob = blob;
    return 0;
}

void i18n_free(i18n_t *s) {
    if (!s) return;
    if (g_active == s) g_active = NULL; /* чтобы STR() не читал освобождённую память */
    if (s->blob) plat_free(s->blob);
    memset(s, 0, sizeof *s);
}

const char *i18n_get(const i18n_t *s, int id) {
    if (!s || !s->base || id < 0 || id >= s->count) return k_missing;
    return s->base + s->offsets[id];
}

void i18n_use(const i18n_t *s) {
    g_active = (s && s->base) ? s : NULL;
}

const char *i18n_str(int id) {
    return i18n_get(g_active, id); /* без набора i18n_get вернёт "?" */
}
