/* screens.h — экраны игры и переходы между ними. Состояние экранов отделено от игрового мира:
 * screens_tick принимает ввод и возвращает выбранное действие, игра его исполняет. */
#ifndef ARGUS_SCREENS_H
#define ARGUS_SCREENS_H
#include "input.h"
#include "frame.h"
#include "palette.h"
#include "ease.h"

enum { SCR_TITLE = 0, SCR_GAME, SCR_PAUSE, SCR_ENDING, SCR_CREDITS, SCR_COUNT };

/* Действия меню, которые возвращает screens_tick. */
enum {
    ACT_NONE = 0, ACT_NEW, ACT_CONTINUE, ACT_LANG, ACT_QUIT,
    ACT_RESUME, ACT_TO_TITLE, ACT_ENDING_DONE
};

#define SCREEN_MAX_ITEMS 5

typedef struct {
    int current;                 /* SCR_* */
    int pending;                 /* экран после закрытия занавеса, -1 если перехода нет */
    tween_t curtain;             /* 0 — открыто, 1 — чёрный экран */
    int index;                   /* выбранный пункт меню */
    int items[SCREEN_MAX_ITEMS]; /* ACT_* пунктов текущего меню */
    int item_count;
    int frames;                  /* кадров на текущем экране */
    int ending_variant;          /* 0 — разбудить Аргуса, 1 — отпустить мир */
    int has_save;
} screens_t;

void screens_init(screens_t *s, int start, int has_save);
/* Начинает переход на другой экран через занавес. */
void screens_goto(screens_t *s, int screen);
/* 1 — идёт переход, игровой ввод игнорируется. */
int screens_busy(const screens_t *s);
float screens_curtain(const screens_t *s);
/* Шаг логики. pressed — маска кнопок, нажатых именно в этом кадре.
 * Возвращает ACT_* или ACT_NONE. */
int screens_tick(screens_t *s, unsigned pressed);
/* Рисует надписи текущего экрана (кроме игрового HUD — он в game.c). */
void screens_build(const screens_t *s, frame_t *f, const palette_t *pal, int lang,
                   int eyes, int eyes_total);

#endif
