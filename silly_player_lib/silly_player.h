#pragma once

#include "c99defs.h"
#include "silly_player_params.h"

#ifdef __cplusplus
extern "C" {
#endif

EXPORT int silly_audio_open(const char *filename, const silly_audiospec *sa_desired, silly_audiospec *sa_obtained);

EXPORT void silly_audio_close();

EXPORT void silly_audio_pause();

EXPORT void silly_audio_resume();

EXPORT int silly_audio_seek(int sec);

EXPORT double silly_audio_time();

EXPORT int silly_audio_fetch_start(int channels, int samplerate);
EXPORT int silly_audio_fetch(float *sample_buffer, int sample_buffer_size, bool blocking);
EXPORT void silly_audio_fetch_stop();

EXPORT void silly_audio_printspec(const silly_audiospec *spec);

#ifdef __cplusplus
};
#endif