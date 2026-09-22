/* Тесты флагов мира (core/world.c) и сохранения (core/save.c): CRC32, round-trip,
 * устойчивость к повреждению файла, запись и чтение через platform_stub. */
#include "minitest.h"
#include "tests.h"
#include "world.h"
#include "save.h"
#include "platform.h"
#include <stdlib.h>

/* Размер записи фиксирован форматом: магия 4 + версия 4 + тело 70 + CRC32 4. */
#define REC_SIZE 82u
#define OFF_MAGIC 0u
#define OFF_VERSION 4u
#define OFF_BODY 8u
#define OFF_CRC 78u

/* Детерминированный генератор (rand нельзя: тесты должны быть воспроизводимы). */
static unsigned rnd_state = 0x12345678u;
static unsigned rnd_next(void) {
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

/* Заметные значения во всех полях, чтобы перепутанный порядок полей был виден. */
static void fill_sample(save_data_t *d) {
    memset(d, 0, sizeof *d);
    d->version = SAVE_VERSION;
    d->lang = 1;
    d->level_index = 7;
    d->px = 12.5f;
    d->py = -3.25f;
    d->pz = 0.125f;
    d->pyaw = 270.0f;
    d->cam_angle = 3;
    world_reset(&d->world);
    world_set_flag(&d->world, 0, 1);
    world_set_flag(&d->world, 5, 1);
    world_set_flag(&d->world, 199, 1);
    world_set_flag(&d->world, WFLAG_INTRO_DONE, 1);
    world_set_flag(&d->world, 255, 1);
    world_set_counts(&d->world, 3, 17, 5);
}

static void check_same(const save_data_t *a, const save_data_t *b) {
    CHECK_EQ(a->version, b->version);
    CHECK_EQ(a->lang, b->lang);
    CHECK_EQ(a->level_index, b->level_index);
    CHECK_NEAR(a->px, b->px, 0.0);
    CHECK_NEAR(a->py, b->py, 0.0);
    CHECK_NEAR(a->pz, b->pz, 0.0);
    CHECK_NEAR(a->pyaw, b->pyaw, 0.0);
    CHECK_EQ(a->cam_angle, b->cam_angle);
    CHECK(memcmp(a->world.bits, b->world.bits, sizeof a->world.bits) == 0);
    CHECK_EQ(a->world.eyes_opened, b->world.eyes_opened);
    CHECK_EQ(a->world.small_eyes, b->world.small_eyes);
    CHECK_EQ(a->world.feathers, b->world.feathers);
}

TEST(test_crc32_vectors) {
    /* Эталонные значения стандартного CRC-32 (те же, что у zlib.crc32). */
    CHECK_EQ(crc32_buf("123456789", 9), 0xCBF43926u);
    CHECK_EQ(crc32_buf("a", 1), 0xE8B7BE43u);
    CHECK_EQ(crc32_buf("abc", 3), 0x352441C2u);
    CHECK_EQ(crc32_buf("message digest", 14), 0x20159D7Fu);
    CHECK_EQ(crc32_buf("abcdefghijklmnopqrstuvwxyz", 26), 0x4C2750BDu);
    CHECK_EQ(crc32_buf("The quick brown fox jumps over the lazy dog", 43), 0x414FA339u);

    /* Пустые данные — 0, в любом виде. */
    CHECK_EQ(crc32_buf("", 0), 0u);
    CHECK_EQ(crc32_buf(NULL, 0), 0u);
    CHECK_EQ(crc32_buf(NULL, 16), 0u);

    /* Нулевые байты тоже считаются. */
    unsigned char zeros[32];
    memset(zeros, 0, sizeof zeros);
    CHECK_EQ(crc32_buf(zeros, sizeof zeros), 0x190A55ADu);
    unsigned char ramp[256];
    for (unsigned i = 0; i < 256; i++) ramp[i] = (unsigned char)i;
    CHECK_EQ(crc32_buf(ramp, sizeof ramp), 0x29058C73u);

    /* Один изменённый бит меняет сумму. */
    ramp[128] ^= 1u;
    CHECK(crc32_buf(ramp, sizeof ramp) != 0x29058C73u);

    /* Длина учитывается: префикс той же строки даёт другую сумму. */
    CHECK(crc32_buf("123456789", 8) != 0xCBF43926u);
}

TEST(test_save_roundtrip) {
    save_data_t src;
    fill_sample(&src);

    unsigned char buf[SAVE_BUF_SIZE];
    memset(buf, 0xCD, sizeof buf);
    size_t len = 0;
    CHECK_EQ(save_serialize(&src, buf, sizeof buf, &len), 0);
    CHECK_EQ(len, REC_SIZE);

    /* Заголовок на месте, версия — формата, CRC совпадает с телом. */
    CHECK(memcmp(buf + OFF_MAGIC, "ASAV", 4) == 0);
    unsigned ver = 0, crc = 0;
    memcpy(&ver, buf + OFF_VERSION, 4);
    memcpy(&crc, buf + OFF_CRC, 4);
    CHECK_EQ(ver, (unsigned)SAVE_VERSION);
    CHECK_EQ(crc, crc32_buf(buf, OFF_CRC));
    /* За записью буфер не тронут. */
    CHECK_EQ(buf[REC_SIZE], 0xCD);

    save_data_t dst;
    memset(&dst, 0xAB, sizeof dst);
    CHECK_EQ(save_deserialize(&dst, buf, len), 0);
    check_same(&src, &dst);

    /* Флаги дошли поштучно. */
    CHECK_EQ(world_flag(&dst.world, 0), 1);
    CHECK_EQ(world_flag(&dst.world, 5), 1);
    CHECK_EQ(world_flag(&dst.world, 199), 1);
    CHECK_EQ(world_flag(&dst.world, WFLAG_INTRO_DONE), 1);
    CHECK_EQ(world_flag(&dst.world, 255), 1);
    CHECK_EQ(world_flag(&dst.world, 1), 0);
    CHECK_EQ(world_flag(&dst.world, 254), 0);

    /* Повторная сериализация даёт тот же байт-в-байт результат. */
    unsigned char again[SAVE_BUF_SIZE];
    size_t len2 = 0;
    CHECK_EQ(save_serialize(&dst, again, sizeof again, &len2), 0);
    CHECK_EQ(len2, len);
    CHECK(memcmp(buf, again, len) == 0);

    /* Нулевое сохранение (новая игра) тоже ходит туда-обратно. */
    save_data_t zero;
    memset(&zero, 0, sizeof zero);
    zero.version = SAVE_VERSION;
    CHECK_EQ(save_serialize(&zero, buf, sizeof buf, &len), 0);
    memset(&dst, 0xAB, sizeof dst);
    CHECK_EQ(save_deserialize(&dst, buf, len), 0);
    check_same(&zero, &dst);

    /* out_len необязателен. */
    CHECK_EQ(save_serialize(&src, buf, sizeof buf, NULL), 0);
    /* Нулевые аргументы не роняют. */
    CHECK_EQ(save_serialize(NULL, buf, sizeof buf, &len), -1);
    CHECK_EQ(save_serialize(&src, NULL, sizeof buf, &len), -1);
    CHECK_EQ(save_deserialize(NULL, buf, REC_SIZE), -1);
    CHECK_EQ(save_deserialize(&dst, NULL, REC_SIZE), -1);
}

TEST(test_save_corruption) {
    save_data_t src;
    fill_sample(&src);
    unsigned char good[SAVE_BUF_SIZE];
    size_t len = 0;
    CHECK_EQ(save_serialize(&src, good, sizeof good, &len), 0);
    CHECK_EQ(len, REC_SIZE);

    /* Порча любого одного байта записи (магия, версия, тело, сама CRC) ловится,
     * и структура вызывающего при отказе не меняется. */
    static const unsigned char masks[] = {0x01u, 0x80u, 0xFFu};
    save_data_t ref;
    memset(&ref, 0xAB, sizeof ref);
    for (unsigned i = 0; i < REC_SIZE; i++) {
        for (unsigned m = 0; m < sizeof masks; m++) {
            unsigned char bad[SAVE_BUF_SIZE];
            memcpy(bad, good, len);
            bad[i] ^= masks[m];
            save_data_t dst;
            memcpy(&dst, &ref, sizeof dst); /* копия побайтно: заполнение структуры тоже сравниваем */
            CHECK_EQ(save_deserialize(&dst, bad, len), -1);
            CHECK(memcmp(&dst, &ref, sizeof dst) == 0);
        }
    }

    /* Случайная порча случайных байтов тела и CRC. */
    for (int it = 0; it < 400; it++) {
        unsigned char bad[SAVE_BUF_SIZE];
        memcpy(bad, good, len);
        unsigned pos = OFF_BODY + rnd_next() % (REC_SIZE - OFF_BODY);
        unsigned char delta = (unsigned char)(1u + rnd_next() % 255u);
        bad[pos] ^= delta;
        save_data_t dst;
        memcpy(&dst, &ref, sizeof dst);
        CHECK_EQ(save_deserialize(&dst, bad, len), -1);
        CHECK(memcmp(&dst, &ref, sizeof dst) == 0);
    }

    /* Порча двух байтов тела разом. */
    for (int it = 0; it < 200; it++) {
        unsigned char bad[SAVE_BUF_SIZE];
        memcpy(bad, good, len);
        unsigned a = OFF_BODY + rnd_next() % (REC_SIZE - OFF_BODY);
        unsigned b = OFF_BODY + rnd_next() % (REC_SIZE - OFF_BODY);
        bad[a] ^= 0x5Au;
        bad[b] ^= 0xA5u;
        if (memcmp(bad, good, len) == 0) continue; /* маски совпали и погасили друг друга */
        save_data_t dst;
        memcpy(&dst, &ref, sizeof dst);
        CHECK_EQ(save_deserialize(&dst, bad, len), -1);
    }

    /* Полностью мусорный буфер и буфер нулей. */
    unsigned char junk[REC_SIZE];
    for (unsigned i = 0; i < REC_SIZE; i++) junk[i] = (unsigned char)rnd_next();
    save_data_t dst;
    memcpy(&dst, &ref, sizeof dst);
    CHECK_EQ(save_deserialize(&dst, junk, REC_SIZE), -1);
    memset(junk, 0, sizeof junk);
    CHECK_EQ(save_deserialize(&dst, junk, REC_SIZE), -1);
    CHECK(memcmp(&dst, &ref, sizeof dst) == 0);
}

TEST(test_save_length_and_version) {
    save_data_t src;
    fill_sample(&src);
    unsigned char good[SAVE_BUF_SIZE];
    size_t len = 0;
    CHECK_EQ(save_serialize(&src, good, sizeof good, &len), 0);

    save_data_t ref;
    memset(&ref, 0xAB, sizeof ref);

    /* Обрезанный буфер любой длины. */
    for (size_t n = 0; n < REC_SIZE; n++) {
        save_data_t dst;
        memcpy(&dst, &ref, sizeof dst);
        CHECK_EQ(save_deserialize(&dst, good, n), -1);
        CHECK(memcmp(&dst, &ref, sizeof dst) == 0);
    }
    /* Лишние байты в хвосте — тоже отказ: длина записи фиксирована. */
    save_data_t dst;
    memcpy(&dst, &ref, sizeof dst);
    CHECK_EQ(save_deserialize(&dst, good, REC_SIZE + 1), -1);
    CHECK_EQ(save_deserialize(&dst, good, sizeof good), -1);

    /* Чужая версия (с корректной CRC — подделать целиком) не принимается. */
    for (unsigned v = 0; v < 5; v++) {
        if (v == (unsigned)SAVE_VERSION) continue;
        unsigned char bad[SAVE_BUF_SIZE];
        memcpy(bad, good, len);
        memcpy(bad + OFF_VERSION, &v, 4);
        unsigned crc = crc32_buf(bad, OFF_CRC);
        memcpy(bad + OFF_CRC, &crc, 4);
        save_data_t out;
        memcpy(&out, &ref, sizeof out);
        CHECK_EQ(save_deserialize(&out, bad, len), -1);
        CHECK(memcmp(&out, &ref, sizeof out) == 0);
    }

    /* Чужая магия с пересчитанной CRC. */
    unsigned char bad[SAVE_BUF_SIZE];
    memcpy(bad, good, len);
    memcpy(bad + OFF_MAGIC, "BSAV", 4);
    unsigned crc = crc32_buf(bad, OFF_CRC);
    memcpy(bad + OFF_CRC, &crc, 4);
    save_data_t out;
    memcpy(&out, &ref, sizeof out);
    CHECK_EQ(save_deserialize(&out, bad, len), -1);
    CHECK(memcmp(&out, &ref, sizeof out) == 0);
}

TEST(test_save_small_buffer) {
    save_data_t src;
    fill_sample(&src);
    /* Запись не должна лезть за max: буфер ровно нужного размера и меньше.
     * ASan ловит любое переполнение. */
    for (size_t max = 0; max < REC_SIZE; max++) {
        unsigned char *small = (unsigned char *)malloc(max ? max : 1);
        CHECK(small != NULL);
        size_t len = 12345;
        CHECK_EQ(save_serialize(&src, small, max, &len), -1);
        CHECK_EQ(len, 12345); /* out_len при отказе не трогается */
        free(small);
    }
    unsigned char *exact = (unsigned char *)malloc(REC_SIZE);
    CHECK(exact != NULL);
    size_t len = 0;
    CHECK_EQ(save_serialize(&src, exact, REC_SIZE, &len), 0);
    CHECK_EQ(len, REC_SIZE);
    save_data_t dst;
    memset(&dst, 0, sizeof dst);
    CHECK_EQ(save_deserialize(&dst, exact, len), 0);
    check_same(&src, &dst);
    free(exact);
}

TEST(test_world_flags) {
    world_t w;
    memset(&w, 0xFF, sizeof w);
    world_reset(&w);
    for (int i = 0; i < WORLD_FLAG_COUNT; i++) CHECK_EQ(world_flag(&w, i), 0);
    CHECK_EQ(w.eyes_opened, 0);
    CHECK_EQ(w.small_eyes, 0);
    CHECK_EQ(w.feathers, 0);

    /* Границы диапазона: 0 и 255 — рабочие флаги. */
    world_set_flag(&w, 0, 1);
    world_set_flag(&w, 255, 1);
    CHECK_EQ(world_flag(&w, 0), 1);
    CHECK_EQ(world_flag(&w, 255), 1);
    for (int i = 1; i < 255; i++) CHECK_EQ(world_flag(&w, i), 0);

    /* Вне диапазона: чтение — 0, запись — тихо игнорируется. Счётчики ненулевые:
     * они лежат сразу за битсетом, и промах на один байт их бы задел. */
    world_set_counts(&w, 3, 17, 5);
    world_t before;
    memcpy(&before, &w, sizeof before);
    static const int bad_ids[] = {-1, -8, -256, -1000, 256, 257, 263, 512, 100000};
    for (unsigned i = 0; i < sizeof bad_ids / sizeof bad_ids[0]; i++) {
        CHECK_EQ(world_flag(&w, bad_ids[i]), 0);
        /* Проверяем после каждой записи по отдельности: пара «поставить + снять»
         * замаскировала бы промах мимо битсета. */
        world_set_flag(&w, bad_ids[i], 1);
        CHECK(memcmp(&w, &before, sizeof w) == 0);
        world_set_flag(&w, bad_ids[i], 0);
        CHECK(memcmp(&w, &before, sizeof w) == 0);
        world_set_flag(&w, bad_ids[i], 255);
        CHECK(memcmp(&w, &before, sizeof w) == 0);
    }
    world_set_counts(&w, 0, 0, 0);

    /* Повторная установка идемпотентна, сброс работает, соседи не задеты. */
    world_set_flag(&w, 100, 1);
    world_set_flag(&w, 100, 1);
    CHECK_EQ(world_flag(&w, 100), 1);
    CHECK_EQ(world_flag(&w, 99), 0);
    CHECK_EQ(world_flag(&w, 101), 0);
    world_set_flag(&w, 100, 0);
    world_set_flag(&w, 100, 0);
    CHECK_EQ(world_flag(&w, 100), 0);
    /* Любое ненулевое значение — «установить». */
    world_set_flag(&w, 100, 2);
    CHECK_EQ(world_flag(&w, 100), 1);
    world_set_flag(&w, 100, -5);
    CHECK_EQ(world_flag(&w, 100), 1);

    /* Каждый бит своего байта: ставим все флаги по одному и снимаем обратно. */
    world_reset(&w);
    for (int i = 0; i < WORLD_FLAG_COUNT; i++) {
        world_set_flag(&w, i, 1);
        CHECK_EQ(world_flag(&w, i), 1);
        if (i + 1 < WORLD_FLAG_COUNT) CHECK_EQ(world_flag(&w, i + 1), 0);
    }
    for (unsigned b = 0; b < sizeof w.bits; b++) CHECK_EQ(w.bits[b], 0xFF);
    for (int i = WORLD_FLAG_COUNT - 1; i >= 0; i--) {
        world_set_flag(&w, i, 0);
        CHECK_EQ(world_flag(&w, i), 0);
    }
    for (unsigned b = 0; b < sizeof w.bits; b++) CHECK_EQ(w.bits[b], 0);

    /* Именованные глобальные флаги лежат в старшей зоне 200..255. */
    CHECK(WFLAG_ENDING_SEEN_A > WORLD_ENTITY_FLAG_MAX);
    CHECK(WFLAG_INTRO_DONE < WORLD_FLAG_COUNT);
    world_set_flag(&w, WFLAG_ENDING_SEEN_A, 1);
    world_set_flag(&w, WFLAG_BONUS_OPEN, 1);
    CHECK_EQ(world_flag(&w, WFLAG_ENDING_SEEN_A), 1);
    CHECK_EQ(world_flag(&w, WFLAG_ENDING_SEEN_B), 0);
    CHECK_EQ(world_flag(&w, WFLAG_BONUS_OPEN), 1);
    CHECK_EQ(world_flag(&w, WFLAG_INTRO_DONE), 0);

    /* world_reset чистит и счётчики. */
    world_set_counts(&w, 2, 5, 1);
    world_reset(&w);
    CHECK_EQ(world_flag(&w, WFLAG_BONUS_OPEN), 0);
    CHECK_EQ(w.eyes_opened, 0);
    CHECK_EQ(w.small_eyes, 0);

    /* NULL не роняет. */
    world_reset(NULL);
    world_set_flag(NULL, 1, 1);
    world_set_counts(NULL, 1, 1, 1);
    CHECK_EQ(world_flag(NULL, 1), 0);
}

TEST(test_world_counts) {
    world_t w;
    world_reset(&w);
    world_set_flag(&w, 42, 1);

    world_set_counts(&w, 2, 17, 3);
    CHECK_EQ(w.eyes_opened, 2);
    CHECK_EQ(w.small_eyes, 17);
    CHECK_EQ(w.feathers, 3);
    CHECK_EQ(world_flag(&w, 42), 1); /* счётчики не трогают флаги */

    /* Повторный вызов перезаписывает, а не накапливает. */
    world_set_counts(&w, 4, 24, 6);
    CHECK_EQ(w.eyes_opened, 4);
    CHECK_EQ(w.small_eyes, 24);
    CHECK_EQ(w.feathers, 6);
    world_set_counts(&w, 0, 0, 0);
    CHECK_EQ(w.eyes_opened, 0);
    CHECK_EQ(w.small_eyes, 0);
    CHECK_EQ(w.feathers, 0);

    /* Отрицательные — в нуль; больших глаз не больше четырёх; остальное по границе u16. */
    world_set_counts(&w, -1, -100, -32768);
    CHECK_EQ(w.eyes_opened, 0);
    CHECK_EQ(w.small_eyes, 0);
    CHECK_EQ(w.feathers, 0);
    world_set_counts(&w, 99, 70000, 1000000);
    CHECK_EQ(w.eyes_opened, 4);
    CHECK_EQ(w.small_eyes, 65535);
    CHECK_EQ(w.feathers, 65535);
}

TEST(test_save_file_io) {
    /* platform_stub держит файлы игры в build/host/ (каталог создаёт `make test`). */
    save_data_t src;
    fill_sample(&src);
    CHECK_EQ(save_write_file(&src), 0);

    /* Файла нет — чтение отказывает и не портит структуру. Файл сначала создан,
     * чтобы ветка проверялась и на первом прогоне в чистом дереве. */
    save_data_t probe;
    memset(&probe, 0xAB, sizeof probe);
    save_data_t ref;
    memcpy(&ref, &probe, sizeof ref);
    if (remove("build/host/" SAVE_FILE) == 0) {
        CHECK_EQ(save_read_file(&probe), -1);
        CHECK(memcmp(&probe, &ref, sizeof probe) == 0);
    }
    CHECK_EQ(save_write_file(&src), 0);

    save_data_t dst;
    memset(&dst, 0xAB, sizeof dst);
    CHECK_EQ(save_read_file(&dst), 0);
    check_same(&src, &dst);

    /* Запись атомарна: временный файл переименован в save.bin, значит его больше нет,
     * а в save.bin лежит ровно та запись, которую дал save_serialize. */
    unsigned char expect[SAVE_BUF_SIZE];
    size_t len = 0;
    CHECK_EQ(save_serialize(&src, expect, sizeof expect, &len), 0);
    size_t got_len = 0;
    void *tmp = plat_read_file("save.tmp", &got_len);
    CHECK(tmp == NULL);
    if (tmp) plat_free(tmp);
    void *saved = plat_read_file("save.bin", &got_len);
    CHECK(saved != NULL);
    if (saved) {
        CHECK_EQ(got_len, len);
        CHECK(memcmp(saved, expect, len) == 0);
        plat_free(saved);
    }

    /* Перезапись: второе сохранение читается как второе. */
    src.level_index = 9;
    src.px = -100.5f;
    world_set_flag(&src.world, 12, 1);
    world_set_counts(&src.world, 4, 24, 6);
    CHECK_EQ(save_write_file(&src), 0);
    CHECK_EQ(save_read_file(&dst), 0);
    check_same(&src, &dst);

    /* Битый файл на диске: чтение отказывает, структура не меняется. */
    unsigned char bad[SAVE_BUF_SIZE];
    memcpy(bad, expect, len);
    bad[OFF_BODY + 3] ^= 0xFFu;
    CHECK_EQ(plat_write_file(SAVE_FILE, bad, len), 0);
    save_data_t keep;
    memcpy(&keep, &dst, sizeof keep);
    CHECK_EQ(save_read_file(&dst), -1);
    CHECK(memcmp(&dst, &keep, sizeof dst) == 0);

    /* Хвост после записи файл не портит: save_read_file читает запись фиксированной длины. */
    CHECK_EQ(save_serialize(&src, expect, sizeof expect, &len), 0); /* src уже изменён выше */
    unsigned char padded[SAVE_BUF_SIZE];
    memcpy(padded, expect, len);
    padded[len] = 0;
    padded[len + 1] = 0xFFu;
    CHECK_EQ(plat_write_file(SAVE_FILE, padded, len + 2), 0);
    CHECK_EQ(save_read_file(&dst), 0);
    check_same(&src, &dst);
    memcpy(&keep, &dst, sizeof keep);

    /* Обрезанный файл. */
    CHECK_EQ(plat_write_file(SAVE_FILE, expect, len - 1), 0);
    CHECK_EQ(save_read_file(&dst), -1);
    CHECK(memcmp(&dst, &keep, sizeof dst) == 0);

    /* Короткий файл с правильной магией и версией: отказ по длине, без чтения за концом
     * буфера (ASan поймал бы, если бы разбор поверил заголовку и полез считать CRC). */
    unsigned char stub[20];
    memcpy(stub, expect, sizeof stub);
    CHECK_EQ(plat_write_file(SAVE_FILE, stub, sizeof stub), 0);
    CHECK_EQ(save_read_file(&dst), -1);
    CHECK(memcmp(&dst, &keep, sizeof dst) == 0);

    /* Пустой файл. */
    CHECK_EQ(plat_write_file(SAVE_FILE, expect, 0), 0);
    CHECK_EQ(save_read_file(&dst), -1);

    /* Восстанавливаем корректное сохранение, чтобы порядок тестов ни на что не влиял. */
    CHECK_EQ(save_write_file(&src), 0);
    CHECK_EQ(save_read_file(&dst), 0);
    CHECK_EQ(save_deserialize(&dst, NULL, 0), -1);
}

TEST(test_save_forged_body) {
    /* Подделка тела с пересчитанной CRC — главный сценарий порчи: контрольная сумма
     * ничего не доказывает, если файл правили руками. Числа обязаны быть числами,
     * а мусор в пределах разумного зажимается, а не выбрасывает весь прогресс. */
    save_data_t src;
    memset(&src, 0, sizeof src);
    src.version = SAVE_VERSION;
    src.lang = 1;
    src.level_index = 3;
    src.px = 1.5f; src.py = 2.5f; src.pz = -3.5f; src.pyaw = 90.0f;
    src.cam_angle = 2;
    world_set_counts(&src.world, 2, 5, 1);

    unsigned char buf[SAVE_BUF_SIZE];
    size_t len = 0;
    CHECK_EQ(save_serialize(&src, buf, sizeof buf, &len), 0);
    const size_t crc_at = len - 4;

    /* NaN в позиции: отвергаем целиком. */
    unsigned char bad[SAVE_BUF_SIZE];
    memcpy(bad, buf, len);
    unsigned nan_bits = 0x7FC00000u;
    memcpy(bad + 20, &nan_bits, 4);          /* px */
    unsigned crc = crc32_buf(bad, crc_at);
    memcpy(bad + crc_at, &crc, 4);
    save_data_t got;
    memset(&got, 0xCD, sizeof got);
    CHECK_EQ(save_deserialize(&got, bad, len), -1);

    /* Бесконечность в угле — тоже отказ. */
    memcpy(bad, buf, len);
    unsigned inf_bits = 0x7F800000u;
    memcpy(bad + 32, &inf_bits, 4);          /* pyaw */
    crc = crc32_buf(bad, crc_at);
    memcpy(bad + crc_at, &crc, 4);
    CHECK_EQ(save_deserialize(&got, bad, len), -1);

    /* Язык и остров вне таблицы: принимаем и зажимаем — прогресс дороже. */
    memcpy(bad, buf, len);
    int wild = 9999;
    memcpy(bad + 12, &wild, 4);              /* lang */
    memcpy(bad + 16, &wild, 4);              /* level_index */
    int angle = 7;
    memcpy(bad + 36, &angle, 4);             /* cam_angle */
    unsigned short eyes = 900;
    memcpy(bad + 72, &eyes, 2);              /* eyes_opened */
    crc = crc32_buf(bad, crc_at);
    memcpy(bad + crc_at, &crc, 4);
    CHECK_EQ(save_deserialize(&got, bad, len), 0);
    CHECK_EQ(got.lang, 0);
    CHECK_EQ(got.level_index, 0);
    CHECK_EQ(got.cam_angle, 3);
    CHECK(got.world.eyes_opened <= 4);
    CHECK_NEAR(got.px, 1.5, 1e-6);
}

void tests_save(void) {
    puts("world/save tests");
    RUN(test_crc32_vectors);
    RUN(test_save_roundtrip);
    RUN(test_save_corruption);
    RUN(test_save_length_and_version);
    RUN(test_save_small_buffer);
    RUN(test_world_flags);
    RUN(test_world_counts);
    RUN(test_save_file_io);
    RUN(test_save_forged_body);
}
