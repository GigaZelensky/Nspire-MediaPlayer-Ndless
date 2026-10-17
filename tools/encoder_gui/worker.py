"""Own encoder processes and their children; never copy a source into RAM."""
from pathlib import Path
from dataclasses import asdict
from contextlib import redirect_stdout,redirect_stderr
import io,json,multiprocessing,os,re,shutil,subprocess,traceback,uuid

def send(pipe,kind,**payload):
    try:pipe.send({"kind":kind,**payload})
    except (BrokenPipeError,EOFError,OSError):pass

class LineWriter(io.TextIOBase):
    def __init__(self,pipe):self.pipe=pipe;self.pending=""
    def write(self,text):
        self.pending+=text
        while '\n' in self.pending:
            line,self.pending=self.pending.split('\n',1)
            if line.strip():
                send(self.pipe,"log",text=line)
                if line.startswith("Chunk "):
                    match=re.search(r':\s*([\d.]+)%',line)
                    if match:send(self.pipe,"progress",stage="Writing calculator video",ratio=float(match[1])/100)
        return len(text)
    def flush(self):
        if self.pending:send(self.pipe,"log",text=self.pending);self.pending=""

def configure_subprocesses():
    """Suppress child consoles; keep real pipes and native filesystem access."""
    if os.name!="nt":return
    original=subprocess.Popen
    class QuietPopen(original):
        def __init__(self,*args,**kwargs):
            kwargs['creationflags']=kwargs.get('creationflags',0)|subprocess.CREATE_NO_WINDOW
            super().__init__(*args,**kwargs)
    subprocess.Popen=QuietPopen

def probe(path,pipe,allow_audio=False):
    import encode_ndless_video as encoder
    from media_tracks import probe_audio_tracks
    source=Path(path)
    if not source.is_file():raise ValueError("This video is no longer available. Choose it again.")
    with source.open('rb') as handle:magic=handle.read(4)
    if magic==b'NVP1' or str(source).lower().endswith('.nvp.tns'):
        from nvp_tracks import NvpFile
        encoded=NvpFile.read(source);send(pipe,'source',info=encoded.info())
        first=encoded.chunks[0]
        if first[1]<=2*1024*1024:
            import struct
            with source.open('rb') as handle:handle.seek(first[0]);blob=handle.read(first[1])
            table=first[5];length=struct.unpack_from('<I',blob,table)[0];begin=struct.unpack_from('<I',blob,table+4)[0]
            end=struct.unpack_from('<I',blob,table+8)[0] if first[4]>1 else length
            payload=blob[table+4+4*first[4]+begin:table+4+4*first[4]+end]
            try:
                result=subprocess.run([encoder.imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-f',{'h264':'h264','hevc':'hevc','mpeg4':'m4v','av1':'obu'}[encoded.codec],'-i','pipe:0','-frames:v','1','-f','image2pipe','-vcodec','png','-'],input=payload,capture_output=True,timeout=15)
                if result.returncode==0 and len(result.stdout)<2_000_000:send(pipe,'preview',image=result.stdout)
            except subprocess.TimeoutExpired:pass
        send(pipe,'done',result={'probed':True});return
    try:video=encoder.probe_video(source)
    except RuntimeError:
        if not allow_audio:raise
        from types import SimpleNamespace
        result=subprocess.run([encoder.imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-i',str(source)],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=45)
        match=re.search(r'Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)',result.stderr)
        if not match:raise ValueError('Could not read the track source duration.')
        duration=int(match[1])*3600+int(match[2])*60+float(match[3])
        video=SimpleNamespace(storage_width=0,storage_height=0,display_width=0,display_height=0,fps=0,duration=duration)
    details={"path":str(source.resolve()),"bytes":source.stat().st_size,
        "width":video.storage_width,"height":video.storage_height,
        "display_width":video.display_width,"display_height":video.display_height,
        "fps":video.fps,"duration":video.duration,"video_codec":"", "hdr":False,
        "audio":[asdict(t) for t in probe_audio_tracks(source,encoder.ffprobe_path())],
        "subtitles":[asdict(t) for t in encoder.probe_embedded_subtitle_tracks(source)]}
    # Normalize the existing encoder's subtitle track descriptors for the UI.
    details['subtitles']=[{"ordinal":t['ordinal'],"title":t['name'],"codec":t['codec_name'],
        "language":"", "text_supported":t['text_supported']} for t in details['subtitles']]
    ffprobe=encoder.ffprobe_path()
    if ffprobe:
        result=subprocess.run([str(ffprobe),'-v','error','-select_streams','v:0','-show_entries',
            'stream=codec_name,color_transfer,color_primaries','-of','json',str(source)],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=45)
        if result.returncode==0:
            streams=json.loads(result.stdout).get('streams',[])
            if streams:
                details['video_codec']=streams[0].get('codec_name','').upper()
                details['hdr']=streams[0].get('color_transfer') in ('smpte2084','arib-std-b67')
    send(pipe,"source",info=details)
    if not video.storage_width:send(pipe,'done',result={'probed':True});return
    # A small still is the only source image sent back to the interface.
    seek=min(30,max(.1,video.duration*.1))
    filters=list(encoder.HDR_TO_SDR_FILTERS) if details['hdr'] else []
    filters.append('scale=640:480:force_original_aspect_ratio=decrease')
    try:
        result=subprocess.run([encoder.imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-ss',str(seek),'-i',str(source),
            '-frames:v','1','-vf',','.join(filters),'-f','image2pipe','-vcodec','png','-'],capture_output=True,timeout=60)
        if result.returncode==0 and len(result.stdout)<2_000_000:send(pipe,"preview",image=result.stdout)
    except subprocess.TimeoutExpired:send(pipe,"log",text="The framing preview was skipped because seeking took too long. Encoding is still available.")
    send(pipe,"done",result={"probed":True})

def encode(request,pipe):
    import tempfile
    staging=Path(request['staging'])
    scratch=staging/'scratch';scratch.mkdir()
    # A forced cancellation cannot run Python finalizers. Keep every encoder
    # temporary (including audio and pass logs) under the owned job folder.
    tempfile.tempdir=str(scratch)
    os.environ['TMP']=os.environ['TEMP']=os.environ['TMPDIR']=str(scratch)
    import encode_ndless_video as encoder
    import audio_track
    arguments=request['arguments'][:]
    destination=Path(request['output'])
    stage_file=staging/destination.name
    arguments[arguments.index('--output')+1]=str(stage_file)
    original_progress=encoder.log_ffmpeg_progress
    def progress(**kwargs):
        original_progress(**kwargs)
        out_time=encoder.parse_ffmpeg_time(kwargs['progress_state'].get('out_time','0:0:0'))
        duration=kwargs.get('total_duration') or 0
        send(pipe,"progress",stage=kwargs['label'].replace('FFmpeg','Encoding video').strip(),
            ratio=max(0,min(1,out_time/duration)) if duration else None,
            speed=kwargs['progress_state'].get('speed',''),media_time=out_time)
    encoder.log_ffmpeg_progress=progress
    original_audio=audio_track.prepare_audio
    def prepare_audio(*args,**kwargs):
        send(pipe,"progress",stage="Encoding audio",ratio=None)
        return original_audio(*args,**kwargs)
    audio_track.prepare_audio=prepare_audio
    send(pipe,"progress",stage="Preparing track changes" if request['task']=='edit' else "Preparing video and subtitles",ratio=None)
    if request['task']=='edit':
        from edit_ndless_video import parse_args,settings
        from nvp_tracks import edit_tracks
        args=parse_args(arguments)
        stats=edit_tracks(args.input,stage_file,**settings(args),progress=lambda stage,ratio:send(pipe,'progress',stage=stage,ratio=ratio))
    else:stats=asdict(encoder.encode(encoder.parse_args(arguments)))
    send(pipe,"progress",stage="Saving encoded video",ratio=None)
    # Encoding happens beside the destination. Existing output survives every
    # failure/cancellation; a completed primary file is installed by rename.
    stage_json=stage_file.with_suffix('.json')
    if stage_json.exists():
        data=json.loads(stage_json.read_text())
        data['output_path']=str(destination)
        stage_json.write_text(json.dumps(data,indent=2),encoding='utf8')
        os.replace(stage_json,destination.with_suffix('.json'))
    preview=encoder.preview_mp4_path_for_output(stage_file)
    if preview.exists():os.replace(preview,encoder.preview_mp4_path_for_output(destination))
    os.replace(stage_file,destination)
    stats['output_path']=str(destination)
    send(pipe,"done",result=stats)

def worker_main(pipe,gate,request):
    if os.name!='nt':os.setsid()
    if not gate.wait(15):return
    configure_subprocesses()
    writer=LineWriter(pipe)
    try:
        with redirect_stdout(writer),redirect_stderr(writer):
            if request['task']=='probe':probe(request['path'],pipe,request.get('allow_audio',False))
            else:encode(request,pipe)
    except BaseException as error:
        writer.flush()
        send(pipe,"error",message=str(error) or type(error).__name__,detail=traceback.format_exc())
    finally:
        writer.flush();pipe.close()

class WindowsJob:
    def __init__(self,pid):
        self.handle=None
        if os.name!='nt':return
        import ctypes
        from ctypes import wintypes as w
        class IO(ctypes.Structure):_fields_=[(n,ctypes.c_ulonglong) for n in ('read_ops','write_ops','other_ops','read_bytes','write_bytes','other_bytes')]
        class Limits(ctypes.Structure):
            _fields_=[('process_time',ctypes.c_longlong),('job_time',ctypes.c_longlong),('flags',w.DWORD),
                      ('minimum',ctypes.c_size_t),('maximum',ctypes.c_size_t),('active',w.DWORD),
                      ('affinity',ctypes.c_size_t),('priority',w.DWORD),('scheduling',w.DWORD)]
        class Extended(ctypes.Structure):
            _fields_=[('basic',Limits),('io',IO),('process_memory',ctypes.c_size_t),('job_memory',ctypes.c_size_t),
                      ('peak_process',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.CreateJobObjectW.argtypes=[ctypes.c_void_p,w.LPCWSTR];kernel.CreateJobObjectW.restype=w.HANDLE
        kernel.SetInformationJobObject.argtypes=[w.HANDLE,ctypes.c_int,ctypes.c_void_p,w.DWORD];kernel.SetInformationJobObject.restype=w.BOOL
        kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
        kernel.AssignProcessToJobObject.argtypes=[w.HANDLE,w.HANDLE];kernel.AssignProcessToJobObject.restype=w.BOOL
        kernel.CloseHandle.argtypes=[w.HANDLE];kernel.CloseHandle.restype=w.BOOL
        job=kernel.CreateJobObjectW(None,None)
        process=kernel.OpenProcess(0x0100|0x0001,False,pid)
        limits=Extended();limits.basic.flags=0x2000  # KILL_ON_JOB_CLOSE
        if job and process and kernel.SetInformationJobObject(job,9,ctypes.byref(limits),ctypes.sizeof(limits)) and kernel.AssignProcessToJobObject(job,process):
            self.handle=job;self.kernel=kernel
        elif job:kernel.CloseHandle(job)
        if process:kernel.CloseHandle(process)
    def close(self):
        if self.handle:self.kernel.CloseHandle(self.handle);self.handle=None

def remove_staging(path,output):
    if not path:return
    stage=Path(path).resolve();parent=Path(output).resolve().parent
    if stage.parent!=parent or not re.fullmatch(r'\.nmedia-[0-9a-f]{32}',stage.name):
        raise ValueError("Refusing to remove an unexpected work directory")
    if stage.exists():shutil.rmtree(stage)

class Task:
    def __init__(self,request):
        self.request=request;self.cancelled=False;self.result=None;self.job=None
        context=multiprocessing.get_context('spawn')
        self.pipe,child=context.Pipe();self.gate=context.Event()
        self.process=context.Process(target=worker_main,args=(child,self.gate,request),daemon=False)
        self.process.start();child.close()
        self.job=WindowsJob(self.process.pid)
        self.gate.set()
    def messages(self):
        result=[]
        try:
            for _ in range(100):
                if not self.pipe.poll():break
                event=self.pipe.recv();result.append(event)
                if event['kind']=='done':self.result=event['result']
        except (EOFError,OSError):pass
        return result
    def cancel(self):
        self.cancelled=True
        if self.process.is_alive():
            if self.job and self.job.handle:self.job.close()
            else:
                import psutil
                try:
                    parent=psutil.Process(self.process.pid)
                    children=parent.children(recursive=True)
                    for child in reversed(children):
                        try:child.kill()
                        except psutil.NoSuchProcess:pass
                    parent.kill()
                except psutil.NoSuchProcess:pass
    def close(self):
        if self.process.is_alive():return False
        self.process.join()
        if self.job:self.job.close()
        self.pipe.close()
        remove_staging(self.request.get('staging'),self.request.get('output',''))
        return True

def encode_task(arguments,edit=False):
    destination=Path(arguments[arguments.index('--output')+1]).resolve()
    destination.parent.mkdir(parents=True,exist_ok=True)
    stage=destination.parent/('.nmedia-'+uuid.uuid4().hex)
    stage.mkdir()
    try:return Task({'task':'edit' if edit else 'encode','arguments':arguments,'output':str(destination),'staging':str(stage)})
    except BaseException:
        remove_staging(stage,destination);raise
