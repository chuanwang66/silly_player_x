#pragma once

#include "silly_player_internal.h"

int video_init(VideoState *is);
int video_thread(void *arg);
void video_display(VideoState *is);
