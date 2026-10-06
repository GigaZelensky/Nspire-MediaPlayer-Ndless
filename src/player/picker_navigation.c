#include "picker_navigation.h"
#include "media_browser.h"
static UiTransition hover[2], press[2];
static int captured = -1;
static bool keyboard;
static uint32_t transition_started;
static bool transition_closing, transition_active;
static const SDL_Rect buttons[2] = {{12, 33, 34, 13}, {274, 33, 32, 13}};
static bool available(unsigned i)
{
    return i == 0 ? !media_browser_at_root() : is_cx2;
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
int picker_navigation_poll(const PointerState *pointer, bool enter_edge, bool enter_down,
                           bool escape, uint32_t now, bool *consumed)
{
    int hot = -1, result = PICKER_NAV_NONE;
    for (unsigned i = 0; i < 2; ++i)
        if (available(i) && pointer->visible &&
            rect_contains_point(buttons + i, pointer->x, pointer->y))
            hot = (int)i;
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
    if (mix < 48)
        return;
    for (unsigned i = 0; i < 2; ++i)
        if (available(i)) {
            if (captured < 0)
                ui_transition_update_press_ex(press + i, false, monotonic_clock_now_ms(),
                                              UI_PRESS_ANIM_MS, PICKER_PRESS_RELEASE_ANIM_MS);
            SDL_Rect rect = buttons[i];
            rect.y += offset;
            draw_prompt_button(screen, fonts, &rect, i == 0 ? "Back" : "USB", hover[i].current_mix,
                               press[i].current_mix);
        }
    char path[MAX_PATH_LEN], fitted[MAX_PATH_LEN];
    media_browser_breadcrumb(path, sizeof(path));
    int x = media_browser_at_root() ? 16 : 53;
    copy_fitted_text(fonts->white, path, fitted, sizeof(fitted), (is_cx2 ? 268 : 304) - x);
    draw_ui_label(screen, fonts, x, 35 + offset, fitted);
}
void picker_navigation_animate(uint32_t now, bool closing)
{
    transition_started = now;
    transition_closing = closing;
    transition_active = true;
}
uint8_t picker_navigation_mix(uint32_t now)
{
    if (!transition_active)
        return 255;
    uint8_t mix = ui_ease_out_cubic(now - transition_started, transition_closing ? 100U : 180U);
    if (mix == 255 && !transition_closing)
        transition_active = false;
    return transition_closing ? 255 - mix : mix;
}
int picker_navigation_offset(uint32_t now)
{
    int hidden = 255 - picker_navigation_mix(now);
    return (transition_closing ? -1 : 1) * (hidden * 10 + 127) / 255;
}
