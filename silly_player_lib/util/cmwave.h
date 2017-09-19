//=====================================================================
//
// Extreme Visions WaveOut/WaveIn/Mixer driver for Windows
//
// This program is distributed without warrenty of any kind
// and is not guaranteed to work on any system. You use this
// program entirely at your own risk.
//
//=====================================================================

#if defined(WIN32) || defined(WIN64) || defined(_WIN32) || defined(_WIN64)

#include "c99defs.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>


//---------------------------------------------------------------------
// class definition
//---------------------------------------------------------------------
struct CWAVEOUT;
struct CWAVEIN;
struct CMIXER;

typedef struct CWAVEOUT CWAVEOUT;
typedef struct CWAVEIN CWAVEIN;
typedef struct CMIXER CMIXER;


#ifdef __cplusplus
extern "C" {
#endif
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
//---------------------------------------------------------------------
EXPORT CWAVEOUT *cwaveout_open(int device, int srate, int nch, int bps, 
	int bufferlenms, int prebufferms);


//---------------------------------------------------------------------
// cwaveout_close - close the device if it is open. calling this with
// the device closed will trigger an assert failure in debug mode.
//---------------------------------------------------------------------
EXPORT int cwaveout_close(CWAVEOUT *waveout);


//---------------------------------------------------------------------
// cwaveout_write - this will actually block if you write too much
// but the input plug-in should be smart enough to realise that it
// should write 
// a) no more than 8192 bytes and
// b) no more than imw_canwrite() returns. 
// we could add another size check here but it's not really worth it.
//---------------------------------------------------------------------
EXPORT int cwaveout_write(CWAVEOUT *waveout, const void *data, int size);


//---------------------------------------------------------------------
// cwaveout_canwrite - return number of bytes that can be written - 
// 0 if device is closed, note that this can be called before playback
// has been started.
//---------------------------------------------------------------------
EXPORT int cwaveout_canwrite(const CWAVEOUT *waveout);


//---------------------------------------------------------------------
// cwaveout_isplaying - returns 1 if blocks are currently submitted
// to the wave functions.
//---------------------------------------------------------------------
EXPORT int cwaveout_isplaying(const CWAVEOUT *waveout);


//---------------------------------------------------------------------
// cwaveout_pause - pause or resume playback
//---------------------------------------------------------------------
EXPORT int cwaveout_pause(CWAVEOUT *waveout, int pause);


//---------------------------------------------------------------------
// cwaveout_setvolume - set the volume (volume ranges from 0->255)
//---------------------------------------------------------------------
EXPORT void cwaveout_setvolume(CWAVEOUT *waveout, int volume);


//---------------------------------------------------------------------
// imw_setpan - set the pan (pan ranges from -128 to +128)
//---------------------------------------------------------------------
EXPORT void cwaveout_setpan(CWAVEOUT *waveout, int pan);


//---------------------------------------------------------------------
// cwaveout_flush - flush buffers and continue playback from 'time'.
//---------------------------------------------------------------------
EXPORT void cwaveout_flush(CWAVEOUT *waveout, int time);


//---------------------------------------------------------------------
// cwaveout_getoutputtime - return the number of milliseconds of
// audio that have actually been played.
//---------------------------------------------------------------------
EXPORT int cwaveout_getoutputtime(const CWAVEOUT *waveout);


//---------------------------------------------------------------------
// cwaveout_getwrittentime - return the number of milliseconds of
// audio that have been written to this program but not necessarily
// played (could be still buffered).
//---------------------------------------------------------------------
EXPORT int cwaveout_getwrittentime(CWAVEOUT *waveout);


//---------------------------------------------------------------------
// get HWAVEOUT & DeviceID, returns latency
//---------------------------------------------------------------------
EXPORT int cwaveout_getid(const CWAVEOUT *waveout, LPHWAVEOUT phwo, int *device);

//---------------------------------------------------------------------
// cwaveout_getdevicenum - return the audio output device number
//---------------------------------------------------------------------
EXPORT int cwaveout_getdevicenum();

//=====================================================================
//                          WAVEIN INTERFACE 
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
EXPORT CWAVEIN *cwavein_open(int device, int srate, int nch, int bps,
	int buflen, int prebuf);


//---------------------------------------------------------------------
// cwavein_close - close the device if it is open. calling this with
// the device closed will trigger an assert failure in debug mode.
//---------------------------------------------------------------------
EXPORT int cwavein_close(CWAVEIN *wavein);


//---------------------------------------------------------------------
// imc_read - this will actually block if you write too much
// but the input plug-in should be smart enough to realise that it
// should read 
// a) no more than 8192 bytes and
// b) no more than imc_canread() returns. 
// we could add another size check here but it's not really worth it.
//---------------------------------------------------------------------
EXPORT int cwavein_read(CWAVEIN *wavein, void *data, int size);


//---------------------------------------------------------------------
// imc_canread - return number of bytes that can be read - 
// 0 if device is closed, note that this can be called before playback
// has been started.
//---------------------------------------------------------------------
EXPORT int cwavein_canread(const CWAVEIN *wavein);


//---------------------------------------------------------------------
// cwavein_start - start recording
//---------------------------------------------------------------------
EXPORT int cwavein_start(CWAVEIN *wavein);


//---------------------------------------------------------------------
// cwavein_stop - stop recording
//---------------------------------------------------------------------
EXPORT int cwavein_stop(CWAVEIN *wavein);


//---------------------------------------------------------------------
// imc_reset - reset recording (clear buffer and reset to stop status)
//---------------------------------------------------------------------
EXPORT int cwavein_reset(CWAVEIN *wavein);


//---------------------------------------------------------------------
// get HWAVEOUT & DeviceID, returns latency
//---------------------------------------------------------------------
EXPORT int cwavein_getid(const CWAVEIN *wavein, LPHWAVEIN phwi, int *device);



//=====================================================================
//                          MIXER INTERFACE 
//=====================================================================

EXPORT CMIXER *cmixer_open(const CWAVEIN *wavein, int WaveInDeviceID);

EXPORT void cmixer_close(CMIXER *mixer);

EXPORT int cmixer_getlinecount(const CMIXER *mixer);

EXPORT const MIXERLINE *cmixer_getline(const CMIXER *mixer, int n, MIXERLINE *line);

EXPORT int cmixer_findtype(const CMIXER *mixer, int ComponentType);

EXPORT int cmixer_select(CMIXER *mixer, int n);

EXPORT int cmixer_mute(CMIXER *mixer, int n, int mute);

EXPORT int cmixer_volume(CMIXER *mixer, int n, int volume);

EXPORT int cmixer_loudness(CMIXER *mixer, int n, int loudness);



EXPORT int cmixer_component_select(CMIXER *mixer, int ComponentType);

EXPORT int cmixer_component_volume(CMIXER *mixer, int ComponentType, int vol);

EXPORT int cmixer_component_mute(CMIXER *mixer, int ComponentType, int mute);

EXPORT int cmixer_component_loudness(CMIXER *mixer, int ComponentType, int value);



EXPORT const char *cmixer_get_component_type(DWORD dwComponentType);

EXPORT const char *cmixer_get_target_type(DWORD dwType);

EXPORT const char *cmixer_get_control_type(DWORD dwControlType);



EXPORT int cmixer_easy_select(int WaveInDeviceID, int ComponentType);

EXPORT int cmixer_easy_volume(int WaveInDeviceID, int ComponentType, int volume);

EXPORT int cmixer_easy_mute(int WaveInDeviceID, int ComponentType, int mute);

EXPORT int cmixer_easy_loudness(int WaveInDeviceID, int ComponentType, int value);

EXPORT int cmixer_easy_all_select(int ComponentType);


#ifdef __cplusplus
}
#endif

#endif