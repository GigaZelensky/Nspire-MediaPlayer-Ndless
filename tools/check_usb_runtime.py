#!/usr/bin/env python3
"""Production USB worker, timer and native handoff regressions."""
from pathlib import Path
import subprocess, shutil, tempfile
import re
from check_usb_lifecycle import function
root=Path(__file__).resolve().parents[1]
src=root/'src/storage/usb'
gcc=shutil.which('gcc') or shutil.which('clang')
if not gcc: raise SystemExit('A host C compiler is required.')
with tempfile.TemporaryDirectory(prefix='ndvideo-usb-host-') as temporary:
    work=Path(temporary)
    source=(src/'usb_host_session.c').read_text()
    source=re.sub(r'^#include.*\n','',source,flags=re.M)
    original='#define WORD(address) (*(volatile uint32_t *)(uintptr_t)(address))'
    assert original in source
    source=source.replace(original,
                          '#define WORD(address) (*test_register(address))')
    source=source.replace(function(source,'barrier'),'static void barrier(void) {}')
    fixture=(root/'tools/tests/usb/host_session.c').read_text().replace('/* PRODUCTION_HOST_SESSION */',source)
    (work/'host.c').write_text(fixture)
    subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(src),str(work/'host.c'),'-o',str(work/'host.exe')],check=True)
    subprocess.run([str(work/'host.exe')],check=True,timeout=10)
with tempfile.TemporaryDirectory(prefix='ndvideo-usb-lease-') as temporary:
    work=Path(temporary)
    source=(root/'src/player/native_usb_bus.inc').read_text()
    fixture=(root/'tools/tests/usb/native_lease.c').read_text().replace(
        '/* PRODUCTION_NATIVE_LEASE */', '\n'.join(function(source,name) for name in (
            'native_usb_ui_bus_details','native_usb_ui_bus_phy','running_native_bus','retain_bus_lease')))
    (work/'lease.c').write_text(fixture)
    subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(work/'lease.c'),'-o',str(work/'lease.exe')],check=True)
    subprocess.run([str(work/'lease.exe')],check=True,timeout=10)
with tempfile.TemporaryDirectory(prefix='ndvideo-usb-restart-') as temporary:
    work=Path(temporary)
    source=(root/'src/player/native_usb_ui.c').read_text()
    # Redirect only native-call boundaries; execute the production guards and dispatch.
    root_port=function(source,'enumerate_empty_root')
    root_port=root_port.replace('((int (*)(uint32_t, unsigned, void *))(uintptr_t)reset)', 'reset_port')
    root_port=root_port.replace('((int (*)(uint32_t, unsigned))(uintptr_t)explore)', 'explore_root')
    session=function(source,'native_session_request')
    resume=function(source,'resume_host').replace(
        '((int (*)(uint32_t))(uintptr_t)resume)(tree.hcd)', 'resume_existing(tree.hcd)')
    owner_call='((int (*)(void))(uintptr_t)request)()'
    assert resume.count(owner_call)==1, 'Native owner request boundary changed; review fixture redirection.'
    resume=resume.replace(owner_call, 'request_owner_session()')
    end_call='((int (*)(unsigned))(uintptr_t)request)(0U)'
    assert resume.count(end_call)==1, 'Native owner END boundary changed; review fixture redirection.'
    resume=resume.replace(end_call, 'end_owner_session(0U)')
    poll_call='((int (*)(uint32_t))(uintptr_t)request)(session_trace[0])'
    assert resume.count(poll_call)==1, 'Native input poll boundary changed; review fixture redirection.'
    resume=resume.replace(poll_call, 'poll_owner_session(session_trace[0])')
    fixture=(root/'tools/tests/usb/native_restart.c').read_text().replace(
        '/* PRODUCTION_NATIVE_RESTART */', function(source,'native_usb_ui_request_session_restart')+'\n'+session+'\n'+function(source,'native_session_poll')+'\n'+root_port+'\n'+ '\n'.join(function(source,name) for name in (
            'cold_start_check','cold_advance_idle'))+'\n'+resume)
    (work/'restart.c').write_text(fixture)
    subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(work/'restart.c'),'-o',str(work/'restart.exe')],check=True)
    subprocess.run([str(work/'restart.exe')],check=True,timeout=10)
with tempfile.TemporaryDirectory(prefix='ndvideo-usb-attach-') as temporary:
    work=Path(temporary)
    source=(src/'usb_controller.c').read_text()
    fixture=(root/'tools/tests/usb/attachments.c').read_text().replace(
        '/* PRODUCTION_ATTACHMENTS */', '\n'.join(function(source,name) for name in (
            'select_attachment','remember_attachment','detach')))
    (work/'attachments.c').write_text(fixture)
    subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(work/'attachments.c'),'-o',str(work/'attachments.exe')],check=True)
    subprocess.run([str(work/'attachments.exe')],check=True)
with tempfile.TemporaryDirectory(prefix='ndvideo-usb-worker-') as temporary:
    executable=Path(temporary)/'worker.exe'
    subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-I'+str(src),str(root/'tools/tests/usb/worker_clock.c'),str(root/'tools/tests/usb/fibers.c'),str(src/'usb_fs_worker.c'),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True,timeout=10)
body=(root/'src/player/usb_browser.c').read_text().split('static void connect_native(void *context)',1)[1].split('bool usb_browser_connect(',1)[0]
body='static void connect_native(void *context)'+body
fixture='''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
typedef struct { bool cancelled,preconnected,timed_out,connected,audio_only; int guard_error;const char *status,*line1,*line2;} UsbConnectWindow;
static unsigned kernel_policy=7,now_ms,resume_attempts;static int resume_result;
static bool standby_was_outer,native_clock,player_timer=true,audio_mode,tree_busy,lease,host_running=true;
static unsigned native_waits;
static unsigned soft_refusals,owner_pending;
static bool publish_after_pending;
static unsigned candidate,candidate_total=1,candidate_available=1;
static bool accepting,late_child,quiet_reconnect;
static unsigned late_attachments;
static uint32_t os_counter;
enum {USB_AUTO_UNKNOWN=255,USB_AUTO_ADAPTER=1,NATIVE_USB_SESSION_PENDING=-70};
static void prepare_progress(void) {}
static bool native_usb_ui_clock(const volatile uint32_t **p,uint32_t *hz,void (*f)(void),void *c) {*p=&os_counter;*hz=50;return true;}
static bool monotonic_clock_enter_native(const volatile uint32_t *p,uint32_t hz) {assert(player_timer);assert(!native_clock);native_clock=true;return true;}
static void monotonic_clock_leave_native(void) {if(candidate && !lease)host_running=false;native_clock=false;}
static bool monotonic_clock_restore_player_timer(void) {assert(!native_clock);player_timer=true;return true;}
static unsigned native_critical_enter(void) {return 0xc0;}
static void native_critical_leave(unsigned x) {}
static unsigned TCT_Local_Control_Interrupts(unsigned p) {unsigned old=kernel_policy;kernel_policy=p;return old;}
static bool native_usb_ui_active(void) {return false;}
static bool player_standby_guard_active(void) {return true;}
static int native_usb_ui_acquire(void (*f)(void),void *c) {return 0;}
static void native_usb_ui_release(void) {}
static const char *native_usb_ui_stage(void) {return "fixture";}
static void debug_tracef(const char *f,...) {}
static void connection_checkpoint(UsbConnectWindow *w,const char *s,bool force) {if(!native_clock) player_timer=false;}
static void render_window(UsbConnectWindow *w,bool force) {}
static bool usb_storage_prepare_connection(void) {accepting=true;return true;}
static bool usb_fs_prepare(void) {return true;}
static uint32_t monotonic_clock_now_ms(void) {now_ms+=100;return now_ms;}
static uint32_t connection_seconds(void) {return now_ms/1000;}
static bool host_device_present(void) {return true;}
static unsigned connection_presence(void) {return USB_AUTO_ADAPTER;}
static uint32_t host_connection_state(void) {return tree_busy?4:1;}
static bool usb_storage_driver_registered(void) {return true;}
static void usb_controller_set_error(const char *s,int e) {}
static bool usb_storage_attachment_ready(void) {return !audio_mode && !tree_busy && candidate<candidate_available;}
static bool usb_controller_audio_only(void) {return audio_mode;}
static bool usb_storage_next_candidate(void) {assert(native_clock && kernel_policy==0 && !lease);if(candidate>=candidate_available)return false;++candidate;return true;}
static bool usb_storage_prepare_endpoints(void) {assert(native_clock && kernel_policy==0 && accepting);player_timer=false;return true;}
static bool nspire_is_cx2(void) {return true;}
static bool usb_host_session_active(void) {return lease;}
static bool usb_host_session_prepare(uint32_t (*now)(void),void (*progress)(void),void *context,char *message,size_t capacity) {
 assert(native_clock && kernel_policy==0);lease=true;host_running=true;return true;}
static void usb_host_session_release(void) {if(lease)assert(native_clock && kernel_policy==0);lease=false;}
static bool usb_storage_finish_connection(void) {assert(!native_clock && kernel_policy==7 && player_timer && lease && host_running);return candidate==candidate_total;}
static void progress(void *p) {}
static void usb_fs_set_progress(void (*f)(void *),void *p) {}
static bool usb_volume_mount(int *e) {assert(kernel_policy==7 && !native_clock && player_timer && lease && host_running);return true;}
static int native_usb_ui_resume_host(void) {
 if(soft_refusals){--soft_refusals;return -28;}
 if(owner_pending){--owner_pending;return NATIVE_USB_SESSION_PENDING;}
 if(publish_after_pending && !candidate_available)candidate_available=1;
 if(quiet_reconnect && !candidate_available)candidate_available=1;
 return tree_busy?-51:0;}
static int native_usb_ui_reassert(void) {return 0;}
static int native_usb_ui_wait(void) {
 assert(native_clock && kernel_policy==0 && !lease);++native_waits;tree_busy=false;
 if(late_child && candidate==1 && candidate_available==1 && accepting) {
  candidate_available=2;++late_attachments;
 }
 return 0;}
static void usb_storage_stop_accepting(void) {accepting=false;}
static bool usb_storage_park(void) {return true;}
static bool performance_clock_start(void) {return true;}
static int performance_clock_status(void) {return 0;}
static void usb_storage_unpark(void) {}
'''+body+'''
int main(void) {
 UsbConnectWindow w={.preconnected=true};connect_native(&w);assert(w.connected && !w.guard_error && kernel_policy==7 && !native_clock && player_timer);
 candidate=0;audio_mode=true;w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(w.audio_only && !w.connected && !w.timed_out && !w.guard_error && kernel_policy==7 && !native_clock && player_timer);
 candidate=0;audio_mode=false;tree_busy=true;native_waits=0;w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(w.connected && native_waits && !tree_busy && !w.timed_out && !w.guard_error && kernel_policy==7 && !native_clock && player_timer);
 candidate=0;candidate_total=candidate_available=2;w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(w.connected && candidate==2 && !w.guard_error && kernel_policy==7 && !native_clock && player_timer);
 candidate=0;candidate_total=2;candidate_available=1;late_child=true;late_attachments=0;
 w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(w.connected && candidate==2 && late_attachments==1 && !accepting && !w.timed_out && !w.guard_error);
 candidate=0;candidate_available=1;late_child=false;
 w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(!w.connected && w.timed_out && candidate==1 && !accepting && !w.guard_error);
 candidate=0;candidate_available=0;candidate_total=1;soft_refusals=5;quiet_reconnect=true;
 w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(w.connected && resume_attempts==6 && !soft_refusals && !w.timed_out && !w.guard_error);
 quiet_reconnect=false;
 candidate=0;candidate_available=0;candidate_total=1;owner_pending=6;publish_after_pending=true;
 w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(w.connected && resume_attempts==7 && !owner_pending && !w.timed_out && !w.guard_error);
 candidate=0;candidate_available=0;owner_pending=100;publish_after_pending=false;
 unsigned pending_started=now_ms;
 w=(UsbConnectWindow){.preconnected=true};connect_native(&w);
 assert(!w.connected && w.timed_out && !w.guard_error && resume_attempts>4 &&
        now_ms-pending_started<9000 && !native_clock && kernel_policy==7 && player_timer);
 return 0;}
'''
with tempfile.TemporaryDirectory() as tmp:
 c=Path(tmp)/'handoff.c';exe=Path(tmp)/'handoff.exe';c.write_text(fixture)
 subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=10)
print('PASS production connection retires native policy and reclaims disturbed app timer before filesystem mount')
print('PASS hub discovery: empty reader first, flash attached on a later scheduler tick, and bounded no-media timeout')
print('PASS background recovery retries soft refusals; queued native owner work outlives the wake budget while the connection deadline stays bounded')
clock=(root/'src/player/platform_debug.c').read_text().split('bool monotonic_clock_restore_player_timer(void)',1)[1].split('bool monotonic_clock_try_init_hw_timer(void)',1)[0]
clock='bool monotonic_clock_restore_player_timer(void)'+clock
fixture='''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define MONOTONIC_TIMER_CONTROL_ENABLE_32BIT 0x82u
#define MONOTONIC_TIMER_CLOCK_SOURCE_32768HZ 0xau
typedef struct {uint32_t load,control,source;} TimerSnapshot;
static struct {bool captured,active;TimerSnapshot player,entry;} native_clock;
static unsigned value=42,restores,mask;
static struct {bool initialized,using_hw_timer;unsigned *value_reg,last_value;uint64_t elapsed_ticks;} g_clock;
static unsigned native_critical_enter(void) {unsigned old=mask;mask=0xc0;return old;}
static void native_critical_leave(unsigned old) {mask=old;}
static bool timer_start_player(TimerSnapshot s,uint32_t *out) {assert(s.control==0x82 && s.source==0xa);++restores;value=123;*out=value;return true;}
'''+clock+'''
int main(void) {
 assert(!monotonic_clock_restore_player_timer() && !restores);
 native_clock.captured=true;native_clock.player=(TimerSnapshot){99,0x82,0xa};native_clock.entry=(TimerSnapshot){7,0x20,1};
 g_clock.initialized=true;g_clock.value_reg=&value;g_clock.elapsed_ticks=999;
 native_clock.active=true;assert(!monotonic_clock_restore_player_timer() && !restores);native_clock.active=false;
 assert(monotonic_clock_restore_player_timer() && restores==1 && g_clock.last_value==123 && g_clock.using_hw_timer && g_clock.elapsed_ticks==999 && !mask);
 assert(native_clock.entry.control==0x20 && native_clock.entry.source==1 && native_clock.entry.load==7);
 native_clock.player.source=1;assert(!monotonic_clock_restore_player_timer() && restores==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 c=Path(tmp)/'timer.c';exe=Path(tmp)/'timer.exe';c.write_text(fixture)
 subprocess.run([gcc,'-std=c99','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS production timer reclaim preserves elapsed time/OS snapshot and rejects unowned/native-active state')
