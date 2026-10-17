#!/usr/bin/env python3
"""Add, replace or remove NVP tracks without re-encoding the video."""
from pathlib import Path
import argparse,json,os,tempfile
from nvp_tracks import edit_tracks

def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__,usage='%(prog)s INPUT --output OUTPUT [options]')
    p.add_argument('input');p.add_argument('--output',required=True)
    p.add_argument('--audio',choices=('keep','remove','replace'),default='keep')
    p.add_argument('--track-source',default='');p.add_argument('--source-start',type=float,default=0)
    p.add_argument('--audio-track',type=int,default=0);p.add_argument('--audio-codec',choices=('pcm','ima','mp3','flac','vorbis','aac','opus'),default='opus')
    p.add_argument('--audio-bitrate',type=int,default=128);p.add_argument('--audio-rate',type=int,default=48000);p.add_argument('--audio-channels',type=int,choices=(1,2),default=2)
    p.add_argument('--keep-subtitle-track',type=int,action='append');p.add_argument('--strip-subtitles',action='store_true',help='Remove existing selectable subtitles; new subtitle sources can still be added')
    p.add_argument('--source-subtitle-track',type=int,action='append',default=[]);p.add_argument('--subtitle',action='append',default=[])
    return p.parse_args(argv)

def settings(args):
    return dict(audio_action=args.audio,track_source=args.track_source,source_start=args.source_start,
        audio_track=args.audio_track,audio_codec=args.audio_codec,audio_rate=args.audio_rate,audio_channels=args.audio_channels,audio_bitrate=args.audio_bitrate,
        keep_subtitle_tracks=[] if args.strip_subtitles else args.keep_subtitle_track,
        source_subtitle_tracks=args.source_subtitle_track,external_subtitles=args.subtitle)

def main(argv=None):
    args=parse_args(argv);destination=Path(args.output).resolve()
    if destination==Path(args.input).resolve():raise ValueError('Choose a different output filename to preserve the original.')
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nmedia-tracks-',dir=destination.parent) as work:
        staged=Path(work)/destination.name
        result=edit_tracks(args.input,staged,**settings(args),progress=lambda label,ratio:print(label+(f' {ratio*100:.0f}%' if ratio is not None else ''),flush=True))
        os.replace(staged,destination)
    print('Saved',destination);return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError) as error:raise SystemExit(str(error))
