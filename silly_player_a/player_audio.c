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

#define SAMPLERATE 48000
#define FRAMERATE 10240

int channels = SA_CH_LAYOUT_STEREO;
int samplerate = SAMPLERATE;
float sample_buffer[FRAMERATE << 1];	//这个缓冲尽量不要太大(100ms级别)，以免对is->audio_fetch_buffer太敏感
int sample_buffer_size = sizeof(sample_buffer) / sizeof(sample_buffer[0]);

HANDLE grab_thread;
volatile bool grab_stop = FALSE;
DWORD WINAPI grab_thread_proc(LPVOID lpParam)
{
	int tick = 1000 * ((float)FRAMERATE / (float)SAMPLERATE); //* 0.9;

	silly_audio_fetch_start(channels, samplerate);
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
	grab_stop = FALSE;
	grab_thread = CreateThread(NULL, 0, grab_thread_proc, NULL, 0, NULL);
	if (grab_thread == NULL) {
		fprintf(stderr, "create thread failed.\n");
	}
}

void grab_thread_stop() {
	grab_stop = TRUE;
	WaitForSingleObject(grab_thread, INFINITE);
	CloseHandle(grab_thread);
}

int main(int argc, char* argv[])
{
	char line[128];
	char cmd;
	int sec;
	int num;
	double ts;
	silly_audiospec desired_spec, spec;

	int exit_process = 0;

    if(argc != 2) {
        fprintf(stderr, "usage: $PROG_NAME $VIDEO_FILE_NAME.\n");
        exit(1);
    }

	desired_spec.channels = SA_CH_LAYOUT_STEREO;
	desired_spec.format = SA_SAMPLE_FMT_FLT;
	desired_spec.samplerate = 0;
	desired_spec.samples = 1024;

	//silly_audio_open(argv[1], &desired_spec, NULL);
	silly_audio_open(argv[1], &desired_spec, &spec);
	silly_audio_printspec(&spec);
	grab_thread_start();

	printf("\npress:\n");
	printf("\t 'q\\n' to quit\n");
	printf("\t 'p\\n' to pause\n");
	printf("\t 'r\\n' to resume\n");
	printf("\t 's sec\\n' to seek to position\n");
	printf("\t 't\\n' to query current time\n");
	printf("\t 'm\\n' to start grabbing samples\n");
	printf("\t 'n\\n' to stop grabbing samples\n");
	do {
		fgets(line, sizeof(line), stdin);
		if ((num = sscanf(line, "%s %d", &cmd, &sec)) <= 0)
			continue;
		switch (cmd) {
		case 'q':
			silly_audio_close();
			exit_process = 1;
			break;
		case 'p':
			silly_audio_pause();
			break;
		case 'r':
			silly_audio_resume();
			break;
		case 's':
			if (num == 2) {
				printf("seek to %d (sec)\n", sec);
				silly_audio_seek(sec);
			}
			break;
		case 't':
			ts = silly_audio_time();
			printf("current time: %f\n", ts);
			break;
		case 'm':
			grab_thread_start();
			break;
		case 'n':
			grab_thread_stop();
			break;
		default:
			continue;
		}
	} while (!exit_process);

	exit(0);
}