"""Probe real Unicode metadata independently of the Windows text code page."""
from pathlib import Path
import json, subprocess, sys, tempfile, unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import encode_ndless_video as encoder
from encoder_gui.worker import probe


class ProbeTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='nmedia-probe-text-')
        cls.work = Path(cls.temporary.name)
        cls.title = 'A closing quote \u201d, accents \u00e9 and \u65e5\u672c\u8a9e'
        cls.source = cls.work / 'Movie \u201d \u00e9.mp4'
        cls.subtitle = cls.work / 'subtitle.srt'
        cls.subtitle.write_text('1\n00:00:00,000 --> 00:00:00,400\n' + cls.title + '\n', encoding='utf-8')
        cls.with_subtitle = cls.work / 'Movie with subtitles.mkv'
        ffmpeg = encoder.imageio_ffmpeg.get_ffmpeg_exe()
        subprocess.run([ffmpeg, '-v', 'error', '-y', '-f', 'lavfi', '-i',
                        'testsrc2=size=96x64:rate=24', '-f', 'lavfi', '-i',
                        'sine=frequency=440:sample_rate=48000', '-t', '0.5',
                        '-c:v', 'libx264', '-preset', 'ultrafast', '-c:a', 'aac',
                        '-metadata', 'title=' + cls.title, str(cls.source)],
                       capture_output=True, text=True, encoding='utf-8', errors='replace',
                       check=True, timeout=30)
        subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', str(cls.source),
                        '-i', str(cls.subtitle), '-map', '0:v', '-map', '0:a', '-map', '1:s',
                        '-c', 'copy', '-c:s', 'srt', '-metadata:s:s:0', 'language=eng',
                        '-metadata:s:s:0', 'title=' + cls.title, str(cls.with_subtitle)],
                       capture_output=True, text=True, encoding='utf-8', errors='replace',
                       check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_bundled_fallback_keeps_metadata_output_on_windows_code_page(self):
        # A UTF-8 closing quote contains 0x9d, undefined in cp1252. On Windows
        # strict locale decoding kills Popen's reader thread and returns None.
        with patch('subprocess._text_encoding', return_value='cp1252'), \
             patch.object(encoder, 'ffprobe_path', return_value=None):
            video = encoder.probe_video(self.source)
            self.assertEqual((video.storage_width, video.storage_height, video.fps), (96, 64, 24))
            self.assertEqual(encoder.probe_embedded_subtitle_tracks(self.source), [])
            tracks = encoder.probe_embedded_subtitle_tracks(self.with_subtitle)
            self.assertEqual(len(tracks), 1)
            self.assertEqual(tracks[0].codec_name, 'subrip')
            extracted = encoder.extract_embedded_subtitle_track(self.with_subtitle, self.work / 'extracted.srt', 0)
            self.assertIn(self.title, extracted.read_text(encoding='utf-8'))

    def test_gui_probes_unicode_source_without_external_ffprobe(self):
        class Pipe:
            def __init__(self): self.events = []
            def send(self, event): self.events.append(event)
        pipe = Pipe()
        with patch('subprocess._text_encoding', return_value='cp1252'), \
             patch.object(encoder, 'ffprobe_path', return_value=None):
            probe(str(self.source), pipe, allow_audio=True)
        info = next(event['info'] for event in pipe.events if event['kind'] == 'source')
        self.assertEqual((info['width'], info['height']), (96, 64))
        self.assertEqual(len(info['audio']), 1)
        self.assertEqual(info['subtitles'], [])
        self.assertTrue(any(event['kind'] == 'preview' for event in pipe.events))
        self.assertEqual(pipe.events[-1]['kind'], 'done')

    def test_ffprobe_json_uses_utf8_for_video_and_subtitle_probes(self):
        run = subprocess.run
        payload = json.dumps({'streams': [{
            'width': 96, 'height': 64, 'avg_frame_rate': '24/1', 'codec_name': 'subrip',
            'tags': {'language': 'eng', 'title': self.title}}],
            'format': {'duration': '0.5'}}, ensure_ascii=False).encode('utf-8')
        def json_probe(command, **kwargs):
            # Still use a real native child/pipe; substitute just the executable
            # so the check doesn't require ffprobe to be installed alongside FFmpeg.
            return run([sys.executable, '-c', 'import sys;sys.stdout.buffer.write(' + repr(payload) + ')'], **kwargs)
        with patch('subprocess._text_encoding', return_value='cp1252'), \
             patch.object(encoder, 'ffprobe_path', return_value=Path('ffprobe')), \
             patch.object(encoder.subprocess, 'run', side_effect=json_probe):
            self.assertEqual(encoder.probe_video(self.source).storage_width, 96)
            self.assertEqual(encoder.probe_embedded_subtitle_tracks(self.source)[0].name, 'ENG - ' + self.title)

    def test_missing_diagnostics_gives_a_source_error(self):
        empty = subprocess.CompletedProcess([], 1, stdout=None, stderr=None)
        with patch.object(encoder, 'ffprobe_path', return_value=None), \
             patch.object(encoder.subprocess, 'run', return_value=empty):
            self.assertEqual(encoder.probe_embedded_subtitle_tracks(self.source), [])
            with self.assertRaisesRegex(RuntimeError, 'Failed to determine input video'):
                encoder.probe_video(self.source)
            with self.assertRaisesRegex(RuntimeError, 'subtitle extraction failed'):
                encoder.extract_embedded_subtitle_track(self.source, self.work / 'missing.srt', 0)

    def test_invalid_metadata_bytes_do_not_hide_stream_details(self):
        run = subprocess.run
        payload = (b'Duration: 00:00:00.50, start: 0.000000\n'
                   b'  title: broken metadata \xff\x81\n'
                   b'  Stream #0:0: Video: h264, yuv420p, 96x64, 24 fps\n'
                   b'  Stream #0:1(eng): Subtitle: subrip\n')
        def text_probe(command, **kwargs):
            return run([sys.executable, '-c', 'import sys;sys.stderr.buffer.write(' + repr(payload) + ')'], **kwargs)
        with patch('subprocess._text_encoding', return_value='cp1252'), \
             patch.object(encoder, 'ffprobe_path', return_value=None), \
             patch.object(encoder.subprocess, 'run', side_effect=text_probe):
            self.assertEqual(encoder.probe_video(self.source).storage_width, 96)
            self.assertEqual(encoder.probe_embedded_subtitle_tracks(self.source)[0].codec_name, 'subrip')


if __name__ == '__main__': unittest.main()
