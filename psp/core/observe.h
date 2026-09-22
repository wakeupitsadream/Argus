/* observe.h — фирменная механика: объект «под наблюдением», если игрок держит взгляд
 * и объект попадает в кадр ортокамеры. Спящие объекты двигаются только вне наблюдения. */
#ifndef ARGUS_OBSERVE_H
#define ARGUS_OBSERVE_H
#include "frame.h"

/* 1, если режим взгляда включён и сфера (pos, radius) видна в кадре камеры.
 * Кадр считается с полями: объект считается наблюдаемым, только если он заметно внутри,
 * чтобы на границе экрана не было дребезга. */
int observe_is_watched(const frame_cam_t *cam, const float pos[3], float radius, int look_active);

#endif
