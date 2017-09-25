#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define SA_CH_LAYOUT_INVAL	0x00000000	//invalid channel layout
#define SA_CH_LAYOUT_MONO	0x00000001	//1 channel
#define SA_CH_LAYOUT_STEREO	0x00000002	//2 channels

#define SA_SAMPLE_FMT_INVAL	0x00000000	//invalid audio format
#define SA_SAMPLE_FMT_S16	0x00000001	//signed 16 bits
#define SA_SAMPLE_FMT_FLT	0x00000002	//float

typedef struct silly_audiospec
{
	int channels;	//number of channels: SA_CH_LAYOUT_MONO, SA_CH_LAYOUT_STEREO
	int format;		//audio data format: SA_SAMPLE_FMT_S16, SA_SAMPLE_FMT_FLT
	int samplerate;	//samples per second
	int samples;	//audio buffer size in samples (power of 2)
}silly_audiospec;

#ifdef __cplusplus
};
#endif