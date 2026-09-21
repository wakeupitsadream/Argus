/* frame.h — граница core ↔ platform: плоский список команд рендера на один кадр.
 * core заполняет frame_t, платформа только рисует. */
#ifndef ARGUS_FRAME_H
#define ARGUS_FRAME_H
#include "mesh.h"

#define FRAME_MAX_MESHES 32
#define FRAME_NAME_LEN 32

typedef struct {
    const mesh_t *mesh;
    float pos[3];
    float yaw_deg;
} frame_mesh_t;

typedef struct {
    float target[3];
    float yaw_deg, pitch_deg; /* поворот вокруг цели и наклон камеры */
    float dist;               /* расстояние глаза от цели */
    float half_w;             /* полуширина орто-кадра в мировых единицах */
} frame_cam_t;

typedef struct {
    unsigned sky_top, sky_bottom, fog_color; /* 0xAABBGGRR */
    float fog_near, fog_far;
} frame_env_t;

typedef struct {
    frame_env_t env;
    frame_cam_t cam;
    frame_mesh_t meshes[FRAME_MAX_MESHES];
    int mesh_count;
    char shot_name[FRAME_NAME_LEN]; /* непустое — сохранить кадр под этим именем */
    int quit;                       /* 1 — завершить игру после кадра */
    char dbg[96];                   /* строка отладочного оверлея */
} frame_t;

#endif
