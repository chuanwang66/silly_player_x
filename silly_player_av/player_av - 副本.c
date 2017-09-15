#include <stdio.h>
#include "silly_player.h"

#include <SDL.h>

//ffmpeg
#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

/** **************************** video displaying(main thread) **************************** **/
static Uint32 video_refresh_timer_cb(Uint32 delay, void *opaque){
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;
}

static void video_refresh_timer(void *userdata){
    VideoState *is = (VideoState *)userdata;
    VideoPicture *vp;
    double actural_delay, delay, diff;

    //decoder not opened -> check later
    if(!is->video_st){
        SDL_AddTimer(100, video_refresh_timer_cb, is);
        return;
    }
    assert(is->video_st != NULL);

    //YUV image not ready -> check later
    if(is->pictq_size == 0){
        SDL_AddTimer(1, video_refresh_timer_cb, is);
        return;
    }
    assert(is->pictq_size > 0);

    //
    vp = &is->pictq;

#if 1
    //maintain delay & pts
    delay = vp->pts - is->frame_last_pts;
    if(delay <= 0 || delay >= 1.0){ //unit: second
        delay = is->frame_last_delay;
    }
    delay = fmax(delay, AV_SYNC_THRESHOLD);
    is->frame_last_delay = delay;
    is->frame_last_pts = vp->pts;

    //(update delay to sync to audio)
    diff = vp->pts - get_audio_clock(is);
    if(fabs(diff) <= AV_NOSYNC_THRESHOLD){ //if it's possible to sync
        if(diff <= -delay){
            delay = 0; //speed video up
        }else{
            delay = 2 * delay; //slow video down
        }
    }
    is->frame_timer += delay;

    actural_delay = is->frame_timer - (av_gettime() / 1000000.0);
    actural_delay = fmax(actural_delay, 0.010);
#else
    actural_delay = 0.04;
#endif

    //schedule the next refresh
    SDL_AddTimer((int)(actural_delay * 1000 + 0.5), video_refresh_timer_cb, is);

    //FINALLY, a YUV image is waiting for us to display!
    video_display(is);

    //hunger for more, please decoding!
    SDL_LockMutex(is->pictq_mutex);
    --is->pictq_size;
    SDL_CondSignal(is->pictq_cond);
    SDL_UnlockMutex(is->pictq_mutex);
}

int main_player(int argc, char* argv[])
{
    SDL_Event sdlEvent;
    SDL_Thread *parse_tid, *video_tid;
    uint64_t refresh_cnt;

    if(argc < 2)
    {
        fprintf(stderr, "usage: $PROG_NAME $VIDEO_FILE_NAME.\n");
        exit(1);
    }

    is = av_mallocz(sizeof(VideoState)); //memory allocation with alignment, why???
    strncpy(is->filename, argv[1], sizeof(is->filename));
    is->pictq_mutex = SDL_CreateMutex();
    is->pictq_cond = SDL_CreateCond();
    is->audio_stream_index = -1;
    is->video_stream_index = -1;

    //register all formats & codecs
    av_register_all();

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        fprintf(stderr, "SDL_Init() error: %s\n", SDL_GetError());
        av_free(is);
        exit(1);
    }

    if(open_input() != 0) {
        av_free(is);
        return -1;
    }

    if(open_audio_decoder() != 0 || open_video_decoder() != 0) {
        av_free(is);
        return -1;
    }

    //parsing thread (reading packets from stream)
    parse_tid = SDL_CreateThread(parse_thread, "PARSING_THREAD", is);
    if(!parse_tid) {
        fprintf(stderr, "create parsing thread failed.\n");
        av_free(is);
        return -1;
    }

    //audio-decoding thread (audio pkt --> frame --> audio buffer)
    SDL_PauseAudio(0);

    //video-decoding thread (video pkt --> frame --> YUV image)
    video_tid = SDL_CreateThread(video_thread, "VIDEO_DECODING_THREAD", is);
    if(!video_tid) {
        fprintf(stderr, "create video thread failed.\n");
        av_free(is);
        return -1;
    }

    //////////////////////////////// the window ////////////////////////////////
    //once AVCodecContext is known, the size of window is known
    video_init(is);
    SDL_AddTimer(40, video_refresh_timer_cb, is);

    refresh_cnt = 0;
    for(;;){
        SDL_WaitEvent(&sdlEvent);
        switch(sdlEvent.type){
        case FF_REFRESH_EVENT:
            //fprintf(stderr, "[fre%ld]", ++refresh_cnt);
            video_refresh_timer(sdlEvent.user.data1);
            break;
        case SDL_QUIT:
            fprintf(stderr, "event:quit\n");
            global_exit_parse = 1;
            global_exit = 1;
            break;
        default:
            //fprintf(stderr, "event:%d\n", sdlEvent.type);
            break;
        }

        if(global_exit) break;
    }

    SDL_CondSignal(is->pictq_cond);
    SDL_DestroyCond(is->pictq_cond);
    SDL_DestroyMutex(is->pictq_mutex);

    SDL_WaitThread(parse_tid, NULL);
    SDL_WaitThread(video_tid, NULL);

    SDL_Quit();
    return 0;
}
