#include "usb_audio_format.h"
#include <string.h>

const uint32_t usb_audio_rates[USB_AUDIO_RATE_COUNT] = {48000, 44100, 32000, 16000};
static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}
static uint32_t le24(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
}
static uint32_t le32(const uint8_t *p)
{
    return le24(p) | (uint32_t)p[3] << 24;
}

static bool valid_config(const uint8_t *c, size_t n)
{
    if (!c || n < 9 || c[0] != 9 || c[1] != 2 || le16(c + 2) != n || !c[5])
        return false;
    for (size_t p = 9; p < n; p += c[p]) {
        if (n - p < 2 || c[p] < 2 || c[p] > n - p)
            return false;
        unsigned type = c[p + 1], size = c[p];
        if ((type == 4 && size < 9) || (type == 5 && size < 7) || (type == 11 && size < 8) ||
            ((type == 0x24 || type == 0x25) && size < 3))
            return false;
    }
    return true;
}

const uint8_t *usb_audio_control_unit(const uint8_t *c, size_t n, uint8_t iface, uint8_t id)
{
    if (!valid_config(c, n) || !id)
        return NULL;
    bool active = false;
    const uint8_t *found = NULL;
    for (size_t p = 9; p < n; p += c[p]) {
        const uint8_t *d = c + p;
        if (d[1] == 4)
            active = d[2] == iface && !d[3] && d[5] == 1 && d[6] == 1;
        else if (active && d[1] == 0x24 && d[2] > 1 && d[0] >= 4 && d[3] == id) {
            if (found)
                return NULL; /* Duplicate entity IDs are ambiguous. */
            found = d;
        }
    }
    return found;
}

static bool control_interface(const uint8_t *c, size_t n, UsbAudioFormat *f)
{
    int association = -1, only = -1;
    unsigned count = 0;
    for (size_t p = 9; p < n; p += c[p]) {
        const uint8_t *d = c + p;
        if (d[1] == 11 && d[4] == 1 && f->interface_number >= d[2] &&
            (unsigned)f->interface_number < (unsigned)d[2] + d[3])
            association = d[2];
        if (d[1] == 4 && d[3] == 0 && d[5] == 1 && d[6] == 1 && d[7] == f->protocol) {
            only = d[2];
            ++count;
        }
    }
    bool active = false;
    for (size_t p = 9; p < n; p += c[p]) {
        const uint8_t *d = c + p;
        if (d[1] == 4) {
            active = d[3] == 0 && d[5] == 1 && d[6] == 1 && d[7] == f->protocol &&
                     (association >= 0 ? d[2] == association : count == 1 && d[2] == only);
            if (active)
                f->control_interface = d[2];
        } else if (active && d[1] == 0x24 && d[2] == 1) {
            if (f->protocol == 0x20)
                return d[0] >= 9 && le16(d + 3) == 0x200;
            if (d[0] < 8 || le16(d + 3) != 0x100 || d[0] < 8U + d[7])
                return false;
            for (unsigned i = 0; i < d[7]; ++i)
                if (d[8 + i] == f->interface_number)
                    return true;
        }
    }
    return false;
}

/* Follow signal connections from an output terminal back to the USB input.
 * Restrict mixer writes to a feature unit on the playback path, never a mic. */
static bool signal_path(const uint8_t *c, size_t n, UsbAudioFormat *f, uint8_t id, uint8_t *seen,
                        unsigned depth, uint8_t *feature)
{
    if (id == f->terminal_link)
        return true;
    if (!id || seen[id] || depth >= 24)
        return false;
    seen[id] = 1;
    const uint8_t *d = usb_audio_control_unit(c, n, f->control_interface, id);
    if (!d)
        return false;
    uint8_t sources[16];
    unsigned count = 0;
    switch (d[2]) {
    case 3:
        if (d[0] >= 9)
            sources[count++] = d[7];
        break;
    case 6:
        if (d[0] >= 6)
            sources[count++] = d[4];
        break;
    case 4:
    case 5:
        if (d[0] >= 5 && d[4] <= 16 && d[0] >= 5U + d[4]) {
            count = d[4];
            memcpy(sources, d + 5, count);
        }
        break;
    default:
        break;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (signal_path(c, n, f, sources[i], seen, depth + 1, feature)) {
            if (d[2] == 6)
                *feature = id;
            return true;
        }
    }
    return false;
}

static void feature_controls(const uint8_t *c, size_t n, UsbAudioFormat *f)
{
    for (size_t p = 9; p < n; p += c[p]) {
        const uint8_t *d = c + p;
        if (d[1] != 0x24 || d[0] < 9 || d[2] != 3 || le16(d + 4) == 0x0101)
            continue;
        if (usb_audio_control_unit(c, n, f->control_interface, d[3]) != d)
            continue;
        uint8_t seen[256] = {0}, feature = 0;
        if (!signal_path(c, n, f, d[3], seen, 0, &feature) || !feature)
            continue;
        const uint8_t *unit = usb_audio_control_unit(c, n, f->control_interface, feature);
        unsigned size = f->protocol == 0x20 ? 4 : unit[5];
        unsigned base = f->protocol == 0x20 ? 5 : 6;
        if (!size || size > 4 || unit[0] < base + size + 1)
            continue;
        f->feature_unit = feature;
        for (unsigned ch = 0; ch <= f->channels && base + size * (ch + 1) + 1 <= unit[0]; ++ch) {
            const uint8_t *controls = unit + base + size * ch;
            if (f->protocol == 0x20) {
                uint32_t bits = le32(controls);
                if ((bits & 3) == 3)
                    f->mute_mask |= 1U << ch;
                if (((bits >> 2) & 3) == 3)
                    f->volume_mask |= 1U << ch;
            } else {
                if (controls[0] & 1)
                    f->mute_mask |= 1U << ch;
                if (controls[0] & 2)
                    f->volume_mask |= 1U << ch;
            }
        }
        return;
    }
}

static bool complete_format(const uint8_t *c, size_t n, bool hs, UsbAudioFormat *f)
{
    if (!f->endpoint || !f->channels || !f->sample_bytes || f->sample_bits < 16 ||
        f->sample_bits > f->sample_bytes * 8 || !f->data_interval ||
        (hs ? f->data_interval > 4 : f->data_interval != 1))
        return false;
    if (f->sync_type == 1 && (!f->feedback_endpoint || !f->feedback_period_uframes))
        return false;
    if (f->protocol == 0) {
        if (f->frequency_control & 0x80)
            return false;
        if (!f->rate_mask)
            return false;
        if ((f->rate_mask & (f->rate_mask - 1)) && !f->frequency_control)
            return false;
        for (unsigned i = 0; i < USB_AUDIO_RATE_COUNT; ++i) {
            if (f->rate_mask & (1U << i)) {
                f->sample_rate = usb_audio_rates[i];
                break;
            }
        }
    }
    bool ac = control_interface(c, n, f);
    if (f->protocol == 0x20) {
        if (!ac)
            return false;
        const uint8_t *terminal =
            usb_audio_control_unit(c, n, f->control_interface, f->terminal_link);
        if (!terminal || terminal[2] != 2 || terminal[0] < 17 || le16(terminal + 4) != 0x0101 ||
            !terminal[7])
            return false;
        f->clock_root = terminal[7];
    }
    if (ac)
        feature_controls(c, n, f);
    uint32_t rate = f->sample_rate ? f->sample_rate : 16000;
    unsigned period = hs ? 1U << (f->data_interval - 1) : 8;
    unsigned frames = (rate * period + 7999) / 8000;
    if (f->max_packet < frames * f->channels * f->sample_bytes)
        return false;
    return true;
}

static unsigned score(const UsbAudioFormat *f)
{
    return (f->sync_type != 1 ? 1000 : 0) + (f->channels == 2 ? 100 : 0) +
           (4 - f->sample_bytes) * 10 + (f->protocol == 0 ? 1 : 0);
}

unsigned usb_audio_formats(const uint8_t *c, size_t n, bool hs, UsbAudioFormat *out, unsigned cap)
{
    if (!out || !cap || !valid_config(c, n))
        return 0;
    UsbAudioFormat f = {0};
    bool active = false, pcm = false, data_endpoint = false;
    unsigned count = 0;
    for (size_t p = 9; p <= n;) {
        const uint8_t *d = p < n ? c + p : NULL;
        unsigned len = d ? d[0] : 0;
        if (!d || d[1] == 4) {
            if (active && pcm && complete_format(c, n, hs, &f)) {
                unsigned insert = count;
                while (insert && score(&f) > score(out + insert - 1))
                    --insert;
                if (insert < cap) {
                    unsigned end = count < cap ? count : cap - 1;
                    for (unsigned i = end; i > insert; --i)
                        out[i] = out[i - 1];
                    out[insert] = f;
                    if (count < cap)
                        ++count;
                }
            }
            if (!d)
                break;
            memset(&f, 0, sizeof(f));
            f.configuration = c[5];
            f.interface_number = d[2];
            f.alternate = d[3];
            f.protocol = d[7];
            active = d[5] == 1 && d[6] == 2 && d[3] != 0 && (d[7] == 0 || d[7] == 0x20);
            pcm = data_endpoint = false;
        } else if (active && d[1] == 0x24) {
            if (d[2] == 1) {
                if (f.protocol == 0) {
                    if (len < 7)
                        return 0;
                    f.terminal_link = d[3];
                    pcm = le16(d + 5) == 1;
                } else {
                    if (len < 16)
                        return 0;
                    f.terminal_link = d[3];
                    pcm = d[5] == 1 && (le32(d + 6) & 1);
                    f.channels = d[10];
                }
            } else if (d[2] == 2) {
                if (f.protocol == 0) {
                    if (len < 8)
                        return 0;
                    f.channels = d[4];
                    f.sample_bytes = d[5];
                    f.sample_bits = d[6];
                    if (d[3] != 1)
                        pcm = false;
                    if (d[7] == 0) {
                        if (len < 14)
                            return 0;
                        uint32_t lo = le24(d + 8), hi = le24(d + 11);
                        for (unsigned i = 0; i < USB_AUDIO_RATE_COUNT; ++i)
                            if (lo <= usb_audio_rates[i] && hi >= usb_audio_rates[i])
                                f.rate_mask |= 1U << i;
                        /* A variable-frequency endpoint must expose a control. */
                        if (lo != hi)
                            f.frequency_control = 0x80;
                    } else {
                        if (len < 8U + 3U * d[7])
                            return 0;
                        for (unsigned j = 0; j < d[7]; ++j)
                            for (unsigned i = 0; i < USB_AUDIO_RATE_COUNT; ++i)
                                if (le24(d + 8 + 3 * j) == usb_audio_rates[i])
                                    f.rate_mask |= 1U << i;
                        if (d[7] > 1)
                            f.frequency_control = 0x80;
                    }
                } else {
                    if (len < 6)
                        return 0;
                    if (d[3] != 1)
                        pcm = false;
                    f.sample_bytes = d[4];
                    f.sample_bits = d[5];
                }
                if (f.channels < 1 || f.channels > 2 || f.sample_bytes < 2 || f.sample_bytes > 4 ||
                    (f.sample_bits != 16 && f.sample_bits != 24 && f.sample_bits != 32))
                    pcm = false;
            }
        } else if (active && d[1] == 5) {
            data_endpoint = false;
            unsigned transfer = d[3] & 3, sync = (d[3] >> 2) & 3, usage = (d[3] >> 4) & 3;
            unsigned packet = le16(d + 4);
            if (transfer == 1 && !(d[2] & 0x80) && (d[2] & 15) && sync && !usage &&
                !(packet & 0xf800) && (packet & 0x7ff) <= (hs ? 1024U : 1023U)) {
                if (f.endpoint) {
                    active = false;
                }
                f.endpoint = d[2];
                f.max_packet = (uint16_t)packet;
                f.sync_type = (uint8_t)sync;
                f.data_interval = d[6];
                data_endpoint = true;
                if (len >= 9 && d[8])
                    f.feedback_endpoint = d[8];
            } else if (transfer == 1 && (d[2] & 0x80) && (d[2] & 15) &&
                       (usage == 1 || f.feedback_endpoint == d[2] ||
                        (!usage && len >= 9 && d[7])) &&
                       packet >= 3 && packet <= 4) {
                if (f.feedback_endpoint && f.feedback_endpoint != d[2]) {
                    active = false;
                }
                f.feedback_endpoint = d[2];
                f.feedback_max_packet = (uint16_t)packet;
                f.feedback_interval = d[6];
                if (d[6] >= 1 && d[6] <= 11) {
                    unsigned period = hs ? 1U << (d[6] - 1) : 8U << (d[6] - 1);
                    if (!hs && len >= 9 && d[7] <= 7 && d[7] && (8U << d[7]) > period)
                        period = 8U << d[7];
                    if (period <= 1024)
                        f.feedback_period_uframes = (uint16_t)period;
                }
            }
        } else if (active && data_endpoint && d[1] == 0x25) {
            if (f.protocol == 0) {
                if (len < 7 || d[2] != 1)
                    return 0;
                bool needs = (f.frequency_control & 0x80) != 0;
                f.frequency_control = d[3] & 1;
                if (needs && !f.frequency_control)
                    pcm = false;
                f.lock_delay_units = d[4];
                f.lock_delay = le16(d + 5);
            } else {
                if (len < 8 || d[2] != 1)
                    return 0;
                f.lock_delay_units = d[5];
                f.lock_delay = le16(d + 6);
            }
        }
        p += len;
    }
    return count;
}

bool usb_audio_find_format(const uint8_t *c, size_t n, UsbAudioFormat *out)
{
    return usb_audio_formats(c, n, false, out, 1) != 0;
}

bool usb_audio_feedback_rate(const uint8_t *b, unsigned count, bool hs, uint32_t nominal,
                             uint32_t *rate_q16)
{
    if (!b || !rate_q16 || (count != 3 && count != 4) || !nominal || nominal > 48000)
        return false;
    uint64_t raw = count == 3 ? (uint64_t)le24(b) << 2 : le32(b);
    uint64_t expected = (uint64_t)nominal << 16;
    /* Full-speed units are samples/ms; high-speed units are samples/125us.
     * Some high-speed UAC1 devices retain the original full-speed convention. */
    for (unsigned i = 0; i < (hs ? 2U : 1U); ++i) {
        uint64_t rate = raw * 1000 * (hs && i == 0 ? 8 : 1);
        if (rate >= expected - expected / 100 && rate <= expected + expected / 100 &&
            rate <= UINT32_MAX) {
            *rate_q16 = (uint32_t)rate;
            return true;
        }
    }
    return false;
}
