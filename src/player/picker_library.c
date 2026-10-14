#include "player_internal.h"
#include "picker_navigation.h"
#include "media_browser.h"

enum { LIBRARY_HOVER_SHIFT = 6 };

static void library_rect(SDL_Surface *screen, int x, int y, int width, int height, Uint16 color)
{
    SDL_Rect rect = {x, y, width, height};
    fill_rect_rgb565(screen, &rect, color);
}

static Uint16 library_icon_color(uint8_t hover)
{
    return rgb565_lerp(picker_text_color(true), UI_COLOR_ACCENT_HOT, hover, 255);
}

static void library_folder_icon(SDL_Surface *screen, int x, int y, uint8_t hover)
{
    Uint16 edge = library_icon_color(hover);
    library_rect(screen, x + 1, y, 6, 2, edge);
    library_rect(screen, x, y + 2, 15, 10, UI_COLOR_GUNMETAL);
    library_rect(screen, x, y + 2, 15, 1, edge);
    library_rect(screen, x, y + 3, 1, 9, edge);
    library_rect(screen, x + 14, y + 3, 1, 9, edge);
    library_rect(screen, x + 1, y + 11, 13, 1, edge);
    library_rect(screen, x + 2, y + 4, 11, 1, ui_theme()->carbon);
}

static void library_movie_icon(SDL_Surface *screen, int x, int y, uint8_t hover)
{
    Uint16 color = library_icon_color(hover);
    library_rect(screen, x, y, 13, 11, color);
    library_rect(screen, x + 1, y + 1, 11, 9, UI_COLOR_BG_TOP);
    for (int row = 1; row < 10; row += 3) {
        library_rect(screen, x + 1, y + row, 1, 1, color);
        library_rect(screen, x + 11, y + row, 1, 1, color);
    }
    for (int row = 0; row < 6; ++row)
        library_rect(screen, x + 5, y + 2 + row, 1 + (row < 3 ? row : 5 - row), 1, color);
}

static const char *library_codec(const MovieFile *file)
{
    switch (file->metadata_codec) {
    case MOVIE_CODEC_H264:
        return "H.264";
    case MOVIE_CODEC_HEVC:
        return "HEVC";
    case MOVIE_CODEC_AV1:
        return "AV1";
    case MOVIE_CODEC_MPEG4:
        return "MPEG-4";
    default:
        return "Video";
    }
}

static void library_panel(SDL_Surface *screen, const SDL_Rect *rect, Uint16 color, uint8_t mix)
{
    if (!mix)
        return;
    SDL_Surface *target;
    SDL_Rect clip;
    int dx, dy;
    SDL_Surface *layer = begin_faded_region_draw(screen, rect, mix, &target, &clip, &dx, &dy);
    SDL_Rect box = offset_sdl_rect(rect, dx, dy);
    draw_soft_glass_panel_body_from_y(target, &box, box.y, color);
    end_faded_region_draw(screen, layer, rect, &clip, mix);
}

static void library_separator(SDL_Surface *screen, const Fonts *fonts, int y, int x_offset,
                              uint8_t mix)
{
    int width = nSDL_GetStringWidth(fonts->white, "USB"),
        text_x = (SCREEN_W - width) / 2 + x_offset;
    int line_y = y + 7, left = 12 + x_offset, right = SCREEN_W - 12 + x_offset;
    SDL_Rect left_line = {left, line_y, text_x - 10 - left, 1},
             right_line = {text_x + width + 10, line_y, right - (text_x + width + 10), 1};
    fill_rect_rgb565_mix(screen, &left_line, ui_theme()->row_divider, mix);
    fill_rect_rgb565_mix(screen, &right_line, ui_theme()->row_divider, mix);
    draw_ui_label_tint(screen, fonts, text_x, y + 3, "USB", picker_text_color(true), mix);
}

static void library_footer(SDL_Surface *screen, const Fonts *fonts, const MovieFile *files,
                           size_t count, int offset, uint8_t mix)
{
    SDL_Rect panel = {6, 218 + offset, 308, 18};
    library_panel(screen, &panel, ui_theme()->footer_panel, mix);
    SDL_Rect accent = {8, 218 + offset, 304, 1};
    fill_rect_rgb565_mix(screen, &accent, ui_theme()->footer_accent_top, mix);
    draw_ui_label_tint(screen, fonts, 12, 224 + offset,
                       "Made by GigaZelensky",
                       picker_text_color(false), mix);
    size_t items = count;
    for (size_t index = 0; index < count; ++index)
        if (files[index].kind == PICKER_SEPARATOR)
            --items;
    char label[32];
    snprintf(label, sizeof(label), "%lu %s", (unsigned long)items, items == 1 ? "item" : "items");
    draw_ui_label_tint(screen, fonts, 212 - nSDL_GetStringWidth(fonts->white, label) / 2,
                       224 + offset, label, picker_text_color(true), mix);
    clock_menu_draw_footer(screen, fonts, offset, mix);
}

void render_picker(SDL_Surface *screen, const Fonts *fonts, MovieFile *files, size_t count,
                   size_t scroll_start, int scroll_offset_y, size_t selected,
                   size_t previous_selected, uint32_t selection_anim_started_ms,
                   uint8_t selected_start_mix, uint8_t previous_start_mix, int movie_tooltip_index,
                   uint8_t movie_tooltip_mix, int resume_badge_hover_index,
                   uint8_t resume_badge_hover_mix, int resume_tooltip_index,
                   uint8_t resume_tooltip_mix, int pressed_row_index,
                   int pressed_resume_badge_index, uint8_t press_mix, const PointerState *pointer,
                   const ScreenshotPreviewState *screenshot_preview, uint32_t now_ms,
                   uint32_t intro_started_ms, uint32_t exit_elapsed_ms, uint8_t loading_mix,
                   const char *loading_label, int loading_phase, bool present_output)
{
    uint8_t background_mix = picker_intro_mix_for_transition(intro_started_ms, now_ms, 0,
                                                             PICKER_INTRO_ANIM_MS, exit_elapsed_ms);
    uint8_t header_mix =
        picker_intro_mix_for_transition(intro_started_ms, now_ms, 18, 180, exit_elapsed_ms);
    uint8_t footer_mix =
        picker_intro_mix_for_transition(intro_started_ms, now_ms, 100, 180, exit_elapsed_ms);
    SDL_Rect background = {0, 0, SCREEN_W, SCREEN_H};
    draw_vertical_gradient(screen, &background,
                           rgb565_lerp(UI_COLOR_BLACK, UI_COLOR_BG_TOP, background_mix, 255),
                           rgb565_lerp(UI_COLOR_BLACK, UI_COLOR_BG_BOTTOM, background_mix, 255));
    screenshot_set_protected_content(false);

    int header_offset = -picker_intro_offset(header_mix, 8);
    SDL_Rect header = {0, header_offset, SCREEN_W, 44};
    library_panel(screen, &header, UI_COLOR_GUNMETAL, header_mix);
    draw_ui_label_tint(screen, fonts,
                       (SCREEN_W - nSDL_GetStringWidth(fonts->white, "ND Video Player")) / 2,
                       4 + header_offset, "ND Video Player", UI_COLOR_WHITE, header_mix);
    picker_navigation_draw_shortcuts(screen, fonts, header_mix, header_offset);
    picker_navigation_draw(screen, fonts, header_mix, header_offset);

    size_t start = picker_scroll_start_clamped(count, scroll_start);
    size_t end = start + PICKER_VISIBLE_ROWS + (scroll_offset_y != 0);
    if (end > count)
        end = count;
    SDL_Rect old_clip,
        rows_clip = {0, PICKER_LIST_TOP, SCREEN_W, PICKER_LIST_BOTTOM - PICKER_LIST_TOP};
    SDL_GetClipRect(screen, &old_clip);
    SDL_SetClipRect(screen, &rows_clip);
    int y = PICKER_LIST_TOP + scroll_offset_y;
    for (size_t index = start; index < end && y < PICKER_LIST_BOTTOM; ++index) {
        MovieFile *file = files + index;
        int step = picker_item_step(files, index);
        uint8_t mix = picker_intro_mix_for_transition(intro_started_ms, now_ms,
                                                      115U + (uint32_t)(index - start) *
                                                                 PICKER_INTRO_ROW_STAGGER_MS,
                                                      PICKER_INTRO_ANIM_MS - 120U, exit_elapsed_ms);
        uint8_t metadata =
            file->timing_checked ? ui_ease_out_cubic(now_ms - file->metadata_ready_ms, 120U) : 0;
        if (mix > metadata)
            mix = metadata;
        mix = (unsigned)mix * picker_navigation_mix(now_ms) / 255U;
        int row_y = y + picker_intro_offset(mix, 9), offset = picker_navigation_offset(now_ms);
        if (file->kind == PICKER_SEPARATOR) {
            library_separator(screen, fonts, row_y, offset, mix);
            y += step;
            continue;
        }
        if (row_y + 28 <= PICKER_LIST_TOP || row_y >= PICKER_LIST_BOTTOM) {
            y += step;
            continue;
        }
        SDL_Rect row = picker_row_rect_for_y(row_y);
        row.x += offset;
        uint8_t hover =
            picker_row_selection_mix(index, selected, previous_selected, selection_anim_started_ms,
                                     now_ms, selected_start_mix, previous_start_mix);
        uint8_t pushed = pressed_row_index == (int)index ? press_mix : 0;
        uint8_t selection = (unsigned)hover * mix / 255U;
        int press_x = pressed_control_offset_x(pushed);
        int press_y = pressed_control_offset_y(pushed);
        SDL_Rect button = {row.x + press_x, row.y + press_y, row.w - 2 * press_x, row.h - press_y};
        if (selection) {
            Uint16 base = pressed_control_base(ui_theme()->row_selected, pushed);
            draw_picker_selection_panel_mix(screen, &button, base, selection);
            draw_pressed_control_reflection(screen, &button, (unsigned)pushed * selection / 255U);
            cut_rect_corners(screen, &button,
                             picker_background_color_at_y_mix(button.y, background_mix));
        }
        SDL_Rect divider = picker_row_divider_rect(&row);
        if (index + 1 >= count || files[index + 1].kind != PICKER_SEPARATOR)
            fill_rect_rgb565_mix(screen, &divider, ui_theme()->row_divider, mix);
        SDL_Rect resume = {0};
        bool have_resume = picker_resume_badge_rect(fonts, file, row.y, &resume);
        resume.x += offset;
        int hover_offset = ((int)selection * LIBRARY_HOVER_SHIFT + 127) / 255;
        int content_offset = offset + hover_offset + press_x;
        int text_x = 40 + content_offset, text_y = row.y + 5 + press_y;
        int right = have_resume ? resume.x - 8 : row.x + row.w - 12;
        if (file->kind == PICKER_FOLDER)
            right -= 8;
        /* Reserve the complete slide so long labels keep the same ellipsis during hover. */
        int text_width = right - (40 + LIBRARY_HOVER_SHIFT + offset);
        char title[128];
        copy_fitted_text(fonts->white, file->name, title, sizeof(title), text_width);
        if (mix > 42) {
            /* Center each glyph in the same 15 by 12 pixel icon slot. */
            if (file->kind == PICKER_FOLDER)
                library_folder_icon(screen, 16 + content_offset, row.y + 5 + press_y, hover);
            else if (file->encrypted)
                draw_lock_icon_tinted(screen, 16 + content_offset + (15 - 8) / 2,
                               row.y + 5 + press_y + (12 - 10) / 2, library_icon_color(hover), mix);
            else
                library_movie_icon(screen, 17 + content_offset, row.y + 5 + press_y, hover);
        }
        draw_ui_label_tint(screen, fonts, text_x, text_y, title, picker_text_color(false), mix);
        if (file->kind == PICKER_FOLDER) {
            draw_ui_label_tint(screen, fonts, 294 + offset + press_x, row.y + 6 + press_y, ">",
                               picker_text_color(true), mix);
            char count_text[64], fitted_count[64];
            if (!file->folder_count_valid)
                snprintf(count_text, sizeof(count_text), "Folder");
            else if (!file->folder_files && !file->folder_folders)
                snprintf(count_text, sizeof(count_text), "Empty folder");
            else if (file->folder_files && file->folder_folders)
                snprintf(count_text, sizeof(count_text), "%lu %s, %lu %s",
                         (unsigned long)file->folder_files, file->folder_files == 1 ? "video" : "videos",
                         (unsigned long)file->folder_folders, file->folder_folders == 1 ? "folder" : "folders");
            else
                snprintf(count_text, sizeof(count_text), "%lu %s",
                         (unsigned long)(file->folder_files ? file->folder_files : file->folder_folders),
                         file->folder_files ? (file->folder_files == 1 ? "video" : "videos")
                                            : (file->folder_folders == 1 ? "folder" : "folders"));
            uint8_t count_mix = file->folder_count_valid
                ? ui_ease_out_cubic(now_ms - file->folder_count_ready_ms, 100U) : 255;
            copy_fitted_text(fonts->white, count_text, fitted_count, sizeof(fitted_count), text_width);
            draw_ui_label_tint(screen, fonts, text_x, row.y + 16 + press_y, fitted_count,
                               picker_text_color(true), (unsigned)mix * count_mix / 255U);
        } else {
            char duration[24] = {0}, detail[256], fitted[256], codec[32];
            snprintf(codec, sizeof(codec), "%s%s%s", file->encrypted ? "Locked" : "",
                     file->encrypted && file->metadata_codec != MOVIE_CODEC_UNKNOWN ? " " : "",
                     file->encrypted && file->metadata_codec == MOVIE_CODEC_UNKNOWN ? "" : library_codec(file));
            if (file->duration_ms)
                format_clock(file->duration_ms, duration, sizeof(duration));
            const char *description = file->detail && *file->detail ? file->detail : duration;
            snprintf(detail, sizeof(detail), "%s%s%s", codec,
                     *description ? "    " : "", description);
            copy_fitted_text(fonts->white, detail, fitted, sizeof(fitted), text_width);
            draw_ui_label_tint(screen, fonts, text_x, row.y + 16 + press_y, fitted,
                               picker_text_color(true), mix);
        }
        if (have_resume)
            draw_resume_badge(screen, fonts, &resume,
                              resume_badge_hover_index == (int)index ? resume_badge_hover_mix : 0,
                              pressed_resume_badge_index == (int)index ? press_mix : 0, mix,
                              picker_background_color_at_y(resume.y));
        y += step;
    }
    SDL_SetClipRect(screen, &old_clip);
    if (!count)
        draw_ui_label_tint(screen, fonts, 16, 76, "No videos in this folder",
                           picker_text_color(false), header_mix);
    uint8_t scroll_mix =
        picker_intro_mix_for_transition(intro_started_ms, now_ms, 100, 200, exit_elapsed_ms);
    scroll_mix = (unsigned)scroll_mix * picker_navigation_mix(now_ms) / 255U;
    if (count > PICKER_VISIBLE_ROWS && scroll_mix) {
        int height = PICKER_LIST_BOTTOM - PICKER_LIST_TOP;
        int thumb_height =
            clamp_int((int)((uint64_t)height * PICKER_VISIBLE_ROWS / count), 18, height);
        int position = (int)start * 256;
        if (scroll_offset_y < 0)
            position += (-scroll_offset_y * 256) / picker_item_step(files, start);
        int maximum = (int)(count - PICKER_VISIBLE_ROWS) * 256;
        position = clamp_int(position, 0, maximum);
        int top = PICKER_LIST_TOP + (int)((uint64_t)(height - thumb_height) * position / maximum);
        int slide = picker_intro_offset(scroll_mix, 5);
        SDL_Rect track = {315 + slide, PICKER_LIST_TOP, 2, height},
                 thumb = {314 + slide, top, 4, thumb_height};
        SDL_Rect bounds = {thumb.x, PICKER_LIST_TOP, SCREEN_W - thumb.x, height}, saved_clip;
        if (bounds.w > thumb.w)
            bounds.w = thumb.w;
        SDL_Surface *target;
        int dx, dy;
        SDL_Surface *layer =
            begin_faded_region_draw(screen, &bounds, scroll_mix, &target, &saved_clip, &dx, &dy);
        SDL_Rect local_track = offset_sdl_rect(&track, dx, dy),
                 local_thumb = offset_sdl_rect(&thumb, dx, dy);
        draw_vertical_gradient(target, &local_track, ui_theme()->scroll_track_top,
                               ui_theme()->scroll_track_bottom);
        draw_soft_glass_panel_mix(target, &local_thumb, ui_theme()->scroll_thumb, 0);
        end_faded_region_draw(screen, layer, &bounds, &saved_clip, scroll_mix);
    }
    library_footer(screen, fonts, files, count, picker_intro_offset(footer_mix, 10), footer_mix);
    if (loading_label && loading_mix) {
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        dim_rect_rgb565(screen, &full, (UI_LOADING_DIM_ALPHA * (int)loading_mix + 127) / 255);
        draw_loading_overlay_mix(screen, fonts, loading_label, loading_phase, loading_mix, true);
    } else if (resume_tooltip_index >= 0 && (size_t)resume_tooltip_index < count &&
               resume_tooltip_mix) {
        draw_resume_hover_tooltip(screen, fonts, files + resume_tooltip_index, pointer,
                                  resume_tooltip_mix);
    } else if (movie_tooltip_index >= 0 && (size_t)movie_tooltip_index < count &&
               movie_tooltip_mix) {
        draw_movie_hover_tooltip(screen, fonts, files + movie_tooltip_index, pointer,
                                 movie_tooltip_mix);
    }
    draw_screenshot_preview_osd(screen, fonts, screenshot_preview, now_ms);
    night_mode_draw_status(screen, fonts, NULL, now_ms);
    if (pointer && pointer->visible)
        draw_cursor(screen, pointer->x, pointer->y);
    if (present_output)
        present_screen(screen);
}
