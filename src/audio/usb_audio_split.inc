/* EHCI siTDs for full-speed endpoints behind a high-speed transaction
 * translator. Field layout, port shift and OUT transaction count corroborated
 * with the selected native firmware's ISO scheduler (10a6bab4). Each siTD owns
 * two complete ARM926 cache lines, including zero extended-page fields, as in
 * the native allocator; iTDs remain exclusive to the direct/HS path. */
static void init_sitd(UsbAudio *u, Sitd *td, uint8_t *packet, unsigned endpoint, unsigned bytes)
{
    bool input = (endpoint & 0x80) != 0;
    unsigned splits = (bytes + 187) / 188;
    uintptr_t address = (uintptr_t)packet;
    memset(td, 0, sizeof(*td));
    td->next = td->back = 1;
    td->endpoint = u->address | ((endpoint & 15U) << 8) | ((uint32_t)u->tt_hub << 16) |
                   ((uint32_t)u->tt_port << 24) | (input ? 1U << 31 : 0);
    /* OUT uses start-splits at 0..5. A feedback IN uses start 3, complete
     * 5..7. Rate selection limits OUT+feedback to at most three splits. */
    td->schedule = input ? 0xe008U : (1U << splits) - 1;
    td->state = 0x80U | (bytes << 16);
    td->buffer[0] = (uint32_t)address;
    td->buffer[1] = (uint32_t)((address & ~(uintptr_t)4095) + 4096) |
                    (input ? 0U : splits | (splits > 1 ? 1U << 3 : 0));
}
static bool split_completed(UsbAudio *u, unsigned sequence)
{
    Sitd *td = u->sitd + (sequence & (RING - 1));
    dma_invalidate(td, sizeof(*td));
    if (!(td->state & (0xfcU | 0x3ff0000U)))
        return true;
    snprintf(last_transfer_fault, sizeof(last_transfer_fault),
             "split token=%08lx route=%08lx schedule=%08lx sequence=%u frame=%lu checked=%lu "
             "next=%lu cmd=%08lx sts=%08lx port=%08lx saved_cmd=%08lx",
             (unsigned long)td->state, (unsigned long)td->endpoint, (unsigned long)td->schedule,
             sequence, (unsigned long)u->frame, (unsigned long)u->checked, (unsigned long)u->next,
             (unsigned long)CMD, (unsigned long)STS, (unsigned long)PORT,
             (unsigned long)u->saved_command);
    u->error = "USB split isochronous transfer failed";
    return false;
}
