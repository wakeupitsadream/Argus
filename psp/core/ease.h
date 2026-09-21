/* ease.h — кривые сглаживания и tween. Всё движение в игре идёт через них (CLAUDE.md, п.10). */
#ifndef ARGUS_EASE_H
#define ARGUS_EASE_H
#include <math.h>

static inline float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

static inline float ease_linear(float t) { return clamp01(t); }

static inline float ease_in_out_cubic(float t) {
    t = clamp01(t);
    if (t < 0.5f) return 4.0f * t * t * t;
    float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * 0.5f;
}

static inline float ease_out_cubic(float t) {
    t = clamp01(t);
    float u = 1.0f - t;
    return 1.0f - u * u * u;
}

static inline float ease_out_expo(float t) {
    t = clamp01(t);
    return t >= 1.0f ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

static inline float ease_out_back(float t) {
    t = clamp01(t);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

typedef float (*ease_fn)(float);

typedef struct {
    float from, to, value;
    int frames, elapsed;
} tween_t;

static inline void tween_set(tween_t *tw, float value) {
    tw->from = tw->to = tw->value = value;
    tw->frames = 0;
    tw->elapsed = 0;
}

static inline void tween_start(tween_t *tw, float to, int frames) {
    tw->from = tw->value;
    tw->to = to;
    tw->frames = frames > 0 ? frames : 1;
    tw->elapsed = 0;
}

static inline int tween_done(const tween_t *tw) { return tw->elapsed >= tw->frames; }

/* Один шаг (1/60 с). Возвращает текущее значение. */
static inline float tween_update(tween_t *tw, ease_fn ease) {
    if (tween_done(tw)) {
        tw->value = tw->to;
        return tw->value;
    }
    tw->elapsed++;
    float t = ease((float)tw->elapsed / (float)tw->frames);
    tw->value = tw->from + (tw->to - tw->from) * t;
    return tw->value;
}

#endif
