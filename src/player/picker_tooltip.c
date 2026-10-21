#include "player_internal.h"

void format_file_size(uint64_t bytes, char *out, size_t size)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    unsigned unit = 0;
    uint64_t divisor = 1;
    while (unit < 4 && bytes / divisor >= 1024) { ++unit; divisor *= 1024; }
    if (!unit) snprintf(out, size, "%lu B", (unsigned long)bytes);
    else {
        uint64_t whole = bytes / divisor;
        unsigned tenth = (unsigned)((bytes % divisor) * 10 / divisor);
        snprintf(out, size, "%lu.%u %s", (unsigned long)whole, tenth, units[unit]);
    }
}

enum {
    TOOLTIP_LINES = 16,
    TOOLTIP_LINE_BYTES = 96,
    TOOLTIP_TEXT_WIDTH = 170,
    TOOLTIP_PAD_X = 7,
    TOOLTIP_PAD_Y = 6,
    TOOLTIP_LINE_STEP = 11
};

typedef struct {
    char lines[TOOLTIP_LINES][TOOLTIP_LINE_BYTES];
    unsigned count, title_lines, limit, width;
} TooltipText;

static bool tooltip_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r';
}

/* Greedy pixel wrapping, including long words/filenames without spaces.
 * Measure glyph advances once per character; no heap or prefix measurements. */
static bool tooltip_append(TooltipText *wrapped, nSDL_Font *font, const char *text)
{
    if (!text)
        return true;
    while (*text) {
        while (tooltip_space(*text) || *text == '\n')
            ++text;
        if (!*text)
            break;
        if (wrapped->count == wrapped->limit)
            return false;
        size_t length = 0, word_end = 0;
        int width = 0;
        while (text[length] && text[length] != '\n' && length + 1 < TOOLTIP_LINE_BYTES) {
            int advance = font_char_advance_width(font, (unsigned char)text[length]);
            if (width + advance > (int)wrapped->width)
                break;
            if (tooltip_space(text[length]))
                word_end = length;
            width += advance;
            ++length;
        }
        if (text[length] && text[length] != '\n' && word_end)
            length = word_end;
        if (!length)
            length = 1;
        char *line = wrapped->lines[wrapped->count++];
        memcpy(line, text, length);
        line[length] = 0;
        text += length;
        while (length && tooltip_space(line[length - 1]))
            line[--length] = 0;
    }
    return true;
}

static void tooltip_truncate(TooltipText *wrapped, nSDL_Font *font)
{
    if (!wrapped->count)
        return;
    char *last = wrapped->lines[wrapped->count - 1];
    size_t length = strlen(last);
    while (length && (length + 4 > TOOLTIP_LINE_BYTES ||
                      nSDL_GetStringWidth(font, last) + nSDL_GetStringWidth(font, "...") >
                          (int)wrapped->width))
        last[--length] = 0;
    memcpy(last + length, "...", 4);
}

static void draw_tooltip(SDL_Surface *screen, const Fonts *fonts, const MovieFile *file,
                         const PointerState *pointer, uint8_t mix, bool resume,bool path)
{
    if (!screen || !fonts || !file || !pointer || !pointer->visible || !mix)
        return;
    TooltipText text = {.limit = path?TOOLTIP_LINES:6,.width=path?230:TOOLTIP_TEXT_WIDTH};
    bool complete = tooltip_append(&text, fonts->white, file->name);
    text.title_lines = text.count;
    bool unavailable=!path && picker_codec_unavailable(file);
    unsigned warning_begin=text.count,warning_end=text.count;
    if(unavailable && complete) {
        complete=tooltip_append(&text,fonts->white,"Codec not included in this build");
        warning_end=text.count;
    }
    if (complete && text.count < TOOLTIP_LINES)
        complete = tooltip_append(&text, fonts->white, file->detail);
    char time_line[64] = {0}, duration[24], position[24];
    if (resume) {
        if (file->resume_time_known) {
            format_clock(file->resume_ms, position, sizeof(position));
            format_clock(file->duration_ms, duration, sizeof(duration));
            snprintf(time_line, sizeof(time_line), "Resume %s / %s", position, duration);
        } else
            strcpy(time_line, "Resume saved");
    } else if (file->duration_ms)
        format_clock(file->duration_ms, time_line, sizeof(time_line));
    if (complete && text.count < TOOLTIP_LINES)
        complete = tooltip_append(&text, fonts->white, time_line);
    if (!complete)
        tooltip_truncate(&text, fonts->white);
    if (file->kind != PICKER_FOLDER && file->kind != PICKER_SEPARATOR) {
        char size[32]; format_file_size(file->file_bytes, size, sizeof(size));
        text.limit = 7;
        tooltip_append(&text, fonts->white, size);
    }
    if (!text.count)
        return;
    int width = 72;
    for (unsigned index = 0; index < text.count; ++index) {
        int line_width = nSDL_GetStringWidth(fonts->white, text.lines[index]) + 2 * TOOLTIP_PAD_X;
        if (line_width > width)
            width = line_width;
    }
    int height = 2 * TOOLTIP_PAD_Y + NSP_FONT_HEIGHT + (text.count - 1) * TOOLTIP_LINE_STEP;
    int x = pointer->x + 12, y = pointer->y + 10;
    if (x + width > SCREEN_W - 4)
        x = pointer->x - width - 8;
    if (y + height > SCREEN_H - 4)
        y = pointer->y - height - 8;
    x = clamp_int(x, 4, SCREEN_W - width - 4);
    y = clamp_int(y + ((255 - mix) * 4 + 127) / 255, 4, SCREEN_H - height - 4);
    SDL_Rect panel = {x, y, width, height}, old_clip;
    SDL_Surface *target;
    int dx, dy;
    SDL_Surface *layer = begin_faded_region_draw(screen, &panel, mix, &target, &old_clip, &dx, &dy);
    SDL_Rect box = offset_sdl_rect(&panel, dx, dy);
    draw_soft_glass_panel_body_from_y(target, &box, box.y, ui_theme()->tooltip_base);
    draw_soft_glass_panel_rim(target, &box, ui_theme()->tooltip_base, 255);
    for (unsigned index = 0; index < text.count; ++index)
        draw_ui_label_tint(target, fonts, box.x + TOOLTIP_PAD_X,
                           box.y + TOOLTIP_PAD_Y + index * TOOLTIP_LINE_STEP, text.lines[index],
                           unavailable && (index<text.title_lines ||
                            (index>=warning_begin && index<warning_end))?UI_COLOR_UNSUPPORTED:
                            picker_text_color(index >= text.title_lines), 255);
    end_faded_region_draw(screen, layer, &panel, &old_clip, mix);
}

void draw_movie_hover_tooltip(SDL_Surface *screen, const Fonts *fonts, const MovieFile *file,
                              const PointerState *pointer, uint8_t tooltip_mix)
{
    draw_tooltip(screen, fonts, file, pointer, tooltip_mix, false,false);
}

void draw_resume_hover_tooltip(SDL_Surface *screen, const Fonts *fonts, const MovieFile *file,
                               const PointerState *pointer, uint8_t tooltip_mix)
{
    draw_tooltip(screen, fonts, file, pointer, tooltip_mix, true,false);
}

void draw_path_hover_tooltip(SDL_Surface *screen,const Fonts *fonts,char *path,
                              const PointerState *pointer,uint8_t mix)
{
    MovieFile file={.kind=PICKER_FOLDER,.name=path,.detail=""};
    draw_tooltip(screen,fonts,&file,pointer,mix,false,true);
}
