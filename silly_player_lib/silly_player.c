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

static int stream_component_open(int stream_index, silly_audiospec *sa_desired, silly_audiospec *sa_obtained)
{
	AVCodec *codec = NULL;
	AVCodecContext *codecCtx = NULL;
	SDL_AudioSpec desired_spec, spec;

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
		desired_spec.channels = sa_desired->channels == SA_CH_LAYOUT_MONO ? av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO) : av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
		desired_spec.format = sa_desired->format == SA_SAMPLE_FMT_S16 ? AUDIO_S16SYS : AUDIO_F32SYS;
		desired_spec.freq = codecCtx->sample_rate;

		desired_spec.silence = 0;
		desired_spec.samples = sa_desired->samples;

		desired_spec.callback = audio_callback;
		desired_spec.userdata = is;

		if (SDL_OpenAudio(&desired_spec, &spec) < 0)
		{
			fprintf(stderr, "SDL_OpenAudio(): %s.\n", SDL_GetError());
			return -1;
		}
		if (sa_obtained) {
			is->audiospec.channels = sa_obtained->channels = SA_CH_LAYOUT_INVAL;
			if (spec.channels == av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO))
				is->audiospec.channels = sa_obtained->channels = SA_CH_LAYOUT_MONO;
			if (spec.channels == av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO))
				is->audiospec.channels = sa_obtained->channels = SA_CH_LAYOUT_STEREO;

			is->audiospec.format = sa_obtained->format = SA_SAMPLE_FMT_INVAL;
			if (spec.format == AUDIO_S16SYS)
				is->audiospec.format = sa_obtained->format = SA_SAMPLE_FMT_S16;
			if (spec.format == AUDIO_F32SYS)
				is->audiospec.format = sa_obtained->format = SA_SAMPLE_FMT_FLT;

			is->audiospec.samplerate = sa_obtained->samplerate = spec.freq;
			is->audiospec.samples = sa_obtained->samples = spec.samples;
		}

		is->audio_hw_buf_size = spec.size;
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
			sa_obtained->channels == SA_CH_LAYOUT_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO,	//out_ch_layout
			sa_obtained->format == SA_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT,		//out_sample_fmt
			sa_obtained->samplerate,																//out_sample_rate
			av_get_default_channel_layout(codecCtx->channels),		//in_ch_layout
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

static int open_audio_decoder(silly_audiospec *sa_desired, silly_audiospec *sa_obtained)
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
		if (stream_component_open(audio_stream_index, sa_desired, sa_obtained) < 0)
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
		if (stream_component_open(video_stream_index, NULL, NULL) < 0)
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

//@param[in] filename: audio to be played
//@param[in] sa_desired: audio sepc desired
//			sa_desired.channels:	SA_CH_LAYOUT_MONO, SA_CH_LAYOUT_STEREO
//			sa_desired.format:		SA_SAMPLE_FMT_S16, SA_SAMPLE_FMT_FLT
//			sa_desired.samplerate:	UNUSED
//			sa_desired.samples:		audio buffer size in samples (power of 2)
//@param[out] sa_obtained: audio spec obtained
//			sa_obtained.channels:	SA_CH_LAYOUT_MONO, SA_CH_LAYOUT_STEREO
//			sa_obtained.format:		SA_SAMPLE_FMT_S16, SA_SAMPLE_FMT_FLT
//			sa_obtained.samplerate:	audio sample rate
//			sa_obtained.samples:	audio buffer size in samples (power of 2)
int silly_audio_open(const char *filename, silly_audiospec *sa_desired, silly_audiospec *sa_obtained)
{
	if (filename == 0 || *filename == 0)
		return -1;
	if (!sa_desired)
		return -2;

	is = av_mallocz(sizeof(VideoState)); //memory allocation with alignment, why???
	is->audio_stream_index = -1;
	is->video_stream_index = -1;
	strncpy(is->filename, filename, sizeof(is->filename));
	is->seek_pos_sec = 0;

	pthread_mutex_init_value(&is->audio_ring_mutex);
	if (pthread_mutex_init(&is->audio_ring_mutex, NULL) != 0)
		return -3;

	//register all formats & codecs
	av_register_all();

	//if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	if (SDL_Init(SDL_INIT_AUDIO)) {
		fprintf(stderr, "SDL_Init() error: %s\n", SDL_GetError());
		av_free(is);
		return -4;
	}

	if (open_input() != 0) {
		av_free(is);
		return -5;
	}

	if (open_audio_decoder(sa_desired, sa_obtained) != 0) {
		av_free(is);
		return -6;
	}

	//parsing thread (reading packets from stream)
	parse_tid = SDL_CreateThread(parse_thread, "PARSING_THREAD", is);
	if (!parse_tid) {
		fprintf(stderr, "create parsing thread failed.\n");
		av_free(is);
		return -7;
	}

	SDL_PauseAudio(0);

	return 0;
}

void silly_audio_printspec(const silly_audiospec *spec)
{
	fprintf(stderr, "spec->samples=%d (size of the audio buffer in samples)\n", spec->samples);

	fprintf(stderr, "spec->samplerate=%d (samples per second)\n", spec->samplerate);

	switch (spec->channels) {
	case SA_CH_LAYOUT_MONO:
		fprintf(stderr, "spec->channels=SA_CH_LAYOUT_MONO\n");
		break;
	case SA_CH_LAYOUT_STEREO:
		fprintf(stderr, "spec->channels=SA_CH_LAYOUT_STEREO\n");
		break;
	default:
		fprintf(stderr, "spec->channels=SA_CH_LAYOUT_INVAL\n");
		break;
	}

	switch (spec->format)
	{
	case SA_SAMPLE_FMT_S16:
		fprintf(stderr, "spec->format=SA_SAMPLE_FMT_S16\n");
		break;
	case SA_SAMPLE_FMT_FLT:
		fprintf(stderr, "spec->format=SA_SAMPLE_FMT_FLT\n");
		break;
	default:
		fprintf(stderr, "spec->format=SA_SAMPLE_FMT_INVAL\n");
		break;
	}
}

int silly_audio_close()
{
	global_exit = 1;
	global_exit_parse = 1;

	SDL_WaitThread(parse_tid, NULL);

	SDL_Quit();

	pthread_mutex_destroy(&is->audio_ring_mutex);
	circlebuf_free(&is->audio_ring);

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