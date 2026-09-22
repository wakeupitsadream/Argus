/* audio.h — генеративный звук: эмбиент-петля региона плюс тональные события
 * (каждое взаимодействие звучит нотой из аккорда региона, как в Monument Valley).
 * Синтез целиком в core: платформа только отдаёт буфер микшеру. Музыки нет (GDD §1.6). */
#ifndef ARGUS_AUDIO_H
#define ARGUS_AUDIO_H

#define AUDIO_RATE 44100          /* pspaudiolib работает только на 44,1 кГц */
#define AUDIO_VOICES 8
#define AUDIO_QUEUE 16            /* очередь нот от игрового потока к микшеру */

enum { WAVE_SINE = 0, WAVE_TRI, WAVE_SQUARE, WAVE_NOISE };

/* События игры, которым соответствуют ноты аккорда региона. */
enum {
    SFX_STEP = 0, SFX_INTERACT, SFX_LEVER, SFX_MIRROR, SFX_BLOCK, SFX_PLATE,
    SFX_BEAM_HIT, SFX_EYE_SMALL, SFX_EYE_BIG, SFX_FEATHER, SFX_DENY, SFX_UI_MOVE,
    SFX_UI_OK, SFX_COUNT
};

typedef struct {
    float attack, decay, sustain, release; /* секунды; sustain — уровень 0..1 */
} adsr_t;

typedef struct {
    int active;
    int wave;
    float freq, phase, gain;
    float env, env_step;
    int stage;          /* 0 атака, 1 спад, 2 удержание, 3 затухание */
    int hold_frames;
    adsr_t adsr;
    unsigned rng;
} voice_t;

typedef struct {
    int kind;           /* SFX_* */
    int step;           /* смещение по аккорду (0..5) */
    float gain;
} audio_event_t;

typedef struct {
    voice_t voices[AUDIO_VOICES];
    /* Однонаправленная очередь: пишет игровой поток, читает поток микшера.
     * Индексы меняются по одному, поэтому блокировки не нужны. */
    audio_event_t queue[AUDIO_QUEUE];
    volatile int q_write, q_read;

    const short *ambient;   /* моно PCM, зациклен; владение снаружи */
    int ambient_len;
    float ambient_pos;      /* дробная позиция: петля 22 050 Гц играется на 44 100 */
    float ambient_gain, ambient_target;

    int chord[4];           /* полутоны аккорда региона от корня */
    int root_midi;          /* корень аккорда, номер MIDI */
    float master;
    unsigned rng;
} audio_t;

void audio_init(audio_t *a, unsigned seed);
/* Аккорд региона: корень (MIDI) и до четырёх полутоновых смещений. */
void audio_set_chord(audio_t *a, int root_midi, const int *semitones, int count);
/* Подключает эмбиент-петлю (моно 22 050 Гц). pcm может быть NULL — тишина. */
void audio_set_ambient(audio_t *a, const short *pcm, int len, float gain);
/* Ставит событие в очередь. Безопасно вызывать из игрового потока. */
void audio_play(audio_t *a, int kind);
/* Микширует frames кадров в стерео-буфер (интерливед). Вызывается из потока аудио:
 * без malloc, без ввода-вывода, без блокировок. */
void audio_mix(audio_t *a, short *out, int frames);

#endif
