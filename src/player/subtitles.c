#include "player_internal.h"

#define SUBTITLE_OUTLINE_PX 1
#define SUBTITLE_LINE_GAP_PX 2

const char *active_subtitle_track_name(const Movie *movie)
{
    if (!movie || movie->subtitle_track_count == 0 || movie->selected_subtitle_track >= movie->subtitle_track_count) {
        return "No subtitles";
    }
    if (!movie->subtitle_tracks[movie->selected_subtitle_track].name || movie->subtitle_tracks[movie->selected_subtitle_track].name[0] == '\0') {
        return "Subtitles";
    }
    return movie->subtitle_tracks[movie->selected_subtitle_track].name;
}

void draw_outlined_text(SDL_Surface *surface, nSDL_Font *white_font, nSDL_Font *outline_font, int x, int y, const char *text)
{
    nSDL_DrawString(surface, outline_font, x - 1, y, "%s", text);
    nSDL_DrawString(surface, outline_font, x + 1, y, "%s", text);
    nSDL_DrawString(surface, outline_font, x, y - 1, "%s", text);
    nSDL_DrawString(surface, outline_font, x, y + 1, "%s", text);
    nSDL_DrawString(surface, white_font, x, y, "%s", text);
}

static int subtitle_text_padding_px(int scale_num, int scale_den)
{
    if (scale_num <= 0 || scale_den <= 0) return 0;
    if (scale_num == scale_den) {
        return SUBTITLE_OUTLINE_PX * 2;
    }
    return 2 * ((scale_num + scale_den - 1) / scale_den);
}

static int subtitle_text_draw_inset_px(int scale_num, int scale_den)
{
    if (scale_num == scale_den) {
        return SUBTITLE_OUTLINE_PX;
    }
    return 0;
}

static int subtitle_scaled_extent(int value, int scale_num, int scale_den)
{
    if (value <= 0 || scale_num <= 0 || scale_den <= 0) {
        return 0;
    }
    if (scale_num == scale_den) {
        return value;
    }
    return (value * scale_num) / scale_den;
}

static int subtitle_rendered_text_width(nSDL_Font *font, const char *text, int scale_num, int scale_den)
{
    int text_w;

    if (!font || !text) {
        return 0;
    }
    text_w = nSDL_GetStringWidth(font, text);
    return subtitle_scaled_extent(text_w, scale_num, scale_den) + subtitle_text_padding_px(scale_num, scale_den);
}

static int subtitle_rendered_text_height(nSDL_Font *font, const char *text, int scale_num, int scale_den)
{
    int text_h;

    if (!font || !text) {
        return 0;
    }
    text_h = nSDL_GetStringHeight(font, text);
    return subtitle_scaled_extent(text_h, scale_num, scale_den) + subtitle_text_padding_px(scale_num, scale_den);
}

static int subtitle_wrap_limit_width(int wrap_width, int scale_num, int scale_den)
{
    int padding = subtitle_text_padding_px(scale_num, scale_den);
    int limit;

    if (wrap_width <= padding) {
        return 1;
    }
    if (scale_num == scale_den) {
        limit = wrap_width - padding;
    } else {
        limit = ((wrap_width - padding) * scale_den) / scale_num;
    }
    return limit > 0 ? limit : 1;
}

static int subtitle_prepare_lines(
    nSDL_Font *font,
    const char *text,
    int wrap_width,
    int scale_num,
    int scale_den,
    char lines[MAX_SUBTITLE_LINES][MAX_SUBTITLE_LINE_LEN],
    int *out_line_height,
    int *out_max_line_width
)
{
    int line_count;
    int line_height;
    int max_line_width = 0;
    int line_index;

    if (!font || !text || !out_line_height || !out_max_line_width) {
        return 0;
    }

    line_count = wrap_subtitle(font, text, subtitle_wrap_limit_width(wrap_width, scale_num, scale_den), lines);
    if (line_count <= 0) {
        return 0;
    }

    line_height = subtitle_rendered_text_height(font, "Ag", scale_num, scale_den);
    if (line_height < 10) {
        line_height = 10;
    }
    for (line_index = 0; line_index < line_count; ++line_index) {
        int width = subtitle_rendered_text_width(font, lines[line_index], scale_num, scale_den);
        int height = subtitle_rendered_text_height(font, lines[line_index], scale_num, scale_den);
        if (width > max_line_width) {
            max_line_width = width;
        }
        if (height > line_height) {
            line_height = height;
        }
    }
    if (max_line_width <= 0 || line_height <= 0) {
        return 0;
    }

    *out_line_height = line_height;
    *out_max_line_width = max_line_width;
    return line_count;
}

static int subtitle_lines_height(int line_count, int line_height)
{
    if (line_count <= 0 || line_height <= 0) {
        return 0;
    }
    return (line_count * line_height) + ((line_count - 1) * SUBTITLE_LINE_GAP_PX);
}

static int subtitle_page_line_capacity(int max_height, int line_height)
{
    int capacity;

    if (line_height <= 0) {
        return 1;
    }
    if (max_height < line_height) {
        return 1;
    }
    capacity = (max_height + SUBTITLE_LINE_GAP_PX) / (line_height + SUBTITLE_LINE_GAP_PX);
    return capacity > 0 ? capacity : 1;
}

static int subtitle_layout_max_surface_height(const SubtitleLayoutSpec *layout)
{
    int top = 2;
    int bottom;
    const SDL_Rect *video_rect;

    if (!layout) {
        return SCREEN_H;
    }

    video_rect = &layout->video_rect;
    bottom = subtitle_visible_bottom_limit(layout, 0);

    if (layout->mode == SUBTITLE_CUE_POSITION_ABSOLUTE && layout->align >= 1 && layout->align <= 9) {
        int row = subtitle_align_row(layout->align);
        if (row == 0) {
            top = video_rect->y;
            bottom = layout->absolute_y;
        } else if (row == 2) {
            top = layout->absolute_y;
        } else {
            top = video_rect->y;
        }
    } else if (layout->mode == SUBTITLE_CUE_POSITION_MARGIN && layout->align >= 1 && layout->align <= 9) {
        int row = subtitle_align_row(layout->align);
        if (row == 0) {
            top = video_rect->y;
            bottom = subtitle_visible_bottom_limit(layout, layout->margin_v);
        } else if (row == 2) {
            top = video_rect->y + layout->margin_v;
        } else {
            top = video_rect->y;
        }
    } else {
        switch (layout->manual_placement) {
            case SUBTITLE_POS_BAR_BOTTOM:
                top = 2;
                bottom = SCREEN_H - 2 - ui_bar_visible_height_for_mix(layout->overlay_mix);
                break;
            case SUBTITLE_POS_VIDEO_TOP:
                top = video_rect->y + 4;
                bottom = subtitle_visible_bottom_limit(layout, 0);
                break;
            case SUBTITLE_POS_BAR_TOP:
                top = 4;
                bottom = subtitle_visible_bottom_limit(layout, 0);
                break;
            case SUBTITLE_POS_VIDEO_BOTTOM:
            default:
                top = video_rect->y + 4;
                bottom = subtitle_visible_bottom_limit(layout, 8);
                break;
        }
    }

    top = clamp_int(top, 0, SCREEN_H - 1);
    bottom = clamp_int(bottom, top + 1, SCREEN_H);
    return bottom - top;
}

static void subtitle_select_line_window(
    int line_count,
    int line_capacity,
    const SubtitleCue *cue,
    uint32_t current_ms,
    int *out_start_line,
    int *out_line_count
)
{
    int start_line = 0;
    int visible_lines;

    if (!out_start_line || !out_line_count) {
        return;
    }
    if (line_count <= 0) {
        *out_start_line = 0;
        *out_line_count = 0;
        return;
    }

    line_capacity = clamp_int(line_capacity, 1, line_count);
    if (line_capacity < line_count && cue && cue->end_ms > cue->start_ms) {
        uint32_t duration = cue->end_ms - cue->start_ms;
        uint32_t elapsed = current_ms > cue->start_ms ? current_ms - cue->start_ms : 0;
        uint32_t target_line;

        if (elapsed >= duration) {
            elapsed = duration - 1;
        }
        target_line = (uint32_t) (((uint64_t) elapsed * (uint64_t) line_count) / duration);
        if (target_line >= (uint32_t) line_count) {
            target_line = (uint32_t) (line_count - 1);
        }
        start_line = ((int) target_line / line_capacity) * line_capacity;
        if (start_line >= line_count) {
            start_line = line_count - line_capacity;
        }
    }

    visible_lines = line_count - start_line;
    if (visible_lines > line_capacity) {
        visible_lines = line_capacity;
    }
    *out_start_line = start_line;
    *out_line_count = visible_lines;
}

int wrap_subtitle(nSDL_Font *font, const char *text, int max_width, char lines[MAX_SUBTITLE_LINES][MAX_SUBTITLE_LINE_LEN])
{
    char current[MAX_SUBTITLE_LINE_LEN];
    size_t pos = 0;
    int line_count = 0;

    if (!font || !text || max_width <= 0) {
        return 0;
    }

    current[0] = '\0';
    memset(lines, 0, sizeof(char) * MAX_SUBTITLE_LINES * MAX_SUBTITLE_LINE_LEN);
    while (text[pos] != '\0' && line_count < MAX_SUBTITLE_LINES) {
        char word[64];
        size_t word_len = 0;
        while (text[pos] == ' ') {
            pos++;
        }
        if (text[pos] == '\n') {
            if (current[0] != '\0') {
                strncpy(lines[line_count], current, MAX_SUBTITLE_LINE_LEN - 1);
                line_count++;
                current[0] = '\0';
            }
            pos++;
            continue;
        }
        while (text[pos] != '\0' && text[pos] != ' ' && text[pos] != '\n' && word_len + 1 < sizeof(word)) {
            word[word_len++] = text[pos++];
        }
        word[word_len] = '\0';
        if (word_len == 0) {
            continue;
        }
        if (current[0] == '\0') {
            snprintf(current, sizeof(current), "%s", word);
        } else {
            char candidate[MAX_SUBTITLE_LINE_LEN];
            size_t current_len = strlen(current);
            if (current_len + 1 + word_len >= sizeof(candidate)) {
                strncpy(lines[line_count], current, MAX_SUBTITLE_LINE_LEN - 1);
                line_count++;
                if (line_count >= MAX_SUBTITLE_LINES) {
                    break;
                }
                snprintf(current, sizeof(current), "%s", word);
            } else {
                memcpy(candidate, current, current_len);
                candidate[current_len] = ' ';
                memcpy(candidate + current_len + 1, word, word_len + 1);
                if (nSDL_GetStringWidth(font, candidate) > max_width) {
                    strncpy(lines[line_count], current, MAX_SUBTITLE_LINE_LEN - 1);
                    line_count++;
                    if (line_count >= MAX_SUBTITLE_LINES) {
                        break;
                    }
                    snprintf(current, sizeof(current), "%s", word);
                } else {
                    memcpy(current, candidate, word_len + current_len + 2);
                }
            }
        }
    }
    if (current[0] != '\0' && line_count < MAX_SUBTITLE_LINES) {
        strncpy(lines[line_count], current, MAX_SUBTITLE_LINE_LEN - 1);
        line_count++;
    }
    return line_count;
}

int subtitle_scale_num(int subtitle_size)
{
    static const int numerators[4] = {1, 4, 5, 2};
    subtitle_size = clamp_int(subtitle_size, 0, 3);
    return numerators[subtitle_size];
}

int subtitle_scale_den(int subtitle_size)
{
    static const int denominators[4] = {1, 3, 3, 1};
    subtitle_size = clamp_int(subtitle_size, 0, 3);
    return denominators[subtitle_size];
}

int subtitle_font_id_for_index(size_t subtitle_font_index)
{
    if (subtitle_font_index >= SUBTITLE_FONT_CHOICE_COUNT) {
        return g_subtitle_font_choices[SUBTITLE_FONT_DEFAULT_INDEX];
    }
    return g_subtitle_font_choices[subtitle_font_index];
}

const char *subtitle_font_name_for_index(size_t subtitle_font_index)
{
    if (subtitle_font_index >= SUBTITLE_FONT_CHOICE_COUNT) {
        return g_subtitle_font_names[SUBTITLE_FONT_DEFAULT_INDEX];
    }
    return g_subtitle_font_names[subtitle_font_index];
}

SubtitlePlacement subtitle_opposite_placement(SubtitlePlacement placement)
{
    switch (placement) {
        case SUBTITLE_POS_AUTO:
            return SUBTITLE_POS_BAR_TOP;
        case SUBTITLE_POS_VIDEO_BOTTOM:
            return SUBTITLE_POS_VIDEO_TOP;
        case SUBTITLE_POS_VIDEO_TOP:
            return SUBTITLE_POS_VIDEO_BOTTOM;
        case SUBTITLE_POS_BAR_TOP:
            return SUBTITLE_POS_BAR_BOTTOM;
        case SUBTITLE_POS_BAR_BOTTOM:
        default:
            return SUBTITLE_POS_BAR_TOP;
    }
}

const char *subtitle_placement_label(SubtitlePlacement placement)
{
    switch (placement) {
        case SUBTITLE_POS_VIDEO_BOTTOM:
            return "VIDEO BOTTOM";
        case SUBTITLE_POS_VIDEO_TOP:
            return "VIDEO TOP";
        case SUBTITLE_POS_BAR_TOP:
            return "BAR TOP";
        case SUBTITLE_POS_AUTO:
            return "AUTO";
        case SUBTITLE_POS_BAR_BOTTOM:
        default:
            return "BAR BOTTOM";
    }
}

bool subtitle_track_supports_auto_positioning(const Movie *movie, uint16_t track_index)
{
    if (!movie || !movie->subtitle_tracks || movie->subtitle_track_count == 0 || track_index >= movie->subtitle_track_count) {
        return false;
    }
    return movie->subtitle_tracks[track_index].supports_positioning != 0;
}

bool selected_subtitle_track_supports_auto_positioning(const Movie *movie)
{
    if (!movie || movie->subtitle_track_count == 0 || movie->selected_subtitle_track >= movie->subtitle_track_count) {
        return false;
    }
    return subtitle_track_supports_auto_positioning(movie, movie->selected_subtitle_track);
}

SubtitlePlacement subtitle_normalize_placement(SubtitlePlacement placement, bool auto_supported)
{
    placement = (SubtitlePlacement) clamp_int((int) placement, SUBTITLE_POS_BAR_BOTTOM, SUBTITLE_POS_COUNT - 1);
    if (!auto_supported && placement == SUBTITLE_POS_AUTO) {
        return SUBTITLE_POS_BAR_BOTTOM;
    }
    return placement;
}

SubtitlePlacement subtitle_cycle_placement(SubtitlePlacement placement, bool auto_supported)
{
    SubtitlePlacement next = subtitle_normalize_placement(placement, auto_supported);

    do {
        next = (SubtitlePlacement) ((next + 1) % SUBTITLE_POS_COUNT);
    } while (!auto_supported && next == SUBTITLE_POS_AUTO);
    return next;
}

SubtitlePlacement subtitle_effective_manual_placement(SubtitlePlacement placement, SubtitlePlacement fallback)
{
    if (placement == SUBTITLE_POS_AUTO) {
        if (fallback == SUBTITLE_POS_AUTO) {
            return SUBTITLE_POS_BAR_BOTTOM;
        }
        return fallback;
    }
    return placement;
}

int subtitle_align_column(uint8_t align)
{
    int value = clamp_int((int) align, 1, 9) - 1;
    return value % 3;
}

int subtitle_align_row(uint8_t align)
{
    int value = clamp_int((int) align, 1, 9) - 1;
    return value / 3;
}

int subtitle_scale_coord(uint16_t value, int extent)
{
    if (extent <= 0) {
        return 0;
    }
    return (int) (((uint32_t) value * (uint32_t) extent + (SUBTITLE_COORD_SCALE / 2U)) / SUBTITLE_COORD_SCALE);
}

bool subtitle_resolve_layout_spec(
    const SDL_Rect *video_rect,
    uint8_t overlay_mix,
    SubtitlePlacement placement,
    SubtitlePlacement manual_fallback,
    const SubtitleCue *cue,
    SubtitleLayoutSpec *layout
)
{
    SubtitlePlacement effective_manual;

    if (!video_rect || !layout) {
        return false;
    }

    memset(layout, 0, sizeof(*layout));
    layout->video_rect = *video_rect;
    layout->overlay_mix = overlay_mix;
    effective_manual = subtitle_effective_manual_placement(placement, manual_fallback);
    layout->manual_placement = effective_manual;

    if (placement == SUBTITLE_POS_AUTO &&
        cue &&
        cue->position_mode != SUBTITLE_CUE_POSITION_NONE &&
        cue->align >= 1 &&
        cue->align <= 9) {
        int column = subtitle_align_column(cue->align);

        layout->mode = cue->position_mode;
        layout->align = cue->align;
        layout->margin_l = subtitle_scale_coord(cue->margin_l, video_rect->w);
        layout->margin_r = subtitle_scale_coord(cue->margin_r, video_rect->w);
        layout->margin_v = subtitle_scale_coord(cue->margin_v, video_rect->h);
        layout->absolute_x = video_rect->x + subtitle_scale_coord(cue->pos_x, video_rect->w);
        layout->absolute_y = video_rect->y + subtitle_scale_coord(cue->pos_y, video_rect->h);

        if (layout->mode == SUBTITLE_CUE_POSITION_ABSOLUTE) {
            int left_space = layout->absolute_x - video_rect->x;
            int right_space = (video_rect->x + video_rect->w) - layout->absolute_x;

            if (column == 0) {
                layout->wrap_width = right_space - 6;
            } else if (column == 2) {
                layout->wrap_width = left_space - 6;
            } else {
                layout->wrap_width = (2 * (left_space < right_space ? left_space : right_space)) - 6;
            }
        } else if (layout->mode == SUBTITLE_CUE_POSITION_MARGIN) {
            if (column == 0) {
                layout->wrap_width = video_rect->w - layout->margin_l - 6;
            } else if (column == 2) {
                layout->wrap_width = video_rect->w - layout->margin_r - 6;
            } else {
                layout->wrap_width = video_rect->w - layout->margin_l - layout->margin_r - 12;
            }
        }

        if (layout->wrap_width >= 32) {
            return true;
        }

        memset(layout, 0, sizeof(*layout));
        layout->video_rect = *video_rect;
        layout->overlay_mix = overlay_mix;
        layout->manual_placement = effective_manual;
    }

    if (layout->manual_placement == SUBTITLE_POS_BAR_BOTTOM || layout->manual_placement == SUBTITLE_POS_BAR_TOP) {
        layout->wrap_width = SCREEN_W - 12;
    } else {
        layout->wrap_width = video_rect->w - 12;
    }
    return layout->wrap_width > 0;
}

int ui_bar_hidden_offset_for_mix(uint8_t chrome_mix)
{
    return (int) (((uint32_t) UI_BAR_H * (255U - chrome_mix) + 127U) / 255U);
}

int ui_bar_visible_height_for_mix(uint8_t chrome_mix)
{
    return UI_BAR_H - ui_bar_hidden_offset_for_mix(chrome_mix);
}

int subtitle_visible_bottom_limit(const SubtitleLayoutSpec *layout, int bottom_margin)
{
    int bottom;

    if (!layout) {
        return SCREEN_H;
    }

    bottom = layout->video_rect.y + layout->video_rect.h - bottom_margin;
    if (layout->overlay_mix > 0) {
        int ui_top = SCREEN_H - 2 - ui_bar_visible_height_for_mix(layout->overlay_mix);
        if (bottom > ui_top) {
            bottom = ui_top;
        }
    }
    return bottom;
}

int subtitle_visible_max_y(const SubtitleLayoutSpec *layout, int surface_h)
{
    int min_y;
    int max_y;

    if (!layout) {
        return 0;
    }

    min_y = layout->video_rect.y;
    max_y = subtitle_visible_bottom_limit(layout, 0) - surface_h;
    return max_y < min_y ? min_y : max_y;
}

void subtitle_layout_dst_rect(
    const SubtitleLayoutSpec *layout,
    int surface_w,
    int surface_h,
    SDL_Rect *dst
)
{
    int x = 0;
    int y = 0;
    const SDL_Rect *video_rect;

    if (!layout || !dst) {
        return;
    }

    video_rect = &layout->video_rect;
    if (layout->mode == SUBTITLE_CUE_POSITION_ABSOLUTE && layout->align >= 1 && layout->align <= 9) {
        int column = subtitle_align_column(layout->align);
        int row = subtitle_align_row(layout->align);

        if (column == 0) {
            x = layout->absolute_x;
        } else if (column == 2) {
            x = layout->absolute_x - surface_w;
        } else {
            x = layout->absolute_x - (surface_w / 2);
        }

        if (row == 0) {
            y = layout->absolute_y - surface_h;
        } else if (row == 2) {
            y = layout->absolute_y;
        } else {
            y = layout->absolute_y - (surface_h / 2);
        }

        x = clamp_int(x, video_rect->x, video_rect->x + video_rect->w - surface_w);
        y = clamp_int(y, video_rect->y, subtitle_visible_max_y(layout, surface_h));
    } else if (layout->mode == SUBTITLE_CUE_POSITION_MARGIN && layout->align >= 1 && layout->align <= 9) {
        int column = subtitle_align_column(layout->align);
        int row = subtitle_align_row(layout->align);

        if (column == 0) {
            x = video_rect->x + layout->margin_l;
        } else if (column == 2) {
            x = (video_rect->x + video_rect->w) - layout->margin_r - surface_w;
        } else {
            x = video_rect->x + ((video_rect->w - surface_w) / 2) + ((layout->margin_l - layout->margin_r) / 2);
        }

        if (row == 0) {
            y = subtitle_visible_bottom_limit(layout, layout->margin_v) - surface_h;
        } else if (row == 2) {
            y = video_rect->y + layout->margin_v;
        } else {
            y = video_rect->y + ((video_rect->h - surface_h) / 2);
        }

        x = clamp_int(x, video_rect->x, video_rect->x + video_rect->w - surface_w);
        y = clamp_int(y, video_rect->y, subtitle_visible_max_y(layout, surface_h));
    } else {
        int area_x;
        int area_y;
        int area_w;
        int area_top;
        int area_bottom;
        int area_height;

        if (layout->manual_placement == SUBTITLE_POS_BAR_BOTTOM || layout->manual_placement == SUBTITLE_POS_BAR_TOP) {
            area_x = 0;
            area_w = SCREEN_W;
        } else {
            area_x = video_rect->x;
            area_w = video_rect->w;
        }

        switch (layout->manual_placement) {
            case SUBTITLE_POS_BAR_BOTTOM:
                area_top = video_rect->y + video_rect->h + 2;
                area_bottom = SCREEN_H - 2 - ui_bar_visible_height_for_mix(layout->overlay_mix);
                area_height = area_bottom - area_top;
                if (area_height >= surface_h) {
                    area_y = area_top + (area_height - surface_h) / 2;
                } else {
                    area_y = area_bottom - surface_h;
                    if (area_y < 2) {
                        area_y = 2;
                    }
                }
                break;
            case SUBTITLE_POS_VIDEO_TOP:
                area_y = video_rect->y + 4;
                break;
            case SUBTITLE_POS_BAR_TOP:
                area_top = 2;
                area_bottom = video_rect->y - 2;
                area_height = area_bottom - area_top;
                if (area_height >= surface_h) {
                    area_y = area_top + (area_height - surface_h) / 2;
                } else {
                    area_y = 4;
                }
                break;
            case SUBTITLE_POS_VIDEO_BOTTOM:
            default:
                area_y = subtitle_visible_bottom_limit(layout, 8) - surface_h;
                if (area_y < video_rect->y + 4) {
                    area_y = video_rect->y + 4;
                }
                break;
        }

        x = area_x + (area_w - surface_w) / 2;
        y = area_y;
    }

    dst->x = (Sint16) x;
    dst->y = (Sint16) y;
    dst->w = (Uint16) surface_w;
    dst->h = (Uint16) surface_h;
}

static void repair_space_font(nSDL_Font *font)
{
    /* nSDL's Space bitmap has disconnected top-row pixels in these four
     * glyphs. Match the whole original glyph before clearing that row, so
     * corrected/different library versions retain their own artwork. */
    static const struct { unsigned char ch, rows[8]; } fixes[] = {
        {'-', {0x60,0,0,0,0xfc,0,0,0}},
        {'q', {0x60,0,0,0x7e,0x46,0x42,0x7e,0x02}},
        {'r', {0x02,0,0,0x7e,0x46,0x60,0x60,0x60}},
        {'z', {0x7c,0,0,0x7e,0x0c,0x18,0x30,0x7e}}
    };
    if (!font) return;
    for (unsigned i=0;i<sizeof(fixes)/sizeof(fixes[0]);++i) {
        SDL_Surface *glyph=font->chars[fixes[i].ch];
        if (!glyph || glyph->w!=8 || glyph->h!=8 || !(glyph->flags&SDL_SRCCOLORKEY)) continue;
        bool locked=SDL_MUSTLOCK(glyph),matches=true;
        if (locked && SDL_LockSurface(glyph)) continue;
        for (int y=0;y<8 && matches;++y) for (int x=0;x<8;++x)
            if (surface_pixel_has_ink(glyph,x,y)!=!!(fixes[i].rows[y]&(128U>>x))) {
                matches=false;break;
            }
        if (locked) SDL_UnlockSurface(glyph);
        if (matches) {
            SDL_Rect top={0,0,8,1};
            SDL_FillRect(glyph,&top,glyph->format->colorkey);
        }
    }
}
void subtitle_fonts_for_style(const Fonts *fonts, size_t subtitle_font_index, nSDL_Font **white_font, nSDL_Font **outline_font)
{
    int font_id = subtitle_font_id_for_index(subtitle_font_index);
    SubtitleFonts *cache = fonts->subtitles;
    if (cache && !cache->attempted[font_id]) {
        cache->attempted[font_id] = true;
        nSDL_Font *white = nSDL_LoadFont(font_id, 255, 255, 255);
        nSDL_Font *outline = nSDL_LoadFont(font_id, 0, 0, 0);
        if (font_id==NSDL_FONT_SPACE) {
            repair_space_font(white);
            repair_space_font(outline);
        }
        if (white && outline) {
            nSDL_SetFontSpacing(white, 0, 0);
            nSDL_SetFontSpacing(outline, 0, 0);
            cache->white[font_id] = white;
            cache->outline[font_id] = outline;
        } else {
            if (white) nSDL_FreeFont(white);
            if (outline) nSDL_FreeFont(outline);
        }
    }
    *white_font = cache ? cache->white[font_id] : NULL;
    *outline_font = cache ? cache->outline[font_id] : NULL;
    if (!*white_font || !*outline_font) {
        *white_font = fonts->white;
        *outline_font = fonts->outline;
    }
}

void draw_scaled_outlined_text(
    SDL_Surface *screen, nSDL_Font *white_font, nSDL_Font *outline_font,
    int x, int y, const char *text, int scale_num, int scale_den
)
{
    if (!screen || !white_font || !text || scale_num <= 0 || scale_den <= 0) return;
    if (scale_num == scale_den || screen->format->BitsPerPixel != 16) {
        draw_outlined_text(screen, white_font, outline_font, x, y, text);
        return;
    }
    int text_w = nSDL_GetStringWidth(white_font, text);
    int text_h = nSDL_GetStringHeight(white_font, text);
    int glyph_w = subtitle_scaled_extent(text_w, scale_num, scale_den);
    int glyph_h = subtitle_scaled_extent(text_h, scale_num, scale_den);
    int radius = subtitle_text_padding_px(scale_num, scale_den) / 2;
    if (text_w <= 0 || text_h <= 0 || glyph_w <= 0 || glyph_h <= 0 || radius <= 0 ||
        (size_t)glyph_w > SIZE_MAX / (size_t)glyph_h) return;
    SDL_Surface *glyphs = create_rgb565_surface(text_w, text_h);
    uint8_t *mask = malloc((size_t)glyph_w * glyph_h);
    if (!glyphs || !mask) {
        if (glyphs) SDL_FreeSurface(glyphs);
        free(mask);
        draw_outlined_text(screen, white_font, outline_font, x, y, text);
        return;
    }
    Uint16 key = (Uint16)SDL_MapRGB(glyphs->format, 255, 0, 255);
    SDL_FillRect(glyphs, NULL, key);
    nSDL_DrawString(glyphs, white_font, 0, 0, "%s", text);
    bool source_locked = SDL_MUSTLOCK(glyphs), target_locked = SDL_MUSTLOCK(screen);
    if (source_locked && SDL_LockSurface(glyphs) != 0) goto free_glyphs;
    const Uint16 *source = glyphs->pixels;
    int pitch = glyphs->pitch / 2;
    /* Scale only the letters. Pixel-centre sampling gives both ends the same
     * treatment; the border is generated afterwards in destination pixels. */
    for (int gy = 0; gy < glyph_h; ++gy) {
        int sy = ((2 * gy + 1) * text_h) / (2 * glyph_h);
        for (int gx = 0; gx < glyph_w; ++gx) {
            int sx = ((2 * gx + 1) * text_w) / (2 * glyph_w);
            mask[gy * glyph_w + gx] = source[sy * pitch + sx] != key;
        }
    }
    if (source_locked) SDL_UnlockSurface(glyphs);
    if (target_locked && SDL_LockSurface(screen) != 0) goto free_glyphs;
    Uint16 *pixels = screen->pixels;
    Uint16 white = (Uint16)SDL_MapRGB(screen->format, 255, 255, 255);
    Uint16 black = (Uint16)SDL_MapRGB(screen->format, 0, 0, 0);
    SDL_Rect clip = screen->clip_rect;
    for (int oy = 0; oy < glyph_h + 2 * radius; ++oy) {
        int py = y + oy, gy = oy - radius;
        if (py < 0 || py >= screen->h || py < clip.y || py >= clip.y + clip.h) continue;
        for (int ox = 0; ox < glyph_w + 2 * radius; ++ox) {
            int px = x + ox, gx = ox - radius;
            if (px < 0 || px >= screen->w || px < clip.x || px >= clip.x + clip.w) continue;
            bool inside = gx >= 0 && gx < glyph_w && gy >= 0 && gy < glyph_h && mask[gy * glyph_w + gx];
            bool border = inside;
            for (int dy = -radius; !border && dy <= radius; ++dy) {
                int my = gy + dy;
                if (my < 0 || my >= glyph_h) continue;
                for (int dx = -radius; dx <= radius; ++dx) {
                    int mx = gx + dx;
                    if (dx * dx + dy * dy <= radius * radius && mx >= 0 && mx < glyph_w &&
                        mask[my * glyph_w + mx]) { border = true; break; }
                }
            }
            if (border) pixels[py * (screen->pitch / 2) + px] = inside ? white : black;
        }
    }
    if (target_locked) SDL_UnlockSurface(screen);
free_glyphs:
    SDL_FreeSurface(glyphs);
    free(mask);
}

bool ensure_subtitle_surface_cache(
    SubtitleSurfaceCache *cache,
    SDL_Surface *screen,
    const Fonts *fonts,
    const SubtitleCue *cue,
    uint32_t current_ms,
    size_t subtitle_font_index,
    int subtitle_size,
    const SubtitleLayoutSpec *layout
)
{
    char lines[MAX_SUBTITLE_LINES][MAX_SUBTITLE_LINE_LEN];
    int line_count;
    nSDL_Font *white_font;
    nSDL_Font *outline_font;
    int scale_num;
    int scale_den;
    int line_height;
    int max_height;
    int line_capacity;
    int page_start_line;
    int page_line_count;
    int page_height;
    int line_index;
    int max_line_width = 0;
    int draw_inset;
    const char *text = cue ? cue->text : NULL;
    Uint32 key;

    (void) screen;

    if (!cache || !screen || !fonts || !layout || !text || !*text || subtitle_size < 0 || layout->wrap_width <= 0) {
        invalidate_subtitle_surface_cache(cache);
        return false;
    }

    subtitle_size = clamp_int(subtitle_size, 0, 3);
    max_height = subtitle_layout_max_surface_height(layout);
    if (cache->surface &&
        cache->text == text &&
        cache->subtitle_font_index == subtitle_font_index &&
        cache->subtitle_size == subtitle_size &&
        cache->wrap_width == layout->wrap_width &&
        cache->wrapped_line_count > 0 &&
        cache->line_height > 0) {
        line_capacity = subtitle_page_line_capacity(max_height, cache->line_height);
        subtitle_select_line_window(
            cache->wrapped_line_count,
            line_capacity,
            cue,
            current_ms,
            &page_start_line,
            &page_line_count
        );
        if (cache->page_start_line == page_start_line && cache->page_line_count == page_line_count) {
            return true;
        }
    }

    invalidate_subtitle_surface_cache(cache);
    subtitle_fonts_for_style(fonts, subtitle_font_index, &white_font, &outline_font);
    scale_num = subtitle_scale_num(subtitle_size);
    scale_den = subtitle_scale_den(subtitle_size);
    draw_inset = subtitle_text_draw_inset_px(scale_num, scale_den);

    line_count = subtitle_prepare_lines(
        white_font,
        text,
        layout->wrap_width,
        scale_num,
        scale_den,
        lines,
        &line_height,
        &max_line_width
    );
    if (line_count <= 0) {
        return false;
    }

    line_capacity = subtitle_page_line_capacity(max_height, line_height);
    subtitle_select_line_window(
        line_count,
        line_capacity,
        cue,
        current_ms,
        &page_start_line,
        &page_line_count
    );
    page_height = subtitle_lines_height(page_line_count, line_height);
    if (max_line_width <= 0 || page_height <= 0) {
        return false;
    }

    cache->surface = create_rgb565_surface(max_line_width, page_height);
    if (!cache->surface) {
        return false;
    }
    key = SDL_MapRGB(cache->surface->format, 255, 0, 255);
    SDL_FillRect(cache->surface, NULL, key);
    SDL_SetColorKey(cache->surface, SDL_SRCCOLORKEY, key);
    for (line_index = 0; line_index < page_line_count; ++line_index) {
        int source_line = page_start_line + line_index;
        int width = subtitle_rendered_text_width(white_font, lines[source_line], scale_num, scale_den);
        int x = (max_line_width - width) / 2;
        draw_scaled_outlined_text(
            cache->surface,
            white_font,
            outline_font,
            x + draw_inset,
            line_index * (line_height + SUBTITLE_LINE_GAP_PX) + draw_inset,
            lines[source_line],
            scale_num,
            scale_den
        );
    }

    cache->text = text;
    cache->subtitle_font_index = subtitle_font_index;
    cache->subtitle_size = subtitle_size;
    cache->wrap_width = layout->wrap_width;
    cache->wrapped_line_count = line_count;
    cache->line_height = line_height;
    cache->page_start_line = page_start_line;
    cache->page_line_count = page_line_count;
    return true;
}

void draw_subtitle_cached(
    SDL_Surface *screen,
    const Fonts *fonts,
    SubtitleSurfaceCache *cache,
    const SubtitleCue *cue,
    uint32_t current_ms,
    size_t subtitle_font_index,
    int subtitle_size,
    const SubtitleLayoutSpec *layout
)
{
    SDL_Rect dst;
    const char *text = cue ? cue->text : NULL;

    if (!screen || !cache || !layout || !text || !*text || subtitle_size < 0) {
        invalidate_subtitle_surface_cache(cache);
        return;
    }
    if (!ensure_subtitle_surface_cache(
            cache,
            screen,
            fonts,
            cue,
            current_ms,
            subtitle_font_index,
            subtitle_size,
            layout)) {
        return;
    }

    subtitle_layout_dst_rect(layout, cache->surface->w, cache->surface->h, &dst);
    SDL_BlitSurface(cache->surface, NULL, screen, &dst);
}

void draw_subtitle(
    SDL_Surface *screen,
    const Fonts *fonts,
    const char *text,
    size_t subtitle_font_index,
    int subtitle_size,
    const SubtitleLayoutSpec *layout
)
{
    char lines[MAX_SUBTITLE_LINES][MAX_SUBTITLE_LINE_LEN];
    int line_count;
    int line_index;
    nSDL_Font *white_font;
    nSDL_Font *outline_font;
    int scale_num;
    int scale_den;
    int line_height;
    int total_height;
    int base_y = 0;
    int line_max_width = 0;
    int draw_inset;
    SDL_Rect dst;
    if (!text || !*text) {
        return;
    }
    if (!layout || subtitle_size < 0 || layout->wrap_width <= 0) {
        return;
    }
    subtitle_size = clamp_int(subtitle_size, 0, 3);
    subtitle_fonts_for_style(fonts, subtitle_font_index, &white_font, &outline_font);
    scale_num = subtitle_scale_num(subtitle_size);
    scale_den = subtitle_scale_den(subtitle_size);
    draw_inset = subtitle_text_draw_inset_px(scale_num, scale_den);
    line_count = subtitle_prepare_lines(
        white_font,
        text,
        layout->wrap_width,
        scale_num,
        scale_den,
        lines,
        &line_height,
        &line_max_width
    );
    if (line_count <= 0) {
        return;
    }
    total_height = line_count * line_height;
    if (line_count > 1) {
        total_height += (line_count - 1) * SUBTITLE_LINE_GAP_PX;
    }
    if (line_max_width <= 0) {
        return;
    }
    subtitle_layout_dst_rect(layout, line_max_width, total_height, &dst);
    base_y = dst.y;
    for (line_index = 0; line_index < line_count; ++line_index) {
        int width = subtitle_rendered_text_width(white_font, lines[line_index], scale_num, scale_den);
        int x = dst.x + (line_max_width - width) / 2;
        draw_scaled_outlined_text(
            screen,
            white_font,
            outline_font,
            x + draw_inset,
            base_y + (line_index * (line_height + SUBTITLE_LINE_GAP_PX)) + draw_inset,
            lines[line_index],
            scale_num,
            scale_den
        );
    }
}

