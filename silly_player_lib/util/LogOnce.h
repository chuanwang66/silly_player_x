#pragma once

#include "c99defs.h"

#ifdef __cplusplus
extern "C" {
#endif

EXPORT int LogOnce(const char* strFile, const void* buf, int nBufSize);

#ifdef __cplusplus
}
#endif