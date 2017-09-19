#include <stdio.h>
#include "LogOnce.h"

int LogOnce(const char* strFile, const void* buf, int nBufSize)
{
#ifdef WIN32
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
#endif
	return 1;
}