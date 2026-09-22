/* entity.h — живое состояние сущностей уровня: сигналы, связи, взаимодействие, анимация.
 * Определения приходят из level_t (данные), здесь только то, что меняется во время игры. */
#ifndef ARGUS_ENTITY_H
#define ARGUS_ENTITY_H
#include "level.h"
#include "world.h"
#include "frame.h"
#include "ease.h"

#define ENT_RUNTIME_MAX 128
#define ENT_OUT_BITS 4  /* сколько выходов у сущности (битовая маска) */
#define ENT_IN_BITS 4

typedef struct {
    const level_entity_t *def;
    float x, y, z;        /* текущая позиция (блоки и плавучие блоки двигаются) */
    float yaw;            /* текущий угол (зеркала, сегменты) */
    float phase;          /* фаза анимации в градусах */
    tween_t anim;         /* активная анимация поворота/сдвига; tween_done — покой */
    unsigned char state;  /* смысл зависит от типа: угол зеркала 0..3, нажат ли рычаг и т.п. */
    unsigned char outputs;
    unsigned char inputs;
    unsigned char watched; /* под наблюдением в этом кадре (механика взгляда) */
    unsigned char active;  /* 1 — сущность жива (не собрана, не удалена) */
} entity_t;

typedef struct {
    entity_t items[ENT_RUNTIME_MAX];
    int count;
    const level_t *level;
    world_t *world;
    int water_steps;        /* уровень воды в шагах level->step_y */
    int busy_frames;        /* > 0 — идёт анимация, ввод заблокирован */
    int last_event;         /* id сущности, вызвавшей событие в этом кадре, или 0 */
} entities_t;

/* Готовит состояние по данным уровня; учитывает уже установленные флаги мира
 * (например собранные малые глаза становятся неактивными). */
void entities_init(entities_t *es, const level_t *l, world_t *w);
/* Шаг логики: анимации, спящие объекты (двигаются только когда !watched), вода. */
void entities_tick(entities_t *es, const frame_cam_t *cam, int look_active);
/* Разносит выходы по входам по таблице связей уровня. Вызывается после каждого изменения. */
void entities_propagate(entities_t *es);
/* Взаимодействие «крест»: ищет ближайшую сущность в радиусе reach от (px, pz) и применяет её
 * действие (повернуть зеркало, дёрнуть рычаг, толкнуть блок от игрока, собрать глаз).
 * Возвращает id сущности или 0. dir_x/dir_z — направление взгляда игрока (для толкания блоков). */
int entities_interact(entities_t *es, float px, float pz, float dir_x, float dir_z, float reach);
entity_t *entities_by_id(entities_t *es, int id);
const entity_t *entities_by_id_const(const entities_t *es, int id);
/* Есть ли у типа действие по «кресту» — для подсказки на экране и для поиска цели. */
int entity_can_interact(int type);
/* Сколько больших глаз открыто на этом уровне. */
int entities_eyes_open(const entities_t *es);

#endif
