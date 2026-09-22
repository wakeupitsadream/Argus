#include "screens.h"
#include "strings_ids.h"
#include "i18n.h"
#include <string.h>

#define CURTAIN_FRAMES 22       /* ~0,37 с на закрытие и столько же на открытие */
#define SCR_W 480
#define SCR_H 272

static void set_menu(screens_t *s) {
    s->index = 0;
    s->item_count = 0;
    switch (s->current) {
    case SCR_TITLE:
        if (s->has_save) s->items[s->item_count++] = ACT_CONTINUE;
        s->items[s->item_count++] = ACT_NEW;
        s->items[s->item_count++] = ACT_LANG;
        s->items[s->item_count++] = ACT_QUIT;
        break;
    case SCR_PAUSE:
        s->items[s->item_count++] = ACT_RESUME;
        s->items[s->item_count++] = ACT_LANG;
        s->items[s->item_count++] = ACT_TO_TITLE;
        break;
    default:
        break;
    }
}

void screens_init(screens_t *s, int start, int has_save) {
    memset(s, 0, sizeof *s);
    s->current = (start >= 0 && start < SCR_COUNT) ? start : SCR_TITLE;
    s->pending = -1;
    s->has_save = has_save ? 1 : 0;
    tween_set(&s->curtain, 0.0f);
    set_menu(s);
}

void screens_goto(screens_t *s, int screen) {
    if (screen < 0 || screen >= SCR_COUNT || s->pending >= 0) return;
    s->pending = screen;
    tween_start(&s->curtain, 1.0f, CURTAIN_FRAMES);
}

int screens_busy(const screens_t *s) { return s->pending >= 0 || !tween_done(&s->curtain); }
float screens_curtain(const screens_t *s) { return s->curtain.value; }

static int menu_action(screens_t *s, unsigned pressed) {
    if (s->item_count <= 0) return ACT_NONE;
    if (pressed & (BTN_UP | BTN_LEFT)) {
        s->index = (s->index + s->item_count - 1) % s->item_count;
        return ACT_NONE;
    }
    if (pressed & (BTN_DOWN | BTN_RIGHT)) {
        s->index = (s->index + 1) % s->item_count;
        return ACT_NONE;
    }
    if (pressed & (BTN_CROSS | BTN_START)) return s->items[s->index];
    return ACT_NONE;
}

int screens_tick(screens_t *s, unsigned pressed) {
    s->frames++;

    if (s->pending >= 0) {
        tween_update(&s->curtain, ease_in_out_cubic);
        if (tween_done(&s->curtain)) {
            /* Занавес закрыт — меняем экран и открываем обратно. */
            s->current = s->pending;
            s->pending = -1;
            s->frames = 0;
            set_menu(s);
            tween_start(&s->curtain, 0.0f, CURTAIN_FRAMES);
        }
        return ACT_NONE;
    }
    tween_update(&s->curtain, ease_in_out_cubic);
    if (!tween_done(&s->curtain)) return ACT_NONE; /* занавес ещё открывается */

    switch (s->current) {
    case SCR_TITLE:
    case SCR_PAUSE:
        return menu_action(s, pressed);
    case SCR_ENDING:
        /* Финал ждёт подтверждения, но не раньше, чем текст прочитан. */
        if (s->frames > 90 && (pressed & (BTN_CROSS | BTN_START))) return ACT_ENDING_DONE;
        return ACT_NONE;
    case SCR_CREDITS:
        if (s->frames > 120 && (pressed & (BTN_CROSS | BTN_START))) return ACT_TO_TITLE;
        return ACT_NONE;
    default:
        return ACT_NONE;
    }
}

static int action_str(int action, int lang) {
    switch (action) {
    case ACT_NEW: return STR_MENU_NEW;
    case ACT_CONTINUE: return STR_MENU_CONTINUE;
    case ACT_LANG: return lang == LANG_RU ? STR_LANG_EN : STR_LANG_RU;
    case ACT_QUIT: return STR_MENU_QUIT;
    case ACT_RESUME: return STR_PAUSE_RESUME;
    case ACT_TO_TITLE: return STR_PAUSE_MENU;
    default: return STR_TITLE;
    }
}

static void build_menu(const screens_t *s, frame_t *f, const palette_t *pal, int lang, int y0) {
    unsigned on = pal->slots[SLOT_TOP];
    unsigned off = pal->slots[SLOT_TOP_ALT];
    unsigned mark = pal->slots[SLOT_ACCENT];
    for (int i = 0; i < s->item_count; i++) {
        int y = y0 + i * 22;
        int sel = (i == s->index);
        frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, sel ? 66 : 60, y, sel ? on : off,
                               "%s", i18n_str(action_str(s->items[i], lang)));
        if (sel) frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 44, y, mark, "•");
    }
}

void screens_build(const screens_t *s, frame_t *f, const palette_t *pal, int lang,
                   int eyes, int eyes_total) {
    if (!pal) return;
    unsigned title_c = pal->slots[SLOT_ACCENT];
    unsigned body_c = pal->slots[SLOT_TOP];
    unsigned dim_c = pal->slots[SLOT_TOP_ALT];

    switch (s->current) {
    case SCR_TITLE:
        frame_push_text_shadow(f, FONT_TITLE, TEXT_LEFT, 44, 86, title_c, "%s", STR(STR_TITLE));
        frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 46, 110, dim_c, "%s", STR(STR_HUB_CAPTION));
        build_menu(s, f, pal, lang, 158);
        break;
    case SCR_PAUSE:
        frame_push_text_shadow(f, FONT_TITLE, TEXT_LEFT, 44, 86, title_c, "%s", STR(STR_PAUSE_TITLE));
        frame_push_text_shadow(f, FONT_BODY, TEXT_LEFT, 46, 112, dim_c, "%s %d / %d",
                               STR(STR_EYE_COUNT), eyes, eyes_total);
        build_menu(s, f, pal, lang, 158);
        break;
    case SCR_ENDING:
        frame_push_text_shadow(f, FONT_TITLE, TEXT_CENTER, SCR_W / 2, 118, title_c, "%s",
                               STR(s->ending_variant ? STR_ENDING_B_TITLE : STR_ENDING_A_TITLE));
        frame_push_text_shadow(f, FONT_BODY, TEXT_CENTER, SCR_W / 2, 150, body_c, "%s",
                               STR(s->ending_variant ? STR_ENDING_B_LINE : STR_ENDING_A_LINE));
        if (s->frames > 90) {
            frame_push_text_shadow(f, FONT_BODY, TEXT_CENTER, SCR_W / 2, SCR_H - 28, dim_c,
                                   "%s", STR(STR_PRESS_START));
        }
        break;
    case SCR_CREDITS:
        frame_push_text_shadow(f, FONT_TITLE, TEXT_CENTER, SCR_W / 2, 110, title_c, "%s", STR(STR_TITLE));
        frame_push_text_shadow(f, FONT_BODY, TEXT_CENTER, SCR_W / 2, 140, body_c, "%s", STR(STR_CREDITS_LINE));
        frame_push_text_shadow(f, FONT_BODY, TEXT_CENTER, SCR_W / 2, 162, dim_c, "%s %d / %d",
                               STR(STR_EYE_COUNT), eyes, eyes_total);
        break;
    default:
        break;
    }
}
