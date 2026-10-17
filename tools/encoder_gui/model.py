"""GUI-independent settings, validation and exact CLI argument generation."""
from dataclasses import dataclass,field
from pathlib import Path
import math,os,re,shlex

VIDEO_CODECS = {"h264":"H.264", "mpeg4":"MPEG-4", "hevc":"H.265 / HEVC", "av1":"AV1"}
AUDIO_CODECS = {"opus":"Opus", "mp3":"MP3", "aac":"AAC", "vorbis":"Vorbis", "ima":"IMA-ADPCM", "pcm":"PCM", "flac":"FLAC"}
LANGUAGES = {"eng":"English","en":"English","spa":"Spanish","es":"Spanish","fra":"French","fre":"French","deu":"German","ger":"German","ita":"Italian","por":"Portuguese","jpn":"Japanese","kor":"Korean","zho":"Chinese","chi":"Chinese","rus":"Russian","und":"Unknown language"}

def duration_text(seconds):
    value=max(0,int(round(seconds)))
    return f"{value//3600}:{value//60%60:02d}:{value%60:02d}" if value>=3600 else f"{value//60}:{value%60:02d}"

def size_text(size):
    for unit in ("B","KB","MB","GB","TB"):
        if size<1000 or unit=="TB":return f"{size:.1f} {unit}" if unit!="B" else f"{int(size)} B"
        size/=1000

def time_value(value):
    try:
        parts=[float(x) for x in str(value).strip().split(":")]
        if not 1<=len(parts)<=3 or any(not math.isfinite(x) or x<0 for x in parts):raise ValueError()
        if len(parts)>1 and any(x>=60 for x in parts[1:]):raise ValueError()
        result=0.0
        for part in parts:result=result*60+part
        return result
    except (ValueError,TypeError):
        raise ValueError("Use seconds or a time such as 1:30 or 01:20:00.") from None

def output_path(value):
    value=str(value).strip()
    if not value:return ""
    if not value.lower().endswith(".nvp.tns"):
        value += ".tns" if value.lower().endswith(".nvp") else ".nvp.tns"
    return str(Path(value).expanduser())

def track_label(track):
    language=LANGUAGES.get(track.get("language","und"),track.get("language","und"))
    raw=track.get('details','');details=[]
    rate=re.search(r'(\d+)\s*Hz',raw)
    if rate:details.append(f"{int(rate[1])/1000:g} kHz")
    if 'stereo' in raw.lower():details.append('Stereo')
    elif 'mono' in raw.lower():details.append('Mono')
    else:
        channels=re.search(r'\b(\d+\.\d+)(?:\([^)]*\))?',raw) or re.search(r'\b(\d+)\s*ch',raw)
        if channels:details.append(channels[1]+' channels')
    bitrate=re.search(r'(\d+)\s*kb/s',raw)
    if bitrate:details.append(bitrate[1]+' kbps')
    fields=[language,track.get("title",""),track.get("codec","unknown").upper(),*details]
    return " · ".join(str(s) for s in fields if s)+( " · Default" if track.get("default") else "")

@dataclass
class EncodeOptions:
    source:str=""
    output:str=""
    codec:str="h264"
    fps:str="24"
    width:int=320
    height:int=240
    crf:float=24
    rate_mode:str="quality"
    target_mb:float=85
    two_pass:bool=True
    video_kbps:int=500
    preset:str="slow"
    stream_profile:str="fast"
    hdr:bool=False
    audio:bool=True
    audio_track:int=0
    audio_codec:str="opus"
    audio_bitrate:int=128
    audio_rate:int=48000
    audio_channels:int=2
    subtitle_mode:str="none"
    subtitle_tracks:list[int]=field(default_factory=list)
    external_subtitles:list[str]=field(default_factory=list)
    subtitle_size:float=1
    start:str="0"
    end:str=""
    crop:str=""
    active_aspect:str=""
    idr:str="auto"
    chunk_kib:int=64
    preview:bool=False

    def clip_duration(self,info):
        start=time_value(self.start)
        end=time_value(self.end) if self.end.strip() else float(info.get("duration",0))
        return max(0,end-start)

    def audio_kbps(self):
        if not self.audio:return 0
        if self.audio_codec=="pcm":return self.audio_rate*self.audio_channels*16/1000
        if self.audio_codec=="ima":return self.audio_rate*self.audio_channels*4/1000*1.02
        if self.audio_codec=="flac":return None
        return self.audio_bitrate

    def budget_kbps(self,info):
        seconds=self.clip_duration(info)
        return math.floor(self.target_mb*1_000_000*.97*8/seconds/1000-(self.audio_kbps() or 0)) if seconds>0 else 0

    def errors(self,info):
        errors={}
        try:
            if not self.source or not Path(self.source).is_file():errors["source"]="Choose a video file that is available on this computer."
            if not self.output.strip():errors["output"]="Choose where to save the encoded video."
            elif Path(output_path(self.output)).resolve()==Path(self.source).resolve():errors["output"]="Choose a different output file to keep your source video."
            elif Path(output_path(self.output)).is_dir():errors["output"]="Choose a filename, rather than a folder."
        except (OSError,ValueError):errors["output"]="Choose a valid output path."
        if not info:errors.setdefault("source","Wait for the video details to load.")
        if self.codec not in VIDEO_CODECS:errors["codec"]="Choose a supported video codec."
        try:
            if self.fps!="source" and (not math.isfinite(float(self.fps)) or not 1<=float(self.fps)<=60):raise ValueError()
            if self.fps=='source' and info and info['fps']>65:errors['fps']='Choose 60 FPS or lower for this high-frame-rate source.'
        except ValueError:errors["fps"]="Enter a frame rate between 1 and 60, or choose Source."
        if not 16<=self.width<=320 or not 16<=self.height<=240:errors["dimensions"]="Use a size between 16 × 16 and the calculator's 320 × 240 screen."
        maximum=63 if self.codec=="av1" else 51
        if not 0<=self.crf<=maximum or (self.codec=="av1" and self.crf!=int(self.crf)):errors["quality"]="Choose a valid quality value for this codec."
        try:
            start=time_value(self.start)
            seconds=self.clip_duration(info or {})
            if seconds<=0:errors["trim"]="The end time must be after the start time."
            elif info and (start>=info["duration"] or start+seconds>info["duration"]+.1):errors["trim"]="Keep the selected range within the source video."
        except ValueError as error:errors["trim"]=str(error)
        if self.audio:
            if not info or self.audio_track not in [t['ordinal'] for t in info.get('audio',[])]:errors["audio"]="Choose an audio track from this video, or turn audio off."
            if self.audio_codec not in AUDIO_CODECS or not 32<=self.audio_bitrate<=320:errors["audio"]="Choose a supported audio format and bitrate."
        if self.rate_mode=="size" and info and 'trim' not in errors:
            if self.audio_kbps() is None:errors["size"]="Choose compressed audio, PCM or IMA for a target size. FLAC's size varies."
            elif self.target_mb<=0 or self.budget_kbps(info)<16:errors["size"]="This size leaves too little room for video. Increase it or lower the audio bitrate."
        if self.subtitle_mode!="none":
            selected=[t for t in (info or {}).get('subtitles',[]) if t['ordinal'] in self.subtitle_tracks]
            if len(selected)!=len(self.subtitle_tracks):errors["subtitles"]="A selected subtitle track is no longer available."
            if self.subtitle_mode=="embedded" and any(not t['text_supported'] for t in selected):errors["subtitles"]="Image-based subtitles must be burned into the picture. Choose Burn in."
            if self.subtitle_mode=="burn" and len(selected)+len(self.external_subtitles)!=1:errors["subtitles"]="Choose one subtitle track or file to burn into the picture."
            if any(not Path(p).is_file() for p in self.external_subtitles):errors["subtitles"]="A subtitle file is unavailable. Remove it or choose it again."
        if self.idr=="byte-auto" and self.codec!="h264":errors["advanced"]="Measured keyframe spacing is available with H.264 only."
        if self.idr not in ('auto','byte-auto') and (not self.idr.isdecimal() or int(self.idr)<=0):errors["advanced"]="Use Automatic, Measured byte budget, or a positive frame count."
        if self.crop and self.active_aspect:errors["advanced"]="Use either a crop rectangle or an aspect ratio."
        if self.crop:
            parts=self.crop.split(':')
            if len(parts)!=4 or any(not p.isdecimal() for p in parts):errors["advanced"]="Enter the crop as width:height:x:y."
            elif info:
                w,h,x,y=map(int,parts)
                if not w or not h or w+x>info['width'] or h+y>info['height']:errors["advanced"]="Keep the crop rectangle within the source picture."
        return errors

    def arguments(self,info):
        errors=self.errors(info)
        if errors:raise ValueError(next(iter(errors.values())))
        args=[str(Path(self.source).resolve()),"--output",str(Path(output_path(self.output)).resolve()),
              "--codec",self.codec,"--fps",self.fps,"--max-width",str(self.width),"--max-height",str(self.height),
              "--preset",self.preset,"--stream-profile",self.stream_profile,"--idr-frames",self.idr,
              "--max-chunk-kib",str(self.chunk_kib)]
        if self.rate_mode=="size":
            args += ["--bitrate-kbps",str(self.budget_kbps(info))]
            if self.two_pass:args.append('--two-pass')
        elif self.codec=="mpeg4":args += ["--bitrate-kbps",str(self.video_kbps)]
        else:args += ["--crf",str(self.crf)]
        if self.hdr:args.append("--hdr-to-sdr")
        if self.audio:
            args += ["--audio","--audio-track",str(self.audio_track),"--audio-codec",self.audio_codec,
                     "--audio-bitrate",str(self.audio_bitrate),"--audio-rate",str(self.audio_rate),
                     "--audio-channels",str(self.audio_channels)]
        if self.subtitle_mode!="none":
            if self.subtitle_tracks:
                args += ["--subtitle","embedded"]
                for index in self.subtitle_tracks:args += ["--subtitle-track",str(index)]
            for path in self.external_subtitles:args += ["--subtitle",path]
            if self.subtitle_mode=="burn":args += ["--burn-subtitles","--burn-subtitle-size",str(self.subtitle_size)]
        if time_value(self.start):args += ["--start",str(time_value(self.start))]
        if self.end.strip():args += ["--duration",str(self.clip_duration(info))]
        if self.crop:args += ["--crop",self.crop]
        if self.active_aspect:args += ["--active-aspect",self.active_aspect]
        if self.preview:args.append("--preview-mp4")
        return args

@dataclass
class AudioOptions(EncodeOptions):
    keep_tags:bool=True
    keep_art:bool=True

    def metadata_overhead(self,info):
        # Probe records the embedded pictures separately, so disabling them
        # releases their budget immediately, before conversion starts.
        music=(info or {}).get('music',{})
        total=music.get('metadata_bytes',{}).get(self.audio_codec,8192)
        if not self.keep_tags:
            total-=sum(len(k.encode('utf8'))+sum(len(v.encode('utf8'))+32 for v in values)
                       for k,values in music.get('tags',{}).items())
        if not self.keep_art:
            art=music.get('cover_bytes',0)
            total-=art*4//3 if self.audio_codec in ('opus','vorbis') else art
        return max(8192,total)

    def budget_kbps(self,info):
        from convert_audio import target_bitrate
        return target_bitrate(self.target_mb,self.clip_duration(info),self.metadata_overhead(info),
                              self.audio_codec,self.audio_rate or info.get('music',{}).get('rate') or 48000)

    def errors(self,info):
        from convert_audio import output_path as audio_path
        errors={}
        try:
            if not self.source or not Path(self.source).is_file():errors['source']='Choose an available music or video file.'
            if not self.output.strip():errors['output']='Choose where to save the converted audio.'
            elif Path(audio_path(self.output,self.audio_codec)).resolve()==Path(self.source).resolve():errors['output']='Use a different output filename to keep your original file.'
            elif Path(audio_path(self.output,self.audio_codec)).is_dir():errors['output']='Choose a filename, rather than a folder.'
        except (OSError,ValueError):errors['output']='Choose a valid output filename.'
        if not info:errors['source']='Wait for the media details to load.'
        if self.audio_track not in [t['ordinal'] for t in (info or {}).get('audio',[])]:errors['audio']='Choose a source audio track.'
        try:
            duration=self.clip_duration(info or {})
            start=time_value(self.start)
            if duration<=0 or (info and start+duration>info['duration']+.1):errors['trim']='Keep the selected range within the source file.'
        except ValueError as error:errors['trim']=str(error)
        if self.rate_mode=='size' and info and 'trim' not in errors:
            if self.audio_codec not in ('opus','mp3','aac','vorbis'):errors['size']='Choose Opus, MP3, AAC or Vorbis for a target size. Lossless and WAV sizes depend on the recording.'
            elif self.target_mb<=0 or self.budget_kbps(info)<8:errors['size']='This target leaves too little room for audio and the cover. Increase it or turn off album art.'
        return errors

    def arguments(self,info):
        from convert_audio import output_path as audio_path
        errors=self.errors(info)
        if errors:raise ValueError(next(iter(errors.values())))
        args=[self.source,'--output',audio_path(self.output,self.audio_codec),'--codec',self.audio_codec,
              '--bitrate',str(self.audio_bitrate),'--rate',str(self.audio_rate),
              '--channels',str(self.audio_channels),'--track',str(self.audio_track),'--start',str(time_value(self.start))]
        if self.end.strip():args+=['--duration',str(self.clip_duration(info))]
        if self.rate_mode=='size':args+=['--target-mb',str(self.target_mb)]
        if not self.keep_tags:args.append('--strip-tags')
        if not self.keep_art:args.append('--strip-art')
        return args

@dataclass
class TrackEditOptions(EncodeOptions):
    track_source:str=''
    source_start:str='0'
    audio_action:str='keep'
    keep_subtitle_tracks:list[int]=field(default_factory=list)
    source_subtitle_tracks:list[int]=field(default_factory=list)
    original_info:dict|None=None

    def errors(self,info):
        errors={}
        if not self.source or not Path(self.source).is_file():errors['source']='Choose an encoded nMedia video.'
        if not self.output.strip():errors['output']='Choose where to save the edited video.'
        else:
            try:
                if Path(output_path(self.output)).resolve()==Path(self.source).resolve():errors['output']='Save under a different filename to keep the original encoded video.'
            except (OSError,ValueError):errors['output']='Choose a valid output filename.'
        if not info or not info.get('encoded'):errors['source']='Choose an encoded nMedia video.'
        original=self.original_info or {}
        if self.audio_action=='replace' or (self.source_subtitle_tracks and self.subtitle_mode!='none'):
            if not self.track_source or not original:errors['audio']='Choose the original video or a track source file.'
            elif original.get('encoded'):errors['audio']='Choose a normal video or audio file as the track source.'
        if self.audio_action=='replace' and self.audio_track not in [t['ordinal'] for t in original.get('audio',[])]:errors['audio']='Choose an audio track from the original or track source.'
        try:
            start=time_value(self.source_start)
            if self.audio_action=='replace' and original and info and original['duration']+.1<start+info['duration']:errors['audio']='The track source is shorter than this video. Check the source start time or choose another file.'
        except ValueError as error:errors['trim']=str(error)
        if self.source_subtitle_tracks and self.subtitle_mode!='none':
            selected=[t for t in original.get('subtitles',[]) if t['ordinal'] in self.source_subtitle_tracks]
            if len(selected)!=len(self.source_subtitle_tracks) or any(not t['text_supported'] for t in selected):errors['subtitles']='Only text subtitles can be added without re-encoding. Image subtitles must be burned in during conversion.'
        if self.subtitle_mode!='none' and any(not Path(path).is_file() for path in self.external_subtitles):errors['subtitles']='A selected subtitle file is unavailable.'
        return errors

    def arguments(self,info):
        errors=self.errors(info)
        if errors:raise ValueError(next(iter(errors.values())))
        args=[str(Path(self.source).resolve()),'--output',str(Path(output_path(self.output)).resolve()),'--audio',self.audio_action]
        if self.track_source:args+=['--track-source',self.track_source,'--source-start',str(time_value(self.source_start))]
        if self.audio_action=='replace':args+=['--audio-track',str(self.audio_track),'--audio-codec',self.audio_codec,'--audio-bitrate',str(self.audio_bitrate),'--audio-rate',str(self.audio_rate),'--audio-channels',str(self.audio_channels)]
        if self.keep_subtitle_tracks and self.subtitle_mode!='none':
            for index in self.keep_subtitle_tracks:args+=['--keep-subtitle-track',str(index)]
        else:args.append('--strip-subtitles')
        if self.subtitle_mode!='none':
            for index in self.source_subtitle_tracks:args+=['--source-subtitle-track',str(index)]
            for path in self.external_subtitles:args+=['--subtitle',path]
        return args

def cli_command(arguments,editor=False,audio=False):
    args=["python","tools/convert_audio.py" if audio else "tools/edit_ndless_video.py" if editor else "tools/encode_ndless_video.py",*arguments]
    if os.name=="nt":return "python "+" ".join("'"+s.replace("'","''")+"'" for s in args[1:])
    return shlex.join(args)
