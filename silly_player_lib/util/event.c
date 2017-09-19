#include "event.h"

#include <errno.h>
#include <Windows.h>

void *event_create(enum event_type type, const char *name) {
	HANDLE handle = CreateEventA(NULL, (type == EVENT_TYPE_MANUAL), FALSE, name);
	return handle;
}

void *event_open(const char *name) {
	if (name == NULL || *name == 0)
		return NULL;
	HANDLE handle = OpenEventA(EVENT_ALL_ACCESS, FALSE, name);
	return handle;
}

void event_close(void *handle) {
	if (handle)
		CloseHandle((HANDLE)handle);
}

int event_wait(void *handle) {
	DWORD code;
	if (!handle) 
		return EINVAL;

	code = WaitForSingleObject((HANDLE)handle, INFINITE);
	if (code != WAIT_OBJECT_0)
		return EINVAL;
	return 0;
}

int event_timedwait(void *handle, unsigned long milliseconds) {
	DWORD code;
	if (!handle)
		return EINVAL;

	code = WaitForSingleObject((HANDLE)handle, milliseconds);
	if (code == WAIT_TIMEOUT)
		return ETIMEDOUT;
	else if (code != WAIT_OBJECT_0)
		return EINVAL;
	return 0;
}

int event_try(void *handle) {
	DWORD code;
	if (!handle)
		return EINVAL;

	code = WaitForSingleObject((HANDLE)handle, 0);
	if (code == WAIT_TIMEOUT)
		return EAGAIN;
	else if (code != WAIT_OBJECT_0)
		return EINVAL;
	return 0;
}

int event_signal(void *handle) {
	if (!handle)
		return EINVAL;
	if (!SetEvent((HANDLE)handle))
		return EINVAL;
	return 0;
}

void event_reset(void *handle) {
	if (!handle)
		return;
	ResetEvent((HANDLE)handle);
}

int process_caller_event_wait(const char *event_name, unsigned long milliseconds)
{
	void *startup_event = event_create(EVENT_TYPE_MANUAL, event_name);
	int wait_ret;
	if (startup_event == NULL) {
		return -1;
	}

	if (milliseconds <= 0) wait_ret = event_wait(startup_event);	//blocking wait
	else wait_ret = event_timedwait(startup_event, milliseconds);	//timeout wait

	if (wait_ret == EINVAL) return -2;
	else if (wait_ret == ETIMEDOUT) return -3;
	else return 0;
}

void process_callee_event_done(const char *event_name)
{
	DWORD open_wait_timeout = 60 * 1000; //1min
	DWORD open_wait_start = GetTickCount();

	void *startup_event = event_open(event_name);
	while (!startup_event) {
		if (GetTickCount() - open_wait_start >= open_wait_timeout)
			break;
		Sleep(10);
		startup_event = event_open(event_name);
	}
	event_signal(startup_event);
}