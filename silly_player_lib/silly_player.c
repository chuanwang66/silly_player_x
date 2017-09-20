#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "c99defs.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <SDL.h>

#include "audio.h"
#include "video.h"
#include "packet_queue.h"
#include "parse.h"
#include "silly_player_internal.h"
#include "silly_player.h"

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__APPLE__)
#include <unistd.h>
#define Sleep(msecond) usleep(msecond * 1000)
#endif

extern int global_exit;
extern int global_exit_parse;
static VideoState *is; //global video state

static int open_input()
{
	if (avformat_open_input(&is->pFormatCtx, is->filename, NULL, NULL) != 0)
	{
		fprintf(stderr, "could not open video file.\n");
		return -1;
	}
	if (avformat_find_stream_info(is->pFormatCtx, NULL) < 0)
	{
		fprintf(stderr, "could not find stream info.\n");
		return -1;
	}
	av_dump_format(is->pFormatCtx, 0, is->filename, 0);
	return 0;
}

static int stream_component_open(int stream_index)
{
	AVCodec *codec = NULL;
	AVCodecContext *codecCtx = NULL;
	SDL_AudioSpec desired_spec, spec;
	int conv_spec_channels = CONV_CHANNELS;
	int conv_spec_format = CONV_AUDIO_FORMAT;

	if (stream_index < 0 || stream_index >= is->pFormatCtx->nb_streams)
	{
		fprintf(stderr, "stream index invalid: stream_index=%d, stream #=%d.\n", stream_index, is->pFormatCtx->nb_streams);
		return -1;
	}

	codec = avcodec_find_decoder(is->pFormatCtx->streams[stream_index]->codec->codec_id);
	if (!codec)
	{
		fprintf(stderr, "unsupported codec.\n");
		return -1;
	}

	codecCtx = avcodec_alloc_context3(codec);
	if (avcodec_copy_context(codecCtx, is->pFormatCtx->streams[stream_index]->codec) != 0)
	{
		fprintf(stderr, "could not build codecCtx");
		return -1;
	}

	//open SDL audio
	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO)
	{
		desired_spec.channels = conv_spec_channels == 1 ? av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO) : av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
		desired_spec.format = conv_spec_format == 1 ? AUDIO_S16SYS : AUDIO_F32SYS;
		desired_spec.freq = codecCtx->sample_rate;

		desired_spec.silence = 0;
		desired_spec.samples = codecCtx->frame_size;	//SDL_AUDIO_BUFFER_SIZE;

		desired_spec.callback = audio_callback;
		desired_spec.userdata = is;

		if (SDL_OpenAudio(&desired_spec, &spec) < 0)
		{
			fprintf(stderr, "SDL_OpenAudio(): %s.\n", SDL_GetError());
			return -1;
		}
		is->audio_hw_buf_size = spec.size;

		fprintf(stderr, "spec.samples=%d (size of the audio buffer in samples)\n", spec.samples);
		fprintf(stderr, "spec.freq=%d (samples per second)\n", spec.freq);
		fprintf(stderr, "spec.channels=%d\n", spec.channels);
		fprintf(stderr, "spec.format=%d (size & type of each sample)\n", spec.format);
	}

	//open decoder
	if (avcodec_open2(codecCtx, codec, NULL) < 0)
	{
		fprintf(stderr, "avcodec_open2() error.\n");
		return -1;
	}

	//initialize 'is' audio/video info
	switch (codecCtx->codec_type)
	{
	case AVMEDIA_TYPE_AUDIO:
		is->audio_stream_index = stream_index;
		is->audio_st = is->pFormatCtx->streams[stream_index];
		is->audio_ctx = codecCtx;

		//memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		is->audio_pkt_ptr = (AVPacket *)av_malloc(sizeof(AVPacket));
		av_init_packet(is->audio_pkt_ptr);

		is->audio_buf_size = 0;
		is->audio_buf_index = 0;

		//prepare conversion facility (FLTP -> S16)
		//和swr_convert()保持一致: 一个声道最多MAX_AUDIO_FRAME_SIZE字节，假设最多两个声道
		is->out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE << 1);

		is->swr_ctx = swr_alloc();
		is->swr_ctx = swr_alloc_set_opts(is->swr_ctx,
			conv_spec_channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO,	//out_ch_layout
			conv_spec_format == 1 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT,		//out_sample_fmt
			codecCtx->sample_rate,												//out_sample_rate
			av_get_default_channel_layout(is->audio_ctx->channels),	//in_ch_layout
			codecCtx->sample_fmt,									//in_sample_fmt
			codecCtx->sample_rate,									//in_sample_rate
			0,		//log_offset
			NULL	//log_ctx
			);
		swr_init(is->swr_ctx);
		break;
	case AVMEDIA_TYPE_VIDEO:
		is->video_stream_index = stream_index;
		is->video_st = is->pFormatCtx->streams[stream_index];
		is->video_ctx = codecCtx;
		is->frame_timer = (double)av_gettime() / 1000000.0;
		is->frame_last_delay = 40e-3; //40ms

		is->sws_ctx = sws_getContext(is->video_ctx->width, is->video_ctx->height,
			is->video_ctx->pix_fmt,
			is->video_ctx->width, is->video_ctx->height,
			AV_PIX_FMT_YUV420P, SWS_BICUBIC,
			NULL, NULL, NULL);

		break;
	}

	fprintf(stderr, "stream[%d] opened.\n", stream_index);
	return 0;
}

static int open_audio_decoder()
{
	int audio_stream_index = -1;
	int i;

	for (i = 0; i<is->pFormatCtx->nb_streams; ++i)
	{
		int media_type = is->pFormatCtx->streams[i]->codec->codec_type;

		if (media_type == AVMEDIA_TYPE_AUDIO && audio_stream_index == -1)
		{
			audio_stream_index = i;
			break;
		}
	}

	if (audio_stream_index >= 0)
	{
		if (stream_component_open(audio_stream_index) < 0)
		{
			fprintf(stderr, "%s: could not open audio codecs.\n", is->filename);
			return -1;
		}
	}
	else
	{
		fprintf(stderr, "%s: could not find audio stream.\n", is->filename);
		return -1;
	}

	return 0;
}

static int open_video_decoder()
{
	int video_stream_index = -1;
	int i;

	for (i = 0; i<is->pFormatCtx->nb_streams; ++i)
	{
		int media_type = is->pFormatCtx->streams[i]->codec->codec_type;

		if (media_type == AVMEDIA_TYPE_VIDEO && video_stream_index == -1)
		{
			video_stream_index = i;
			break;
		}
	}

	if (video_stream_index >= 0)
	{
		if (stream_component_open(video_stream_index) < 0)
		{
			fprintf(stderr, "%s: could not open video codecs.\n", is->filename);
			return -1;
		}

	}
	else
	{
		fprintf(stderr, "%s: could not find video stream.\n", is->filename);
		return -1;
	}

	return 0;
}

static SDL_Thread *parse_tid = NULL;

int silly_audio_startup(const char *filename)
{
	is = av_mallocz(sizeof(VideoState)); //memory allocation with alignment, why???
	is->audio_stream_index = -1;
	is->video_stream_index = -1;
	strncpy(is->filename, filename, sizeof(is->filename));
	is->seek_pos_sec = 0;

	//register all formats & codecs
	av_register_all();

	//if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	if (SDL_Init(SDL_INIT_AUDIO)) {
		fprintf(stderr, "SDL_Init() error: %s\n", SDL_GetError());
		av_free(is);
		return -1;
	}

	if (open_input() != 0) {
		av_free(is);
		return -2;
	}

	if (open_audio_decoder() != 0) {
		av_free(is);
		return -3;
	}

	//parsing thread (reading packets from stream)
	parse_tid = SDL_CreateThread(parse_thread, "PARSING_THREAD", is);
	if (!parse_tid) {
		fprintf(stderr, "create parsing thread failed.\n");
		av_free(is);
		return -4;
	}

	SDL_PauseAudio(0);

	return 0;
}

int silly_audio_shutdown()
{
	global_exit = 1;
	global_exit_parse = 1;

	SDL_WaitThread(parse_tid, NULL);

	SDL_Quit();

	return 0;
}

void silly_audio_pause()
{
	SDL_PauseAudio(1);
}

void silly_audio_resume()
{
	SDL_PauseAudio(0);
}

int silly_audio_seek(int sec)
{
	global_exit_parse = 1;
	SDL_WaitThread(parse_tid, NULL);
	global_exit_parse = 0;

	is->seek_pos_sec = sec;
	parse_tid = SDL_CreateThread(parse_thread, "SILLY_PLAYER_PARSING_THREAD", is);
	if (!parse_tid) {
		fprintf(stderr, "create parsing thread failed.\n");
		av_free(is);
		return -1;
	}

	SDL_PauseAudio(0);

	return 0;
}

double silly_audio_time()
{
	return get_audio_clock(is);
}