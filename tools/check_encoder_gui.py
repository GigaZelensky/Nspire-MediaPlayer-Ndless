#!/usr/bin/env python3
"""Exercise the real desktop encoder, tracks, output transactions and cancellation."""
from pathlib import Path
import argparse,json,multiprocessing,os,struct,subprocess,sys,tempfile,time
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
from encoder_gui.model import EncodeOptions,output_path,time_value
from encoder_gui.worker import Task,encode_task
import imageio_ffmpeg

def wait(task,seconds=90):
    events=[];deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        events.extend(task.messages())
        if not task.process.is_alive():
            events.extend(task.messages());result=task.result;task.close();return result,events
        time.sleep(.02)
    task.cancel();task.process.join(5);task.close();raise AssertionError('Worker timed out')

def source_fixture(work):
    for name,text in [('english','Hello from the first track.'),('spanish','Hola desde la segunda pista.')]:
        (work/(name+'.srt')).write_text('1\n00:00:00,000 --> 00:00:02,500\n'+text+'\n',encoding='utf8')
    source=work/'A video é $ [test].mkv'
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-y',
        '-f','lavfi','-i','testsrc2=size=384x216:rate=24','-f','lavfi','-i','sine=frequency=440:sample_rate=48000',
        '-f','lavfi','-i','sine=frequency=880:sample_rate=48000','-i',str(work/'english.srt'),'-i',str(work/'spanish.srt'),
        '-t','3','-map','0:v','-map','1:a','-map','2:a','-map','3:s','-map','4:s','-c:v','libx264',
        '-preset','ultrafast','-c:a','aac','-c:s','srt','-metadata:s:a:0','language=eng',
        '-metadata:s:a:1','language=spa','-metadata:s:a:1','title=Second soundtrack',
        '-metadata:s:s:0','language=eng','-metadata:s:s:1','language=spa',str(source)],check=True,timeout=30)
    return source

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--screenshots',type=Path);args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nmedia-gui-check-') as temporary:
        work=Path(temporary);source=source_fixture(work)
        result,events=wait(Task({'task':'probe','path':str(source)}))
        assert result and not [e for e in events if e['kind']=='error'],events
        info=next(e['info'] for e in events if e['kind']=='source');preview=next(e['image'] for e in events if e['kind']=='preview')
        assert len(info['audio'])==2 and len(info['subtitles'])==2 and info['audio'][1]['language']=='spa'
        assert info['bytes']==source.stat().st_size and info['width']==384 and info['height']==216
        options=EncodeOptions(source=str(source),output=str(work/'movie.nvp.tns'),audio_track=1,
            subtitle_mode='embedded',subtitle_tracks=[1],preset='ultrafast')
        assert not options.errors(info),options.errors(info)
        options.rate_mode='size';options.target_mb=1;assert options.budget_kbps(info)>0
        assert '--two-pass' in options.arguments(info)
        options.two_pass=False;assert '--two-pass' not in options.arguments(info);options.two_pass=True
        options.start='invalid';assert 'trim' in options.errors(info)
        options.start='0';options.audio_codec='flac';assert 'size' in options.errors(info)
        options.audio_codec='opus';options.rate_mode='quality'
        options.crop='999:999:0:0';assert 'advanced' in options.errors(info);options.crop=''
        assert time_value('1:02:03')==3723 and output_path('movie.nvp')=='movie.nvp.tns'
        destination=work/'movie.nvp.tns';destination.write_bytes(b'previous file')
        task=encode_task(options.arguments(info));staging=Path(task.request['staging'])
        result,events=wait(task)
        assert result and not [e for e in events if e['kind']=='error'],events
        assert destination.read_bytes()[:4]==b'NVP1' and not staging.exists()
        stats=json.loads(destination.with_suffix('.json').read_text())
        assert stats['audio']['source_track']==1 and stats['audio']['codec']=='opus'
        assert Path(stats['output_path']).resolve()==destination.resolve()
        assert any(e['kind']=='progress' and e.get('ratio') is not None for e in events)
        from audio_track import FOOTER
        raw=destination.read_bytes();footer=FOOTER.unpack(raw[-FOOTER.size:])
        assert footer[0]==b'NVPAUD13' and footer[7]==6
        track=work/'sound.opus';track.write_bytes(raw[footer[2]:footer[2]+footer[3]])
        pcm=subprocess.check_output([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-i',str(track),'-ac','1','-ar','8000','-f','s16le','-'])
        samples=struct.unpack('<'+'h'*(len(pcm)//2),pcm)
        crossings=sum(a<=0<b for a,b in zip(samples,samples[1:]))
        assert 850<crossings/(len(samples)/8000)<910,'The selected soundtrack must be the 880 Hz second track'
        from check_nvp_tracks import check_tracks
        stripped=check_tracks(source,destination,work)
        options.rate_mode='size';options.target_mb=.15
        for two_pass in (False,True):
            options.two_pass=two_pass;options.output=str(work/f'video-size-{two_pass}.nvp.tns')
            sized,records=wait(encode_task(options.arguments(info)))
            assert sized and not [e for e in records if e['kind']=='error'],records
            assert 1000<Path(options.output).stat().st_size<180000
        options.rate_mode='quality';options.output=str(destination)
        print('PASS actual probe, language/title tracks, Unicode paths, real H.264 + selected Opus soundtrack, subtitles and final metadata')
        kept=destination.read_bytes()
        bad=options.arguments(info);bad[bad.index('--codec')+1]='invalid'
        result,events=wait(encode_task(bad));assert not result and any(e['kind']=='error' for e in events)
        assert destination.read_bytes()==kept
        # A longer native encoding pass makes cancellation deterministic on
        # fast hosts; a three-second clip can finish between process polls.
        cancel_source=work/'cancel-encoding.mkv'
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-y','-f','lavfi','-i',
            'testsrc2=size=384x216:rate=24','-t','45','-c:v','libx264','-preset','ultrafast',str(cancel_source)],check=True,timeout=30)
        options.source=str(cancel_source);options.codec='h264';options.crf=18;options.fps='60';options.preset='veryslow';options.audio=False;options.subtitle_mode='none'
        task=encode_task(options.arguments(info));staging=Path(task.request['staging'])
        import psutil
        deadline=time.monotonic()+15;children=[];cancel_events=[]
        while time.monotonic()<deadline and task.process.is_alive():
            cancel_events.extend(task.messages())
            try:children=[child for child in psutil.Process(task.process.pid).children(recursive=True) if '-progress' in child.cmdline()]
            except psutil.NoSuchProcess:break
            if children:break
            time.sleep(.02)
        assert children,('Cancellation test must interrupt an actual encoding pass, not its metadata probe',cancel_events[-8:])
        assert (staging/'scratch').is_dir()
        task.cancel();task.process.join(timeout=8)
        assert not task.process.is_alive();task.close();assert not staging.exists()
        assert destination.read_bytes()==kept
        for child in children:
            try:assert not child.is_running() or child.status()==psutil.STATUS_ZOMBIE
            except psutil.NoSuchProcess:pass
        assert not list(work.glob('.nmedia-*'))
        print('PASS failed and cancelled work preserves existing output, stops native children and removes only its staging directory')
        from PySide6.QtWidgets import QApplication
        from PySide6.QtGui import QPixmap,QFontDatabase
        from PySide6.QtCore import Qt,QSettings
        from encoder_gui.app import EncoderWindow,apply_style
        app=QApplication.instance() or QApplication([])
        if os.name=='nt':QFontDatabase.addApplicationFont(str(Path(os.environ.get('SystemRoot','C:/Windows'))/'Fonts/bahnschrift.ttf'))
        apply_style(app)
        QSettings.setDefaultFormat(QSettings.Format.IniFormat);QSettings.setPath(QSettings.Format.IniFormat,QSettings.Scope.UserScope,str(work/'settings'))
        window=EncoderWindow();window.selected_path=str(source);window.output.setText(str(work/'gui.nvp.tns'));window.set_source_info(info)
        pixmap=QPixmap();pixmap.loadFromData(preview);window.preview.set_image(pixmap);window.source.set_image(pixmap)
        window.source.title.setText('A video with two soundtracks.mkv');window.show();app.processEvents()
        assert window.encode.isEnabled()
        assert not window.windowIcon().isNull(),'The bundled nMedia logo must load as the window icon'
        window.audio_track.setCurrentIndex(1);assert window.options().audio_track==1
        window.subtitle_mode.setCurrentIndex(1);window.tracks.topLevelItem(1).setCheckState(0,Qt.CheckState.Checked)
        assert window.options().subtitle_tracks==[1]
        window.fps.setEditText('23.976');assert window.options().fps=='23.976'
        window.start.setText('bad');assert not window.encode.isEnabled()
        window.start.setText('0');assert window.encode.isEnabled()
        window.copy_command();assert '--audio-track' in app.clipboard().text()
        window.codec.setCurrentIndex(1);assert not window.preset.isEnabled()
        assert [window.stream_profile.itemData(i) for i in range(window.stream_profile.count())]==['fast','intra']
        window.codec.setCurrentIndex(3);assert window.preset.isEnabled()
        assert [window.stream_profile.itemData(i) for i in range(window.stream_profile.count())]==['fast','quality','intra']
        window.codec.setCurrentIndex(0)
        window.size_mode.setChecked(True);window.size_preset.setCurrentIndex(2)
        assert window.options().target_mb==25 and '--two-pass' in window.options().arguments(info)
        window.two_pass.setChecked(False);assert '--two-pass' not in window.options().arguments(info)
        if args.screenshots:
            args.screenshots.mkdir(parents=True,exist_ok=True)
            window.tabs.setCurrentIndex(0);app.processEvents();window.grab().save(str(args.screenshots/'target-size.png'))
        window.quality_mode.setChecked(True)
        if args.screenshots:
            args.screenshots.mkdir(parents=True,exist_ok=True)
            for index,name in enumerate(('video','audio','subtitles')):
                window.tabs.setCurrentIndex(index);app.processEvents();window.grab().save(str(args.screenshots/(name+'.png')))
        window.resize(820,650);app.processEvents()
        assert window.preview.geometry().bottom()<window.framing.geometry().top(),'Preview must not overlap text on a smaller screen'
        from nvp_tracks import NvpFile
        window.selected_path=str(stripped);window.output.setText(str(work/'gui-edited.nvp.tns'));window.set_source_info(NvpFile.read(stripped).info())
        window.original_path=str(source);window.set_original_info(info);window.audio_action.setCurrentIndex(2)
        assert window.encode.isEnabled() and not window.tabs.widget(0).isEnabled()
        argv=window.options().arguments(window.info)
        assert argv[argv.index('--audio')+1]=='replace' and '--track-source' in argv
        window.audio_action.setCurrentIndex(1);assert window.encode.isEnabled()
        window.audio_action.setCurrentIndex(2);window.audio_track.setCurrentIndex(1)
        target=Path(window.options().output);window.start_encode();deadline=time.monotonic()+45
        while window.task and time.monotonic()<deadline:app.processEvents();time.sleep(.01)
        assert not window.task and window.completed_output,window.log.toPlainText()
        assert NvpFile.read(target).video_hash()==NvpFile.read(stripped).video_hash()
        assert window.encode.text()=='Save edited video'
        window.close();print('PASS native controls, track selection, inline validation, CLI copy and compact window layout')
        from check_audio_conversion import check
        check(work,args.screenshots)

if __name__=='__main__':
    multiprocessing.freeze_support();main()
