/* test_i18n.c — юнит-тесты таблицы строк ASTR (core/i18n.c). Запуск: make test
 * (ARGUS_ASSETS=build/assets, файлы строк собирает tools/stringsgen.py). */
#include "minitest.h"
#include "i18n.h"
#include "platform.h"
#include "strings_ids.h"
#include "tests.h"
#include <string.h>

/* Строки эталонного набора: есть пустая и есть кириллица в UTF-8. */
static const char *const k_sample[] = {
    "ARGUS",
    "",
    "Сад Зеркал",
    "Глаза: %d",
    "x",
};
#define SAMPLE_N ((int)(sizeof k_sample / sizeof k_sample[0]))

static void wr_u32(unsigned char *p, unsigned v) {
    memcpy(p, &v, sizeof v);
}

/* Собирает корректный blob ASTR. Память — plat_alloc16, как у plat_read_file,
 * так что i18n_free освободит её тем же plat_free. */
static unsigned char *make_blob(const char *const *strs, int count, size_t *out_len) {
    size_t data = 0;
    for (int i = 0; i < count; i++) data += strlen(strs[i]) + 1;
    size_t len = 16u + 4u * (size_t)count + data;
    unsigned char *b = (unsigned char *)plat_alloc16(len);
    if (!b) return NULL;
    memcpy(b, "ASTR", 4);
    wr_u32(b + 4, 1);             /* версия */
    wr_u32(b + 8, (unsigned)count);
    wr_u32(b + 12, 0);            /* резерв */
    unsigned off = (unsigned)(16 + 4 * count);
    for (int i = 0; i < count; i++) {
        size_t n = strlen(strs[i]) + 1;
        wr_u32(b + 16 + 4 * i, off);
        memcpy(b + off, strs[i], n);
        off += (unsigned)n;
    }
    *out_len = len;
    return b;
}

TEST(test_i18n_synthetic) {
    size_t len = 0;
    unsigned char *blob = make_blob(k_sample, SAMPLE_N, &len);
    CHECK(blob != NULL);
    if (!blob) return;
    i18n_t s;
    CHECK_EQ(i18n_load(&s, blob, len), 0);
    CHECK_EQ(s.count, SAMPLE_N);
    CHECK(s.base == (const char *)blob);
    CHECK(s.blob == blob);
    for (int i = 0; i < SAMPLE_N; i++) CHECK_STR(i18n_get(&s, i), k_sample[i]);
    CHECK_EQ(strlen(i18n_get(&s, 1)), 0);  /* пустая строка — это "", а не "?" */
    CHECK_EQ(strlen(i18n_get(&s, 2)), 19); /* «Сад Зеркал»: 10 символов, кириллица по 2 байта */

    /* Выход за границы — статическая "?", один и тот же указатель. */
    CHECK_STR(i18n_get(&s, -1), "?");
    CHECK_STR(i18n_get(&s, SAMPLE_N), "?");
    CHECK_STR(i18n_get(&s, 1 << 20), "?");
    CHECK_STR(i18n_get(NULL, 0), "?");
    CHECK(i18n_get(&s, -1) == i18n_get(NULL, 7));

    /* Активный набор и макрос STR(). */
    i18n_use(NULL);
    CHECK_STR(STR(0), "?");
    i18n_use(&s);
    for (int i = 0; i < SAMPLE_N; i++) CHECK_STR(STR(i), k_sample[i]);
    CHECK_STR(STR(SAMPLE_N), "?");

    i18n_free(&s);
    CHECK_EQ(s.count, 0);
    CHECK(s.blob == NULL);
    CHECK_STR(STR(0), "?"); /* освобождённый набор перестаёт быть активным */
    i18n_free(&s);          /* повторное освобождение и NULL безопасны */
    i18n_free(NULL);
}

TEST(test_i18n_empty_table) {
    size_t len = 0;
    unsigned char *blob = make_blob(k_sample, 0, &len);
    CHECK(blob != NULL);
    if (!blob) return;
    CHECK_EQ(len, 16u);
    i18n_t s;
    CHECK_EQ(i18n_load(&s, blob, len), 0);
    CHECK_EQ(s.count, 0);
    CHECK_STR(i18n_get(&s, 0), "?");
    i18n_use(&s);
    CHECK_STR(STR(0), "?");
    i18n_free(&s);
}

/* Каждый случай портит свежую копию корректного blob; при отказе освобождаем сами. */
TEST(test_i18n_bad_blobs) {
    i18n_t s;
    size_t len = 0;
    unsigned char *b = NULL;

    /* Мусор вместо файла. Выравнивание 16 — чтобы отказ был из-за магии, а не адреса. */
    _Alignas(16) unsigned char junk[64] = "JUNK";
    CHECK_EQ(i18n_load(&s, junk, sizeof junk), -1);
    CHECK_EQ(s.count, 0);
    CHECK(s.blob == NULL);
    CHECK_STR(i18n_get(&s, 0), "?"); /* после отказа набор пуст, а не полуживой */

    /* Нет буфера, обрезанный заголовок. */
    CHECK_EQ(i18n_load(&s, NULL, 0), -1);
    b = make_blob(k_sample, SAMPLE_N, &len);
    CHECK_EQ(i18n_load(&s, b, 15), -1);
    CHECK_EQ(i18n_load(&s, b, 0), -1);
    /* Невыровненный буфер (и заодно сбитая магия). */
    CHECK_EQ(i18n_load(&s, b + 1, len - 1), -1);
    plat_free(b);

    /* Чужая версия. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    wr_u32(b + 4, 2);
    CHECK_EQ(i18n_load(&s, b, len), -1);
    plat_free(b);

    /* count не влезает в файл и count на грани переполнения. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    wr_u32(b + 8, (unsigned)SAMPLE_N + 100u);
    CHECK_EQ(i18n_load(&s, b, len), -1);
    wr_u32(b + 8, 0xFFFFFFFFu);
    CHECK_EQ(i18n_load(&s, b, len), -1);
    plat_free(b);

    /* Обрезанный файл: часть таблицы и строк отрезана. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    CHECK_EQ(i18n_load(&s, b, len - 5), -1);
    CHECK_EQ(i18n_load(&s, b, 16 + 4 * SAMPLE_N - 1), -1);
    plat_free(b);

    /* Смещение за концом файла и ровно на конце. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    wr_u32(b + 16 + 4 * 2, (unsigned)len + 1000u);
    CHECK_EQ(i18n_load(&s, b, len), -1);
    wr_u32(b + 16 + 4 * 2, (unsigned)len);
    CHECK_EQ(i18n_load(&s, b, len), -1);
    plat_free(b);

    /* Смещение внутрь таблицы смещений — строки лежат только после неё. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    wr_u32(b + 16, 8u);
    CHECK_EQ(i18n_load(&s, b, len), -1);
    plat_free(b);

    /* Последняя строка без завершающего нуля. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    b[len - 1] = 'z';
    CHECK_EQ(i18n_load(&s, b, len), -1);
    plat_free(b);

    /* Строка, которая упирается в конец буфера: нуль есть, но он уже вне len. */
    b = make_blob(k_sample, SAMPLE_N, &len);
    CHECK_EQ(i18n_load(&s, b, len - 1), -1);
    plat_free(b);
}

TEST(test_i18n_real_files) {
    static const char *const files[LANG_COUNT] = { "data/strings_ru.bin", "data/strings_en.bin" };
    for (int lang = 0; lang < LANG_COUNT; lang++) {
        size_t len = 0;
        void *blob = plat_read_file(files[lang], &len);
        if (!blob) {
            printf("\n    нет %s — пропуск\n", files[lang]);
            continue;
        }
        i18n_t s;
        int rc = i18n_load(&s, blob, len);
        CHECK_EQ(rc, 0);
        if (rc != 0) {
            plat_free(blob);
            continue;
        }
        CHECK_EQ(s.count, STR_COUNT);
        int multibyte = 0;
        for (int i = 0; i < s.count; i++) {
            const char *v = i18n_get(&s, i);
            CHECK(v[0] != 0);              /* ни одной пустой строки */
            CHECK(strcmp(v, "?") != 0);    /* и ни одной заглушки */
            for (const unsigned char *p = (const unsigned char *)v; *p; p++)
                if (*p >= 0x80u) multibyte++;
        }
        if (lang == LANG_RU) CHECK(multibyte > 0); /* кириллица в UTF-8 дожила до файла */
        i18n_use(&s);
        CHECK_STR(STR(STR_TITLE), i18n_get(&s, STR_TITLE));
        CHECK(strchr(i18n_get(&s, STR_EYE_COUNT), '%') != NULL); /* счётчик глаз со %d */
        i18n_free(&s);
    }
    i18n_use(NULL);
}

void tests_i18n(void) {
    puts("i18n tests");
    RUN(test_i18n_synthetic);
    RUN(test_i18n_empty_table);
    RUN(test_i18n_bad_blobs);
    RUN(test_i18n_real_files);
}
