/* Diagnostic descriptor/status reads plus journaled port resume/suspend.
 * Native enumeration owns addresses and hub power/reset. Full-speed children
 * behind a high-speed hub are deliberately
 * not guessed at: their endpoint-zero requests need a validated TT route. */
#define TOPOLOGY_DEVICES 16U
#define TOPOLOGY_PORTS 31U
typedef struct {
    uint8_t address, descriptor[18], hub[64], hub_size, ports;
    UsbAudioRoute route;
    uint8_t config[512];
    uint16_t config_size;
    uint32_t port_status[TOPOLOGY_PORTS];
    uint32_t port_valid;
    uint32_t resumed, resume_failed;
} TopologyDevice;
static TopologyDevice topology[TOPOLOGY_DEVICES];
static unsigned topology_count;
static unsigned topology_root_speed;
static bool topology_probe;
static char topology_summary[128];
static unsigned topology_restore_errors;
static unsigned topology_restore_skipped;
static const char *topology_handoff_reason = "none";
typedef struct {
    const char *stage;
    uint32_t before, first, after, elapsed_ms;
    uint8_t hub, port;
    unsigned status_reads;
    bool command_ok, status_valid, complete;
} HubWakeTrace;
static HubWakeTrace hub_wakes[32], hub_wake_overflow;
static unsigned hub_wake_count, hub_wake_dropped;
void usb_audio_probe_only(bool enabled)
{
    topology_probe = enabled;
}
static bool hub_delay(UsbAudio *u, unsigned ms)
{
    uint32_t start = u->now_ms();
    for (unsigned guard = 0; guard < 2000000; ++guard) {
        if (!probe_tick(u) || !connected())
            return false;
        if ((uint32_t)(u->now_ms() - start) >= ms)
            return true;
    }
    return false;
}
static bool resume_hub_port(UsbAudio *u, unsigned port, uint32_t before)
{
    HubWakeTrace *trace;
    if (hub_wake_count < 32)
        trace = hub_wakes + hub_wake_count++;
    else {
        ++hub_wake_dropped;
        trace = &hub_wake_overflow;
    }
    *trace = (HubWakeTrace){.hub=u->address, .port=(uint8_t)port,
                           .before=before, .stage="journal capacity"};
    uint32_t started = u->now_ms();
    bool ok = false;
    if (u->hub_undo_count >= sizeof(u->hub_undo) / sizeof(u->hub_undo[0]))
        goto done;
    /* Journal before the request: a failed status stage can still follow a
     * successful device-side write. No address, power, reset or config change. */
    u->hub_undo[u->hub_undo_count++] =
        (HubPortUndo){u->address, (uint8_t)port, u->max_control, u->tt_hub,
                      u->tt_port, u->high_speed, u->low_speed,   u->tt_multi};
    trace->stage = "clear suspend request";
    trace->command_ok = control(u, 0x23, 1, 2, port, NULL, 0, 100);
    if (!trace->command_ok)
        goto done;
    /* Retain the old20ms observation for diagnosis, without treating an
     * in-progress suspend bit as failure. Allow40ms interoperability margin
     * (also Linux USB_RESUME_TIMEOUT) before polling completion/recovery. */
    uint32_t signaled = u->now_ms();
    trace->stage = "resume signal wait";
    if (!hub_delay(u, 20))
        goto done;
    uint8_t first_status[4];
    trace->stage = "initial port status request";
    ++trace->status_reads;
    if (!control(u, 0xa3, 0, 0, port, first_status, 4, 100))
        goto done;
    trace->first = trace->after =
        (uint32_t)get16(first_status) | (uint32_t)get16(first_status + 2) << 16;
    trace->status_valid = true;
    if ((trace->first & 3U) != 3U || (trace->first & 0x18U)) {
        trace->stage = "port disconnected disabled or reset";
        goto done;
    }
    uint32_t elapsed = u->now_ms() - signaled;
    trace->stage = "resume signal margin";
    if (elapsed < 40U && !hub_delay(u, 40U - elapsed))
        goto done;
    for (unsigned polls = 0; polls < 64; ++polls) {
        if ((uint32_t)(u->now_ms() - started) >= 250U) {
            trace->stage = "resume status deadline";
            goto done;
        }
        uint8_t status[4];
        trace->stage = "port status request";
        ++trace->status_reads;
        if (!control(u, 0xa3, 0, 0, port, status, 4, 100))
            goto done;
        uint32_t value = (uint32_t)get16(status) | (uint32_t)get16(status + 2) << 16;
        trace->after = value;
        if ((value & 3U) != 3U || (value & 0x18U)) {
            trace->stage = "port disconnected disabled or reset";
            goto done;
        }
        if (!(value & 4U)) {
            trace->stage = "resume recovery wait";
            if (!hub_delay(u, 10))
                goto done;
            trace->stage = "complete";
            trace->complete = ok = true;
            goto done;
        }
        trace->stage = "suspend still pending";
        if (!hub_delay(u, 5))
            goto done;
    }
    trace->stage = "resume poll limit";
done:
    trace->elapsed_ms = u->now_ms() - started;
    return ok;
}
static void restore_hub_ports(UsbAudio *u)
{
    uint8_t address = u->address, max_control = u->max_control;
    bool high_speed = u->high_speed;
    bool low_speed = u->low_speed, tt_multi = u->tt_multi;
    uint8_t tt_hub = u->tt_hub, tt_port = u->tt_port;
    while (u->hub_undo_count && connected() && !u->unsafe_dma) {
        HubPortUndo v = u->hub_undo[--u->hub_undo_count];
        /* A journal describes the old attachment. Keep its path awake after
         * loss/change so native hub workers can detach and enumerate children;
         * suspending ancestors here can hide the changed subtree from them. */
        if (u->link_lost) {
            ++topology_restore_skipped;
            continue;
        }
        u->address = v.address;
        u->max_control = v.max_control;
        u->high_speed = v.high_speed;
        u->low_speed = v.low_speed;
        u->tt_hub = v.tt_hub;
        u->tt_port = v.tt_port;
        u->tt_multi = v.tt_multi;
        uint8_t status[4];
        bool restored = control(u, 0xa3, 0, 0, v.port, status, 4, 100);
        if (!restored) {
            ++topology_restore_errors;
            ++topology_restore_skipped;
            u->link_lost = true;
            topology_handoff_reason = "hub port unreadable";
            continue;
        }
        /* Ignore C_SUSPEND, which our own resume generated. Connection,
         * enable, overcurrent and reset changes belong to the native owner.
         * Never acknowledge/clear those events or re-suspend their path. */
        if ((get16(status) & 3U) != 3U || (get16(status) & 0x18U) ||
            (get16(status + 2) & 0x1bU)) {
            ++topology_restore_skipped;
            u->link_lost = true;
            topology_handoff_reason = "hub connection changed";
            continue;
        }
        if (restored && (get16(status) & 3U) == 3U && !(get16(status) & 4U)) {
            restored = control(u, 0x23, 3, 2, v.port, NULL, 0, 100) && hub_delay(u, 10) &&
                       control(u, 0xa3, 0, 0, v.port, status, 4, 100) && (get16(status) & 4U);
        }
        if (!restored)
            ++topology_restore_errors;
    }
    u->address = address;
    u->max_control = max_control;
    u->high_speed = high_speed;
    u->low_speed = low_speed;
    u->tt_hub = tt_hub;
    u->tt_port = tt_port;
    u->tt_multi = tt_multi;
    snprintf(topology_summary, sizeof(topology_summary),
             "USB topology: %u devices; restore errors %u", topology_count,
             topology_restore_errors);
}
static void capture_topology_device(UsbAudio *u, const uint8_t *descriptor)
{
    if (topology_count == TOPOLOGY_DEVICES)
        return;
    TopologyDevice *d = topology + topology_count++;
    memset(d, 0, sizeof(*d));
    d->address = u->address;
    d->route = (UsbAudioRoute){u->address, u->high_speed ? 3 : u->low_speed ? 1 : 2,
                               u->parent,  u->port_number,
                               u->tt_hub,  u->tt_port,
                               u->tt_multi};
    memcpy(d->descriptor, descriptor, 18);
    if (control(u, 0x80, 6, 0x200, 0, d->config, 9, 100)) {
        unsigned size = get16(d->config + 2);
        if (size >= 9 && size <= sizeof(d->config) &&
            control(u, 0x80, 6, 0x200, 0, d->config, size, 100)) {
            d->config_size = (uint16_t)size;
            u->max_power_ma = d->config[8] * 2;
        }
    }
    if (descriptor[4] != 9)
        return;
    if (!control(u, 0xa0, 6, 0x2900, 0, d->hub, 7, 100))
        return;
    unsigned size = d->hub[0];
    if (size < 7 || size > sizeof(d->hub) || d->hub[1] != 0x29 ||
        !control(u, 0xa0, 6, 0x2900, 0, d->hub, size, 100))
        return;
    d->hub_size = (uint8_t)size;
    d->ports = d->hub[2];
    for (unsigned port = 1; port <= d->ports && port <= TOPOLOGY_PORTS && probe_tick(u); ++port) {
        uint8_t status[4];
        if (control(u, 0xa3, 0, 0, port, status, 4, 100)) {
            d->port_status[port - 1] = (uint32_t)get16(status) | (uint32_t)get16(status + 2) << 16;
            d->port_valid |= 1U << (port - 1);
            /* The OS may suspend an unsupported audio child while keeping
             * its hub and storage siblings active. Playback needs the same
             * journaled port resume as a topology probe before EP0 can reply.
             * Restore the original suspend state when this owner closes. */
            if ((get16(status) & 7U) == 7U && !(get16(status) & 0x18U)) {
                if (resume_hub_port(u, port, d->port_status[port - 1]))
                    d->resumed |= 1U << (port - 1);
                else
                    d->resume_failed |= 1U << (port - 1);
            }
        }
        if (u->unsafe_dma || !connected())
            break;
    }
}
void usb_audio_hub_debug(FILE *file)
{
    unsigned resumed = 0, failed = 0;
    for (unsigned i = 0; i < topology_count; ++i) {
        for (uint32_t bits = topology[i].resumed; bits; bits &= bits - 1)
            ++resumed;
        for (uint32_t bits = topology[i].resume_failed; bits; bits &= bits - 1)
            ++failed;
    }
    fprintf(file, "usb_audio_hubs resumed_ports=%u failed_ports=%u restore_errors=%u\n",
            resumed, failed, topology_restore_errors);
    if (topology_restore_skipped)
        fprintf(file, "usb_audio_hub_handoff skipped_ports=%u reason=%s\n",
                topology_restore_skipped, topology_handoff_reason);
    for (unsigned i = 0; i < hub_wake_count; ++i) {
        const HubWakeTrace *w = hub_wakes + i;
        fprintf(file, "usb_audio_hub_wake hub=%u port=%u stage=%s command_ok=%u "
                "status_valid=%u status_reads=%u before=%08lx first=%08lx after=%08lx "
                "elapsed_ms=%lu complete=%u\n", w->hub, w->port, w->stage, w->command_ok,
                w->status_valid, w->status_reads, (unsigned long)w->before,
                (unsigned long)w->first, (unsigned long)w->after,
                (unsigned long)w->elapsed_ms, w->complete);
    }
    if (hub_wake_dropped)
        fprintf(file, "usb_audio_hub_wake omitted=%u\n", hub_wake_dropped);
}
void usb_audio_topology_debug(FILE *file)
{
    fprintf(file,"usb_audio_probe valid_routes=%08lx,%08lx,%08lx,%08lx candidates=%u last_fault=%s\n",
        (unsigned long)audio_probe_routes[0],(unsigned long)audio_probe_routes[1],
        (unsigned long)audio_probe_routes[2],(unsigned long)audio_probe_routes[3],
        audio_probe_count,last_transfer_fault);
    for(unsigned i=0;i<audio_probe_count;++i) {
        const AudioProbeTrace *p=audio_probes+i;
        fprintf(file,"usb_audio_probe_device addr=%u speed=%u parent=%u port=%u tt=%u/%u multi=%u "
            "attempts=%u vid=%04x pid=%04x stage=%s config_bytes=%u formats=%u descriptor=",
            p->route.address,p->route.speed,p->route.parent,p->route.port,p->route.tt_hub,
            p->route.tt_port,p->route.tt_multi,p->attempts,p->vendor,p->product,
            p->stage?p->stage:"none",p->config_bytes,p->formats);
        for(unsigned j=0;j<18;++j)fprintf(file,"%02x",p->descriptor[j]);
        fprintf(file,"\nusb_audio_probe_native addr=%u config_cached=%u selected=%u alternate_cached=%u",
            p->route.address,p->configuration_cached,p->selected_configuration,p->alternate_cached);
        fprintf(file,"\nusb_audio_probe_config addr=%u captured=%u hex=",p->route.address,p->captured_config);
        for(unsigned j=0;j<p->captured_config;++j)fprintf(file,"%02x",p->config[j]);
        fprintf(file,"\nusb_audio_probe_fault addr=%u %s\n",p->route.address,*p->fault?p->fault:"none");
    }
    fprintf(file,
            "usb_topology devices=%u probe_only=%u root_speed=%u restore_errors=%u tt_clears=%u "
            "tt_clear_errors=%u\n",
            topology_count, topology_probe, topology_root_speed, topology_restore_errors, tt_clears,
            tt_clear_errors);
    for (unsigned i = 0; i < topology_count; ++i) {
        TopologyDevice *d = topology + i;
        fprintf(file,
                "usb_topology_device address=%u vid=%04x pid=%04x class=%u protocol=%u "
                "configurations=%u config_bytes=%u descriptor=",
                d->address, get16(d->descriptor + 8), get16(d->descriptor + 10), d->descriptor[4],
                d->descriptor[6], d->descriptor[17], d->config_size);
        for (unsigned j = 0; j < 18; ++j)
            fprintf(file, "%02x", d->descriptor[j]);
        fprintf(file,
                "\nusb_topology_route address=%u speed=%u parent=%u port=%u tt_hub=%u tt_port=%u "
                "tt_multi=%u",
                d->address, d->route.speed, d->route.parent, d->route.port, d->route.tt_hub,
                d->route.tt_port, d->route.tt_multi);
        fprintf(file, "\nusb_topology_config address=%u hex=", d->address);
        for (unsigned j = 0; j < d->config_size; ++j)
            fprintf(file, "%02x", d->config[j]);
        fprintf(
            file,
            "\nusb_topology_hub address=%u ports=%u resumed=%08lx resume_failed=%08lx descriptor=",
            d->address, d->ports, (unsigned long)d->resumed, (unsigned long)d->resume_failed);
        for (unsigned j = 0; j < d->hub_size; ++j)
            fprintf(file, "%02x", d->hub[j]);
        fputc('\n', file);
        for (unsigned port = 1; port <= d->ports && port <= TOPOLOGY_PORTS; ++port) {
            uint32_t status = d->port_status[port - 1];
            fprintf(file,
                    "usb_topology_port hub=%u port=%u valid=%u raw=%08lx connected=%u enabled=%u "
                    "speed=%s\n",
                    d->address, port, !!(d->port_valid & (1U << (port - 1))), (unsigned long)status,
                    !!(status & 1), !!(status & 2),
                    !(status & 1)           ? "none"
                    : (status & (1U << 10)) ? "HS"
                    : (status & (1U << 9))  ? "LS"
                                            : "FS");
        }
    }
}
