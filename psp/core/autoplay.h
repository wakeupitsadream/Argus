/* autoplay.h — скриптованный ввод по кадрам для автотестов (autoplay.txt рядом с EBOOT).
 * Строки:
 *   "@<кадр> press|release <кнопка>"  — кнопки: cross circle square triangle L R
 *                                       up down left right start select
 *   "@<кадр> stick <x> <y>"           — аналоговый стик, -1..1
 *   "@<кадр> shot <имя>"              — сохранить кадр в shots/<имя>.bmp
 *   "@<кадр> assert <выражение>"      — проверка состояния, например fps>=58, heap_free>=4000000,
 *                                       tris<=8000, eyes>=1, flag203=1 (вычисляет игра)
 *   "@<кадр> quit"                    — выход
 * '#' — комментарий. */
#ifndef ARGUS_AUTOPLAY_H
#define ARGUS_AUTOPLAY_H
#include "input.h"

#define AP_MAX_EVENTS 256
#define AP_NAME_LEN 32
#define AP_ASSERT_LEN 48
#define AP_PENDING_MAX 4

enum { AP_PRESS = 1, AP_RELEASE, AP_STICK, AP_SHOT, AP_QUIT, AP_ASSERT };

typedef struct {
    int frame, kind;
    unsigned btn;
    float x, y;
    char name[AP_ASSERT_LEN]; /* имя кадра для shot или выражение для assert */
} ap_event_t;

typedef struct {
    ap_event_t ev[AP_MAX_EVENTS];
    int count, next;
    int active;
    input_t input; /* текущее синтетическое состояние ввода */
    char asserts[AP_PENDING_MAX][AP_ASSERT_LEN]; /* проверки этого кадра */
    int assert_count;
} autoplay_t;

/* Возвращает число событий или -1 при ошибке разбора (номер строки в plat_log). */
int autoplay_parse(autoplay_t *ap, const char *text);

/* Флаги результата шага. */
enum { AP_FLAG_SHOT = 1, AP_FLAG_QUIT = 2, AP_FLAG_ASSERT = 4 };

/* Применяет события кадра frame, копирует синтетический ввод в *out.
 * При AP_FLAG_SHOT имя кадра — в out_shot. При AP_FLAG_ASSERT выражения лежат в ap->asserts
 * (ap->assert_count штук) и действительны до следующего вызова. */
int autoplay_step(autoplay_t *ap, int frame, input_t *out, char out_shot[AP_NAME_LEN]);

#endif
