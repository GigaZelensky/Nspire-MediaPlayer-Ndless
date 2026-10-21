#include "audio/audio_playback.h"
#include <assert.h>
#include <string.h>
struct AudioPlayback {unsigned pumps;bool fail_on_pump,stop_on_pump;};
static bool due;
static unsigned acquisitions;
static uint32_t clock_ms;
static char sequence[16];
static unsigned sequence_size;
static void event(char c){assert(sequence_size+1<sizeof(sequence));sequence[sequence_size++]=c;sequence[sequence_size]=0;}
static bool battery_diagnostics_acquisition_due(uint32_t now){(void)now;return due;}
static bool battery_diagnostics_acquire(uint32_t now)
{assert(now==clock_ms);event('A');++acquisitions;clock_ms+=8;return true;}
static uint32_t monotonic_clock_now_ms(void){return clock_ms;}
void audio_playback_pump(AudioPlayback *a){event('P');++a->pumps;}
bool audio_playback_info(const AudioPlayback *a,AudioPlaybackInfo *info)
{event('I');info->failed=a->fail_on_pump;info->running=!a->stop_on_pump;return true;}
/* PRODUCTION_MAINTENANCE */
static void reset(AudioPlayback *a,AudioPlaybackInfo *info)
{*a=(AudioPlayback){0};*info=(AudioPlaybackInfo){.running=true};sequence_size=acquisitions=0;sequence[0]=0;clock_ms=100;due=true;}
int main(void)
{
    AudioPlayback audio;AudioPlaybackInfo info;uint32_t now=100;
    reset(&audio,&info);music_battery_maintenance(&audio,&info,&now);
    assert(!strcmp(sequence,"PIAPI") && acquisitions==1 && audio.pumps==2 && now==108);
    reset(&audio,&info);due=false;music_battery_maintenance(&audio,&info,&now);assert(!sequence_size && !acquisitions);
    reset(&audio,&info);info.running=false;music_battery_maintenance(&audio,&info,&now);assert(!sequence_size && !acquisitions);
    reset(&audio,&info);info.failed=true;music_battery_maintenance(&audio,&info,&now);assert(!sequence_size && !acquisitions);
    reset(&audio,&info);audio.fail_on_pump=true;music_battery_maintenance(&audio,&info,&now);
    assert(!strcmp(sequence,"PI") && !acquisitions && info.failed);
    reset(&audio,&info);audio.stop_on_pump=true;music_battery_maintenance(&audio,&info,&now);
    assert(!strcmp(sequence,"PI") && !acquisitions && !info.running);
    return 0;
}
