"""Edit selectable tracks by copying encoded NVP video chunks unchanged."""
from dataclasses import dataclass,replace
from pathlib import Path
import hashlib,math,struct,time

HEADER=struct.Struct('<4s12H5I')
LARGE_HEADER=struct.Struct('<4s12H3I2Q')
INDEX=struct.Struct('<6I')
LARGE_INDEX=struct.Struct('<Q5I')
CUE=struct.Struct('<IIHBBHHHHH')
AUDIO=struct.Struct('<8sQQQIIHHI')
CODECS=('h264','mpeg4','hevc','av1')
AUDIO_CODECS=('pcm','ima','mp3','flac','vorbis','aac','opus')

def exact(handle,count):
    data=handle.read(count)
    if len(data)!=count:raise ValueError('The encoded video is truncated.')
    return data

@dataclass
class NvpFile:
    path:Path
    header:list
    chunks:list
    subtitles:list
    audio:tuple|None
    video_bytes:int
    @property
    def duration(self):return self.header[13]*self.header[10]/self.header[9]
    @property
    def codec(self):return CODECS[0 if self.header[1]==10 else self.header[2]&15]

    @classmethod
    def read(cls,path):
        from encode_ndless_video import SubtitleCue,SubtitleTrack
        path=Path(path);size=path.stat().st_size
        with path.open('rb') as source:
            start=exact(source,8)
            if start[:4]!=b'NVP1':raise ValueError('Choose an unencrypted .nvp.tns video. Encrypted videos cannot be edited directly.')
            version=int.from_bytes(start[4:6],'little')
            if version not in (10,11,12,13):raise ValueError('This NVP version is not supported by the track editor. Re-encode the original source with the current encoder.')
            wire=LARGE_HEADER if version>=12 else HEADER
            source.seek(0);header=list(wire.unpack(exact(source,wire.size)))
            codec=0 if version==10 else header[2]&15
            if codec>=len(CODECS) or not header[9] or not header[10] or not header[13] or not 0<header[14]<=250_000:
                raise ValueError('The encoded video has an invalid header.')
            if header[13]*1000*header[10]//header[9]>0xffffffff:raise ValueError('The video timeline exceeds the player format limit.')
            if not (0<header[7]<=header[3] and 0<header[8]<=header[4] and header[5]+header[7]<=header[3] and header[6]+header[8]<=header[4]):
                raise ValueError('The encoded video has invalid picture dimensions.')
            audio=None;video_bytes=size
            if version==13:
                source.seek(size-AUDIO.size);audio=AUDIO.unpack(exact(source,AUDIO.size))
                magic,video_bytes,offset,length,rate,samples,channels,audio_codec,reserved=audio
                expected=(header[13]*header[10]*rate+header[9]//2)//header[9]
                if magic!=b'NVPAUD13' or offset!=video_bytes or offset+length+AUDIO.size!=size or length<12 or reserved or audio_codec>6 or not 8000<=rate<=96000 or channels not in (1,2) or samples!=expected:
                    raise ValueError('The encoded video has an invalid audio section.')
            index=LARGE_INDEX if version>=12 else INDEX
            if not wire.size<=header[16]<=video_bytes or header[14]*index.size>video_bytes-header[16]:raise ValueError('The encoded video has an invalid chunk index.')
            source.seek(header[16]);chunks=[];frame=0;previous=wire.size
            for _ in range(header[14]):
                entry=index.unpack(exact(source,index.size));offset,packed,unpacked,first,count,table=entry
                if offset<previous or offset+packed>header[16] or packed!=unpacked or not count or first!=frame or count>header[13]-frame or table>packed or packed-table<5+4*count:
                    raise ValueError('The encoded video has an invalid chunk entry.')
                chunks.append(entry);frame+=count;previous=offset+packed
            if frame!=header[13]:raise ValueError('The encoded frame count does not match its index.')
            subtitles=[]
            if header[15]:
                begin=header[17]
                if begin<header[16]+len(chunks)*index.size or not 0<video_bytes-begin<=64*1024*1024:raise ValueError('The subtitle section is invalid or too large to edit.')
                source.seek(begin);count=int.from_bytes(exact(source,2),'little')
                if not 0<count<=64 or header[15]>1_000_000:raise ValueError('The subtitle track count is invalid.')
                descriptions=[]
                for _ in range(count):
                    length,cues=struct.unpack('<HI',exact(source,6))
                    if cues>header[15] or source.tell()+length>video_bytes:raise ValueError('A subtitle track extends beyond the file.')
                    descriptions.append((exact(source,length).decode('ascii','replace'),cues))
                if sum(c for _,c in descriptions)!=header[15]:raise ValueError('The subtitle cue count does not match its tracks.')
                for name,count in descriptions:
                    cues=[]
                    for _ in range(count):
                        if source.tell()+CUE.size>video_bytes:raise ValueError('A subtitle cue is truncated.')
                        start,end,length,position,align,x,y,ml,mr,mv=CUE.unpack(exact(source,CUE.size))
                        if end<start or source.tell()+length>video_bytes:raise ValueError('A subtitle cue is invalid.')
                        text=exact(source,length).decode('ascii','replace')
                        cues.append(SubtitleCue(start,end,text,position,align,x,y,ml,mr,mv))
                    subtitles.append(SubtitleTrack(name,cues))
                if source.tell()!=video_bytes:raise ValueError('Unexpected data follows the selectable subtitles.')
            elif header[17] and not header[16]+len(chunks)*index.size<=header[17]<=video_bytes:
                raise ValueError('The subtitle offset is invalid.')
        return cls(path,header,chunks,subtitles,audio,video_bytes)

    def info(self):
        audio=[]
        if self.audio:
            audio=[{'ordinal':0,'language':'und','title':'Current soundtrack','codec':AUDIO_CODECS[self.audio[7]],
                'details':f'{self.audio[4]} Hz / {self.audio[6]} ch','default':True}]
        return {'path':str(self.path.resolve()),'bytes':self.path.stat().st_size,'encoded':True,
            'width':self.header[7],'height':self.header[8],'display_width':self.header[7],'display_height':self.header[8],
            'fps':self.header[9]/self.header[10],'duration':self.duration,'video_codec':self.codec.upper(),'hdr':False,
            'audio':audio,'subtitles':[{'ordinal':i,'title':t.name,'language':'','codec':'text','text_supported':True,'existing':True} for i,t in enumerate(self.subtitles)]}

    def video_hash(self):
        digest=hashlib.sha256()
        with self.path.open('rb') as source:
            for offset,size,*_ in self.chunks:
                source.seek(offset);remaining=size
                while remaining:
                    data=exact(source,min(remaining,1024*1024));digest.update(data);remaining-=len(data)
        return digest.hexdigest()

def retime_tracks(tracks,start,duration):
    begin=round(start*1000);end=begin+round(duration*1000);result=[]
    for track in tracks:
        cues=[replace(cue,start_ms=max(begin,cue.start_ms)-begin,end_ms=min(end,cue.end_ms)-begin)
            for cue in track.cues if cue.end_ms>begin and cue.start_ms<end]
        if cues:result.append(replace(track,cues=cues))
    return result

def copy_range(source,output,offset,length,progress=None):
    source.seek(offset);copied=0;last=0
    while copied<length:
        data=exact(source,min(1024*1024,length-copied));output.write(data);copied+=len(data)
        if progress and (copied==length or time.monotonic()-last>.15):progress(copied/length);last=time.monotonic()

def write_subtitles(output,tracks):
    from encode_ndless_video import sanitize_subtitle_text
    if not tracks:return 0
    output.write(struct.pack('<H',len(tracks)))
    for track in tracks:
        name=sanitize_subtitle_text(track.name).encode('ascii','replace')
        if len(name)>65535:raise ValueError('A subtitle track name is too long.')
        output.write(struct.pack('<HI',len(name),len(track.cues)));output.write(name)
    for track in tracks:
        for cue in track.cues:
            text=sanitize_subtitle_text(cue.text).encode('ascii','replace')
            if len(text)>65535:raise ValueError('A subtitle cue is too long.')
            output.write(CUE.pack(cue.start_ms,cue.end_ms,len(text),cue.position_mode,cue.align,cue.pos_x,cue.pos_y,cue.margin_l,cue.margin_r,cue.margin_v));output.write(text)
    return sum(len(track.cues) for track in tracks)

def edit_tracks(source_path,output_path,*,audio_action='keep',track_source='',source_start=0,
                audio_track=0,audio_codec='opus',audio_rate=48000,audio_channels=2,audio_bitrate=128,
                keep_subtitle_tracks=None,source_subtitle_tracks=(),external_subtitles=(),progress=None):
    from encode_ndless_video import load_subtitle_tracks
    from audio_track import prepare_audio
    source=NvpFile.read(source_path);output_path=Path(output_path)
    if source.path.resolve()==output_path.resolve():raise ValueError('Save the edited video under a different filename.')
    if audio_action not in ('keep','remove','replace') or not math.isfinite(source_start) or source_start<0:raise ValueError('Invalid track editing settings.')
    if (audio_action=='replace' or source_subtitle_tracks) and (not track_source or not Path(track_source).is_file()):raise ValueError('Choose the original video or a track source file.')
    keep=list(range(len(source.subtitles))) if keep_subtitle_tracks is None else list(keep_subtitle_tracks)
    if len(set(keep))!=len(keep) or any(i<0 or i>=len(source.subtitles) for i in keep):raise ValueError('A selected current subtitle track is unavailable.')
    tracks=[source.subtitles[i] for i in keep]
    if source_subtitle_tracks:
        added=load_subtitle_tracks(Path(track_source),output_path,['embedded'],list(source_subtitle_tracks))
        tracks.extend(retime_tracks(added,source_start,source.duration))
    if external_subtitles:
        added=load_subtitle_tracks(source.path,output_path,list(external_subtitles),None)
        tracks.extend(retime_tracks(added,0,source.duration))
    prepared=None
    if audio_action=='replace':
        if progress:progress('Encoding replacement audio',None)
        prepared=prepare_audio(Path(track_source),frames=source.header[13],fps_num=source.header[9],fps_den=source.header[10],start=source_start,
            track=audio_track,codec=audio_codec,rate=audio_rate,channels=audio_channels,bitrate=audio_bitrate)
    try:
        header=source.header[:];header[1]=13 if prepared or (audio_action=='keep' and source.audio) else 12
        if source.header[1]==10:header[2]=0
        with source.path.open('rb') as incoming,output_path.open('wb') as outgoing:
            outgoing.write(b'\0'*LARGE_HEADER.size);index=[];total=sum(c[1] for c in source.chunks);copied=0;last_progress=0
            for entry in source.chunks:
                index.append((outgoing.tell(),*entry[1:]))
                copy_range(incoming,outgoing,entry[0],entry[1])
                copied+=entry[1]
                if progress and (copied==total or time.monotonic()-last_progress>.1):progress('Copying video unchanged',copied/total);last_progress=time.monotonic()
            header[16]=outgoing.tell()
            for entry in index:outgoing.write(LARGE_INDEX.pack(*entry))
            header[17]=outgoing.tell() if tracks else 0;header[15]=write_subtitles(outgoing,tracks)
            if prepared:prepared.append(outgoing,outgoing.tell())
            elif audio_action=='keep' and source.audio:
                before=outgoing.tell();audio=source.audio
                copy_range(incoming,outgoing,audio[2],audio[3],lambda ratio:progress('Copying current audio',ratio) if progress else None)
                outgoing.write(AUDIO.pack(audio[0],before,before,*audio[3:]))
            outgoing.seek(0);outgoing.write(LARGE_HEADER.pack(*header))
        check=NvpFile.read(output_path)
        return {'output_path':str(output_path),'bytes_written':output_path.stat().st_size,'encoded_duration':source.duration,
            'width':header[7],'height':header[8],'fps':header[9]/header[10],'video_copied':True,
            'audio':bool(check.audio),'subtitle_tracks':len(check.subtitles)}
    finally:
        if prepared:prepared.close()
