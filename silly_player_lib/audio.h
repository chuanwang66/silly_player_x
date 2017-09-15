#pragma once

#include <stdint.h>
#include "silly_player_internal.h"

void audio_callback(void *userdata, uint8_t *stream, int len);
double get_audio_clock(VideoState *is);