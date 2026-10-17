"""Compare the real C IMA decoder against FFmpeg's WAV IMA reconstruction."""
from pathlib import Path
import hashlib
import json
import math
import random
import struct
import subprocess
import imageio_ffmpeg
from audio_track import FOOTER

def check(work,decoder,base_movie):
    work.mkdir(parents=True,exist_ok=True)
    data=base_movie.read_bytes()
    header=struct.unpack_from('<4s12H3I2Q',data)
    footer=FOOTER.unpack(data[-48:]);video=data[:footer[1]]
    assert header[1]==13 and footer[0]==b'NVPAUD13'
    ffmpeg=imageio_ffmpeg.get_ffmpeg_exe();results=[]
    for rate,channels,block in ((44100,1,256),(44100,1,1024),(44100,2,1024),
                               (48000,2,4096),(22050,1,2048)):
        name=f'{rate}-{channels}-{block}'
        samples=(header[13]*header[10]*rate+header[9]//2)//header[9]
        rng=random.Random(1729);pcm=bytearray()
        for frame in range(samples):
            for channel in range(channels):
                mode=(frame//(rate//4))%4
                value=(int(7*math.sin(frame*0.37)+channel*3) if mode==0 else
                       int(19000*math.sin(frame*(0.13+channel*0.031))) if mode==1 else
                       rng.randrange(-32768,32768) if mode==2 else
                       32767 if (frame//7+channel)%2 else -32768)
                pcm.extend(struct.pack('<h',value))
        raw=work/(name+'.s16');raw.write_bytes(pcm)
        wav=work/(name+'.wav');reference=work/(name+'.reference.s16')
        subprocess.run([ffmpeg,'-nostdin','-y','-v','error','-f','s16le','-ar',str(rate),
            '-ac',str(channels),'-i',str(raw),'-c:a','adpcm_ima_wav','-block_size',str(block),str(wav)],check=True)
        audio=wav.read_bytes();movie=work/(name+'.nvp.tns')
        movie.write_bytes(video+audio+FOOTER.pack(b'NVPAUD13',len(video),len(video),len(audio),rate,samples,channels,1,0))
        actual=work/(name+'.player.s16')
        subprocess.run([str(decoder),str(movie),str(actual)],check=True)
        subprocess.run([ffmpeg,'-nostdin','-y','-v','error','-i',str(wav),'-f','s16le',str(reference)],check=True)
        result=actual.read_bytes();expected=reference.read_bytes()[:samples*channels*2]
        assert result==expected,f'IMA reconstruction mismatch: {name}'
        results.append(dict(rate=rate,channels=channels,block_bytes=block,samples=samples,
                            bytes=len(result),sha256=hashlib.sha256(result).hexdigest()))
    (work/'results.json').write_text(json.dumps(dict(ffmpeg_version=imageio_ffmpeg.get_ffmpeg_version(),cases=results),indent=2)+'\n')
    print('PASS independent WAV IMA reference: five mono/stereo/rate/block cases, quiet/tonal/noisy/clipped samples')
