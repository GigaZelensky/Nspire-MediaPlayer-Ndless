#include "native_usb_ui_resolver.h"
#include <string.h>
typedef struct {
    unsigned slot, bytes;
    uint32_t hash, first[4];
} UiCodeWitness;
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
        uint32_t op = word(v, at + offset);
        if ((op & 0x0e000000U) == 0x0a000000U) {
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
    if ((op & 0xffff0000U) != 0xe59f0000U)
        return 0;
    return word(v, pc + 8 + (op & 4095U));
}
bool native_usb_ui_resolve(const PortableView *v, NativeUsbUiApi *api, bool (*progress)(void *),
                           void *context)
{
    if (!v || !api)
        return false;
    memset(api, 0, sizeof(*api));
    uint32_t begin = v->code_begin, end = v->code_end;
    if (begin != 0x10000000U || end < begin + 0x1000000U)
        return false;
    end = begin + 0x1000000U;
    /* Scan mapped OS pages once. Never inspect unmapped or peripheral space. */
    for (uint32_t page = begin; page < end; page += 4096) {
        if ((page & 0x7fff) == 0 && progress && !progress(context))
            return false;
        if (!v->allow_span(v->context, page, 4096, PORTABLE_CODE))
            continue;
        for (uint32_t at = page; at < page + 4096; at += 4) {
            uint32_t first = word(v, at);
            for (unsigned i = 0; i < sizeof(witnesses) / sizeof(*witnesses); ++i) {
                const UiCodeWitness *p = witnesses + i;
                if (first != p->first[0] || !prefix(v, at, p->first, 4) || !witness(v, at, p))
                    continue;
                if (p->slot == UI_DISPLAY_CONTROL) {
                    if (literal(v, at) != 1003U)
                        continue;
                    uint32_t dispatch = branch(v, at + 0x1c), getter = branch(v, dispatch + 8);
                    const uint32_t shape[] = {0xe92d4010U, 0xe59f401cU, 0xe5940000U, 0xe3500000U};
                    if (!prefix(v, getter, shape, 4) || word(v, getter + 0x18) != 0xe3a0001fU)
                        continue;
                }
                if (!unique(api->function + p->slot, at))
                    return false;
            }
            if (first == 0x494f4452U && at >= begin + 12) {
                uint32_t descriptor = at - 12;
                const uint32_t shape[] = {0xe3e0300eU, 0xe92d4070U, 0xe5813008U, 0xe590001cU};
                if (word(v, descriptor + 0x2c) == 31U &&
                    prefix(v, word(v, descriptor + 0x38), shape, 4))
                    if (!unique(&api->display_descriptor, descriptor))
                        return false;
            }
        }
    }
    for (unsigned i = 0; i < UI_FUNCTIONS; ++i)
        if (!api->function[i])
            return false;
    if (!api->display_descriptor)
        return false;
    api->display_state = word(v, api->display_descriptor + 0x14);
    if (!v->allow_span(v->context, api->display_state, 0xdc + 2048, PORTABLE_DATA))
        return false;
    uint32_t io = word(v, api->display_descriptor + 0x38);
    if (branch(v, io + 0x140) != api->function[UI_DISPLAY_START] ||
        branch(v, io + 0x1e4) != api->function[UI_DISPLAY_STOP])
        return false;
    api->display_timer = literal(v, api->function[UI_DISPLAY_START] + 0x2c);
    api->display_hisr = literal(v, api->function[UI_DISPLAY_HISR_CREATE] + 0x24);
    api->busy_base = literal(v, api->function[UI_BUSY_STOP] + 4);
    if (!api->busy_base || api->busy_base != literal(v, api->function[UI_BUSY_START] + 4) ||
        !v->allow_span(v->context, api->busy_base, 16, PORTABLE_DATA))
        return false;
    api->busy_interval = literal(v, api->function[UI_BUSY_START] + 0x28);
    api->busy_callback = literal(v, api->function[UI_BUSY_START] + 0x2c);
    const uint32_t busy[] = {0xe92d4070U, 0xe59f40c8U, 0xe5943004U, 0xe3530000U};
    if (!prefix(v, api->busy_callback, busy, 4))
        return false;
    api->cursor_count = literal(v, api->function[UI_CURSOR_HIDE] + 8);
    if (api->cursor_count != word(v, api->function[UI_CURSOR_SHOW] + 0x210))
        return false;
    api->event_mask = api->cursor_count + 2;
    api->cursor_device_hide = branch(v, api->function[UI_CURSOR_HIDE] + 0x48);
    const uint32_t cursor[] = {0xe59f101cU, 0xe52de004U, 0xe24dd02cU, 0xe3a03000U};
    if (!prefix(v, api->cursor_device_hide, cursor, 4) ||
        literal(v, api->cursor_device_hide) != 1005U)
        return false;
    api->dim_disable = branch(v, api->function[UI_APD_ALLOW] + 0xb4);
    const uint32_t dim[] = {0xe92d4010U, 0xe59fc054U, 0xe59fe054U, 0xe24dd008U};
    if (!prefix(v, api->dim_disable, dim, 4))
        return false;
    bool event_setter = false;
    const uint32_t setter[] = {0xe59f3004U, 0xe1c300b0U, 0xe12fff1eU};
    const uint32_t tracking[] = {0xe59f300cU, 0xe1d300b0U, 0xe3c02008U, 0xe1c320b0U};
    /* Input code lives close to the checked cursor call targets. Limit this
     * second pass to the kernel range rather than searching heap objects. */
    uint32_t center = branch(v, api->function[UI_CURSOR_SHOW] + 0x198);
    if (center < begin + 0x10000U || center > end - 0x10000U)
        return false;
    for (uint32_t at = (center - 0x10000U) & ~3U; at < center + 0x10000U; at += 4) {
        if (prefix(v, at, setter, 3) && literal(v, at) == api->event_mask)
            event_setter = true;
        if (prefix(v, at, tracking, 4) && word(v, at + 0x10) == 0xe8bd8070U)
            if (!unique(&api->tracking_flags, literal(v, at)))
                return false;
    }
    return event_setter && api->tracking_flags &&
           v->allow_span(v->context, api->cursor_count, 4, PORTABLE_DATA) &&
           v->allow_span(v->context, api->tracking_flags, 4, PORTABLE_DATA) &&
           v->allow_span(v->context, api->busy_interval, 4, PORTABLE_DATA);
}
