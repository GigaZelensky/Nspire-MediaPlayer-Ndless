"""Translate music tags and embedded pictures between ordinary audio formats."""
from dataclasses import dataclass, field
import base64
import io
import struct
import mutagen
from mutagen import id3
from mutagen.flac import FLAC, Picture
from mutagen.mp4 import MP4, MP4Cover, MP4FreeForm

ID3_KEYS = {'TITLE':'TIT2','ARTIST':'TPE1','ALBUMARTIST':'TPE2','ALBUM':'TALB',
    'TRACKNUMBER':'TRCK','DISCNUMBER':'TPOS','DATE':'TDRC','GENRE':'TCON',
    'COMPOSER':'TCOM','LYRICIST':'TEXT','COPYRIGHT':'TCOP','BPM':'TBPM',
    'ISRC':'TSRC','GROUPING':'TIT1','CONDUCTOR':'TPE3','REMIXER':'TPE4'}
MP4_KEYS = {'TITLE':'\xa9nam','ARTIST':'\xa9ART','ALBUMARTIST':'aART','ALBUM':'\xa9alb',
    'DATE':'\xa9day','GENRE':'\xa9gen','COMPOSER':'\xa9wrt','COMMENT':'\xa9cmt',
    'LYRICS':'\xa9lyr','COPYRIGHT':'cprt','GROUPING':'\xa9grp'}
# These describe the old encoded waveform/stream, rather than the music.
STALE = ('REPLAYGAIN_', 'R128_', 'ENCODER', 'ENCODING', 'DURATION', 'LENGTH',
         'TOTALSAMPLES', 'STREAMSIZE', 'MD5', 'ITUNSMPB', 'ITUNNORM')

@dataclass
class Metadata:
    tags: dict = field(default_factory=dict)
    pictures: list = field(default_factory=list)

    def overhead(self, codec, keep_tags=True, keep_art=True):
        text = sum(len(k.encode('utf8')) + sum(len(v.encode('utf8'))+32 for v in values)
                   for k, values in self.tags.items()) if keep_tags else 0
        art = sum(len(p.write())+128 for p in self.pictures) if keep_art else 0
        if codec in ('opus','vorbis'):art = (art*4+2)//3
        return text + art + 8192

def read_metadata(path):
    result = Metadata()
    audio = mutagen.File(path)
    if audio is None:return result
    tags = audio.tags
    if isinstance(tags, id3.ID3):
        for key, name in ID3_KEYS.items():
            values = [str(t) for frame in tags.getall(name) for t in frame.text]
            if values:result.tags[key] = values
        for frame in tags.getall('COMM'):
            result.tags.setdefault('COMMENT', []).extend(str(v) for v in frame.text)
        for frame in tags.getall('USLT'):result.tags.setdefault('LYRICS', []).append(frame.text)
        for frame in tags.getall('TXXX'):result.tags.setdefault(frame.desc.upper(), []).extend(map(str, frame.text))
        for frame in tags.getall('APIC'):
            p=Picture();p.data=frame.data;p.mime=frame.mime;p.type=frame.type;p.desc=frame.desc
            result.pictures.append(p)
    elif isinstance(audio, MP4):
        tags=tags or {}
        for key, name in MP4_KEYS.items():
            if name in tags:result.tags[key]=list(map(str,tags[name]))
        for key, name in [('TRACKNUMBER','trkn'),('DISCNUMBER','disk')]:
            if tags.get(name):
                n,total=tags[name][0];result.tags[key]=[f'{n}/{total}' if total else str(n)]
        if tags.get('tmpo'):result.tags['BPM']=[str(tags['tmpo'][0])]
        for name, values in tags.items():
            if name.startswith('----:com.apple.iTunes:'):
                result.tags[name.split(':',2)[2].upper()]=[bytes(v).decode('utf8','replace') for v in values]
        for cover in tags.get('covr',[]):
            p=Picture();p.data=bytes(cover);p.mime='image/png' if cover.imageformat==MP4Cover.FORMAT_PNG else 'image/jpeg';p.type=3
            result.pictures.append(p)
    elif tags:
        for key, values in tags.items():
            key=key.upper()
            if key=='METADATA_BLOCK_PICTURE':
                result.pictures.extend(Picture(base64.b64decode(v,validate=True)) for v in values)
            elif key not in ('COVERART','COVERARTMIME'):result.tags[key]=list(map(str,values))
        if not result.pictures:
            for data in tags.get('coverart',[]):
                p=Picture();p.data=base64.b64decode(data,validate=True);p.type=3
                p.mime=(tags.get('coverartmime') or ['image/jpeg'])[0];result.pictures.append(p)
    if isinstance(audio, FLAC):result.pictures=list(audio.pictures)
    result.pictures.sort(key=lambda p:p.type!=3)
    result.tags={k:v for k,v in result.tags.items() if not k.startswith(STALE)}
    # Normalize common aliases while keeping additional textual tags.
    for old,new in [('ALBUM ARTIST','ALBUMARTIST'),('YEAR','DATE'),('DESCRIPTION','COMMENT')]:
        if old in result.tags:result.tags.setdefault(new,result.tags.pop(old))
    for key,total in [('TRACKNUMBER','TRACKTOTAL'),('DISCNUMBER','DISCTOTAL')]:
        count=result.tags.get(total) or result.tags.get('TOTALTRACKS' if key=='TRACKNUMBER' else 'TOTALDISCS')
        if key in result.tags and count and '/' not in result.tags[key][0]:result.tags[key][0]+='/'+count[0]
    return result

def write_metadata(path, metadata, keep_tags=True, keep_art=True):
    audio=mutagen.File(path)
    if audio is None:raise ValueError('The converted file could not be opened for tagging.')
    if audio.tags is None:audio.add_tags()
    tags=audio.tags;tags.clear()
    values=metadata.tags if keep_tags else {}
    pictures=metadata.pictures if keep_art else []
    if isinstance(tags,id3.ID3):
        for key, items in values.items():
            if key in ID3_KEYS:tags.add(getattr(id3,ID3_KEYS[key])(encoding=3,text=items))
            elif key=='COMMENT':tags.add(id3.COMM(encoding=3,lang='eng',desc='',text=items))
            elif key=='LYRICS':tags.add(id3.USLT(encoding=3,lang='eng',desc='',text='\n'.join(items)))
            else:tags.add(id3.TXXX(encoding=3,desc=key,text=items))
        descriptions=set()
        for i,p in enumerate(pictures):
            desc=p.desc or f'Cover {i+1}'
            if desc in descriptions:desc+=f' ({i+1})'
            descriptions.add(desc);tags.add(id3.APIC(encoding=3,mime=p.mime,type=p.type,desc=desc,data=p.data))
        audio.save(padding=lambda _:0)
    elif isinstance(audio,MP4):
        for key, items in values.items():
            if key in MP4_KEYS:tags[MP4_KEYS[key]]=items
            elif key in ('TRACKNUMBER','DISCNUMBER'):
                parts=items[0].split('/')
                try:tags['trkn' if key=='TRACKNUMBER' else 'disk']=[(int(parts[0]),int(parts[1]) if len(parts)>1 else 0)]
                except ValueError:tags['----:com.apple.iTunes:'+key]=[MP4FreeForm(v.encode('utf8')) for v in items]
            else:tags['----:com.apple.iTunes:'+key]=[MP4FreeForm(v.encode('utf8')) for v in items]
        if pictures:
            covers=[]
            for p in pictures:
                if p.mime in ('image/jpeg','image/jpg','image/png'):
                    covers.append(MP4Cover(p.data,imageformat=MP4Cover.FORMAT_PNG if p.mime=='image/png' else MP4Cover.FORMAT_JPEG))
                else:
                    from PIL import Image
                    output=io.BytesIO();Image.open(io.BytesIO(p.data)).convert('RGB').save(output,'PNG')
                    covers.append(MP4Cover(output.getvalue(),imageformat=MP4Cover.FORMAT_PNG))
            tags['covr']=covers
        audio.save(padding=lambda _:0)
    else:
        for key, items in values.items():tags[key]=items
        if isinstance(audio,FLAC):
            audio.clear_pictures()
            for p in pictures:audio.add_picture(p)
        elif pictures:tags['metadata_block_picture']=[base64.b64encode(p.write()).decode('ascii') for p in pictures]
        audio.save(padding=lambda _:0)

def music_info(path):
    """Recognize audio files without treating attached album art as video."""
    audio=mutagen.File(path)
    if audio is None or not getattr(audio.info,'length',0):return None
    metadata=read_metadata(path)
    duration=audio.info.length
    # Mutagen treats compressed WAVE block alignment like PCM and reports an
    # incorrect IMA duration. The RIFF fact chunk stores decoded sample frames.
    if isinstance(audio,mutagen.wave.WAVE):
        with open(path,'rb') as handle:
            header=handle.read(12)
            if header[:4]==b'RIFF' and header[8:]==b'WAVE':
                while True:
                    block=handle.read(8)
                    if len(block)!=8:break
                    kind,size=struct.unpack('<4sI',block)
                    if kind==b'fact' and size>=4:
                        duration=struct.unpack('<I',handle.read(4))[0]/audio.info.sample_rate;break
                    handle.seek(size+(size&1),1)
    return dict(duration=duration,rate=getattr(audio.info,'sample_rate',0),
        channels=getattr(audio.info,'channels',2),tags=metadata.tags,
        cover_bytes=sum(len(p.data) for p in metadata.pictures),covers=len(metadata.pictures),
        metadata_bytes={codec:metadata.overhead(codec) for codec in ('opus','mp3','aac','vorbis','flac','pcm','ima')})
