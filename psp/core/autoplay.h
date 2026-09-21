/* autoplay.h — скриптованный ввод по кадрам для автотестов (autoplay.txt рядом с EBOOT).
 * Строки: "@<кадр> press <кнопка>", "release <кнопка>", "stick <x> <y>", "shot <имя>", "quit".
 * Кнопки: cross circle square triangle L R up down left right start select. '#' — комментарий. */
#ifndef ARGUS_AUTOPLAY_H
#define ARGUS_AUTOPLAY_H
#include "input.h"

#define AP_MAX_EVENTS 256
#define AP_NAME_LEN 32

enum { AP_PRESS = 1, AP_RELEASE, AP_STICK, AP_SHOT, AP_QUIT };

typedef struct {
    int frame, kind;
    unsigned btn;
    float x, y;
    char name[AP_NAME_LEN];
} ap_event_t;

typedef struct {
    ap_event_t ev[AP_MAX_EVENTS];
    int count, next;
    int active;
    input_t input; /* текущее синтетическое состояние ввода */
} autoplay_t;

/* Возвращает число событий или -1 при ошибке разбора (номер строки в plat_log). */
int autoplay_parse(autoplay_t *ap, const char *text);

/* Флаги результата шага. */
enum { AP_FLAG_SHOT = 1, AP_FLAG_QUIT = 2 };

/* Применяет события кадра frame, копирует синтетический ввод в *out.
 * При AP_FLAG_SHOT имя кадра — в out_shot. */
int autoplay_step(autoplay_t *ap, int frame, input_t *out, char out_shot[AP_NAME_LEN]);

#endif
