#include "math3.h"
#include <math.h>
#include <string.h>

void v3_set(float out[3], float x, float y, float z) { out[0] = x; out[1] = y; out[2] = z; }
void v3_add(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] + b[0]; out[1] = a[1] + b[1]; out[2] = a[2] + b[2];
}
void v3_sub(float out[3], const float a[3], const float b[3]) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}
void v3_scale(float out[3], const float a[3], float k) {
    out[0] = a[0] * k; out[1] = a[1] * k; out[2] = a[2] * k;
}
float v3_dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void v3_cross(float out[3], const float a[3], const float b[3]) {
    float x = a[1] * b[2] - a[2] * b[1];
    float y = a[2] * b[0] - a[0] * b[2];
    float z = a[0] * b[1] - a[1] * b[0];
    out[0] = x; out[1] = y; out[2] = z;
}
float v3_len(const float a[3]) { return sqrtf(v3_dot(a, a)); }

int v3_norm(float v[3]) {
    float len = v3_len(v);
    if (len <= 1.0e-6f) return 0;
    v[0] /= len; v[1] /= len; v[2] /= len;
    return 1;
}

void v3_lerp(float out[3], const float a[3], const float b[3], float t) {
    out[0] = a[0] + (b[0] - a[0]) * t;
    out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = a[2] + (b[2] - a[2]) * t;
}

void m4_identity(mat4_t *out) {
    memset(out->m, 0, sizeof out->m);
    out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0f;
}

void m4_mul(mat4_t *out, const mat4_t *a, const mat4_t *b) {
    mat4_t r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) sum += a->m[k * 4 + row] * b->m[c * 4 + k];
            r.m[c * 4 + row] = sum;
        }
    }
    *out = r;
}

void m4_look_at(mat4_t *out, const float eye[3], const float center[3], const float up[3]) {
    float f[3], s[3], u[3];
    v3_sub(f, center, eye);
    if (!v3_norm(f)) { m4_identity(out); return; }
    v3_cross(s, f, up);
    if (!v3_norm(s)) { v3_set(s, 1.0f, 0.0f, 0.0f); }
    v3_cross(u, s, f);

    m4_identity(out);
    out->m[0] = s[0]; out->m[4] = s[1]; out->m[8]  = s[2];
    out->m[1] = u[0]; out->m[5] = u[1]; out->m[9]  = u[2];
    out->m[2] = -f[0]; out->m[6] = -f[1]; out->m[10] = -f[2];
    out->m[12] = -v3_dot(s, eye);
    out->m[13] = -v3_dot(u, eye);
    out->m[14] = v3_dot(f, eye);
}

void m4_ortho(mat4_t *out, float left, float right, float bottom, float top, float near_z, float far_z) {
    m4_identity(out);
    float rl = right - left, tb = top - bottom, fn = far_z - near_z;
    if (rl == 0.0f || tb == 0.0f || fn == 0.0f) return;
    out->m[0] = 2.0f / rl;
    out->m[5] = 2.0f / tb;
    out->m[10] = -2.0f / fn;
    out->m[12] = -(right + left) / rl;
    out->m[13] = -(top + bottom) / tb;
    out->m[14] = -(far_z + near_z) / fn;
}

void m4_rot_y(mat4_t *out, float deg) {
    float c = cosf(deg * M3_DEG2RAD), s = sinf(deg * M3_DEG2RAD);
    m4_identity(out);
    out->m[0] = c; out->m[2] = -s;
    out->m[8] = s; out->m[10] = c;
}

void m4_translate(mat4_t *out, float x, float y, float z) {
    m4_identity(out);
    out->m[12] = x; out->m[13] = y; out->m[14] = z;
}

void m4_transform(const mat4_t *m, const float v[3], float out[4]) {
    for (int row = 0; row < 4; row++) {
        out[row] = m->m[row] * v[0] + m->m[4 + row] * v[1] + m->m[8 + row] * v[2] + m->m[12 + row];
    }
}
