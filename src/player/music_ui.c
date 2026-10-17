#include "music_ui.h"
#include "cover_image.h"
#include "media_files.h"
#include "storage/usb/usb_storage.h"
#include "usb_browser.h"
#include <errno.h>

static const char *const views[] = {"Album cover", "Flow", "Vortex", "Prism", "Pulse"};
static unsigned remembered_view;
static PlaybackMode remembered_mode = PLAYBACK_MODE_AUTO_NEXT;
static const char *codec_name(unsigned codec)
{
    static const char *const names[] = {"PCM", "IMA-ADPCM", "MP3", "FLAC", "Vorbis", "AAC", "Opus"};
    return codec < sizeof(names) / sizeof(*names) ? names[codec] : "Audio";
}
typedef struct
{
    MovieReadFile *file;
    LoadingProgress *loading;
} TagInput;
static size_t tag_read(void *context, void *out, size_t bytes)
{
    TagInput *in = context;
    loading_progress_tick(in->loading, false);
    if (isKeyPressed(KEY_NSPIRE_ESC))
        return 0;
    return movie_file_read(out, 1, bytes, in->file);
}
static bool tag_seek(void *context, uint64_t at)
{
    return movie_file_seek(((TagInput *)context)->file, at) == 0;
}
static void folder_cover(MusicUi *ui, const char *path, LoadingProgress *loading)
{
    uint32_t start = monotonic_clock_now_ms();
    bool cached = movie_picker_cover_path(path, ui->cover_path, sizeof(ui->cover_path));
    if (!cached)
    {
        char directory[MAX_PATH_LEN], found[256] = {0};
        unsigned rank = 0;
        snprintf(directory, sizeof(directory), "%s", path);
        strip_filename(directory);
        if (usb_path(path))
        {
            int error = 0;
            UsbDirectory *dir = usb_directory_open(directory, &error);
            unsigned entries = 0;
            while (dir && entries < 512 && monotonic_clock_now_ms() - start < 2000)
            {
                const char *entry = NULL;
                bool folder = false;
                int result = usb_directory_next(dir, &entry, &folder, &error);
                loading_progress_tick(loading, false);
                if (result < 0 || result == 2 || isKeyPressed(KEY_NSPIRE_ESC))
                    break;
                if (result == 1)
                {
                    ++entries;
                    unsigned r = folder ? 0 : media_cover_rank(entry);
                    if (r > rank)
                    {
                        snprintf(found, sizeof(found), "%s", entry);
                        rank = r;
                    }
                }
                else
                    player_delay_ms(1);
            }
            if (dir)
                usb_directory_close(dir);
        }
        else
        {
            DIR *dir = opendir(directory);
            struct dirent *entry;
            unsigned entries = 0;
            while (dir && entries++ < 512 && (entry = readdir(dir)))
            {
                unsigned r = media_cover_rank(entry->d_name);
                if (r > rank)
                {
                    snprintf(found, sizeof(found), "%s", entry->d_name);
                    rank = r;
                }
            }
            if (dir)
                closedir(dir);
        }
        if (*found)
            snprintf(ui->cover_path, sizeof(ui->cover_path), "%s/%s", directory, found);
    }
    if (!*ui->cover_path || isKeyPressed(KEY_NSPIRE_ESC))
        return;
    MovieReadFile *file = movie_file_open(ui->cover_path);
    if (!file)
    {
        snprintf(ui->cover_status, sizeof(ui->cover_status), "folder cover open failed errno=%d",
                 errno);
        ui->cover_error = true;
        return;
    }
    uint64_t bytes = movie_file_size(file);
    if (bytes && bytes <= AUDIO_ART_LIMIT)
    {
        loading_progress_tick(loading, false);
        void *data = malloc((size_t)bytes);
        if (data && movie_file_read(data, 1, (size_t)bytes, file) == bytes &&
            audio_metadata_art(&ui->tags, data, (size_t)bytes, 3))
            snprintf(ui->cover_status, sizeof(ui->cover_status),
                     cached ? "folder cover cached" : "folder cover discovered");
        else
        {
            snprintf(ui->cover_status, sizeof(ui->cover_status),
                     "folder cover read failed errno=%d", errno);
            ui->cover_error = true;
        }
        free(data);
    }
    else
        snprintf(ui->cover_status, sizeof(ui->cover_status), "folder cover outside size limit");
    movie_file_close(file);
    ui->cover_read_ms = monotonic_clock_now_ms() - start;
}
void music_ui_open(MusicUi *ui, const char *path, LoadingProgress *loading)
{
    memset(ui, 0, sizeof(*ui));
    strcpy(ui->cover_status, "no cover");
    ui->view = NDMEDIA_VISUALIZATIONS?remembered_view:0;
    ui->mode = remembered_mode;
    ui_transition_init(&ui->chrome, true);
    ui->chrome_until = monotonic_clock_now_ms() + 2400;
    MovieReadFile *file = movie_file_open(path);
    if (file)
    {
        TagInput context = {file, loading};
        AudioInput input = {&context, tag_read, tag_seek, NULL, movie_file_size(file)};
        audio_metadata_read(&input, &ui->tags);
        movie_file_close(file);
    }
    if (ui->tags.art)
        strcpy(ui->cover_status, "embedded cover");
    if (NDMEDIA_ARTWORK && !ui->tags.art && !isKeyPressed(KEY_NSPIRE_ESC))
        folder_cover(ui, path, loading);
    if (NDMEDIA_ARTWORK && ui->tags.art)
    {
        loading_progress_tick(loading, true);
        uint32_t started = monotonic_clock_now_ms();
        ui->cover = cover_image_decode(ui->tags.art, ui->tags.art_bytes, MUSIC_COVER_SIDE);
        ui->cover_decode_ms = monotonic_clock_now_ms() - started;
        if (!ui->cover)
        {
            strcpy(ui->cover_status, "cover decode failed");
            ui->cover_error = true;
        }
        free(ui->tags.art);
        ui->tags.art = NULL;
        ui->tags.art_bytes = 0;
    }
    char fallback[MAX_PATH_LEN];
    media_audio_title(path, fallback, sizeof(fallback));
    display_text_ascii(*ui->tags.title ? ui->tags.title : fallback, ui->title, sizeof(ui->title),
                       !*ui->tags.title && usb_path(path));
    audio_metadata_display_text(ui->tags.artist, ui->artist, sizeof(ui->artist));
    audio_metadata_display_text(ui->tags.album, ui->album, sizeof(ui->album));
    /* Resolve once before USB audio starts; hover never scans the filesystem. */
    find_previous_movie_path(path, ui->previous, sizeof(ui->previous));
    find_next_movie_path(path, ui->next, sizeof(ui->next));
    if (NDMEDIA_VISUALIZATIONS && ui->view)
        ui->visual = calloc(1, sizeof(*ui->visual));
    if (ui->view && !ui->visual)
        ui->view = 0;
}
void music_ui_close(MusicUi *ui)
{
    remembered_view = ui->view;
    remembered_mode = ui->mode;
    free(ui->cover);
    free(ui->visual);
    audio_metadata_close(&ui->tags);
}
void music_ui_cycle_view(MusicUi *ui)
{
    if(!NDMEDIA_VISUALIZATIONS)return;
    ui->view = (ui->view + 1) % MUSIC_VIEW_COUNT;
    if (ui->view && !ui->visual)
        ui->visual = calloc(1, sizeof(*ui->visual));
    if (!ui->visual)
        ui->view = 0;
    if (!ui->view)
    {
        free(ui->visual);
        ui->visual = NULL;
    }
    ui->visual_at = 0;
    ui->chrome_until = monotonic_clock_now_ms() + 2400;
    ui_transition_init(&ui->chrome, true);
}
void music_ui_status(MusicUi *ui, const char *text, uint32_t now)
{
    snprintf(ui->status, sizeof(ui->status), "%s", text);
    ui->status_at = now;
    ui->status_until = now + STATUS_OVERLAY_MS;
}
void music_ui_brightness(MusicUi *ui, bool up, bool down, bool up_edge, bool down_edge,
                         uint32_t now)
{
    int direction = up == down ? 0 : up ? -1 : 1;
    bool step = direction < 0 ? up_edge : down_edge;
    if (step)
    {
        ui->brightness_direction = direction;
        ui->brightness_repeat_at = now + TAB_HOLD_FRAME_REPEAT_DELAY_MS;
    }
    else if (!direction || direction != ui->brightness_direction)
        ui->brightness_direction = 0;
    else if ((int32_t)(now - ui->brightness_repeat_at) >= 0)
    {
        step = true;
        ui->brightness_repeat_at = now + TAB_HOLD_FRAME_REPEAT_FALLBACK_INTERVAL_MS;
    }
    if (step && direction)
    {
        uint32_t from = current_lcd_brightness();
        uint32_t logical = display_power_logical_brightness(&g_display_power_state);
        display_power_cancel_brightness_restore(&g_display_power_state);
        uint32_t to = lcd_brightness_step_target_from(
            ui->brightness_active ? ui->brightness_to : logical, direction);
        ui->brightness_from = from;
        ui->brightness_to = to;
        ui->brightness_at = now;
        ui->brightness_active = true;
    }
    if (ui->brightness_active)
    {
        uint8_t mix = ui_ease_smoothstep(now - ui->brightness_at, LCD_BRIGHTNESS_FADE_MS);
        int raw = (int)ui->brightness_from +
                  ((int)ui->brightness_to - (int)ui->brightness_from) * mix / 255;
        set_lcd_brightness(raw);
        char status[48];
        snprintf(status, sizeof(status), "BRIGHT %u%%", lcd_brightness_percent(raw));
        music_ui_status(ui, status, now);
        if (mix == 255)
            ui->brightness_active = false;
    }
}
void music_ui_update(MusicUi *ui, AudioPlayback *audio, const AudioPlaybackInfo *info,
                     const MusicControls *c, const PointerState *pointer, uint32_t now)
{
    bool activity=pointer->moved || pointer->press_edge || pointer->release_edge ||
                  pointer->down || c->capture;
    bool show=music_hud_should_show(ui->view,c->paused,c->info_open || c->art_open,
        c->scrubbing || c->pending_seek,activity,now,&ui->chrome_until);
    ui_transition_update(&ui->chrome, show, now, 180);
    int hot = pointer->visible ? music_control_at(pointer->x, pointer->y, info->duration_known) : 0;
    if (ui->view && ui->chrome.current_mix < 128)
        hot = 0;
    if (hot == MUSIC_ART && (!ui->cover || ui->view))
        hot = 0;
    if (c->art_open)
        hot = 0;
    if (c->info_open && hot != MUSIC_BACK && hot != MUSIC_INFO && hot != MUSIC_SEEK)
        hot = 0;
    if (hot != ui->hovered)
    {
        ui->hovered = hot;
        ui->hover_at = now;
    }
    if(hot==MUSIC_MODE || hot==MUSIC_VISUAL)ui->tooltip_target=hot;
    ui->animating = ui->brightness_active || ui->chrome.current_mix != (show ? 255 : 0);
    for (unsigned i = 1; i < MUSIC_CONTROL_COUNT; ++i)
    {
        ui_transition_update(&ui->hover[i], hot == (int)i, now,
            i==MUSIC_MODE || i==MUSIC_VISUAL?200U:UI_HOVER_ANIM_MS);
        ui_transition_update_press(&ui->press[i], c->capture == (int)i, now);
        if (ui->hover[i].current_mix != (ui->hover[i].target_active ? 255 : 0) ||
            ui->press[i].current_mix != (ui->press[i].target_active ? 255 : 0))
            ui->animating = true;
    }
    ui_transition_update(&ui->info, c->info_open, now, 160);
    if (ui->info.current_mix != (c->info_open ? 255 : 0))
        ui->animating = true;
    ui_transition_update(&ui->art, c->art_open, now, 180);
    if (ui->art.current_mix != (c->art_open ? 255 : 0))
        ui->animating = true;
    bool queue =
        (hot == MUSIC_PREVIOUS || hot == MUSIC_NEXT) && !c->capture && now - ui->hover_at >= 400;
    if (queue)
        ui->queue_target = hot;
    ui_transition_update(&ui->queue, queue, now, UI_HOVER_ANIM_MS);
    if (ui->queue.current_mix != (queue ? 255 : 0))
        ui->animating = true;
    if (c->badge_delta_ms && (int32_t)(now - c->badge_hide_at) < (int)SEEK_BADGE_EXIT_ANIM_MS)
        ui->animating = true;
    if (ui->status_until && (int32_t)(now - ui->status_until) < (int)STATUS_BADGE_EXIT_ANIM_MS)
        ui->animating = true;
    if (NDMEDIA_VISUALIZATIONS && ui->view && ui->visual && now - ui->visual_at >= 40)
    {
        uint32_t started = monotonic_clock_now_ms();
        size_t frames = audio_playback_snapshot(audio, ui->visual->pcm, MUSIC_VIS_SAMPLES);
        music_visual_analyze(ui->visual, frames);
        music_visual_frame(ui->visual, ui->view, now);
        uint32_t spent = monotonic_clock_now_ms() - started;
        ++ui->visual_calls;
        ui->visual_ms += spent;
        if (spent > ui->visual_max_ms)
            ui->visual_max_ms = spent;
        ui->visual_at = now;
    }
}
static void box(SDL_Surface *s, int x, int y, int w, int h, Uint16 color)
{
    SDL_Rect r = {x, y, w, h};
    fill_rect_rgb565(s, &r, color);
}
static void line(SDL_Surface *s, int x0, int y0, int x1, int y1, Uint16 color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1,
        err = dx + dy;
    for (;;)
    {
        box(s, x0, y0, 1, 1, color);
        if (x0 == x1 && y0 == y1)
            break;
        int twice = err * 2;
        if (twice >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (twice <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}
static void music_label(SDL_Surface *s, const Fonts *f, int x, int y, const char *text, int width,
                        bool secondary, uint8_t mix)
{
    char fitted[256];
    copy_fitted_text(f->white, text, fitted, sizeof(fitted), width);
    draw_ui_label_tint(s, f, x, y, fitted, secondary ? picker_text_color(true) : UI_COLOR_WHITE,
                       mix);
}
static void music_corner_pixel(SDL_Surface *s,int x,int y,Uint16 body,Uint16 edge,
                               unsigned outer,unsigned inner)
{
    if(!outer)return;
    Uint16 *pixel=(Uint16 *)((uint8_t *)s->pixels+y*s->pitch)+x;
    unsigned background=255-outer,border=outer-inner;
    unsigned red=((*pixel>>11)*background+(edge>>11)*border+(body>>11)*inner+127)/255;
    unsigned green=(((*pixel>>5)&63)*background+((edge>>5)&63)*border+((body>>5)&63)*inner+127)/255;
    unsigned blue=((*pixel&31)*background+(edge&31)*border+(body&31)*inner+127)/255;
    *pixel=(Uint16)((red<<11)|(green<<5)|blue);
}
static void music_button(SDL_Surface *s,const Fonts *f,const SDL_Rect *r,const char *text,
                         uint8_t hover,uint8_t press)
{
    /* A circular 5 px radius, sampled on a 4x4 subpixel grid. The transposed
     * symmetry is intentional: horizontal and vertical curves are identical.
     * The inner 4 px arc makes a consistent one-pixel border. */
    static const uint8_t outer[5][5]={{0,0,79,191,255},{0,159,255,255,255},
        {79,255,255,255,255},{191,255,255,255,255},{255,255,255,255,255}};
    static const uint8_t inner[4][4]={{0,47,175,255},{47,239,255,255},
        {175,255,255,255},{255,255,255,255}};
    Uint16 base=pressed_control_base(rgb565_lerp(UI_COLOR_GUNMETAL,ui_theme()->row_selected,hover,255),press);
    Uint16 top=blend_rgb565(base,UI_COLOR_WHITE,38),bottom=blend_rgb565(base,UI_COLOR_BLACK,92);
    Uint16 edge=control_outline_color(base,max_u8(hover,press));
    int half=r->h/2;
    for(int row=0;row<r->h;++row) {
        int distance=row<r->h-1-row?row:r->h-1-row;
        Uint16 c=rgb565_lerp(top,bottom,row,r->h-1);
        /* Let the reflection taper continuously instead of ending in a hard
         * stripe one third of the way down the larger play/pause button. */
        if(row<half)c=blend_rgb565(c,UI_COLOR_WHITE,36*(half-row)*(half-row)/(half*half));
        int inset=distance<5?5:0;
        SDL_Rect span={r->x+inset,r->y+row,r->w-2*inset,1};
        fill_rect_rgb565(s,&span,distance?c:edge);
        if(distance<5) {
            for(int x=0;x<5;++x) {
                unsigned inside=x && distance?inner[distance-1][x-1]:0;
                music_corner_pixel(s,r->x+x,r->y+row,c,edge,outer[distance][x],inside);
                music_corner_pixel(s,r->x+r->w-1-x,r->y+row,c,edge,outer[distance][x],inside);
            }
        } else {
            SDL_Rect pixel={span.x,span.y,1,1};fill_rect_rgb565(s,&pixel,edge);
            pixel.x=span.x+span.w-1;fill_rect_rgb565(s,&pixel,edge);
        }
    }
    draw_ui_label(s,f,r->x+(r->w-nSDL_GetStringWidth(f->white,text))/2+pressed_control_offset_x(press),
        r->y+(r->h-NSP_FONT_HEIGHT)/2-1+pressed_control_offset_y(press),text);
}
static void button(SDL_Surface *s, const Fonts *f, MusicUi *ui, const MusicControls *c, SDL_Rect r,
                   int id, const char *text)
{
    uint8_t hover = ui->hover[id].current_mix, press = ui->press[id].current_mix;
    music_button(s, f, &r, text, hover, press);
    if (id == MUSIC_MODE || id == MUSIC_VISUAL)
    {
        int icon_width=id==MUSIC_MODE && ui->mode==PLAYBACK_MODE_ONCE?11:18;
        int x = r.x + (r.w - icon_width) / 2 + pressed_control_offset_x(press),
            y = r.y + 6 + pressed_control_offset_y(press);
        Uint16 color = UI_COLOR_WHITE;
        if (id == MUSIC_MODE)
        {
            if (ui->mode == PLAYBACK_MODE_REPEAT)
            {
                line(s, x + 3, y + 2, x + 16, y + 2, color);
                line(s, x + 16, y + 2, x + 16, y + 10, color);
                line(s, x + 16, y + 10, x + 2, y + 10, color);
                line(s, x + 2, y + 10, x + 2, y + 4, color);
                line(s, x + 3, y + 2, x + 6, y - 1, color);
                line(s, x + 3, y + 2, x + 6, y + 5, color);
                line(s, x + 14, y + 7, x + 17, y + 10, color);
                line(s, x + 14, y + 13, x + 17, y + 10, color);
                music_label(s, f, x + 7, y + 3, "1", 8, false, 255);
            }
            else
            {
                for (int i = 0; i < 5; ++i)
                    box(s, x + i, y + 3 + i, 1, 9 - i * 2, color);
                if (ui->mode == PLAYBACK_MODE_ONCE)
                    box(s, x + 9, y + 3, 2, 9, color);
                else
                    for (int i = 0; i < 3; ++i)
                        box(s, x + 10, y + 2 + i * 5, 8, 1, color);
            }
        }
        else if (!ui->view)
        {
            box(s, x, y, 18, 1, color);
            box(s, x, y + 13, 18, 1, color);
            box(s, x, y, 1, 14, color);
            box(s, x + 17, y, 1, 14, color);
            box(s, x + 12, y + 3, 2, 2, color);
            line(s, x + 2, y + 11, x + 7, y + 5, color);
            line(s, x + 7, y + 5, x + 12, y + 11, color);
            line(s, x + 12, y + 11, x + 15, y + 8, color);
        }
        else if (ui->view == MUSIC_VIEW_FLOW)
        {
            for (int i = 0; i < 5; ++i)
                box(s, x + i * 4, y + 12 - (i % 3 + 1) * 4, 2, (i % 3 + 1) * 4, color);
        }
        else
        {
            int py = y + 7;
            for (int i = 0; i < 18; ++i)
            {
                int yy = y + 7 + (i % 8 < 4 ? i % 8 : 7 - i % 8) * 2 - 3;
                if (i)
                    line(s, x + i - 1, py, x + i, yy, color);
                py = yy;
            }
        }
        return;
    }
    if (id != MUSIC_TOGGLE && id != MUSIC_NEXT && id != MUSIC_PREVIOUS)
        return;
    int dx=pressed_control_offset_x(press),dy=pressed_control_offset_y(press);
    if (id == MUSIC_TOGGLE && !c->paused)
    {
        int x=r.x+(r.w-12)/2+dx,y=r.y+(r.h-14)/2+dy;
        box(s,x,y,4,14,UI_COLOR_WHITE);
        box(s,x+8,y,4,14,UI_COLOR_WHITE);
    }
    else
    {
        bool left = id == MUSIC_PREVIOUS;
        bool skip=id==MUSIC_PREVIOUS || id==MUSIC_NEXT;
        int width=skip?12:8;
        int x=r.x+(r.w-width)/2+dx+(skip?0:1),y=r.y+(r.h-16)/2+dy;
        for (int i = 0; i < 8; ++i)
            box(s,x+(left?11-i:i),y+i,1,16-2*i,UI_COLOR_WHITE);
        if(skip)box(s,x+(left?0:10),y+1,2,14,UI_COLOR_WHITE);
    }
}
static void cover_blit(SDL_Surface *screen, const uint16_t *cover, int x, int y, unsigned side)
{
    if(side==MUSIC_COVER_SIDE) {
        for(unsigned row=0;row<side;++row)
            memcpy((uint8_t *)screen->pixels+(y+row)*screen->pitch+x*2,cover+row*side,side*2);
        return;
    }
    unsigned column[240];
    for (unsigned i = 0; i < side; ++i)
        column[i] = (unsigned)clamp_int((int)((2*i+1)*MUSIC_COVER_SIDE*128/side)-128,0,(MUSIC_COVER_SIDE-1)*256);
    for (unsigned row = 0; row < side; ++row)
    {
        unsigned yy=(unsigned)clamp_int((int)((2*row+1)*MUSIC_COVER_SIDE*128/side)-128,0,(MUSIC_COVER_SIDE-1)*256);
        const uint16_t *source = cover + (yy>>8) * MUSIC_COVER_SIDE;
        const uint16_t *below = source + ((yy>>8)+1<MUSIC_COVER_SIDE?MUSIC_COVER_SIDE:0);
        uint16_t *target = (uint16_t *)((uint8_t *)screen->pixels + (y + row) * screen->pitch) + x;
        for (unsigned i = 0; i < side; ++i)
                target[i] = music_pixel_lerp(music_pixel_lerp(source[column[i]>>8],
                    source[(column[i]>>8)+((column[i]>>8)+1<MUSIC_COVER_SIDE)],(column[i]>>3)&31),
                    music_pixel_lerp(below[column[i]>>8],below[(column[i]>>8)+((column[i]>>8)+1<MUSIC_COVER_SIDE)],
                        (column[i]>>3)&31),(yy>>3)&31);
    }
}
static void art(SDL_Surface *s, const Fonts *f, MusicUi *ui)
{
    uint8_t press = ui->press[MUSIC_ART].current_mix;
    int inset = pressed_control_offset_x(press);
    SDL_Rect border = {14 + inset, 40 + inset, 92 - 2 * inset, 92 - 2 * inset};
    if (ui->cover)
        draw_prompt_button(s, f, &border, "", ui->hover[MUSIC_ART].current_mix, press);
    if (ui->cover)
        cover_blit(s, ui->cover, 16 + inset, 42 + inset, 88 - 2 * inset);
    else
    {
        SDL_Rect flat = {15, 41, 90, 90};
        fill_rect_rgb565(s, &flat, UI_COLOR_BG_BOTTOM);
        Uint16 c = picker_text_color(true);
        box(s, 52, 63, 2, 28, c);
        box(s, 54, 63, 17, 3, c);
        box(s, 69, 63, 2, 23, c);
        box(s, 45, 87, 9, 5, c);
        box(s, 62, 82, 9, 5, c);
        music_label(s, f, 34, 111, "No cover", 62, true, 255);
    }
}
static void expanded_art(SDL_Surface *s, const Fonts *f, MusicUi *ui)
{
    unsigned mix = ui->art.current_mix;
    if (!mix || !ui->cover)
        return;
    SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
    dim_rect_rgb565(s, &full, 160 * mix / 255);
    unsigned side = 88 + ((MUSIC_COVER_SIDE - 88) * mix + 127) / 255;
    int x=16+(36*(int)mix+127)/255,y=12+(30*(255-(int)mix)+127)/255;
    SDL_Rect frame = {x - 2, y - 2, side + 4, side + 4};
    draw_soft_glass_panel(s, &frame, UI_COLOR_GUNMETAL, false);
    cover_blit(s, ui->cover, x, y, side);
    if (mix > 220)
        music_label(s, f, 55, 232, "Click / Esc to close", 240, false, (uint8_t)mix);
}
static void visualization(SDL_Surface *screen, const MusicUi *ui)
{
    if (!ui->visual)
    {
        SDL_Rect full = {0, 0, SCREEN_W, SCREEN_H};
        fill_rect_rgb565(screen, &full, UI_COLOR_BLACK);
        return;
    }
    const uint16_t *pixels=music_visual_pixels(ui->visual);
    for(unsigned y=0;y<MUSIC_VIS_HEIGHT;++y){
        uint16_t *a=(uint16_t *)((uint8_t *)screen->pixels+2*y*screen->pitch);
        uint16_t *b=(uint16_t *)((uint8_t *)screen->pixels+(2*y+1)*screen->pitch);
        const uint16_t *row=pixels+y*MUSIC_VIS_WIDTH;
        const uint16_t *below=pixels+(y+1<MUSIC_VIS_HEIGHT?y+1:y)*MUSIC_VIS_WIDTH;
        for(unsigned x=0;x<MUSIC_VIS_WIDTH;++x){
            unsigned next=x+1<MUSIC_VIS_WIDTH?x+1:x;
            uint16_t p=row[x],q=row[next],r=below[x],t=below[next];
            uint16_t pq=(p&q)+(((p^q)&0xf7deU)>>1),rt=(r&t)+(((r^t)&0xf7deU)>>1);
            a[x*2]=p;a[x*2+1]=pq;
            b[x*2]=(p&r)+(((p^r)&0xf7deU)>>1);
            b[x*2+1]=(pq&rt)+(((pq^rt)&0xf7deU)>>1);
        }
    }
}
static void info_panel(SDL_Surface *s, const Fonts *f, const char *path,
                       const AudioPlaybackInfo *info, MusicUi *ui)
{
    uint8_t mix = ui->info.current_mix;
    if (!mix)
        return;
    SDL_Rect panel = {12, 36, 296, 151};
    SDL_Surface *target;
    SDL_Rect clip;
    int dx, dy;
    SDL_Surface *layer = begin_faded_region_draw(s, &panel, mix, &target, &clip, &dx, &dy);
    SDL_Rect r = offset_sdl_rect(&panel, dx, dy);
    fill_rect_rgb565(target, &r, ui_theme()->tooltip_base);
    draw_soft_glass_panel(target, &r, ui_theme()->tooltip_base, false);
    char text[256], size[32], duration[24];
    int y = r.y + 9;
    music_label(target, f, r.x + 9, y, ui->title, 278, false, 255);
    y += 16;
    if (*ui->artist)
    {
        music_label(target, f, r.x + 9, y, ui->artist, 278, true, 255);
        y += 12;
    }
    if (*ui->album)
    {
        music_label(target, f, r.x + 9, y, ui->album, 278, true, 255);
        y += 12;
    }
    snprintf(text, sizeof(text), "%s / %lu Hz / %s", codec_name(info->codec),
             (unsigned long)info->rate, info->channels == 1 ? "Mono" : "Stereo");
    music_label(target, f, r.x + 9, y, text, 278, false, 255);
    y += 14;
    format_file_size(ui->tags.file_bytes, size, sizeof(size));
    format_clock(info->rate ? (uint32_t)((uint64_t)info->frames * 1000 / info->rate) : 0, duration,
                 sizeof(duration));
    snprintf(text, sizeof(text), "%s   %s", size,
             info->duration_known ? duration : "Duration unknown");
    music_label(target, f, r.x + 9, y, text, 278, true, 255);
    y += 12;
    if (info->duration_known && info->frames)
    {
        uint64_t kbps = ui->tags.file_bytes * 8 * info->rate / info->frames / 1000;
        snprintf(text, sizeof(text), "File average ~%lu kb/s; output PCM 16-bit",
                 (unsigned long)kbps);
        music_label(target, f, r.x + 9, y, text, 278, true, 255);
        y += 14;
    }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    display_text_ascii(name, text, sizeof(text), usb_path(path));
    music_label(target, f, r.x + 9, y, text, 278, false, 255);
    y += 12;
    music_label(target, f, r.x + 9, y, usb_path(path) ? "Source: USB drive" : "Source: calculator",
                278, true, 255);
    if (y + 24 < r.y + r.h - 14)
    {
        display_text_ascii(path, text, sizeof(text), usb_path(path));
        music_label(target, f, r.x + 9, y + 12, text, 278, true, 255);
    }
    music_label(target, f, r.x + 9, r.y + r.h - 14, "I / Esc close   Playback continues", 278, true,
                255);
    end_faded_region_draw(s, layer, &panel, &clip, mix);
}
static void seek_knob(SDL_Surface *screen, const Fonts *fonts, int center, uint8_t hover,
                      uint8_t press)
{
    SDL_Rect knob = {center - 4, 198, 9, 13};
    draw_prompt_button(screen, fonts, &knob, "", hover, press);
    draw_soft_glass_panel_rim(screen, &knob, UI_COLOR_GUNMETAL, 255);
}
void music_ui_render(SDL_Surface *s, const Fonts *f, const char *path, AudioPlayback *audio,
                     const AudioPlaybackInfo *info, const MusicControls *c,
                     const PointerState *pointer, MusicUi *ui, uint32_t now)
{
    SDL_Rect full = {0, 0, 320, 240};
    bool status_live=ui->status_until &&
        (int32_t)(now-(ui->status_until+STATUS_BADGE_EXIT_ANIM_MS))<0;
    if (NDMEDIA_VISUALIZATIONS && ui->view)
        visualization(s, ui);
    else
        fill_rect_rgb565(s, &full, UI_COLOR_BG_BOTTOM);
    SDL_Surface *screen = s, *layer = NULL;
    SDL_Rect clip;
    int dx, dy;
    uint8_t chrome = ui->view ? ui->chrome.current_mix : 255;
    if (chrome)
    {
        layer = begin_faded_region_draw(screen, &full, chrome, &s, &clip, &dx, &dy);
        if (ui->view)
        {
            SDL_Rect top = {0, 0, 320, 36}, bottom = {0, 147, 320, 93};
            dim_rect_rgb565(s, &top, 188);
            dim_rect_rgb565(s, &bottom, 188);
        }

        button(s, f, ui, c, (SDL_Rect){8, 8, 44, 22}, MUSIC_BACK, "Back");
        button(s, f, ui, c, (SDL_Rect){268, 8, 44, 22}, MUSIC_INFO, "Info");
        if (ui->view)
            music_label(s, f, 61, 16, ui->title, 199, false, 255);
        else
            music_label(s, f, 140, 16, "Music", 100, false, 255);
        char text[256];
        if (!ui->view)
        {
            art(s, f, ui);
            music_label(s, f, 118, 48, ui->title, 184, false, 255);
            music_label(s, f, 118, 66, *ui->artist ? ui->artist : "Unknown artist", 184, true, 255);
            music_label(s, f, 118, 84, *ui->album ? ui->album : "", 184, true, 255);
            snprintf(text, sizeof(text), "%s  %lu Hz", codec_name(info->codec),
                     (unsigned long)info->rate);
            music_label(s, f, 118, 110, text, 184, true, 255);
        }

        const char *status = info->finished ? "Finished"
                             : c->paused    ? "Paused"
                             : c->scrubbing ? "Seeking"
                             : ui->view     ? ui->title
                                            : "";
        if (!ui->view)
            music_label(s, f, 16, 137, status, 288, true, 255);
        button(s, f, ui, c, (SDL_Rect){16, 155, 42, 26}, MUSIC_MODE, "");
        button(s, f, ui, c, (SDL_Rect){80, 151, 40, 34}, MUSIC_PREVIOUS, "");
        button(s, f, ui, c, (SDL_Rect){140, 148, 40, 40}, MUSIC_TOGGLE, "");
        button(s, f, ui, c, (SDL_Rect){200, 151, 40, 34}, MUSIC_NEXT, "");
        if(NDMEDIA_VISUALIZATIONS)button(s, f, ui, c, (SDL_Rect){260, 155, 44, 26}, MUSIC_VISUAL, "");
        uint32_t position = c->scrubbing || c->pending_seek ? c->preview : info->position;
        SDL_Rect track = {16, 202, 288, 5};
        uint8_t hover = ui->hover[MUSIC_SEEK].current_mix;
        track.h = 5 + 2 * hover / 255;
        track.y -= hover / 255;
        draw_progress_track(s, &track, &track);
        int width = info->duration_known && info->frames
                        ? (int)((uint64_t)position * 287 / info->frames)
                        : 0;
        if (width > 1)
        {
            SDL_Rect fill = {17, track.y + 1, width > 286 ? 286 : width - 1, track.h - 2};
            draw_vertical_gradient(s, &fill, ui_theme()->progress_fill_top,
                                   ui_theme()->progress_fill_bottom);
        }
        if (info->duration_known)
        {
            seek_knob(s, f, 16 + width, hover, ui->press[MUSIC_SEEK].current_mix);
        }
        format_clock(info->rate ? (uint32_t)((uint64_t)position * 1000 / info->rate) : 0, text,
                     sizeof(text));
        music_label(s, f, 16, 217, text, 100, false, 255);
        if (info->duration_known)
        {
            format_clock((uint32_t)((uint64_t)info->frames * 1000 / info->rate), text,
                         sizeof(text));
            music_label(s, f, 304 - nSDL_GetStringWidth(f->white, text), 217, text, 100, false,
                        255);
        }
        if (!ui->art.current_mix)
            music_label(s, f, 72, 231, "P mode   V visual   I info", 240, true, 255);
        if (hover && pointer->visible && info->duration_known && !c->scrubbing)
        {
            int x = clamp_int(pointer->x - 16, 0, 287);
            uint32_t sample = (uint32_t)((uint64_t)x * info->frames / 287);
            format_clock(info->rate ? (uint32_t)((uint64_t)sample * 1000 / info->rate) : 0, text,
                         sizeof(text));
            int width = nSDL_GetStringWidth(f->white, text) + 12;
            SDL_Rect tip = {clamp_int(pointer->x - width / 2, 8, 312 - width), 178, width, 16};
            draw_soft_glass_panel_mix(s, &tip, ui_theme()->tooltip_base, hover);
            music_label(s, f, tip.x + 6, tip.y + 4, text, width - 10, false, hover);
        }
        if (ui->queue.current_mix && !c->info_open)
        {
            char title[MAX_PATH_LEN], display[MAX_PATH_LEN];
            bool next = ui->queue_target == MUSIC_NEXT;
            const char *queue = next ? ui->next : ui->previous;
            if (!next && info->position > (uint64_t)info->rate * 3)
                snprintf(title, sizeof(title), "Restart this song");
            else if (*queue)
                media_audio_title(queue, title, sizeof(title));
            else
                snprintf(title, sizeof(title), "%s of folder", next ? "End" : "Start");
            display_text_ascii(title, display, sizeof(display), usb_path(queue));
            SDL_Rect tip = {16, 115, 288, 29};
            draw_soft_glass_panel_mix(s, &tip, ui_theme()->tooltip_base, ui->queue.current_mix);
            music_label(s, f, 23, 120, next ? "Next" : "Previous", 270, true,
                        ui->queue.current_mix);
            music_label(s, f, 23, 132, display, 270, false, ui->queue.current_mix);
        }
        int tooltip=ui->tooltip_target;
        if (!c->info_open && !status_live && !night_mode_status_visible(now) &&
            (tooltip == MUSIC_MODE || tooltip == MUSIC_VISUAL) && ui->hover[tooltip].current_mix)
        {
            if (tooltip == MUSIC_VISUAL)
                snprintf(text, sizeof(text), "View: %s (V)", views[ui->view]);
            else
                snprintf(text, sizeof(text), "%s (P)",
                         ui->mode == PLAYBACK_MODE_REPEAT      ? "Repeat this song"
                         : ui->mode == PLAYBACK_MODE_AUTO_NEXT ? "Continue through folder"
                                                               : "Play this song once");
            int w = nSDL_GetStringWidth(f->white, text) + 14;
            SDL_Rect tip = {clamp_int(tooltip == MUSIC_MODE ? 16 : 304 - w, 8, 312 - w), 128, w,
                            18};
            uint8_t mix=ui->hover[tooltip].current_mix;
            SDL_Surface *target;SDL_Rect old_clip;int tx,ty;
            SDL_Surface *tooltip=begin_faded_region_draw(s,&tip,mix,&target,&old_clip,&tx,&ty);
            SDL_Rect local=offset_sdl_rect(&tip,tx,ty);
            draw_soft_glass_panel(target, &local, ui_theme()->tooltip_base, false);
            music_label(target, f, local.x + 7, local.y + 5, text, w - 12, false, 255);
            end_faded_region_draw(s,tooltip,&tip,&old_clip,mix);
        }
        info_panel(s, f, path, info, ui);
        end_faded_region_draw(screen, layer, &full, &clip, chrome);
    }
    s = screen;
    uint32_t hide = (int32_t)(now - c->badge_hide_at) >= 0 ? now - c->badge_hide_at + 1 : 0;
    draw_seek_delta_badge(s, f, c->badge_delta_ms, c->badge_started, hide, now);
    draw_status_overlay_badge(s, f, 8, 36, ui->status, ui->status_at, ui->status_until, now, 255);
    night_mode_draw_status_at(s,f,8,status_live?58:36,now);
    expanded_art(s, f, ui);
    audio_controls_draw(s, f, audio, true, now);
    if (pointer->visible && (!ui->view || chrome))
        draw_cursor(s, pointer->x, pointer->y);
    present_screen(s);
}

void music_ui_debug(FILE *file, const void *context)
{
    const MusicUi *ui = context;
    fprintf(file, "music_cover status=%s path=%s read_ms=%lu decode_ms=%lu error=%u\n",
            ui->cover_status, ui->cover_path, (unsigned long)ui->cover_read_ms,
            (unsigned long)ui->cover_decode_ms, ui->cover_error);
    fprintf(file,
            "music_ui renders=%lu render_ms=%lu render_max_ms=%lu visual_updates=%lu visual_ms=%lu "
            "visual_max_ms=%lu view=%u\n",
            (unsigned long)ui->render_calls, (unsigned long)ui->render_ms,
            (unsigned long)ui->render_max_ms, (unsigned long)ui->visual_calls,
            (unsigned long)ui->visual_ms, (unsigned long)ui->visual_max_ms, ui->view);
    fprintf(file,"music_display off_events=%lu wake_events=%lu off_sample=%lu wake_sample=%lu\n",
        (unsigned long)ui->screen_offs,(unsigned long)ui->screen_wakes,
        (unsigned long)ui->screen_off_position,(unsigned long)ui->screen_wake_position);
    usb_browser_debug(file);
    fprintf(file,"music_recovery losses=%lu attempts=%lu max_wait_ms=%lu resume_sample=%lu\n",
        (unsigned long)ui->reconnects,(unsigned long)ui->reconnect_attempts,
        (unsigned long)ui->reconnect_max_ms,(unsigned long)ui->reconnect_sample);
}
