/* Isochronous OUT completion confirms transmission, not a reply from the
 * selected DAC. A vanished child behind a connected hub can otherwise leave
 * playback advancing silently. GET_DESCRIPTOR is already known to work for
 * this selected device; GET_CONFIGURATION can STALL on usable adapters.
 *
 * This query neither changes audio controls nor stops the periodic schedule.
 * It owns endpoint-zero DMA only for this bounded foreground call. The caller
 * first proves storage quiescent; ASE/ASS are rechecked before publication.
 * Native USB workers remain excluded by the existing host-session lease. */
bool usb_audio_check_health(UsbAudio *u)
{
    if (!u || !u->running)
        return true;
    uint32_t now = u->now_ms();
    if ((int32_t)(now - u->health_at) < 0 || (CMD & ASE) || (STS & ASS))
        return true;
    uint32_t previous_async = ASYNC;
    uint8_t descriptor[18];
    ++u->health_checks;
    u->health_check = true;
    /* Use the setup path's existing reply deadline, rather than rejecting a
     * previously supported adapter just because its firmware replies slowly.
     * The 192 ms periodic runway continues playing during this bounded wait. */
    bool replied = control(u, 0x80, 6, 0x100, 0, descriptor, sizeof(descriptor), 100);
    u->health_check = false;
    uint32_t elapsed = u->now_ms() - now;
    if (elapsed > u->health_max_ms)
        u->health_max_ms = elapsed;
    /* A stopped queue is no longer reachable. Preserve the previous idle
     * list for the next storage request, including after a safe timeout. */
    if (!u->unsafe_dma && !(CMD & ASE) && !(STS & ASS)) {
        ASYNC = previous_async;
        io_barrier();
    }
    bool same = replied && descriptor[0] == 18 && descriptor[1] == 1 &&
                descriptor[7] == u->max_control && get16(descriptor + 8) == u->vendor &&
                get16(descriptor + 10) == u->product;
    if (same) {
        u->health_failures = 0;
        u->health_at = now + 1000U;
        return true;
    }
    ++u->health_errors;
    ++u->health_failures;
    u->health_at = now + 250U;
    if (replied)
        snprintf(last_transfer_fault, sizeof(last_transfer_fault),
                 "audio device identity changed addr=%u vid=%04x pid=%04x", u->address,
                 get16(descriptor + 8), get16(descriptor + 10));
    /* One transient reply failure gets a short retry. A halted controller or
     * an address now belonging to another device must stop immediately. */
    if (u->health_failures >= 2 || replied || u->unsafe_dma || !(CMD & RUN) ||
        !(CMD & PSE) || !connected()) {
        u->error = "USB audio device stopped responding";
        return false;
    }
    return true;
}
