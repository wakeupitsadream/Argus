/* Тесты математики, камеры и наблюдения. */
#include "minitest.h"
#include "tests.h"
#include "math3.h"
#include "camera.h"
#include "observe.h"
#include <math.h>
#include <string.h>

TEST(test_v3_basics) {
    float a[3] = { 1.0f, 2.0f, 3.0f }, b[3] = { 4.0f, 5.0f, 6.0f }, out[3];
    v3_add(out, a, b);
    CHECK_NEAR(out[0], 5.0, 1e-6); CHECK_NEAR(out[2], 9.0, 1e-6);
    v3_sub(out, b, a);
    CHECK_NEAR(out[1], 3.0, 1e-6);
    v3_scale(out, a, 2.0f);
    CHECK_NEAR(out[2], 6.0, 1e-6);
    CHECK_NEAR(v3_dot(a, b), 32.0, 1e-5);
    v3_cross(out, a, b);
    CHECK_NEAR(out[0], -3.0, 1e-5); CHECK_NEAR(out[1], 6.0, 1e-5); CHECK_NEAR(out[2], -3.0, 1e-5);
    /* cross на месте: out совпадает с входом */
    float c[3] = { 1.0f, 0.0f, 0.0f }, d[3] = { 0.0f, 1.0f, 0.0f };
    v3_cross(c, c, d);
    CHECK_NEAR(c[2], 1.0, 1e-6);
    float z[3] = { 0.0f, 0.0f, 0.0f };
    CHECK_EQ(v3_norm(z), 0);
    float n[3] = { 3.0f, 4.0f, 0.0f };
    CHECK_EQ(v3_norm(n), 1);
    CHECK_NEAR(v3_len(n), 1.0, 1e-6);
    float l[3];
    v3_lerp(l, a, b, 0.5f);
    CHECK_NEAR(l[0], 2.5, 1e-6);
}

TEST(test_m4_basics) {
    mat4_t id, t, r, prod;
    m4_identity(&id);
    m4_translate(&t, 1.0f, 2.0f, 3.0f);
    m4_mul(&prod, &id, &t);
    for (int i = 0; i < 16; i++) CHECK_NEAR(prod.m[i], t.m[i], 1e-6);

    /* Поворот на 90° вокруг Y переводит +X в -Z */
    m4_rot_y(&r, 90.0f);
    float v[3] = { 1.0f, 0.0f, 0.0f }, out[4];
    m4_transform(&r, v, out);
    CHECK_NEAR(out[0], 0.0, 1e-5); CHECK_NEAR(out[2], -1.0, 1e-5); CHECK_NEAR(out[3], 1.0, 1e-6);

    /* Перенос применяется после поворота: mul(t, r) */
    m4_mul(&prod, &t, &r);
    m4_transform(&prod, v, out);
    CHECK_NEAR(out[0], 1.0, 1e-5); CHECK_NEAR(out[1], 2.0, 1e-5); CHECK_NEAR(out[2], 2.0, 1e-5);

    /* Ортопроекция переводит углы куба в ±1 */
    mat4_t o;
    m4_ortho(&o, -2.0f, 2.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    float p[3] = { 2.0f, 1.0f, -11.0f };
    m4_transform(&o, p, out);
    CHECK_NEAR(out[0], 1.0, 1e-5); CHECK_NEAR(out[1], 1.0, 1e-5); CHECK_NEAR(out[2], 1.0, 1e-5);
    float q[3] = { -2.0f, -1.0f, -1.0f };
    m4_transform(&o, q, out);
    CHECK_NEAR(out[0], -1.0, 1e-5); CHECK_NEAR(out[1], -1.0, 1e-5); CHECK_NEAR(out[2], -1.0, 1e-5);
}

TEST(test_m4_look_at) {
    float eye[3] = { 0.0f, 0.0f, 10.0f }, center[3] = { 0.0f, 0.0f, 0.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
    mat4_t view;
    m4_look_at(&view, eye, center, up);
    float out[4];
    m4_transform(&view, center, out);
    CHECK_NEAR(out[0], 0.0, 1e-5); CHECK_NEAR(out[1], 0.0, 1e-5); CHECK_NEAR(out[2], -10.0, 1e-5);
    float right[3] = { 1.0f, 0.0f, 0.0f };
    m4_transform(&view, right, out);
    CHECK_NEAR(out[0], 1.0, 1e-5); /* +X мира — вправо на экране */
}

TEST(test_camera_rotation) {
    float target[3] = { 0.0f, 0.0f, 0.0f };
    camera_t c;
    camera_init(&c, 0, 0, target);
    CHECK_NEAR(c.yaw.value, 45.0, 1e-4);
    CHECK_EQ(c.angle, 0);

    camera_rotate(&c, 1);
    CHECK_EQ(c.angle, 1);
    camera_rotate(&c, 1); /* во время поворота — игнор */
    CHECK_EQ(c.angle, 1);
    for (int i = 0; i < 40; i++) camera_update(&c, target);
    CHECK_NEAR(c.yaw.value, 135.0, 1e-3);

    for (int k = 0; k < 3; k++) {
        camera_rotate(&c, 1);
        for (int i = 0; i < 40; i++) camera_update(&c, target);
    }
    CHECK_EQ(c.angle, 0);
    CHECK_NEAR(c.yaw.value, 405.0, 1e-3); /* полный круг: угол нормализуется, значение накапливается */

    camera_t locked;
    camera_init(&locked, 2, 1, target);
    camera_rotate(&locked, 1);
    CHECK_EQ(locked.angle, 2);
    CHECK_NEAR(locked.yaw.value, 225.0, 1e-4);
}

TEST(test_camera_look_zoom) {
    float target[3] = { 0.0f, 0.0f, 0.0f };
    camera_t c;
    camera_init(&c, 0, 0, target);
    float wide = c.half_w.value;
    camera_set_look(&c, 1);
    for (int i = 0; i < 20; i++) camera_update(&c, target);
    CHECK(c.half_w.value < wide);
    CHECK_NEAR(c.half_w.value, wide * 0.85, 1e-3);
    camera_set_look(&c, 0);
    for (int i = 0; i < 20; i++) camera_update(&c, target);
    CHECK_NEAR(c.half_w.value, wide, 1e-3);
}

TEST(test_camera_follow) {
    float start[3] = { 0.0f, 0.0f, 0.0f }, goal[3] = { 10.0f, 0.0f, 0.0f };
    camera_t c;
    camera_init(&c, 0, 0, start);
    for (int i = 0; i < 120; i++) camera_update(&c, goal);
    CHECK_NEAR(c.target[0], 10.0, 0.05); /* сглаживание сходится к цели */
}

TEST(test_cam_project) {
    camera_t c;
    float target[3] = { 2.0f, 1.0f, -3.0f };
    camera_init(&c, 0, 0, target);
    frame_cam_t fc;
    camera_fill(&c, &fc);

    float sx = 0.0f, sy = 0.0f, depth = 0.0f;
    CHECK_EQ(cam_project(&fc, target, &sx, &sy, &depth), 1);
    CHECK_NEAR(sx, CAM_SCREEN_W / 2.0, 0.01);
    CHECK_NEAR(sy, CAM_SCREEN_H / 2.0, 0.01);
    CHECK_NEAR(depth, fc.dist, 0.01);

    /* Сдвиг цели на half_w вдоль правого вектора камеры уходит к правому краю кадра. */
    float eye[3], fwd[3], right[3], up[3] = { 0.0f, 1.0f, 0.0f };
    cam_eye(&fc, eye);
    v3_sub(fwd, target, eye);
    v3_norm(fwd);
    v3_cross(right, fwd, up);
    v3_norm(right);
    float edge[3];
    v3_scale(edge, right, fc.half_w);
    v3_add(edge, target, edge);
    CHECK_EQ(cam_project(&fc, edge, &sx, &sy, &depth), 1);
    CHECK_NEAR(sx, CAM_SCREEN_W, 0.5);

    /* Далеко за кадром — вне кадра, но координаты всё равно посчитаны. */
    float far_pt[3] = { 200.0f, 0.0f, 0.0f };
    CHECK_EQ(cam_project(&fc, far_pt, &sx, &sy, &depth), 0);
    CHECK(sx > CAM_SCREEN_W);
}

TEST(test_cam_forward) {
    camera_t c;
    float target[3] = { 0.0f, 0.0f, 0.0f };
    camera_init(&c, 0, 0, target); /* yaw 45° */
    frame_cam_t fc;
    camera_fill(&c, &fc);
    float fx = 0.0f, fz = 0.0f;
    cam_forward_xz(&fc, &fx, &fz);
    CHECK_NEAR(fx, -0.7071, 1e-3);
    CHECK_NEAR(fz, -0.7071, 1e-3);
    /* Направление вглубь совпадает с направлением от глаза к цели по горизонтали. */
    float eye[3];
    cam_eye(&fc, eye);
    CHECK(fx * (target[0] - eye[0]) > 0.0f);
    CHECK(fz * (target[2] - eye[2]) > 0.0f);
}

TEST(test_observe) {
    camera_t c;
    float target[3] = { 0.0f, 0.0f, 0.0f };
    camera_init(&c, 0, 0, target);
    frame_cam_t fc;
    camera_fill(&c, &fc);

    CHECK_EQ(observe_is_watched(&fc, target, 0.5f, 0), 0); /* взгляд выключен */
    CHECK_EQ(observe_is_watched(&fc, target, 0.5f, 1), 1); /* центр кадра */

    float far_pt[3] = { 60.0f, 0.0f, 0.0f };
    CHECK_EQ(observe_is_watched(&fc, far_pt, 0.5f, 1), 0);

    /* Объект у самого края не считается наблюдаемым (запас от границы). */
    float eye[3], fwd[3], right[3], up[3] = { 0.0f, 1.0f, 0.0f }, edge[3];
    cam_eye(&fc, eye);
    v3_sub(fwd, target, eye);
    v3_norm(fwd);
    v3_cross(right, fwd, up);
    v3_norm(right);
    v3_scale(edge, right, fc.half_w * 0.99f);
    v3_add(edge, target, edge);
    CHECK_EQ(observe_is_watched(&fc, edge, 0.5f, 1), 0);
    /* А ближе к центру — считается. */
    v3_scale(edge, right, fc.half_w * 0.5f);
    v3_add(edge, target, edge);
    CHECK_EQ(observe_is_watched(&fc, edge, 0.5f, 1), 1);
}

void tests_camera(void) {
    puts("camera/math tests");
    RUN(test_v3_basics);
    RUN(test_m4_basics);
    RUN(test_m4_look_at);
    RUN(test_camera_rotation);
    RUN(test_camera_look_zoom);
    RUN(test_camera_follow);
    RUN(test_cam_project);
    RUN(test_cam_forward);
    RUN(test_observe);
}
