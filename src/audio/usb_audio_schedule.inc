/* One OUT iTD and optional feedback iTD per USB frame. High-speed endpoints
 * use multiple microframe transactions. Fractional rates and feedback share
 * a rational sample accumulator; video uses positions of transmitted samples. */
uint64_t usb_audio_position(UsbAudio *u)
{
    if (!u || !u->running)
        return u ? u->origin : 0;
    if (!update_frame(u) || u->frame <= u->start)
        return u->origin;
    if (u->frame >= u->next)
        return u->generated_position;
    return u->frame_positions[(u->frame - u->start - 1) & (RING - 1)];
}
static void init_itd(UsbAudio *u, Itd *itd, uint8_t *buffer, unsigned endpoint, unsigned packet)
{
    memset(itd, 0, sizeof(*itd));
    itd->next = 1;
    uintptr_t page = (uintptr_t)buffer & ~(uintptr_t)4095;
    itd->buffer[0] = (uint32_t)page | u->address | ((endpoint & 15) << 8);
    itd->buffer[1] = (uint32_t)(page + 4096) | packet | ((endpoint & 0x80) ? 1U << 11 : 0);
    itd->buffer[2] = (uint32_t)(page + 8192) | 1;
}
static void pcm_sample(uint8_t *dst, int sample, unsigned bytes)
{
    /* USB PCM is left justified within its subslot. Multiplication avoids
     * undefined left-shifts of negative signed samples. */
    uint32_t value = (uint32_t)((int64_t)sample * (1U << (8 * (bytes - 2))));
    for (unsigned i = 0; i < bytes; ++i)
        dst[i] = (uint8_t)(value >> (8 * i));
}
static bool feedback_frame(UsbAudio *u, unsigned sequence)
{
    unsigned period = u->format.feedback_period_uframes;
    return u->format.feedback_endpoint && period && (period <= 8 || sequence % (period / 8) == 0);
}
#include "usb_audio_split.inc"
static unsigned volume_gain_q15(unsigned volume)
{
    if(volume>100)volume=100;
    /* A quadratic loudness taper gives useful quiet settings. Unity at 100%
     * is exact; compute only when the slider changes, not for each sample. */
    return (volume*volume*32768U+5000U)/10000U;
}
static int pcm_gain(int sample,unsigned gain_q15)
{ return sample*(int)gain_q15/32768; }
static bool pack_frame(UsbAudio *u, unsigned sequence)
{
    unsigned slot = sequence & (RING - 1);
    UsbAudioFormat *f = &u->format;
    uint8_t *packet = u->packets + slot * PACKET_STRIDE;
    Itd *itd = u->itd + slot;
    init_itd(u, itd, packet, f->endpoint, f->max_packet);
    unsigned period = u->high_speed ? 1U << (f->data_interval - 1) : 8;
    unsigned used = 0;
    for (unsigned micro = 0; micro < 8; micro += period) {
        u->packet_phase += (uint64_t)u->rate_q16 * period;
        unsigned frames = (unsigned)(u->packet_phase / (8000ULL << 16));
        u->packet_phase %= 8000ULL << 16;
        unsigned bytes = frames * f->channels * f->sample_bytes;
        if (bytes > f->max_packet || used + bytes > PACKET_STRIDE) {
            u->error = "USB audio packet exceeds device capacity";
            return false;
        }
        for (unsigned i = 0; i < frames; ++i) {
            int16_t sample[2] = {0, 0};
            if (sequence >= u->prime_frames) {
                audio_buffer_channels(u->stream, u->generated_position, sample);
                u->generated_position += u->step;
            }
            for (unsigned channel = 0; channel < f->channels; ++channel)
                pcm_sample(packet + used + (i * f->channels + channel) * f->sample_bytes,
                           pcm_gain(f->channels == 1 ? (sample[0] + sample[1]) / 2 : sample[channel],
                               u->gain_q15),
                           f->sample_bytes);
        }
        itd->transaction[micro] =
            ACTIVE | (bytes << 16) | (((uint32_t)(uintptr_t)packet + used) & 4095);
        used += bytes;
    }
    u->frame_positions[slot] = u->generated_position;
    if (u->stream->failed) {
        u->error = u->stream->pcm_missed ? "Decoded audio buffer underrun"
                                         : "Audio data read or decode failed";
        return false;
    }
    dma_clean(packet, PACKET_STRIDE);
    if (u->tt_hub) {
        init_sitd(u, u->sitd + slot, packet, f->endpoint, used);
        dma_clean(u->sitd + slot, sizeof(Sitd));
    } else
        dma_clean(itd, sizeof(*itd));
    if (feedback_frame(u, sequence)) {
        uint8_t *buffer = u->feedback + slot * FEEDBACK_STRIDE;
        Itd *feedback = u->feedback_itd + slot;
        memset(buffer, 0, FEEDBACK_STRIDE);
        init_itd(u, feedback, buffer, f->feedback_endpoint, f->feedback_max_packet);
        feedback->next = (uint32_t)(uintptr_t)itd;
        unsigned interval = f->feedback_period_uframes;
        for (unsigned micro = 0; micro < 8; micro += interval) {
            unsigned offset = micro * 4;
            feedback->transaction[micro] = ACTIVE | ((unsigned)f->feedback_max_packet << 16) |
                                           (((uint32_t)(uintptr_t)buffer + offset) & 4095);
        }
        dma_clean(buffer, FEEDBACK_STRIDE);
        if (u->tt_hub) {
            Sitd *split = u->feedback_sitd + slot;
            init_sitd(u, split, buffer, f->feedback_endpoint, f->feedback_max_packet);
            split->next = (uint32_t)(uintptr_t)(u->sitd + slot) | 4U;
            dma_clean(split, sizeof(*split));
        } else
            dma_clean(feedback, sizeof(*feedback));
    }
    return true;
}
static uint32_t frame_link(UsbAudio *u, unsigned sequence)
{
    unsigned slot = sequence & (RING - 1);
    if (u->tt_hub)
        return (uint32_t)(uintptr_t)(feedback_frame(u, sequence) ? u->feedback_sitd + slot
                                                                 : u->sitd + slot) |
               4U;
    return (uint32_t)(uintptr_t)(feedback_frame(u, sequence) ? u->feedback_itd + slot
                                                             : u->itd + slot);
}
static void read_feedback(UsbAudio *u, unsigned sequence)
{
    unsigned slot = sequence & (RING - 1);
    Itd *itd = u->feedback_itd + slot;
    uint8_t *buffer = u->feedback + slot * FEEDBACK_STRIDE;
    if (u->tt_hub) {
        Sitd *td = u->feedback_sitd + slot;
        uint32_t rate;
        dma_invalidate(td, sizeof(*td));
        dma_invalidate(buffer, FEEDBACK_STRIDE);
        unsigned remaining = (td->state >> 16) & 1023U;
        if (!(td->state & 0xfcU) && remaining <= u->format.feedback_max_packet &&
            usb_audio_feedback_rate(buffer, u->format.feedback_max_packet - remaining, false,
                                    u->format.sample_rate, &rate)) {
            u->rate_q16 = rate;
            u->last_feedback_frame = u->start + sequence + 1;
        }
        return;
    }
    dma_invalidate(itd, sizeof(*itd));
    dma_invalidate(buffer, FEEDBACK_STRIDE);
    for (unsigned micro = 0; micro < 8; micro += u->format.feedback_period_uframes) {
        uint32_t token = itd->transaction[micro], rate;
        if (!(token & 0xf0000000U) &&
            usb_audio_feedback_rate(buffer + micro * 4, (token >> 16) & 4095, u->high_speed,
                                    u->format.sample_rate, &rate)) {
            u->rate_q16 = rate;
            u->last_feedback_frame = u->start + sequence + 1;
        }
    }
}
bool usb_audio_service(UsbAudio *u)
{
    if (!u || !u->running)
        return false;
    if (!connected() || (STS & (1U << 4))) {
        u->error = "USB audio disconnected or controller error";
        return false;
    }
    if (!update_frame(u))
        return false;
    unsigned period = u->high_speed ? 1U << (u->format.data_interval - 1) : 8;
    while (u->checked < u->frame && u->checked < u->next) {
        unsigned sequence = u->checked - u->start;
        if (u->tt_hub && !split_completed(u, sequence))
            return false;
        Itd *itd = u->itd + (sequence & (RING - 1));
        dma_invalidate(itd, sizeof(*itd));
        for (unsigned micro = 0; !u->tt_hub && micro < 8; micro += period) {
            if (itd->transaction[micro] & 0xf0000000U) {
                snprintf(last_transfer_fault, sizeof(last_transfer_fault),
                         "token=%08lx micro=%u sequence=%u frame=%lu checked=%lu next=%lu "
                         "cmd=%08lx sts=%08lx port=%08lx",
                         (unsigned long)itd->transaction[micro], micro, sequence,
                         (unsigned long)u->frame, (unsigned long)u->checked, (unsigned long)u->next,
                         (unsigned long)CMD, (unsigned long)STS, (unsigned long)PORT);
                u->error = "USB isochronous transfer failed";
                return false;
            }
        }
        if (feedback_frame(u, sequence))
            read_feedback(u, sequence);
        unsigned slot = (u->list_bias + u->checked) & (RING - 1);
        u->list[slot] = 1;
        dma_clean(u->list + slot, 4);
        ++u->checked;
    }
    if (u->next <= u->frame + 1) {
        u->error = "USB audio buffer underrun";
        return false;
    }
    if (u->format.sync_type == 1 && u->frame - u->last_feedback_frame > 600 && u->frame > 600) {
        u->error = "USB audio feedback missing or invalid";
        return false;
    }
    while (u->next < u->frame + LEAD) {
        unsigned sequence = u->next - u->start;
        if (!pack_frame(u, sequence))
            return false;
        unsigned slot = (u->list_bias + u->next) & (RING - 1);
        u->list[slot] = frame_link(u, sequence);
        dma_clean(u->list + slot, 4);
        ++u->next;
    }
    return true;
}
bool usb_audio_start(UsbAudio *u, AudioBuffer *s, uint64_t position, unsigned numerator,
                     unsigned denominator, unsigned volume)
{
    if (!u || !s || !s->pcm || !numerator || !denominator || !usb_audio_pause(u))
        return false;
    u->stream = s;
    u->origin = u->generated_position = position;
    u->volume = volume > 100 ? 100 : volume;
    u->gain_q15 = volume_gain_q15(u->volume);
    u->step = (((uint64_t)s->sample_rate << 32) * numerator) /
              ((uint64_t)u->format.sample_rate * denominator);
    u->rate_q16 = u->format.sample_rate << 16;
    u->packet_phase = 0;
    u->prime_frames = 0;
    if (u->format.lock_delay_units == 1)
        u->prime_frames = u->format.lock_delay;
    else if (u->format.lock_delay_units == 2)
        u->prime_frames = ((uint32_t)u->format.lock_delay * 1000 + u->format.sample_rate - 1) /
                          u->format.sample_rate;
    if (u->prime_frames > 2000) {
        u->error = "USB audio clock settling delay too long";
        return false;
    }
    for (unsigned i = 0; i < LEAD; ++i)
        if (!pack_frame(u, i))
            return false;
    u->last_frame = u->list_bias = (FRINDEX >> 3) & 2047;
    u->last_ms = u->now_ms();
    u->frame = 0;
    u->start = u->checked = START_DELAY;
    u->next = START_DELAY + LEAD;
    u->last_feedback_frame = 0;
    for (unsigned i = 0; i < LEAD; ++i)
        u->list[(u->list_bias + u->start + i) & (RING - 1)] = frame_link(u, i);
    dma_clean(u->list, 4096);
    u->running = true;
    CMD |= PSE;
    if (!wait_status(u, PSS, PSS)) {
        usb_audio_pause(u);
        return false;
    }
    return true;
}
