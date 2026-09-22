#include "camera.h"
#include <math.h>
#include <string.h>

#define CAM_TURN_FRAMES 36     /* 0,6 с на поворот 90° */
#define CAM_LOOK_FRAMES 15     /* 0,25 с на наезд в режиме взгляда */
#define CAM_PITCH_DEG 33.0f
#define CAM_DIST 40.0f
#define CAM_HALF_W 13.0f     /* полуширина ортокадра: остров 24 клетки должен влезать силуэтом */
#define CAM_LOOK_SCALE 0.85f
#define CAM_FOLLOW 0.14f       /* коэффициент экспоненциального сглаживания цели */
#define CAM_NEAR 1.0f
#define CAM_FAR_SCALE 2.5f

static float angle_yaw(int angle) { return 45.0f + 90.0f * (float)angle; }

void camera_init(camera_t *c, int angle, int locked, const float target[3]) {
    memset(c, 0, sizeof *c);
    c->angle = ((angle % 4) + 4) % 4;
    c->locked = locked ? 1 : 0;
    c->pitch_deg = CAM_PITCH_DEG;
    c->dist = CAM_DIST;
    tween_set(&c->yaw, angle_yaw(c->angle));
    tween_set(&c->half_w, CAM_HALF_W);
    if (target) { c->target[0] = target[0]; c->target[1] = target[1]; c->target[2] = target[2]; }
}

void camera_rotate(camera_t *c, int dir) {
    if (c->locked || dir == 0 || !tween_done(&c->yaw)) return;
    c->angle = ((c->angle + (dir > 0 ? 1 : -1)) % 4 + 4) % 4;
    tween_start(&c->yaw, c->yaw.value + (dir > 0 ? 90.0f : -90.0f), CAM_TURN_FRAMES);
}

void camera_set_look(camera_t *c, int look_active) {
    look_active = look_active ? 1 : 0;
    if (c->look_active == look_active) return;
    c->look_active = look_active;
    tween_start(&c->half_w, look_active ? CAM_HALF_W * CAM_LOOK_SCALE : CAM_HALF_W, CAM_LOOK_FRAMES);
}

void camera_update(camera_t *c, const float target[3]) {
    if (target) {
        for (int i = 0; i < 3; i++) c->target[i] += (target[i] - c->target[i]) * CAM_FOLLOW;
    }
    tween_update(&c->yaw, ease_in_out_cubic);
    tween_update(&c->half_w, ease_out_cubic);
}

void camera_fill(const camera_t *c, frame_cam_t *out) {
    out->target[0] = c->target[0];
    out->target[1] = c->target[1];
    out->target[2] = c->target[2];
    out->yaw_deg = c->yaw.value;
    out->pitch_deg = c->pitch_deg;
    out->dist = c->dist;
    out->half_w = c->half_w.value;
}

void cam_eye(const frame_cam_t *cam, float out[3]) {
    float yaw = cam->yaw_deg * M3_DEG2RAD, pitch = cam->pitch_deg * M3_DEG2RAD;
    out[0] = cam->target[0] + cam->dist * cosf(pitch) * sinf(yaw);
    out[1] = cam->target[1] + cam->dist * sinf(pitch);
    out[2] = cam->target[2] + cam->dist * cosf(pitch) * cosf(yaw);
}

/* Матрицы кадра: строго те же параметры, что использует платформенный рендер. */
static void cam_matrices(const frame_cam_t *cam, mat4_t *view, mat4_t *proj) {
    float eye[3];
    static const float up[3] = { 0.0f, 1.0f, 0.0f };
    cam_eye(cam, eye);
    m4_look_at(view, eye, cam->target, up);
    float hw = cam->half_w;
    float hh = hw * ((float)CAM_SCREEN_H / (float)CAM_SCREEN_W);
    m4_ortho(proj, -hw, hw, -hh, hh, CAM_NEAR, cam->dist * CAM_FAR_SCALE);
}

int cam_project(const frame_cam_t *cam, const float p[3], float *sx, float *sy, float *depth) {
    mat4_t view, proj;
    cam_matrices(cam, &view, &proj);
    float vp[4], cp[4];
    m4_transform(&view, p, vp);
    float v3[3] = { vp[0], vp[1], vp[2] };
    m4_transform(&proj, v3, cp);
    float x = (cp[0] * 0.5f + 0.5f) * (float)CAM_SCREEN_W;
    float y = (1.0f - (cp[1] * 0.5f + 0.5f)) * (float)CAM_SCREEN_H;
    float d = -vp[2]; /* камера смотрит вдоль -z */
    if (sx) *sx = x;
    if (sy) *sy = y;
    if (depth) *depth = d;
    if (d < CAM_NEAR || d > cam->dist * CAM_FAR_SCALE) return 0;
    return (x >= 0.0f && x <= (float)CAM_SCREEN_W && y >= 0.0f && y <= (float)CAM_SCREEN_H) ? 1 : 0;
}

void cam_forward_xz(const frame_cam_t *cam, float *fx, float *fz) {
    /* Направление «вглубь кадра» по горизонтали: от глаза к цели, спроецированное на XZ. */
    float yaw = cam->yaw_deg * M3_DEG2RAD;
    float x = -sinf(yaw), z = -cosf(yaw);
    if (fx) *fx = x;
    if (fz) *fz = z;
}
