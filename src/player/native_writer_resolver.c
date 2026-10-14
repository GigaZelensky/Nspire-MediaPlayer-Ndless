#include "native_writer_resolver.h"
#include <string.h>

typedef struct { uint32_t bytes, hash, first, mask, prefix[4]; } WriterWitness;
#include "native_writer_patterns.h"
enum { WR_STACK = WR_EXPORTS, WR_DISPATCH, WR_CONTROL, WR_CALLBACK, WR_SPI_GET, WR_SCHEDULER };

static bool span(const PortableView *v, uint32_t a, uint32_t n, unsigned kind)
{
    uint32_t low = kind == PORTABLE_CODE ? v->code_begin : v->ram_begin;
    uint32_t high = kind == PORTABLE_CODE ? v->code_end : v->ram_end;
    return !(a & 3U) && low < high && n && n <= high - low && a >= low && a <= high - n &&
           v->allow_span(v->context, a, n, kind);
}
static bool read_word(const PortableView *v, uint32_t a, uint32_t *value)
{
    return span(v, a, 4, PORTABLE_DATA) && v->read_word(v->context, a, value);
}
static uint32_t word(const PortableView *v, uint32_t a)
{
    uint32_t value = 0;
    read_word(v, a, &value);
    return value;
}
static bool literal(const PortableView *v, uint32_t at, uint32_t op, uint32_t *value)
{
    if ((op & 0x0f7f0000U) != 0x051f0000U) return false;
    int64_t address = (int64_t)at + 8 + ((op & 0x800000U) ? (int)(op & 4095U) : -(int)(op & 4095U));
    return address >= 0 && address <= UINT32_MAX && read_word(v, (uint32_t)address, value);
}
static uint32_t load_literal(const PortableView *v, uint32_t at)
{
    uint32_t value = 0;
    literal(v, at, word(v, at), &value);
    return value;
}
static uint32_t branch(const PortableView *v, uint32_t at, uint32_t op)
{
    int32_t displacement = (int32_t)(op << 8) >> 6;
    int64_t target = (int64_t)at + 8 + displacement;
    return target >= 0 && target <= UINT32_MAX && span(v, (uint32_t)target, 4, PORTABLE_CODE)
        ? (uint32_t)target : 0;
}
static uint32_t prefix_word(uint32_t op)
{
    if ((op & 0x0e000000U) == 0x0a000000U) return op & 0xff000000U;
    if ((op & 0x0f7f0000U) == 0x051f0000U) return op & 0xff7ff000U;
    return op;
}
static bool witness(const PortableView *v, uint32_t at, unsigned role)
{
    const WriterWitness *p = writer_witnesses + role;
    if (!span(v, at, p->bytes, PORTABLE_CODE) || (word(v, at) & p->mask) != p->first)
        return false;
    /* Reject common prologues before hashing whole native functions. This
     * keeps cold journal discovery from walking millions of mapped words. */
    const uint32_t *code = v->code_words ? v->code_words(v->context, at, p->bytes) : NULL;
    for (unsigned i = 0; i < 4 && i * 4 < p->bytes; ++i)
        if (prefix_word(code ? code[i] : word(v, at + i * 4)) != p->prefix[i]) return false;
    uint32_t hash = 2166136261U;
    for (uint32_t offset = 0; offset < p->bytes; offset += 4) {
        uint32_t op, extra = 0;
        if (code) op = code[offset / 4];
        else if (!read_word(v, at + offset, &op)) return false;
        bool has_literal = false;
        if ((op & 0x0e000000U) == 0x0a000000U) {
            uint32_t target = branch(v, at + offset, op);
            if (!target) return false;
            if ((op & 0x01000000U) || target < at || target >= at + p->bytes)
                op &= 0xff000000U;
        } else if ((op & 0x0f7f0000U) == 0x051f0000U) {
            if (!literal(v, at + offset, op, &extra)) return false;
            if (extra >= 0x10000000U && extra < 0x14000000U) extra = 0x10000000U;
            op &= 0xff7ff000U;
            has_literal = true;
        }
        hash = (hash ^ op) * 16777619U;
        if (has_literal) hash = (hash ^ extra) * 16777619U;
    }
    return hash == p->hash;
}
static bool unique(uint32_t *out, uint32_t value)
{
    if (!value || (*out && *out != value)) return false;
    *out = value;
    return true;
}
bool native_writer_resolve_progress(const PortableView *v, const uint32_t anchors[WR_EXPORTS], NativeWriterApi *out,
                                    bool (*progress)(void *), void *context)
{
    if (!v || !anchors || !out || !v->allow_span || !v->read_word) return false;
    memset(out, 0, sizeof(*out));
    NativeWriterApi api = {0};
    for (unsigned i = 0; i < WR_EXPORTS; ++i) {
        if (!witness(v, anchors[i], i)) return false;
        api.function[i] = anchors[i];
    }
    api.task_global = load_literal(v, anchors[WR_TASK]);
    if (!span(v, api.task_global, 4, PORTABLE_DATA) ||
        branch(v, anchors[WR_ERRNO] + 4, word(v, anchors[WR_ERRNO] + 4)) != anchors[WR_TASK])
        return false;
    uint32_t stack = 0, dispatch = 0, getter = 0, scheduler = 0;
    if (v->code_begin != 0x10000000U || v->code_end < 0x11000000U) return false;
    for (uint32_t page = v->code_begin; page < 0x11000000U; page += 4096) {
        if ((page & 0x7fffU) == 0 && progress && !progress(context)) return false;
        if (!span(v, page, 4096, PORTABLE_CODE)) continue;
        const uint32_t *code = v->code_words ? v->code_words(v->context, page, 4096) : NULL;
        for (uint32_t at = page; at < page + 4096; at += 4) {
            uint32_t op = code ? code[(at - page) / 4] : word(v, at);
            const unsigned roles[] = {WR_STACK, WR_DISPATCH, WR_SPI_GET, WR_SCHEDULER};
            for (unsigned i = 0; i < 4; ++i) {
                unsigned role = roles[i];
                if ((op & writer_witnesses[role].mask) != writer_witnesses[role].first ||
                    !witness(v, at, role)) continue;
                if (role == WR_STACK) {
                    if (load_literal(v, at) == api.task_global && !unique(&stack, at)) return false;
                } else if (role == WR_DISPATCH) {
                    uint32_t target = branch(v, at + 12, word(v, at + 12));
                    if (witness(v, target, WR_STACK) && load_literal(v, target) == api.task_global &&
                        !unique(&dispatch, at)) return false;
                } else if (role == WR_SPI_GET) {
                    uint32_t global = load_literal(v, at + 4), device = word(v, global);
                    if (span(v, device, 132, PORTABLE_DATA) &&
                        witness(v, word(v, device + 72), WR_CONTROL) &&
                        witness(v, word(v, device + 112), WR_CALLBACK) && !unique(&getter, at))
                        return false;
                } else if (!unique(&scheduler, at)) return false;
            }
        }
    }
    if (!stack || !dispatch || !getter || !scheduler) return false;
    api.interrupt_policy = load_literal(v, scheduler);
    api.spi_global = load_literal(v, getter + 4);
    uint32_t device = word(v, api.spi_global);
    api.spi_control = word(v, device + 72);
    api.spi_callback = word(v, device + 112);
    api.spi_context = word(v, device + 76);
    if (!span(v, api.interrupt_policy, 4, PORTABLE_DATA) ||
        !span(v, api.spi_global, 4, PORTABLE_DATA) || !span(v, api.spi_context, 4, PORTABLE_DATA) ||
        word(v, api.spi_context) != 0xb8000000U || word(v, device + 64) != device ||
        word(v, device + 100) != 16U) return false;
    *out = api;
    return true;
}

bool native_writer_resolve(const PortableView *v, const uint32_t anchors[WR_EXPORTS], NativeWriterApi *out)
{
    return native_writer_resolve_progress(v, anchors, out, NULL, NULL);
}
