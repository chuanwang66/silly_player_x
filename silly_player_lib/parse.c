#include "c99defs.h"

#include "silly_player_internal.h"
#include "parse.h"

extern int global_exit;
extern int global_exit_parse;

void seek_to(VideoState *is, uint32_t seek_pos_sec)
{
	AVRational time_base = is->audio_st->time_base;
	int64_t seek_time = is->audio_st->start_time + av_rescale(seek_pos_sec, time_base.den, time_base.num);

	if (seek_time > is->audio_st->cur_dts) {
		av_seek_frame(is->pFormatCtx, is->audio_stream_index, seek_time, AVSEEK_FLAG_ANY);
	}
	else {
		av_seek_frame(is->pFormatCtx, is->audio_stream_index, seek_time, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
	}
}

int parse_thread(void *arg)
{
    VideoState *is = (VideoState *)arg;
    AVPacket pkt1, *packet = &pkt1;
	int ret;

    //seek to position (audio seeking supported ONLY)
    packet_queue_clear(&is->audioq);

	seek_to(is, is->seek_pos_sec);

    for(;;)
    {
        if(global_exit_parse) break;
        //seek stuff goes here ???

        //reading too fast, slow down!
        if(is->audioq.size > MAX_AUDIOQ_SIZE || is->videoq.size > MAX_VIDEOQ_SIZE)
        {
            SDL_Delay(10);
            continue;
        }
        if((ret = av_read_frame(is->pFormatCtx, packet)) < 0)
        {
			if (ret == AVERROR_EOF || url_feof(is->pFormatCtx->pb))
			{
				if (!is->loop) {
					global_exit_parse = 1;
					break;
				}
				else {
					seek_to(is, 0);
					continue;
				}
			}

            if(is->pFormatCtx->pb->error == 0)
            {
                SDL_Delay(100); /* no error; wait for user input */
                continue;
            }
            else
            {
				global_exit_parse = 1;
                break;
            }
        }

        if(packet->stream_index == is->audio_stream_index)
        {
            packet_queue_put(&is->audioq, packet);
        }
        else if(packet->stream_index == is->video_stream_index)
        {
            packet_queue_put(&is->videoq, packet);
        }
        else
        {
            av_free_packet(packet);
        }
    }

    /* wait for quitting */
    while(!global_exit_parse)
    {
        SDL_Delay(100);
    }

    return 0;
}
