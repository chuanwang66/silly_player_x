//=====================================================================
//
// Extreme Visions WaveOut/WaveIn/Mixer driver for Windows
//
// This program is distributed without warrenty of any kind
// and is not guaranteed to work on any system. You use this
// program entirely at your own risk.
//
//=====================================================================
#include "cmwave.h"

#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <mmsystem.h>


#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma warning(disable:4996)
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#endif


//=====================================================================
// GLOBAL STATE DEFINITION
//=====================================================================

//---------------------------------------------------------------------
// wave format
//---------------------------------------------------------------------
struct CWAVEFMT
{
    int srate;
    int nch;
    int bps;
    int conv;
};


//---------------------------------------------------------------------
// wave config
//---------------------------------------------------------------------
struct CWAVECFG
{
    int block_count;
    int block_size;
    int volume_enabled;
    int reverse_balance;
    int device;
    int pcmconv_enabled;
};


//---------------------------------------------------------------------
// wave stats
//---------------------------------------------------------------------
struct CWAVESTATS
{
    int active;
    int total_buffer_size;
    int total_buffer_usage;
    int block_size;
    int block_usage;
    int block_count;
    int bytes_written;
    int bytes_played;
    int start_position;
    int latency;
	struct CWAVEFMT format;
};	


//---------------------------------------------------------------------
// Block Definition
//---------------------------------------------------------------------
#define IMW_BLOCK_COUNT 20
#define IMW_BLOCK_SIZE  8192

#define IMW_BLOCK_COUNT_MAX 50
#define IMW_BLOCK_COUNT_MIN 2
#define IMW_BLOCK_SIZE_MAX 786432
#define IMW_BLOCK_SIZE_MIN 80


//---------------------------------------------------------------------
// WAVEOUT
//---------------------------------------------------------------------
struct CWAVEOUT
{
	int current_header;
	int free_headers;
	int written;
	int output;
	int pos;
	int volume;
	int pan;
	int bytes_per_sec;
	int paused;
	int latency;
	int device;
	struct CWAVEFMT fmt;
	struct CWAVECFG cfg;
	WAVEHDR *headers;
	HWAVEOUT handle;
	CRITICAL_SECTION lock;
};


//---------------------------------------------------------------------
// WAVEIN
//---------------------------------------------------------------------
struct CWAVEIN
{
	int current_header;
	int done_headers;
	int recorded;
	int input;
	int pos;
	int volume;
	int pan;
	int bytes_per_sec;
	int paused;
	int latency;
	int started;
	int device;
	struct CWAVEFMT fmt;
	struct CWAVECFG cfg;
	WAVEHDR *headers;
	HWAVEIN handle;
	CRITICAL_SECTION lock;
};


//---------------------------------------------------------------------
// GLOBAL DEFINITION
//---------------------------------------------------------------------
static void CALLBACK cwaveout_callback(HWAVEOUT, UINT, DWORD, DWORD, DWORD);
static void CALLBACK cwavein_callback(HWAVEIN, UINT, DWORD, DWORD, DWORD);

char cmw_error[4096];


//=====================================================================
//                        WAVEOUT INTERFACE 
//=====================================================================

//---------------------------------------------------------------------
// cwaveout_open - open the device
// returns CWAVEOUT* on success, NULL on failure
// device < 0 stands for default output device 
// srate: 8000, 16000, 44100, etc
// nch: num of channels
// bps: 8 or 16 
// bufferlenms and prebufferms must be in ms. 0 to use defaults. 
// prebufferms must be <= bufferlenms
// Sam:
// prebufferms: 每一块缓存可存放多少ms的音频采样
// bufferlenms: 所有  缓存可存放多少ms的音频采样
//---------------------------------------------------------------------
CWAVEOUT *cwaveout_open(int device, int srate, int nch, int bps, 
	int buflen, int prebuf)
{
    WAVEFORMATEX wfx;
    MMRESULT result;
	CWAVEOUT *waveout;
    unsigned char* buffer;
	UINT DeviceID;
	DWORD volume;
    int left, right, i;

	waveout = (CWAVEOUT*)malloc(sizeof(CWAVEOUT));
	if (waveout == NULL) return NULL;

    waveout->fmt.bps       = bps;
    waveout->fmt.nch       = nch;
    waveout->fmt.srate     = srate;
    waveout->fmt.conv      = 0;

    if (waveOutGetNumDevs() == 0) {
		free(waveout);
		sprintf(cmw_error, 
		"Playback cannot proceed because there are no valid wave devices");
		return NULL;
    }

	if (device >= (int)waveOutGetNumDevs()) {
		free(waveout);
		sprintf(cmw_error, 
		"Playback cannot proceed because there is an invalid device id %d", 
		device);
		return NULL;
	}

    if (prebuf > 0) {
		waveout->cfg.block_size = MulDiv(nch * (bps >> 3) * srate, 
			prebuf, 1000);
    }

    if (buflen > 0) {
		waveout->cfg.block_count = (buflen + prebuf - 1) / prebuf;
    }

    if (waveout->cfg.block_count < IMW_BLOCK_COUNT_MIN)
        waveout->cfg.block_count = IMW_BLOCK_COUNT_MIN;
    else if (waveout->cfg.block_count > IMW_BLOCK_COUNT_MAX)
        waveout->cfg.block_count = IMW_BLOCK_COUNT_MAX;

    if (waveout->cfg.block_size < IMW_BLOCK_SIZE_MIN)
        waveout->cfg.block_size = IMW_BLOCK_SIZE_MIN;
    else if (waveout->cfg.block_size > IMW_BLOCK_SIZE_MAX)
        waveout->cfg.block_size = IMW_BLOCK_SIZE_MAX;

    sprintf(cmw_error, "open: blocksize=%d blockcount=%d", 
		waveout->cfg.block_size, waveout->cfg.block_count);

	waveout->cfg.device = device;

    if (waveout->cfg.device > (int)waveOutGetNumDevs() - 1)
        waveout->cfg.device = -1;
    else if (waveout->cfg.device < -1)
        waveout->cfg.device = -1;

    wfx.cbSize = 0;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = nch;
    wfx.nSamplesPerSec = srate;
    wfx.wBitsPerSample = bps;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) >> 3;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    
    result = waveOutOpen(&waveout->handle, waveout->cfg.device, &wfx, 
        (DWORD)cwaveout_callback, (DWORD)waveout, CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR) {
        WAVEOUTCAPS woc;
        char buffer[512];
        
        if (waveOutGetDevCaps((UINT)waveout->cfg.device, &woc, 
            sizeof(WAVEOUTCAPS)) != MMSYSERR_NOERROR)
            lstrcpy(woc.szPname, TEXT("[Invalid Device]"));

        switch (result) 
        {
        case WAVERR_BADFORMAT:
            sprintf(buffer,
            "The \"%s\" device returned a Bad Format error (%d) during\n"
            "the waveOutOpen call. This is because the device does not\n"
            "support the following PCM format:\n\n"
            "Sample Rate:\t%ld Hz\n"
            "Sample Size:\t%d bits (%s)\n"
            "Channels:\t%d\n\n"
            "%sinput plug-in has options for changing the output format\n"
            "try adjusting these to a format supported by your sound card.\n"
            "Consult your soundcard manual for exact format specifications.",
            woc.szPname,
            result,
            wfx.nSamplesPerSec,
            wfx.wBitsPerSample,
            ((wfx.wBitsPerSample == 8) ? "unsigned" : "signed"),
            wfx.nChannels,
            (waveout->cfg.pcmconv_enabled) ? 
            "If the current " :
            "You might be able to fix this problem by enabling the "
            "\"PCM Conversion\"\noption in the configuration "
            "for this plug-in. Alternatively, if the\ncurrent "
            );
            break;
        case MMSYSERR_BADDEVICEID:
            sprintf(buffer,
            "The waveOut system returned a Bad Device ID error (%d)\n"
            "during the waveOutOpen call. The device identifier \n"
            "given was %d.Please ensure that you have a soundcard and \n"
            "that the drivers are installed correctly. If this is already\n"
            "the case, try selecting a different device from the \"Output\n"
            "Device\" section in the configuration for this plug-in\n",
            result,
            waveout->cfg.device
            );
            break;
        default:
            sprintf(buffer,
            "An MMSYSTEM error occurred while opening the wave device.\n\n"
            "Output device: %s\n"
            "Error code: %u\n\n",
            woc.szPname,
            result
            );
        }
        memcpy(cmw_error, buffer, sizeof(buffer));
		free(waveout);
        return NULL;
    }

	// get device id
	result = waveOutGetID(waveout->handle, &DeviceID);

	if (result != MMSYSERR_NOERROR) {
        waveOutClose(waveout->handle);
		waveout->handle = NULL;
		free(waveout);
        strcpy(cmw_error, "can not get device id");
        return NULL;
	}

	waveout->device = (int)DeviceID;

	result = waveOutGetVolume(waveout->handle, &volume);

	if (result != MMSYSERR_NOERROR) {
        waveOutClose(waveout->handle);
		waveout->handle = NULL;
		free(waveout);
        strcpy(cmw_error, "can not get volume");
        return NULL;
	}

    // allocate memory for buffers, headers etc.
    buffer =    (unsigned char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 
                (sizeof(WAVEHDR) + waveout->cfg.block_size) * 
                waveout->cfg.block_count);

    if (buffer == NULL) {
        waveOutClose(waveout->handle);
		waveout->handle = NULL;
		free(waveout);
        strcpy(cmw_error, "Memory allocation error allocating wave buffer");
        return NULL;
    }

    InitializeCriticalSection(&waveout->lock);

    waveout->headers          = (WAVEHDR*)buffer;
    waveout->current_header   = 0;
    waveout->free_headers     = waveout->cfg.block_count;
    waveout->written          = 0;
    waveout->output           = 0;
    waveout->pos              = 0;
    waveout->bytes_per_sec    = nch * (bps >> 3) * srate;
    waveout->paused           = 0;
    
    // set up buffer pointers
    for (i = 0; i < waveout->cfg.block_count; i++) {
        waveout->headers[i].lpData = (char*)(buffer + 
            (sizeof(WAVEHDR) * waveout->cfg.block_count) + 
            (i * waveout->cfg.block_size));
        waveout->headers[i].dwBufferLength = waveout->cfg.block_size;
        waveout->headers[i].dwUser = 0;
		waveout->headers[i].dwFlags = 0;
    }
 
    // now actually set volume and pan in case they've been set before 
    // the device was open
    cwaveout_setpan(waveout, 0);
    cwaveout_setvolume(waveout, 255);
        
    // get max amount of time it'll take to play through the buffer in ms
    waveout->latency = MulDiv((waveout->cfg.block_count + 1) * 
        waveout->cfg.block_size, 1000, waveout->bytes_per_sec);

	right = volume & 0xffff;
	left = (volume >> 16) & 0xffff;

	if (left == right) {
		waveout->volume = left;
		waveout->pan = 0;
	}	
	else if (left < right) {
		waveout->volume = right * 0xff / 0xffff;
		waveout->pan = (right - left) * 128 / right;
	}
	else {
		waveout->volume = left * 0xff / 0xffff;
		waveout->pan = (right - left) * 128 / left;
	}

    return waveout;
}


//---------------------------------------------------------------------
// cwaveout_close - close the device if it is open. calling this with
// the device closed will trigger an assert failure in debug mode.
//---------------------------------------------------------------------
int cwaveout_close(CWAVEOUT *waveout)
{
    int i;

    assert(waveout != NULL);

    // unpause if paused
    if (waveout->paused) {
        waveOutRestart(waveout->handle);
        waveout->paused = 0;
    }

    // reset the device
    waveOutReset(waveout->handle);
     
    // wait for all buffers to complete
    while (waveout->free_headers != waveout->cfg.block_count) Sleep(1);

    // unprepare any prepared headers
    for (i = 0; i < waveout->cfg.block_count; i++) {
        if (waveout->headers[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(waveout->handle, &waveout->headers[i], 
            sizeof(WAVEHDR));
    }

    DeleteCriticalSection(&waveout->lock);

    // close the device
    waveOutClose(waveout->handle);
    waveout->handle = NULL;

    // and finally free the used memory
    HeapFree(GetProcessHeap(), 0, waveout->headers);
    waveout->headers = NULL;

	free(waveout);

    return 0;
}


//---------------------------------------------------------------------
// cwaveout_write - this will actually block if you write too much
// but the input plug-in should be smart enough to realise that it
// should write 
// a) no more than 8192 bytes and
// b) no more than imw_canwrite() returns. 
// we could add another size check here but it's not really worth it.
//---------------------------------------------------------------------
int cwaveout_write(CWAVEOUT *waveout, const void *data, int size)
{
    WAVEHDR* current;
    int remain;
    char* buf = (char*)data;

    assert(waveout);
	assert(waveout->handle);
    assert(size <= 8192);

    while (size > 0) {
        while (!waveout->free_headers) Sleep(1);
        current = &waveout->headers[waveout->current_header];

        if (current->dwFlags & WHDR_PREPARED) 
            waveOutUnprepareHeader(waveout->handle, current, 
			sizeof(WAVEHDR));

        remain = (int)(waveout->cfg.block_size - current->dwUser);
        remain = size < remain ? size : remain;

        memcpy(current->lpData + current->dwUser, buf, remain);

        buf += remain;
        size -= remain;
        current->dwUser += remain;

        if ((int)current->dwUser < waveout->cfg.block_size) {
            break;
        }
        
        current->dwBufferLength = waveout->cfg.block_size;
        current->dwUser = 0;

        waveOutPrepareHeader(waveout->handle, current, sizeof(WAVEHDR));
        waveOutWrite(waveout->handle, current, sizeof(WAVEHDR));

        EnterCriticalSection(&waveout->lock);
        waveout->free_headers--;
        LeaveCriticalSection(&waveout->lock);

        waveout->current_header++;
        if (waveout->current_header >= waveout->cfg.block_count)
            waveout->current_header = 0;
    }

    return 0;
}


//---------------------------------------------------------------------
// cwaveout_canwrite - return number of bytes that can be written - 
// 0 if device is closed, note that this can be called before playback
// has been started.
//---------------------------------------------------------------------
int cwaveout_canwrite(const CWAVEOUT *waveout)
{
    WAVEHDR* current;
    int nbytes;

    if (!waveout) return 0;
	if (!waveout->handle) return 0;

	current = &waveout->headers[waveout->current_header];
    
    nbytes = waveout->free_headers * waveout->cfg.block_size - 
        (int)current->dwUser;

    return nbytes;
}

//---------------------------------------------------------------------
// waveOut callback - don't call at all, ever. this is called by 
// the windows waveOut subsystem thingy. and guess what - you really
// can't call waveOut functions from this without deadlocking the
// system (at some point). accurate MS documentation...
//
// it would be brilliant to be able to call waveOutUnprepareHeader here
// since all headers passed to this have been played and are thus prepared.
// the fact that you can't effectively renders using a callback function 
// useless (well, almost).
//---------------------------------------------------------------------
static void CALLBACK cwaveout_callback(HWAVEOUT device, UINT msg, DWORD inst,
    DWORD parm1, DWORD parm2)
{
	CWAVEOUT *waveout = (CWAVEOUT*)inst;
    WAVEHDR* current = (WAVEHDR*)parm1;
    
    if (msg == WOM_DONE) {
        assert(current);
        // we've now played current->dwBufferLength more bytes
        waveout->output += current->dwBufferLength;
        // update the number of free headers
        EnterCriticalSection(&waveout->lock);
        // increase the free headers
		waveout->free_headers++;
        LeaveCriticalSection(&waveout->lock);
    }
}


//---------------------------------------------------------------------
// cwaveout_isplaying - returns 1 if blocks are currently submitted
// to the wave functions.
//---------------------------------------------------------------------
int cwaveout_isplaying(const CWAVEOUT *waveout)
{
    if (!waveout) return 0;
	if (!waveout->handle) return 0;

    return (waveout->free_headers != waveout->cfg.block_count);
}

//---------------------------------------------------------------------
// cwaveout_pause - pause or resume playback
//---------------------------------------------------------------------
int cwaveout_pause(CWAVEOUT *waveout, int pause)
{
    assert(waveout);
	assert(waveout->handle);
    
    if (pause) {
        waveOutPause(waveout->handle);
        waveout->paused = 1;
    }
    else {
        waveOutRestart(waveout->handle);
        waveout->paused = 0;
    }

    return !pause;
}


//---------------------------------------------------------------------
// cwaveout_setvolume - set the volume (volume ranges from 0->255)
//---------------------------------------------------------------------
void cwaveout_setvolume(CWAVEOUT *waveout, int volume)
{
    unsigned short left;
    unsigned short right;

    // out of range so do the default (use what the system is set to)
    if (volume < 0 || volume > 255) {
        return;
    }

    waveout->volume = volume;

    // imw_setvolume can be called before the device
    if (!waveout->handle) return;
    
    if (waveout->cfg.volume_enabled) {
        volume = volume * 0xffff / 0xff;
        left = (unsigned short)volume;
        right = (unsigned short)volume;
    
        // compensate for the pan setting
        if (waveout->pan > 0)
            left -= waveout->pan * volume / 128;
        else if (waveout->pan < 0)
            right -= -waveout->pan * volume / 128;
                    
        // and actually set the volume
        if (waveout->cfg.reverse_balance) {
            waveOutSetVolume(waveout->handle, (right & 0xffff) | 
                ((left & 0xffff) << 16));
        }    else {
            waveOutSetVolume(waveout->handle, (left & 0xffff) | 
                ((right & 0xffff) << 16));
        }
    }
}

//---------------------------------------------------------------------
// imw_setpan - set the pan (pan ranges from -128 to +128)
//---------------------------------------------------------------------
void cwaveout_setpan(CWAVEOUT *waveout, int pan)
{
    // out of range so do the default (centre)
    if (pan < -128 || pan > 128) {
        waveout->pan = 0;
        return;
    }

    waveout->pan = pan;

    // imw_setpan can be called before the device is opened for 
    // setting the defaults
    if (!waveout->handle) return;

    // windows waveOut uses waveOutSetVolume to set l&r volumes 
    // together so leave the volume setter to deal with it
    cwaveout_setvolume(waveout, waveout->volume);
}


//---------------------------------------------------------------------
// cwaveout_flush - flush buffers and continue playback from 'time'.
//---------------------------------------------------------------------
void cwaveout_flush(CWAVEOUT *waveout, int time)
{
    WAVEHDR* current;
    assert(waveout);
	assert(waveout->handle);

    current = &waveout->headers[waveout->current_header];
    
    // if the current buffer has some data in it then write it
    if (current->dwUser) {
        current->dwBufferLength = (int)current->dwUser;
        current->dwUser = 0;
        
        waveOutPrepareHeader(waveout->handle, current, sizeof(WAVEHDR));
        waveOutWrite(waveout->handle, current, sizeof(WAVEHDR));
        
        EnterCriticalSection(&waveout->lock);
        waveout->free_headers--;
        LeaveCriticalSection(&waveout->lock);
    }

    /*
     * reset the device - take note that the microsoft documentation
     * states that calling other waveOut functions from the callback
     * will cause a deadlock - well calling waveOutUnprepare header
     * doesn't until you call waveOutReset, at which point the app
     * will freeze (hence the lame arse implementation here).
     */
    waveOutReset(waveout->handle);
    
    // wait for all buffers to be returned by the reset
    while (waveout->free_headers != waveout->cfg.block_count) Sleep(1);

    // point to the next block
    waveout->current_header++;
    waveout->current_header %= waveout->cfg.block_count;
    
    // reset positioning/statistics variables
    waveout->output = 0;
    waveout->written = 0;
    waveout->pos = time;

    // playback can now continue
}

//---------------------------------------------------------------------
// cwaveout_getoutputtime - return the number of milliseconds of
// audio that have actually been played.
//---------------------------------------------------------------------
int cwaveout_getoutputtime(const CWAVEOUT *waveout)
{
    MMTIME time;
    
    if (!waveout) return 0;
	if (!waveout->handle) return 0;
    
    // want time in bytes as this is the most accurate
    time.wType = TIME_BYTES;
    
    if (waveOutGetPosition(waveout->handle, &time, sizeof(MMTIME)) == 
        MMSYSERR_NOERROR) {
        /*
         * waveOutGetPosition apparently can return any type of time it
         * feels like if the one you specified wasn't good enough for 
         * it... sorry, "not supported".
         */
        switch (time.wType) {
        case TIME_BYTES:
            return waveout->pos + MulDiv(time.u.cb, 1000, 
				waveout->bytes_per_sec);
        case TIME_MS:
            return waveout->pos + time.u.ms;
        case TIME_SAMPLES:
            return waveout->pos + MulDiv(time.u.sample, 1000, 
				waveout->fmt.srate);
        default:
            break;
        }
    }

    /*
     * worst case - return our own stored value of the time. this value is
     * only accurate to the nearest block and is incremented in the callback
     * function. using this with particularly large blocks (ie > 8k) will 
     * cause the internal vis to become jumpy, ideally we want the above
     * to work.
     */
    return waveout->pos + MulDiv(waveout->output, 1000, 
		waveout->bytes_per_sec);
}

//---------------------------------------------------------------------
// cwaveout_getwrittentime - return the number of milliseconds of
// audio that have been written to this program but not necessarily
// played (could be still buffered).
//---------------------------------------------------------------------
int cwaveout_getwrittentime(CWAVEOUT *waveout)
{
    if (!waveout) return 0;
	if (!waveout->handle) return 0;
    return waveout->pos + MulDiv(waveout->written, 1000, 
		waveout->bytes_per_sec);
}


//---------------------------------------------------------------------
// get HWAVEOUT & DeviceID
//---------------------------------------------------------------------
int cwaveout_getid(const CWAVEOUT *waveout, LPHWAVEOUT phwo, int *device)
{
	assert(waveout);
	if (phwo) *phwo = waveout->handle;
	if (device) *device = waveout->device;
	return waveout->latency;
}

//---------------------------------------------------------------------
// cwaveout_getdevicenum - return the audio output device number
//---------------------------------------------------------------------
int cwaveout_getdevicenum()
{
	return (int)waveOutGetNumDevs();
}

//=====================================================================
//                        WAVEIN INTERFACE 
//=====================================================================


//---------------------------------------------------------------------
// cwavein_open - open wavein device. 
// device < 0 stands for default output device 
// srate: 8000, 16000, 44100, etc
// nch: num of channels
// bps: 8 or 16 
// bufferlenms and prebufferms must be in ms. 0 to use defaults. 
// prebufferms must be <= bufferlenms
//---------------------------------------------------------------------
CWAVEIN *cwavein_open(int device, int srate, int nch, int bps, 
	int buflen, int prebuf)
{
    WAVEFORMATEX wfx;
    MMRESULT result;
	CWAVEIN *wavein;
    unsigned char* buffer;
	UINT DeviceID;
    int i;

	wavein = (CWAVEIN*)malloc(sizeof(CWAVEIN));
	if (wavein == NULL) {
		return NULL;
	}

    wavein->fmt.bps       = bps;
    wavein->fmt.nch       = nch;
    wavein->fmt.srate     = srate;
    wavein->fmt.conv      = 0;

    if (waveInGetNumDevs() == 0) {
		free(wavein);
        sprintf(cmw_error, 
        "Playback cannot proceed because there are no valid wave devices");
        return NULL;
    }

	if (prebuf > 0) {
		wavein->cfg.block_size = MulDiv(nch * (bps >> 3) * srate, 
			prebuf, 1000);
	}

	if (buflen > 0) {
		wavein->cfg.block_count = (buflen + prebuf - 1) / prebuf;
	}

    if (wavein->cfg.block_count < IMW_BLOCK_COUNT_MIN)
        wavein->cfg.block_count = IMW_BLOCK_COUNT_MIN;
    else if (wavein->cfg.block_count > IMW_BLOCK_COUNT_MAX)
        wavein->cfg.block_count = IMW_BLOCK_COUNT_MAX;

    if (wavein->cfg.block_size < IMW_BLOCK_SIZE_MIN)
        wavein->cfg.block_size = IMW_BLOCK_SIZE_MIN;
    else if (wavein->cfg.block_size > IMW_BLOCK_SIZE_MAX)
        wavein->cfg.block_size = IMW_BLOCK_SIZE_MAX;

	sprintf(cmw_error, "open: blocksize=%d blockcount=%d", \
		wavein->cfg.block_size, wavein->cfg.block_count);

	wavein->cfg.device = device;

    if (wavein->cfg.device > (int)waveInGetNumDevs() - 1)
        wavein->cfg.device = -1;
    else if (wavein->cfg.device < -1)
        wavein->cfg.device = -1;

    wfx.cbSize = 0;
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = nch;
    wfx.nSamplesPerSec = srate;
    wfx.wBitsPerSample = bps;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) >> 3;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    result = waveInOpen(&wavein->handle, wavein->cfg.device, &wfx, 
        (DWORD)cwavein_callback, (DWORD)wavein, CALLBACK_FUNCTION);

    if (result != MMSYSERR_NOERROR) {
        WAVEOUTCAPS woc;
        char buffer[512];
        
        if (waveOutGetDevCaps((UINT)wavein->cfg.device, &woc, 
            sizeof(WAVEOUTCAPS)) != MMSYSERR_NOERROR)
            lstrcpy(woc.szPname, TEXT("[Invalid Device]"));

        switch (result) 
        {
        case WAVERR_BADFORMAT:
            sprintf(buffer,
            "The \"%s\" device returned a Bad Format error (%d) during\n"
            "the waveOutOpen call. This is because the device does not\n"
            "support the following PCM format:\n\n"
            "Sample Rate:\t%ld Hz\n"
            "Sample Size:\t%d bits (%s)\n"
            "Channels:\t%d\n\n"
            "%sinput plug-in has options for changing the output format\n"
            "try adjusting these to a format supported by your sound card.\n"
            "Consult your soundcard manual for exact format specifications.",
            woc.szPname,
            result,
            wfx.nSamplesPerSec,
            wfx.wBitsPerSample,
            ((wfx.wBitsPerSample == 8) ? "unsigned" : "signed"),
            wfx.nChannels,
            (wavein->cfg.pcmconv_enabled) ? 
            "If the current " :
            "You might be able to fix this problem by enabling the "
            "\"PCM Conversion\"\noption in the configuration "
            "for this plug-in. Alternatively, if the\ncurrent "
            );
            break;
        case MMSYSERR_BADDEVICEID:
            sprintf(buffer,
            "The waveIn system returned a Bad Device ID error (%d)\n"
            "during the waveOutOpen call. The device identifier \n"
            "given was %d.Please ensure that you have a soundcard and \n"
            "that the drivers are installed correctly. If this is already\n"
            "the case, try selecting a different device from the \"Output\n"
            "Device\" section in the configuration for this plug-in\n",
            result,
            wavein->cfg.device
            );
            break;
        default:
            sprintf(buffer,
            "An MMSYSTEM error occurred while opening the wave device.\n\n"
            "Output device: %s\n"
            "Error code: %u\n\n",
            woc.szPname,
            result
            );
        }
        memcpy(cmw_error, buffer, sizeof(buffer));
		free(wavein);
        return NULL;
    }

	result = waveInGetID(wavein->handle, &DeviceID);

	if (result != MMSYSERR_NOERROR) {
        waveInClose(wavein->handle);
        wavein->handle = NULL;
		free(wavein);
        strcpy(cmw_error, "can not get device id");
        return NULL;
	}

	wavein->device = (int)DeviceID;

    // allocate memory for buffers, headers etc.
    buffer =    (unsigned char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 
                (sizeof(WAVEHDR) + wavein->cfg.block_size) * 
                wavein->cfg.block_count);

    if (buffer == NULL) {
        waveInClose(wavein->handle);
        wavein->handle = NULL;
		free(wavein);
        strcpy(cmw_error, "Memory allocation error allocating wave buffer");
        return NULL;
    }

    InitializeCriticalSection(&wavein->lock);

    wavein->headers          = (WAVEHDR*)buffer;
    wavein->current_header   = 0;
    wavein->done_headers     = 0;
    wavein->recorded         = 0;
    wavein->input            = 0;
    wavein->pos              = 0;
    wavein->bytes_per_sec    = nch * (bps >> 3) * srate;
    wavein->started          = 0;
    
    waveInStop(wavein->handle);

    // set up buffer pointers
    for (i = 0; i < wavein->cfg.block_count; i++) {
        wavein->headers[i].lpData = (char*)(buffer + 
            (sizeof(WAVEHDR) * wavein->cfg.block_count) + 
            (i * wavein->cfg.block_size));
        wavein->headers[i].dwBufferLength = wavein->cfg.block_size;
		wavein->headers[i].dwUser = 0;
		wavein->headers[i].dwFlags = 0;
		waveInPrepareHeader(wavein->handle, &wavein->headers[i], 
			sizeof(WAVEHDR));
		waveInAddBuffer(wavein->handle, &wavein->headers[i], 
			sizeof(WAVEHDR));
    }

    // get max amount of time it'll take to play through the buffer in ms
    wavein->latency = MulDiv((wavein->cfg.block_count + 1) * 
        wavein->cfg.block_size, 1000, wavein->bytes_per_sec);

	return wavein;
}


//---------------------------------------------------------------------
// cwavein_close - close the device if it is open. calling this with
// the device closed will trigger an assert failure in debug mode.
//---------------------------------------------------------------------
int cwavein_close(CWAVEIN *wavein)
{
    int i;

    assert(wavein != NULL);

    // unpause if paused
    if (wavein->started == 0) {
        waveInStart(wavein->handle);
    }

    // reset the device
    waveInReset(wavein->handle);

    // wait for all buffers to complete
    while (wavein->done_headers != wavein->cfg.block_count) Sleep(1);

    // unprepare any prepared headers
    for (i = 0; i < wavein->cfg.block_count; i++) {
        if (wavein->headers[i].dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(wavein->handle, &wavein->headers[i], 
            sizeof(WAVEHDR));
    }

    DeleteCriticalSection(&wavein->lock);

    // close the device
    waveInClose(wavein->handle);
    wavein->handle = NULL;

    // and finally free the used memory
    HeapFree(GetProcessHeap(), 0, wavein->headers);
    wavein->headers = NULL;

	free(wavein);

    return 0;
}


//---------------------------------------------------------------------
// WaveIut callback - don't call at all, ever. this is called by 
// the windows waveIn subsystem thingy. and guess what - you really
// can't call waveOut functions from this without deadlocking the
// system (at some point). accurate MS documentation...
//
// it would be brilliant to be able to call waveInUnprepareHeader here
// since all headers passed to this have been played and are thus prepared.
// the fact that you can't effectively renders using a callback function 
// useless (well, almost).
//---------------------------------------------------------------------
static void CALLBACK cwavein_callback(HWAVEIN device, UINT msg, DWORD inst,
    DWORD parm1, DWORD parm2)
{
	CWAVEIN* wavein = (CWAVEIN*)inst;
    WAVEHDR* current = (WAVEHDR*)parm1;
    
    if (msg == WIM_DATA) {
        assert(current);
        // we've now played current->dwBufferLength more bytes
        wavein->input += current->dwBufferLength;
        // update the number of free headers
        EnterCriticalSection(&wavein->lock);
        wavein->done_headers++;
        LeaveCriticalSection(&wavein->lock);
    }
}


//---------------------------------------------------------------------
// imc_read - this will actually block if you write too much
// but the input plug-in should be smart enough to realise that it
// should read 
// a) no more than 8192 bytes and
// b) no more than imc_canread() returns. 
// we could add another size check here but it's not really worth it.
//---------------------------------------------------------------------
int cwavein_read(CWAVEIN *wavein, void *data, int size)
{
    WAVEHDR* current;
    int remain;
    char* buf = (char*)data;

    assert(wavein);
    assert(size <= 8192);
    
    wavein->recorded += size;

    while (size > 0) {
		while (!wavein->done_headers) Sleep(1);

		current = &wavein->headers[wavein->current_header];

		// a block is first time to unprepare
		if (current->dwFlags & WHDR_PREPARED) 
			waveInUnprepareHeader(wavein->handle, current, sizeof(WAVEHDR));

		remain = wavein->cfg.block_size - (int)current->dwUser;
		remain = size < remain ? size : remain;

		if (buf != NULL) {
			memcpy(buf, current->lpData + current->dwUser, remain);
			buf += remain;
		}

		size -= remain;
		current->dwUser += remain;

		// there is more data on a block, so break
		if ((int)current->dwUser < wavein->cfg.block_size) 
			break;

		// there is no more data on a block, so add buffer
		data = current->lpData;
		memset(current, 0, sizeof(WAVEHDR));
		current->dwBufferLength = wavein->cfg.block_size;
		current->lpData = (char*)data;
		current->dwUser = 0;

		waveInPrepareHeader(wavein->handle, current, sizeof(WAVEHDR));
		waveInAddBuffer(wavein->handle, current, sizeof(WAVEHDR));

        /*
         * writing to imc_done_headers must be encased in 
         * Enter/LeaveCriticalSection calls since two different
         * threads write to it (reading should be ok though).
         */
        EnterCriticalSection(&wavein->lock);
        wavein->done_headers--;
        LeaveCriticalSection(&wavein->lock);

		wavein->current_header++;
		if (wavein->current_header >= wavein->cfg.block_count)
			wavein->current_header = 0;
	}

	return 0;
}


//---------------------------------------------------------------------
// imc_canread - return number of bytes that can be read - 
// 0 if device is closed, note that this can be called before playback
// has been started.
//---------------------------------------------------------------------
int cwavein_canread(const CWAVEIN *wavein)
{
    WAVEHDR* current;
    int nbytes;

	assert(wavein);

	current = &wavein->headers[wavein->current_header];
    
    nbytes = wavein->done_headers * wavein->cfg.block_size - 
        (int)current->dwUser;

    return nbytes;
}


//---------------------------------------------------------------------
// cwavein_start - start recording
//---------------------------------------------------------------------
int cwavein_start(CWAVEIN *wavein)
{
	assert(wavein);

	if (wavein->started == 0) {
		waveInStart(wavein->handle);
		wavein->started = 1;
	}

	return 0;
}


//---------------------------------------------------------------------
// cwavein_stop - stop recording
//---------------------------------------------------------------------
int cwavein_stop(CWAVEIN *wavein)
{
	assert(wavein);

	if (wavein->started != 0) {
		waveInStop(wavein->handle);
		wavein->started = 0;
	}

	return 0;
}


//---------------------------------------------------------------------
// imc_reset - reset recording (clear buffer and reset to stop status)
//---------------------------------------------------------------------
int cwavein_reset(CWAVEIN *wavein)
{
	int i;

	assert(wavein);

	// start device
	cwavein_start(wavein);

    // reset the device
    waveInReset(wavein->handle);

    // wait for all buffers to complete
    while (wavein->done_headers != wavein->cfg.block_count) Sleep(1);

	// stop device
	cwavein_stop(wavein);

    /*
     * writing to imc_done_headers must be encased in 
     * Enter/LeaveCriticalSection calls since two different
     * threads write to it (reading should be ok though).
     */
    EnterCriticalSection(&wavein->lock);
    wavein->done_headers = 0;
    LeaveCriticalSection(&wavein->lock);

    // reset all prepared headers
    for (i = 0; i < wavein->cfg.block_count; i++) {
		void *lpData = wavein->headers[i].lpData;

        if (wavein->headers[i].dwFlags & WHDR_PREPARED)
            waveInUnprepareHeader(wavein->handle, &wavein->headers[i], 
            sizeof(WAVEHDR));

		memset(&wavein->headers[i], 0, sizeof(WAVEHDR));

		wavein->headers[i].dwBufferLength = wavein->cfg.block_size;
		wavein->headers[i].lpData = (char*)lpData;
		wavein->headers[i].dwUser = 0;

		waveInPrepareHeader(wavein->handle, &wavein->headers[i],
			sizeof(WAVEHDR));
		waveInAddBuffer(wavein->handle, &wavein->headers[i], 
			sizeof(WAVEHDR));
    }

	wavein->recorded = 0;
	wavein->input = 0;

	return 0;
}


//---------------------------------------------------------------------
// imc_getstatus - get status returns the size of a single block
//---------------------------------------------------------------------
int cwavein_getstatus(CWAVEIN *wavein, int *samplerate, int *nchannels, 
	int *bitspersamp)
{
    if (wavein == NULL) return 0;

    if (samplerate) *samplerate = wavein->fmt.srate;
    if (nchannels) *nchannels = wavein->fmt.nch;
    if (bitspersamp) *bitspersamp = wavein->fmt.bps;

	return wavein->cfg.block_size;
}


//---------------------------------------------------------------------
// get HWAVEOUT & DeviceID
//---------------------------------------------------------------------
int cwavein_getid(const CWAVEIN *wavein, LPHWAVEIN phwi, int *device)
{
	assert(wavein);
	if (phwi) *phwi = wavein->handle;
	if (device) *device = wavein->device;
	return wavein->latency;
}




//=====================================================================
//                          MIXER INTERFACE 
//=====================================================================
struct CMIXER
{
	HMIXER handle;
	HWAVEIN wavein;
	int count;
	MIXERLINE *lines;
};


CMIXER *cmixer_open(const CWAVEIN *wavein, int WaveInDeviceID)
{
	MIXERLINE mxline;
	HMIXER handle;
	CMIXER *mixer;
	MMRESULT mr;
	int count, i;
	int dest;

	if (wavein != NULL) {
		mr = mixerOpen(&handle, (UINT)wavein->handle, 0, 0, 
			MIXER_OBJECTF_HWAVEIN);
	}	else {
		mr = mixerOpen(&handle, (UINT)WaveInDeviceID, 0, 0,
			MIXER_OBJECTF_WAVEIN);
	}

	if (mr != MMSYSERR_NOERROR) {
		return NULL;
	}

	mxline.cbStruct = sizeof(mxline);
	mxline.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_WAVEIN;

	mr = mixerGetLineInfo((HMIXEROBJ)handle, &mxline, 
		MIXER_GETLINEINFOF_COMPONENTTYPE);

	if (mr != MMSYSERR_NOERROR) {
		mixerClose(handle);
		return NULL;
	}

	count = mxline.cConnections;
	dest = (int)mxline.dwDestination;

	mixer = (CMIXER*)malloc(sizeof(CMIXER));

	if (mixer == NULL) {
		mixerClose(handle);
		return NULL;
	}

	mixer->handle = handle;
	mixer->wavein = (wavein)? wavein->handle : NULL;
	mixer->count = count;
	mixer->lines = (MIXERLINE*)malloc(sizeof(MIXERLINE) * count);

	if (mixer->lines == NULL) {
		free(mixer);
		return NULL;
	}

	for (i = 0; i < count; i++) {
		mxline.cbStruct = sizeof(mxline);
		mxline.dwDestination = (DWORD)dest;
		mxline.dwSource = i;

		mr = mixerGetLineInfo((HMIXEROBJ)handle, &mxline, 
			MIXER_GETLINEINFOF_SOURCE);

		if (mr != MMSYSERR_NOERROR) {
			mixerClose(handle);
			free(mixer->lines);
			free(mixer);
			return NULL;
		}

		mixer->lines[i] = mxline;
	}

	return mixer;
}


void cmixer_close(CMIXER *mixer)
{
	assert(mixer);
	assert(mixer->handle);

	mixerClose(mixer->handle);
	mixer->handle = NULL;

	if (mixer->lines) free(mixer->lines);
	mixer->lines = NULL;

	free(mixer);
}

int cmixer_getlinecount(const CMIXER *mixer)
{
	assert(mixer);
	assert(mixer->handle);
	return mixer->count;
}

const MIXERLINE *cmixer_getline(const CMIXER *mixer, int n, MIXERLINE *line)
{
	assert(mixer);
	assert(mixer->handle);
	if (n < 0 || n >= mixer->count) {
		if (line) memset(line, 0, sizeof(MIXERLINE));
		return NULL;
	}
	if (line) *line = mixer->lines[n];
	return &(mixer->lines[n]);
}

int cmixer_findtype(const CMIXER *mixer, int ComponentType)
{
	int i;

	assert(mixer);
	assert(mixer->handle);

	for (i = 0; i < mixer->count; i++) {
		if ((int)mixer->lines[i].dwComponentType == ComponentType) 
			return i;
	}

	return -1;
}

int cmixer_ctrl_get(CMIXER *mixer, const MIXERLINE *line, LPMIXERCONTROL *c)
{

	return 0;
}

int cmixer_select(CMIXER *mixer, int n)
{
	LPMIXERCONTROLDETAILS_LISTTEXT plisttext;
	LPMIXERCONTROLDETAILS_BOOLEAN plistbool;
	MIXERCONTROLDETAILS mxcd;
	MIXERLINE mxline, mxl;
	LPMIXERCONTROL pmxctrl;
	MIXERLINECONTROLS mxlctrl;
	DWORD cMultipleItems;
	DWORD cChannels;
	int bOneItemOnly;
	MMRESULT mr;
	int i, j, k;

	assert(mixer);
	assert(mixer->handle);
	
	if (n < 0 || n >= mixer->count) {
		return -1;
	}

	mxline = mixer->lines[n];

	mxl.cbStruct = sizeof(mxl);
	mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_WAVEIN;

	mr = mixerGetLineInfo((HMIXEROBJ)mixer->handle, &mxl, 
		MIXER_GETLINEINFOF_COMPONENTTYPE);

	if (mr != MMSYSERR_NOERROR) {
		return -2;
	}

	pmxctrl = (LPMIXERCONTROL)malloc(sizeof(MIXERCONTROL) * mxl.cControls);

	if (pmxctrl == NULL) {
		return -3;
	}

	mxlctrl.cbStruct = sizeof(mxlctrl);
	mxlctrl.dwLineID = mxl.dwLineID;
	mxlctrl.dwControlID = 0;
	mxlctrl.cControls = mxl.cControls;
	mxlctrl.cbmxctrl = sizeof(MIXERCONTROL);
	mxlctrl.pamxctrl = pmxctrl;

	mr = mixerGetLineControls((HMIXEROBJ)mixer->handle, &mxlctrl, 
		MIXER_GETLINECONTROLSF_ALL);

	if (mr != MMSYSERR_NOERROR) {
		free(pmxctrl);
		return -4;
	}

	for (i = 0; i < (int)mxl.cControls; i++) {
		if (MIXERCONTROL_CT_CLASS_LIST == (pmxctrl[i].dwControlType & 
			MIXERCONTROL_CT_CLASS_MASK)) break;
	}

	if (i >= (int)mxl.cControls) {
		free(pmxctrl);
		return -5;
	}

	bOneItemOnly = 0;
	cChannels = mxl.cChannels;
	cMultipleItems = 0;

	if (pmxctrl[i].dwControlType == MIXERCONTROL_CONTROLTYPE_MUX ||
		pmxctrl[i].dwControlType == MIXERCONTROL_CONTROLTYPE_SINGLESELECT)
		bOneItemOnly = 1;

	if (MIXERCONTROL_CONTROLF_UNIFORM & pmxctrl[i].fdwControl) cChannels = 1;
	if (MIXERCONTROL_CONTROLF_MULTIPLE & pmxctrl[i].fdwControl) {
		cMultipleItems = pmxctrl[i].cMultipleItems;
	}

	if (cMultipleItems == 0) {
		free(pmxctrl);
		return -6;
	}

	plisttext = (LPMIXERCONTROLDETAILS_LISTTEXT)
		malloc(cChannels * cMultipleItems * 
		sizeof(MIXERCONTROLDETAILS_LISTTEXT));

	if (plisttext == NULL) {
		free(pmxctrl);
		return -7;
	}

	mxcd.cbStruct = sizeof(mxcd);
	mxcd.dwControlID = pmxctrl[i].dwControlID;
	mxcd.cChannels = cChannels;
	mxcd.cMultipleItems = cMultipleItems;
	mxcd.cbDetails = sizeof(MIXERCONTROLDETAILS_LISTTEXT);
	mxcd.paDetails = (LPVOID)plisttext;

	mr = mixerGetControlDetails((HMIXEROBJ)mixer->handle, &mxcd, 
		MIXER_GETCONTROLDETAILSF_LISTTEXT);

	if (mr != MMSYSERR_NOERROR) {
		free(pmxctrl);
		free(plisttext);
		return -8;
	}

	plistbool = (LPMIXERCONTROLDETAILS_BOOLEAN)malloc(cChannels * 
		cMultipleItems * sizeof(MIXERCONTROLDETAILS_BOOLEAN));

	if (plistbool == NULL) {
		free(pmxctrl);
		free(plisttext);
		return -9;
	}

	mxcd.cbDetails = sizeof(MIXERCONTROLDETAILS_BOOLEAN);
	mxcd.paDetails = plistbool;

	mr = mixerGetControlDetails((HMIXEROBJ)mixer->handle, &mxcd, 
		MIXER_GETCONTROLDETAILSF_VALUE);

	if (mr != MMSYSERR_NOERROR) {
		free(pmxctrl);
		free(plisttext);
		free(plistbool);
		return -10;
	}

	for (j = 0; j < (int)cMultipleItems; j = j + cChannels) {
		if (lstrcmp(plisttext[j].szName, mixer->lines[n].szName) == 0) {
			for (k = 0; k < (int)cChannels; k++) 
				plistbool[j + k].fValue = 1;
			//printf("%s: on\n", plisttext[j].szName);
		}
		else if (bOneItemOnly) {
			for (k = 0; k < (int)cChannels; k++)
				plistbool[j + k].fValue = 0;
			//printf("%s: off\n", plisttext[j].szName);
		}
	}

	mr = mixerSetControlDetails((HMIXEROBJ)mixer->handle, &mxcd, 
		MIXER_SETCONTROLDETAILSF_VALUE);

	free(plistbool);
	free(plisttext);
	free(pmxctrl);

	if (mr != MMSYSERR_NOERROR) {
		return -11;
	}

	return 0;
}

int cmixer_mute(CMIXER *mixer, int n, int mute)
{
	MIXERCONTROL MxCtrl;
	MIXERLINECONTROLS MxLCtrl;
	MIXERCONTROLDETAILS_BOOLEAN bValue[2];
	MIXERCONTROLDETAILS MxControlDetails;
	MIXERLINE mxline;
	MMRESULT mr;
	int i;

	mxline = mixer->lines[n];

	MxLCtrl.cbStruct = sizeof(MIXERLINECONTROLS);
	MxLCtrl.dwLineID = mxline.dwLineID;
	MxLCtrl.dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
	MxLCtrl.cControls = 1;
	MxLCtrl.cbmxctrl = sizeof(MIXERCONTROL);
	MxLCtrl.pamxctrl = &MxCtrl;

	mr = mixerGetLineControls((HMIXEROBJ)mixer->handle, 
		&MxLCtrl, MIXER_GETLINECONTROLSF_ONEBYTYPE);

	if (mr != MMSYSERR_NOERROR) {
		return -1;
	}

	MxControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
	MxControlDetails.dwControlID = MxCtrl.dwControlID;
	MxControlDetails.cChannels = mxline.cChannels;

	if (MIXERCONTROL_CONTROLF_UNIFORM &  MxCtrl.fdwControl) 
		MxControlDetails.cChannels = 1;

	MxControlDetails.cMultipleItems = 0;
	MxControlDetails.hwndOwner = (HWND) 0;
	MxControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_BOOLEAN);
	MxControlDetails.paDetails = bValue;

	for (i = 0; i < (int)MxControlDetails.cChannels; i++) {
		bValue[i].fValue = mute;
	}

	mr = mixerSetControlDetails((HMIXEROBJ)mixer->handle, &MxControlDetails, 
		MIXER_SETCONTROLDETAILSF_VALUE);

	if (mr != MMSYSERR_NOERROR) {
		return -2;
	}

	return 0;
}

int cmixer_volume(CMIXER *mixer, int n, int volume)
{
	MIXERCONTROL MxCtrl;
	MIXERLINECONTROLS MxLCtrl;
	MIXERCONTROLDETAILS_UNSIGNED uValue[8];
	MIXERCONTROLDETAILS MxControlDetails;
	MIXERLINE mxline;
	MMRESULT mr;
	UINT vol;
	int i;

	if (volume > 100) vol = 100;
	else if (volume < 0) vol = 0;
	else vol = (UINT)volume;

	mxline = mixer->lines[n];

	MxLCtrl.cbStruct = sizeof(MIXERLINECONTROLS);
	MxLCtrl.dwLineID = mxline.dwLineID;
	MxLCtrl.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
	MxLCtrl.cControls = 1;
	MxLCtrl.cbmxctrl = sizeof(MIXERCONTROL);
	MxLCtrl.pamxctrl = &MxCtrl;

	mr = mixerGetLineControls((HMIXEROBJ)mixer->handle, 
		&MxLCtrl, MIXER_GETLINECONTROLSF_ONEBYTYPE);

	if (mr != MMSYSERR_NOERROR) {
		return -1;
	}

	MxControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
	MxControlDetails.dwControlID = MxCtrl.dwControlID;
	MxControlDetails.cChannels = mxline.cChannels;

	if (MIXERCONTROL_CONTROLF_UNIFORM &  MxCtrl.fdwControl) 
		MxControlDetails.cChannels = 1;

	MxControlDetails.cMultipleItems = 0;
	MxControlDetails.hwndOwner = (HWND) 0;
	MxControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
	MxControlDetails.paDetails = uValue;

	vol = MxCtrl.Bounds.dwMinimum + (UINT)(((MxCtrl.Bounds.dwMaximum - 
		MxCtrl.Bounds.dwMinimum) * vol)/100);

	for (i = 0; i < (int)MxControlDetails.cChannels; i++) {
		uValue[i].dwValue = vol;
	}

	mr = mixerSetControlDetails((HMIXEROBJ)mixer->handle, &MxControlDetails, 
		MIXER_SETCONTROLDETAILSF_VALUE);

	if (mr != MMSYSERR_NOERROR) {
		return -2;
	}

	return 0;
}

int cmixer_loudness(CMIXER *mixer, int n, int loudness)
{
	MIXERCONTROL MxCtrl;
	MIXERLINECONTROLS MxLCtrl;
	MIXERCONTROLDETAILS_BOOLEAN bValue[2];
	MIXERCONTROLDETAILS MxControlDetails;
	MIXERLINE mxline;
	MMRESULT mr;
	int i;

	mxline = mixer->lines[n];

	MxLCtrl.cbStruct = sizeof(MIXERLINECONTROLS);
	MxLCtrl.dwLineID = mxline.dwLineID;
	MxLCtrl.dwControlType = MIXERCONTROL_CONTROLTYPE_LOUDNESS;
	MxLCtrl.cControls = 1;
	MxLCtrl.cbmxctrl = sizeof(MIXERCONTROL);
	MxLCtrl.pamxctrl = &MxCtrl;

	mr = mixerGetLineControls((HMIXEROBJ)mixer->handle, 
		&MxLCtrl, MIXER_GETLINECONTROLSF_ONEBYTYPE);

	if (mr != MMSYSERR_NOERROR) {
		return -1;
	}

	MxControlDetails.cbStruct = sizeof(MIXERCONTROLDETAILS);
	MxControlDetails.dwControlID = MxCtrl.dwControlID;
	MxControlDetails.cChannels = mxline.cChannels;

	if (MIXERCONTROL_CONTROLF_UNIFORM &  MxCtrl.fdwControl) 
		MxControlDetails.cChannels = 1;

	MxControlDetails.cMultipleItems = 0;
	MxControlDetails.hwndOwner = (HWND) 0;
	MxControlDetails.cbDetails = sizeof(MIXERCONTROLDETAILS_BOOLEAN);
	MxControlDetails.paDetails = bValue;

	for (i = 0; i < (int)MxControlDetails.cChannels; i++) {
		bValue[i].fValue = loudness;
	}

	mr = mixerSetControlDetails((HMIXEROBJ)mixer->handle, &MxControlDetails, 
		MIXER_SETCONTROLDETAILSF_VALUE);

	if (mr != MMSYSERR_NOERROR) {
		return -2;
	}

	return 0;
}


int cmixer_component_select(CMIXER *mixer, int ComponentType)
{
	int i;
	for (i = 0; i < mixer->count; i++) {
		if ((int)mixer->lines[i].dwComponentType == ComponentType) {
			return cmixer_select(mixer, i);
		}
	}
	return -100;
}

int cmixer_component_volume(CMIXER *mixer, int ComponentType, int vol)
{
	int i;
	for (i = 0; i < mixer->count; i++) {
		if ((int)mixer->lines[i].dwComponentType == ComponentType) {
			return cmixer_volume(mixer, i, vol);
		}
	}
	return -100;
}

int cmixer_component_mute(CMIXER *mixer, int ComponentType, int mute)
{
	int i;
	for (i = 0; i < mixer->count; i++) {
		if ((int)mixer->lines[i].dwComponentType == ComponentType) {
			return cmixer_mute(mixer, i, mute);
		}
	}
	return -100;
}

int cmixer_component_loudness(CMIXER *mixer, int ComponentType, int value)
{
	int i;
	for (i = 0; i < mixer->count; i++) {
		if ((int)mixer->lines[i].dwComponentType == ComponentType) {
			return cmixer_loudness(mixer, i, value);
		}
	}
	return -100;
}


const char *cmixer_get_component_type(DWORD dwComponentType)
{
#define TYPE_TO_STR(x) case x: return #x;
    switch (dwComponentType) {
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_UNDEFINED);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_DIGITAL);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_LINE);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_MONITOR);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_SPEAKERS);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_HEADPHONES);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_TELEPHONE);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_WAVEIN);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_DST_VOICEIN);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_UNDEFINED);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_DIGITAL);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_LINE);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_MICROPHONE);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_SYNTHESIZER);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_TELEPHONE);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_PCSPEAKER);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_AUXILIARY);
    TYPE_TO_STR(MIXERLINE_COMPONENTTYPE_SRC_ANALOG);
    }
#undef TYPE_TO_STR
    return "MIXERLINE_COMPONENTTYPE_UNKNOW";
}


const char *cmixer_get_target_type(DWORD dwType)
{
#define TYPE_TO_STR(x) case x: return #x;
    switch (dwType) {
    TYPE_TO_STR(MIXERLINE_TARGETTYPE_UNDEFINED);
    TYPE_TO_STR(MIXERLINE_TARGETTYPE_WAVEOUT);
    TYPE_TO_STR(MIXERLINE_TARGETTYPE_WAVEIN);
    TYPE_TO_STR(MIXERLINE_TARGETTYPE_MIDIOUT);
    TYPE_TO_STR(MIXERLINE_TARGETTYPE_MIDIIN);
    TYPE_TO_STR(MIXERLINE_TARGETTYPE_AUX);
    }
#undef TYPE_TO_STR
    return "MIXERLINE_TARGETTYPE_UNKNOW";
}


const char *cmixer_get_control_type(DWORD dwControlType)
{
#define TYPE_TO_STR(x) case x: return #x
    switch (dwControlType) {
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_CUSTOM);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_BOOLEANMETER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_SIGNEDMETER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_PEAKMETER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_UNSIGNEDMETER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_BOOLEAN);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_ONOFF);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MUTE);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MONO);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_LOUDNESS);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_STEREOENH);
#ifdef MIXERCONTROL_CONTROLTYPE_BASS_BOOST
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_BASS_BOOST);
#endif
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_BUTTON);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_DECIBELS);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_SIGNED);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_UNSIGNED);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_PERCENT);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_SLIDER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_PAN);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_QSOUNDPAN);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_FADER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_VOLUME);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_BASS);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_TREBLE);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_EQUALIZER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_SINGLESELECT);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MUX);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MULTIPLESELECT);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MIXER);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MICROTIME);
    TYPE_TO_STR(MIXERCONTROL_CONTROLTYPE_MILLITIME);
    }
#undef TYPE_TO_STR
    return "MIXERCONTROL_CONTROLTYPE_UNKNOW";
}


int cmixer_easy_select(int WaveInDeviceID, int ComponentType)
{
	CMIXER *mixer;
	int retval;
	mixer = cmixer_open(NULL, WaveInDeviceID);
	if (mixer == NULL) return -1000;
	retval = cmixer_component_select(mixer, ComponentType);
	cmixer_close(mixer);
	return retval;
}

int cmixer_easy_volume(int WaveInDeviceID, int ComponentType, int volume)
{
	CMIXER *mixer;
	int retval;
	mixer = cmixer_open(NULL, WaveInDeviceID);
	if (mixer == NULL) return -1000;
	retval = cmixer_component_volume(mixer, ComponentType, volume);
	cmixer_close(mixer);
	return retval;
}

int cmixer_easy_mute(int WaveInDeviceID, int ComponentType, int mute)
{
	CMIXER *mixer;
	int retval;
	mixer = cmixer_open(NULL, WaveInDeviceID);
	if (mixer == NULL) return -1000;
	retval = cmixer_component_mute(mixer, ComponentType, mute);
	cmixer_close(mixer);
	return retval;
}

int cmixer_easy_loudness(int WaveInDeviceID, int ComponentType, int value)
{
	CMIXER *mixer;
	int retval;
	mixer = cmixer_open(NULL, WaveInDeviceID);
	if (mixer == NULL) return -1000;
	retval = cmixer_component_loudness(mixer, ComponentType, value);
	cmixer_close(mixer);
	return retval;
}


int cmixer_easy_all_select(int ComponentType)
{
	int i;
	for (i = 0; i < (int)waveInGetNumDevs(); i++) 
		cmixer_easy_select(i, ComponentType);
	return 0;
}


#endif


