#!/usr/bin/env python3
"""Open the nMedia Encoder desktop app. The original encoder CLI is unchanged."""
import multiprocessing,sys

if __name__=='__main__':
    multiprocessing.freeze_support()
    if len(sys.argv)>1 and sys.argv[1]=='--self-test':
        from pathlib import Path
        import json,traceback
        report=Path(sys.argv[2]).resolve();report.parent.mkdir(parents=True,exist_ok=True)
        try:
            # Match normal startup: initialize the GUI runtime before native
            # encoding and worker processes load their own runtime libraries.
            import os
            os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
            from PySide6.QtWidgets import QApplication
            app=QApplication([])
            from encoder_gui.worker import configure_subprocesses
            import imageio_ffmpeg
            configure_subprocesses()
            ffmpeg=Path(imageio_ffmpeg.get_ffmpeg_exe()).resolve()
            if getattr(sys,'frozen',False):assert ffmpeg.is_relative_to(Path(sys._MEIPASS).resolve()),'The executable must carry its own FFmpeg'
            from check_encoder_gui import main as check
            sys.argv=[sys.argv[0],'--screenshots',str(report.parent/'screenshots')]
            check();report.write_text(json.dumps({'passed':True,'bundled_ffmpeg':bool(getattr(sys,'frozen',False))}),encoding='utf8')
        except BaseException:
            report.write_text(json.dumps({'passed':False,'error':traceback.format_exc()}),encoding='utf8')
            raise SystemExit(1)
        raise SystemExit(0)
    from encoder_gui.app import main
    raise SystemExit(main())
