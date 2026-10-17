#include "movie_audio.h"
#include "audio_playback.h"
#include "player/player_internal.h"

static void context(const Movie *m)
{
    if (m) audio_playback_context(m->audio,m->current_frame,m->header.frame_count,
                                  movie_codec_name(m->codec));
}
void movie_audio_open(Movie *m, const char *path, LoadingProgress *loading)
{
    if (!NDMEDIA_AUDIO || !m || m->header.version!=MOVIE_VERSION_MEDIA || m->audio) return;
    if(!media_audio_codec_supported(m->audio_track.codec))return;
    AudioPlaybackTrack track={m->audio_track.offset,m->audio_track.bytes,
        m->audio_track.samples,m->audio_track.rate,m->audio_track.channels,m->audio_track.codec};
    m->audio=audio_playback_open(path,&track,loading);
    context(m);
}
bool movie_audio_close(Movie *m)
{
    if (m) { context(m);audio_playback_close(m->audio);m->audio=NULL; }
    return true;
}
void movie_audio_checkpoint(const Movie *m)
{ if (m) {context(m);audio_playback_checkpoint(m->audio,"clock menu");} }
void movie_audio_suspend(const Movie *m)
{ if (m) audio_playback_suspend(m->audio); }
void movie_audio_pause(const Movie *m)
{ if (m) audio_playback_pause(m->audio); }
static uint32_t frame_sample(const Movie *m)
{
    uint64_t sample=(uint64_t)m->current_frame*m->header.fps_den*m->audio_track.rate/m->header.fps_num;
    return sample>m->audio_track.samples?m->audio_track.samples:(uint32_t)sample;
}
void movie_audio_reanchor(const Movie *m)
{ if (m && m->audio) audio_playback_seek(m->audio,frame_sample(m)); }
void movie_audio_tick(const Movie *m, bool paused, unsigned num, unsigned den)
{ if (m) {context(m);audio_playback_tick(m->audio,paused,num,den);} }
void movie_audio_pump(const Movie *m)
{ if (m) audio_playback_pump(m->audio); }
void movie_audio_idle(const Movie *m)
{
    /* The shared transport retains its current playback rate on pause/suspend.
     * Normal movie turns pass the selected rate explicitly. */
    if (movie_audio_master(m)) audio_playback_idle(m->audio);
}
bool movie_audio_master(const Movie *m)
{ return m && audio_playback_active(m->audio); }
bool movie_audio_source_lost(const Movie *m)
{ return m && audio_playback_source_lost(m->audio); }
uint32_t movie_audio_frame(const Movie *m)
{
    if (!movie_audio_master(m)) return m?m->current_frame:0;
    uint64_t sample=audio_playback_position(m->audio)>>32;
    return (uint32_t)(sample*m->header.fps_num/((uint64_t)m->audio_track.rate*m->header.fps_den));
}
uint32_t movie_audio_wait_ms(const Movie *m, unsigned num, unsigned den)
{
    if (!movie_audio_master(m) || !num) return 1;
    uint64_t sample=audio_playback_position(m->audio)>>32;
    uint64_t next=((uint64_t)m->current_frame+1)*m->header.fps_den*m->audio_track.rate/m->header.fps_num;
    uint64_t wait=next>sample?(next-sample)*1000*den/((uint64_t)m->audio_track.rate*num):0;
    return wait>1000?1000:(uint32_t)wait;
}
const char *movie_audio_status(const Movie *m)
{ return m?audio_playback_status(m->audio):NULL; }
bool movie_audio_level(const Movie *m, MovieAudioLevel *level)
{ return m && audio_playback_level(m->audio,level); }
void movie_audio_set_volume(const Movie *m, unsigned volume)
{ if (m) audio_playback_set_volume(m->audio,volume); }
void movie_audio_volume(const Movie *m, int change)
{ if (m) audio_playback_volume(m->audio,change); }
void movie_audio_mute(const Movie *m)
{
    if (!m) return;
    AudioPlaybackInfo info;
    if (audio_playback_info(m->audio,&info) && info.failed)
        audio_playback_retry(m->audio,frame_sample(m));
    else audio_playback_mute(m->audio);
}
void movie_audio_debug(const Movie *m, FILE *file)
{ if (m) audio_playback_debug(m->audio,file); }
