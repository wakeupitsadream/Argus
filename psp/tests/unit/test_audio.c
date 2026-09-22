/* test_audio.c — синтезатор: огибающая, очередь событий, отсутствие клиппинга,
 * тишина в покое, бесшовная петля эмбиента, детерминизм и защита audio_set_chord. */
#include "minitest.h"
#include "tests.h"
#include "audio.h"

/* Константы микшера продублированы из core/audio.c: если там поменяется запас
 * по громкости или форма мягкого ограничения — этот тест первым скажет об этом. */
#define REF_VOICE_MIX   0.55f
#define REF_AMBIENT_MIX 0.50f
#define REF_KNEE        0.75f
#define REF_KNEE_SPAN   0.25f
#define REF_KNEE_INV    4.0f

/* Секунда звука: 44 100 кадров по два канала. Держим в BSS, а не на стеке. */
#define SEC_FRAMES AUDIO_RATE
static short g_buf[SEC_FRAMES * 2];
static short g_buf2[SEC_FRAMES * 2];

/* audio_t окружён «канарейками»: переполнение очереди не должно задевать соседей. */
static struct {
    unsigned char before[128];
    audio_t a;
    unsigned char after[128];
} g_guard;

static audio_t g_a, g_b;

/* Эмбиент-петли для тестов. */
#define AMB_LEN 441                    /* 441 сэмпл на 22 050 Гц = один период 50 Гц */
static short g_amb[AMB_LEN];
static short g_amb_loud[64];

/* ---- вспомогательное ---- */

static float ref_soft_clip(float x) {
    float ax = x < 0.0f ? -x : x;
    if (ax <= REF_KNEE) return x;
    float u = (ax - REF_KNEE) * REF_KNEE_INV;
    float y = REF_KNEE + REF_KNEE_SPAN * (u / (1.0f + u));
    return x < 0.0f ? -y : y;
}

static int ref_to_s16(float y) {
    int s = (int)(y * 32767.0f + (y >= 0.0f ? 0.5f : -0.5f));
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    return s;
}

static int guard_intact(void) {
    for (int i = 0; i < (int)sizeof(g_guard.before); i++) {
        if (g_guard.before[i] != 0xA5u) return 0;
    }
    for (int i = 0; i < (int)sizeof(g_guard.after); i++) {
        if (g_guard.after[i] != 0x5Au) return 0;
    }
    return 1;
}

static int active_voices(const audio_t *a) {
    int n = 0;
    for (int i = 0; i < AUDIO_VOICES; i++) {
        if (a->voices[i].active) n++;
    }
    return n;
}

/* Самая длинная серия подряд идущих сэмплов на границе int16. */
static int longest_edge_run(const short *buf, int frames) {
    int best = 0, run = 0;
    for (int i = 0; i < frames; i++) {
        short s = buf[2 * i];
        if (s == 32767 || s == -32768) {
            run++;
            if (run > best) best = run;
        } else {
            run = 0;
        }
    }
    return best;
}

static int peak_abs(const short *buf, int frames) {
    int best = 0;
    for (int i = 0; i < 2 * frames; i++) {
        int v = buf[i] < 0 ? -(int)buf[i] : (int)buf[i];
        if (v > best) best = v;
    }
    return best;
}

/* Восемь голосов на максимуме: все четыре формы волны, полная громкость,
 * огибающая заморожена на удержании, чтобы не спадала в течение теста. */
static void force_voices_max(audio_t *a) {
    for (int i = 0; i < AUDIO_VOICES; i++) {
        voice_t *v = &a->voices[i];
        v->active = 1;
        v->wave = i % 4;                      /* sine, tri, square, noise */
        v->freq = 220.0f * (float)(i + 1);    /* разные частоты — биения складываются */
        v->phase = 0.0f;
        v->gain = 1.0f;
        v->env = 1.0f;
        v->env_step = 0.0f;
        v->stage = 2;                         /* удержание */
        v->hold_frames = 1 << 24;             /* хватит на весь тест */
        v->adsr.attack = 0.01f;
        v->adsr.decay = 0.10f;
        v->adsr.sustain = 1.0f;
        v->adsr.release = 1.00f;
        v->rng = 0x1000u + (unsigned)i;
    }
}

/* Частота первого заведённого голоса после одного события. */
static float first_voice_freq(audio_t *a, int kind) {
    short tmp[4];
    audio_init(a, 0x5EEDu);
    audio_play(a, kind);
    audio_mix(a, tmp, 2);
    return a->voices[0].freq;
}

/* ---- огибающая ---- */

TEST(audio_envelope_shape) {
    audio_t *a = &g_a;
    short chunk[64];                 /* 32 кадра стерео */
    audio_init(a, 0xA17D10u);
    audio_play(a, SFX_INTERACT);

    /* Первый вызов забирает событие из очереди и заводит голос. */
    audio_mix(a, chunk, 32);
    CHECK_EQ(a->voices[0].active, 1);
    CHECK_EQ(a->voices[0].stage, 0);
    CHECK(a->voices[0].env > 0.0f);

    /* Атака: пока stage == 0, огибающая строго растёт и доходит до единицы. */
    float prev = a->voices[0].env;
    int attack_chunks = 0;
    while (a->voices[0].stage == 0 && attack_chunks < 200) {
        audio_mix(a, chunk, 32);
        CHECK(a->voices[0].env >= prev);
        prev = a->voices[0].env;
        attack_chunks++;
    }
    CHECK(attack_chunks > 2);            /* атака 0,010 с — это ~14 кусков по 32 кадра */
    CHECK(attack_chunks < 100);
    CHECK_NEAR(prev, 1.0f, 0.06f);       /* на пике огибающей — единица */

    /* Спад: значение падает до уровня sustain и не ниже. */
    while (a->voices[0].stage == 1 && attack_chunks < 400) {
        audio_mix(a, chunk, 32);
        CHECK(a->voices[0].env <= prev + 1e-6f);
        prev = a->voices[0].env;
        attack_chunks++;
    }
    CHECK_EQ(a->voices[0].stage, 2);
    CHECK_NEAR(prev, 0.30f, 0.02f);      /* sustain у SFX_INTERACT = 0,30 */

    /* Через полсекунды звук ещё живёт, через полторы — голос освобождён и тих. */
    audio_mix(a, g_buf, SEC_FRAMES / 4);
    CHECK_EQ(a->voices[0].active, 1);
    audio_mix(a, g_buf, SEC_FRAMES);
    audio_mix(a, g_buf, SEC_FRAMES / 2);
    CHECK_EQ(a->voices[0].active, 0);
    CHECK_NEAR(a->voices[0].env, 0.0f, 1e-6f);
    CHECK_EQ(active_voices(a), 0);

    /* Освобождённый слот переиспользуется следующим событием. */
    audio_play(a, SFX_UI_OK);
    audio_mix(a, chunk, 32);
    CHECK_EQ(a->voices[0].active, 1);
}

TEST(audio_envelope_release_reaches_zero) {
    audio_t *a = &g_a;
    /* Короткий щелчок без сустейна: голос обязан освободиться очень быстро. */
    audio_init(a, 0x11u);
    audio_play(a, SFX_STEP);
    audio_mix(a, g_buf, 4410);          /* 0,1 с — щелчок длится ~0,03 с */
    CHECK_EQ(active_voices(a), 0);

    /* Длинный тон большого глаза укладывается в три секунды вместе с арпеджио. */
    audio_init(a, 0x12u);
    audio_play(a, SFX_EYE_BIG);
    for (int i = 0; i < 3; i++) audio_mix(a, g_buf, SEC_FRAMES);
    CHECK_EQ(active_voices(a), 0);
}

/* ---- очередь событий ---- */

TEST(audio_queue_overflow_safe) {
    audio_t *a = &g_guard.a;
    for (int i = 0; i < (int)sizeof(g_guard.before); i++) g_guard.before[i] = 0xA5u;
    for (int i = 0; i < (int)sizeof(g_guard.after); i++) g_guard.after[i] = 0x5Au;
    audio_init(a, 0xC0FFEEu);

    /* Пишем сильно больше AUDIO_QUEUE, не давая микшеру вычерпать очередь. */
    for (int i = 0; i < 500; i++) audio_play(a, SFX_UI_MOVE);
    CHECK(guard_intact());
    CHECK_EQ(a->q_read, 0);
    CHECK_EQ(a->q_write, AUDIO_QUEUE);          /* лишние события потеряны, индекс не убежал */
    CHECK(a->q_write - a->q_read <= AUDIO_QUEUE);

    /* Арпеджио из трёх нот тоже не переполняет очередь сверх предела. */
    for (int i = 0; i < 50; i++) audio_play(a, SFX_EYE_BIG);
    CHECK_EQ(a->q_write, AUDIO_QUEUE);
    CHECK(guard_intact());

    /* Микшер вычерпывает всё до конца и заводит голоса. */
    audio_mix(a, g_buf, 64);
    CHECK_EQ(a->q_read, a->q_write);
    CHECK_EQ(active_voices(a), AUDIO_VOICES);   /* 16 событий на 8 голосов */
    CHECK(guard_intact());

    /* Состояние голосов остаётся осмысленным: частоты в слышимом диапазоне. */
    for (int i = 0; i < AUDIO_VOICES; i++) {
        CHECK(a->voices[i].freq > 10.0f);
        CHECK(a->voices[i].freq < (float)AUDIO_RATE * 0.5f);
        CHECK(a->voices[i].gain >= 0.0f);
        CHECK(a->voices[i].gain <= 1.1f);
    }

    /* После вычерпывания очередь снова принимает события. */
    audio_play(a, SFX_LEVER);
    CHECK_EQ(a->q_write - a->q_read, 1);
    CHECK(guard_intact());
}

TEST(audio_queue_events_reach_mixer) {
    audio_t *a = &g_a;
    audio_init(a, 0x2233u);
    CHECK_EQ(a->q_write, 0);

    audio_play(a, SFX_INTERACT);
    audio_play(a, SFX_DENY);
    audio_play(a, SFX_STEP);
    CHECK_EQ(a->q_write, 3);
    CHECK_EQ(a->q_read, 0);
    CHECK_EQ(active_voices(a), 0);      /* до микширования голоса не заводятся */

    audio_mix(a, g_buf, 16);
    CHECK_EQ(a->q_read, 3);
    CHECK_EQ(active_voices(a), 3);
    /* Формы волны разных событий действительно различаются. */
    CHECK_EQ(a->voices[0].wave, WAVE_SINE);
    CHECK_EQ(a->voices[1].wave, WAVE_TRI);
    CHECK_EQ(a->voices[2].wave, WAVE_NOISE);

    /* Неизвестный вид события игнорируется и очередь не двигает. */
    int w = a->q_write;
    audio_play(a, -1);
    audio_play(a, SFX_COUNT);
    audio_play(a, 1000);
    CHECK_EQ(a->q_write, w);
}

TEST(audio_event_voices_distinct) {
    audio_t *a = &g_a;
    /* Отказ должен быть ниже взаимодействия, шаг — заметно тише всех. */
    float f_interact = first_voice_freq(a, SFX_INTERACT);
    float f_deny = first_voice_freq(a, SFX_DENY);
    CHECK(f_deny < f_interact);
    CHECK(f_deny > 40.0f);

    audio_init(a, 0x777u);
    audio_play(a, SFX_STEP);
    audio_play(a, SFX_INTERACT);
    audio_mix(a, g_buf, 8);
    CHECK(a->voices[0].gain < a->voices[1].gain * 0.4f);

    /* Частота считается по формуле 440·2^((midi−69)/12): корень 69 и нулевой аккорд
     * дают для SFX_INTERACT (ступень 2, октава +1) ровно 880 Гц. */
    const int zero_chord[4] = { 0, 0, 0, 0 };
    audio_init(a, 0x888u);
    audio_set_chord(a, 69, zero_chord, 4);
    audio_play(a, SFX_INTERACT);
    audio_mix(a, g_buf, 4);
    CHECK_NEAR(a->voices[0].freq, 880.0f, 0.2f);

    /* И октавой ниже для SFX_DENY (ступень 0, октава −1). */
    audio_init(a, 0x889u);
    audio_set_chord(a, 69, zero_chord, 4);
    audio_play(a, SFX_DENY);
    audio_mix(a, g_buf, 4);
    CHECK_NEAR(a->voices[0].freq, 220.0f, 0.1f);
}

TEST(audio_arpeggio_spread_in_time) {
    audio_t *a = &g_a;
    audio_init(a, 0x999u);
    audio_play(a, SFX_EYE_BIG);
    CHECK_EQ(a->q_write, 3);                 /* три ноты — три события */

    audio_mix(a, g_buf, 1);
    CHECK_EQ(active_voices(a), 3);
    /* Первая нота звучит сразу, остальные ждут своей задержки в hold_frames. */
    CHECK_EQ(a->voices[0].hold_frames, 0);
    CHECK(a->voices[0].env > 0.0f);
    CHECK(a->voices[1].hold_frames > 6000);
    CHECK(a->voices[2].hold_frames > a->voices[1].hold_frames);
    CHECK_NEAR(a->voices[1].env, 0.0f, 1e-9f);
    CHECK_NEAR(a->voices[2].env, 0.0f, 1e-9f);
    /* Арпеджио идёт вверх по аккорду. */
    CHECK(a->voices[1].freq > a->voices[0].freq);
    CHECK(a->voices[2].freq > a->voices[1].freq);

    int d1 = a->voices[1].hold_frames;
    int d2 = a->voices[2].hold_frames;
    CHECK_NEAR((double)(d2 - d1), (double)d1, 2.0);   /* ноты разнесены равномерно */

    /* Дошли до вступления второй ноты — она зазвучала, третья ещё молчит. */
    audio_mix(a, g_buf, d1 + 8);
    CHECK_EQ(a->voices[1].hold_frames, 0);
    CHECK(a->voices[1].env > 0.0f);
    CHECK(a->voices[2].hold_frames > 0);
    CHECK_NEAR(a->voices[2].env, 0.0f, 1e-9f);
}

/* ---- клиппинг ---- */

TEST(audio_no_clipping_all_voices) {
    audio_t *a = &g_a;
    audio_init(a, 0x4B1Du);
    for (int i = 0; i < (int)(sizeof(g_amb_loud) / sizeof(g_amb_loud[0])); i++) {
        g_amb_loud[i] = (i < 32) ? 32767 : -32768;   /* эмбиент на полной шкале */
    }
    audio_set_ambient(a, g_amb_loud, (int)(sizeof(g_amb_loud) / sizeof(g_amb_loud[0])), 1.0f);
    a->ambient_gain = 1.0f;                          /* без плавного входа — сразу максимум */
    a->master = 1.0f;
    force_voices_max(a);

    audio_mix(a, g_buf, SEC_FRAMES);
    CHECK_EQ(active_voices(a), AUDIO_VOICES);        /* голоса не съело за секунду */

    /* Запаса хватает: до границы int16 не доходим вообще. */
    CHECK_EQ(longest_edge_run(g_buf, SEC_FRAMES), 0);
    CHECK(peak_abs(g_buf, SEC_FRAMES) < 32767);
    CHECK(peak_abs(g_buf, SEC_FRAMES) > 8000);       /* и при этом звук действительно громкий */

    /* Левый и правый каналы пишутся оба. */
    int nonzero = 0;
    for (int i = 0; i < 1000; i++) {
        if (g_buf[2 * i] != 0) nonzero++;
        CHECK_EQ(g_buf[2 * i], g_buf[2 * i + 1]);
    }
    CHECK(nonzero > 900);

    /* Даже при заведомом перегрузе мягкое ограничение не даёт переполнения int16. */
    force_voices_max(a);
    a->master = 6.0f;
    audio_mix(a, g_buf, SEC_FRAMES / 4);
    for (int i = 0; i < SEC_FRAMES / 2; i++) {
        CHECK(g_buf[i] >= -32768 && g_buf[i] <= 32767);
    }
    CHECK(peak_abs(g_buf, SEC_FRAMES / 4) <= 32767);
}

/* ---- тишина ---- */

TEST(audio_silence_when_idle) {
    audio_t *a = &g_a;
    audio_init(a, 0xDEADu);
    for (int i = 0; i < SEC_FRAMES * 2; i++) g_buf[i] = 0x3C3C;   /* мусор до вызова */

    audio_mix(a, g_buf, SEC_FRAMES);
    int nonzero = 0;
    for (int i = 0; i < SEC_FRAMES * 2; i++) {
        if (g_buf[i] != 0) nonzero++;
    }
    CHECK_EQ(nonzero, 0);
    CHECK_EQ(active_voices(a), 0);

    /* Эмбиент подключён, но громкость цели нулевая — по-прежнему строгий ноль. */
    for (int i = 0; i < AMB_LEN; i++) g_amb[i] = (short)(i * 70 - 15000);
    audio_set_ambient(a, g_amb, AMB_LEN, 0.0f);
    audio_mix(a, g_buf, 4410);
    for (int i = 0; i < 4410 * 2; i++) CHECK_EQ(g_buf[i], 0);

    /* master = 0 глушит даже играющие голоса. */
    audio_init(a, 0xBEEFu);
    a->master = 0.0f;
    force_voices_max(a);
    audio_mix(a, g_buf, 2048);
    for (int i = 0; i < 2048 * 2; i++) CHECK_EQ(g_buf[i], 0);

    /* Вырожденные аргументы не портят память и не падают. */
    audio_mix(a, g_buf, 0);
    audio_mix(a, g_buf, -5);
    audio_mix(a, 0, 16);
    audio_mix(0, g_buf, 16);
    audio_play(0, SFX_STEP);
    audio_set_ambient(0, g_amb, AMB_LEN, 1.0f);
    audio_set_chord(0, 60, 0, 1);
    audio_init(0, 1u);
}

/* ---- эмбиент ---- */

TEST(audio_ambient_loops_seamlessly) {
    audio_t *a = &g_a;
    /* Косинус ровно на один период: стык петли приходится на максимум,
     * поэтому любой разрыв при заворачивании сразу виден как скачок. */
    for (int i = 0; i < AMB_LEN; i++) {
        double ph = 6.283185307179586 * (double)i / (double)AMB_LEN;
        g_amb[i] = (short)(20000.0 * cos(ph) + (cos(ph) >= 0 ? 0.5 : -0.5));
    }
    audio_init(a, 0x1A1Au);
    audio_set_ambient(a, g_amb, AMB_LEN, 1.0f);
    a->ambient_gain = 1.0f;          /* фиксируем громкость: проверяем именно петлю */
    a->ambient_target = 1.0f;

    const int period = AMB_LEN * 2;  /* моно 22 050 Гц на 44 100 — шаг 0,5 */
    const int total = period * 3;
    audio_mix(a, g_buf, total);

    /* Выход строго периодичен: позиция возвращается ровно в ноль. */
    int diffs = 0;
    for (int f = 0; f < period * 2; f++) {
        if (g_buf[2 * f] != g_buf[2 * (f + period)]) diffs++;
    }
    CHECK_EQ(diffs, 0);
    CHECK_NEAR(a->ambient_pos, 0.0f, 1e-6f);

    /* На стыке нет скачка: соседние сэмплы вокруг петли отличаются на единицы. */
    for (int f = period - 4; f < period + 4; f++) {
        int d = g_buf[2 * (f + 1)] - g_buf[2 * f];
        if (d < 0) d = -d;
        CHECK(d <= 4);
    }
    /* А во всём буфере максимальный шаг ограничен формой волны (50 Гц). */
    int max_step = 0;
    for (int f = 0; f + 1 < total; f++) {
        int d = g_buf[2 * (f + 1)] - g_buf[2 * f];
        if (d < 0) d = -d;
        if (d > max_step) max_step = d;
    }
    CHECK(max_step < 100);
    CHECK(peak_abs(g_buf, total) > 5000);   /* эмбиент действительно слышен */

    /* Дробный шаг 0,5: значение на полусэмпле — среднее соседних отсчётов. */
    audio_init(a, 0x1B1Bu);
    audio_set_ambient(a, g_amb, AMB_LEN, 1.0f);
    a->ambient_gain = 1.0f;
    a->ambient_target = 1.0f;
    audio_mix(a, g_buf, 2);
    float mid = ((float)g_amb[0] + (float)g_amb[1]) * 0.5f * (1.0f / 32768.0f);
    int expect = ref_to_s16(ref_soft_clip(mid * REF_AMBIENT_MIX));
    CHECK_EQ(g_buf[2], expect);

    /* Отключение эмбиента возвращает тишину. */
    audio_set_ambient(a, 0, 0, 1.0f);
    audio_mix(a, g_buf, 512);
    for (int i = 0; i < 512 * 2; i++) CHECK_EQ(g_buf[i], 0);
}

TEST(audio_ambient_gain_glides) {
    audio_t *a = &g_a;
    for (int i = 0; i < AMB_LEN; i++) g_amb[i] = 12000;
    audio_init(a, 0x2C2Cu);
    audio_set_ambient(a, g_amb, AMB_LEN, 1.0f);
    CHECK_NEAR(a->ambient_gain, 0.0f, 1e-9f);

    /* Экспоненциальный подход: за 0,05 с уже слышно, но далеко не максимум. */
    audio_mix(a, g_buf, AUDIO_RATE / 20);
    CHECK(a->ambient_gain > 0.05f);
    CHECK(a->ambient_gain < 0.5f);
    float g1 = a->ambient_gain;

    /* Постоянная времени ≈0,4 с: за секунду проходим большую часть пути, */
    audio_mix(a, g_buf, SEC_FRAMES);
    CHECK(a->ambient_gain > g1);
    CHECK(a->ambient_gain > 0.85f);
    CHECK(a->ambient_gain <= 1.0f);
    /* за три — практически доходим до цели. */
    audio_mix(a, g_buf, SEC_FRAMES);
    audio_mix(a, g_buf, SEC_FRAMES);
    CHECK_NEAR(a->ambient_gain, 1.0f, 0.02f);

    /* И обратно к нулю. */
    a->ambient_target = 0.0f;
    audio_mix(a, g_buf, SEC_FRAMES);
    audio_mix(a, g_buf, SEC_FRAMES);
    CHECK_NEAR(a->ambient_gain, 0.0f, 0.01f);
    /* Ведение плавное: первые сэмплы после смены цели не обрываются в ноль. */
    CHECK(g_buf[0] != 0);
}

/* ---- детерминизм ---- */

/* Один и тот же сценарий: различаться может только seed. */
static void run_scene(audio_t *a, unsigned seed, short *out) {
    const int chord[3] = { 0, 3, 7 };
    audio_init(a, seed);
    audio_set_chord(a, 60, chord, 3);
    for (int i = 0; i < AMB_LEN; i++) {
        g_amb[i] = (short)((i * 137) % 30000 - 15000);
    }
    audio_set_ambient(a, g_amb, AMB_LEN, 0.7f);

    int pos = 0;
    const int chunk = 1024;
    const int kinds[6] = { SFX_STEP, SFX_INTERACT, SFX_EYE_BIG, SFX_STEP, SFX_DENY, SFX_FEATHER };
    for (int c = 0; pos + chunk <= SEC_FRAMES; c++) {
        if (c % 3 == 0) audio_play(a, kinds[(c / 3) % 6]);
        audio_mix(a, out + pos * 2, chunk);
        pos += chunk;
    }
}

TEST(audio_determinism) {
    run_scene(&g_a, 0x13572468u, g_buf);
    run_scene(&g_b, 0x13572468u, g_buf2);
    CHECK_EQ(memcmp(g_buf, g_buf2, sizeof(short) * 2 * (SEC_FRAMES / 1024) * 1024), 0);
    CHECK_EQ(g_a.rng, g_b.rng);
    CHECK_EQ(g_a.q_read, g_b.q_read);
    for (int i = 0; i < AUDIO_VOICES; i++) {
        CHECK_EQ(g_a.voices[i].active, g_b.voices[i].active);
        CHECK(g_a.voices[i].phase == g_b.voices[i].phase);
        CHECK(g_a.voices[i].env == g_b.voices[i].env);
        CHECK_EQ(g_a.voices[i].rng, g_b.voices[i].rng);
    }

    /* Другой seed — другой шум, иначе ГПСЧ никак не участвует. */
    run_scene(&g_b, 0x2468ACE0u, g_buf2);
    CHECK(memcmp(g_buf, g_buf2, sizeof(short) * 2 * (SEC_FRAMES / 1024) * 1024) != 0);

    /* Разбиение на куски не влияет на результат: сравниваем 4×512 и 1×2048. */
    audio_init(&g_a, 0x55u);
    audio_init(&g_b, 0x55u);
    audio_play(&g_a, SFX_EYE_SMALL);
    audio_play(&g_b, SFX_EYE_SMALL);
    audio_mix(&g_a, g_buf, 2048);
    for (int i = 0; i < 4; i++) audio_mix(&g_b, g_buf2 + i * 512 * 2, 512);
    CHECK_EQ(memcmp(g_buf, g_buf2, sizeof(short) * 2 * 2048), 0);
}

/* ---- синус из таблицы ---- */

TEST(audio_sine_table_matches_sinf) {
    audio_t *a = &g_a;
    audio_init(a, 0x3141u);
    a->master = 1.0f;
    /* Один синусный голос с замороженной огибающей — на выходе чистая волна. */
    voice_t *v = &a->voices[0];
    v->active = 1;
    v->wave = WAVE_SINE;
    v->freq = 441.0f;                 /* ровно 100 кадров на период */
    v->phase = 0.0f;
    v->gain = 1.0f;
    v->env = 1.0f;
    v->env_step = 0.0f;
    v->stage = 2;
    v->hold_frames = 1 << 24;
    v->adsr.attack = 0.01f;
    v->adsr.decay = 0.10f;
    v->adsr.sustain = 1.0f;
    v->adsr.release = 1.0f;
    v->rng = 1u;

    const int n = 4000;              /* 90 мс — дольше любого игрового звука */
    audio_mix(a, g_buf, n);
    int worst = 0;
    double sq = 0.0;
    for (int i = 0; i < n; i++) {
        float ph = (float)((double)i * 441.0 / (double)AUDIO_RATE);
        ph -= (float)(int)ph;
        float want = ref_soft_clip(sinf(6.28318531f * ph) * REF_VOICE_MIX);
        int d = g_buf[2 * i] - ref_to_s16(want);
        if (d < 0) d = -d;
        sq += (double)d * (double)d;
        if (d > worst) worst = d;
    }
    /* Амплитуда здесь 18 021 LSB: таблица на 256 точек с линейной интерполяцией
     * плюс накопление фазы во float дают единицы LSB, то есть −80 дБ. */
    CHECK(worst <= 4);
    CHECK(sqrt(sq / (double)n) < 2.0);

    /* Фаза остаётся в [0, 1): индекс таблицы никогда не выходит за границы. */
    CHECK(a->voices[0].phase >= 0.0f);
    CHECK(a->voices[0].phase < 1.0f);

    /* Высокая нота (близко к Найквисту) тоже не выводит фазу за диапазон. */
    a->voices[0].freq = 8000.0f;
    audio_mix(a, g_buf, 8192);
    CHECK(a->voices[0].phase >= 0.0f);
    CHECK(a->voices[0].phase < 1.0f);
}

/* ---- аккорд ---- */

TEST(audio_set_chord_guards) {
    audio_t *a = &g_a;
    audio_init(a, 0x606u);
    int saved[4];
    for (int i = 0; i < 4; i++) saved[i] = a->chord[i];
    int saved_root = a->root_midi;

    const int good[4] = { 0, 5, 7, 12 };
    /* count вне диапазона и отсутствующая таблица не меняют ничего. */
    audio_set_chord(a, 40, good, 0);
    audio_set_chord(a, 41, good, -1);
    audio_set_chord(a, 42, good, 5);
    audio_set_chord(a, 43, good, 100);
    audio_set_chord(a, 44, good, -100000);
    audio_set_chord(a, 45, 0, 4);
    audio_set_chord(a, -50, good, 4);        /* корень вне MIDI */
    audio_set_chord(a, 500, good, 4);
    for (int i = 0; i < 4; i++) CHECK_EQ(a->chord[i], saved[i]);
    CHECK_EQ(a->root_midi, saved_root);

    /* Корректный вызов проходит целиком. */
    audio_set_chord(a, 62, good, 4);
    CHECK_EQ(a->root_midi, 62);
    for (int i = 0; i < 4; i++) CHECK_EQ(a->chord[i], good[i]);

    /* Короткий аккорд достраивается октавами — ступени 0..5 остаются валидными. */
    const int triad[3] = { 0, 4, 7 };
    audio_set_chord(a, 57, triad, 3);
    CHECK_EQ(a->chord[0], 0);
    CHECK_EQ(a->chord[1], 4);
    CHECK_EQ(a->chord[2], 7);
    CHECK_EQ(a->chord[3], 12);

    const int single[1] = { 2 };
    audio_set_chord(a, 57, single, 1);
    CHECK_EQ(a->chord[0], 2);
    CHECK_EQ(a->chord[1], 14);
    CHECK_EQ(a->chord[2], 26);
    CHECK_EQ(a->chord[3], 38);

    /* После любого аккорда все виды событий дают конечные частоты в диапазоне. */
    for (int kind = 0; kind < SFX_COUNT; kind++) {
        audio_init(a, 0x707u + (unsigned)kind);
        audio_set_chord(a, 96, single, 1);   /* высокий корень — проверяем ограничение сверху */
        audio_play(a, kind);
        audio_mix(a, g_buf, 32);
        for (int i = 0; i < AUDIO_VOICES; i++) {
            if (!a->voices[i].active) continue;
            CHECK(a->voices[i].freq > 5.0f);
            CHECK(a->voices[i].freq < (float)AUDIO_RATE * 0.5f);
        }
    }
}

void tests_audio(void) {
    puts("audio tests");
    RUN(audio_envelope_shape);
    RUN(audio_envelope_release_reaches_zero);
    RUN(audio_queue_overflow_safe);
    RUN(audio_queue_events_reach_mixer);
    RUN(audio_event_voices_distinct);
    RUN(audio_arpeggio_spread_in_time);
    RUN(audio_no_clipping_all_voices);
    RUN(audio_silence_when_idle);
    RUN(audio_ambient_loops_seamlessly);
    RUN(audio_ambient_gain_glides);
    RUN(audio_determinism);
    RUN(audio_sine_table_matches_sinf);
    RUN(audio_set_chord_guards);
}
