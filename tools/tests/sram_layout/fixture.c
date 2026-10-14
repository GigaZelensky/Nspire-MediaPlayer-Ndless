#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
static uint32_t control_input = 0x5107fU, domain_input = 3U;
static uint32_t table_input = 0x11348000U, reset_input = 0x10004000U;
static bool override_word, spans_ok = true, reset_span_ok = true;
static uint32_t override_address, override_value, active_sp;
/* Production arrays are emitted here by the host-check builder. */
/* KNOWN_ARRAYS */
static uint32_t sram_read_word(uint32_t address, void *context) {
    (void)context;
    if (override_word && address == override_address) return override_value;
    if (address == table_input + (table_input >> 20)*4U) return 0x11300c12U;
    if (address == table_input + 0xa40U*4U) return 0xa4000c1aU;
    if (address == 0x10000020U) return reset_input;
    for (unsigned i=0;i<7;++i) {
        if (address == reset_input+i*4U) return known_reset_code[i];
        if (address == reset_input-known_stack_offsets[i]) return known_stack_values[i];
    }
    for (unsigned i=0;i<13;++i)
        if (address == 0x10000050U+i*4U) return known_startup_code[i];
    for (unsigned i=0;i<4;++i)
        if (address == 0x10000298U+i*4U) return known_extents[i];
    return 0;
}
static bool sram_ram_span(uint32_t address, uint32_t bytes, uint32_t table) {
    (void)bytes; (void)table;
    return spans_ok && (address != reset_input - 0x98U || reset_span_ok);
}
static uint32_t sram_banked_sp(unsigned mode) { (void)mode; return active_sp; }
/* PRODUCTION_CHECKER */
static void word_failure(uint32_t address, uint32_t value, const char *stage) {
    override_word=true; override_address=address; override_value=value;
    assert(!sram_cx_layout(table_input));
    assert(!strcmp(g_cx_layout.stage,stage) && g_cx_layout.address==address && g_cx_layout.actual==value);
    override_word=false;
}
int main(void) {
    assert(sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"validated"));
    word_failure(table_input+0xa40U*4U,0xa4000c12U,"sram_descriptor");
    assert(g_cx_layout.expected==0xa4000c1aU);
    word_failure(table_input+(table_input>>20)*4U,0,"ttbr_self");
    word_failure(reset_input,0,"reset_code");
    word_failure(reset_input-known_stack_offsets[2],0,"stack_literal");
    word_failure(0x10000050U,0,"startup_code");
    word_failure(0x10000298U,0,"copy_extent");
    word_failure(0xa401d000U,0x12345678U,"pool_occupied");
    active_sp=0xa401c800U; assert(!sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"active_stack")); active_sp=0;
    control_input=0; assert(!sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"mmu")); control_input=0x5107f;
    domain_input=0; assert(!sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"domain")); domain_input=3;
    spans_ok=false; assert(!sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"startup_span")); spans_ok=true;
    reset_span_ok=false; assert(!sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"reset_span")); reset_span_ok=true;
    assert(!sram_cx_layout(0) && !strcmp(g_cx_layout.stage,"ttbr_range"));
    word_failure(0x10000020U,0,"reset_vector");
    /* The same native layout is accepted again, without stale failure fields. */
    assert(sram_cx_layout(table_input) && !strcmp(g_cx_layout.stage,"validated"));
    assert(!g_cx_layout.address && !g_cx_layout.actual && !g_cx_layout.expected);
    puts("PASS production CX SRAM guard and exact rejection diagnostics; layout policy unchanged");
}
