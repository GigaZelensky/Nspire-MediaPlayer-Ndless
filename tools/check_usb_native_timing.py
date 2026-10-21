"""Guarded native USB timing and restoration; optional exact raw OS carrier."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
cc=os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc:raise SystemExit('Host C compiler required')
with tempfile.TemporaryDirectory(prefix='nmedia-native-usb-timing-') as tmp:
    exe=Path(tmp)/'test.exe'
    base=[cc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'src')]
    source=root/'src/player/native_usb_timing.c'
    fixture=root/'tools/tests/usb/native_timing.c'
    subprocess.run([*base,'-DNDMEDIA_USB_FAST_ENUMERATION=1',str(fixture),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
    if len(sys.argv)>1:
        subprocess.run([*base,'-DNDMEDIA_USB_FAST_ENUMERATION=1','-DTEST_REAL_RESOLVER=1',str(fixture),str(source),
            str(root/'src/player/native_usb_ui_resolver.c'),'-o',str(exe)],check=True)
        for path in sys.argv[1:]:subprocess.run([str(exe),path],check=True,timeout=15)
    c=Path(tmp)/'quiet.c';c.write_text('''#include "player/native_usb_timing.h"
#include <assert.h>
int main(void){assert(native_usb_timing_end());assert(native_usb_timing_original(0,1,2)==2);
 FILE *f=tmpfile();assert(f);native_usb_timing_debug(f);assert(ftell(f)==0);fclose(f);return 0;}
''')
    subprocess.run([*base,str(c),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
print('PASS disabled native timing path contains no override or report')
