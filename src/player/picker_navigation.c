#include "picker_navigation.h"
#include "media_browser.h"
#include "../storage/usb/usb_storage.h"
static UiTransition hover[2], press[2];
static int captured = -1;
static bool keyboard;
static uint32_t transition_started;
static bool transition_closing, transition_active, transition_backwards;
enum { SHORTCUT_Y = 15, SHORTCUT_HEIGHT = 13, SHORTCUT_GAP = 5 };
static const char *shortcut_key(unsigned index)
{
    return index == 0 ? "ENTER" : index == 1 ? "U" : "ESC";
}
static const char *shortcut_action(unsigned index)
{
    return index == 0 ? "OPEN" : index == 1 ? "USB" : media_browser_at_root() ? "EXIT" : "BACK";
}
static SDL_Rect shortcut_rect(const Fonts *fonts, unsigned index, int offset)
{
    int widths[3], total = 0;
    for (unsigned i = 0; i < 3; ++i) {
        widths[i] = (i != 1 || usb_storage_supported())
            ? header_shortcut_badge_width(fonts, shortcut_key(i), shortcut_action(i)) : 0;
        if (widths[i]) total += widths[i] + SHORTCUT_GAP;
    }
    total -= SHORTCUT_GAP;
    int x = (SCREEN_W - total) / 2;
    for (unsigned i = 0; i < index; ++i)
        if (widths[i]) x += widths[i] + SHORTCUT_GAP;
    return (SDL_Rect){x, SHORTCUT_Y + offset, widths[index], SHORTCUT_HEIGHT};
}
static SDL_Rect button_rect(const Fonts *fonts, unsigned index, int offset)
{
    return index ? shortcut_rect(fonts, 1, offset) : (SDL_Rect){8, 28 + offset, 31, 14};
}
static bool available(unsigned i)
{
    return i == 0 ? !media_browser_at_root() : usb_storage_supported();
}
void picker_navigation_reset(void)
{
    captured = -1;
    keyboard = false;
    for (unsigned i = 0; i < 2; ++i) {
        ui_transition_init(hover + i, false);
        ui_transition_init(press + i, false);
    }
}
int picker_navigation_poll(const Fonts *fonts, const PointerState *pointer, bool enter_edge,
                           bool enter_down, bool escape, uint32_t now, bool *consumed)
{
    int hot = -1, result = PICKER_NAV_NONE;
    for (unsigned i = 0; i < 2; ++i) {
        SDL_Rect rect = button_rect(fonts, i, 0);
        if (available(i) && pointer->visible && rect_contains_point(&rect, pointer->x, pointer->y))
            hot = (int)i;
    }
    *consumed = captured >= 0 || hot >= 0;
    if (captured < 0 && hot >= 0 && (pointer->press_edge || enter_edge)) {
        captured = hot;
        keyboard = enter_edge;
    }
    if (captured >= 0) {
        bool held = keyboard ? enter_down : pointer->down;
        if (escape) {
            ui_transition_begin_press_release(press + captured, now);
            captured = -1;
        } else if (!held) {
            if (hot == captured)
                result = captured == 0 ? PICKER_NAV_BACK : PICKER_NAV_USB;
            ui_transition_begin_press_release(press + captured, now);
            captured = -1;
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        ui_transition_update(hover + i, hot == (int)i, now, UI_HOVER_ANIM_MS);
        ui_transition_update_press_ex(press + i, captured == (int)i && hot == captured, now,
                                      UI_PRESS_ANIM_MS, PICKER_PRESS_RELEASE_ANIM_MS);
    }
    return result;
}
void picker_navigation_draw(SDL_Surface *screen, const Fonts *fonts, uint8_t mix, int offset)
{
    uint32_t now = monotonic_clock_now_ms();
    mix = (unsigned)mix * picker_navigation_mix(now) / 255;
    int slide = picker_navigation_offset(now);
    if (mix < 48)
        return;
    if (available(0)) {
        if (captured < 0)
            ui_transition_update_press_ex(press, false, monotonic_clock_now_ms(), UI_PRESS_ANIM_MS,
                                          PICKER_PRESS_RELEASE_ANIM_MS);
        SDL_Rect rect = button_rect(fonts, 0, offset);
        rect.x += slide;
        SDL_Surface *target;
        SDL_Rect old_clip;
        int dx, dy;
        SDL_Surface *layer =
            begin_faded_region_draw(screen, &rect, mix, &target, &old_clip, &dx, &dy);
        SDL_Rect button = offset_sdl_rect(&rect, dx, dy);
        draw_prompt_button(target, fonts, &button, "<", hover[0].current_mix, press[0].current_mix);
        end_faded_region_draw(screen, layer, &rect, &old_clip, mix);
    }
    char path[MAX_PATH_LEN], fitted[MAX_PATH_LEN];
    media_browser_breadcrumb(path, sizeof(path));
    int x = media_browser_at_root() ? 12 : 46;
    copy_fitted_text(fonts->white, path, fitted, sizeof(fitted), 308 - x);
    draw_ui_label_tint(screen, fonts, x + slide, 32 + offset, fitted, picker_text_color(false),
                       mix);
}
void picker_navigation_draw_shortcuts(SDL_Surface *screen, const Fonts *fonts,
                                      uint8_t mix, int offset)
{
    if (!mix) return;
    if (captured != 1)
        ui_transition_update_press_ex(press + 1, false, monotonic_clock_now_ms(),
                                      UI_PRESS_ANIM_MS, PICKER_PRESS_RELEASE_ANIM_MS);
    for (unsigned i = 0; i < 3; ++i) {
        SDL_Rect box = shortcut_rect(fonts, i, offset), clip;
        if (!box.w) continue;
        SDL_Surface *target;
        int dx, dy;
        SDL_Surface *layer = begin_faded_region_draw(screen, &box, mix, &target, &clip, &dx, &dy);
        draw_header_shortcut_badge_animated(target, fonts, box.x + dx, box.y + dy,
            shortcut_key(i), shortcut_action(i), 255,
            i == 1 ? hover[1].current_mix : 0, i == 1 ? press[1].current_mix : 0);
        end_faded_region_draw(screen, layer, &box, &clip, mix);
    }
}
void picker_navigation_animate(uint32_t now, bool closing, bool backwards)
{
    transition_started = now;
    transition_closing = closing;
    transition_backwards = backwards;
    transition_active = true;
}
uint8_t picker_navigation_mix(uint32_t now)
{
    if (!transition_active)
        return 255;
    uint8_t mix = ui_ease_out_cubic(now - transition_started, transition_closing ? (transition_backwards ? 160U : 100U)
                                                             : (transition_backwards ? 220U : 180U));
    if (mix == 255 && !transition_closing)
        transition_active = false;
    return transition_closing ? 255 - mix : mix;
}
int picker_navigation_offset(uint32_t now)
{
    int hidden = 255 - picker_navigation_mix(now);
    return (transition_closing ? -1 : 1) * (transition_backwards ? -1 : 1) *
           (hidden * 10 + 127) / 255;
}
