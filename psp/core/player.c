#include "player.h"
#include "camera.h"
#include <math.h>
#include <string.h>

#define PLAYER_RADIUS 0.30f
#define PLAYER_STEP_MAX 0.55f   /* один шаг высоты (step_y = 0,5) берётся, два — нет */
#define PLAYER_SPEED 3.4f       /* единиц в секунду */
#define PLAYER_ACCEL 0.18f      /* сглаживание разгона и торможения за кадр */
#define PLAYER_TURN 0.22f       /* сглаживание разворота */
#define PLAYER_BOB_RATE 9.0f    /* радиан в секунду при полной скорости */
#define PLAYER_BOB_AMP 0.035f
#define PLAYER_LEAN_DEG 5.0f
#define PLAYER_BLINK_RATE 0.35f
#define DT (1.0f / 60.0f)

#define BODY_H 0.55f            /* высота тела до центра головы */

static float wrap180(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

void player_init(player_t *p, const level_t *l) {
    memset(p, 0, sizeof *p);
    walk_spawn(l, &p->pos);
    p->yaw_deg = l ? l->spawn_yaw : 0.0f;
}

void player_tick(player_t *p, const level_t *l, const input_t *in, float cam_yaw_deg) {
    if (!p || !l || !in) return;

    /* Ввод: стик плюс крестовина. Ось Y стика PSP растёт вниз, «вверх» = от камеры. */
    float ix = in->lx, iy = in->ly;
    if (in->buttons & BTN_LEFT) ix -= 1.0f;
    if (in->buttons & BTN_RIGHT) ix += 1.0f;
    if (in->buttons & BTN_UP) iy -= 1.0f;
    if (in->buttons & BTN_DOWN) iy += 1.0f;
    if (ix > 1.0f) ix = 1.0f;
    if (ix < -1.0f) ix = -1.0f;
    if (iy > 1.0f) iy = 1.0f;
    if (iy < -1.0f) iy = -1.0f;

    /* Направление в мире: вперёд — вглубь кадра, вправо — вправо по экрану. */
    float yaw = cam_yaw_deg * M3_DEG2RAD;
    float fx = -sinf(yaw), fz = -cosf(yaw);
    float rx = -fz, rz = fx;
    float dx = rx * ix + fx * (-iy);
    float dz = rz * ix + fz * (-iy);
    float mag = sqrtf(dx * dx + dz * dz);
    if (mag > 1.0f) { dx /= mag; dz /= mag; mag = 1.0f; }

    float target_speed = PLAYER_SPEED * mag;
    p->speed += (target_speed - p->speed) * PLAYER_ACCEL;
    if (p->speed < 0.005f) p->speed = 0.0f;

    if (mag > 0.05f) {
        float want = atan2f(dx, dz) / M3_DEG2RAD; /* 0° — вдоль +Z */
        p->yaw_deg += wrap180(want - p->yaw_deg) * PLAYER_TURN;
        p->yaw_deg = wrap180(p->yaw_deg);
        float step = p->speed * DT;
        walk_move(l, &p->pos, dx * step, dz * step, PLAYER_RADIUS, PLAYER_STEP_MAX);
    }

    /* Процедурная анимация: покачивание и наклон пропорционально скорости. */
    float t = p->speed / PLAYER_SPEED;
    p->bob += PLAYER_BOB_RATE * t * DT;
    if (p->bob > 2.0f * M3_PI) p->bob -= 2.0f * M3_PI;
    p->lean += (PLAYER_LEAN_DEG * t - p->lean) * 0.12f;
    p->blink += PLAYER_BLINK_RATE * DT;
    if (p->blink > 1.0f) p->blink -= 1.0f;

    p->look_active = (in->buttons & BTN_CIRCLE) ? 1 : 0;
}

void player_build(const player_t *p, frame_t *f, const mesh_t *body, const mesh_t *head, const mesh_t *iris) {
    if (!p || !f) return;
    float bob = sinf(p->bob) * PLAYER_BOB_AMP * (p->speed / PLAYER_SPEED);
    float x = p->pos.x, y = p->pos.y, z = p->pos.z;

    frame_mesh_t *m = frame_push_mesh(f, body, x, y + bob, z, p->yaw_deg);
    if (m) m->pitch_deg = -p->lean;

    /* Голова: выше тела, покачивание в противофазе — живее, чем жёсткая связка. */
    float head_y = y + BODY_H + bob * 1.4f;
    m = frame_push_mesh(f, head, x, head_y, z, p->yaw_deg);
    if (m) m->pitch_deg = -p->lean * 0.6f;

    /* Радужка: чуть впереди головы, сплющивается при моргании. */
    float blink = p->blink > 0.94f ? (1.0f - (p->blink - 0.94f) / 0.06f) : 1.0f;
    float ahead = 0.16f;
    float dirx = sinf(p->yaw_deg * M3_DEG2RAD), dirz = cosf(p->yaw_deg * M3_DEG2RAD);
    m = frame_push_mesh(f, iris, x + dirx * ahead, head_y, z + dirz * ahead, p->yaw_deg);
    if (m) m->scale = 0.35f + 0.65f * blink;

    /* Око — источник света, но силуэт важнее свечения: широкий яркий ореол съедал
     * голову целиком, и персонаж читался белым пятном. Ореол узкий и тихий, ядро —
     * блик в зрачке размером с сам зрачок. Оба гаснут на моргании. */
    float halo[3] = { x, head_y, z };
    unsigned halo_a = (unsigned)(30.0f * blink);
    frame_push_sprite(f, SPRITE_GLOW, halo, 0.82f, (halo_a << 24) | 0x00C8E4FFu);
    float core[3] = { x + dirx * (ahead + 0.02f), head_y, z + dirz * (ahead + 0.02f) };
    unsigned core_a = (unsigned)(120.0f * blink);
    frame_push_sprite(f, SPRITE_SPARK, core, 0.17f, (core_a << 24) | 0x00E8F6FFu);
}
