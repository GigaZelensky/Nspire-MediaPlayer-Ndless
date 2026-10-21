"""Check feature-selected builds and bounded automatic error reports."""
from pathlib import Path
import json, shutil, subprocess, sys, tempfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    cc=shutil.which('gcc') or shutil.which('clang')
    assert cc,'A host C compiler is required'
    with tempfile.TemporaryDirectory(prefix='nmedia-features-') as temporary:
        work=Path(temporary)
        source=work/'features.c'
        source.write_text('''#include <assert.h>
#include "media_config.h"
#include "player/media_files.h"
int main(void) {
 const unsigned video[]={NDVIDEO_WITH_H264,NDVIDEO_WITH_MPEG4,NDVIDEO_WITH_HEVC,NDVIDEO_WITH_AV1};
 const unsigned audio[]={NDMEDIA_WITH_PCM,NDMEDIA_WITH_IMA,NDMEDIA_WITH_MP3,NDMEDIA_WITH_FLAC,NDMEDIA_WITH_VORBIS,NDMEDIA_WITH_AAC,NDMEDIA_WITH_OPUS};
 for(unsigned i=0;i<4;++i)assert(media_video_codec_supported(i)==!!video[i]);
 for(unsigned i=0;i<7;++i)assert(media_audio_codec_supported(i)==!!(NDMEDIA_AUDIO && audio[i]));
 assert(!media_video_codec_supported(99) && !media_audio_codec_supported(99));
 assert(media_audio_path_supported("/nMedia/song.OPUS.tns")==!!(NDMEDIA_AUDIO && NDMEDIA_WITH_OPUS));
 assert(media_audio_path_supported("song.mp3")==!!(NDMEDIA_AUDIO && NDMEDIA_WITH_MP3));
 assert(media_is_playable("song.flac")==!!NDMEDIA_AUDIO);
 assert(media_is_playable("movie.nvp.tns"));
 assert(media_cover_rank("cover.jpg.tns")==3);
}''')
        cases=[['PROFILE=full'],['PROFILE=lite'],['PROFILE=custom','CODECS=',
            'AUDIO_CODECS=mp3 opus','USB_STORAGE=1','ARTWORK=0','VISUALIZATIONS=0']]
        for case in cases:
            config=json.loads(subprocess.check_output(['make','--no-print-directory','build-config',
                'PYTHON='+sys.executable,*case],cwd=ROOT,text=True))
            definitions=[x for x in config['compiler_flags'] if x.startswith('-D')]
            fast_usb=int(bool(config['usb_storage'] or config['audio_codecs']))
            assert '-DNDMEDIA_USB_FAST_ENUMERATION='+str(fast_usb) in definitions
            exe=work/'features.exe'
            subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),*definitions,str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
            if config['flavor']=='lite':
                assert not config['audio_codecs'] and not config['usb_storage']
                assert not any(s.startswith('src/audio/vendor/') for s in config['sources'])
                assert not any(s.startswith('src/player/music') for s in config['sources'])
            assert not config['automatic_healthy_logs']
        source.write_text('''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player/player_data.h"
int main(void) {
 const char *path="bounded.log";char block[1024];memset(block,'X',sizeof(block));
 FILE *f=player_error_log_open(path,false);assert(f);
 for(unsigned i=0;i<64;++i)assert(fwrite(block,1,sizeof(block),f)==sizeof(block));
 fclose(f);f=player_error_log_open(path,true);assert(f);fputs("new complete record",f);fclose(f);
 f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);assert(ftell(f)==19);fclose(f);
 f=player_error_log_open(path,true);assert(f);fputs(" plus another",f);fclose(f);
 f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);assert(ftell(f)==32);fclose(f);
 remove(path);
}''')
        subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(source),str(ROOT/'src/player/error_log.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=work,check=True)
    print('PASS Full/Lite/custom capabilities, stripped Lite source selection and complete-record error log rollover')

if __name__=='__main__':main()
