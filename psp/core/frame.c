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

/* Убирает оборванную в конце последовательность UTF-8: иначе на месте обрезки
 * появится символ-заменитель. */
static void trim_utf8(char *s) {
    size_t n = strlen(s);
    while (n > 0 && ((unsigned char)s[n - 1] & 0xC0u) == 0x80u) n--; /* продолжающие байты */
    if (n == 0) { s[0] = 0; return; }
    unsigned char lead = (unsigned char)s[n - 1];
    size_t need = 1;
    if ((lead & 0xE0u) == 0xC0u) need = 2;
    else if ((lead & 0xF0u) == 0xE0u) need = 3;
    else if ((lead & 0xF8u) == 0xF0u) need = 4;
    if (need > strlen(s) - (n - 1)) s[n - 1] = 0; /* ведущий байт без хвоста — отбросить */
}

frame_panel_t *frame_push_panel(frame_t *f, int x, int y, int w, int h,
                                unsigned color_top, unsigned color_bottom) {
    if (!f || f->panel_count >= FRAME_MAX_PANELS || w <= 0 || h <= 0) return NULL;
    frame_panel_t *p = &f->panels[f->panel_count++];
    p->x = (short)x; p->y = (short)y; p->w = (short)w; p->h = (short)h;
    p->color_top = color_top;
    p->color_bottom = color_bottom;
    return p;
}

frame_water_t *frame_push_water(frame_t *f, float x0, float z0, float x1, float z1) {
    if (!f || f->water_count >= FRAME_MAX_WATER) return NULL;
    frame_water_t *w = &f->water[f->water_count++];
    w->x0 = x0; w->z0 = z0; w->x1 = x1; w->z1 = z1;
    return w;
}

frame_shadow_t *frame_push_shadow(frame_t *f, float x, float y, float z, float radius,
                                  unsigned char alpha) {
    if (!f || f->shadow_count >= FRAME_MAX_SHADOWS || radius <= 0.0f || alpha == 0) return NULL;
    frame_shadow_t *sh = &f->shadows[f->shadow_count++];
    sh->pos[0] = x; sh->pos[1] = y; sh->pos[2] = z;
    sh->radius = radius;
    sh->alpha = alpha;
    return sh;
}

frame_mesh_t *frame_push_ghost(frame_t *f, const mesh_t *m, float x, float y, float z, float yaw_deg) {
    if (!m || f->ghost_count >= FRAME_MAX_GHOSTS) return NULL;
    frame_mesh_t *cmd = &f->ghosts[f->ghost_count++];
    cmd->mesh = m;
    cmd->pos[0] = x; cmd->pos[1] = y; cmd->pos[2] = z;
    cmd->yaw_deg = yaw_deg;
    cmd->pitch_deg = 0.0f;
    cmd->scale = 1.0f;
    return cmd;
}

void frame_push_text(frame_t *f, int font, int align, int x, int y, unsigned color, const char *fmt, ...) {
    frame_text_t *t = push_text_raw(f, font, align, x, y, color);
    if (!t) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t->utf8, sizeof t->utf8, fmt, ap);
    va_end(ap);
    trim_utf8(t->utf8);
}

void frame_push_text_shadow(frame_t *f, int font, int align, int x, int y, unsigned color, const char *fmt, ...) {
    char text[FRAME_TEXT_LEN];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    trim_utf8(text);
    /* Альфа подложки — от альфы текста, чтобы затухание работало для обоих слоёв. */
    unsigned alpha = (color >> 24) & 0xFFu;
    unsigned shadow = (alpha << 24) | 0x00101418u;
    frame_text_t *t = push_text_raw(f, font, align, x + 1, y + 1, shadow);
    if (t) memcpy(t->utf8, text, sizeof t->utf8);
    t = push_text_raw(f, font, align, x, y, color);
    if (t) memcpy(t->utf8, text, sizeof t->utf8);
}
