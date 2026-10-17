"""Run the packaged encoder's full probe/encode/cancel/UI smoke test."""
from pathlib import Path
import argparse,json,subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=Path('build/encoder-exe-check'))
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    report=(args.output/'report.json').resolve()
    if report.exists():raise SystemExit('Choose a fresh check output directory.')
    process=subprocess.Popen([str(args.exe.resolve()),'--self-test',str(report)],
        stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        _,stderr=process.communicate(timeout=180)
    except subprocess.TimeoutExpired:
        process.kill();process.wait();raise SystemExit('Packaged encoder check timed out.')
    if not report.exists():raise SystemExit((stderr or b'').decode('utf8','replace') or 'Packaged encoder did not produce its test report.')
    result=json.loads(report.read_text())
    if not result.get('passed') or not result.get('bundled_ffmpeg'):raise SystemExit(result.get('error','Packaged encoder check failed.'))
    print('PASS packaged executable: bundled native FFmpeg, real tracks/encode, safe cancellation, output preservation and native UI')

if __name__=='__main__':main()
