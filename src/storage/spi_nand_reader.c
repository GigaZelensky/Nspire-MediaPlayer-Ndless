#include "spi_nand_reader.h"
#include <string.h>

enum {
    READY_ISSUE,
    READY_WAIT,
    PAGE_ISSUE,
    PAGE_WAIT,
    STATUS_ISSUE,
    STATUS_WAIT,
    CACHE_ISSUE,
    CACHE_DRAIN,
    RESET_WAIT,
    DMA_WAIT,
    DMA_FINISH,
    DMA_STOP,
    ID_ISSUE, ID_WAIT, FEATURE_A_ISSUE, FEATURE_A_WAIT,
    FEATURE_B_ISSUE, FEATURE_B_WAIT
};
enum {
    CONTROL = 0x10U,
    STATUS = 0x18U,
    INT_ENABLE = 0x20U,
    INT_STATUS = 0x24U,
    RESPONSE = 0x28U,
    FEATURES = 0x54U,
    FIFO = 0x100U
};
static uint32_t rd(SpiNandReader *r, uint32_t reg)
{
    return r->bus.read32(r->bus.context, reg);
}
static void wr(SpiNandReader *r, uint32_t reg, uint32_t value)
{
    r->bus.write32(r->bus.context, reg, value);
}
static void issue(SpiNandReader *r, uint32_t address, uint32_t format, uint32_t bytes,
                  uint32_t command)
{
    /* Exact register ordering of 1001A5E8 / 1001A558. Writing the command
     * register can launch the transfer; never restore old command registers. */
    wr(r, INT_STATUS, rd(r, INT_STATUS) | 1U);
    wr(r, 0, address);
    wr(r, 4, format);
    wr(r, 8, bytes);
    wr(r, 12, command);
    wr(r, INT_ENABLE, rd(r, INT_ENABLE) | 2U);
    r->quiescent = false;
}
static bool completed(SpiNandReader *r)
{
    if (!(rd(r, INT_STATUS) & 1U))
        return false;
    wr(r, INT_STATUS, rd(r, INT_STATUS) | 1U);
    wr(r, INT_ENABLE, r->saved_enable);
    r->quiescent = true;
    return true;
}
static void terminal(SpiNandReader *r, SpiNandStatus status)
{
    r->status = status;
    r->destination = NULL;
}
static void abort_transfer(SpiNandReader *r, uint32_t now)
{
    /* Reset the SPI controller/FIFO, never NAND program/erase or device reset.
     * CTRL bit 8 is the same self-clearing reset used by 1001A358. */
    wr(r, INT_ENABLE, rd(r, INT_ENABLE) & ~2U);
    wr(r, CONTROL, rd(r, CONTROL) | 0x100U);
    r->phase = RESET_WAIT;
    r->reset_started = now;
    r->quiescent = false;
}
bool spi_nand_init(SpiNandReader *r, SpiNandBus bus)
{
    if (!r || !bus.read32 || !bus.read8 || !bus.write32)
        return false;
    memset(r, 0, sizeof(*r));
    r->bus = bus;
    r->quiescent = true;
    return true;
}
bool spi_nand_set_dma(SpiNandReader *r, SpiNandDma dma)
{
    if (!r || !r->quiescent || r->status == SPI_NAND_PENDING ||
        (dma.begin && (!dma.poll || !dma.abort || !dma.release))) return false;
    r->dma = dma;
    return true;
}
bool spi_nand_enable_wide(SpiNandReader *r)
{
    if (!r || !r->quiescent || r->status == SPI_NAND_PENDING) return false;
    r->wide_enabled = true;
    return true;
}
static void abort_dma(SpiNandReader *r, uint32_t now, bool failed)
{
    r->dma.abort(r->dma.context, failed);
    r->phase = DMA_STOP;
    r->reset_started = now;
    /* A transfer fault must reach the caller, not silently disable DMA and
     * retry through PIO. Cleanup still restores controller ownership first. */
    r->reset_for_dma = true;
    r->quiescent = false;
}
bool spi_nand_begin(SpiNandReader *r, uint32_t page, void *destination, size_t bytes, uint32_t now,
                    uint32_t timeout)
{
    if (bytes != 2048U) {
        if (r && r->status != SPI_NAND_PENDING)
            r->error = SPI_NAND_BAD_ARGUMENT;
        return false;
    }
    return spi_nand_begin_range(r, page, 0, destination, bytes, now, timeout);
}
bool spi_nand_begin_range(SpiNandReader *r, uint32_t page, uint32_t column, void *destination,
                          size_t bytes, uint32_t now, uint32_t timeout)
{
    if (!r || !r->bus.read32 || !r->bus.read8 || !r->bus.write32)
        return false;
    if (r->status == SPI_NAND_PENDING || !r->quiescent)
        return false;
    r->error = SPI_NAND_OK;
    uintptr_t dest = (uintptr_t)destination, object = (uintptr_t)r;
    if (!destination || !bytes || column >= 2112U || bytes > 2112U - column ||
        dest > UINTPTR_MAX - bytes || (dest < object + sizeof(*r) && object < dest + bytes) ||
        page >= 65536U || !timeout || timeout >= 0x80000000U) {
        r->error = SPI_NAND_BAD_ARGUMENT;
        return false;
    }
    uint32_t control = rd(r, CONTROL), enable = rd(r, INT_ENABLE);
    r->initial_status = rd(r, STATUS);
    r->initial_features = rd(r, FEATURES);
    r->initial_interrupt = rd(r, INT_STATUS);
    r->fifo_depth = (r->initial_features >> 8) & 255U;
    if ((control & 0x100100U) || (enable & 3U) || (r->initial_status & 2U)) {
        r->error = SPI_NAND_NOT_IDLE;
        return false;
    }
    if (!r->fifo_depth) {
        r->error = SPI_NAND_BAD_ARGUMENT;
        return false;
    }
    r->destination = destination;
    r->page = page;
    r->copied = 0;
    r->started = now;
    r->timeout = timeout;
    r->column = column;
    r->requested = (uint32_t)bytes;
    r->saved_enable = enable;
    r->saved_control = control;
    r->cancel = false;
    r->corrected = false;
    r->last_status = 0;
    r->rx_credit = 0;
    r->fifo_word = r->fifo_bytes = 0;
    r->reset_for_dma = false;
    r->phase = READY_ISSUE;
    r->status = SPI_NAND_PENDING;
    return true;
}
void spi_nand_cancel(SpiNandReader *r)
{
    if (r && r->status == SPI_NAND_PENDING)
        r->cancel = true;
}
SpiNandStatus spi_nand_step(SpiNandReader *r, uint32_t now, uint32_t budget)
{
    if (!r)
        return SPI_NAND_ERROR;
    /* Cancellation and failures retain ownership until BOTH engines stop. */
    if (r->phase == DMA_STOP && r->dma_active) {
        if (!r->dma.release(r->dma.context, false)) {
            if ((uint32_t)(now - r->reset_started) >= r->timeout) {
                r->error = SPI_NAND_DMA_STOP_FAILED;
                r->status = SPI_NAND_ERROR;
            }
            return r->status;
        }
        r->dma_active = false;
        abort_transfer(r, now);
        return r->status;
    }
    /* A reset timeout reports ERROR but does not release controller ownership.
     * Keep polling cleanup on later steps so a delayed reset can finish and
     * restore the saved registers before another reader/writer uses the bus. */
    if (r->phase == RESET_WAIT && !r->quiescent) {
        if (!(rd(r, CONTROL) & 0x100U)) {
            wr(r, CONTROL, r->saved_control);
            wr(r, INT_STATUS, rd(r, INT_STATUS) | 1U);
            wr(r, INT_ENABLE, r->saved_enable);
            r->quiescent = true;
            if (r->reset_for_dma && r->cancel && r->status != SPI_NAND_ERROR) {
                r->error = SPI_NAND_OK;
                terminal(r, SPI_NAND_CANCELED);
            } else terminal(r, SPI_NAND_ERROR);
        } else if ((uint32_t)(now - r->reset_started) >= r->timeout) {
            r->error = SPI_NAND_RESET_FAILED;
            terminal(r, SPI_NAND_ERROR);
        }
        return r->status;
    }
    if (r->status != SPI_NAND_PENDING)
        return r->status;
    if (r->dma_active && r->cancel) {
        abort_dma(r, now, false);
        return r->status;
    }
    if ((uint32_t)(now - r->started) >= r->timeout) {
        r->error = SPI_NAND_TIMEOUT;
        if (r->dma_active)
            abort_dma(r, now, true);
        else if (r->quiescent)
            terminal(r, SPI_NAND_ERROR);
        else
            abort_transfer(r, now);
        return r->status;
    }
    switch (r->phase) {
    case READY_ISSUE:
        if (r->cancel) {
            terminal(r, SPI_NAND_CANCELED);
            break;
        }
        issue(r, 0xC0U, 0x01000001U, 0, 0x0F00010CU);
        r->phase = READY_WAIT;
        break;
    case READY_WAIT:
        if (completed(r)) {
            r->last_status = rd(r, RESPONSE) & 255U;
            r->phase = (r->last_status & 1U) ? READY_ISSUE :
                r->wide_enabled && !r->profile_ready ? ID_ISSUE : PAGE_ISSUE;
        }
        break;
    case ID_ISSUE:
        if (r->cancel) { terminal(r, SPI_NAND_CANCELED); break; }
        r->id_received = false;
        issue(r, 0, 0x01080000U, 4, 0x9f000100U);
        r->phase = ID_WAIT;
        break;
    case ID_WAIT:
        if (!r->id_received && ((rd(r, STATUS) & 2U) || (rd(r, INT_STATUS) & 1U))) {
            r->chip_id = rd(r, FIFO);
            r->id_received = true;
        }
        if (r->id_received && completed(r)) r->phase = FEATURE_A_ISSUE;
        break;
    case FEATURE_A_ISSUE:
        if (r->cancel) { terminal(r, SPI_NAND_CANCELED); break; }
        issue(r, 0xa0, 0x01000001U, 0, 0x0f00010cU);
        r->phase = FEATURE_A_WAIT;
        break;
    case FEATURE_A_WAIT:
        if (completed(r)) { r->feature_a0 = rd(r, RESPONSE); r->phase = FEATURE_B_ISSUE; }
        break;
    case FEATURE_B_ISSUE:
        if (r->cancel) { terminal(r, SPI_NAND_CANCELED); break; }
        issue(r, 0xb0, 0x01000001U, 0, 0x0f00010cU);
        r->phase = FEATURE_B_WAIT;
        break;
    case FEATURE_B_WAIT:
        if (completed(r)) {
            r->feature_b0 = rd(r, RESPONSE);
            bool known = (r->chip_id & 0xffffffU) == 0x21baefU || (r->chip_id >> 8) == 0x21baefU;
            bool settings = (r->feature_b0 & 0x18U) == 0x18U && !(r->feature_b0 & 0x40U) && !(r->last_status & 2U);
            r->profile_reason = !known ? 1 : !settings ? 2 : (r->feature_a0 & 2U) ? 3 : 0;
            r->wide_mode = known && settings ? (r->feature_a0 & 2U ? 1 : 2) : 0;
            r->profile_ready = true;
            r->phase = PAGE_ISSUE;
        }
        break;
    case PAGE_ISSUE:
        if (r->cancel) {
            terminal(r, SPI_NAND_CANCELED);
            break;
        }
        issue(r, r->page, 0x01000003U, 0, 0x13000102U);
        r->phase = PAGE_WAIT;
        break;
    case PAGE_WAIT:
        if (completed(r))
            r->phase = STATUS_ISSUE;
        break;
    case STATUS_ISSUE:
        issue(r, 0xC0U, 0x01000001U, 0, 0x0F00010CU);
        r->phase = STATUS_WAIT;
        break;
    case STATUS_WAIT:
        if (completed(r)) {
            r->last_status = rd(r, RESPONSE) & 255U;
            if (r->last_status & 1U) {
                r->phase = STATUS_ISSUE;
                break;
            }
            if (r->cancel) {
                terminal(r, SPI_NAND_CANCELED);
                break;
            }
            unsigned ecc = (r->last_status >> 4) & 7U;
            if (ecc > 1U) {
                r->error = SPI_NAND_ECC;
                terminal(r, SPI_NAND_ERROR);
                break;
            }
            r->corrected = ecc == 1U;
            r->phase = CACHE_ISSUE;
        }
        break;
    case CACHE_ISSUE:
        if (r->cancel) {
            terminal(r, SPI_NAND_CANCELED);
            break;
        }
        int dma = r->dma.begin ? r->dma.begin(r->dma.context, r->destination, r->requested) : 0;
        if (dma < 0) { r->error = SPI_NAND_DMA_ERROR; terminal(r, SPI_NAND_ERROR); break; }
        r->dma_active = dma > 0;
        unsigned mode = r->dma_active ? r->wide_mode : 0;
        const uint32_t commands[] = {0x0b000100U, 0x3b000120U, 0x6b000140U};
        if (mode == 2) ++r->quad_commands;
        else if (mode == 1) ++r->dual_commands;
        else ++r->single_commands;
        issue(r, (((r->page / 64U) & 1U) << 12) | r->column, 0x01080002U, r->requested, commands[mode]);
        if (r->dma_active) {
            /* Hardware-verified order: arm DMA, issue command, enable request. */
            wr(r, INT_ENABLE, r->saved_enable | 3U);
            r->phase = DMA_WAIT;
        } else r->phase = CACHE_DRAIN;
        break;
    case DMA_WAIT: {
        int status = r->dma.poll(r->dma.context);
        if (status < 0) {
            r->error = SPI_NAND_DMA_ERROR;
            abort_dma(r, now, true);
            break;
        }
        if (!status || !completed(r)) break;
        r->quiescent = false; /* DMA lease/cache still belongs to the reader. */
        r->phase = DMA_FINISH;
    }
        /* fall through */
    case DMA_FINISH:
        if (!r->dma.release(r->dma.context, true)) break;
        r->dma_active = false;
        r->quiescent = true;
        r->copied = r->requested;
        terminal(r, SPI_NAND_DONE);
        break;
    case CACHE_DRAIN: {
        /* 0x54 is constant capacity, not occupancy. The native receive loop
         * waits for STATUS.RX_READY before draining one capacity-sized burst.
         * Keep the undrained part across small-budget steps; do not wait for
         * another full-burst indication halfway through the final burst. */
        if (!r->rx_credit && r->copied < r->requested && (rd(r, STATUS) & 2U)) {
            r->rx_credit = r->fifo_depth;
            if (r->rx_credit > r->requested - r->copied)
                r->rx_credit = r->requested - r->copied;
        }
        uint32_t n = r->requested - r->copied;
        if (n > r->rx_credit)
            n = r->rx_credit;
        if (n > budget)
            n = budget;
        if (n > 128U)
            n = 128U;
        uint8_t *destination = r->destination + r->copied;
        r->rx_credit -= n;
        while (n >= 4U && !r->fifo_bytes) {
            uint32_t value = rd(r, FIFO);
            destination[0] = (uint8_t)value;
            destination[1] = (uint8_t)(value >> 8);
            destination[2] = (uint8_t)(value >> 16);
            destination[3] = (uint8_t)(value >> 24);
            destination += 4;
            n -= 4U;
            r->copied += 4U;
        }
        while (n--) {
            if (!r->fifo_bytes) {
                /* A byte-wide FIFO access consumes a whole word on FTSPI020.
                 * Keep unused bytes across tiny service budgets; discard only
                 * the padding of the command's final partial word. */
                r->fifo_word = rd(r, FIFO);
                uint32_t left = r->requested - r->copied;
                r->fifo_bytes = left < 4U ? left : 4U;
            }
            *destination++ = (uint8_t)r->fifo_word;
            r->fifo_word >>= 8;
            --r->fifo_bytes;
            ++r->copied;
        }
        if (r->copied == r->requested && completed(r))
            terminal(r, r->cancel ? SPI_NAND_CANCELED : SPI_NAND_DONE);
        break;
    }
    default:
        r->error = SPI_NAND_BAD_ARGUMENT;
        abort_transfer(r, now);
        break;
    }
    return r->status;
}
