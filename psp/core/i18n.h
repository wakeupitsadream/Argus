/* i18n.h — таблицы строк (формат ASTR v1 из docs/FORMATS.md).
 * Идентификаторы STR_* — в сгенерированном build/assets/strings_ids.h. */
#ifndef ARGUS_I18N_H
#define ARGUS_I18N_H
#include <stddef.h>

enum { LANG_RU = 0, LANG_EN = 1, LANG_COUNT = 2 };

typedef struct {
    int count;
    const unsigned *offsets; /* внутрь blob */
    const char *base;        /* начало blob */
    void *blob;
} i18n_t;

/* Принимает буфер файла во владение. 0 при успехе. */
int i18n_load(i18n_t *s, void *blob, size_t len);
void i18n_free(i18n_t *s);
/* Строка по идентификатору; при выходе за границы — "?". */
const char *i18n_get(const i18n_t *s, int id);

/* Активный набор для макроса STR(): переключается при смене языка. */
void i18n_use(const i18n_t *s);
const char *i18n_str(int id);
#define STR(id) i18n_str(id)

#endif
