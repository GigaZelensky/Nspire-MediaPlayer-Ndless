/* Private to usb_audio.c. Every changed, readable class control is journaled
 * so a failed candidate or normal shutdown can unwind it in reverse order. */
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}
static bool undo_controls(UsbAudio *u, unsigned target)
{
    bool ok = true;
    while (u->undo_count > target) {
        ControlUndo *v = u->undo + --u->undo_count;
        if (!control(u, v->type, v->request, v->value, v->index, v->data, v->size, 100))
            ok = false;
    }
    return ok;
}
static bool set_saved_control(UsbAudio *u, unsigned type, unsigned value, unsigned index,
                              const uint8_t *before, const uint8_t *after, unsigned size)
{
    if (size > 4 || u->undo_count >= CONTROL_UNDO_MAX)
        return false;
    if (!memcmp(before, after, size))
        return true;
    ControlUndo *v = u->undo + u->undo_count++;
    v->type = (uint8_t)type;
    v->request = 1;
    v->value = (uint16_t)value;
    v->index = (uint16_t)index;
    v->size = (uint8_t)size;
    memcpy(v->data, before, size);
    /* A failed status stage can still follow a successful device-side write. */
    return control(u, type, 1, value, index, (void *)after, size, 100);
}
static bool usable_rate(const UsbAudioFormat *f, bool hs, uint32_t rate)
{
    bool supported = false;
    for (unsigned i = 0; i < USB_AUDIO_RATE_COUNT; ++i)
        if (rate == usb_audio_rates[i])
            supported = true;
    unsigned period = hs ? 1U << (f->data_interval - 1) : 8;
    unsigned frames =
        f->sync_type == 1 ? (rate * period * 101 + 799999) / 800000 : (rate * period + 7999) / 8000;
    unsigned bytes = frames * f->channels * f->sample_bytes;
    /* With feedback, reserve room for a separate IN split and three complete
     * splits later in the same frame. Larger OUT-only packets use up to six. */
    return supported && bytes <= f->max_packet && bytes <= PACKET_STRIDE &&
           !(!hs && f->feedback_endpoint && bytes > 3U * 188U);
}
static bool range_supports(const uint8_t *ranges, unsigned count, uint32_t rate)
{
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *r = ranges + 12 * i;
        uint32_t low = get32(r), high = get32(r + 4), res = get32(r + 8);
        if (low <= rate && rate <= high && (!res || (rate - low) % res == 0))
            return true;
    }
    return false;
}
static bool configure_clock_source(UsbAudio *u, const uint8_t *source)
{
    if (source[0] < 8 || !(source[5] & 1))
        return false;
    unsigned index = (source[3] << 8) | u->format.control_interface;
    uint8_t current[4], chosen[4], range[2 + 64 * 12];
    if (!control(u, 0xa1, 1, 0x100, index, current, 4, 100))
        return false;
    uint32_t rate = get32(current);
    bool writable = (source[5] & 3) == 3;
    bool have_range = false;
    unsigned count = 0;
    if (writable && control(u, 0xa1, 2, 0x100, index, range, 2, 100)) {
        count = get16(range);
        if (count && count <= 64 && control(u, 0xa1, 2, 0x100, index, range, 2 + 12 * count, 100) &&
            get16(range) == count)
            have_range = true;
    }
    if (have_range) {
        rate = 0;
        for (unsigned i = 0; i < USB_AUDIO_RATE_COUNT; ++i) {
            if (usable_rate(&u->format, u->high_speed, usb_audio_rates[rate_index(i)]) &&
                range_supports(range + 2, count, usb_audio_rates[rate_index(i)])) {
                rate = usb_audio_rates[rate_index(i)];
                break;
            }
        }
    }
    if (!usable_rate(&u->format, u->high_speed, rate))
        return false;
    put32(chosen, rate);
    if (get32(current) != rate) {
        if (!writable || !set_saved_control(u, 0x21, 0x100, index, current, chosen, 4))
            return false;
        uint8_t verify[4];
        if (!control(u, 0xa1, 1, 0x100, index, verify, 4, 100) || get32(verify) != rate)
            return false;
    }
    if ((source[5] >> 2) & 1) {
        uint32_t start = u->now_ms();
        uint8_t valid = 0;
        for (unsigned attempts = 0; attempts < 256; ++attempts) {
            if (!control(u, 0xa1, 1, 0x200, index, &valid, 1, 100))
                return false;
            if (valid)
                break;
            if ((uint32_t)(u->now_ms() - start) >= 500)
                return false;
        }
        if (!valid)
            return false;
    }
    u->format.sample_rate = rate;
    return true;
}
static bool configure_clock(UsbAudio *u, uint8_t id, uint8_t *seen, unsigned depth)
{
    if (!id || seen[id] || depth >= 8)
        return false;
    const uint8_t *d =
        usb_audio_control_unit(u->config, u->config_length, u->format.control_interface, id);
    if (!d)
        return false;
    seen[id] = 1;
    if (d[2] == 10)
        return configure_clock_source(u, d);
    if (d[2] != 11 || d[0] < 8 || !d[4] || d[4] > 16 || d[0] < 7U + d[4])
        return false;
    unsigned index = (id << 8) | u->format.control_interface;
    uint8_t current;
    if (!control(u, 0xa1, 1, 0x100, index, &current, 1, 100) || !current || current > d[4])
        return false;
    unsigned control_bits = d[5 + d[4]];
    for (unsigned attempt = 0; attempt < d[4]; ++attempt) {
        uint8_t selection = (uint8_t)(1 + (current - 1 + attempt) % d[4]);
        if (selection != current && (control_bits & 3) != 3)
            continue;
        unsigned saved = u->undo_count;
        if (selection != current &&
            !set_saved_control(u, 0x21, 0x100, index, &current, &selection, 1))
            continue;
        uint8_t path[256];
        memcpy(path, seen, sizeof(path));
        if (configure_clock(u, d[4 + selection], path, depth + 1))
            return true;
        if (!undo_controls(u, saved))
            return false;
    }
    return false;
}
static bool configure_frequency(UsbAudio *u)
{
    if (u->format.protocol == 0x20) {
        uint8_t seen[256] = {0};
        if (!configure_clock(u, u->format.clock_root, seen, 0)) {
            u->error = "USB audio clock/rate negotiation failed";
            return false;
        }
        return true;
    }
    for (unsigned i = 0; i < USB_AUDIO_RATE_COUNT; ++i) {
        uint32_t rate = usb_audio_rates[rate_index(i)];
        if (!(u->format.rate_mask & (1U << rate_index(i))) ||
            !usable_rate(&u->format, u->high_speed, rate))
            continue;
        if (u->format.frequency_control) {
            uint8_t previous[3],
                next[3] = {(uint8_t)rate, (uint8_t)(rate >> 8), (uint8_t)(rate >> 16)};
            bool readable = control(u, 0xa2, 0x81, 0x100, u->format.endpoint, previous, 3, 100);
            if (readable) {
                if (!set_saved_control(u, 0x22, 0x100, u->format.endpoint, previous, next, 3))
                    continue;
            } else if (!control(u, 0x22, 1, 0x100, u->format.endpoint, next, 3, 100))
                continue;
        }
        u->format.sample_rate = rate;
        return true;
    }
    u->error = "USB audio has no usable sample rate";
    return false;
}
static bool configure_mixer(UsbAudio *u)
{
    UsbAudioFormat *f = &u->format;
    if (!f->feature_unit)
        return true;
    unsigned index = (f->feature_unit << 8) | f->control_interface;
    unsigned get = f->protocol == 0x20 ? 1 : 0x81;
    for (unsigned ch = 0; ch <= f->channels; ++ch) {
        uint8_t current[2], next[2] = {0};
        if (f->volume_mask & (1U << ch)) {
            int low = 0, high = 0, res = 0;
            bool range_ok = false;
            if (f->protocol == 0x20) {
                uint8_t r[8];
                if (control(u, 0xa1, 2, 0x200 | ch, index, r, 8, 100) && get16(r) >= 1) {
                    low = (int16_t)get16(r + 2);
                    high = (int16_t)get16(r + 4);
                    res = get16(r + 6);
                    range_ok = true;
                }
            } else {
                uint8_t a[2], b[2], r[2];
                if (control(u, 0xa1, 0x82, 0x200 | ch, index, a, 2, 100) &&
                    control(u, 0xa1, 0x83, 0x200 | ch, index, b, 2, 100) &&
                    control(u, 0xa1, 0x84, 0x200 | ch, index, r, 2, 100)) {
                    low = (int16_t)get16(a);
                    high = (int16_t)get16(b);
                    res = get16(r);
                    range_ok = true;
                }
            }
            if (range_ok && low <= high &&
                control(u, 0xa1, get, 0x200 | ch, index, current, 2, 100)) {
                int volume = high < 0 ? high : 0;
                if (volume < low)
                    volume = low;
                if (res)
                    volume = low + (volume - low) / res * res;
                put16(next, (uint16_t)volume);
                if (!set_saved_control(u, 0x21, 0x200 | ch, index, current, next, 2))
                    return false;
            }
        }
        if ((f->mute_mask & (1U << ch)) &&
            control(u, 0xa1, get, 0x100 | ch, index, current, 1, 100)) {
            next[0] = 0;
            if (!set_saved_control(u, 0x21, 0x100 | ch, index, current, next, 1))
                return false;
        }
    }
    return true;
}
static bool rollback_format(UsbAudio *u)
{
    bool ok = undo_controls(u, 0);
    if (u->alternate_set) {
        if (!control(u, 1, 11, u->previous_alternate, u->format.interface_number, NULL, 0, 100))
            ok = false;
        u->alternate_set = false;
    }
    if (u->configured) {
        if (!control(u, 0, 9, u->previous_configuration, 0, NULL, 0, 100))
            ok = false;
        u->configured = false;
    }
    return ok;
}
static bool find_device(UsbAudio *u)
{
    uint8_t desc[18], config_value;
    UsbAudioFormat *formats = u->alternatives;
    bool found = false;
    uint8_t seen[128] = {0};
    unsigned pass = 0;
    unsigned resumed_before;
probe_pass:
    resumed_before = u->hub_undo_count;
    for (unsigned addr = 1; addr < 128 && !found && probe_tick(u); ++addr) {
        active_probe = NULL;
        if (seen[addr])
            continue;
        u->address = (uint8_t)addr;
        u->high_speed = u->root_high_speed;
        u->low_speed = false;
        u->tt_hub = u->tt_port = u->parent = u->port_number = 0;
        u->tt_multi = false;
        if (route_lookup) {
            UsbAudioRoute route;
            if (!route_lookup(route_context, (uint8_t)addr, &route) || route.address != addr ||
                route.speed < 1 || route.speed > 3 || route.tt_hub > 127 || route.tt_port > 127 ||
                (!!route.tt_hub != !!route.tt_port))
                continue;
            if (u->root_high_speed && route.speed != 3 && !route.tt_hub)
                continue;
            if (!topology_probe && route.speed == 1)
                continue; /* LS has no ISO. */
            u->high_speed = route.speed == 3;
            u->low_speed = route.speed == 1;
            u->tt_hub = route.tt_hub;
            u->tt_port = route.tt_port;
            u->tt_multi = route.tt_multi;
            u->parent = route.parent;
            u->port_number = route.port;
        }
        audio_probe_routes[addr / 32] |= 1U << (addr % 32);
        for (unsigned i = 0; i < audio_probe_count; ++i)
            if (audio_probes[i].route.address == addr)
                active_probe = audio_probes + i;
        if (!active_probe && audio_probe_count < 16)
            active_probe = audio_probes + audio_probe_count++;
        if (active_probe) {
            active_probe->route = (UsbAudioRoute){u->address,
                u->high_speed ? 3 : u->low_speed ? 1 : 2, u->parent, u->port_number,
                u->tt_hub, u->tt_port, u->tt_multi};
            ++active_probe->attempts;
            active_probe->stage = "device header";
        }
        u->max_control = u->high_speed ? 64 : 8;
        bool header_ok = control(u, 0x80, 6, 0x100, 0, desc, 8, route_lookup ? 100 : 6);
        if (!header_ok && route_lookup && u->tt_hub && !u->unsafe_dma && connected() &&
            probe_tick(u))
            header_ok = control(u, 0x80, 6, 0x100, 0, desc, 8, 100);
        if (!header_ok) {
            if (u->unsafe_dma || !connected())
                break;
            continue;
        }
        if (desc[0] != 18 || desc[1] != 1 ||
            (desc[7] != 8 && desc[7] != 16 && desc[7] != 32 && desc[7] != 64))
            continue;
        u->max_control = desc[7];
        if (active_probe) active_probe->stage = "device descriptor";
        if (!control(u, 0x80, 6, 0x100, 0, desc, 18, 100) || !desc[17])
            continue;
        seen[addr] = 1;
        u->vendor = get16(desc + 8);
        u->product = get16(desc + 10);
        if (active_probe) {
            active_probe->vendor = u->vendor;
            active_probe->product = u->product;
            memcpy(active_probe->descriptor, desc, 18);
        }
        if (topology_probe || desc[4] == 9) {
            if (active_probe) active_probe->stage = "hub/topology";
            seen[addr] = 1;
            capture_topology_device(u, desc);
            if (u->unsafe_dma || !connected())
                break;
            if (topology_probe) {
                snprintf(topology_summary, sizeof(topology_summary),
                         "USB topology: %u devices; restore errors 0", topology_count);
                u->error = topology_summary;
                continue;
            }
            continue;
        }
        if (active_probe) active_probe->stage = "get configuration";
        if (!control(u, 0x80, 8, 0, 0, &config_value, 1, 100)) {
            UsbDeviceState native;
            if ((!u->last_control_stalled && !u->last_control_short) || !state_lookup) continue;
            if (active_probe) active_probe->stage = "native selected configuration lookup";
            if (!state_lookup(state_context, u->address, -1, &native)) continue;
            config_value = native.configuration;
            if (active_probe) active_probe->configuration_cached = true;
        }
        if (active_probe) active_probe->selected_configuration = config_value;
        for (unsigned ci = 0; ci < desc[17] && !found; ++ci) {
            if (active_probe) active_probe->stage = "configuration header";
            if (!control(u, 0x80, 6, 0x200 | ci, 0, u->config, 9, 100))
                continue;
            unsigned size = get16(u->config + 2);
            if (active_probe) active_probe->config_bytes = (uint16_t)size;
            /* Keep the OS's selected configuration intact. Unconfigured
             * devices can select any compatible configuration and restore 0. */
            if (size < 9 || size > sizeof(u->config) ||
                (config_value && config_value != u->config[5]))
                continue;
            if (active_probe) active_probe->stage = "configuration descriptor";
            if (!control(u, 0x80, 6, 0x200 | ci, 0, u->config, size, 100))
                continue;
            u->config_length = size;
            u->max_power_ma = u->config[8] * 2;
            unsigned count =
                usb_audio_formats(u->config, size, u->high_speed, formats, USB_AUDIO_MAX_FORMATS);
            if (active_probe) {
                active_probe->formats = count;
                active_probe->stage = "parse playback formats";
                active_probe->captured_config = size < sizeof(active_probe->config)
                    ? size : sizeof(active_probe->config);
                memcpy(active_probe->config, u->config, active_probe->captured_config);
            }
            for (unsigned i = 0; i < count; ++i) {
                u->format = formats[i];
                u->previous_configuration = config_value;
                if (active_probe) active_probe->stage = "select configuration";
                if (!config_value) {
                    if (!control(u, 0, 9, u->format.configuration, 0, NULL, 0, 100))
                        break;
                    u->configured = true;
                }
                if (active_probe) active_probe->stage = "get alternate";
                /* A native USB client already has a selected-interface record.
                 * Use that observed state before issuing a redundant query:
                 * some otherwise usable devices STALL or never answer it.
                 * We do not reinterpret a timed-out request as alternate 0. */
                bool alternate_ok = false;
                UsbDeviceState native;
                if (u->configured) {
                    /* Our successful SET_CONFIGURATION just initialized every
                     * alternate to zero under the exclusive lease. */
                    u->previous_alternate = 0;
                    alternate_ok = true;
                } else if (state_lookup && state_lookup(state_context, u->address,
                           u->format.interface_number, &native) && native.alternate_known &&
                           native.configuration == u->format.configuration) {
                    u->previous_alternate = native.alternate;
                    alternate_ok = true;
                }
                if (alternate_ok && active_probe) active_probe->alternate_cached = true;
                if (!alternate_ok)
                    alternate_ok = control(u, 0x81, 10, 0, u->format.interface_number,
                                           &u->previous_alternate, 1, 100);
                if (!alternate_ok || u->previous_alternate != 0) {
                    u->error = "USB audio interface unavailable or busy";
                    rollback_format(u);
                    break;
                }
                if (active_probe) active_probe->stage = "select alternate";
                if (control(u, 1, 11, u->format.alternate, u->format.interface_number, NULL, 0,
                            100)) {
                    u->alternate_set = true;
                    if (active_probe) active_probe->stage = "configure sample rate";
                    bool frequency_ok = configure_frequency(u);
                    if (frequency_ok && active_probe) active_probe->stage = "configure playback mixer";
                    if (frequency_ok && configure_mixer(u)) {
                        if (active_probe) active_probe->stage = "ready";
                        found = true;
                        break;
                    }
                }
                if (!rollback_format(u)) {
                    u->error = "USB audio setup rollback failed";
                    goto done;
                }
            }
        }
    }
    /* Address reuse can put a child before its parent in numeric order.
     * Repeat discovery after waking new ports; never assume increasing IDs. */
    if (!found && u->hub_undo_count > resumed_before && ++pass < 7 && !u->unsafe_dma &&
        connected() && probe_tick(u))
        goto probe_pass;
done:
    return found;
}
