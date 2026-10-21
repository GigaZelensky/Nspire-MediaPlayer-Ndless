#include "audio_playback.h"
#include "media_config.h"
#include <string.h>
#if !NDMEDIA_AUDIO

/* Keep the movie-clock adapter independent of optional sound support. This
 * implementation has no USB/audio decoder dependencies in a video-only build. */
AudioPlayback *audio_playback_open(const char *p,const AudioPlaybackTrack *t,struct LoadingProgress *l)
{ (void)p;(void)t;(void)l;return NULL; }
void audio_playback_close(AudioPlayback *a){(void)a;}
void audio_playback_discard_retry(AudioPlayback *a){(void)a;}
bool audio_playback_can_reconnect(const AudioPlayback *a){(void)a;return false;}
bool audio_playback_cancelled(const AudioPlayback *a){(void)a;return false;}
void audio_playback_tick_loading(AudioPlayback *a,struct LoadingProgress *l,bool p,unsigned n,unsigned d)
{(void)a;(void)l;(void)p;(void)n;(void)d;}
void audio_playback_tick(AudioPlayback *a,bool p,unsigned n,unsigned d){(void)a;(void)p;(void)n;(void)d;}
void audio_playback_idle(AudioPlayback *a){(void)a;}
void audio_playback_pump(AudioPlayback *a){(void)a;}
void audio_playback_pause(AudioPlayback *a){(void)a;}
void audio_playback_suspend(AudioPlayback *a){(void)a;}
bool audio_playback_seek(AudioPlayback *a,uint32_t s){(void)a;(void)s;return false;}
bool audio_playback_retry(AudioPlayback *a,uint32_t s){(void)a;(void)s;return false;}
bool audio_playback_active(const AudioPlayback *a){(void)a;return false;}
bool audio_playback_source_lost(const AudioPlayback *a){(void)a;return false;}
uint64_t audio_playback_position(const AudioPlayback *a){(void)a;return 0;}
bool audio_playback_info(const AudioPlayback *a,AudioPlaybackInfo *i){(void)a;if(i)memset(i,0,sizeof(*i));return false;}
size_t audio_playback_snapshot(const AudioPlayback *a,int16_t *s,size_t n){(void)a;if(s)memset(s,0,n*4);return 0;}
bool audio_playback_level(const AudioPlayback *a,AudioPlaybackLevel *l){(void)a;if(l)memset(l,0,sizeof(*l));return false;}
uint32_t audio_playback_output_key(const AudioPlayback *a){(void)a;return 0;}
void audio_playback_set_volume(AudioPlayback *a,unsigned v){(void)a;(void)v;}
void audio_playback_volume(AudioPlayback *a,int v){(void)a;(void)v;}
void audio_playback_mute(AudioPlayback *a){(void)a;}
const char *audio_playback_status(AudioPlayback *a){(void)a;return NULL;}
void audio_playback_debug(const AudioPlayback *a,FILE *f){(void)a;(void)f;}
void audio_playback_context(AudioPlayback *a,uint32_t p,uint32_t n,const char *c){(void)a;(void)p;(void)n;(void)c;}
void audio_playback_checkpoint(AudioPlayback *a,const char *e){(void)a;(void)e;}
void audio_playback_diagnostics(AudioPlayback *a,void (*fn)(FILE *,const void *),const void *c,bool e)
{(void)a;(void)fn;(void)c;(void)e;}
#endif
