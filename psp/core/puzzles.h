/* puzzles.h — шесть семейств головоломок (GDD §1.4). Логика чистая и тестируемая:
 * состояние живёт в entities_t, здесь — правила. */
#ifndef ARGUS_PUZZLES_H
#define ARGUS_PUZZLES_H
#include "entity.h"

#define BEAM_MAX_SEGMENTS 48

/* Отрезок луча в мировых координатах (для рендера линией спрайтов). */
typedef struct {
    float x0, z0, x1, z1, y;
} beam_seg_t;

typedef struct {
    beam_seg_t segs[BEAM_MAX_SEGMENTS];
    int count;
    int hit_receiver_id; /* id приёмника, куда пришёл луч, или 0 */
} beam_t;

/* Трассировка луча от всех излучателей по сетке: марш по клеткам в четырёх направлениях,
 * зеркала поворачивают на ±90°, призма делит на два, спящие стены пропускают луч только
 * когда на них смотрят (watched). Заполняет beam и выставляет выход приёмника. */
void beam_trace(entities_t *es, beam_t *out);

/* Поворот зеркала/призмы на следующий из четырёх углов (с анимацией). 1 если началось. */
int mirror_rotate(entities_t *es, int entity_id);

/* Толкание блока на одну клетку в направлении (dx, dz) ∈ {-1,0,1}: проверяет проходимость
 * целевой клетки, отсутствие других блоков и перепад высоты. 1 если блок пошёл. */
int block_push(entities_t *es, int entity_id, int dx, int dz);

/* Переключение рычага: меняет состояние и выход. 1 если переключился. */
int lever_toggle(entities_t *es, int entity_id);

/* Пересчёт нажатых плит по позициям игрока и блоков. */
void plates_update(entities_t *es, float px, float pz);

/* Уровень воды в шагах: поднимает плавучие клетки и блоки, меняет проходимость. */
void water_set_level(entities_t *es, int steps);
/* Вентиль воды: +1/-1 шаг в пределах params[0]..params[1] сущности. */
int water_valve_use(entities_t *es, int entity_id);

/* Панель памяти: ввод очередного значения. Возвращает 1 — принято, 0 — сброс из-за ошибки,
 * 2 — последовательность завершена (выход панели включён). */
int memory_input(entities_t *es, int entity_id, int value);

/* Вращающийся сегмент: поворот на 90° вокруг своей оси (анимация 0,6 с, ввод блокируется). */
int segment_rotate(entities_t *es, int segment_id, int dir);

/* Проверка «головоломка решена»: у всех приёмников уровня входы активны. */
int puzzles_all_solved(const entities_t *es);

#endif
