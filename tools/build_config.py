"""Describe the exact feature selection used by the native Makefile."""
import argparse
import json
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('flavor','video','audio','usb','artwork','visualizations','video_modules','audio_modules','flags','sources'):
        p.add_argument('--'+key.replace('_','-'),required=True)
    p.add_argument('--output',type=Path)
    a=p.parse_args()
    result={'product':'nMedia Player','flavor':a.flavor,'video_codecs':a.video.split(),
        'audio_codecs':a.audio.split(),'usb_storage':a.usb=='1','artwork':a.artwork=='1',
        'visualizations':a.visualizations=='1','video_modules':a.video_modules=='1',
        'audio_modules':a.audio_modules=='1','compiler_flags':a.flags.split(),
        'sources':a.sources.split(),'automatic_healthy_logs':False}
    text=json.dumps(result,indent=2)+'\n'
    if a.output:
        a.output.parent.mkdir(parents=True,exist_ok=True)
        if not a.output.exists() or a.output.read_text()!=text:a.output.write_text(text)
    else:print(text,end='')

if __name__=='__main__':main()
