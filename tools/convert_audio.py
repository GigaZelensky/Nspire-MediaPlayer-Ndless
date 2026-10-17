#!/usr/bin/env python3
"""Convert an ordinary music file, preserving text tags and embedded album art."""
from pathlib import Path
import argparse
import math
import os
import subprocess
import tempfile
import imageio_ffmpeg
from audio_metadata import Metadata, read_metadata, music_info, write_metadata

EXTENSIONS={'opus':'.opus','mp3':'.mp3','aac':'.m4a','vorbis':'.ogg','flac':'.flac','pcm':'.wav','ima':'.wav'}
MP3_RATES=(8,16,24,32,40,48,56,64,80,96,112,128,160,192,224,256,320)

def output_path(path,codec):
    path=Path(path).expanduser()
    suffix='.tns' if path.name.lower().endswith('.tns') else ''
    base=path.with_suffix('') if suffix else path
    extension=EXTENSIONS[codec]
    if base.suffix.lower()!=extension:base=base.with_suffix(extension)
    return str(base)+suffix

def target_bitrate(target_mb,duration,overhead,codec,rate=48000):
    # Leave room for packets, codec headers and rate-control variation.
    budget=math.floor((target_mb*1_000_000-overhead)*.97*8/duration/1000) if duration>0 else 0
    if codec=='mp3':
        allowed=[n for n in MP3_RATES if (n>=32 if rate>=32000 else n<=160)]
        return max((n for n in allowed if n<=budget),default=0)
    return min(320,budget)

def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__,usage='%(prog)s INPUT --output OUTPUT [options]')
    p.add_argument('input',type=Path);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--codec',choices=EXTENSIONS,default='opus')
    p.add_argument('--bitrate',type=int,default=128);p.add_argument('--target-mb',type=float)
    p.add_argument('--rate',type=int,choices=(0,8000,16000,22050,32000,44100,48000,96000),default=0)
    p.add_argument('--channels',type=int,choices=(0,1,2),default=0)
    p.add_argument('--track',type=int,default=0);p.add_argument('--start',type=float,default=0)
    p.add_argument('--duration',type=float);p.add_argument('--strip-tags',action='store_true');p.add_argument('--strip-art',action='store_true')
    return p.parse_args(argv)

def convert(args,progress=lambda stage,ratio:None):
    source=args.input.resolve();destination=Path(output_path(args.output,args.codec)).resolve()
    if source==destination:raise ValueError('Choose a different output filename to keep the original music file.')
    if not source.is_file():raise ValueError('The source file is unavailable.')
    if args.track<0 or not math.isfinite(args.start) or args.start<0:raise ValueError('Choose a valid track and start time.')
    if args.duration is not None and (not math.isfinite(args.duration) or args.duration<=0):raise ValueError('Choose a positive duration.')
    if not 8<=args.bitrate<=320:raise ValueError('Choose an audio bitrate between 8 and 320 kbps.')
    info=music_info(source)
    # A movie can also be used to extract an ordinary audio file.
    metadata=read_metadata(source) if info else Metadata()
    duration=args.duration or (max(0,info['duration']-args.start) if info else 0)
    rate=args.rate or (info or {}).get('rate') or 48000
    if args.codec=='opus':rate=48000
    if args.codec in ('mp3','aac','vorbis'):rate=min(48000,rate)
    if args.codec=='mp3' and rate not in (8000,11025,12000,16000,22050,24000,32000,44100,48000):rate=48000
    bitrate=args.bitrate
    overhead=metadata.overhead(args.codec,not args.strip_tags,not args.strip_art)
    if args.target_mb is not None:
        if not math.isfinite(args.target_mb) or args.target_mb<=0:raise ValueError('Choose a positive target file size.')
        if args.codec not in ('opus','mp3','aac','vorbis'):raise ValueError('A size target requires Opus, MP3, AAC or Vorbis. Lossless/PCM/IMA sizes follow the audio, not a bitrate target.')
        if not duration:raise ValueError('A size target requires a known duration. Set --duration for this source.')
        bitrate=target_bitrate(args.target_mb,duration,overhead,args.codec,rate)
        if bitrate<8:raise ValueError('The cover and selected duration leave too little room for audio. Increase the target size or remove album art.')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nmedia-audio-',dir=destination.parent) as temporary:
        stage=Path(temporary)/('track'+EXTENSIONS[args.codec])
        for attempt in range(3):
            codec={
                'opus':['-c:a','libopus','-b:a',f'{bitrate}k','-vbr','on'],
                'mp3':['-c:a','libmp3lame','-b:a',f'{bitrate}k'],
                'aac':['-c:a','aac','-b:a',f'{bitrate}k','-movflags','+faststart'],
                'vorbis':['-c:a','libvorbis','-b:a',f'{bitrate}k'],
                'flac':['-c:a','flac'], 'pcm':['-c:a','pcm_s16le'], 'ima':['-c:a','adpcm_ima_wav']}[args.codec]
            command=[imageio_ffmpeg.get_ffmpeg_exe(),'-nostdin','-hide_banner','-v','error','-y',
                '-ss',str(args.start),'-i',str(source),'-map',f'0:a:{args.track}','-vn','-sn','-dn',
                '-map_metadata','-1' if info or args.strip_tags else '0','-ar',str(rate),
                '-ac',str(args.channels or min(2,(info or {}).get('channels',2))),*codec]
            if args.duration is not None:command+=['-t',str(args.duration)]
            command+=['-progress','pipe:1','-nostats',str(stage)]
            progress('Encoding audio' if not attempt else 'Adjusting audio to fit',0)
            with tempfile.TemporaryFile() as errors:
                with subprocess.Popen(command,stdout=subprocess.PIPE,stderr=errors,text=True,encoding='utf8',errors='replace') as process:
                    for line in process.stdout:
                        if line.startswith('out_time_us='):
                            try:position=int(line.split('=',1)[1])/1_000_000
                            except ValueError:continue
                            progress('Encoding audio' if not attempt else 'Adjusting audio to fit',min(1,position/duration) if duration else None)
                    code=process.wait()
                if code:
                    errors.seek(0);raise RuntimeError('Audio conversion failed: '+errors.read().decode('utf8','replace')[-4000:])
            progress('Keeping tags and album art',None)
            if info or args.strip_tags or args.strip_art:write_metadata(stage,metadata,not args.strip_tags,not args.strip_art)
            size=stage.stat().st_size
            if args.target_mb is None or size<=args.target_mb*1_000_000:break
            next_rate=target_bitrate(args.target_mb,duration,overhead,args.codec,rate)
            next_rate=min(next_rate,math.floor(bitrate*args.target_mb*1_000_000/size*.95))
            if args.codec=='mp3':next_rate=max((n for n in MP3_RATES if n<=next_rate and n<bitrate),default=0)
            if next_rate<8 or attempt==2:raise ValueError('Could not fit this audio and cover within the target. Increase the size or remove album art.')
            bitrate=next_rate
        result_info=music_info(stage)
        os.replace(stage,destination)
    return dict(output_path=str(destination),bytes_written=size,encoded_duration=(result_info or {}).get('duration',duration),
                audio_codec=args.codec,bitrate=bitrate,tags=len(metadata.tags) if not args.strip_tags else 0,
                covers=len(metadata.pictures) if not args.strip_art else 0)

def main(argv=None):
    import json
    print(json.dumps(convert(parse_args(argv)),ensure_ascii=False,indent=2))

if __name__=='__main__':main()
