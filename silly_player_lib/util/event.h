#pragma once

#include "c99defs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum event_type {
	EVENT_TYPE_AUTO,
	EVENT_TYPE_MANUAL
};

EXPORT void *event_create(enum event_type type, const char *name);
EXPORT void *event_open(const char *name);
EXPORT void event_close(void *handle);

EXPORT int event_wait(void *handle);
EXPORT int event_timedwait(void *handle, unsigned long milliseconds);
EXPORT int event_try(void *handle);

//ע: ����A signal event֮�󣬽���B�������������B�ղ���event. ��ˣ���Ҫ��֤������̶������󣬲�ʹ��eventͬ������
EXPORT int event_signal(void *handle);

EXPORT void event_reset(void *handle);

EXPORT int process_caller_event_wait(const char *event_name, unsigned long milliseconds);
EXPORT void process_callee_event_done(const char *event_name);

#ifdef __cplusplus
}
#endif