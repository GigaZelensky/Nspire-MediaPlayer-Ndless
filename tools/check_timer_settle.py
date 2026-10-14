#!/usr/bin/env python3
"""Delayed timer reload and stopped-clock regression checks."""
from pathlib import Path
import subprocess,tempfile,shutil
root=Path(__file__).resolve().parents[1]
source=(root/'src/player/platform_debug.c').read_text()
body=source[source.index('static bool timer_start_player('):source.index('void monotonic_clock_capture_native_timer(')]
fixture='''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef struct {uint32_t load,control,source;} TimerSnapshot;
static unsigned reads,mode,loaded;
static void timer_restore(TimerSnapshot s) {assert(s.load==UINT32_MAX);loaded++;reads=0;}
static uint32_t timer_value(void) {
 ++reads;
 if(mode==1) return 0xfffd0000u; /* Stopped, never reloads. */
 if(mode==2) return UINT32_MAX; /* Reloaded but not moving. */
 if(reads<10000) return 0xfffd0000u-(reads/50); /* Observed stale clock domain. */
 return UINT32_MAX-(reads-10000)/50;
}
'''+body+'''
int main(void) {
 uint32_t base=0;TimerSnapshot s={7,0x82,0xa};
 assert(timer_start_player(s,&base));assert(reads>=32768 && UINT32_MAX-base<8192 && loaded==1);
 /* Baseline now decrements normally; old FFFD0000 would create wrap-sized timeout. */
 assert(base-timer_value()<10);
 mode=1;assert(!timer_start_player(s,&base) && reads<=131073);
 mode=2;assert(!timer_start_player(s,&base) && reads<=131073);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 c=Path(tmp)/'settle.c';exe=Path(tmp)/'settle.exe';c.write_text(fixture)
 subprocess.run([shutil.which('gcc'),'-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=10)
print('PASS production delayed-reload settling and bounded stopped/unmoving counter rejection')
