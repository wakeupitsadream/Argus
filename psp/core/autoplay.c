#include "autoplay.h"
#include "platform.h"
#include <stdio.h>
#include <string.h>

static const struct { const char *name; unsigned bit; } k_buttons[] = {
    {"cross", BTN_CROSS}, {"circle", BTN_CIRCLE}, {"square", BTN_SQUARE}, {"triangle", BTN_TRIANGLE},
    {"L", BTN_L}, {"R", BTN_R}, {"up", BTN_UP}, {"down", BTN_DOWN}, {"left", BTN_LEFT},
    {"right", BTN_RIGHT}, {"start", BTN_START}, {"select", BTN_SELECT},
};

static unsigned button_bit(const char *name) {
    for (size_t i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++) {
        if (strcmp(k_buttons[i].name, name) == 0) return k_buttons[i].bit;
    }
    return 0;
}

int autoplay_parse(autoplay_t *ap, const char *text) {
    memset(ap, 0, sizeof *ap);
    if (!text) return 0;
    int line_no = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t raw = eol ? (size_t)(eol - p) : strlen(p);
        char line[256];
        /* Длинную строку обрезаем для разбора, но пропускаем целиком: иначе её хвост
         * станет «следующей строкой» и сломает разбор (комментарии бывают длинными). */
        size_t n = raw < sizeof line - 1 ? raw : sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p += raw + (eol ? 1 : 0);
        line_no++;

        char *s = line;
        while (*s == ' ' || *s == '\t' || *s == '\r') s++;
        if (*s == 0 || *s == '#') continue;
        if (ap->count >= AP_MAX_EVENTS) { plat_log("autoplay: слишком много событий"); return -1; }

        ap_event_t *e = &ap->ev[ap->count];
        char cmd[16] = {0}, arg[AP_ASSERT_LEN] = {0};
        int frame = 0;
        if (sscanf(s, "@%d %15s %47s", &frame, cmd, arg) < 2) {
            plat_log("autoplay: строка %d: ожидается '@<кадр> <команда>'", line_no);
            return -1;
        }
        e->frame = frame;
        if (strcmp(cmd, "press") == 0 || strcmp(cmd, "release") == 0) {
            e->kind = cmd[0] == 'p' ? AP_PRESS : AP_RELEASE;
            e->btn = button_bit(arg);
            if (!e->btn) { plat_log("autoplay: строка %d: неизвестная кнопка '%s'", line_no, arg); return -1; }
        } else if (strcmp(cmd, "stick") == 0) {
            e->kind = AP_STICK;
            if (sscanf(s, "@%*d %*s %f %f", &e->x, &e->y) != 2) {
                plat_log("autoplay: строка %d: stick <x> <y>", line_no);
                return -1;
            }
        } else if (strcmp(cmd, "shot") == 0) {
            e->kind = AP_SHOT;
            if (!arg[0]) { plat_log("autoplay: строка %d: shot <имя>", line_no); return -1; }
            snprintf(e->name, sizeof e->name, "%s", arg);
        } else if (strcmp(cmd, "assert") == 0) {
            e->kind = AP_ASSERT;
            if (!arg[0]) { plat_log("autoplay: строка %d: assert <выражение>", line_no); return -1; }
            snprintf(e->name, sizeof e->name, "%s", arg);
        } else if (strcmp(cmd, "level") == 0) {
            e->kind = AP_LEVEL;
            if (!arg[0]) { plat_log("autoplay: строка %d: level <имя> [вход]", line_no); return -1; }
            snprintf(e->name, sizeof e->name, "%s", arg);
            e->arg = 0;
            sscanf(s, "@%*d %*s %*s %d", &e->arg);
        } else if (strcmp(cmd, "set") == 0) {
            e->kind = AP_SET;
            if (sscanf(s, "@%*d %*s %47s %d", e->name, &e->arg) != 2) {
                plat_log("autoplay: строка %d: set <счётчик> <число>", line_no);
                return -1;
            }
        } else if (strcmp(cmd, "quit") == 0) {
            e->kind = AP_QUIT;
        } else {
            plat_log("autoplay: строка %d: неизвестная команда '%s'", line_no, cmd);
            return -1;
        }
        /* Шаг читает список строго по порядку, поэтому кадры обязаны не убывать:
         * иначе более ранние события молча не сработают. */
        if (ap->count > 0 && e->frame < ap->ev[ap->count - 1].frame) {
            plat_log("autoplay: строка %d: кадр %d идёт после %d — события не по порядку",
                     line_no, e->frame, ap->ev[ap->count - 1].frame);
            return -1;
        }
        ap->count++;
    }
    ap->active = ap->count > 0;
    return ap->count;
}

int autoplay_step(autoplay_t *ap, int frame, input_t *out, char out_shot[AP_NAME_LEN]) {
    int flags = 0;
    ap->assert_count = 0;
    while (ap->next < ap->count && ap->ev[ap->next].frame <= frame) {
        const ap_event_t *e = &ap->ev[ap->next++];
        switch (e->kind) {
        case AP_PRESS:   ap->input.buttons |= e->btn; break;
        case AP_RELEASE: ap->input.buttons &= ~e->btn; break;
        case AP_STICK:   ap->input.lx = e->x; ap->input.ly = e->y; break;
        case AP_SHOT:
            flags |= AP_FLAG_SHOT;
            if (out_shot) snprintf(out_shot, AP_NAME_LEN, "%.*s", AP_NAME_LEN - 1, e->name);
            break;
        case AP_ASSERT:
            if (ap->assert_count < AP_PENDING_MAX) {
                /* memcpy, а не snprintf: источник и приёмник лежат в одной структуре,
                 * и компилятор не может доказать отсутствие наложения (-Wrestrict). */
                char *dst = ap->asserts[ap->assert_count++];
                size_t len = strlen(e->name);
                if (len > AP_ASSERT_LEN - 1) len = AP_ASSERT_LEN - 1;
                memcpy(dst, e->name, len);
                dst[len] = 0;
                flags |= AP_FLAG_ASSERT;
            } else {
                plat_log("autoplay: больше %d проверок на кадр — '%s' пропущена", AP_PENDING_MAX, e->name);
            }
            break;
        case AP_LEVEL:
            flags |= AP_FLAG_LEVEL;
            snprintf(ap->level_name, sizeof ap->level_name, "%.*s", AP_NAME_LEN - 1, e->name);
            ap->level_entry = e->arg;
            break;
        case AP_SET:
            flags |= AP_FLAG_SET;
            snprintf(ap->set_name, sizeof ap->set_name, "%.*s", AP_NAME_LEN - 1, e->name);
            ap->set_value = e->arg;
            break;
        case AP_QUIT:    flags |= AP_FLAG_QUIT; break;
        default: break;
        }
    }
    if (out) *out = ap->input;
    return flags;
}
