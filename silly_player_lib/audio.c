#include <assert.h>
#include "c99defs.h"

#include <libswresample/swresample.h>
#include <libavutil/time.h>

#include "silly_player_params.h"
#include "silly_player_internal.h"
#include "audio.h"

#define CONVERT_FMT_SWR
//#define SHOW_AUDIO_FRAME

extern int global_exit;

static float cmid(float x, float min, float max){
    return (x<min) ? min : ((x>max) ? max: x);
}

//decode one frame
//@param[in] is: 
//@param[out] audio_buf: would be filled with the frame decoded
//@param[in] audio_buf_size: size of audio_buf in bytes
//
//return: bytes of the frame decoded
static int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int audio_buf_size){
    int pkt_consumed, data_size = 0;

    //data_size: bytes of frame decoded
    data_size = av_samples_get_buffer_size(NULL,
		is->audiospec.channels == SA_CH_LAYOUT_MONO ? av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO) : av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO),
        is->audio_ctx->frame_size,
		is->audiospec.format == SA_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT,
        1);

    for(;;){
        //step 1. is->audio_pkt_ptr  ==解码==>  is->audio_frame  ==转码==>  is->out_buffer
        //  1.1 is->audio_pkt_ptr用完，跳到step 2.重新取出一个AVPacket *
        //  1.2 is->audio_pkt_ptr未用完，再解码出一个is->audio_frame
        while(is->audio_pkt_size > 0){
            int got_frame = 0;
            pkt_consumed = avcodec_decode_audio4(is->audio_ctx, &is->audio_frame, &got_frame, is->audio_pkt_ptr);  //pkt_consumed: how many bytes of packet consumed

            if(pkt_consumed < 0){
                is->audio_pkt_size = 0;
                break;
            }
            is->audio_pkt_data += pkt_consumed;
            is->audio_pkt_size -= pkt_consumed;

            if(got_frame){
                /*ATTENTION:
                    swr_convert(..., in_count)
                    in_count: number of input samples available in one channel
                    so half of data_size is provided here. HOLY SHIT!!!
                */
                if(swr_convert(is->swr_ctx, &is->out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)is->audio_frame.data, is->audio_frame.nb_samples) < 0){
                    fprintf(stderr, "swr_convert: error while converting.\n");
                    return -1;
                }
                memcpy(audio_buf, is->out_buffer, data_size);
            }

			is->current_clock = is->audio_clock;
			is->audio_clock += (double)data_size / (double)((is->audiospec.format == SA_SAMPLE_FMT_S16 ? 2 : 4) * (is->audiospec.channels == SA_CH_LAYOUT_MONO ? 1 : 2) * is->audio_ctx->frame_size);

            return data_size;
        }

        //step 2. 重新从PacketQueue取出一个AVPacket *
        if(is->audio_pkt_ptr->data){
            av_free_packet(is->audio_pkt_ptr); //free on destroy ???
        }
        if(global_exit){
            return -1;
        }

        if(packet_queue_get(&is->audioq, is->audio_pkt_ptr, 1) < 0){
            return -1;
        }
        is->audio_pkt_data = is->audio_pkt_ptr->data;
        is->audio_pkt_size = is->audio_pkt_ptr->size;

        if(is->audio_pkt_ptr->pts != AV_NOPTS_VALUE){ //???why
            //fprintf(stderr, "is->audio_clock=%f\n", is->audio_clock);
            is->audio_clock = av_q2d(is->audio_st->time_base) * is->audio_pkt_ptr->pts;
        }
    }
}

#define PRINT_TOTAL_SAMPLES 0
#if PRINT_TOTAL_SAMPLES == 1
static int total_samples = 0; //total sample number (1 sample: audio data of all channels)
#endif

//'len' bytes should be fed to 'stream'
void audio_callback(void *userdata, uint8_t *stream, int len){
    VideoState *is = (VideoState *)userdata;
	size_t actual_len, audio_size;

#if PRINT_TOTAL_SAMPLES == 1
	total_samples += (len / (is->audiospec.channels == SA_CH_LAYOUT_MONO ? 1 : 2) / (is->audiospec.format == SA_SAMPLE_FMT_S16 ? 2 : 4));
	fprintf(stderr, "%lf: total samples: %d\n", (float)av_gettime() / 1000000.0, total_samples);
#endif

    SDL_memset(stream, 0, len);  //SDL 2.0

    //take 'len' bytes from 'is->audio_buf' to 'stream'.
    //NOTE: if there's not enough in 'is-audio_buf', audio_decode_frame() more to fill it!
    while(len > 0){
        if(is->audio_buf_index >= is->audio_buf_size){  //we have sent all our data(in audio buf), decode more
            audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf));
            if(audio_size < 0){  //error, output silence
                is->audio_buf_size = SDL_AUDIO_BUFFER_SIZE;
                memset(is->audio_buf, 0, is->audio_buf_size);
            }else{
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }

        //there're data left in audio buf, feed to stream
		actual_len = min(is->audio_buf_size - is->audio_buf_index, len);
		SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, actual_len, SDL_MIX_MAXVOLUME);
		
		pthread_mutex_lock(&is->audio_ring_mutex);
		circlebuf_push_back(&is->audio_ring, (uint8_t *)is->audio_buf + is->audio_buf_index, actual_len);
		//printf("circlebuf size: %d\n", is->audio_ring.size);
		pthread_mutex_unlock(&is->audio_ring_mutex);

		len -= actual_len;
		stream += actual_len;
		is->audio_buf_index += actual_len;
    }
}

double get_audio_clock(VideoState *is) {
  double pts;
  pts = is->current_clock; /* maintained in the audio thread */
  return pts;
}
