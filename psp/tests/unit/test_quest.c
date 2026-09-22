/* Тесты мини-квестов отголосков: жизненный цикл по флагам мира, идемпотентность сдачи,
 * поиск по id отголоска и настоящая таблица из сгенерированного quests_data.h. */
#include "minitest.h"
#include "tests.h"
#include "quest.h"
#include "world.h"
#include "quests_data.h"
#include <string.h>

/* Границы из core/world.h и схемы assets/quests.toml (см. шапку файла данных). */
#define ENTITY_FLAG_MIN 1
#define ENTITY_FLAG_MAX WORLD_ENTITY_FLAG_MAX
#define GLOBAL_FLAG_MIN 204
#define GLOBAL_FLAG_MAX (WORLD_FLAG_COUNT - 1)

/* Пробный квест: флаги по той же схеме, что в данных (условие — id сущности,
 * служебные флаги — глобальные). Строки — произвольные различимые номера. */
static const quest_def_t Q_TEST = {
    .giver_id = 10, .need_flag = 30,
    .reward_flag = 209, .accept_flag = 208,
    .str_offer = 101, .str_progress = 102, .str_done = 103
};

TEST(test_quest_state_by_flags) {
    world_t w;
    world_reset(&w);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_NEW);

    /* Выполненное условие само по себе состояние не меняет: квест ещё не принят. */
    world_set_flag(&w, Q_TEST.need_flag, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_NEW);

    world_set_flag(&w, Q_TEST.accept_flag, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_ACTIVE);

    world_set_flag(&w, Q_TEST.reward_flag, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_DONE);

    /* Награда важнее принятия: сохранение из старой версии могло не нести accept_flag. */
    world_reset(&w);
    world_set_flag(&w, Q_TEST.reward_flag, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_DONE);
}

TEST(test_quest_lifecycle) {
    world_t w;
    world_reset(&w);

    /* 1. Первый разговор — квест принят, перьев не прибавилось. */
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_offer);
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_ACTIVE);
    CHECK_EQ(world_flag(&w, Q_TEST.accept_flag), 1);
    CHECK_EQ(world_flag(&w, Q_TEST.reward_flag), 0);
    CHECK_EQ(w.feathers, 0);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_progress);

    /* 2. Разговор без выполненного условия — остаётся ACTIVE. */
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_ACTIVE);
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_ACTIVE);
    CHECK_EQ(world_flag(&w, Q_TEST.reward_flag), 0);
    CHECK_EQ(w.feathers, 0);

    /* 3. Условие выполнено (собрана нужная сущность) — состояние пока ACTIVE. */
    world_set_flag(&w, Q_TEST.need_flag, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_ACTIVE);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_progress);

    /* 4. Сдача: флаг награды, перо и реплика-отдача. */
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_DONE);
    CHECK_EQ(world_flag(&w, Q_TEST.reward_flag), 1);
    CHECK_EQ(w.feathers, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_DONE);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_done);
}

TEST(test_quest_idempotent) {
    world_t w;
    world_reset(&w);
    world_set_flag(&w, Q_TEST.need_flag, 1);
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_ACTIVE);
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_DONE);
    CHECK_EQ(w.feathers, 1);

    /* Повторные разговоры не начисляют перо второй раз и ничего не меняют в мире. */
    world_t after = w;
    for (int i = 0; i < 5; i++) {
        CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_DONE);
    }
    CHECK_EQ(w.feathers, 1);
    CHECK_EQ(memcmp(&w, &after, sizeof w), 0);

    /* Даже если условие потом сбросят (пересборка уровня), перо остаётся выданным. */
    world_set_flag(&w, Q_TEST.need_flag, 0);
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_DONE);
    CHECK_EQ(w.feathers, 1);
}

TEST(test_quest_need_before_accept) {
    world_t w;
    world_reset(&w);

    /* Игрок нашёл предмет до разговора: квест всё равно сначала принимается... */
    world_set_flag(&w, Q_TEST.need_flag, 1);
    CHECK_EQ(quest_state(&w, &Q_TEST), QUEST_NEW);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_offer);
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_ACTIVE);
    CHECK_EQ(w.feathers, 0);

    /* ...и сдаётся сразу при следующем разговоре, без беготни. */
    CHECK_EQ(quest_talk(&w, &Q_TEST), QUEST_DONE);
    CHECK_EQ(w.feathers, 1);
    CHECK_EQ(world_flag(&w, Q_TEST.reward_flag), 1);
}

TEST(test_quest_line_str) {
    world_t w;
    world_reset(&w);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_offer);
    world_set_flag(&w, Q_TEST.accept_flag, 1);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_progress);
    world_set_flag(&w, Q_TEST.reward_flag, 1);
    CHECK_EQ(quest_line_str(&w, &Q_TEST), Q_TEST.str_done);

    /* Без мира или без определения реплики нет; отрицательный id i18n_get отдаст как "?". */
    CHECK(quest_line_str(NULL, &Q_TEST) < 0);
    CHECK(quest_line_str(&w, NULL) < 0);
}

TEST(test_quest_find) {
    /* Каждый giver из таблицы находится, и находится именно свой квест. */
    for (int i = 0; i < QUEST_COUNT; i++) {
        const quest_def_t *q = quest_find(QUESTS, QUEST_COUNT, QUESTS[i].giver_id);
        CHECK(q == &QUESTS[i]);
    }

    /* Несуществующий id, ноль, отрицательное и не влезающее в поле — NULL, не мусор. */
    CHECK(quest_find(QUESTS, QUEST_COUNT, 999) == NULL);
    CHECK(quest_find(QUESTS, QUEST_COUNT, 0) == NULL);
    CHECK(quest_find(QUESTS, QUEST_COUNT, -5) == NULL);
    CHECK(quest_find(QUESTS, QUEST_COUNT, 0x10000 + QUESTS[0].giver_id) == NULL);

    /* Пустая и отсутствующая таблица. */
    CHECK(quest_find(QUESTS, 0, QUESTS[0].giver_id) == NULL);
    CHECK(quest_find(QUESTS, -1, QUESTS[0].giver_id) == NULL);
    CHECK(quest_find(NULL, QUEST_COUNT, QUESTS[0].giver_id) == NULL);
}

TEST(test_quest_nulls) {
    world_t w;
    world_reset(&w);
    world_t before = w;

    CHECK_EQ(quest_state(NULL, &Q_TEST), QUEST_NEW);
    CHECK_EQ(quest_state(&w, NULL), QUEST_NEW);
    CHECK_EQ(quest_state(NULL, NULL), QUEST_NEW);
    CHECK_EQ(quest_talk(NULL, &Q_TEST), QUEST_NEW);
    CHECK_EQ(quest_talk(&w, NULL), QUEST_NEW);
    CHECK_EQ(quest_talk(NULL, NULL), QUEST_NEW);

    /* Мир не тронут: ни флага, ни пера. */
    CHECK_EQ(memcmp(&w, &before, sizeof w), 0);
}

TEST(test_quest_independent) {
    world_t w;
    world_reset(&w);

    /* Разговор с одним отголоском не двигает остальные квесты — скрытого состояния нет. */
    CHECK_EQ(quest_talk(&w, &QUESTS[0]), QUEST_ACTIVE);
    for (int i = 1; i < QUEST_COUNT; i++) {
        CHECK_EQ(quest_state(&w, &QUESTS[i]), QUEST_NEW);
    }

    world_set_flag(&w, QUESTS[0].need_flag, 1);
    CHECK_EQ(quest_talk(&w, &QUESTS[0]), QUEST_DONE);
    CHECK_EQ(w.feathers, 1);
    for (int i = 1; i < QUEST_COUNT; i++) {
        CHECK_EQ(quest_state(&w, &QUESTS[i]), QUEST_NEW);
        CHECK_EQ(quest_line_str(&w, &QUESTS[i]), QUESTS[i].str_offer);
    }
}

TEST(test_quest_data_table) {
    CHECK(QUEST_COUNT >= 1);
    CHECK_EQ(QUEST_COUNT, 6); /* шесть просьб по GDD §1.3 */

    int count = -1;
    CHECK(quests_table(&count) == QUESTS);
    CHECK_EQ(count, QUEST_COUNT);
    CHECK(quests_table(NULL) == QUESTS);

    for (int i = 0; i < QUEST_COUNT; i++) {
        const quest_def_t *q = &QUESTS[i];
        /* Диапазоны: giver и условие — id сущностей, служебные флаги — глобальные. */
        CHECK(q->giver_id >= ENTITY_FLAG_MIN && q->giver_id <= ENTITY_FLAG_MAX);
        CHECK(q->need_flag >= ENTITY_FLAG_MIN && q->need_flag <= ENTITY_FLAG_MAX);
        CHECK(q->accept_flag >= GLOBAL_FLAG_MIN && q->accept_flag <= GLOBAL_FLAG_MAX);
        CHECK(q->reward_flag >= GLOBAL_FLAG_MIN && q->reward_flag <= GLOBAL_FLAG_MAX);
        CHECK(q->accept_flag != q->reward_flag);
        CHECK(q->need_flag != q->giver_id);
        CHECK(q->str_offer < STR_COUNT && q->str_progress < STR_COUNT && q->str_done < STR_COUNT);

        /* Уникальность giver и служебных флагов по всей таблице. */
        for (int j = i + 1; j < QUEST_COUNT; j++) {
            const quest_def_t *o = &QUESTS[j];
            CHECK(q->giver_id != o->giver_id);
            CHECK(q->accept_flag != o->accept_flag && q->accept_flag != o->reward_flag);
            CHECK(q->reward_flag != o->accept_flag && q->reward_flag != o->reward_flag);
        }
    }

    /* Полный проход по таблице: все шесть просьб закрываются и дают ровно шесть перьев. */
    world_t w;
    world_reset(&w);
    for (int i = 0; i < QUEST_COUNT; i++) {
        const quest_def_t *q = quest_find(QUESTS, QUEST_COUNT, QUESTS[i].giver_id);
        CHECK(q != NULL);
        if (!q) continue;
        CHECK_EQ(quest_talk(&w, q), QUEST_ACTIVE);
        CHECK_EQ(quest_talk(&w, q), QUEST_ACTIVE);
        world_set_flag(&w, q->need_flag, 1);
        CHECK_EQ(quest_talk(&w, q), QUEST_DONE);
        CHECK_EQ(quest_talk(&w, q), QUEST_DONE);
    }
    CHECK_EQ(w.feathers, QUEST_COUNT);
    for (int i = 0; i < QUEST_COUNT; i++) {
        CHECK_EQ(quest_state(&w, &QUESTS[i]), QUEST_DONE);
    }
}

void tests_quest(void) {
    puts("quest tests");
    RUN(test_quest_state_by_flags);
    RUN(test_quest_lifecycle);
    RUN(test_quest_idempotent);
    RUN(test_quest_need_before_accept);
    RUN(test_quest_line_str);
    RUN(test_quest_find);
    RUN(test_quest_nulls);
    RUN(test_quest_independent);
    RUN(test_quest_data_table);
}
