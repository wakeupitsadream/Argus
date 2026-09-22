/* level.h — остров: сетка клеток с высотами, сущности, связи, порталы.
 * Формат ALVL v1 описан в docs/FORMATS.md; собирается tools/levelc.py из levels/ (TOML). */
#ifndef ARGUS_LEVEL_H
#define ARGUS_LEVEL_H
#include <stddef.h>

/* Биты level_cell_t.flags */
enum {
    CELL_EXISTS = 1 << 0, /* есть геометрия (иначе пустота) */
    CELL_WALK   = 1 << 1, /* можно стоять */
    CELL_STAIR  = 1 << 2, /* рампа: высота интерполируется между соседями */
    CELL_WATER  = 1 << 3, /* водная поверхность */
    CELL_FLOAT  = 1 << 4, /* плавучая: пол клетки идёт за уровнем воды (level_t.water_steps) */
    CELL_SEG    = 1 << 5  /* принадлежит вращающемуся сегменту (segment != 0) */
};

/* Байт segment: младшие 6 бит — номер сегмента (0 — статичная геометрия),
 * старшие 2 бита — состояние сегмента, при котором клетка проходима.
 * Клетка с CELL_SEG проходима, когда level_t.seg_states[id] == требуемому состоянию. */
#define CELL_SEG_ID(seg) ((seg) & 0x3F)
#define CELL_SEG_STATE(seg) (((seg) >> 6) & 0x03)
#define LEVEL_MAX_SEGMENTS 64
/* Битовая карта занятых клеток: хватает на сетку до 64×64 (лимит компилятора уровней). */
#define LEVEL_BLOCKED_BYTES 512

typedef struct {
    unsigned char height;  /* в шагах step_y */
    unsigned char flags;   /* CELL_* */
    unsigned char segment; /* см. CELL_SEG_ID / CELL_SEG_STATE */
    unsigned char pad;
} level_cell_t; /* 4 байта */

#define LEVEL_ENTITY_PARAMS 6

typedef struct {
    unsigned short type;  /* ENT_* из сгенерированного entity_types.h */
    unsigned short flags;
    float x, y, z;
    float yaw_deg;
    int params[LEVEL_ENTITY_PARAMS];
    unsigned short name_str_id; /* STR_* или 0xFFFF */
    unsigned short id;          /* стабильный идентификатор для связей и флагов мира */
} level_entity_t; /* 48 байт */

typedef struct {
    unsigned short src_id;
    unsigned char src_out;
    unsigned char pad;
    unsigned short dst_id;
    unsigned char dst_in;
    unsigned char pad2;
} level_link_t; /* 8 байт */

typedef struct {
    unsigned short cx, cz;         /* клетка левого верхнего угла */
    unsigned char w, h;            /* размер в клетках */
    unsigned short target_level;   /* индекс уровня (LVL_* из level_ids.h) */
    unsigned short target_entry;   /* id сущности-входа или 0 — спавн уровня */
    unsigned short pad;
    unsigned pad2;
} level_portal_t; /* 16 байт */

typedef struct {
    char name[17];
    char palette[17];
    int cells_x, cells_z;
    float cell_size, step_y;
    float spawn_x, spawn_z, spawn_yaw;
    int cam_angle, cam_lock;
    unsigned name_str_id;
    const level_cell_t *cells;
    const level_entity_t *entities;
    const level_link_t *links;
    const level_portal_t *portals;
    int entity_count, link_count, portal_count;
    /* Состояния вращающихся сегментов (0..3). В файле их нет — это состояние игры,
     * которое меняют головоломки; walk_is_walkable сверяется с ним. */
    unsigned char seg_states[LEVEL_MAX_SEGMENTS];
    /* Уровень воды в шагах step_y — тоже состояние игры, а не данные файла.
     * Пишет его water_set_level, читает walk.c: пол плавучей клетки идёт за водой. */
    unsigned char water_steps;
    /* Занятые подвижными объектами клетки (блоки и плавучие блоки), по биту на клетку.
     * Тоже состояние игры: сетка в файле не знает, где сейчас стоит блок, а ходьба
     * обязана об него спотыкаться — иначе игрок проходит сквозь сокобан-блок. */
    unsigned char blocked[LEVEL_BLOCKED_BYTES];
    void *blob;
} level_t;

/* Принимает буфер файла во владение. 0 при успехе, -1 при неверном формате. */
int level_load(level_t *l, void *blob, size_t len);
void level_free(level_t *l);

/* NULL вне сетки. */
const level_cell_t *level_cell(const level_t *l, int cx, int cz);
/* Занята ли клетка подвижным объектом (блок, плавучий блок). */
int level_cell_blocked(const level_t *l, int cx, int cz);
/* Помечает клетку занятой; вызывается только из core/entity.c при пересчёте. */
void level_set_blocked(level_t *l, int cx, int cz, int value);
/* Сбрасывает всю карту занятости. */
void level_clear_blocked(level_t *l);
/* Мировая высота верха клетки; -1e9f если клетки нет. */
float level_cell_top(const level_t *l, int cx, int cz);
/* Центр клетки в мировых координатах (остров центрирован в начале координат). */
void level_cell_center(const level_t *l, int cx, int cz, float *x, float *z);
/* Клетка под мировой точкой; 1 если точка внутри сетки. */
int level_cell_at(const level_t *l, float x, float z, int *cx, int *cz);
const level_entity_t *level_entity_by_id(const level_t *l, int id);
/* Портал под мировой точкой или NULL. */
const level_portal_t *level_portal_at(const level_t *l, float x, float z);

#endif
