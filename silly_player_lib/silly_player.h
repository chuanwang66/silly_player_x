#pragma once

#include "c99defs.h"

#ifdef __cplusplus
extern "C" {
#endif

EXPORT int silly_audio_startup(const char *filename);

EXPORT int silly_audio_shutdown();

EXPORT void silly_audio_pause();

EXPORT void silly_audio_resume();

EXPORT int silly_audio_seek(int sec);

EXPORT double silly_audio_time();

#ifdef __cplusplus
};
#endif