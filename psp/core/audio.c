/* audio.c — генеративный синтезатор ARGUS: 8 голосов с ADSR, эмбиент-петля региона
 * и микшер, который вызывается из потока звука.
 *
 * Принципы (docs/TECH.md §2.5, CLAUDE.md п.14):
 *   — никаких глобальных переменных: всё изменяемое состояние живёт в audio_t;
 *     на файловом уровне только константные таблицы (read-only, потокобезопасны);
 *   — в audio_mix нет malloc, ввода-вывода, блокировок и функций libm:
 *     синус берётся из таблицы на 256 точек с линейной интерполяцией
 *     (sinf на каждый сэмпл при 44,1 кГц для MIPS 222 МГц слишком дорог),
 *     частота ноты — из таблицы полутонов и сдвига октав вместо powf;
 *   — детерминизм: шум и микро-разброс громкости считает свой xorshift, состояние
 *     которого лежит в audio_t, поэтому два прогона с одним seed совпадают побайтово.
 */
#include "audio.h"

/* ---- настройки микшера ---- */

#define A_RATE_INV     (1.0f / (float)AUDIO_RATE)
#define A_TAB          256            /* точек в таблице синуса */
#define A_STEP_MAX     5              /* audio_event_t.step: 0..5 */
#define A_CHORD        4              /* chord[] всегда заполнен целиком */

/* Вклад голосов и эмбиента в сумму. Один тон на максимуме даёт ≈0,34 — заметно
 * громче эмбиента и при этом далеко от порога сжатия, так что обычная игра
 * (один-три звука сразу) проходит через микшер линейно, без окраски. */
#define A_VOICE_MIX    0.55f
#define A_AMBIENT_MIX  0.50f

/* Мягкое ограничение с коленом: |x| ≤ A_KNEE проходит без изменений, выше —
 * гладкое сжатие к ±1 по y = K + (1−K)·u/(1+u), где u = (|x|−K)/(1−K).
 * В колене совпадают и значение, и производная (=1), поэтому вход в сжатие
 * не слышен как щелчок, а результат строго меньше единицы: границы int16
 * микшер не достигает даже при восьми голосах на полной громкости.
 * Деление выполняется только на громких сэмплах — в тишине его в цикле нет. */
#define A_KNEE         0.75f
#define A_KNEE_SPAN    0.25f          /* 1 − A_KNEE */
#define A_KNEE_INV     4.0f           /* 1 / (1 − A_KNEE) */
#define A_SANE_MAX     1.0e6f         /* защита от inf/NaN в master: дальше сразу ±1 */

/* Эмбиент: моно 22 050 Гц играется на 44 100, поэтому шаг 0,5 сэмпла. */
#define A_AMBIENT_STEP 0.5f
/* Экспоненциальный подход громкости эмбиента к цели: постоянная времени ≈0,4 с. */
#define A_AMBIENT_LERP (1.0f / (0.4f * (float)AUDIO_RATE))
#define A_AMBIENT_SNAP 1.0e-5f        /* ближе этого — приравниваем, чтобы не ползти вечно */

#define A_ENV_EPS      0.0005f        /* уровень, ниже которого голос считаем отработавшим */
#define A_STEAL_LEVEL  0.06f          /* громче — голос не отбираем, событие теряем */

#define A_MIDI_MIN     12
#define A_MIDI_MAX     120            /* ≈8,4 кГц: заведомо ниже Найквиста */

/* ---- константные таблицы ---- */

/* sin(2π·i/256); 257-я точка дублирует нулевую, чтобы интерполяция не заворачивала индекс. */
static const float A_SIN[A_TAB + 1] = {
    0.00000000f, 0.02454123f, 0.04906767f, 0.07356456f, 0.09801714f, 0.12241068f, 0.14673047f, 0.17096189f,
    0.19509032f, 0.21910124f, 0.24298018f, 0.26671276f, 0.29028468f, 0.31368174f, 0.33688985f, 0.35989504f,
    0.38268343f, 0.40524131f, 0.42755509f, 0.44961133f, 0.47139674f, 0.49289819f, 0.51410274f, 0.53499762f,
    0.55557023f, 0.57580819f, 0.59569930f, 0.61523159f, 0.63439328f, 0.65317284f, 0.67155895f, 0.68954054f,
    0.70710678f, 0.72424708f, 0.74095113f, 0.75720885f, 0.77301045f, 0.78834643f, 0.80320753f, 0.81758481f,
    0.83146961f, 0.84485357f, 0.85772861f, 0.87008699f, 0.88192126f, 0.89322430f, 0.90398929f, 0.91420976f,
    0.92387953f, 0.93299280f, 0.94154407f, 0.94952818f, 0.95694034f, 0.96377607f, 0.97003125f, 0.97570213f,
    0.98078528f, 0.98527764f, 0.98917651f, 0.99247953f, 0.99518473f, 0.99729046f, 0.99879546f, 0.99969882f,
    1.00000000f, 0.99969882f, 0.99879546f, 0.99729046f, 0.99518473f, 0.99247953f, 0.98917651f, 0.98527764f,
    0.98078528f, 0.97570213f, 0.97003125f, 0.96377607f, 0.95694034f, 0.94952818f, 0.94154407f, 0.93299280f,
    0.92387953f, 0.91420976f, 0.90398929f, 0.89322430f, 0.88192126f, 0.87008699f, 0.85772861f, 0.84485357f,
    0.83146961f, 0.81758481f, 0.80320753f, 0.78834643f, 0.77301045f, 0.75720885f, 0.74095113f, 0.72424708f,
    0.70710678f, 0.68954054f, 0.67155895f, 0.65317284f, 0.63439328f, 0.61523159f, 0.59569930f, 0.57580819f,
    0.55557023f, 0.53499762f, 0.51410274f, 0.49289819f, 0.47139674f, 0.44961133f, 0.42755509f, 0.40524131f,
    0.38268343f, 0.35989504f, 0.33688985f, 0.31368174f, 0.29028468f, 0.26671276f, 0.24298018f, 0.21910124f,
    0.19509032f, 0.17096189f, 0.14673047f, 0.12241068f, 0.09801714f, 0.07356456f, 0.04906767f, 0.02454123f,
    0.00000000f, -0.02454123f, -0.04906767f, -0.07356456f, -0.09801714f, -0.12241068f, -0.14673047f, -0.17096189f,
    -0.19509032f, -0.21910124f, -0.24298018f, -0.26671276f, -0.29028468f, -0.31368174f, -0.33688985f, -0.35989504f,
    -0.38268343f, -0.40524131f, -0.42755509f, -0.44961133f, -0.47139674f, -0.49289819f, -0.51410274f, -0.53499762f,
    -0.55557023f, -0.57580819f, -0.59569930f, -0.61523159f, -0.63439328f, -0.65317284f, -0.67155895f, -0.68954054f,
    -0.70710678f, -0.72424708f, -0.74095113f, -0.75720885f, -0.77301045f, -0.78834643f, -0.80320753f, -0.81758481f,
    -0.83146961f, -0.84485357f, -0.85772861f, -0.87008699f, -0.88192126f, -0.89322430f, -0.90398929f, -0.91420976f,
    -0.92387953f, -0.93299280f, -0.94154407f, -0.94952818f, -0.95694034f, -0.96377607f, -0.97003125f, -0.97570213f,
    -0.98078528f, -0.98527764f, -0.98917651f, -0.99247953f, -0.99518473f, -0.99729046f, -0.99879546f, -0.99969882f,
    -1.00000000f, -0.99969882f, -0.99879546f, -0.99729046f, -0.99518473f, -0.99247953f, -0.98917651f, -0.98527764f,
    -0.98078528f, -0.97570213f, -0.97003125f, -0.96377607f, -0.95694034f, -0.94952818f, -0.94154407f, -0.93299280f,
    -0.92387953f, -0.91420976f, -0.90398929f, -0.89322430f, -0.88192126f, -0.87008699f, -0.85772861f, -0.84485357f,
    -0.83146961f, -0.81758481f, -0.80320753f, -0.78834643f, -0.77301045f, -0.75720885f, -0.74095113f, -0.72424708f,
    -0.70710678f, -0.68954054f, -0.67155895f, -0.65317284f, -0.63439328f, -0.61523159f, -0.59569930f, -0.57580819f,
    -0.55557023f, -0.53499762f, -0.51410274f, -0.49289819f, -0.47139674f, -0.44961133f, -0.42755509f, -0.40524131f,
    -0.38268343f, -0.35989504f, -0.33688985f, -0.31368174f, -0.29028468f, -0.26671276f, -0.24298018f, -0.21910124f,
    -0.19509032f, -0.17096189f, -0.14673047f, -0.12241068f, -0.09801714f, -0.07356456f, -0.04906767f, -0.02454123f,
    0.00000000f
};

/* 2^(k/12) для k = 0..11 — вместе со сдвигом октав даёт 440·2^((midi−69)/12) без powf. */
static const float A_SEMI[12] = {
    1.00000000f, 1.05946309f, 1.12246205f, 1.18920712f, 1.25992105f, 1.33483985f,
    1.41421356f, 1.49830708f, 1.58740105f, 1.68179283f, 1.78179744f, 1.88774863f
};

/* Описание вида события: какая ступень аккорда, какая форма волны, какая огибающая.
 * Тембры подобраны так, чтобы события различались на слух: мягкие синусы на успех,
 * низкий глухой треугольник на отказ, шум-щелчок на шаг, короткий меандр на попадание луча. */
typedef struct {
    int wave;          /* WAVE_* */
    int step;          /* базовая ступень аккорда, 0..5 */
    int octave;        /* сдвиг в октавах относительно корня */
    float gain;        /* базовая громкость события, 0..1 */
    int notes;         /* 1 — одна нота; >1 — арпеджио по соседним ступеням */
    int arp_frames;    /* задержка между нотами арпеджио, кадров микшера */
    adsr_t adsr;       /* attack, decay, sustain (уровень), release */
} sfx_def_t;

static const sfx_def_t A_SFX[SFX_COUNT] = {
    /* SFX_STEP      — тихий щелчок под ногой: шум, почти без хвоста. */
    { WAVE_NOISE,  0,  0, 0.10f, 1,    0, { 0.0012f, 0.028f, 0.00f, 0.020f } },
    /* SFX_INTERACT  — короткий мягкий тон: чистый синус третьей ступени. */
    { WAVE_SINE,   2,  1, 0.55f, 1,    0, { 0.0100f, 0.090f, 0.30f, 0.260f } },
    /* SFX_LEVER     — рычаг: треугольник, чуть «деревянный». */
    { WAVE_TRI,    1,  0, 0.50f, 1,    0, { 0.0060f, 0.120f, 0.26f, 0.300f } },
    /* SFX_MIRROR    — поворот зеркала: светлый синус выше по аккорду. */
    { WAVE_SINE,   3,  1, 0.48f, 1,    0, { 0.0080f, 0.110f, 0.24f, 0.320f } },
    /* SFX_BLOCK     — блок по камню: низкий треугольник с заметной атакой. */
    { WAVE_TRI,    0, -1, 0.52f, 1,    0, { 0.0040f, 0.170f, 0.12f, 0.240f } },
    /* SFX_PLATE     — нажатая плита: мягкий синус корня. */
    { WAVE_SINE,   1,  0, 0.45f, 1,    0, { 0.0050f, 0.100f, 0.20f, 0.280f } },
    /* SFX_BEAM_HIT  — луч дошёл до приёмника: короткий меандр, специально «электрический». */
    { WAVE_SQUARE, 4,  1, 0.22f, 1,    0, { 0.0030f, 0.060f, 0.00f, 0.060f } },
    /* SFX_EYE_SMALL — малый глаз: тон длиннее и мягче обычного взаимодействия. */
    { WAVE_SINE,   2,  1, 0.58f, 1,    0, { 0.0120f, 0.150f, 0.34f, 0.520f } },
    /* SFX_EYE_BIG   — большой глаз: светлое арпеджио из трёх нот через 0,16 с. */
    { WAVE_SINE,   0,  1, 0.62f, 3, 7056, { 0.0140f, 0.180f, 0.38f, 0.900f } },
    /* SFX_FEATHER   — перо павлина: воздушный треугольник с медленной атакой. */
    { WAVE_TRI,    5,  1, 0.32f, 1,    0, { 0.0300f, 0.200f, 0.30f, 0.700f } },
    /* SFX_DENY      — отказ: ниже и глуше всех, октавой вниз, почти без сустейна. */
    { WAVE_TRI,    0, -1, 0.46f, 1,    0, { 0.0140f, 0.230f, 0.08f, 0.300f } },
    /* SFX_UI_MOVE   — перемещение по меню: еле слышный короткий тик. */
    { WAVE_SINE,   1,  1, 0.18f, 1,    0, { 0.0020f, 0.035f, 0.00f, 0.030f } },
    /* SFX_UI_OK     — подтверждение в меню: тот же тон, что взаимодействие, но короче. */
    { WAVE_SINE,   2,  1, 0.42f, 1,    0, { 0.0060f, 0.080f, 0.22f, 0.200f } }
};

/* Аккорд по умолчанию: A3 + sus2/6 (пентатоника) — любые две ступени созвучны. */
static const int A_CHORD_DEFAULT[A_CHORD] = { 0, 2, 7, 9 };
#define A_ROOT_DEFAULT 57             /* A3, 220 Гц */

/* ---- мелкие утилиты ---- */

static float a_clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

/* Мягкое ограничение суммы (см. комментарий к A_KNEE). */
static float a_soft_clip(float x) {
    float ax = x < 0.0f ? -x : x;
    /* Сравнение через отрицание ловит и NaN: он уйдёт в ±1, а не в UB при (int). */
    if (!(ax <= A_SANE_MAX)) return x < 0.0f ? -1.0f : 1.0f;
    if (ax <= A_KNEE) return x;
    float u = (ax - A_KNEE) * A_KNEE_INV;
    float y = A_KNEE + A_KNEE_SPAN * (u / (1.0f + u));
    return x < 0.0f ? -y : y;
}

/* Барьер компилятора: полезная нагрузка записана до индекса очереди.
 * PSP — одно ядро с упорядоченной записью, поэтому барьеров ядра здесь не нужно. */
static void a_release_fence(void) {
#if defined(__GNUC__)
    __atomic_thread_fence(__ATOMIC_RELEASE);
#endif
}

/* xorshift32 на состоянии микшера; вызывается только из потока звука. */
static unsigned a_rand(audio_t *a) {
    unsigned x = a->rng;
    if (x == 0u) x = 0x9E3779B9u;     /* нулевое состояние xorshift не выходит из нуля */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    a->rng = x;
    return x;
}

/* Тот же генератор на состоянии голоса — шум каждого голоса независим. */
static unsigned a_voice_rand(voice_t *v) {
    unsigned x = v->rng;
    if (x == 0u) x = 0x2545F491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v->rng = x;
    return x;
}

/* freq = 440·2^((midi−69)/12): остаток полутонов берём из таблицы, октавы — умножением на 2. */
static float a_note_freq(int midi) {
    if (midi < A_MIDI_MIN) midi = A_MIDI_MIN;
    if (midi > A_MIDI_MAX) midi = A_MIDI_MAX;
    int n = midi - 69;
    int oct = n / 12;
    int rem = n - oct * 12;
    if (rem < 0) { rem += 12; oct -= 1; }
    float f = 440.0f * A_SEMI[rem];
    while (oct > 0) { f *= 2.0f; oct--; }
    while (oct < 0) { f *= 0.5f; oct++; }
    return f;
}

/* Скорость изменения огибающей: delta уровня за seconds секунд, в долях на сэмпл.
 * Нулевое или отрицательное время — ступень проходится за один сэмпл. */
static float a_env_rate(float delta, float seconds) {
    if (seconds <= 0.0f) return 1.0f;
    float r = delta * (1.0f / (float)AUDIO_RATE) / seconds;
    if (r <= 0.0f) return 1.0f;
    return r;
}

/* ---- формы волны ---- */

/* Один сэмпл текущей формы в диапазоне [−1, 1]; phase всегда лежит в [0, 1). */
static float a_wave(voice_t *v) {
    switch (v->wave) {
    case WAVE_TRI: {
        /* Начинается с нуля, как и синус, — при мягкой атаке щелчка нет. */
        float p = v->phase;
        if (p < 0.25f) return p * 4.0f;
        if (p < 0.75f) return 2.0f - p * 4.0f;
        return p * 4.0f - 4.0f;
    }
    case WAVE_SQUARE:
        /* Не ±1, а ±0,9: меандр и без того самый громкий по энергии. */
        return v->phase < 0.5f ? 0.9f : -0.9f;
    case WAVE_NOISE:
        /* 24 старших бита → [0, 1) → [−1, 1). */
        return (float)(a_voice_rand(v) >> 8) * (2.0f / 16777216.0f) - 1.0f;
    default: {
        /* WAVE_SINE: таблица 256 точек + линейная интерполяция. */
        float x = v->phase * (float)A_TAB;
        int i = (int)x;
        if (i < 0) i = 0;
        if (i > A_TAB - 1) i = A_TAB - 1;
        float f = x - (float)i;
        return A_SIN[i] + (A_SIN[i + 1] - A_SIN[i]) * f;
    }
    }
}

/* ---- огибающая ---- */

/* Один шаг ADSR. Ступени: 0 атака, 1 спад, 2 удержание, 3 затухание.
 * В adsr_t нет времени удержания, поэтому на удержании голос стоит столько же,
 * сколько длился спад; отсчёт ведёт hold_frames (после атаки он всегда свободен). */
static void a_env_advance(voice_t *v) {
    switch (v->stage) {
    case 0:
        v->env += v->env_step;
        if (v->env >= 1.0f) {
            v->env = 1.0f;
            v->stage = 1;
            v->env_step = a_env_rate(1.0f - v->adsr.sustain, v->adsr.decay);
        }
        break;
    case 1:
        v->env -= v->env_step;
        if (v->env <= v->adsr.sustain) {
            v->env = v->adsr.sustain;
            if (v->env <= A_ENV_EPS) {   /* сустейна нет — звук уже закончился */
                v->env = 0.0f;
                v->active = 0;
                break;
            }
            v->stage = 2;
            v->hold_frames = (int)(v->adsr.decay * (float)AUDIO_RATE);
            if (v->hold_frames < 1) v->hold_frames = 1;
        }
        break;
    case 2:
        v->hold_frames--;
        if (v->hold_frames <= 0) {
            v->hold_frames = 0;
            v->stage = 3;
            v->env_step = a_env_rate(v->env, v->adsr.release);
        }
        break;
    default:
        v->env -= v->env_step;
        if (v->env <= 0.0f) {
            v->env = 0.0f;
            v->active = 0;
            v->stage = 3;
        }
        break;
    }
}

/* ---- распределение голосов ---- */

/* Слышимость голоса; −1 — голос свободен. Нота арпеджио, ещё ждущая своей
 * задержки, оценивается по полной громкости, чтобы её не отобрали до вступления. */
static float a_voice_level(const voice_t *v) {
    if (!v->active) return -1.0f;
    if (v->stage == 0 && v->hold_frames > 0) return v->gain;
    return v->env * v->gain;
}

/* Заводит голос под событие. Вызывается только из потока микшера. */
static void a_voice_start(audio_t *a, const audio_event_t *ev) {
    if (ev->kind < 0 || ev->kind >= SFX_COUNT) return;
    const sfx_def_t *d = &A_SFX[ev->kind];

    int slot = -1;
    for (int i = 0; i < AUDIO_VOICES; i++) {
        if (!a->voices[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        /* Свободных голосов нет: забираем самый тихий, и только если он почти не слышен.
         * Обрывать громкую ноту нельзя — это щелчок, поэтому событие теряем. */
        int quiet = 0;
        float worst = a_voice_level(&a->voices[0]);
        for (int i = 1; i < AUDIO_VOICES; i++) {
            float l = a_voice_level(&a->voices[i]);
            if (l < worst) { worst = l; quiet = i; }
        }
        if (worst > A_STEAL_LEVEL) return;
        slot = quiet;
    }

    int step = ev->step;
    if (step < 0) step = 0;
    if (step > A_STEP_MAX) step = A_STEP_MAX;
    /* Ступени выше размера аккорда продолжают его вверх по октавам. */
    int midi = a->root_midi + a->chord[step % A_CHORD] + 12 * (step / A_CHORD) + 12 * d->octave;

    /* Индекс ноты в арпеджио: события пишутся подряд от базовой ступени. */
    int note = step - d->step;
    if (note < 0) note = 0;

    voice_t *v = &a->voices[slot];
    v->active = 1;
    v->wave = d->wave;
    v->freq = a_note_freq(midi);
    v->phase = 0.0f;
    /* ±5 % громкости: повторяющиеся шаги и тики меню не звучат машинно. */
    v->gain = a_clamp01(ev->gain) * (0.95f + (float)(a_rand(a) >> 8) * (0.10f / 16777216.0f));
    v->env = 0.0f;
    v->env_step = a_env_rate(1.0f, d->adsr.attack);
    v->stage = 0;
    v->hold_frames = note * d->arp_frames;   /* задержка вступления ноты арпеджио */
    v->adsr.attack = d->adsr.attack;
    v->adsr.decay = d->adsr.decay;
    v->adsr.sustain = a_clamp01(d->adsr.sustain);
    v->adsr.release = d->adsr.release;
    v->rng = a_rand(a) | 1u;
}

/* Вычерпывает очередь до q_read == q_write. Никаких блокировок: игровой поток
 * только двигает q_write вперёд, микшер — только q_read; оба индекса меняются
 * по одному и монотонно растут. Переполнение int (≈2·10⁹ событий, годы игры)
 * сделает pending отрицательным: audio_play тогда молча теряет события, а
 * ближайший вызов микшера синхронизирует индексы и очередь оживает. */
static void a_drain_queue(audio_t *a) {
    int w = a->q_write;
    int r = a->q_read;
    int pending = w - r;
    if (pending <= 0) {
        if (pending < 0) a->q_read = w;   /* индексы разъехались — синхронизируемся */
        return;
    }
    /* Игровой поток мог переполнить очередь и перезаписать хвост: читаем только
     * последние AUDIO_QUEUE событий, всё что старше — потеряно (так и задумано). */
    if (pending > AUDIO_QUEUE) r = w - AUDIO_QUEUE;
    while (r != w) {
        audio_event_t ev = a->queue[((unsigned)r) % AUDIO_QUEUE];
        a_voice_start(a, &ev);
        r++;
    }
    a->q_read = w;
}

/* ---- публичный интерфейс ---- */

void audio_init(audio_t *a, unsigned seed) {
    if (!a) return;
    for (int i = 0; i < AUDIO_VOICES; i++) {
        voice_t *v = &a->voices[i];
        v->active = 0;
        v->wave = WAVE_SINE;
        v->freq = 0.0f;
        v->phase = 0.0f;
        v->gain = 0.0f;
        v->env = 0.0f;
        v->env_step = 0.0f;
        v->stage = 0;
        v->hold_frames = 0;
        v->adsr.attack = 0.0f;
        v->adsr.decay = 0.0f;
        v->adsr.sustain = 0.0f;
        v->adsr.release = 0.0f;
        v->rng = 0u;
    }
    for (int i = 0; i < AUDIO_QUEUE; i++) {
        a->queue[i].kind = 0;
        a->queue[i].step = 0;
        a->queue[i].gain = 0.0f;
    }
    a->q_write = 0;
    a->q_read = 0;

    a->ambient = 0;
    a->ambient_len = 0;
    a->ambient_pos = 0.0f;
    a->ambient_gain = 0.0f;
    a->ambient_target = 0.0f;

    for (int i = 0; i < A_CHORD; i++) a->chord[i] = A_CHORD_DEFAULT[i];
    a->root_midi = A_ROOT_DEFAULT;
    a->master = 1.0f;
    a->rng = seed ? seed : 0x9E3779B9u;
}

void audio_set_chord(audio_t *a, int root_midi, const int *semitones, int count) {
    if (!a) return;
    /* count вне диапазона или отсутствующая таблица — состояние не меняем вовсе. */
    if (!semitones || count < 1 || count > A_CHORD) return;
    if (root_midi < A_MIDI_MIN || root_midi > A_MIDI_MAX) return;

    for (int i = 0; i < count; i++) a->chord[i] = semitones[i];
    /* Аккорд короче четырёх ступеней достраиваем вверх октавами: chord[] всегда полон,
     * поэтому отдельное поле count не нужно, а ступени 0..5 остаются осмысленными. */
    for (int i = count; i < A_CHORD; i++) a->chord[i] = a->chord[i - count] + 12;
    a->root_midi = root_midi;
}

void audio_set_ambient(audio_t *a, const short *pcm, int len, float gain) {
    if (!a) return;
    /* Буфером владеет вызывающая сторона: освобождать старый эмбиент можно только
     * после того, как отработал хотя бы один audio_mix (микшер кэширует указатель
     * на время одного вызова). Плавная смена региона: сперва target = 0, дать
     * громкости уйти в ноль, и только потом подставлять новую петлю. */
    if (!pcm || len < 1) {
        a->ambient_len = 0;
        a_release_fence();
        a->ambient = 0;
        a->ambient_pos = 0.0f;
        a->ambient_target = 0.0f;
        return;
    }
    /* Сначала обнуляем длину — микшер перестаёт читать старый буфер; только потом
     * меняем указатель. Порядок держит барьер компилятора. */
    a->ambient_len = 0;
    a_release_fence();
    a->ambient = pcm;
    a->ambient_pos = 0.0f;
    a_release_fence();
    a->ambient_len = len;
    a->ambient_target = a_clamp01(gain);
}

void audio_play(audio_t *a, int kind) {
    if (!a || kind < 0 || kind >= SFX_COUNT) return;
    const sfx_def_t *d = &A_SFX[kind];
    int notes = d->notes < 1 ? 1 : d->notes;

    for (int n = 0; n < notes; n++) {
        int pending = a->q_write - a->q_read;
        /* Очередь полна — событие теряется: игровой поток не ждёт микшер
         * и не берёт блокировок, потерянный щелчок дешевле пропущенного кадра. */
        if (pending < 0 || pending >= AUDIO_QUEUE) return;

        int step = d->step + n;
        if (step > A_STEP_MAX) step = A_STEP_MAX;

        audio_event_t *ev = &a->queue[((unsigned)a->q_write) % AUDIO_QUEUE];
        ev->kind = kind;
        ev->step = step;
        ev->gain = d->gain * (1.0f - 0.14f * (float)n);   /* ноты арпеджио чуть тише */

        a_release_fence();          /* запись события видна до сдвига индекса */
        a->q_write = a->q_write + 1;
    }
}

void audio_mix(audio_t *a, short *out, int frames) {
    if (!a || !out || frames <= 0) return;

    a_drain_queue(a);

    const short *amb = a->ambient;
    int amb_len = a->ambient_len;
    if (!amb) amb_len = 0;
    float master = a->master;
    if (master < 0.0f) master = 0.0f;

    float pos = a->ambient_pos;
    if (amb_len > 0) {
        if (pos < 0.0f || pos >= (float)amb_len) pos = 0.0f;
    }
    float gain = a->ambient_gain;
    float target = a_clamp01(a->ambient_target);

    for (int i = 0; i < frames; i++) {
        /* --- голоса --- */
        float acc = 0.0f;
        for (int k = 0; k < AUDIO_VOICES; k++) {
            voice_t *v = &a->voices[k];
            if (!v->active) continue;
            if (v->stage == 0 && v->hold_frames > 0) {
                v->hold_frames--;   /* нота арпеджио ещё ждёт вступления */
                continue;
            }
            acc += a_wave(v) * v->env * v->gain;
            v->phase += v->freq * A_RATE_INV;
            if (v->phase >= 1.0f) v->phase -= 1.0f;
            a_env_advance(v);
        }

        /* --- эмбиент: моно 22 050 Гц, дробный шаг 0,5 с линейной интерполяцией --- */
        float d = target - gain;
        if (d > -A_AMBIENT_SNAP && d < A_AMBIENT_SNAP) gain = target;
        else gain += d * A_AMBIENT_LERP;

        float amb_s = 0.0f;
        if (amb_len > 0) {
            int i0 = (int)pos;
            if (i0 < 0) i0 = 0;                /* страховка от краевых случаев float */
            if (i0 >= amb_len) i0 = amb_len - 1;
            int i1 = i0 + 1;
            if (i1 >= amb_len) i1 = 0;      /* стык петли: за последним сэмплом идёт первый */
            float f = pos - (float)i0;
            float s0 = (float)amb[i0];
            float s1 = (float)amb[i1];
            amb_s = (s0 + (s1 - s0) * f) * (1.0f / 32768.0f);
            pos += A_AMBIENT_STEP;
            if (pos >= (float)amb_len) pos -= (float)amb_len;
        }

        /* --- сумма, мягкое ограничение, стерео-интерливед (оба канала одинаковы) --- */
        float mix = (acc * A_VOICE_MIX + amb_s * gain * A_AMBIENT_MIX) * master;
        float y = a_soft_clip(mix);

        int s = (int)(y * 32767.0f + (y >= 0.0f ? 0.5f : -0.5f));
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        out[2 * i] = (short)s;
        out[2 * i + 1] = (short)s;
    }

    a->ambient_pos = pos;
    a->ambient_gain = gain;
}
