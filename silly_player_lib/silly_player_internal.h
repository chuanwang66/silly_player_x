#pragma once

#include "c99defs.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "packet_queue.h"
#include "silly_player_params.h"
#include "util/circlebuf.h"
#include "util/threading.h"

#define MAX_AUDIO_FRAME_SIZE 192000
#define SDL_AUDIO_BUFFER_SIZE 1024

//note: allocated once
typedef struct VideoPicture{
	//SDL_Overlay *bmp; //SDL2 counterpart ???
	//int width, height;
	AVFrame *pFrameYUV;
	int width, height;
	int allocated;
	double pts;
}VideoPicture;

typedef struct VideoState{
	AVFormatContext *pFormatCtx;
	struct SwsContext *sws_ctx;
	volatile bool loop;

	/** ************** audio related ************** */
	int audio_stream_index;
	AVStream *audio_st;
	AVCodecContext *audio_ctx;
	uint32_t seek_pos_sec; //seek position in seconds

	struct silly_audiospec audiospec;	//（转换后）音频采样格式

	double current_clock;	//first pos decoded (in sec)	当前已播放时间戳
	double audio_clock;		//next first pos to be decoded (in sec)	当前已解码时间戳

	//注: 从音频流中读出的audio packet放入队列audioq，但一个audio packet可能被解为多个audio frame并放入audio buffer
	//    因此audio_pkt就是上一次没完全解完的audio packet，audio_pkt_data[0, ... , audio_pkt_size-1]是其中的剩余部分

	//(1) packets are read from audio stream, appending to the audioq.
	PacketQueue audioq; //list of AVPacketList(a AVPacket Wrapper)

	//(2) a single packet is picked out, waiting for decoding into one or more frames.
	//one "audio packet" may be decoded into multiple "audio frames", that is,
	//audio_pkt_data[0, ... , audio_pkt_size-1] is the remaining part waiting for decoding
	AVPacket *audio_pkt_ptr;
	uint8_t *audio_pkt_data;
	int audio_pkt_size;

	//(3) copy frames into audio_buf[], which would be consumed by the audio device later.
	//audio_buf[audio_index, ... , audio_buf_size-1] is a bunch of audio frame
	AVFrame audio_frame;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) >> 1]; //why???
	size_t audio_buf_index;
	size_t audio_buf_size;

	SwrContext *swr_ctx; //to convert audio frame
	uint8_t *out_buffer; //to contain the conversion result

	/** ************** audio fetching related ************** */
	struct circlebuf audio_fetch_buffer;		//ring buffer to hold audio frames decoded, providing for callback
	pthread_mutex_t audio_fetch_buffer_mutex;

	int channels_fetch;
	int samplerate_fetch;
	SwrContext *swr_ctx_fetch;
	volatile bool active_fetch;

	/** ************** video related ************** */
	int video_stream_index;
	AVStream *video_st;
	AVCodecContext *video_ctx;

	double video_clock;
	double frame_timer; //predicted pts of the next video frame.
	double frame_last_pts; //(actual) pts of the last video frame.
	double frame_last_delay; //last delay of two adjacent video frames.

	//(1)video packet queue
	PacketQueue videoq;

	//(2)AVPacket allocated within "video decoding thread"

	//(3)AVFrame allocated within "video decoding thread"
	//AVFrame
	//  --(YUV conversion)--> VideoPicture
	//  --(copy into back buffer)--> pictq[0]
	VideoPicture pictq;
	int pictq_size;
	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;

	char filename[1024];
}VideoState;