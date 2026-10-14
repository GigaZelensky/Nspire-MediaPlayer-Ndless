#include "native_screen_power.h"
#include "native_interrupts.h"
#include "native_firmware.h"

#include <stddef.h>
#include <os.h>

static uint32_t screen_off_entry, screen_on_entry, screen_supervisor_state;
#define SCREEN_LCD_CONTROL 0xC0000018U
#define SCREEN_PWM_DUTY 0x90130014U
#define SCREEN_PWM_PERIOD 0x90130018U
#define SCREEN_PWM_CONTROL 0x90130020U
#define SCREEN_LED_SUPPLY 0x90140800U
#define SCREEN_LED_SUPPLY_BIT 0x10U
#define SCREEN_SUPERVISOR_STATE screen_supervisor_state
#define SCREEN_LCD_ENABLE_BITS 0x801U
#define SCREEN_PWM_BLANK 0xFFFFFU

static bool screen_restore_required;
static uint32_t screen_saved_pwm_mode;
static uint32_t screen_saved_lcd_control;
static int screen_last_status, screen_native_result;
static int screen_platform_gate = -1;

static uint32_t screen_word(uintptr_t address)
{
    return *(const volatile uint32_t *)address;
}

static void screen_set_duty(uint32_t value)
{
    *(volatile uint32_t *)SCREEN_PWM_DUTY = value;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(0) : "memory");
}

static uint32_t power_word(const PortableView *v, uint32_t at)
{
    uint32_t value = 0;
    if (!(at & 3U) && v->allow_span(v->context, at, 4, PORTABLE_DATA))
        v->read_word(v->context, at, &value);
    return value;
}

static uint32_t power_branch(const PortableView *v, uint32_t at)
{
    uint32_t op = power_word(v, at);
    if ((op & 0xff000000U) != 0xeb000000U) return 0;
    uint32_t target = at + 8U + ((int32_t)(op << 8) >> 6);
    return target >= v->code_begin && target < v->code_end &&
        v->allow_span(v->context, target, 4, PORTABLE_CODE) ? target : 0;
}

static bool power_shape(const PortableView *v, uint32_t at, const uint32_t *words, unsigned count)
{
    if (!at || !v->allow_span(v->context, at, count * 4U, PORTABLE_CODE)) return false;
    for (unsigned i = 0; i < count; ++i)
        if (words[i] && power_word(v, at + i * 4U) != words[i]) return false;
    return true;
}

static bool power_wrapper(const PortableView *v, uint32_t at, uint32_t *command, uint32_t *dispatch)
{
    const uint32_t shape[] = {0xe59f101cU,0xe52de004U,0xe24dd02cU,0xe3a03000U,
        0xe1a0000dU,0xe88d000aU,0,0xe28dd02cU,0xe8bd8000U};
    const uint32_t dispatcher[] = {0xe92d4010U,0xe1a04000U,0,0xe3500000U,
        0x03e00005U,0x08bd8010U,0xe1a01004U,0,0xe5940008U,0xe8bd8010U};
    const uint32_t getter[] = {0xe92d4010U,0xe59f401cU,0xe5940000U,0xe3500000U,
        0x18bd8010U,0xe3e01000U,0xe3a0001bU,0,0xe5840000U,0xe8bd8010U};
    if (!power_shape(v, at, shape, 9)) return false;
    *command = power_word(v, at + 36U);
    *dispatch = power_branch(v, at + 24U);
    if ((*command != 1001U && *command != 1002U) || !power_shape(v, *dispatch, dispatcher, 10) ||
        !power_branch(v, *dispatch + 28U)) return false;
    uint32_t get = power_branch(v, *dispatch + 8U);
    return power_shape(v, get, getter, 10) && power_branch(v, get + 28U) &&
        v->allow_span(v->context, power_word(v, get + 40U), 4, PORTABLE_DATA);
}

static bool screen_validate_platform(void)
{
    PortableReaderPlatform mapping = {0};
    unsigned saved = native_critical_enter();
    bool valid = false;
    uint32_t off = 0, on = 0, dispatch = 0, supervisor = 0, backlight = 0;
    if (screen_word(0x900a0000U) != 0x202U || !portable_reader_platform_refresh(&mapping)) goto done;
    const PortableView *v = portable_reader_platform_view(&mapping);
    if (!v) goto done;
    /* Small, unique no-argument wrappers issue OFF/ON to LCD device 27.
     * Follow their branches and data literals instead of assuming OS addresses. */
    for (uint32_t at = v->code_begin; at < v->code_begin + 0x20000U; at += 4) {
        if (power_word(v, at) != 0xe59f101cU) continue;
        uint32_t command, call;
        if (!power_wrapper(v, at, &command, &call)) continue;
        uint32_t *slot = command == 1001U ? &off : &on;
        if (*slot || (dispatch && dispatch != call)) goto done;
        *slot = at; dispatch = call;
    }
    if (!off || !on) goto done;
    /* Bind the typed LCD/backlight descriptors. Page checks authenticate the
     * live mapping; each hit validates its driver handler and mutable state. */
    for (uint32_t page = v->ram_begin; page < v->ram_begin + 0x1000000U; page += 4096U) {
        if (!v->allow_span(v->context, page, 4096, PORTABLE_DATA)) continue;
        const uint32_t *words = v->code_words ? v->code_words(v->context, page, 4096) : NULL;
        for (unsigned i = 0; i < 1024; ++i) {
            uint32_t at = page + i * 4U;
            if ((words ? words[i] : power_word(v, at)) != 0x494f4452U || at < v->ram_begin + 12U) continue;
            uint32_t descriptor = at - 12U, id = power_word(v, descriptor + 44U);
            if (id != 27U && id != 31U) continue;
            const uint32_t handler[] = {0xe3e0300eU, id == 27U ? 0xe92d41f0U : 0xe92d4070U,
                0xe5813008U,0xe590001cU};
            if (!power_shape(v, power_word(v, descriptor + 56U), handler, 4)) continue;
            if (id == 27U) { if (backlight) goto done; backlight = descriptor; }
            else {
                if (supervisor) goto done;
                supervisor = power_word(v, descriptor + 20U);
                if (!v->allow_span(v->context, supervisor, 0xdcU + 2048U, PORTABLE_DATA)) goto done;
            }
        }
    }
    if (!backlight || !supervisor) goto done;
    screen_off_entry = off; screen_on_entry = on; screen_supervisor_state = supervisor;
    valid = true;
done:
    native_critical_leave(saved);
    return valid;
}

bool native_screen_power_supported(void)
{
    /* Firmware and driver code cannot change during this app instance.
     * Keep live state checks per transition, but avoid repeated SDK/ROM scans. */
    if (screen_platform_gate < 0)
        screen_platform_gate = screen_validate_platform() ? 1 : 0;
    return screen_platform_gate != 0;
}

static uint32_t screen_copy_enabled(void)
{
    return screen_word(SCREEN_SUPERVISOR_STATE + 0x58U);
}

static bool screen_state_valid(void)
{
    return screen_word(SCREEN_SUPERVISOR_STATE) == 1U && screen_copy_enabled() <= 1U;
}

static bool screen_copier_postcondition(uint32_t expected_enabled)
{
    if (screen_copy_enabled() != expected_enabled)
        return false;
    /* Backlight commands must leave the existing copier configuration alone.
     * No timer, LCD controller, scanout or cursor is restarted on wake. */
    return !expected_enabled || (screen_word(SCREEN_SUPERVISOR_STATE + 0x1CU) == 0x54494D45U &&
                                 (screen_word(SCREEN_SUPERVISOR_STATE + 0x30U) & 0xFFU) == 1U);
}

static bool screen_off_postcondition(void)
{
    /* Cut LED supply, not merely brightness. Keep panel configuration alive
     * so waking does not reset/reinitialize it for another ~400ms. */
    return !(screen_word(SCREEN_LED_SUPPLY) & SCREEN_LED_SUPPLY_BIT) &&
           screen_word(SCREEN_LCD_CONTROL) == screen_saved_lcd_control &&
           screen_word(SCREEN_PWM_DUTY) == SCREEN_PWM_BLANK &&
           screen_word(SCREEN_PWM_PERIOD) == SCREEN_PWM_BLANK;
}

static int screen_control(bool on, uint32_t raw)
{
    unsigned saved = native_critical_enter();
    int result;
    /* Native backlight commands do not reset the LCD or change its scanout,
     * copier or timing. They gate the LED supply and PWM peripheral. Replace
     * the OS-default duty immediately, preserving the caller's exact mask. */
    result = ((int (*)(void))(uintptr_t)(on ? screen_on_entry : screen_off_entry))();
    native_critical_enter();
    if (on)
        screen_set_duty(raw);
    native_critical_leave(saved);
    return result;
}

int native_screen_power_off(void)
{
    uint32_t copy_enabled;
    if (!native_screen_power_supported())
        return screen_last_status = NATIVE_SCREEN_POWER_UNSUPPORTED;
    if (!screen_state_valid())
        return screen_last_status = NATIVE_SCREEN_POWER_BAD_STATE;
    copy_enabled = screen_copy_enabled();
    if (screen_restore_required && !screen_native_result && screen_off_postcondition() &&
        screen_copier_postcondition(copy_enabled))
        return screen_last_status = NATIVE_SCREEN_POWER_OK;
    if (!screen_restore_required &&
        (screen_word(SCREEN_LCD_CONTROL) & SCREEN_LCD_ENABLE_BITS) != SCREEN_LCD_ENABLE_BITS)
        return screen_last_status = NATIVE_SCREEN_POWER_BAD_STATE;
    if (!screen_restore_required && copy_enabled &&
        (screen_word(SCREEN_SUPERVISOR_STATE + 0x1CU) != 0x54494D45U ||
         (screen_word(SCREEN_SUPERVISOR_STATE + 0x30U) & 0xFFU) != 1U))
        return screen_last_status = NATIVE_SCREEN_POWER_BAD_STATE;
    /* Native backlight control uses bit0 as a configured operating mode, not
     * proof that the LED supply is on. A visible display can legitimately use
     * zero (as in the device crash journal). Save before the first OFF, never
     * replace it with the state left by a partial transition or a retry. */
    if (!screen_restore_required) {
        screen_saved_pwm_mode = screen_word(SCREEN_PWM_CONTROL) & 1U;
        screen_saved_lcd_control = screen_word(SCREEN_LCD_CONTROL);
    }
    screen_restore_required = true;
    screen_native_result = screen_control(false, 0);
    return screen_last_status = !screen_native_result && screen_off_postcondition() &&
                                        screen_copier_postcondition(copy_enabled)
                                    ? NATIVE_SCREEN_POWER_OK
                                    : NATIVE_SCREEN_POWER_OFF_FAILED;
}

int native_screen_power_on(uint32_t brightness_raw)
{
    uint32_t copy_enabled;
    if (brightness_raw > 255U)
        return screen_last_status = NATIVE_SCREEN_POWER_BAD_STATE;
    if (!screen_restore_required)
        return screen_last_status = NATIVE_SCREEN_POWER_OK;
    if (!native_screen_power_supported())
        return screen_last_status = NATIVE_SCREEN_POWER_UNSUPPORTED;
    if (!screen_state_valid())
        return screen_last_status = NATIVE_SCREEN_POWER_BAD_STATE;
    copy_enabled = screen_copy_enabled();
    screen_native_result = screen_control(true, brightness_raw);
    if (screen_native_result || screen_word(SCREEN_LCD_CONTROL) != screen_saved_lcd_control ||
        !(screen_word(SCREEN_LED_SUPPLY) & SCREEN_LED_SUPPLY_BIT) ||
        screen_word(SCREEN_PWM_DUTY) != brightness_raw || screen_word(SCREEN_PWM_PERIOD) != 255U ||
        (screen_word(SCREEN_PWM_CONTROL) & 1U) != screen_saved_pwm_mode ||
        !screen_copier_postcondition(copy_enabled))
        return screen_last_status = NATIVE_SCREEN_POWER_ON_FAILED;
    screen_restore_required = false;
    return screen_last_status = NATIVE_SCREEN_POWER_OK;
}

bool native_screen_power_is_off(void)
{
    return screen_restore_required;
}

void native_screen_power_snapshot(NativeScreenPowerSnapshot *snapshot)
{
    bool supported;
    unsigned saved;
    if (!snapshot)
        return;
    supported = native_screen_power_supported();
    saved = native_critical_enter();
    snapshot->supported = supported;
    snapshot->restore_required = screen_restore_required;
    snapshot->lcd_control = supported ? screen_word(SCREEN_LCD_CONTROL) : 0;
    snapshot->pwm_duty = supported ? screen_word(SCREEN_PWM_DUTY) : 0;
    snapshot->pwm_period = supported ? screen_word(SCREEN_PWM_PERIOD) : 0;
    snapshot->pwm_control = supported ? screen_word(SCREEN_PWM_CONTROL) : 0;
    snapshot->display_copy_enabled = supported ? screen_copy_enabled() : 0;
    snapshot->native_result = screen_native_result;
    snapshot->last_status = screen_last_status;
    native_critical_leave(saved);
}
