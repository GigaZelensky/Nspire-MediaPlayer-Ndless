"""Library cache transactions, cached covers and display-only character folding."""
from pathlib import Path
import argparse
import tempfile
import subprocess
from check_audio_controls import function
ROOT=Path(__file__).resolve().parents[1]
def check(out):
    root=ROOT
    out.mkdir(parents=True,exist_ok=True)
    internal=(root/'src/player/player_internal.h').read_text();start=internal.index('typedef struct {\n    char *name;');end=internal.index('} MoviePickerCache;',start)+len('} MoviePickerCache;');types=internal[start:end]
    source=(root/'src/player/movie_open_scan.c').read_text();a=source.index('typedef struct {\n    MovieFile *files;');b=source.index('static MovieFile *scan_directory',a)
    preamble='''#include <assert.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "player/media_files.h"
    #include "player/display_text.h"
    #define MAX_PATH_LEN 512
    #define PICKER_MAX_FILES 1024
    typedef int MovieCodec;
    #define MOVIE_CODEC_UNKNOWN 0
    '''+types+'''
    typedef struct {size_t count;struct {const char *path;bool has_resume;uint32_t frame;} *entries;} HistoryStore;
    MoviePickerCache g_picker_cache,g_previous_picker_cache;
    static char *dup_string(const char *s){size_t n=strlen(s)+1;char *p=malloc(n);assert(p);memcpy(p,s,n);return p;}
    static bool usb_path(const char *p){return !strncmp(p,"usb:",4);}
    static const char *player_data_directory(void){return "/data";}
    static const char *media_browser_root(void){return "/calc";}
    static const char *usb_storage_root(void){return "usb:/123";}
    static bool usb_storage_mounted(void){return false;}
    static void movie_display_fields_for_filename(const char *n,char **t,char **d){*t=dup_string(n);*d=NULL;}
    static void movie_picker_timing_stop(void){}
    static void strip_filename(char *p){char *s=strrchr(p,'/');if(s)*s=0;}
    '''
    mem=(root/'src/player/input_timing_memory.c').read_text()
    preamble+=function(mem,'free_movie_files')+'\n'+function(mem,'clear_movie_picker_cache')+'\n'+source[a:b]
    preamble+='\n'+ '\n'.join(line for line in internal.splitlines() if line.startswith(('#define PICKER_VISIBLE_ROWS ', '#define PICKER_INTRO_ANIM_MS ', '#define PICKER_INTRO_ROW_STAGGER_MS ')))+'\n'
    preamble+=function((root/'src/player/playback_ui.c').read_text(),'ui_ease_out_cubic')+'\n'
    preamble+=(root/'src/player/picker_refresh.c').read_text().replace('#include "player_internal.h"','')+'\n'
    preamble+='''
    static MovieFile *counting;
    static uint32_t pending_files,pending_folders,fake_now=1200;
    static uint32_t monotonic_clock_now_ms(void){return fake_now;}
    static void picker_folder_counts_stop(void){counting=NULL;}
    '''+function((root/'src/player/picker_folder_counts.c').read_text(),'count_finished')+'\n'
    preamble+=(root/'tools/tests/media/library_refresh.c').read_text()+'\n'
    preamble+='''
    static unsigned scans;static bool scan_ok=true;
    static MovieFile *scan_directory(const char *directory,size_t *count,char *cover,size_t bytes,bool *ok){
     ++scans;*ok=scan_ok;if(!scan_ok)return NULL;
     /* A pending scan has not destroyed the visible old list. */
     if(g_picker_cache.files)assert(g_picker_cache.files[0].name && *g_picker_cache.files[0].name);
     MovieFile *f=calloc(1,sizeof(*f));assert(f);f->name=dup_string(directory);f->path=dup_string(directory);f->kind=PICKER_AUDIO;
     *count=1;if(cover)snprintf(cover,bytes,"%s/cover.jpg",directory);return f;
    }
    MovieFile *scan_movies(const char *d,size_t *n){bool ok;return scan_directory(d,n,NULL,0,&ok);}
    '''
    preamble+=function(source,'movie_picker_cover_path')+'\n'+function(source,'ensure_movie_picker_cache')
    preamble+='''
    int main(void){
     char out[128];
     display_text_ascii("Joaqu\\xc3\\xadn Sabina - F\\xc3\\xadsica y Qu\\xc3\\xadmica",out,sizeof(out),false);
     assert(!strcmp(out,"Joaquin Sabina - Fisica y Quimica"));
     display_text_ascii("Joaqu\\xa1n Sabina - F\\xa1sica y Qu\\xa1mica",out,sizeof(out),true);
     assert(!strcmp(out,"Joaquin Sabina - Fisica y Quimica"));
     display_text_ascii("Bj\\xc3\\xb6rk \\xe2\\x80\\x94 pi\\xc3\\xb1""ata",out,sizeof(out),false);assert(!strcmp(out,"Bjork - pinata"));
     char small[3];display_text_ascii("abcdef",small,sizeof(small),false);assert(!strcmp(small,"ab"));
     MovieScan scan={.directory="usb:/123/F\\xa1sica",.cover=out,.cover_bytes=sizeof(out)};
     assert(scan_entry(&scan,"Sabina_F\\xa1sica_front.jpg",false,173256));assert(!scan.count);
     assert(strstr(out,"front.jpg") && (unsigned char)out[10]==0xa1);
     assert(scan_entry(&scan,"01 - Canci\\xa2n.flac",false,30000000));assert(scan.count==1);
     assert(!strcmp(scan.files[0].name,"01 - Cancion"));assert(strchr(scan.files[0].path,(char)0xa2));
     free_movie_files(scan.files,scan.count);
     ensure_movie_picker_cache(&g_picker_cache,"usb:/123/A");assert(scans==1 && g_picker_cache.valid);
     g_picker_cache.has_position=true;g_picker_cache.selected_index=0;g_picker_cache.scroll_start=3;
     MovieFile *parent=g_picker_cache.files;
     ensure_movie_picker_cache(&g_picker_cache,"usb:/123/B");assert(scans==2 && g_previous_picker_cache.files==parent);
     ensure_movie_picker_cache(&g_picker_cache,"usb:/123/A");assert(scans==2 && g_picker_cache.files==parent && g_picker_cache.scroll_start==3);
     g_picker_cache.valid=false;
     ensure_movie_picker_cache(&g_picker_cache,"usb:/123/A");
     assert(scans==3 && g_picker_cache.files==parent && !g_picker_cache.content_changed && g_picker_cache.scroll_start==3);
     scan_ok=false;ensure_movie_picker_cache(&g_picker_cache,"usb:/123/removed");
     assert(g_picker_cache.files==parent && g_picker_cache.load_failed);
     ensure_movie_picker_cache(&g_picker_cache,"usb:/123/A");assert(!g_picker_cache.load_failed);
     assert(movie_picker_cover_path("usb:/123/A/song.flac",out,sizeof(out)) && !strcmp(out,"usb:/123/A/cover.jpg"));
     clear_movie_picker_cache(&g_picker_cache);clear_movie_picker_cache(&g_previous_picker_cache);
     check_refresh();
     puts("PASS CP437/UTF8 folding preserves storage paths; cover cached during scan; atomic cache replacement, Back reuse and scan failure preserve visible rows");
    }
    '''
    (out/'library-test.c').write_text(preamble)
    import subprocess
    subprocess.run(['gcc','-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(out/'library-test.c'),str(root/'src/player/display_text.c'),'-o',str(out/'library-test.exe')],check=True)
    subprocess.run([str(out/'library-test.exe')],check=True)
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
    if args.output:check(args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='ndvideo-library-') as path:check(Path(path))
