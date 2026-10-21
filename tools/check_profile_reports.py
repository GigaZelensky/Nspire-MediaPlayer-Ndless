"""Exercise the diagnostic report copy with real files, including failure bounds."""
from pathlib import Path
import os, shutil, subprocess, tempfile
from check_usb_lifecycle import function

root = Path(__file__).resolve().parents[1]
source = '''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
'''+function((root/'src/player/main.c').read_text(), 'copy_previous_usb_report')+r'''
static void write_report(unsigned size, int value)
{
    FILE *f=fopen("current", "wb");assert(f);
    for(unsigned i=0;i<size;++i)assert(fputc(value,f)!=EOF);
    assert(!fclose(f));
}
static void verify(const char *path,unsigned size,int value)
{
    FILE *f=fopen(path,"rb");assert(f);
    for(unsigned i=0;i<size;++i)assert(fgetc(f)==value);
    assert(fgetc(f)==EOF && !ferror(f));assert(!fclose(f));
}
int main(void)
{
    assert(!copy_previous_usb_report("absent","previous"));
    write_report(27951,'A');assert(copy_previous_usb_report("current","previous"));
    verify("current",27951,'A');verify("previous",27951,'A');
    write_report(31000,'B');assert(copy_previous_usb_report("current","previous"));
    verify("current",31000,'B');verify("previous",31000,'B');
    assert(!copy_previous_usb_report("current","missing-dir/previous"));
    verify("current",31000,'B');
    write_report(0,'C');assert(copy_previous_usb_report("current","previous"));verify("previous",0,'C');
    write_report(128*1024+1,'D');assert(!copy_previous_usb_report("current","previous"));
    verify("previous",128*1024,'D');verify("current",128*1024+1,'D');
    puts("PASS diagnostic prior-run copy: exact repeated replacement, source preserved, missing files, empty report and 128 KiB I/O bound");
    return 0;
}
'''
cc = os.environ.get('CC') or shutil.which('gcc') or shutil.which('clang')
if not cc: raise SystemExit('A host C compiler is required.')
with tempfile.TemporaryDirectory(prefix='nmedia-profile-reports-') as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test.exe';c.write_text(source)
    subprocess.run([cc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=tmp,check=True,timeout=10)
