#include <stdio.h>
#include <stdlib.h>

#include <Windows.h>

#include "silly_player.h"

int LogOnce(const char* strFile, const void* buf, int nBufSize)
{
	static int bRemove = 0;
	FILE* pFile = NULL;

	if (strFile == NULL)
	{
		return 0;
	}

	if (!bRemove)
	{
		bRemove = 1;
		remove(strFile);
	}

	pFile = fopen(strFile, "ab");
	if (pFile)
	{
		fwrite(buf, nBufSize, 1, pFile);
		fclose(pFile);
	}
	return 1;
}

//您只需要修改已下3个宏，就可以测试各种情况
#define USE_MONO 0			//1:mono,  0:stereo
#define SAMPLERATE 44100
#define FRAMERATE 10240

#if USE_MONO == 1
int channels = SA_CH_LAYOUT_MONO;
#else
int channels = SA_CH_LAYOUT_STEREO;
#endif

int samplerate = SAMPLERATE;
#if USE_MONO == 1
float sample_buffer[FRAMERATE];
#else
float sample_buffer[FRAMERATE << 1];	//这个缓冲尽量不要太大(100ms级别)，以免对is->audio_fetch_buffer太敏感
#endif

int sample_buffer_size = sizeof(sample_buffer) / sizeof(sample_buffer[0]);

HANDLE grab_thread;
volatile bool grab_active = FALSE;
volatile bool grab_stop = FALSE;
DWORD WINAPI grab_thread_proc(LPVOID lpParam)
{
	int ret;
	int tick = 1000 * ((float)FRAMERATE / (float)samplerate); //* 0.9;

	if ( (ret = silly_audio_fetch_start(channels, samplerate)) == 0) grab_active = TRUE;
	else {
		fprintf(stderr, "silly_audio_fetch_start() failed: %d\n", ret);
		grab_active = FALSE;
		return -1;
	}

	while (!grab_stop) {
#if 0
		if(silly_audio_fetch(sample_buffer, sample_buffer_size, false) == -3)	//非阻塞方式: 缓冲不稳定时(用完时)，就会取不到数据
			fprintf(stderr, "samples not enough, try again\n");
#else
		silly_audio_fetch(sample_buffer, sample_buffer_size, true);	//阻塞方式: 每次调用不要阻塞太久
#endif

		LogOnce("audio_grab.pcm", sample_buffer, sample_buffer_size * sizeof(float));

		Sleep(tick);	//取走采样速率 >= 解码出采样速率
	}
	silly_audio_fetch_stop();

	return 0;
}

void grab_thread_start() {
	if (grab_active) {
		fprintf(stderr, "grab thread already started\n");
		return;
	}

	grab_stop = FALSE;
	grab_thread = CreateThread(NULL, 0, grab_thread_proc, NULL, 0, NULL);
	if (grab_thread == NULL) {
		fprintf(stderr, "create grab thread failed.\n");
	}
}

void grab_thread_stop() {
	if (!grab_active) {
		fprintf(stderr, "grab thread already stopped\n");
		return;
	}

	grab_stop = TRUE;
	WaitForSingleObject(grab_thread, INFINITE);
	CloseHandle(grab_thread);

	grab_active = FALSE;
}

int main(int argc, char* argv[])
{
	int ret;
	char line[128];
	char cmd[128];
	int param1;
	int num;

	int exit_process = 0;

    if(argc != 2) {
        fprintf(stderr, "usage: %s $VIDEO_FILE_NAME.\n", argv[0]);
        exit(1);
    }

	printf("\npress:\n");
	printf("\t 'open\\n' to start\n");
	printf("\t 'close\\n' to stop\n");
	printf("\t 'pause\\n' to pause\n");
	printf("\t 'resume\\n' to resume\n");
	printf("\t 'seek sec\\n' to seek to position\n");
	printf("\t 'loop 0/1\\n' to enable looping\n");
	printf("\t 'time\\n' to query current time\n");
	printf("\t 'duration\\n' to query duration\n");
	printf("\t 'gstart\\n' to start grabbing samples\n");
	printf("\t 'gstop\\n' to stop grabbing samples\n");
	printf("\t 'fix\\n' to fix XAudio2_7.dll\n");
	printf("\n");
	printf("\t 'quit\\n' to quit\n");

	ret = silly_audio_initialize();
	if (ret != 0) printf("silly_audio_initialize() failed: %d\n", ret);
	else printf("silly_audio_initialize() ok\n");

	do {
		fgets(line, sizeof(line), stdin);
		if ((num = sscanf(line, "%s %d", &cmd, &param1)) <= 0)
			continue;
		if (strcmpi(cmd, "open") == 0) {
			silly_audiospec desired_spec, spec;
			desired_spec.channels = SA_CH_LAYOUT_STEREO;
			desired_spec.format = SA_SAMPLE_FMT_FLT;
			desired_spec.samplerate = 0;
			desired_spec.samples = 1024;

			//silly_audio_open(argv[1], &desired_spec, NULL);
			if ((ret = silly_audio_open(argv[1], &desired_spec, &spec, false)) == 0) {
				silly_audio_printspec(&spec);
				printf("silly_audio_open() ok\n");
			}
			else printf("silly_audio_open() failed: %d\n", ret);
		}
		else if (strcmpi(cmd, "close") == 0) {
			silly_audio_close();
		}
		else if (strcmpi(cmd, "pause") == 0) {
			silly_audio_pause();
		}
		else if (strcmpi(cmd, "resume") == 0) {
			silly_audio_resume();
		}
		else if (strcmpi(cmd, "seek") == 0 && num == 2) {
			if (silly_audio_seek(param1) == 0)
				printf("seek to %d (sec)\n", param1);
			else
				printf("seek failed\n");
		}
		else if (strcmpi(cmd, "loop") == 0 && num == 2) {
			silly_audio_loop(param1 == 0 ? false: true);
		}
		else if (strcmpi(cmd, "time") == 0) {
			double ts = silly_audio_time();
			printf("current time: %f\n", ts);
		}
		else if (strcmpi(cmd, "duration") == 0) {
			int duration = silly_audio_duration();
			int h = duration / 3600;
			int m = duration / 60 - h * 60;
			int s = duration - m * 60 - h * 3600;
			printf("duration time: %d:%d:%d\n", h, m, s);
		}
		else if (strcmpi(cmd, "gstart") == 0) {
			grab_thread_start();
		}
		else if (strcmpi(cmd, "gstop") == 0) {
			grab_thread_stop();
		}
		else if (strcmpi(cmd, "fix") == 0) {
			silly_audio_fix();
		}
		else if (strcmpi(cmd, "quit") == 0) {
			grab_thread_stop();
			silly_audio_close();

			exit_process = 1;
		}
	} while (!exit_process);

	silly_audio_destroy();

	exit(0);
}