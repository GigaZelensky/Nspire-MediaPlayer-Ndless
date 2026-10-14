#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NDVIDEO_CX_SRAM_SURVEY 1
#define SRAM_CX_HARDWARE_SIZE (128U * 1024U)
#define SRAM_PHYSICAL_ADDRESS 0xa4000000U

static uint32_t fake_sram[SRAM_CX_HARDWARE_SIZE / 4U];
static uint32_t chip = 0x10000101U, table = 0x11348000U;
static uint32_t control_input = 0x5107fU, domain_input = 3U;
static uint32_t self = 0x11300c1eU, descriptor = 0xa4000c1aU;
static unsigned reads;
static bool critical;
static bool nspire_asic_is_cx(uint32_t id) { return id == 0x10000101U; }
static uint32_t nspire_hardware_id(void) { return chip; }
static uint32_t sram_get_ttbr0(void) { return table; }
static unsigned sram_critical_enter(void) { assert(!critical); critical = true; return 0x80; }
static void sram_critical_leave(unsigned saved) { assert(critical && saved == 0x80); critical = false; }
static uint32_t sram_banked_sp(unsigned mode) { assert(critical); return 0xa4009f00U + mode * 4U; }
static uint32_t sram_read_word(uint32_t address, void *context)
{
    (void)context;
    assert(critical);
    if (address == table + (table >> 20) * 4U) return self;
    if (address == table + 0xa40U * 4U) return descriptor;
    assert(address >= SRAM_PHYSICAL_ADDRESS && address < SRAM_PHYSICAL_ADDRESS + sizeof(fake_sram));
    ++reads;
    return fake_sram[(address - SRAM_PHYSICAL_ADDRESS) / 4U];
}

/* PRODUCTION_SURVEY */

static void reset(void)
{
    memset(g_sram_survey, 0, sizeof(g_sram_survey));
    g_sram_survey_count = reads = 0;
}

int main(void)
{
    chip = 0;
    sram_survey_checkpoint("unknown");
    assert(!g_sram_survey_count && !reads);
    chip = 0x10000101U;
    for (unsigned failure = 0; failure < 5U; ++failure) {
        reset();
        if (failure == 0) control_input = 0;
        if (failure == 1) domain_input = 0;
        if (failure == 2) table = 0;
        if (failure == 3) self = 0;
        if (failure == 4) descriptor = 0;
        sram_survey_checkpoint("rejected");
        assert(g_sram_survey_count == 1 && !g_sram_survey[0].valid && !reads);
        control_input = 0x5107fU; domain_input = 3U; table = 0x11348000U;
        self = 0x11300c1eU; descriptor = 0xa4000c1aU;
    }
    reset();
    for (unsigned n = 0; n < sizeof(fake_sram) / 4U; ++n) fake_sram[n] = n ^ 0x21010404U;
    for (unsigned n = 0; n < 3U; ++n) {
        fake_sram[128] += n;
        sram_survey_checkpoint(n == 0 ? "app-entry" : n == 1 ? "before-decoder" : "playback-exit");
        assert(g_sram_survey[n].valid && !memcmp(fake_sram, g_sram_survey[n].words, sizeof(fake_sram)));
        assert(g_sram_survey[n].stacks[0] == 0xa4009f7cU);
    }
    assert(reads == 3U * sizeof(fake_sram) / 4U);
    sram_survey_checkpoint("ignored");
    assert(g_sram_survey_count == 3 && reads == 3U * sizeof(fake_sram) / 4U);
    FILE *file = tmpfile();
    assert(file);
    sram_survey_debug(file);
    rewind(file);
    char line[512], data[257];
    unsigned index, offset, lines = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "sram_survey_hex index=%u offset=%x data=%256s", &index, &offset, data) != 3) continue;
        assert(index == lines / 1024U && offset == (lines % 1024U) * 128U && strlen(data) == 256U);
        for (unsigned n = 0; n < 128U; ++n) {
            unsigned byte;
            assert(sscanf(data + n * 2U, "%2x", &byte) == 1);
            uint32_t word = g_sram_survey[index].words[(offset + n) / 4U];
            assert(byte == ((word >> ((n & 3U) * 8U)) & 0xffU));
        }
        ++lines;
    }
    fclose(file);
    assert(lines == 3072U);
    assert(!memcmp(fake_sram, g_sram_survey[2].words, sizeof(fake_sram)));
    puts("PASS read-only CX SRAM snapshots: mapping rejection, exact bytes, stack metadata, bounded captures and hex export");
    return 0;
}
