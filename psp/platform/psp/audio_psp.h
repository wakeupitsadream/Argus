/* audio_psp.h — вывод звука через pspaudiolib: колбэк отдаёт буфер в audio_mix. */
#ifndef ARGUS_AUDIO_PSP_H
#define ARGUS_AUDIO_PSP_H
#include "audio.h"
/* Запоминает указатель (не копирует) и запускает поток аудио. 0 при успехе. */
int audio_psp_start(audio_t *a);
void audio_psp_stop(void);
#endif
