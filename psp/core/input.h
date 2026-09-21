/* input.h — платформенно-независимое состояние ввода. */
#ifndef ARGUS_INPUT_H
#define ARGUS_INPUT_H

enum {
    BTN_CROSS = 1 << 0, BTN_CIRCLE = 1 << 1, BTN_SQUARE = 1 << 2, BTN_TRIANGLE = 1 << 3,
    BTN_L = 1 << 4, BTN_R = 1 << 5,
    BTN_UP = 1 << 6, BTN_DOWN = 1 << 7, BTN_LEFT = 1 << 8, BTN_RIGHT = 1 << 9,
    BTN_START = 1 << 10, BTN_SELECT = 1 << 11
};

typedef struct {
    unsigned buttons; /* маска BTN_* */
    float lx, ly;     /* аналоговый стик, -1..1, мёртвая зона уже вычтена */
} input_t;

#endif
