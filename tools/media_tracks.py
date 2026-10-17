"""Inspect source soundtrack choices before transcoding a movie."""
from dataclasses import dataclass
from pathlib import Path
import json,re,subprocess,sys
import imageio_ffmpeg

@dataclass(frozen=True)
class AudioTrack:
    ordinal:int
    language:str='und'
    title:str=''
    codec:str='unknown'
    details:str=''
    default:bool=False

    def label(self):
        fields=[self.language,self.title,self.codec,self.details]
        return str(self.ordinal)+': '+' | '.join(field for field in fields if field)+(' | default' if self.default else '')

def probe_audio_tracks(path:Path,ffprobe:Path|None=None):
    if ffprobe:
        result=subprocess.run([str(ffprobe),'-v','error','-select_streams','a',
            '-show_entries','stream=codec_name,sample_rate,channels,channel_layout:stream_tags=language,title:stream_disposition=default',
            '-of','json',str(path)],capture_output=True,text=True,encoding='utf8',errors='replace')
        if result.returncode==0:
            payload=json.loads(result.stdout)
            tracks=[]
            for index,stream in enumerate(payload.get('streams',[])):
                tags=stream.get('tags',{})
                details=' / '.join(str(x) for x in (stream.get('sample_rate','')+' Hz' if stream.get('sample_rate') else '',
                    stream.get('channel_layout') or (str(stream['channels'])+' ch' if stream.get('channels') else '')) if x)
                tracks.append(AudioTrack(index,tags.get('language','und'),tags.get('title',''),stream.get('codec_name','unknown'),details,
                    bool(stream.get('disposition',{}).get('default'))))
            return tracks
    # The bundled FFmpeg is sufficient when a separate ffprobe isn't installed.
    result=subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-i',str(path)],
        capture_output=True,text=True,encoding='utf8',errors='replace')
    tracks=[];current=None
    for line in result.stderr.splitlines():
        if re.match(r'\s*Stream #',line):
            current=None
            match=re.match(r'\s*Stream #\d+:\d+(?:\[[^]]+\])?(?:\(([^)]+)\))?: Audio: ([^,]+)(.*)',line)
            if match:
                current=len(tracks)
                details=match[3].replace('(default)','').strip(' ,')
                tracks.append(AudioTrack(current,match[1] or 'und','',match[2].strip(),details,'(default)' in line))
        elif current is not None:
            match=re.match(r'\s*title\s*:\s*(.*)',line,re.I)
            if match:
                old=tracks[current]
                tracks[current]=AudioTrack(old.ordinal,old.language,match[1],old.codec,old.details,old.default)
    return tracks

def select_audio_track(tracks,selection,*,interactive=None):
    if not tracks:raise ValueError('The input has no audio tracks.')
    if selection=='ask':
        print('Available audio tracks (indices start at 0):')
        for track in tracks:print('  '+track.label())
        if interactive is None:interactive=sys.stdin.isatty()
        if not interactive:raise ValueError('Use --audio-track N in a noninteractive command; --list-audio-tracks lists the choices.')
        while True:
            value=input('Audio track number: ').strip()
            if value.isdecimal() and int(value)<len(tracks):return int(value)
            print('Choose one of the listed track numbers, or press Ctrl+C to cancel.')
    if selection is None:
        if len(tracks)>1 and (sys.stdin.isatty() if interactive is None else interactive):
            return select_audio_track(tracks,'ask',interactive=True)
        selection=0
    index=int(selection)
    if not 0<=index<len(tracks):raise ValueError(f'Audio track {index} is unavailable; use --list-audio-tracks to see the zero-based choices.')
    return index
