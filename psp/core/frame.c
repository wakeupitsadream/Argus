#include "frame.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void frame_reset(frame_t *f) { memset(f, 0, sizeof *f); }

frame_mesh_t *frame_push_mesh(frame_t *f, const mesh_t *m, float x, float y, float z, float yaw_deg) {
    if (!m || f->mesh_count >= FRAME_MAX_MESHES) return NULL;
    frame_mesh_t *cmd = &f->meshes[f->mesh_count++];
    cmd->mesh = m;
    cmd->pos[0] = x; cmd->pos[1] = y; cmd->pos[2] = z;
    cmd->yaw_deg = yaw_deg;
    cmd->pitch_deg = 0.0f;
    cmd->scale = 1.0f;
    return cmd;
}

frame_sprite_t *frame_push_sprite(frame_t *f, int kind, const float pos[3], float size, unsigned color) {
    if (!pos || f->sprite_count >= FRAME_MAX_SPRITES) return NULL;
    frame_sprite_t *s = &f->sprites[f->sprite_count++];
    s->pos[0] = pos[0]; s->pos[1] = pos[1]; s->pos[2] = pos[2];
    s->size = size;
    s->color = color;
    s->kind = (unsigned char)kind;
    return s;
}

static frame_text_t *push_text_raw(frame_t *f, int font, int align, int x, int y, unsigned color) {
    if (f->text_count >= FRAME_MAX_TEXTS) return NULL;
    frame_text_t *t = &f->texts[f->text_count++];
    t->x = (short)x;
    t->y = (short)y;
    t->color = color;
    t->font = (unsigned char)font;
    t->align = (unsigned char)align;
    t->utf8[0] = 0;
    return t;
}

void frame_push_text(frame_t *f, int font, int align, int x, int y, unsigned color, const char *fmt, ...) {
    frame_text_t *t = push_text_raw(f, font, align, x, y, color);
    if (!t) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->utf8, sizeof t->utf8, fmt, ap);
    va_end(ap);
}

void frame_push_text_shadow(frame_t *f, int font, int align, int x, int y, unsigned color, const char *fmt, ...) {
    char text[FRAME_TEXT_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    /* Альфа подложки — от альфы текста, чтобы затухание работало для обоих слоёв. */
    unsigned alpha = (color >> 24) & 0xFFu;
    unsigned shadow = (alpha << 24) | 0x00101418u;
    frame_text_t *t = push_text_raw(f, font, align, x + 1, y + 1, shadow);
    if (t) memcpy(t->utf8, text, sizeof t->utf8);
    t = push_text_raw(f, font, align, x, y, color);
    if (t) memcpy(t->utf8, text, sizeof t->utf8);
}
