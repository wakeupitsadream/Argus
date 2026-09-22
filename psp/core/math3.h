/* math3.h — минимальная 3D-математика для core: векторы, матрицы 4×4, проекция.
 * Матрицы — по столбцам (как в sceGum и OpenGL): элемент (строка r, столбец c) = m[c * 4 + r]. */
#ifndef ARGUS_MATH3_H
#define ARGUS_MATH3_H

#define M3_PI 3.14159265358979f
#define M3_DEG2RAD (M3_PI / 180.0f)

typedef struct { float m[16]; } mat4_t;

void v3_set(float out[3], float x, float y, float z);
void v3_add(float out[3], const float a[3], const float b[3]);
void v3_sub(float out[3], const float a[3], const float b[3]);
void v3_scale(float out[3], const float a[3], float k);
float v3_dot(const float a[3], const float b[3]);
void v3_cross(float out[3], const float a[3], const float b[3]);
float v3_len(const float a[3]);
/* Нормализует на месте; при нулевой длине оставляет вектор как есть и возвращает 0. */
int v3_norm(float v[3]);
/* Линейная интерполяция: out = a + (b - a) * t. */
void v3_lerp(float out[3], const float a[3], const float b[3], float t);

void m4_identity(mat4_t *out);
/* out = a · b (сначала применяется b, как при умножении матриц столбцов). out может совпадать с a или b. */
void m4_mul(mat4_t *out, const mat4_t *a, const mat4_t *b);
void m4_look_at(mat4_t *out, const float eye[3], const float center[3], const float up[3]);
void m4_ortho(mat4_t *out, float left, float right, float bottom, float top, float near_z, float far_z);
void m4_rot_y(mat4_t *out, float deg);
void m4_translate(mat4_t *out, float x, float y, float z);
/* out = m · (v, 1); out[3] — w. */
void m4_transform(const mat4_t *m, const float v[3], float out[4]);

#endif
