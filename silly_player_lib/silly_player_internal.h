#pragma once

#include "c99defs.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "packet_queue.h"

#define MAX_AUDIO_FRAME_SIZE 192000
#define SDL_AUDIO_BUFFER_SIZE 1024

//swresampleת����Ĳ�����ʽ
//ע: SDL2���������ʽ�ص������в���
//ע: �ⲿ�����ȡ�������������ʽ�ص�
#define CONV_CHANNELS 2;		//1: mono, 2:stereo
#define CONV_AUDIO_FORMAT 2;	//1: signed 16 bits; 2: 32-bit float

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

	uint32_t seek_pos_sec; //seek position in seconds

	/** ************** audio related ************** */
	int audio_stream_index;
	AVStream *audio_st;
	AVCodecContext *audio_ctx;

	double current_clock;	//first pos decoded (in sec)
	double audio_clock;		//next first pos to be decoded (in sec)

	//ע: ����Ƶ���ж�����audio packet�������audioq����һ��audio packet���ܱ���Ϊ���audio frame������audio buffer
	//    ���audio_pkt������һ��û��ȫ�����audio packet��audio_pkt_data[0, ... , audio_pkt_size-1]�����е�ʣ�ಿ��

	//(1) packets are read from audio stream, appending to the audioq.
	PacketQueue audioq; //list of AVPacketList(a AVPacket Wrapper)

	//(2) a single packet is picked out, waiting for decoding into one or more frames.
	AVPacket *audio_pkt_ptr;
	//1 "audio packet" may be decoded into multiple "audio frames", that is,
	//audio_pkt_data[0, ... , audio_pkt_size-1] is the remaining part waiting for decoding
	uint8_t *audio_pkt_data;
	int audio_pkt_size;
	int audio_hw_buf_size; //the actual capacity of audio buffer

	//(3) copy frames into audio_buf[], which would be consumed by
	// the audio device later.
	AVFrame audio_frame;
	//audio_buf[audio_index, ... , audio_buf_size-1] is a bunch of audio frame
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) >> 1]; //why???
	size_t audio_buf_index;
	size_t audio_buf_size;

	SwrContext *swr_ctx; //to convert audio frame from AV_SAMPLE_FMT_FLTP to AV_SAMPLE_FMT_S16
	uint8_t *out_buffer; //to contain the conversion result

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