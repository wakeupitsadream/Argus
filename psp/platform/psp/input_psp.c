#include <pspctrl.h>
#include "input_psp.h"

#define DEADZONE 0.18f

void input_init(void) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

static float axis(unsigned char v) {
    float f = ((float)v - 128.0f) / 128.0f;
    if (f > -DEADZONE && f < DEADZONE) return 0.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return f;
}

void input_poll(input_t *in) {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned b = 0;
    if (pad.Buttons & PSP_CTRL_CROSS) b |= BTN_CROSS;
    if (pad.Buttons & PSP_CTRL_CIRCLE) b |= BTN_CIRCLE;
    if (pad.Buttons & PSP_CTRL_SQUARE) b |= BTN_SQUARE;
    if (pad.Buttons & PSP_CTRL_TRIANGLE) b |= BTN_TRIANGLE;
    if (pad.Buttons & PSP_CTRL_LTRIGGER) b |= BTN_L;
    if (pad.Buttons & PSP_CTRL_RTRIGGER) b |= BTN_R;
    if (pad.Buttons & PSP_CTRL_UP) b |= BTN_UP;
    if (pad.Buttons & PSP_CTRL_DOWN) b |= BTN_DOWN;
    if (pad.Buttons & PSP_CTRL_LEFT) b |= BTN_LEFT;
    if (pad.Buttons & PSP_CTRL_RIGHT) b |= BTN_RIGHT;
    if (pad.Buttons & PSP_CTRL_START) b |= BTN_START;
    if (pad.Buttons & PSP_CTRL_SELECT) b |= BTN_SELECT;
    in->buttons = b;
    in->lx = axis(pad.Lx);
    in->ly = axis(pad.Ly);
}
