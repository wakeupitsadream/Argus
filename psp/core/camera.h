/* camera.h — ортографическая изометрическая камера: четыре ракурса, плавный поворот,
 * наезд в режиме взгляда, сглаженное следование за целью, проекция в экранные координаты. */
#ifndef ARGUS_CAMERA_H
#define ARGUS_CAMERA_H
#include "frame.h"
#include "ease.h"
#include "math3.h"

#define CAM_SCREEN_W 480
#define CAM_SCREEN_H 272

typedef struct {
    int angle;        /* 0..3 — ракурс (yaw 45 + 90·angle) */
    int locked;       /* 1 — поворот запрещён уровнем */
    int look_active;  /* 1 — режим взгляда (наезд) */
    int wide;         /* 1 — обзорный масштаб: весь остров в кадре (заставка, финалы) */
    tween_t yaw;      /* градусы */
    tween_t half_w;   /* полуширина орто-кадра */
    float target[3];  /* сглаженная цель */
    float pitch_deg, dist;
} camera_t;

void camera_init(camera_t *c, int angle, int locked, const float target[3]);
/* dir = -1 (L) или +1 (R); игнорируется при locked и во время поворота. */
void camera_rotate(camera_t *c, int dir);
void camera_set_look(camera_t *c, int look_active);
/* Обзорный масштаб: на заставке и финалах кадр отъезжает, в игре — возвращается. */
void camera_set_wide(camera_t *c, int wide);
/* Один шаг логики (1/60 с): сглаживание цели и продвижение твинов. */
void camera_update(camera_t *c, const float target[3]);
void camera_fill(const camera_t *c, frame_cam_t *out);

/* Положение глаза камеры по её параметрам. */
void cam_eye(const frame_cam_t *cam, float out[3]);
/* Проецирует мировую точку в экранные координаты (0..480, 0..272; y вниз).
 * depth — расстояние вдоль взгляда. Возвращает 1, если точка перед камерой и в кадре. */
int cam_project(const frame_cam_t *cam, const float p[3], float *sx, float *sy, float *depth);
/* Направление «от камеры вглубь» по горизонтали (для управления стиком). */
void cam_forward_xz(const frame_cam_t *cam, float *fx, float *fz);

#endif
