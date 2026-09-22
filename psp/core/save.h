/* save.h — сохранение прогресса. Этап 1: плоский файл save.bin рядом с EBOOT
 * (магия ASAV, версия, CRC32); системный диалог sceUtilitySavedata — на этапе полировки. */
#ifndef ARGUS_SAVE_H
#define ARGUS_SAVE_H
#include <stddef.h>
#include "world.h"

#define SAVE_VERSION 1
#define SAVE_FILE "save.bin"
#define SAVE_BUF_SIZE 512

typedef struct {
    int version;
    int lang;
    int level_index;      /* LVL_* текущего острова */
    float px, py, pz;     /* позиция Око */
    float pyaw;
    int cam_angle;
    world_t world;
} save_data_t;

unsigned crc32_buf(const void *data, size_t len);
/* Сериализует в буфер: магия, версия, тело, CRC32. 0 при успехе. */
int save_serialize(const save_data_t *d, void *buf, size_t max, size_t *out_len);
/* Разбор с проверкой магии, версии и CRC32. 0 при успехе, -1 при повреждении. */
int save_deserialize(save_data_t *d, const void *buf, size_t len);
/* Запись через временный файл и переименование (не рвём сохранение при сбое). 0 при успехе. */
int save_write_file(const save_data_t *d);
int save_read_file(save_data_t *d);

#endif
