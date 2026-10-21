#include "native_usb_ui_resolver.h"
#include "native_usb_timing.h"
#include <string.h>
typedef struct {
    unsigned slot, bytes;
    uint32_t hash, first[4];
} UiCodeWitness;
#define UI_WITNESS_CX 0x100U
#define UI_WITNESS_ROLE 0xffU
#include "native_usb_ui_patterns.h"
static uint32_t word(const PortableView *v, uint32_t at)
{
    uint32_t result = 0;
    if (!(at & 3) && v->allow_span(v->context, at, 4, PORTABLE_DATA))
        v->read_word(v->context, at, &result);
    return result;
}
static uint32_t branch(const PortableView *v, uint32_t at)
{
    uint32_t op = word(v, at);
    if ((op & 0x0e000000U) != 0x0a000000U)
        return 0;
    int32_t offset = (int32_t)(op << 8) >> 6;
    uint32_t target = at + 8 + offset;
    return v->allow_span(v->context, target, 4, PORTABLE_CODE) ? target : 0;
}
static bool prefix(const PortableView *v, uint32_t at, const uint32_t *expected, unsigned count)
{
    if (!at || !v->allow_span(v->context, at, count * 4, PORTABLE_CODE))
        return false;
    for (unsigned i = 0; i < count; ++i)
        if (word(v, at + 4 * i) != expected[i])
            return false;
    return true;
}
static bool witness(const PortableView *v, uint32_t at, const UiCodeWitness *p)
{
    if (!v->allow_span(v->context, at, p->bytes, PORTABLE_CODE))
        return false;
    uint32_t hash = 2166136261U;
    for (unsigned offset = 0; offset < p->bytes; offset += 4) {
        uint32_t op = native_usb_timing_original(v,at+offset,word(v, at + offset));
        if ((p->slot & UI_WITNESS_ROLE) == UI_HUB_CONTROL && offset >= 0x30U && offset < 0x8cU) {
            if ((op & 3U) || op < at || op >= at + p->bytes) return false;
            op -= at;
        } else if ((p->slot & UI_WITNESS_ROLE) == UI_CX_FEATURES && offset >= 0x34U && offset < 0x48U) {
            if ((op & 3U) || op < at || op >= at + p->bytes) return false;
            op -= at;
        } else if ((p->slot & UI_WITNESS_ROLE) == UI_HCD_CORE_CONTROL && offset >= 0x20U && offset < 0x5cU) {
            if ((op & 3U) || op < at || op >= at + p->bytes) return false;
            op -= at;
        } else if ((op & 0x0e000000U) == 0x0a000000U) {
            uint32_t target = branch(v, at + offset);
            if (!target)
                return false;
            if ((op & 0x01000000U) || target < at || target >= at + p->bytes)
                op &= 0xff000000U;
        }
        hash = (hash ^ op) * 16777619U;
    }
    return hash == p->hash;
}
bool native_usb_ui_role_matches(const PortableView *v, uint32_t at, unsigned role)
{
    for (unsigned i = 0; i < sizeof(witnesses) / sizeof(*witnesses); ++i)
        if ((witnesses[i].slot & UI_WITNESS_ROLE) == role && witness(v, at, witnesses + i)) return true;
    return false;
}
static bool unique(uint32_t *slot, uint32_t value)
{
    if (*slot && *slot != value)
        return false;
    *slot = value;
    return true;
}
static uint32_t literal(const PortableView *v, uint32_t pc)
{
    uint32_t op = word(v, pc);
    uint32_t shape = op & 0xffff0000U;
    if (shape != 0xe59f0000U && shape != 0xe51f0000U)
        return 0;
    return word(v, shape == 0xe59f0000U ? pc + 8 + (op & 4095U) : pc + 8 - (op & 4095U));
}
/* One resolver per application. No mapped code pointer survives a step.
 * Partial addresses are never invoked; final relationship checks still run
 * before publication. Explicit connection can finish the same pending scan. */
static struct {
    NativeUsbUiApi *output;
    bool classic, event_setter, hinted;
    unsigned phase;
    int result;
    uint32_t cursor, end, optional_ambiguous;
} resolver;
static struct {
    const UiCodeWitness *witness;
    int next;
    unsigned prefix_words;
} candidates[sizeof(witnesses) / sizeof(*witnesses)];
static int buckets[64];
static unsigned candidate_count;
static int resolve_error(void) { resolver.result = -1; return -1; }
static bool resolve_start(const PortableView *v, bool classic_cx, NativeUsbUiApi *api)
{
    if (!v || !api)
        return false;
    memset(api, 0, sizeof(*api));
    api->classic_cx = classic_cx;
    uint32_t begin = v->code_begin, end = v->code_end;
    if (begin != 0x10000000U || end < begin + 0x1000000U)
        return false;
    end = begin + 0x1000000U;
    memset(&resolver, 0, sizeof(resolver));
    resolver.output = api;
    resolver.classic = classic_cx;
    resolver.cursor = begin;
    resolver.end = end;
    for (unsigned i = 0; i < 64; ++i) buckets[i] = -1;
    candidate_count = 0;
    /* Family/layout filtering is invariant across the scan. Keep the per-word
     * path to a first-instruction comparison for almost all OS memory. */
    for (unsigned i = 0; i < sizeof(witnesses) / sizeof(*witnesses); ++i) {
        const UiCodeWitness *p = witnesses + i;
        unsigned role = p->slot & UI_WITNESS_ROLE;
        if (role >= UI_FUNCTIONS) continue;
        bool cx = (p->slot & UI_WITNESS_CX) != 0;
        bool family_specific = role <= UI_DISPLAY_HISR_CREATE || role == UI_BUSY_STOP ||
            role == UI_CURSOR_HIDE || role == UI_CURSOR_SHOW || role == UI_APD_ALLOW ||
            role == UI_BUS_LIST || role == UI_OTG_ATTACH || role == UI_HCD_CONTROL ||
            role == UI_EHCI_WAKE || role == UI_BUSY_CALLBACK || role == UI_DIM_DISABLE ||
            role == UI_CX_FEATURES || role == UI_HOST_LAYOUT;
        if (family_specific && cx != classic_cx) continue;
        /* These small wrappers can legitimately be shared by several
         * host types. Validate their live method pointers instead. */
        if (role == UI_HCD_RESUME || role == UI_HUB_RESUME ||
            (cx && (role == UI_BSD_TAKE || role == UI_BSD_GIVE))) continue;
        unsigned bucket = (p->first[0] ^ (p->first[0] >> 16)) & 63U;
        candidates[candidate_count].witness = p;
        candidates[candidate_count].next = buckets[bucket];
        candidates[candidate_count].prefix_words =
            role == UI_BUS_LIST || role == UI_CX_FEATURES ? 1 :
            role == UI_TASK_SLEEP || role == UI_CLOCK_TIME || role == UI_OTG_ATTACH ? 3 :
            role == UI_SEM_GIVE || role == UI_BSD_LOCK ? 2 : 4;
        buckets[bucket] = (int)candidate_count++;
    }

    return true;
}
static bool resolve_derive(const PortableView *v, NativeUsbUiApi *api)
{
    for (unsigned i = 0; i < UI_FUNCTIONS; ++i)
        if (i < UI_HOST_LAYOUT && (!api->classic_cx || i > UI_DISPLAY_HISR_CREATE) && !api->function[i])
            return false;
    if (!api->classic_cx && !api->display_descriptor)
        return false;
    if (api->function[UI_HOST_LAYOUT]) {
        api->host_global = literal(v, api->function[UI_HOST_LAYOUT] + 12);
        if (!v->allow_span(v->context, api->host_global, 4, PORTABLE_DATA)) api->host_global = 0;
    }
    if (api->function[UI_BUS_LIST]) {
        api->bus_node = literal(v, api->function[UI_BUS_LIST] + (api->classic_cx ? 0 : 8)) +
                        (api->classic_cx ? 0 : 16);
        if (!v->allow_span(v->context, api->bus_node, 16, PORTABLE_DATA)) api->bus_node = 0;
    }
    if (api->classic_cx && api->function[UI_BSD_LOCK]) {
        uint32_t lock = api->function[UI_BSD_LOCK], take = branch(v, lock + 8), give = branch(v, lock - 8);
        if (native_usb_ui_role_matches(v, take, UI_BSD_TAKE) &&
            native_usb_ui_role_matches(v, give, UI_BSD_GIVE) &&
            branch(v, take + 4) == api->function[UI_SEM_TAKE] &&
            branch(v, give) == api->function[UI_SEM_GIVE]) {
            uint32_t table = literal(v, lock);
            if (table == literal(v, lock - 16) && v->allow_span(v->context, table, 12, PORTABLE_DATA)) {
                api->bsd_locks = table;
                api->function[UI_BSD_TAKE] = take;
                api->function[UI_BSD_GIVE] = give;
            }
        }
    }
    if (!api->classic_cx && api->function[UI_BSD_LOCK] && api->function[UI_BSD_TAKE] && api->function[UI_BSD_GIVE] &&
        api->function[UI_SEM_TAKE] && api->function[UI_SEM_GIVE]) {
        uint32_t lock = api->function[UI_BSD_LOCK];
        if (branch(v, lock + 8) == api->function[UI_BSD_TAKE] &&
            branch(v, lock - 8) == api->function[UI_BSD_GIVE] &&
            branch(v, api->function[UI_BSD_TAKE] + 20) == api->function[UI_SEM_TAKE] &&
            branch(v, api->function[UI_BSD_GIVE] + 20) == api->function[UI_SEM_GIVE]) {
            uint32_t table = literal(v, lock);
            if (table == literal(v, lock - 16) && v->allow_span(v->context, table, 12, PORTABLE_DATA))
                api->bsd_locks = table;
            uint32_t mode = literal(v, api->function[UI_BSD_TAKE]);
            if (mode == literal(v, api->function[UI_BSD_GIVE]) &&
                v->allow_span(v->context, mode & ~3U, 4, PORTABLE_DATA)) api->bsd_mode = mode;
        }
    }
    uint32_t clock_getter = branch(v, api->function[UI_CLOCK_TIME] + 12);
    uint32_t clock_load = word(v, clock_getter) & 0xfffff000U;
    if ((clock_load != 0xe59f0000U && clock_load != 0xe51f0000U) ||
        word(v, clock_getter + 4) != 0xe5900000U ||
        word(v, clock_getter + 8) != 0xe12fff1eU)
        return false;
    api->clock_counter = literal(v, clock_getter);
    api->clock_hz = literal(v, api->function[UI_CLOCK_TIME] + 4);
    if (!v->allow_span(v->context, api->clock_counter, 4, PORTABLE_DATA) ||
        !v->allow_span(v->context, api->clock_hz, 4, PORTABLE_DATA))
        return false;
    api->task_global = literal(v, api->function[UI_TASK_SLEEP] + 0x18);
    uint32_t task_protection = literal(v, api->function[UI_TASK_SLEEP] + 0x10);
    if (!v->allow_span(v->context, api->task_global, 4, PORTABLE_DATA) ||
        !v->allow_span(v->context, task_protection, 16, PORTABLE_DATA))
        return false;
    if (!api->classic_cx) {
        api->display_state = word(v, api->display_descriptor + 0x14);
        if (!v->allow_span(v->context, api->display_state, 0xdc + 2048, PORTABLE_DATA))
            return false;
        uint32_t io = word(v, api->display_descriptor + 0x38);
        if (branch(v, io + 0x140) != api->function[UI_DISPLAY_START] ||
            branch(v, io + 0x1e4) != api->function[UI_DISPLAY_STOP])
            return false;
        api->display_timer = literal(v, api->function[UI_DISPLAY_START] + 0x2c);
        api->display_hisr = literal(v, api->function[UI_DISPLAY_HISR_CREATE] + 0x24);
    }
    api->busy_base = literal(v, api->function[UI_BUSY_STOP] + 4);
    if (!api->busy_base || api->busy_base != literal(v, api->function[UI_BUSY_START] + 4) ||
        !v->allow_span(v->context, api->busy_base, 16, PORTABLE_DATA))
        return false;
    api->busy_interval = literal(v, api->function[UI_BUSY_START] + 0x28);
    api->busy_callback = literal(v, api->function[UI_BUSY_START] + 0x2c);
    if (!native_usb_ui_role_matches(v, api->busy_callback, UI_BUSY_CALLBACK)) return false;
    api->cursor_count = literal(v, api->function[UI_CURSOR_HIDE] + 8);
    uint32_t show_count = api->classic_cx ? literal(v, api->function[UI_CURSOR_SHOW] + 0x54)
                                         : word(v, api->function[UI_CURSOR_SHOW] + 0x210);
    if (api->cursor_count != show_count) return false;
    api->event_mask = api->cursor_count + 2;
    api->dim_disable = api->function[UI_DIM_DISABLE];
    if (!api->dim_disable) return false;
    if (api->classic_cx) {
        uint32_t features = api->function[UI_CX_FEATURES];
        if (!features) return false;
        uint32_t state = literal(v, features + 8), magic = literal(v, features + 12);
        if (!v->allow_span(v->context, state, 40, PORTABLE_DATA) || !magic || word(v, state + 36) != magic) return false;
        api->direct_lcd_flags = state + 8;
    } else {
        api->cursor_device_hide = branch(v, api->function[UI_CURSOR_HIDE] + 0x48);
        const uint32_t cursor[] = {0xe59f101cU, 0xe52de004U, 0xe24dd02cU, 0xe3a03000U};
        if (!prefix(v, api->cursor_device_hide, cursor, 4) || literal(v, api->cursor_device_hide) != 1005U ||
            branch(v, api->function[UI_APD_ALLOW] + 0xb4) != api->dim_disable) return false;
    }
    return true;
}
static bool hinted_api(const PortableView *v,bool classic_cx,NativeUsbUiApi *out)
{
    /* Search hints for an inspected CX II 6.2 layout. These are candidates,
     * never trusted bindings: every complete function witness, descriptor and
     * derived relationship is checked exactly as in the general resolver.
     * Other firmware/regions and rejected hints retain the bounded full scan. */
    if(classic_cx)return false;
    static const uint32_t functions[UI_FUNCTIONS]={
        0x100069e4,0x10006d18,0x10006e44,0x10006c90,0x10020134,0x1002022c,
        0x103ce000,0x103ce0b8,0x10101ebc,0x10429934,0x10489a14,0x1061db6c,
        0x1048a1d4,0x10479a60,0x10a60ea4,0x10a627cc,0x1047722c,0x10489c5c,
        0x10489c40,0x103a7154,0x103a700c,0,0x10a63498,0,0x10a6c2a4,
        0x10476804,0x10476d30,0x10a63444,0x10a64bf0,0x10020288,0x101022a0,0};
    NativeUsbUiApi candidate={.display_descriptor=0x10c4e550U};
    for(unsigned i=0;i<UI_FUNCTIONS;++i) {
        if(functions[i] && !native_usb_ui_role_matches(v,functions[i],i))return false;
        candidate.function[i]=functions[i];
    }
    uint32_t d=candidate.display_descriptor;
    const uint32_t shape[]={0xe3e0300eU,0xe92d4070U,0xe5813008U,0xe590001cU};
    if(!v->allow_span(v->context,d,0x3c,PORTABLE_DATA) || word(v,d+12)!=0x494f4452U ||
       word(v,d+0x2c)!=31U || !prefix(v,word(v,d+0x38),shape,4) || !resolve_derive(v,&candidate))return false;
    *out=candidate;return true;
}
static bool resolve_input_window(const PortableView *v,const NativeUsbUiApi *api)
{
    uint32_t begin=v->code_begin,end=begin+0x1000000U;
    uint32_t center=branch(v,api->function[UI_CURSOR_SHOW]+(api->classic_cx?0x12c:0x198));
    if(center<begin+0x10000U || center>end-0x10000U)return false;
    resolver.cursor=(center-0x10000U)&~3U;resolver.end=center+0x10000U;resolver.phase=1;
    return true;
}
int native_usb_ui_resolve_step(const PortableView *v, bool classic_cx, NativeUsbUiApi *api,
                               unsigned word_budget)
{
    if (!v || !api || !word_budget) return -1;
    if (resolver.output != api || resolver.classic != classic_cx) {
        if (!resolve_start(v, classic_cx, api)) return resolve_error();
        if(hinted_api(v,classic_cx,api)) {
            if(!resolve_input_window(v,api)) {
                if(!resolve_start(v,classic_cx,api))return resolve_error();
            } else resolver.hinted=true;
        }
    }
    if (resolver.result) return resolver.result;
    uint32_t begin = v->code_begin;
    while (resolver.phase == 0 && resolver.cursor < resolver.end) {
        uint32_t page = resolver.cursor & ~4095U;
        if (!v->allow_span(v->context, page, 4096, PORTABLE_CODE)) {
            resolver.cursor = page + 4096;
            if (!--word_budget) return 0;
            continue;
        }
        const uint32_t *code = v->code_words ? v->code_words(v->context, page, 4096) : NULL;
        for (uint32_t at = resolver.cursor; at < page + 4096; at += 4) {
            uint32_t first = code ? code[(at - page) / 4] : word(v, at);
            unsigned bucket = (first ^ (first >> 16)) & 63U;
            for (int i = buckets[bucket]; i >= 0; i = candidates[i].next) {
                const UiCodeWitness *p = candidates[i].witness;
                if (first != p->first[0]) continue;
                unsigned role = p->slot & UI_WITNESS_ROLE;
                bool cx = (p->slot & UI_WITNESS_CX) != 0;
                if (resolver.optional_ambiguous & (1U << role)) continue;
                unsigned words = candidates[i].prefix_words;
                if (code && at <= page + 4096U - words * 4U) {
                    /* The page was checked once above. Reject nonmatching
                     * prefixes without repeating MMU walks for every word.
                     * Full function witnesses remain mandatory below. */
                    unsigned n = 1;
                    while (n < words && code[(at - page) / 4 + n] == p->first[n]) ++n;
                    if (n != words) continue;
                } else if (!prefix(v, at, p->first, words)) continue;
                if (!witness(v, at, p)) continue;
                if (role == UI_PROBE_DEVICE &&
                    !native_usb_ui_role_matches(v, branch(v, at + 20), UI_PROBE_BIND)) continue;
                if (role == UI_BSD_LOCK &&
                    !native_usb_ui_role_matches(v, branch(v, at + 8), UI_BSD_TAKE)) continue;
                if (!cx && role == UI_BSD_TAKE &&
                    (!native_usb_ui_role_matches(v, branch(v, at + 20), UI_SEM_TAKE) ||
                     branch(v, at + 20) != branch(v, at + 28))) continue;
                if (!cx && role == UI_BSD_GIVE &&
                    !native_usb_ui_role_matches(v, branch(v, at + 20), UI_SEM_GIVE)) continue;
                if (role == UI_BUS_LIST) {
                    uint32_t list = literal(v, at + (cx ? 0 : 8));
                    if (word(v, list) != 0x903U || (!cx && word(v, list + 16) != 0x915U)) continue;
                }
                if (role == UI_DISPLAY_CONTROL) {
                    if (literal(v, at) != 1003U)
                        continue;
                    uint32_t dispatch = branch(v, at + 0x1c), getter = branch(v, dispatch + 8);
                    const uint32_t shape[] = {0xe92d4010U, 0xe59f401cU, 0xe5940000U, 0xe3500000U};
                    if (!prefix(v, getter, shape, 4) || word(v, getter + 0x18) != 0xe3a0001fU)
                        continue;
                }
                if (!unique(api->function + role, at)) {
                    if (role < UI_HOST_LAYOUT) return resolve_error();
                    api->function[role] = 0;
                    resolver.optional_ambiguous |= 1U << role;
                }
            }
            if (first == 0x494f4452U && at >= begin + 12) {
                uint32_t descriptor = at - 12;
                const uint32_t shape[] = {0xe3e0300eU, 0xe92d4070U, 0xe5813008U, 0xe590001cU};
                if (word(v, descriptor + 0x2c) == 31U &&
                    prefix(v, word(v, descriptor + 0x38), shape, 4))
                    if (!unique(&api->display_descriptor, descriptor))
                        return resolve_error();
            }
            resolver.cursor = at + 4;
            if (!--word_budget) return 0;
        }
    }
    if (resolver.phase == 0) {
        if (!resolve_derive(v, api)) return resolve_error();
        /* Input code lives close to the checked cursor call targets. */
        if(!resolve_input_window(v,api))return resolve_error();
        return 0;
    }
    const uint32_t setter[] = {0xe59f3004U, 0xe1c300b0U, 0xe12fff1eU};
    const uint32_t tracking[] = {0xe59f300cU, 0xe1d300b0U, 0xe3c02008U, 0xe1c320b0U};
    while (resolver.cursor < resolver.end) {
        uint32_t at = resolver.cursor;
        uint32_t first = word(v, at);
        if (first == setter[0] && prefix(v, at, setter, 3) && literal(v, at) == api->event_mask)
            resolver.event_setter = true;
        if (first == tracking[0] && prefix(v, at, tracking, 4) && word(v, at + 0x10) == 0xe8bd8070U)
            if (!unique(&api->tracking_flags, literal(v, at)))
                return resolve_error();
        resolver.cursor = at + 4;
        if (!--word_budget) return 0;
    }
    bool valid = resolver.event_setter && api->tracking_flags &&
           v->allow_span(v->context, api->cursor_count, 4, PORTABLE_DATA) &&
           v->allow_span(v->context, api->tracking_flags, 4, PORTABLE_DATA) &&
           v->allow_span(v->context, api->busy_interval, 4, PORTABLE_DATA);
    if(!valid && resolver.hinted) {
        if(!resolve_start(v,classic_cx,api))return resolve_error();
        return 0;
    }
    resolver.result = valid ? 1 : -1;
    return resolver.result;
}
bool native_usb_ui_resolve(const PortableView *v, bool classic_cx, NativeUsbUiApi *api,
                           bool (*progress)(void *), void *context)
{
    int result;
    do {
        if (progress && !progress(context)) return false;
        result = native_usb_ui_resolve_step(v, classic_cx, api, 65536);
    } while (!result);
    return result > 0;
}
