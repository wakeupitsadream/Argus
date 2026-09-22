#include "observe.h"
#include "camera.h"

/* Запас от края экрана в пикселях: объект считается наблюдаемым, только когда он
 * заметно внутри кадра — иначе на границе возникает дребезг «смотрю / не смотрю». */
#define OBSERVE_MARGIN 20.0f

int observe_is_watched(const frame_cam_t *cam, const float pos[3], float radius, int look_active) {
    if (!look_active || !cam || !pos) return 0;
    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    cam_project(cam, pos, &sx, &sy, &depth);
    if (depth < 0.0f || depth > cam->dist * 2.5f) return 0;

    /* Радиус в пикселях: ортопроекция даёт постоянный масштаб. */
    float ppu = (float)CAM_SCREEN_W / (2.0f * (cam->half_w > 0.001f ? cam->half_w : 0.001f));
    float r_px = radius * ppu;
    float margin = OBSERVE_MARGIN + (r_px > 0.0f ? r_px * 0.5f : 0.0f);
    if (margin > (float)CAM_SCREEN_W * 0.4f) margin = (float)CAM_SCREEN_W * 0.4f;

    return (sx >= margin && sx <= (float)CAM_SCREEN_W - margin &&
            sy >= margin && sy <= (float)CAM_SCREEN_H - margin) ? 1 : 0;
}
