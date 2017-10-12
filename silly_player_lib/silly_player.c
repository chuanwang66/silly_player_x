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

#include "util/darray.h"
#include "util/platform.h"

extern int global_exit;
extern int global_exit_parse;
extern int active;

static volatile VideoState *is = NULL; //global video state

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

static void close_input()
{
	avformat_close_input(&is->pFormatCtx);
}

static int stream_component_open(unsigned int stream_index, const silly_audiospec *sa_desired, silly_audiospec *sa_obtained)
{
	AVCodec *codec = NULL;
	AVCodecContext *codecCtx = NULL;
	SDL_AudioSpec desired_spec, spec;

	if (stream_index >= is->pFormatCtx->nb_streams)
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

		desired_spec.callback = audio_callback;	//TODO use 'SDL_QueueAudio()' instead in a non-callback way
		desired_spec.userdata = is;

		if (SDL_OpenAudio(&desired_spec, &spec) < 0)
		{
			fprintf(stderr, "SDL_OpenAudio(): %s.\n", SDL_GetError());
			return -1;
		}

		//set is->audiospec
		if (spec.channels == av_get_channel_layout_nb_channels(AV_CH_LAYOUT_MONO))
			is->audiospec.channels = SA_CH_LAYOUT_MONO;
		else if (spec.channels == av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO))
			is->audiospec.channels = SA_CH_LAYOUT_STEREO;
		else
			is->audiospec.channels = SA_CH_LAYOUT_INVAL;


		if (spec.format == AUDIO_S16SYS)
			is->audiospec.format = SA_SAMPLE_FMT_S16;
		else if (spec.format == AUDIO_F32SYS)
			is->audiospec.format = SA_SAMPLE_FMT_FLT;
		else
			is->audiospec.format = SA_SAMPLE_FMT_INVAL;

		is->audiospec.samplerate = spec.freq;
		is->audiospec.samples = spec.samples;

		//set sa_obtained
		if (sa_obtained) {
			sa_obtained->channels = is->audiospec.channels;
			sa_obtained->format = is->audiospec.format;
			sa_obtained->samplerate = is->audiospec.samplerate;
			sa_obtained->samples = is->audiospec.samples;
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

		//prepare conversion facility (FLTP -> S16)
		//和swr_convert()保持一致: 一个声道最多MAX_AUDIO_FRAME_SIZE字节，假设最多两个声道
		is->out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE << 1);

		is->swr_ctx = swr_alloc();
		is->swr_ctx = swr_alloc_set_opts(is->swr_ctx,
			is->audiospec.channels == SA_CH_LAYOUT_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO,	//out_ch_layout
			is->audiospec.format == SA_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT,		//out_sample_fmt
			is->audiospec.samplerate,																//out_sample_rate
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

static int open_audio_decoder(const silly_audiospec *sa_desired, silly_audiospec *sa_obtained)
{
	unsigned int audio_stream_index = -1;
	bool audio_stream_index_found = false;
	unsigned int i;

	for (i = 0; i<is->pFormatCtx->nb_streams; ++i)
	{
		int media_type = is->pFormatCtx->streams[i]->codec->codec_type;

		if (media_type == AVMEDIA_TYPE_AUDIO && !audio_stream_index_found)
		{
			audio_stream_index = i;
			audio_stream_index_found = true;
			break;
		}
	}

	if (!audio_stream_index_found) {
		fprintf(stderr, "%s: could not find audio stream.\n", is->filename);
		return -1;
	}

	if (stream_component_open(audio_stream_index, sa_desired, sa_obtained) < 0)
	{
		fprintf(stderr, "%s: could not open audio codecs.\n", is->filename);
		return -2;
	}

	return 0;
}

static void close_audio_decoder()
{
	avcodec_close(is->audio_ctx);
	avcodec_free_context(&(is->audio_ctx));
	is->audio_ctx = NULL;

	av_free(is->out_buffer);
	swr_free(&is->swr_ctx);

	SDL_CloseAudio();
}

static int open_video_decoder()
{
	unsigned int video_stream_index = -1;
	bool video_stream_index_found = false;
	unsigned int i;

	for (i = 0; i<is->pFormatCtx->nb_streams; ++i)
	{
		int media_type = is->pFormatCtx->streams[i]->codec->codec_type;

		if (media_type == AVMEDIA_TYPE_VIDEO && !video_stream_index_found)
		{
			video_stream_index = i;
			video_stream_index_found = true;
			break;
		}
	}

	if (!video_stream_index_found) {
		fprintf(stderr, "%s: could not find video stream.\n", is->filename);
		return -1;
	}
	
	if (stream_component_open(video_stream_index, NULL, NULL) < 0)
	{
		fprintf(stderr, "%s: could not open video codecs.\n", is->filename);
		return -2;
	}

	return 0;
}

static void close_video_decoder()
{
	avcodec_close(is->video_ctx);
	avcodec_free_context(&(is->video_ctx));
	is->video_ctx = NULL;
}

static SDL_Thread *parse_tid = NULL;

//open audio file
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
//@param[in] loop: playing in loop-mode or not
//return 0 on success, negative on error
int silly_audio_open(const char *filename, const silly_audiospec *sa_desired, silly_audiospec *sa_obtained, bool loop)
{
	if (active)
		return -1;
	if (filename == 0 || *filename == 0)
		return -2;
	if (!sa_desired)
		return -3;

	global_exit = 0;
	global_exit_parse = 0;

	is = av_mallocz(sizeof(VideoState)); //memory allocation with alignment, why???
	is->audio_stream_index = -1;
	is->video_stream_index = -1;
	strncpy(is->filename, filename, sizeof(is->filename));
	is->seek_pos_sec = 0;
	is->audio_buf_size = 0;
	is->audio_buf_index = 0;
	is->loop = loop;

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

	active = 1;

	return 0;
}

//close audio file
void silly_audio_close()
{
	if (!active)
		return;
	active = 0;

	if (is->active_fetch)
		silly_audio_fetch_stop();

	global_exit = 1;
	global_exit_parse = 1;

	SDL_WaitThread(parse_tid, NULL);

	close_audio_decoder();

	close_input();

	SDL_Quit();

	av_free(is);
	is = NULL;
}

//pause playing
void silly_audio_pause()
{
	if (!active || global_exit_parse) return;

	SDL_PauseAudio(1);
}

//resume playing
void silly_audio_resume()
{
	if (!active || global_exit_parse) return;

	SDL_PauseAudio(0);
}

//seek audio sec
//@param[in] sec: seek position in second(s)
//return 0 on success, negative on error
int silly_audio_seek(int sec)
{
	if (!active) return -1;

	global_exit_parse = 1;
	SDL_WaitThread(parse_tid, NULL);
	global_exit_parse = 0;

	is->seek_pos_sec = sec;
	parse_tid = SDL_CreateThread(parse_thread, "PARSING_THREAD", is);
	if (!parse_tid) {
		fprintf(stderr, "create parsing thread failed.\n");
		av_free(is);
		return -2;
	}

	SDL_PauseAudio(0);

	return 0;
}

//get current position (in sec) of playing
//return the current position in second(s), negative on error
double silly_audio_time()
{
	if (!active) return -1.0;						//in-active
	//if (active && global_exit_parse) return -2.0;	//active & finished ==> audio is still playing while parsing is finished

	return get_audio_clock(is);
}

void silly_audio_loop(bool enable)
{
	if (!active) return;						//in-active
	if (active && global_exit_parse) { //active & finished
		printf("loop failed\n");	//TODO
		return;
	}

	is->loop = enable;
}

//get the audio duration
//return the duration in second(s)
double silly_audio_duration()
{
	if (!active)	return -1.0;

	int64_t duration = is->pFormatCtx->duration / AV_TIME_BASE;
	return duration;
}

static DARRAY(float) audio_fetch_array;	//used for conversion in 'audio fetching'

//start fetching audio samples
//@param[in] channels: SA_CH_LAYOUT_MONO / SA_CH_LAYOUT_STEREO
//@param[in] samplerate: samplerate required
int silly_audio_fetch_start(int channels, int samplerate)
{
	if (!active) return -1;							//in-active
	if (active && global_exit_parse) return -2;		//active & finished
	if (is->active_fetch) return -3;

	is->channels_fetch = channels;
	is->samplerate_fetch = samplerate;

	is->swr_ctx_fetch = swr_alloc();
	if (!is->swr_ctx_fetch) return -4;

	is->swr_ctx_fetch = swr_alloc_set_opts(is->swr_ctx_fetch,
		is->channels_fetch == SA_CH_LAYOUT_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO,	//out_ch_layout
		AV_SAMPLE_FMT_FLT,																	//out_sample_fmt
		is->samplerate_fetch,																	//out_sample_rate
		is->audiospec.channels == SA_CH_LAYOUT_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO,	//in_ch_layout
		is->audiospec.format == SA_SAMPLE_FMT_S16 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT,		//in_sample_fmt
		is->audiospec.samplerate,																//in_sample_rate
		0,		//log_offset
		NULL	//log_ctx
		);
	swr_init(is->swr_ctx_fetch);

	pthread_mutex_init_value(&is->audio_fetch_buffer_mutex);
	if (pthread_mutex_init(&is->audio_fetch_buffer_mutex, NULL) != 0)
		return -5;

	is->active_fetch = true;

	return 0;
}

//fill sample_buffer with audio samples given the samplerate
//@param[in] sample_buffer: buffer to be filled
//@param[in] sample_buffer_size: size (# of floats) of buffer to be filled
//@param[in] blocking
int silly_audio_fetch(float *sample_buffer, int sample_buffer_size, bool blocking)
{
	memset(sample_buffer, 0, sample_buffer_size * sizeof(float));
	return silly_audio_fetch_internal(sample_buffer, sample_buffer_size, blocking);
}
int silly_audio_fetch_internal(float *sample_buffer, int sample_buffer_size, bool blocking)
{
	if (!active) return -1;						//in-active
	if (active && global_exit_parse) return -2;	//active & finished
	if (!is->active_fetch) return -3;

	int to_channels = is->channels_fetch == SA_CH_LAYOUT_MONO ? 1 : 2;
	int to_samplerate = is->samplerate_fetch;
	int to_sample_buffer_size = sample_buffer_size;	//# of floats

	int from_channels = is->audiospec.channels == SA_CH_LAYOUT_MONO ? 1 : 2;
	int from_samplerate = is->audiospec.samplerate;
	int from_sample_buffer_size = //# of floats
			(float)to_sample_buffer_size 
			* ((float)from_samplerate / (float)to_samplerate)
			* ((float)from_channels / (float)to_channels);
	if (from_sample_buffer_size & 1 != 0)
		++ from_sample_buffer_size;

	while(true) {
		pthread_mutex_lock(&is->audio_fetch_buffer_mutex);
		if (is->audio_fetch_buffer.size >= from_sample_buffer_size * sizeof(float)) {
			pthread_mutex_unlock(&is->audio_fetch_buffer_mutex);
			break;
		}
		pthread_mutex_unlock(&is->audio_fetch_buffer_mutex);

		if (!blocking) {
			return -4;
		}

		if (global_exit_parse) {
			return -5;
		}
		os_sleep_ms(5);
	}

	if (!is->active_fetch) return -6;

	//pop out to is->audio_fetch
	da_resize(audio_fetch_array, from_sample_buffer_size * sizeof(float)); //is->audio_fetch.num: in bytes

	pthread_mutex_lock(&is->audio_fetch_buffer_mutex);
	if (is->audio_fetch_buffer.size < from_sample_buffer_size * sizeof(float)) {
		pthread_mutex_unlock(&is->audio_fetch_buffer_mutex);
		return -7;
	}
	circlebuf_pop_front(&is->audio_fetch_buffer, audio_fetch_array.array, from_sample_buffer_size * sizeof(float));
	pthread_mutex_unlock(&is->audio_fetch_buffer_mutex);

	//is->audio_fetch ==> sample_buffer
	if (swr_convert(is->swr_ctx_fetch,
		(uint8_t **)&sample_buffer,				//out
		to_sample_buffer_size / to_channels,		//out_count
		(const uint8_t **)&audio_fetch_array.array,	//in
		from_sample_buffer_size / from_channels	//in_count
		) < 0) {
		fprintf(stderr, "swr_convert: error while converting.\n");
		return -8;
	}

	return 0;
}

//stop fetching audio samples
void silly_audio_fetch_stop()
{
	if (!active) return;
	if (active && global_exit_parse) return;
	if (!is->active_fetch) return;

	is->active_fetch = false;

	swr_free(&is->swr_ctx_fetch);
	da_free(audio_fetch_array);

	pthread_mutex_lock(&is->audio_fetch_buffer_mutex);
	circlebuf_free(&is->audio_fetch_buffer);
	pthread_mutex_unlock(&is->audio_fetch_buffer_mutex);

	pthread_mutex_destroy(&is->audio_fetch_buffer_mutex);
}

//show silly_audiospec
//@param[in] spec: the audio spec structure to show
void silly_audio_printspec(const silly_audiospec *spec)
{
	if (!spec) return;

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