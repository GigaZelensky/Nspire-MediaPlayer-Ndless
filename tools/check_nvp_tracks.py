#!/usr/bin/env python3
"""Real encoded payload preservation through add/replace/remove track edits."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile,shutil

def validate_with_player(files,work):
    root=Path(__file__).resolve().parents[1]
    compiler=shutil.which('gcc') or shutil.which('clang')
    if not compiler or not (root/'src/movie/nvp_validation.c').is_file():return
    source=work/'player-container-check.c'
    source.write_text(r'''
#include "movie/nvp_validation.h"
#include "movie/nvp_audio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
 for(int i=1;i<argc;i++) {
  FILE *file=fopen(argv[i],"rb");assert(file);
  assert(!fseek(file,0,SEEK_END));long bytes=ftell(file);assert(bytes>56);
  rewind(file);unsigned char header_bytes[56];assert(fread(header_bytes,1,56,file)==56);
  MovieHeader header;assert(nvp_header_parse(&header,header_bytes,56));
  if(header.version==MOVIE_VERSION_MEDIA) {
   unsigned char footer[NVP_AUDIO_FOOTER_BYTES];NvpAudioTrack audio;
   assert(!fseek(file,bytes-NVP_AUDIO_FOOTER_BYTES,SEEK_SET));assert(fread(footer,1,sizeof footer,file)==sizeof footer);
   assert(nvp_audio_parse(&audio,footer,sizeof footer,(uint64_t)bytes,&header));
  } else assert(nvp_header_is_valid(&header,(uint64_t)bytes));
  ChunkIndexEntry *index=calloc(header.chunk_count,sizeof(*index));assert(index);
  assert(!fseek(file,(long)header.index_offset,SEEK_SET));unsigned wire=nvp_index_wire_bytes(header.version);
  for(unsigned n=0;n<header.chunk_count;n++){unsigned char entry[28];assert(fread(entry,1,wire,file)==wire);nvp_index_parse(index+n,entry,header.version);}
  assert(nvp_index_is_valid(&header,index));free(index);fclose(file);
 }
 return 0;
}
''')
    exe=work/'player-container-check.exe'
    subprocess.run([compiler,'-std=c99','-O2','-I'+str(root/'src'),str(source),
        str(root/'src/movie/nvp_validation.c'),str(root/'src/movie/nvp_audio.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe),*[str(file) for file in files]],check=True)
    print('PASS edited files accepted by the actual calculator C header/index/audio validators')

def check_tracks(source,encoded,work):
    from nvp_tracks import NvpFile,edit_tracks
    from encode_ndless_video import encode,parse_args
    before=NvpFile.read(encoded);original_hash=before.video_hash()
    assert before.audio and before.subtitles
    stripped=work/'stripped.nvp.tns'
    edit_tracks(encoded,stripped,audio_action='remove',keep_subtitle_tracks=[])
    plain=NvpFile.read(stripped)
    assert plain.header[1]==12 and plain.audio is None and not plain.subtitles
    assert plain.video_hash()==original_hash
    restored=work/'restored.nvp.tns'
    edit_tracks(stripped,restored,audio_action='replace',track_source=str(source),audio_track=1,
        audio_codec='mp3',source_subtitle_tracks=[0,1],keep_subtitle_tracks=[])
    after=NvpFile.read(restored)
    assert after.header[1]==13 and after.audio[7]==2 and len(after.subtitles)==2
    assert after.video_hash()==original_hash and after.header[9:11]==before.header[9:11]
    retained=work/'retained.nvp.tns'
    edit_tracks(restored,retained,keep_subtitle_tracks=[1])
    kept=NvpFile.read(retained)
    assert kept.video_hash()==original_hash and len(kept.subtitles)==1
    def audio_hash(movie):
        with movie.path.open('rb') as file:file.seek(movie.audio[2]);return hashlib.sha256(file.read(movie.audio[3])).hexdigest()
    assert audio_hash(kept)==audio_hash(after)
    # Exercise the 48-byte/24-byte-index upgrade, not only NVP v13 input.
    legacy=work/'video-only.nvp.tns'
    encode(parse_args([str(source),'--output',str(legacy),'--preset','ultrafast','--fps','24']))
    small=NvpFile.read(legacy);assert small.header[1]==10
    upgraded=work/'upgraded.nvp.tns'
    edit_tracks(legacy,upgraded,audio_action='replace',track_source=str(source),audio_track=0,source_subtitle_tracks=[1])
    upgraded=NvpFile.read(upgraded);assert upgraded.video_hash()==small.video_hash() and upgraded.audio[7]==6 and len(upgraded.subtitles)==1
    # Original-source subtitles follow the same start offset as replacement audio.
    trimmed=work/'trimmed.nvp.tns'
    encode(parse_args([str(source),'--output',str(trimmed),'--preset','ultrafast','--fps','24','--start','1','--duration','1']))
    shifted=work/'shifted.nvp.tns'
    edit_tracks(trimmed,shifted,audio_action='replace',track_source=str(source),source_start=1,source_subtitle_tracks=[1])
    shifted=NvpFile.read(shifted)
    assert shifted.duration==1 and shifted.subtitles[0].cues[0].start_ms==0 and shifted.subtitles[0].cues[0].end_ms==1000
    bad=work/'bad.nvp.tns';bad.write_bytes(encoded.read_bytes()[:80])
    try:NvpFile.read(bad)
    except (ValueError,OSError):pass
    else:raise AssertionError('Truncated media must not be edited')
    assert before.video_hash()==original_hash
    validate_with_player([encoded,stripped,restored,retained,legacy,upgraded.path,trimmed,shifted.path],work)
    print('PASS lossless NVP edits: keep/add/replace/remove audio and subtitles; v10 upgrade, exact FPS, copied video/audio hashes, original-source trim alignment and malformed-input refusal')
    return stripped

def main():
    from check_encoder_gui import source_fixture
    from encode_ndless_video import encode,parse_args
    with tempfile.TemporaryDirectory(prefix='nmedia-tracks-check-') as temporary:
        work=Path(temporary);source=source_fixture(work);encoded=work/'with-tracks.nvp.tns'
        encode(parse_args([str(source),'--output',str(encoded),'--preset','ultrafast','--fps','24',
            '--audio','--audio-codec','opus','--audio-track','1','--subtitle','embedded','--subtitle-track','0']))
        check_tracks(source,encoded,work)

if __name__=='__main__':main()
