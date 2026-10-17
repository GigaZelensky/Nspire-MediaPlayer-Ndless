"""Check language/title discovery and selecting the actual second soundtrack."""
from pathlib import Path
import json,subprocess,sys,tempfile
import imageio_ffmpeg,numpy as np
from audio_track import FOOTER
from media_tracks import probe_audio_tracks,select_audio_track

ROOT=Path(__file__).resolve().parents[1]

def main():
    ffmpeg=imageio_ffmpeg.get_ffmpeg_exe()
    with tempfile.TemporaryDirectory(prefix='nmedia-tracks-') as temporary:
        work=Path(temporary);source=work/'two.mkv'
        subprocess.run([ffmpeg,'-nostdin','-v','error','-f','lavfi','-i','color=s=32x32:r=12:d=1',
            '-f','lavfi','-i','sine=frequency=440:duration=1',
            '-f','lavfi','-i','sine=frequency=880:duration=1',
            '-map','0:v','-map','1:a','-map','2:a','-c:v','mpeg4','-c:a','pcm_s16le',
            '-metadata:s:a:0','language=eng','-metadata:s:a:0','title=English',
            '-metadata:s:a:1','language=spa','-metadata:s:a:1','title=Spanish',str(source)],check=True)
        tracks=probe_audio_tracks(source)
        assert len(tracks)==2 and tracks[0].language=='eng' and tracks[1].language=='spa',tracks
        assert tracks[0].title=='English' and tracks[1].title=='Spanish'
        assert select_audio_track(tracks,'1')==1
        for choice in ('2','-1'):
            try:select_audio_track(tracks,choice)
            except ValueError:pass
            else:raise AssertionError('Invalid track accepted')
        try:select_audio_track(tracks,'ask',interactive=False)
        except ValueError:pass
        else:raise AssertionError('Noninteractive ask must explain the explicit index')
        command=[sys.executable,str(ROOT/'tools/encode_ndless_video.py'),str(source)]
        listing=subprocess.check_output([*command,'--list-audio-tracks'],text=True)
        assert 'English' in listing and 'Spanish' in listing and '1:' in listing
        output=work/'selected.nvp.tns'
        subprocess.run([*command,'--output',str(output),'--codec','mpeg4','--fps','12','--audio',
            '--audio-codec','pcm','--audio-track','1','--quiet'],check=True)
        raw=output.read_bytes();footer=FOOTER.unpack(raw[-48:])
        audio=work/'selected.wav';audio.write_bytes(raw[footer[2]:footer[2]+footer[3]])
        pcm=subprocess.check_output([ffmpeg,'-nostdin','-v','error','-i',str(audio),'-ac','1','-ar','44100','-f','s16le','-'])
        samples=np.frombuffer(pcm,dtype='<i2');frequencies=np.abs(np.fft.rfft(samples.astype(float)))
        peak=int(np.argmax(frequencies))*44100/len(samples)
        assert abs(peak-880)<2,peak
        assert json.loads(output.with_suffix('.json').read_text())['audio']['source_track']==1
    print('PASS language/title discovery, listing, invalid selection, noninteractive behavior and second-track PCM contents')

if __name__=='__main__':main()
