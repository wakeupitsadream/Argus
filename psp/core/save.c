/* save.c — плоский файл сохранения save.bin: магия ASAV, версия, тело, CRC32.
 * Тело пишется поле за полем (memcpy), а не дампом структуры: раскладка save_data_t
 * зависит от компилятора, а формат файла должен быть один для хоста и PSP. */
#include "save.h"
#include "platform.h"
#include <string.h>

#define SAVE_MAGIC "ASAV"
#define SAVE_TMP_FILE "save.tmp"
#define SAVE_HEAD_SIZE 8u  /* магия (4) + версия (4) */
#define SAVE_CRC_SIZE 4u
/* Тело: 4 целых, 4 float, битсет флагов, 3 счётчика. */
#define SAVE_BODY_SIZE (4u * 4u + 4u * 4u + (unsigned)(WORLD_FLAG_COUNT / 8) + 3u * 2u)
#define SAVE_TOTAL_SIZE (SAVE_HEAD_SIZE + SAVE_BODY_SIZE + SAVE_CRC_SIZE)

_Static_assert(sizeof(int) == 4, "формат сохранения: int — ровно 4 байта");
_Static_assert(sizeof(float) == 4, "формат сохранения: float — ровно 4 байта");
_Static_assert(sizeof(unsigned short) == 2, "формат сохранения: счётчик — ровно 2 байта");
_Static_assert(SAVE_TOTAL_SIZE <= SAVE_BUF_SIZE, "SAVE_BUF_SIZE меньше записи сохранения");

/* --- CRC-32 (полином 0xEDB88320, init 0xFFFFFFFF, финальный XOR) --- */

static unsigned crc_table[256];
static int crc_table_ready; /* ленивая инициализация; игровая логика однопоточная */

static void crc_table_build(void) {
    for (unsigned i = 0; i < 256; i++) {
        unsigned c = i;
        for (int bit = 0; bit < 8; bit++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

unsigned crc32_buf(const void *data, size_t len) {
    if (!crc_table_ready) crc_table_build();
    if (!data || len == 0) return 0u; /* CRC32 пустых данных — 0 */
    const unsigned char *p = (const unsigned char *)data;
    unsigned c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* --- курсоры по буферу: поле за полем, без невыровненного доступа --- */

static void put(unsigned char **p, const void *src, size_t n) {
    memcpy(*p, src, n);
    *p += n;
}

static void get(const unsigned char **p, void *dst, size_t n) {
    memcpy(dst, *p, n);
    *p += n;
}

int save_serialize(const save_data_t *d, void *buf, size_t max, size_t *out_len) {
    if (!d || !buf || max < SAVE_TOTAL_SIZE) return -1;
    unsigned char *b = (unsigned char *)buf;
    unsigned char *p = b;

    memcpy(p, SAVE_MAGIC, 4);
    p += 4;
    unsigned ver = (unsigned)SAVE_VERSION;
    put(&p, &ver, 4);

    put(&p, &d->version, 4);
    put(&p, &d->lang, 4);
    put(&p, &d->level_index, 4);
    put(&p, &d->px, 4);
    put(&p, &d->py, 4);
    put(&p, &d->pz, 4);
    put(&p, &d->pyaw, 4);
    put(&p, &d->cam_angle, 4);
    put(&p, d->world.bits, sizeof d->world.bits);
    put(&p, &d->world.eyes_opened, 2);
    put(&p, &d->world.small_eyes, 2);
    put(&p, &d->world.feathers, 2);

    unsigned crc = crc32_buf(b, (size_t)(p - b));
    put(&p, &crc, 4);

    if (out_len) *out_len = (size_t)(p - b);
    return 0;
}

/* Число в разумных пределах и не NaN: сравнение ложно и для NaN, и для бесконечности. */
static int save_finite(float v) { return v > -1.0e6f && v < 1.0e6f; }

int save_deserialize(save_data_t *d, const void *buf, size_t len) {
    if (!d || !buf || len != SAVE_TOTAL_SIZE) return -1;
    const unsigned char *b = (const unsigned char *)buf;
    if (memcmp(b, SAVE_MAGIC, 4) != 0) return -1;

    unsigned ver = 0;
    const unsigned char *p = b + 4;
    get(&p, &ver, 4);
    if (ver != (unsigned)SAVE_VERSION) return -1;

    unsigned crc_stored = 0;
    memcpy(&crc_stored, b + (SAVE_TOTAL_SIZE - SAVE_CRC_SIZE), 4);
    if (crc_stored != crc32_buf(b, SAVE_TOTAL_SIZE - SAVE_CRC_SIZE)) return -1;

    /* Заполняем копию: при любом отказе выше структура вызывающего не тронута. */
    save_data_t tmp;
    memset(&tmp, 0, sizeof tmp);
    get(&p, &tmp.version, 4);
    get(&p, &tmp.lang, 4);
    get(&p, &tmp.level_index, 4);
    get(&p, &tmp.px, 4);
    get(&p, &tmp.py, 4);
    get(&p, &tmp.pz, 4);
    get(&p, &tmp.pyaw, 4);
    get(&p, &tmp.cam_angle, 4);
    get(&p, tmp.world.bits, sizeof tmp.world.bits);
    get(&p, &tmp.world.eyes_opened, 2);
    get(&p, &tmp.world.small_eyes, 2);
    get(&p, &tmp.world.feathers, 2);

    /* Целостность проверена, но не осмысленность: CRC пересчитывается тривиально,
     * а карта памяти — вещь, в которую лазят руками. Числа, которыми игра потом
     * считает клетки и углы, обязаны быть числами: NaN в позиции тихо ломает ходьбу,
     * бесконечность при переводе в int — неопределённое поведение.
     * Мусор в пределах разумного (язык, ракурс, счётчики) не отвергаем, а зажимаем:
     * терять прогресс из-за одного странного байта обиднее, чем начать с хаба. */
    if (!save_finite(tmp.px) || !save_finite(tmp.py) || !save_finite(tmp.pz) ||
        !save_finite(tmp.pyaw)) {
        return -1;
    }
    if (tmp.lang < 0 || tmp.lang >= SAVE_LANG_COUNT) tmp.lang = 0;
    if (tmp.level_index < 0 || tmp.level_index >= SAVE_LEVEL_MAX) tmp.level_index = 0;
    tmp.cam_angle &= 3;
    world_set_counts(&tmp.world, (int)tmp.world.eyes_opened, (int)tmp.world.small_eyes,
                     (int)tmp.world.feathers);

    *d = tmp;
    return 0;
}

int save_write_file(const save_data_t *d) {
    unsigned char buf[SAVE_BUF_SIZE]; /* на стеке: в геймплее нет malloc */
    size_t len = 0;
    if (save_serialize(d, buf, sizeof buf, &len) != 0) return -1;
    /* Атомарная замена: пишем временный файл целиком, потом подменяем им основной.
     * Обрыв питания посреди записи оставляет прежний save.bin нетронутым, а не
     * наполовину записанным (его CRC всё равно бы не сошёлся, но прогресс терялся бы). */
    if (plat_write_file(SAVE_TMP_FILE, buf, len) != 0) return -1;
    if (plat_rename_file(SAVE_TMP_FILE, SAVE_FILE) == 0) return 0;
    /* Переименование не удалось (например, файловая система без него) —
     * пишем напрямую: лучше записать, чем потерять сохранение. */
    return plat_write_file(SAVE_FILE, buf, len);
}

int save_read_file(save_data_t *d) {
    if (!d) return -1;
    size_t len = 0;
    void *blob = plat_read_file(SAVE_FILE, &len);
    if (!blob) return -1;
    /* Запись фиксированной длины; хвост файла (выравнивание, завершающий ноль от
     * plat_read_file у некоторых реализаций) игнорируем, короткий файл — отказ. */
    if (len > SAVE_TOTAL_SIZE) len = SAVE_TOTAL_SIZE;
    int rc = save_deserialize(d, blob, len);
    plat_free(blob);
    return rc;
}
