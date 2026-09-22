/* audio_psp.c — вывод звука на PSP через pspaudiolib.
 *
 * Библиотека (pspsdk/src/audio/pspaudiolib.c) держит по потоку на каждый канал.
 * Поток канала в цикле вызывает наш колбэк на очередной буфер и отдаёт заполненный
 * буфер в sceAudioOutputPannedBlocking — то есть скорость колбэка задаёт железо,
 * блокировать его нельзя. Колбэк здесь короткий: только audio_mix из core
 * (правило 14 CLAUDE.md — без malloc, ввода-вывода и блокировок).
 *
 * Факты, сверенные с $PSPDEV/psp/sdk/include/pspaudiolib.h и исходником библиотеки:
 *   - буфер колбэка — short[PSP_NUM_AUDIO_SAMPLES][2], стерео 16 бит интерливед;
 *   - reqn — число кадров (кадр = левый + правый сэмпл), равно PSP_NUM_AUDIO_SAMPLES;
 *   - частота фиксирована 44 100 Гц (core/audio.h: AUDIO_RATE);
 *   - pspAudioInit() резервирует все PSP_NUM_AUDIO_CHANNELS каналов сразу и
 *     возвращает 0 при успехе, -1 при неудаче;
 *   - сигнатура колбэка: void (*)(void *buf, unsigned int reqn, void *pdata).
 */
#include <pspaudiolib.h>
#include <pspthreadman.h>
#include <string.h>

#include "audio_psp.h"
#include "platform.h"

/* Канал микшера. Колбэк вешаем только на нулевой: всё сведение делает audio_mix,
 * остальные зарезервированные каналы молчат (их колбэки нулевые). */
#define AUDIO_PSP_CHANNEL 0

/* Пауза перед остановкой библиотеки. Один буфер звучит
 * PSP_NUM_AUDIO_SAMPLES / AUDIO_RATE ≈ 23 мс; берём с запасом на два буфера,
 * чтобы уже начатый колбэк успел вернуться до освобождения потоков и audio_t. */
#define AUDIO_PSP_DRAIN_US 60000

static audio_t *s_audio;   /* микшер; владелец — вызывающий (см. audio_psp.h) */
static int s_started;

/* Колбэк потока аудио. buf — стерео 16 бит интерливед на reqn кадров. */
static void audio_psp_callback(void *buf, unsigned int reqn, void *pdata) {
    audio_t *a = (audio_t *)pdata;
    short *out = (short *)buf;
    if (!a) {
        /* Микшера нет — тишина, но буфер заполнить обязаны: иначе в динамик
         * уйдёт предыдущее содержимое (буферов два, они переиспользуются). */
        memset(out, 0, (size_t)reqn * 2u * sizeof(short));
        return;
    }
    audio_mix(a, out, (int)reqn);
}

int audio_psp_start(audio_t *a) {
    if (!a) return -1;
    if (s_started) audio_psp_stop();
    if (pspAudioInit() != 0) {
        plat_log("audio: pspAudioInit не смог зарезервировать каналы");
        return -1;
    }
    s_audio = a;
    s_started = 1;
    pspAudioSetVolume(AUDIO_PSP_CHANNEL, PSP_VOLUME_MAX, PSP_VOLUME_MAX);
    /* Колбэк ставим последним: до этого поток канала уже запущен и выдаёт тишину. */
    pspAudioSetChannelCallback(AUDIO_PSP_CHANNEL, audio_psp_callback, s_audio);
    plat_log("audio: канал %d, %d Гц, %d кадров на колбэк",
             AUDIO_PSP_CHANNEL, AUDIO_RATE, PSP_NUM_AUDIO_SAMPLES);
    return 0;
}

void audio_psp_stop(void) {
    if (!s_started) return;
    /* 1. Снимаем колбэк — дальше поток канала отдаёт нули. */
    pspAudioSetChannelCallback(AUDIO_PSP_CHANNEL, NULL, NULL);
    /* 2. Просим потоки завершиться и ждём: колбэк не должен выполняться после
     *    того, как вызывающий освободит audio_t или эмбиент-буфер. pspAudioEnd
     *    удаляет потоки, не дожидаясь их выхода (sceKernelWaitThreadEnd в
     *    библиотеке закомментирован), поэтому пауза здесь обязательна. */
    pspAudioEndPre();
    sceKernelDelayThread(AUDIO_PSP_DRAIN_US);
    /* 3. Освобождаем потоки и каналы. */
    pspAudioEnd();
    s_started = 0;
    s_audio = NULL;
}
