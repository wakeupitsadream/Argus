/* quest.c — мини-квесты отголосков (GDD §1.4 п.6). Своего состояния модуль не держит:
 * всё выводится из флагов мира, определения — в сгенерированном quests_data.h.
 * Без выделений памяти, времени и rand — поведение детерминированное. */
#include "quest.h"
#include <stddef.h>

/* Перья лежат в unsigned short: выше не поднимаемся, иначе переполнение съест прогресс. */
#define QUEST_FEATHERS_MAX 65535

/* «Реплики нет»: i18n_get на отрицательном идентификаторе вернёт "?" вместо падения. */
#define QUEST_STR_NONE (-1)

int quest_state(const world_t *w, const quest_def_t *q) {
    if (!w || !q) return QUEST_NEW;
    /* Порядок проверок важен: сдан — значит сдан, даже если accept_flag тоже стоит. */
    if (world_flag(w, q->reward_flag)) return QUEST_DONE;
    if (world_flag(w, q->accept_flag)) return QUEST_ACTIVE;
    return QUEST_NEW;
}

int quest_talk(world_t *w, const quest_def_t *q) {
    if (!w || !q) return QUEST_NEW;
    switch (quest_state(w, q)) {
    case QUEST_NEW:
        /* Первый разговор только принимает просьбу. Если условие уже выполнено,
         * квест сдаётся при следующем разговоре — так у игрока есть реплика-отдача. */
        world_set_flag(w, q->accept_flag, 1);
        return QUEST_ACTIVE;
    case QUEST_ACTIVE:
        if (!world_flag(w, q->need_flag)) return QUEST_ACTIVE;
        world_set_flag(w, q->reward_flag, 1);
        /* Перо начисляется ровно один раз: reward_flag уже стоит, и следующий разговор
         * уйдёт в ветку QUEST_DONE. */
        if (w->feathers < QUEST_FEATHERS_MAX) w->feathers++;
        return QUEST_DONE;
    default:
        return QUEST_DONE; /* повторный разговор ничего не меняет и не начисляет */
    }
}

int quest_line_str(const world_t *w, const quest_def_t *q) {
    if (!w || !q) return QUEST_STR_NONE;
    switch (quest_state(w, q)) {
    case QUEST_NEW: return q->str_offer;
    case QUEST_ACTIVE: return q->str_progress;
    default: return q->str_done;
    }
}

const quest_def_t *quest_find(const quest_def_t *defs, int count, int giver_id) {
    /* giver_id приходит из данных уровня (level_entity_t.id, не равен нулю);
     * то, что не влезает в поле giver_id, отсекаем до сравнения. */
    if (!defs || count <= 0 || giver_id <= 0 || giver_id > 0xFFFF) return NULL;
    unsigned short want = (unsigned short)giver_id;
    for (int i = 0; i < count; i++) {
        if (defs[i].giver_id == want) return &defs[i];
    }
    return NULL;
}
