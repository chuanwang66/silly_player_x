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

//注: 进程A signal event之后，进程B才启动，则进程B收不到event. 因此，需要保证多个进程都启动后，才使用event同步机制
EXPORT int event_signal(void *handle);

EXPORT void event_reset(void *handle);

EXPORT int process_caller_event_wait(const char *event_name, unsigned long milliseconds);
EXPORT void process_callee_event_done(const char *event_name);

#ifdef __cplusplus
}
#endif