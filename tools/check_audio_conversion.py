#!/usr/bin/env python3
"""Real audio conversion, metadata/art fidelity, size budgets and GUI checks."""
from pathlib import Path
import hashlib,io,os,subprocess,tempfile,time
import imageio_ffmpeg
from audio_metadata import read_metadata,music_info
from convert_audio import EXTENSIONS,parse_args,convert

def check(work,screenshots=None):
    from PIL import Image
    from mutagen.flac import FLAC,Picture
    source=work/'Music é [original].flac'
    subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-y','-f','lavfi','-i',
        'sine=frequency=523:sample_rate=44100','-t','8','-ac','2','-c:a','flac',str(source)],check=True,timeout=20)
    music=FLAC(source)
    expected={'TITLE':['Canción de prueba'],'ARTIST':['Joaquín Sabina','Second artist'],'ALBUM':['Física y Química'],
        'ALBUMARTIST':['Joaquín Sabina'],'DATE':['1992'],'TRACKNUMBER':['2/12'],'DISCNUMBER':['1/2'],
        'GENRE':['Rock','Pop'],'COMMENT':['Keep this comment'],'CUSTOM_TAG':['Unicode β test']}
    for key,values in expected.items():music[key]=values
    # Different image types and dimensions, both carried without recompression.
    for kind,fmt,size in [(3,'JPEG',(173,111)),(4,'PNG',(64,96))]:
        data=io.BytesIO();Image.new('RGB',size,(30+kind,80,130)).save(data,fmt)
        p=Picture();p.type=kind;p.mime='image/jpeg' if fmt=='JPEG' else 'image/png';p.data=data.getvalue();p.width,p.height=size;music.add_picture(p)
    music['REPLAYGAIN_TRACK_GAIN']=['-4 dB'];music.save()
    metadata=read_metadata(source);original_hash=hashlib.sha256(source.read_bytes()).digest()
    converted={}
    for codec,extension in EXTENSIONS.items():
        destination=work/('converted-'+codec+extension)
        result=convert(parse_args([str(source),'--output',str(destination),'--codec',codec]))
        actual=read_metadata(destination)
        for key,values in expected.items():assert actual.tags.get(key)==values,(codec,key,actual.tags)
        assert [p.data for p in actual.pictures]==[p.data for p in metadata.pictures],codec
        assert 'REPLAYGAIN_TRACK_GAIN' not in actual.tags
        info=music_info(destination);assert 7.8<info['duration']<8.2,(codec,info)
        assert result['covers']==2 and result['tags']==len(metadata.tags)
        converted[codec]=destination
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-v','error','-i',str(destination),'-f','null','-'],check=True,timeout=20)
    for codec in ('mp3','aac','opus'):
        dest=work/(codec+'-roundtrip.flac');convert(parse_args([str(converted[codec]),'--output',str(dest),'--codec','flac']))
        assert read_metadata(dest).tags['TRACKNUMBER']==['2/12']
        assert [p.data for p in read_metadata(dest).pictures]==[p.data for p in metadata.pictures]
    for flag,keep_tags,keep_art in [('--strip-tags',False,True),('--strip-art',True,False)]:
        dest=work/(flag[2:]+'.opus');convert(parse_args([str(source),'--output',str(dest),flag]))
        result=read_metadata(dest)
        assert bool(result.tags)==keep_tags and bool(result.pictures)==keep_art
    for codec in ('opus','mp3','aac','vorbis'):
        dest=work/('sized-'+codec+EXTENSIONS[codec]);result=convert(parse_args([str(source),'--output',str(dest),'--codec',codec,'--target-mb','.10']))
        assert dest.stat().st_size<=100000,(codec,result)
        assert len(read_metadata(dest).pictures)==2
    # Failed conversions keep the previous destination; source is never touched.
    kept=work/'preserved.opus';kept.write_bytes(b'previous file')
    try:convert(parse_args([str(source),'--output',str(kept),'--track','99']))
    except RuntimeError:pass
    else:raise AssertionError('Missing track must fail')
    assert kept.read_bytes()==b'previous file'
    try:convert(parse_args([str(source),'--output',str(kept),'--target-mb','.001']))
    except ValueError:pass
    else:raise AssertionError('An impossible target must fail before writing')
    assert kept.read_bytes()==b'previous file' and hashlib.sha256(source.read_bytes()).digest()==original_hash
    from check_encoder_gui import wait
    from encoder_gui.worker import Task,encode_task
    from encoder_gui.model import AudioOptions
    result,events=wait(Task({'task':'probe','path':str(source)}))
    assert result,events
    info=next(e['info'] for e in events if e['kind']=='source')
    assert info['audio_only'] and info['width']==0 and info['music']['covers']==2
    assert any(e['kind']=='preview' for e in events)
    options=AudioOptions(source=str(source),output=str(work/'worker.opus'),audio_rate=0,audio_channels=0)
    overhead=options.metadata_overhead(info);options.keep_tags=False
    assert options.metadata_overhead(info)<overhead
    options.keep_tags=True
    result,events=wait(encode_task(options.arguments(info),audio=True))
    assert result and not [e for e in events if e['kind']=='error'],events
    assert read_metadata(result['output_path']).tags['TITLE']==expected['TITLE']
    from PySide6.QtWidgets import QApplication
    from PySide6.QtGui import QPixmap
    from encoder_gui.app import EncoderWindow,apply_style
    app=QApplication.instance() or QApplication([]);apply_style(app)
    window=EncoderWindow();window.selected_path=str(source);window.output.setText(str(work/'gui.nvp.tns'));window.set_source_info(info)
    pixmap=QPixmap()
    pixmap.loadFromData(metadata.pictures[0].data);window.preview.set_image(pixmap);window.source.set_image(pixmap)
    window.source.title.setText(source.name);window.show();app.processEvents()
    assert window.is_audio() and window.encode.text()=='Convert audio' and window.encode.isEnabled()
    assert window.output.text().endswith('.opus') and not window.tabs.widget(0).isEnabled()
    window.audio_codec.setCurrentIndex(window.audio_codec.findData('flac'))
    assert window.output.text()!=str(source) and window.output.text().endswith('.flac')
    window.music_preset.setCurrentIndex(1);window.music_size.setChecked(True);window.music_target.setValue(.10)
    assert '--target-mb' in window.options().arguments(info) and not window.audio_bitrate.isEnabled()
    window.copy_command();assert 'convert_audio.py' in app.clipboard().text()
    window.music_tns.setChecked(True);assert window.output.text().endswith('.mp3.tns')
    window.music_tns.setChecked(False);window.music_size.setChecked(False)
    if screenshots:
        screenshots.mkdir(parents=True,exist_ok=True);window.grab().save(str(screenshots/'audio-conversion.png'))
    window.start_encode();deadline=time.monotonic()+30
    while window.task and time.monotonic()<deadline:app.processEvents();time.sleep(.01)
    assert window.completed_output and window.status.text()=='Audio ready.',window.log.toPlainText()
    assert read_metadata(window.completed_output).pictures[0].data==metadata.pictures[0].data
    window.close()
    print('PASS all seven ordinary audio outputs, Unicode/multiple tags, exact embedded JPEG/PNG art, metadata round trips, source rate, file-size limits, failure safety and GUI/worker conversion')

if __name__=='__main__':
    import multiprocessing
    multiprocessing.freeze_support();os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
    with tempfile.TemporaryDirectory(prefix='nmedia-audio-check-') as temporary:check(Path(temporary))
