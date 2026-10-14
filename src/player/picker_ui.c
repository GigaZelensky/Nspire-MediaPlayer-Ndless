#include "player_internal.h"
#include "picker_navigation.h"
#include "media_browser.h"

SDL_Rect picker_row_rect_for_y(int row_y)
{
    const int width = SCREEN_W - 16;
    SDL_Rect row = {chrome_centered_x_for_width(width), (Sint16) row_y, width, 28};
    return row;
}

SDL_Rect picker_row_divider_rect(const SDL_Rect *row)
{
    const int width = SCREEN_W - 16;
    SDL_Rect divider = {chrome_centered_x_for_width(width), 0, width, 1};

    if (row) {
        divider.x = (Sint16) (row->x + 5);
        divider.y = (Sint16) (row->y + 29);
        divider.w = (Uint16) (row->w - 10);
    }
    return divider;
}

size_t picker_scroll_start_centered(size_t count, size_t selected)
{
    if (count == 0) {
        return 0;
    }
    {
        size_t start_index = selected > (PICKER_VISIBLE_ROWS / 2) ? selected - (PICKER_VISIBLE_ROWS / 2) : 0;

        if (start_index + PICKER_VISIBLE_ROWS > count) {
            start_index = count > PICKER_VISIBLE_ROWS ? count - PICKER_VISIBLE_ROWS : 0;
        }
        return start_index;
    }
}

size_t picker_scroll_start_clamped(size_t count, size_t start_index)
{
    if (start_index + PICKER_VISIBLE_ROWS > count) {
        start_index = count > PICKER_VISIBLE_ROWS ? count - PICKER_VISIBLE_ROWS : 0;
    }
    return start_index;
}

size_t picker_scroll_start_for_selection(size_t count, size_t selected, size_t start_index)
{
    start_index = picker_scroll_start_clamped(count, start_index);
    if (count == 0 || count <= PICKER_VISIBLE_ROWS) {
        return 0;
    }
    if (selected < start_index) {
        return selected;
    }
    if (selected >= start_index + PICKER_VISIBLE_ROWS) {
        return selected - PICKER_VISIBLE_ROWS + 1;
    }
    return start_index;
}

void movie_picker_cache_remember_position(
    MoviePickerCache *cache,
    size_t count,
    size_t selected,
    size_t scroll_start
)
{
    if (!cache || count == 0) {
        return;
    }
    if (selected >= count) {
        selected = count - 1;
    }
    cache->selected_index = selected;
    cache->scroll_start = picker_scroll_start_for_selection(count, selected, scroll_start);
    cache->has_position = true;
}

int picker_item_step(const MovieFile *files, size_t index)
{
    return files && files[index].kind == PICKER_SEPARATOR ? 20 : PICKER_ROW_STEP_PX;
}

void picker_scroll_anim_view(
    const MovieFile *files,
    PickerScrollAnim *anim,
    size_t scroll_start,
    uint32_t now_ms,
    size_t *display_start,
    int *offset_y
)
{
    if (display_start) {
        *display_start = scroll_start;
    }
    if (offset_y) {
        *offset_y = 0;
    }
    if (!anim || anim->started_ms == 0 || anim->from_start == anim->to_start) {
        return;
    }
    if ((uint32_t) (now_ms - anim->started_ms) >= PICKER_SCROLL_ANIM_MS) {
        anim->started_ms = 0;
        return;
    }
    {
        uint8_t mix = ui_ease_out_cubic(now_ms - anim->started_ms, PICKER_SCROLL_ANIM_MS);
        size_t leading = anim->to_start > anim->from_start ? anim->from_start : anim->to_start;
        int distance = picker_item_step(files, leading);
        if (anim->to_start > anim->from_start) {
            if (display_start) {
                *display_start = anim->from_start;
            }
            if (offset_y) {
                *offset_y = -((distance * (int) mix + 127) / 255);
            }
        } else {
            if (display_start) {
                *display_start = anim->to_start;
            }
            if (offset_y) {
                *offset_y = -distance + ((distance * (int) mix + 127) / 255);
            }
        }
    }
}

void picker_scroll_to(
    size_t count,
    size_t *scroll_start,
    PickerScrollAnim *anim,
    size_t next_start,
    uint32_t now_ms
)
{
    if (!scroll_start) {
        return;
    }
    next_start = picker_scroll_start_clamped(count, next_start);
    if (*scroll_start == next_start) {
        return;
    }
    if (anim) {
        size_t old_start = *scroll_start;
        if ((old_start + 1 == next_start) || (next_start + 1 == old_start)) {
            anim->from_start = old_start;
            anim->to_start = next_start;
            anim->started_ms = now_ms ? now_ms : 1U;
        } else {
            anim->from_start = next_start;
            anim->to_start = next_start;
            anim->started_ms = 0;
        }
    }
    *scroll_start = next_start;
}

int picker_hover_scroll_direction(size_t count, size_t start_index, const PointerState *pointer)
{
    SDL_Rect first_row;
    SDL_Rect last_row;
    int region_left;
    int region_right;
    int region_top;
    int region_bottom;
    int top_edge;
    int bottom_edge;

    if (!pointer || !pointer->visible || count <= PICKER_VISIBLE_ROWS) {
        return 0;
    }
    start_index = picker_scroll_start_clamped(count, start_index);
    first_row = picker_row_rect_for_y(PICKER_LIST_TOP);
    last_row = picker_row_rect_for_y(PICKER_LIST_TOP + ((PICKER_VISIBLE_ROWS - 1) * PICKER_ROW_STEP_PX));
    region_left = first_row.x;
    region_right = SCREEN_W - 4;
    region_top = first_row.y - 10;
    region_bottom = SCREEN_H - 24;
    top_edge = PICKER_LIST_TOP;
    bottom_edge = last_row.y + PICKER_ROW_STEP_PX;
    if (pointer->x < region_left || pointer->x >= region_right ||
        pointer->y < region_top || pointer->y >= region_bottom) {
        return 0;
    }
    if (pointer->y < top_edge && start_index > 0) {
        return -1;
    }
    if (pointer->y >= bottom_edge && start_index + PICKER_VISIBLE_ROWS < count) {
        return 1;
    }
    return 0;
}

int picker_row_index_at(const MovieFile *files, size_t count, size_t start_index, int row_offset_y, int x, int y)
{
    size_t end_index;
    size_t index;
    int row_y = PICKER_LIST_TOP + row_offset_y;

    if (count == 0 || y < PICKER_LIST_TOP || y >= PICKER_LIST_BOTTOM) {
        return -1;
    }
    start_index = picker_scroll_start_clamped(count, start_index);
    end_index = start_index + PICKER_VISIBLE_ROWS;
    if (row_offset_y != 0) {
        ++end_index;
    }
    if (end_index > count) {
        end_index = count;
    }
    for (index = start_index; index < end_index && row_y < SCREEN_H - 20; ++index) {
        SDL_Rect row = picker_row_rect_for_y(row_y);

        if (files[index].kind != PICKER_SEPARATOR && rect_contains_point(&row, x, y)) {
            return (int) index;
        }
        row_y += picker_item_step(files, index);
    }
    return -1;
}

bool picker_resume_badge_rect(const Fonts *fonts, const MovieFile *file, int row_y, SDL_Rect *rect)
{
    int text_w;

    if (!fonts || !file || !file->has_resume || !rect) {
        return false;
    }
    text_w = nSDL_GetStringWidth(fonts->white, "RESUME");
    {
        SDL_Rect row = picker_row_rect_for_y(row_y);
        rect->x = (Sint16) (row.x + row.w - 8 - text_w - 12);
    }
    rect->y = (Sint16) (row_y + 6);
    rect->w = (Uint16) (text_w + 12);
    rect->h = 16;
    return true;
}

int picker_resume_badge_index_at(
    const Fonts *fonts,
    MovieFile *files,
    size_t count,
    size_t start_index,
    int row_offset_y,
    int x,
    int y
)
{
    size_t end_index;
    size_t index;
    int row_y = PICKER_LIST_TOP + row_offset_y;

    if (!fonts || !files || count == 0 || y < PICKER_LIST_TOP || y >= PICKER_LIST_BOTTOM) {
        return -1;
    }
    start_index = picker_scroll_start_clamped(count, start_index);
    end_index = start_index + PICKER_VISIBLE_ROWS;
    if (row_offset_y != 0) {
        ++end_index;
    }
    if (end_index > count) {
        end_index = count;
    }
    for (index = start_index; index < end_index && row_y < SCREEN_H - 20; ++index) {
        SDL_Rect badge;
        if (picker_resume_badge_rect(fonts, &files[index], row_y, &badge) &&
            x >= badge.x && x < badge.x + badge.w &&
            y >= badge.y && y < badge.y + badge.h) {
            return (int) index;
        }
        row_y += picker_item_step(files, index);
    }
    return -1;
}

void draw_loading_overlay_mix(
    SDL_Surface *screen,
    const Fonts *fonts,
    const char *label,
    int phase,
    uint8_t mix,
    bool opening
)
{
    SDL_Rect panel = {88, 92, 136, 30};
    SDL_Rect accent = {90, 93, 132, 1};
    char spinner[8];
    const char *spinner_full = "...";
    int dot_count = (phase % 3) + 1;
    int offset_y;
    int spinner_x;
    SDL_Surface *composite = NULL;

    if (!screen || !fonts || mix == 0) {
        return;
    }

    memset(spinner, '.', (size_t) dot_count);
    spinner[dot_count] = '\0';
    spinner_x = panel.w - 18 - nSDL_GetStringWidth(fonts->white, spinner_full);

    offset_y = ((255 - mix) * 5 + 127) / 255;
    if (!opening) {
        offset_y = -(((255 - mix) * 8 + 127) / 255);
    }
    panel.y = (Sint16) (panel.y + offset_y);
    accent.y = (Sint16) (accent.y + offset_y);

    if (mix < 255 && panel.x >= 0 && panel.y >= 0 &&
        panel.x + panel.w <= screen->w && panel.y + panel.h <= screen->h) {
        SDL_Rect source = panel;
        SDL_Rect local_panel = {0, 0, panel.w, panel.h};
        SDL_Rect local_accent = {2, 1, accent.w, accent.h};

        composite = create_rgb565_surface(panel.w, panel.h);
        if (composite) {
            SDL_BlitSurface(screen, &source, composite, NULL);
            draw_soft_glass_panel(composite, &local_panel, UI_COLOR_GUNMETAL, false);
            draw_vertical_gradient(composite, &local_accent, UI_COLOR_ACCENT, UI_COLOR_ACCENT_DEEP);
            draw_ui_label(composite, fonts, 12, 10, label ? label : "Loading");
            draw_ui_label(composite, fonts, spinner_x, 10, spinner);
            blit_surface_rgb565_mix(screen, composite, &panel, mix);
            SDL_FreeSurface(composite);
            return;
        }
    }

    draw_soft_glass_panel(screen, &panel, UI_COLOR_GUNMETAL, false);
    draw_vertical_gradient(screen, &accent, UI_COLOR_ACCENT, UI_COLOR_ACCENT_DEEP);
    draw_ui_label(screen, fonts, panel.x + 12, panel.y + 10, label ? label : "Loading");
    draw_ui_label(screen, fonts, panel.x + spinner_x, panel.y + 10, spinner);
}

void draw_loading_overlay_label_mix(
    SDL_Surface *screen,
    const Fonts *fonts,
    const SDL_Rect *panel,
    const char *label,
    int phase,
    uint8_t mix
)
{
    SDL_Rect source;
    SDL_Surface *composite;
    char spinner[8];
    const char *spinner_full = "...";
    const char *label_text = label ? label : "Loading";
    int dot_count = (phase % 3) + 1;
    int label_h;
    int label_y;
    int spinner_w;
    int spinner_x;

    if (!screen || !fonts || !panel || panel->w <= 0 || panel->h <= 0 || mix == 0) {
        return;
    }

    memset(spinner, '.', (size_t) dot_count);
    spinner[dot_count] = '\0';
    label_h = nSDL_GetStringHeight(fonts->white, label_text);
    label_y = (panel->h - label_h) / 2;
    if (label_y < 5) {
        label_y = 5;
    } else if (label_y > 10) {
        label_y = 10;
    }
    spinner_w = nSDL_GetStringWidth(fonts->white, spinner_full);
    spinner_x = panel->w - 18 - spinner_w;
    if (spinner_x < 70) {
        spinner_x = 70;
    }

    if (mix >= 255) {
        draw_ui_label(screen, fonts, panel->x + 12, panel->y + label_y, label_text);
        draw_ui_label(screen, fonts, panel->x + spinner_x, panel->y + label_y, spinner);
        return;
    }

    if (panel->x < 0 || panel->y < 0 ||
        panel->x + panel->w > screen->w ||
        panel->y + panel->h > screen->h) {
        return;
    }

    source = *panel;
    composite = create_rgb565_surface(panel->w, panel->h);
    if (!composite) {
        return;
    }
    SDL_BlitSurface(screen, &source, composite, NULL);
    draw_ui_label(composite, fonts, 12, label_y, label_text);
    draw_ui_label(composite, fonts, spinner_x, label_y, spinner);
    blit_surface_rgb565_mix(screen, composite, panel, mix);
    SDL_FreeSurface(composite);
}

SDL_Surface *capture_screen_surface(SDL_Surface *screen)
{
    SDL_Surface *snapshot;

    if (!screen || screen->w <= 0 || screen->h <= 0) {
        return NULL;
    }
    snapshot = create_rgb565_surface(screen->w, screen->h);
    if (!snapshot) {
        return NULL;
    }
    SDL_BlitSurface(screen, NULL, snapshot, NULL);
    return snapshot;
}

SDL_Surface *capture_rect_surface(SDL_Surface *screen, const SDL_Rect *source)
{
    SDL_Surface *snapshot;

    if (!screen || !source || source->w <= 0 || source->h <= 0 ||
        source->x < 0 || source->y < 0 ||
        source->x + source->w > screen->w ||
        source->y + source->h > screen->h) {
        return NULL;
    }
    snapshot = create_rgb565_surface(source->w, source->h);
    if (!snapshot) {
        return NULL;
    }
    SDL_BlitSurface(screen, (SDL_Rect *) source, snapshot, NULL);
    return snapshot;
}

SDL_Surface *begin_faded_region_draw(
    SDL_Surface *screen,
    const SDL_Rect *bounds,
    uint8_t mix,
    SDL_Surface **target,
    SDL_Rect *old_clip,
    int *dx,
    int *dy
)
{
    SDL_Surface *surface = NULL;

    *target = screen;
    *dx = 0;
    *dy = 0;
    SDL_GetClipRect(screen, old_clip);
    if (mix < 255) {
        surface = capture_rect_surface(screen, bounds);
        if (surface) {
            *target = surface;
            *dx = -bounds->x;
            *dy = -bounds->y;
            return surface;
        }
    }
    SDL_SetClipRect(screen, (SDL_Rect *) bounds);
    return NULL;
}

void end_faded_region_draw(
    SDL_Surface *screen,
    SDL_Surface *surface,
    const SDL_Rect *bounds,
    const SDL_Rect *old_clip,
    uint8_t mix
)
{
    if (surface) {
        blit_surface_rgb565_mix(screen, surface, bounds, mix);
        SDL_FreeSurface(surface);
        return;
    }
    SDL_SetClipRect(screen, (SDL_Rect *) old_clip);
}

SDL_Surface *create_black_screen_snapshot(SDL_Surface *screen)
{
    SDL_Surface *snapshot;
    int width = screen && screen->w > 0 ? screen->w : SCREEN_W;
    int height = screen && screen->h > 0 ? screen->h : SCREEN_H;

    snapshot = create_rgb565_surface(width, height);
    if (!snapshot) {
        return NULL;
    }
    SDL_FillRect(snapshot, NULL, SDL_MapRGB(snapshot->format, 0, 0, 0));
    return snapshot;
}

void draw_loading_transition_frame(
    SDL_Surface *screen,
    SDL_Surface *snapshot,
    const Fonts *fonts,
    const char *label,
    uint8_t mix,
    int phase,
    bool opening,
    bool dim_background
)
{
    SDL_Rect full_screen = {0, 0, SCREEN_W, SCREEN_H};

    if (snapshot) {
        SDL_BlitSurface(snapshot, NULL, screen, NULL);
    } else {
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 8, 10, 14));
    }
    if (dim_background) {
        dim_rect_rgb565(screen, &full_screen, (UI_LOADING_DIM_ALPHA * mix + 127) / 255);
    }
    draw_loading_overlay_mix(screen, fonts, label, phase, mix, opening);
    present_screen(screen);
}

void loading_progress_init(
    LoadingProgress *progress,
    SDL_Surface *screen,
    SDL_Surface *snapshot,
    const Fonts *fonts,
    const char *label,
    bool dim_background
)
{
    if (!progress) {
        return;
    }
    progress->screen = screen;
    progress->snapshot = snapshot;
    progress->fonts = fonts;
    progress->label = label;
    progress->started_ms = monotonic_clock_now_ms();
    progress->last_draw_ms = 0;
    progress->dim_background = dim_background;
}

void loading_progress_tick(LoadingProgress *progress, bool force)
{
    uint32_t now_ms;

    if (!progress || !progress->screen || !progress->fonts) {
        return;
    }
    /* Finish the current loading operation before entering a clock modal. */
    clock_menu_queue_shortcut();
    now_ms = monotonic_clock_now_ms();
    if (!force &&
        progress->last_draw_ms != 0 &&
        now_ms - progress->last_draw_ms < UI_LOADING_PROGRESS_FRAME_MS) {
        return;
    }
    if (progress->last_draw_ms == 0 && !force) {
        progress->last_draw_ms = now_ms;
        return;
    }
    draw_loading_transition_frame(
        progress->screen,
        progress->snapshot,
        progress->fonts,
        progress->label,
        255,
        (int) (((now_ms - progress->started_ms) / 180U) % 3U),
        true,
        progress->dim_background
    );
    progress->last_draw_ms = now_ms;
}

void animate_loading_transition_ex(
    SDL_Surface *screen,
    SDL_Surface *snapshot,
    const Fonts *fonts,
    const char *label,
    bool opening,
    bool dim_background
)
{
    uint32_t started_ms = monotonic_clock_now_ms();

    while (1) {
        uint32_t now_ms = monotonic_clock_now_ms();
        uint8_t eased = ui_ease_out_cubic(now_ms - started_ms, UI_LOADING_ANIM_MS);
        uint8_t mix = opening ? eased : (uint8_t) (255U - eased);

        draw_loading_transition_frame(
            screen,
            snapshot,
            fonts,
            label,
            mix,
            (int) (((now_ms - started_ms) / 180U) % 3U),
            opening,
            dim_background
        );
        if (eased >= 255) {
            break;
        }
        player_delay_ms(16);
    }
}

void animate_loading_transition(
    SDL_Surface *screen,
    SDL_Surface *snapshot,
    const Fonts *fonts,
    const char *label,
    bool opening
)
{
    animate_loading_transition_ex(screen, snapshot, fonts, label, opening, true);
}

void finish_loading_transition_ex(
    SDL_Surface *screen,
    SDL_Surface **snapshot,
    const Fonts *fonts,
    const char *label,
    bool dim_background
)
{
    if (!snapshot || !*snapshot) {
        return;
    }
    animate_loading_transition_ex(screen, *snapshot, fonts, label, false, dim_background);
    SDL_FreeSurface(*snapshot);
    *snapshot = NULL;
}

void finish_loading_transition(
    SDL_Surface *screen,
    SDL_Surface **snapshot,
    const Fonts *fonts,
    const char *label
)
{
    finish_loading_transition_ex(screen, snapshot, fonts, label, true);
}

SDL_Rect movie_vertical_morph_dst(const SDL_Rect *base_dst, uint8_t visible_mix)
{
    SDL_Rect dst = *base_dst;
    int visible_h = ((int) base_dst->h * (int) visible_mix + 127) / 255;

    dst.y = (Sint16) (base_dst->y + (base_dst->h - visible_h) / 2);
    dst.h = (Uint16) visible_h;
    return dst;
}

void animate_movie_collapse_to_black(
    SDL_Surface *screen,
    Movie *movie,
    ScaleMode scale_mode,
    VideoAlign video_align_x,
    VideoAlign video_align_y,
    SDL_Surface **overlay_surface
)
{
    SDL_Rect src;
    SDL_Rect start_dst;
    uint32_t started_ms = monotonic_clock_now_ms();

    if (!screen || !movie || !movie->frame_surface) {
        present_black_screen(screen);
        if (overlay_surface && *overlay_surface) {
            SDL_FreeSurface(*overlay_surface);
            *overlay_surface = NULL;
        }
        return;
    }

    compute_video_rects(movie, scale_mode, video_align_x, video_align_y, &src, &start_dst);
    while (1) {
        uint32_t now_ms = monotonic_clock_now_ms();
        uint32_t elapsed_ms = now_ms - started_ms;
        uint8_t collapse_mix = ui_ease_smoothstep(elapsed_ms, UI_RETURN_COLLAPSE_ANIM_MS);
        uint8_t visible_mix = (uint8_t) (255U - collapse_mix);
        SDL_Rect dst = movie_vertical_morph_dst(&start_dst, visible_mix);

        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
        if (dst.h > 0) {
            draw_movie_frame_scaled_clipped(screen, movie, &src, &dst);
        }
        if (overlay_surface && *overlay_surface && collapse_mix < 255) {
            uint8_t overlay_mix = (uint8_t) (255U - ui_ease_out_cubic(elapsed_ms, UI_CHROME_ANIM_MS));

            if (overlay_mix > 0) {
                SDL_Rect overlay_dst = {0, 0, 0, 0};
                blit_surface_rgb565_mix(screen, *overlay_surface, &overlay_dst, overlay_mix);
            }
        }
        present_screen(screen);
        if (collapse_mix >= 255) {
            break;
        }
        player_delay_ms(16);
    }
    present_black_screen(screen);
    if (overlay_surface && *overlay_surface) {
        SDL_FreeSurface(*overlay_surface);
        *overlay_surface = NULL;
    }
}

void copy_fitted_text(nSDL_Font *font, const char *text, char *buffer, size_t buffer_size, int max_width)
{
    const char *ellipsis = "...";
    size_t length;

    if (!buffer || buffer_size == 0) {
        return;
    }

    if (!text) {
        buffer[0] = '\0';
        return;
    }

    snprintf(buffer, buffer_size, "%s", text);
    if (max_width <= 0) {
        buffer[0] = '\0';
        return;
    }
    if (!font || nSDL_GetStringWidth(font, buffer) <= max_width) {
        return;
    }

    if (nSDL_GetStringWidth(font, ellipsis) > max_width || buffer_size < 4) {
        buffer[0] = '\0';
        return;
    }

    length = strlen(buffer);
    while (length > 0) {
        --length;
        buffer[length] = '\0';
        if (length + 4 <= buffer_size) {
            memcpy(buffer + length, ellipsis, 4);
            if (nSDL_GetStringWidth(font, buffer) <= max_width) {
                return;
            }
            buffer[length] = '\0';
        }
    }

    snprintf(buffer, buffer_size, "%s", ellipsis);
}

void draw_resume_badge(
    SDL_Surface *screen,
    const Fonts *fonts,
    const SDL_Rect *badge,
    uint8_t hover_mix,
    uint8_t press_mix,
    uint8_t visible_mix,
    Uint16 background_color
)
{
    SDL_Rect draw_badge;
    SDL_Rect glint;
    int press_offset_x;
    int press_offset_y;
    Uint16 base;
    Uint16 panel_base;

    if (!screen || !fonts || !badge || visible_mix == 0) {
        return;
    }
    draw_badge = *badge;
    press_offset_x = pressed_control_offset_x(press_mix);
    press_offset_y = pressed_control_offset_y(press_mix);
    base = animated_control_color(UI_COLOR_GUNMETAL, ui_theme()->resume_hover, hover_mix);
    base = rgb565_lerp(background_color, base, visible_mix, 255);
    panel_base = pressed_control_base(base, press_mix);
    draw_soft_glass_panel_mix(
        screen,
        &draw_badge,
        panel_base,
        hover_mix
    );
    if (hover_mix > 0 && draw_badge.w > 6) {
        Uint16 glint_base = blend_rgb565(
            panel_base,
            UI_COLOR_WHITE,
            ui_mix_int(72, 112, hover_mix)
        );
        glint.x = (Sint16) (draw_badge.x + 2);
        glint.y = (Sint16) (draw_badge.y + 2);
        glint.w = (Uint16) (draw_badge.w - 4);
        glint.h = 1;
        fill_rect_rgb565(screen, &glint, rgb565_lerp(glint_base, UI_COLOR_ACCENT_HOT, hover_mix, 255));
    }
    draw_pressed_control_reflection(screen, &draw_badge, press_mix);
    draw_soft_glass_panel_rim(screen, &draw_badge, base, max_u8(hover_mix, press_mix));
    if (visible_mix > 54) {
        draw_ui_label(screen, fonts, draw_badge.x + 6 + press_offset_x, draw_badge.y + 4 + press_offset_y, "RESUME");
    }
}

uint8_t picker_selection_ease(uint32_t elapsed_ms, uint32_t duration_ms)
{
    return ui_ease_smoothstep(elapsed_ms, duration_ms);
}

uint8_t picker_row_selection_mix(
    size_t index,
    size_t selected,
    size_t previous_selected,
    uint32_t selection_anim_started_ms,
    uint32_t now_ms,
    uint8_t selected_start_mix,
    uint8_t previous_start_mix
)
{
    uint8_t eased;

    if (selected == previous_selected || selection_anim_started_ms == 0) {
        return index == selected ? 255 : 0;
    }
    eased = picker_selection_ease(now_ms - selection_anim_started_ms, PICKER_SELECTION_ANIM_MS);
    if (index == selected) {
        return (uint8_t) (selected_start_mix +
            (((255U - selected_start_mix) * eased + 127U) / 255U));
    }
    if (index == previous_selected) {
        uint8_t out_eased = picker_selection_ease(
            now_ms - selection_anim_started_ms,
            PICKER_DESELECTION_ANIM_MS
        );

        return (uint8_t) (previous_start_mix -
            ((previous_start_mix * out_eased + 127U) / 255U));
    }
    return 0;
}

void picker_set_selected_row(
    size_t *selected,
    size_t *previous_selected,
    uint32_t *selection_anim_started_ms,
    uint8_t *selected_start_mix,
    uint8_t *previous_start_mix,
    size_t next_selected,
    uint32_t now_ms
)
{
    uint8_t outgoing_mix;
    uint8_t incoming_mix;

    if (!selected || !previous_selected || !selection_anim_started_ms ||
        !selected_start_mix || !previous_start_mix || *selected == next_selected) {
        return;
    }

    outgoing_mix = picker_row_selection_mix(
        *selected,
        *selected,
        *previous_selected,
        *selection_anim_started_ms,
        now_ms,
        *selected_start_mix,
        *previous_start_mix
    );
    incoming_mix = picker_row_selection_mix(
        next_selected,
        *selected,
        *previous_selected,
        *selection_anim_started_ms,
        now_ms,
        *selected_start_mix,
        *previous_start_mix
    );

    *previous_selected = *selected;
    *selected = next_selected;
    *previous_start_mix = outgoing_mix;
    *selected_start_mix = incoming_mix;
    *selection_anim_started_ms = now_ms ? now_ms : 1U;
}

uint8_t prompt_button_selection_mix(
    size_t index,
    size_t selected,
    size_t previous_selected,
    uint32_t selection_anim_started_ms,
    uint32_t now_ms
)
{
    if (selected == previous_selected || selection_anim_started_ms == 0) {
        return index == selected ? 255 : 0;
    }
    if (index == selected) {
        return ui_ease_out_cubic(now_ms - selection_anim_started_ms, PROMPT_BUTTON_ANIM_MS);
    }
    return 0;
}

uint8_t picker_intro_mix(uint32_t intro_started_ms, uint32_t now_ms, uint32_t delay_ms, uint32_t duration_ms)
{
    uint32_t elapsed_ms;

    if (intro_started_ms == 0) {
        return 255;
    }
    elapsed_ms = now_ms - intro_started_ms;
    if (elapsed_ms <= delay_ms) {
        return 0;
    }
    return ui_ease_out_cubic(elapsed_ms - delay_ms, duration_ms);
}

uint32_t picker_exit_timeline_ms(uint32_t exit_elapsed_ms)
{
    uint64_t scaled_ms;

    if (exit_elapsed_ms >= PICKER_EXIT_TOTAL_ANIM_MS) {
        return PICKER_INTRO_TOTAL_ANIM_MS;
    }
    scaled_ms = (uint64_t) exit_elapsed_ms * (uint64_t) PICKER_INTRO_TOTAL_ANIM_MS;
    return (uint32_t) ((scaled_ms + PICKER_EXIT_TOTAL_ANIM_MS / 2U) / PICKER_EXIT_TOTAL_ANIM_MS);
}

uint8_t picker_intro_mix_for_transition(
    uint32_t intro_started_ms,
    uint32_t now_ms,
    uint32_t delay_ms,
    uint32_t duration_ms,
    uint32_t exit_elapsed_ms
)
{
    uint32_t timeline_ms;
    uint32_t start_ms;
    uint32_t end_ms;

    if (exit_elapsed_ms == PICKER_EXIT_INACTIVE) {
        return picker_intro_mix(intro_started_ms, now_ms, delay_ms, duration_ms);
    }
    timeline_ms = picker_exit_timeline_ms(exit_elapsed_ms);

    start_ms = delay_ms + duration_ms >= PICKER_INTRO_TOTAL_ANIM_MS
        ? 0U
        : (PICKER_INTRO_TOTAL_ANIM_MS - (delay_ms + duration_ms)) / PICKER_EXIT_START_DELAY_DIVISOR;
    end_ms = delay_ms >= PICKER_INTRO_TOTAL_ANIM_MS
        ? start_ms
        : PICKER_INTRO_TOTAL_ANIM_MS - delay_ms;

    if (timeline_ms <= start_ms) {
        return 255;
    }
    if (timeline_ms >= end_ms || end_ms <= start_ms) {
        return 0;
    }
    return (uint8_t) (255 - ui_ease_in_cubic(timeline_ms - start_ms, end_ms - start_ms));
}

int picker_intro_offset(uint8_t mix, int distance)
{
    return ((255 - mix) * distance + 127) / 255;
}

void picker_set_selected(
    size_t *selected,
    size_t *previous_selected,
    uint32_t *selection_anim_started_ms,
    size_t next_selected,
    uint32_t now_ms
)
{
    if (!selected || !previous_selected || !selection_anim_started_ms || *selected == next_selected) {
        return;
    }
    *previous_selected = *selected;
    *selected = next_selected;
    *selection_anim_started_ms = now_ms ? now_ms : 1U;
}

size_t picker_adjacent_selection(size_t count, size_t selected, int direction)
{
    if (count == 0) {
        return selected;
    }
    if (direction < 0) {
        return selected > 0 ? selected - 1 : count - 1;
    }
    return selected + 1 < count ? selected + 1 : 0;
}

