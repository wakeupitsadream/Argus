/* quest.h — мини-квесты отголосков. Определения — данные (assets/quests.toml → quests_data.h),
 * состояние выводится из флагов мира, чтобы не хранить его дважды. */
#ifndef ARGUS_QUEST_H
#define ARGUS_QUEST_H
#include "world.h"

enum { QUEST_NEW = 0, QUEST_ACTIVE = 1, QUEST_DONE = 2 };

typedef struct {
    unsigned short giver_id;     /* id сущности-отголоска */
    unsigned short need_flag;    /* что нужно сделать (флаг мира) */
    unsigned short reward_flag;  /* выставляется при сдаче */
    unsigned short accept_flag;  /* выставляется при первом разговоре */
    unsigned short str_offer, str_progress, str_done;
} quest_def_t;

/* Возвращает QUEST_NEW/ACTIVE/DONE. */
int quest_state(const world_t *w, const quest_def_t *q);
/* Разговор с отголоском: принимает квест или сдаёт его, если условие выполнено.
 * Возвращает новое состояние; выставляет флаги и начисляет перо при сдаче. */
int quest_talk(world_t *w, const quest_def_t *q);
/* Реплика, соответствующая текущему состоянию (идентификатор строки). */
int quest_line_str(const world_t *w, const quest_def_t *q);
/* Поиск определения по id отголоска среди статической таблицы. */
const quest_def_t *quest_find(const quest_def_t *defs, int count, int giver_id);

#endif
