#include "native_usb_timing.h"
#if NDMEDIA_USB_FAST_ENUMERATION
#include "native_usb_ui_resolver.h"
#include "native_interrupts.h"

/* The two complete native function witnesses must match
 * before touching code. USB 2.0 section 7.1.7.3 / Table 7-14 requires at least
 * 100 ms attach debounce and 10 ms reset recovery. Keep 180/40 ms here; leave
 * reset polling, power-good delays and all descriptor-error retries intact.
 * No control-flow instruction, callback or native data object is replaced. */
static struct {
    const PortableView *view;
    NativeUsbTimingIo io;
    uint32_t address[2],original[2],replacement[2];
    unsigned attempts,installs,restores;
    int result;
    bool active;
} timing;

static bool read_code(const PortableView *v,uint32_t at,uint32_t *op)
{
    return v && v->allow_span && v->read_word && !(at&3U) &&
        v->allow_span(v->context,at,4,PORTABLE_CODE) && v->read_word(v->context,at,op);
}
static uint32_t call_target(uint32_t at,uint32_t op)
{
    if((op&0xff000000U)!=0xeb000000U)return 0;
    return at+8U+(uint32_t)((int32_t)(op<<8)>>6);
}
uint32_t native_usb_timing_original(const PortableView *v,uint32_t at,uint32_t op)
{
    /* Only recognize an instruction this transaction owns in this exact
     * live view. Any other modification still fails the full body witness. */
    if(timing.active && v==timing.view)
        for(unsigned i=0;i<2;++i)
            if(at==timing.address[i] && op==timing.replacement[i])return timing.original[i];
    return op;
}
static bool restore(void)
{
    bool ok=true;uint32_t op=0;
    for(unsigned i=0;i<2;++i) {
        if(!read_code(timing.view,timing.address[i],&op)){ok=false;continue;}
        if(op==timing.replacement[i])timing.io.write(timing.address[i],timing.original[i]);
        else if(op!=timing.original[i])ok=false; /* Never overwrite another owner. */
    }
    timing.io.sync(timing.address[0],timing.address[1]);
    bool owned=false;
    for(unsigned i=0;i<2;++i) {
        if(!read_code(timing.view,timing.address[i],&op)){ok=false;owned=true;continue;}
        if(op!=timing.original[i])ok=false;
        if(op==timing.replacement[i])owned=true;
    }
    timing.active=owned;
    if(ok)++timing.restores;
    return ok;
}
bool native_usb_timing_begin(const PortableView *v,uint32_t hub,uint32_t reset,NativeUsbTimingIo io)
{
    unsigned mask=native_critical_enter();++timing.attempts;
    bool ok=false;
    if(timing.active) {
        ok=v==timing.view && hub+0x170U==timing.address[0] && reset+0x2acU==timing.address[1];
        goto done;
    }
    timing.result=-1;
    if(!v || !v->allow_span || !v->read_word || !io.write || !io.sync ||
        hub>UINT32_MAX-0x178U || reset>UINT32_MAX-0x2b4U ||
        !native_usb_ui_role_matches(v,hub,UI_HUB_EXPLORE) ||
        !native_usb_ui_role_matches(v,reset,UI_PORT_RESET))goto done;
    uint32_t address[]={hub+0x170U,reset+0x2acU};
    const uint32_t original[]={0xe3a00f4bU,0xe3a000faU}; /* MOV r0,300 / MOV r0,250 */
    const uint32_t replacement[]={0xe3a000b4U,0xe3a00028U}; /* MOV r0,180 / MOV r0,40 */
    uint32_t target=0;
    for(unsigned i=0;i<2;++i) {
        uint32_t op,call;
        if(!read_code(v,address[i],&op) || op!=original[i] ||
            !read_code(v,address[i]+4U,&call))goto done;
        uint32_t destination=call_target(address[i]+4U,call);
        if(!destination || !v->allow_span(v->context,destination,4,PORTABLE_CODE) ||
            (target && destination!=target))goto done;
        target=destination;
    }
    if(!native_usb_ui_role_matches(v,target,UI_USB_DELAY))goto done;
    timing.view=v;timing.io=io;timing.active=true;
    for(unsigned i=0;i<2;++i) {
        timing.address[i]=address[i];timing.original[i]=original[i];
        timing.replacement[i]=replacement[i];
    }
    for(unsigned i=0;i<2;++i)timing.io.write(address[i],replacement[i]);
    timing.io.sync(timing.address[0],timing.address[1]);
    ok=true;
    for(unsigned i=0;i<2;++i) {
        uint32_t op=0;
        if(!read_code(v,address[i],&op) || op!=replacement[i])ok=false;
    }
    if(ok){++timing.installs;timing.result=1;}
    else timing.result=restore()?-2:-3;
done:
    native_critical_leave(mask);return ok;
}
bool native_usb_timing_end(void)
{
    unsigned mask=native_critical_enter();bool ok=true;
    if(timing.active){ok=restore();timing.result=ok?2:-3;}
    native_critical_leave(mask);return ok;
}
void native_usb_timing_debug(FILE *f)
{
    if(f)fprintf(f,"usb_native_timing guarded=1 attempts=%u installs=%u restores=%u active=%u result=%d debounce_ms=180 reset_recovery_ms=40 sites=%08lx/%08lx\n",
        timing.attempts,timing.installs,timing.restores,timing.active,timing.result,
        (unsigned long)timing.address[0],(unsigned long)timing.address[1]);
}
#endif
