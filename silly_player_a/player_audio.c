#include <stdio.h>
#include "silly_player.h"

int main(int argc, char* argv[])
{
	char line[128];
	char cmd;
	int sec;
	int num;
	double ts;

	int exit_process = 0;

    if(argc != 2) {
        fprintf(stderr, "usage: $PROG_NAME $VIDEO_FILE_NAME.\n");
        exit(1);
    }

	silly_audio_startup(argv[1]);

	printf("\npress:\n");
	printf("\t 'q\\n' to quit\n");
	printf("\t 'p\\n' to pause\n");
	printf("\t 'r\\n' to resume\n");
	printf("\t 's sec\\n' to seek to position\n");
	printf("\t 't\\n' to query current time\n");
	do {
		fgets(line, sizeof(line), stdin);
		if ((num = sscanf(line, "%s %d", &cmd, &sec)) <= 0)
			continue;
		switch (cmd) {
		case 'q':
			silly_audio_shutdown();
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
		default:
			continue;
		}
	} while (!exit_process);

	exit(0);
}