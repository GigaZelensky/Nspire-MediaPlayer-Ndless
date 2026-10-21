#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define MAX_PATH_LEN 512
/* PRODUCTION_TYPES */
static char history_file[1024];
static unsigned g_audio_volume=20;
static bool g_audio_muted,audio_preferences_loaded,pending_audio_save,g_pending_theme_save;
static UiThemeId g_ui_theme_id;
static bool writes_allowed=true;
static unsigned clock_ms,device_volume;
typedef struct {unsigned volume;bool mute;void *usb;} AudioPlayback;
static void usb_audio_volume(void *usb,unsigned volume){assert(usb);device_volume=volume;}
static int clamp_int(int value,int low,int high){return value<low?low:value>high?high:value;}
static VideoAlign clamp_video_align(int value){return (VideoAlign)clamp_int(value,-1,1);}
static UiThemeId ui_theme_clamp(int value){return (UiThemeId)clamp_int(value,0,UI_THEME_COUNT-1);}
static void ui_set_theme(UiThemeId theme){g_ui_theme_id=theme;}
static char *dup_string(const char *value){size_t n=strlen(value)+1;char *out=malloc(n);assert(out);memcpy(out,value,n);return out;}
static void free_history_store(HistoryStore *history)
{for(size_t i=0;i<history->count;++i)free(history->entries[i].path);history->count=0;}
static bool player_data_prepare(void){return writes_allowed;}
static bool player_data_path(const char *name,char *path,size_t size)
{assert(!strcmp(name,HISTORY_FILE_NAME) && strlen(history_file)<size);memcpy(path,history_file,strlen(history_file)+1);return true;}
static void history_path_for_movie(const char *movie,char *path,size_t size)
{(void)movie;assert(strlen(history_file)<size);memcpy(path,history_file,strlen(history_file)+1);}
static bool performance_clock_valid_mhz(unsigned mhz){return !mhz || (mhz>=132 && mhz<=492 && mhz%12==0);}
static uint32_t monotonic_clock_now_ms(void){return ++clock_ms;}
static void debug_tracef(const char *format,...){(void)format;}
/* PRODUCTION_PREFERENCES */
static void put(const char *text)
{FILE *f=fopen(history_file,"wb");assert(f);assert(fputs(text,f)>=0);assert(!fclose(f));}
int main(int argc,char **argv)
{
    assert(argc==2 && strlen(argv[1])<sizeof(history_file));strcpy(history_file,argv[1]);
    unsigned volume=20;bool mute=false;
    const char *bad[]={"@audio\t101\t0\n","@audio\t-1\t0\n","@audio\tx\t0\n",
        "@audio\t3\t2\n","@audio\t3\t0junk\n","@audio\t9999999999999999999999\t0\n","@audio\t3\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i)assert(!history_parse_audio_line(bad[i],&volume,&mute));
    assert(history_parse_audio_line("@audio\t0\t1\r\n",&volume,&mute) && !volume && mute);
    put("NDVH1\n42\tlegacy.nvp.tns\n");
    HistoryStore legacy;assert(load_history_store_from_path(history_file,&legacy));
    assert(legacy.audio_volume==20 && !legacy.audio_muted && !legacy.has_audio_preference && legacy.count==1);
    free_history_store(&legacy);
    put("NDVH6\n@theme\t1\n@clock\t396\t1\n@audio\t33\t1\n"
        "@settings\t0\t3\t2\t0\t2\t0\t0\t0\t0\n"
        "1\t901\t0\t3\t2\t2\t0\t0\t0\t0\t0\t0\tvideo.nvp.tns\n");
    HistoryStore stale,read;assert(load_history_store_from_path(history_file,&stale));
    assert(stale.audio_volume==33 && stale.audio_muted && stale.count==1 && stale.entries[0].frame==901);
    load_app_preferences();
    if(NDMEDIA_AUDIO) {
        assert(audio_preferences_loaded && g_audio_volume==33 && g_audio_muted);
        AudioPlayback track={.volume=33,.mute=true,.usb=&track};
        audio_playback_set_volume(&track,55);
        assert(track.volume==55 && !track.mute && device_volume==55 && g_audio_volume==55 && !g_audio_muted && pending_audio_save);
        assert(load_history_store_from_path(history_file,&read));assert(read.audio_volume==33);free_history_store(&read);
        assert(save_history_store_to_path(history_file,&stale)); /* An old picker snapshot must not overwrite live volume. */
        assert(!pending_audio_save);
        assert(load_history_store_from_path(history_file,&read));
        assert(read.audio_volume==55 && !read.audio_muted && read.count==1 && read.entries[0].frame==901 && read.clock_mhz==396);
        free_history_store(&read);
        history_set_audio_preference(0,true);g_ui_theme_id=UI_THEME_GREEN;ui_queue_theme_save();
        writes_allowed=false;flush_queued_preferences_save("failed");assert(pending_audio_save && g_pending_theme_save);
        writes_allowed=true;flush_queued_preferences_save("exit");assert(!pending_audio_save && !g_pending_theme_save);
        audio_preferences_loaded=false;g_audio_volume=20;g_audio_muted=false;load_app_preferences();
        assert(g_audio_volume==0 && g_audio_muted && g_ui_theme_id==UI_THEME_GREEN);
        history_set_audio_preference(100,false);
        assert(history_save_clock_preference("app",456,false));
        assert(load_history_store_from_path(history_file,&read));
        assert(read.audio_volume==100 && !read.audio_muted && read.clock_mhz==456 && read.count==1 && read.entries[0].frame==901);
        free_history_store(&read);
        put("unreadable history format\n");history_set_audio_preference(12,false);flush_queued_preferences_save("bad-header");
        assert(pending_audio_save);char buffer[80];FILE *f=fopen(history_file,"rb");assert(fgets(buffer,sizeof(buffer),f));fclose(f);
        assert(!strcmp(buffer,"unreadable history format\n"));
    } else {
        assert(!audio_preferences_loaded && g_audio_volume==20 && !g_audio_muted);
        assert(save_history_store_to_path(history_file,&stale));
        assert(load_history_store_from_path(history_file,&read));
        assert(read.audio_volume==33 && read.audio_muted && read.entries[0].frame==901);free_history_store(&read);
    }
    /* Start a valid independent case after the rejected-corrupt-file test. */
    assert(save_history_store_to_path(history_file,&stale));
    g_ui_theme_id=UI_THEME_PURPLE;ui_queue_theme_save();flush_queued_preferences_save("purple");
    assert(!g_pending_theme_save && load_history_store_from_path(history_file,&read));
    assert(read.theme_id==UI_THEME_PURPLE && read.count==1 && read.entries[0].frame==901);
    free_history_store(&read);
    free_history_store(&stale);remove(history_file);
    puts("PASS app-wide volume/mute history: legacy default, zero/full range, stale-snapshot merge, deferred writes, restart, clock/theme/resume preservation, failed-save retry and Lite compatibility");
    return 0;
}
