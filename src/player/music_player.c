#include "player_internal.h"
#include "media_files.h"
#include "music_controls.h"
#include "music_ui.h"
#include "audio_controls.h"
#include "audio/audio_playback.h"
#include "platform/battery_diagnostics.h"
#include "platform/battery_calibration.h"
#include "platform/battery_gauge.h"
#include "platform/battery_meter.h"
#include "performance_clock.h"
#include "storage/usb/usb_storage.h"
#include "wallpaper_image.h"

typedef struct {AudioPlayback *audio;MusicControls *controls;uint32_t serviced_at;} MusicWallpaperService;
static void music_wallpaper_service(void *context)
{
    MusicWallpaperService *service=context;
    uint32_t now=monotonic_clock_now_ms();
    if(now-service->serviced_at<4)return;
    service->serviced_at=now;
    audio_playback_tick(service->audio,service->controls->paused || service->controls->scrubbing,1,1);
    audio_playback_pump(service->audio);
}

#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
static void music_battery_maintenance(AudioPlayback *audio,AudioPlaybackInfo *info,uint32_t *now)
{
    if(!info->running || info->failed || !battery_diagnostics_acquisition_due(*now))return;
    /* Maintenance follows normal decoder/USB service, outside native callbacks.
     * The pump can discover a lost device, so recheck before sampling. */
    audio_playback_pump(audio);
    audio_playback_info(audio,info);
    if(info->running && !info->failed) {
        battery_diagnostics_acquire(monotonic_clock_now_ms());
        audio_playback_pump(audio);
        audio_playback_info(audio,info);
    }
    *now=monotonic_clock_now_ms();
}
#endif

/* Only the display state changes here. The audio loop below keeps its
 * decoder, headset, USB lease and sample clock alive while the LCD is dark. */
static bool music_screen_power(SDL_Surface *screen, uint32_t now, bool activity, bool on,
                               bool paused)
{
    DisplayPowerState *power = &g_display_power_state;
    if ((power->off || power->off_fade_active) && activity)
    {
        display_power_restore_animated(power, now);
        return true;
    }
    if (on)
        display_power_request_off(power, paused, now);
    if (activity)
        display_power_note_activity(power, now);
    display_power_tick_transition(power, now);
    display_power_tick_idle(power, screen, now, true, paused);
    return false;
}

int play_audio_file(SDL_Surface *screen, const Fonts *fonts, const char *path, char *next_path,
                    size_t next_bytes)
{
    movie_picker_timing_stop();
    cleanup_deferred_playback_movie();
    LoadingProgress loading;
    SDL_Surface *snapshot = capture_screen_surface(screen);
    loading_progress_init(&loading, screen, snapshot, fonts, "Opening audio", true);
    loading_progress_tick(&loading, true);
    MusicUi ui;
    music_ui_open(&ui, path, &loading);
    AudioPlayback *audio = audio_playback_open(path, NULL, &loading);
    if (snapshot)
        SDL_FreeSurface(snapshot);
    AudioPlaybackInfo info;
    if (!audio || !audio_playback_info(audio, &info) || info.failed)
    {
        bool cancelled = isKeyPressed(KEY_NSPIRE_ESC) || audio_playback_cancelled(audio);
        const char *status = audio_playback_status(audio);
        if (!cancelled)
            queue_player_message("Audio unavailable",
                                 status ? status : "Not enough memory to open this audio file.");
        audio_playback_close(audio);
        music_ui_close(&ui);
        return cancelled ? PLAY_MOVIE_RESULT_EXIT : PLAY_MOVIE_RESULT_ERROR;
    }
    audio_playback_diagnostics(audio, music_ui_debug, &ui, ui.cover_error);
    battery_calibration_start(monotonic_clock_now_ms());
    PointerState pointer;
    pointer_init(&pointer);
    pointer_update(&pointer);
    MusicControls controls = {0};
    MusicWallpaperService wallpaper_service_context={audio,&controls,0};
    wallpaper_set_service(music_wallpaper_service,&wallpaper_service_context);
    audio_controls_reset();
    audio_controls_set_vertical_offset(-8);
    clock_menu_sync_shortcut();
    bool esc = isKeyPressed(KEY_NSPIRE_ESC), space = isKeyPressed(KEY_NSPIRE_SPACE),
         enter = isKeyPressed(KEY_NSPIRE_ENTER), tab = isKeyPressed(KEY_NSPIRE_TAB),
         left = isKeyPressed(KEY_NSPIRE_LEFT), right = isKeyPressed(KEY_NSPIRE_RIGHT),
         up = isKeyPressed(KEY_NSPIRE_UP), down = isKeyPressed(KEY_NSPIRE_DOWN),
         k2 = isKeyPressed(KEY_NSPIRE_2), k4 = isKeyPressed(KEY_NSPIRE_4),
         k5 = isKeyPressed(KEY_NSPIRE_5), k6 = isKeyPressed(KEY_NSPIRE_6),
         k8 = isKeyPressed(KEY_NSPIRE_8), mute = isKeyPressed(KEY_NSPIRE_M),
         clock = isKeyPressed(KEY_NSPIRE_O), scratch = isKeyPressed(KEY_NSPIRE_SCRATCHPAD),
         info_key = isKeyPressed(KEY_NSPIRE_I), mode_key = isKeyPressed(KEY_NSPIRE_P),
         visual_key = isKeyPressed(KEY_NSPIRE_V), theme_key = isKeyPressed(KEY_NSPIRE_C),
         screenshot_key = isKeyPressed(KEY_NSPIRE_S),
         k7 = isKeyPressed(KEY_NSPIRE_7), k9 = isKeyPressed(KEY_NSPIRE_9), on = false;
    uint32_t draw_at = 0;
    bool redraw = true, keyboard_was_held = enter || k5,theme_was_animating=false;
    bool output_was_waiting=false;
    int result = PLAY_MOVIE_RESULT_EXIT;
    for (;;)
    {
        uint32_t now = monotonic_clock_now_ms();
        if (screenshot_preview_tick(&ui.screenshot, now))
            redraw = true;
        bool pad_captured = (controls.capture && !controls.capture_keyboard) ||
                            audio_controls_touchpad_captured();
        if (pad_captured)
            pointer_update_captured(&pointer);
        else
            pointer_update(&pointer);
        bool ctrl = isKeyPressed(KEY_NSPIRE_CTRL), shift = isKeyPressed(KEY_NSPIRE_SHIFT);
        bool enter_edge = key_pressed_edge(KEY_NSPIRE_ENTER, &enter);
        bool pad5 = key_pressed_edge(KEY_NSPIRE_5, &k5);
        bool up_edge = key_pressed_edge(KEY_NSPIRE_UP, &up),
             down_edge = key_pressed_edge(KEY_NSPIRE_DOWN, &down);
        bool k8_edge = key_pressed_edge(KEY_NSPIRE_8, &k8),
             k2_edge = key_pressed_edge(KEY_NSPIRE_2, &k2);
        bool left_edge = key_pressed_edge(KEY_NSPIRE_LEFT, &left),
             right_edge = key_pressed_edge(KEY_NSPIRE_RIGHT, &right);
        bool k4_edge = key_pressed_edge(KEY_NSPIRE_4, &k4),
             k6_edge = key_pressed_edge(KEY_NSPIRE_6, &k6);
        bool back = key_pressed_edge(KEY_NSPIRE_ESC, &esc),
             toggle = key_pressed_edge(KEY_NSPIRE_SPACE, &space);
        bool tab_edge = key_pressed_edge(KEY_NSPIRE_TAB, &tab),
             mute_edge = key_pressed_edge(KEY_NSPIRE_M, &mute);
        bool clock_edge = key_pressed_edge(KEY_NSPIRE_O, &clock);
        bool theme_edge = key_pressed_edge(KEY_NSPIRE_C, &theme_key);
        bool screenshot_edge = key_pressed_edge(KEY_NSPIRE_S, &screenshot_key);
        bool scratch_edge = key_pressed_edge(KEY_NSPIRE_SCRATCHPAD, &scratch);
        bool mode_edge = key_pressed_edge(KEY_NSPIRE_P, &mode_key),
             visual_edge = key_pressed_edge(KEY_NSPIRE_V, &visual_key);
        bool info_edge = key_pressed_edge(KEY_NSPIRE_I, &info_key);
        bool previous_edge = key_pressed_edge(KEY_NSPIRE_7, &k7),
             next_edge = key_pressed_edge(KEY_NSPIRE_9, &k9);
        bool on_edge = on_key_pressed_edge(&on);
        bool wake_activity =
            pointer.moved || pointer.press_edge || pointer.release_edge || pointer.down ||
            enter_edge || pad5 || up_edge || down_edge || k8_edge || k2_edge || left_edge ||
            right_edge || k4_edge || k6_edge || back || toggle || tab_edge || mute_edge ||
            clock_edge || theme_edge || scratch_edge || mode_edge || visual_edge || info_edge ||
            previous_edge || next_edge || on_edge || screenshot_edge || ctrl || shift || any_key_pressed();
        bool was_off=g_display_power_state.off;
        bool woke=music_screen_power(screen,now,wake_activity,on_edge,controls.paused);
        if(!was_off && g_display_power_state.off){++ui.screen_offs;ui.screen_off_position=info.position;}
        if (woke)
        {
            controls.capture = 0;
            controls.scrubbing = false;
            keyboard_was_held = enter || k5;
            audio_playback_tick(audio, controls.paused, 1, 1);
            audio_playback_info(audio, &info);
            audio_playback_pump(audio);
            ++ui.screen_wakes;ui.screen_wake_position=info.position;
            ui.chrome_until = now + 2400;
            redraw = true;
            player_delay_ms(4);
            continue;
        }
        if (theme_edge)
        {
            ui_cycle_theme();
            redraw = true;
        }
        if (scratch_edge)
        {
            result = PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT;
            break;
        }
        bool brightness = shift && !ctrl;
        music_ui_brightness(&ui, brightness && ((!pad_captured && up) || k8),
                            brightness && ((!pad_captured && down) || k2),
                            brightness && ((!pad_captured && up_edge) || k8_edge),
                            brightness && ((!pad_captured && down_edge) || k2_edge), now);
        bool night = night_mode_poll(now, true);
        bool pressed = pointer.press_edge || enter_edge || (!ctrl && pad5);
        bool keyboard_held = enter || (!ctrl && k5);
        bool keyboard_release = keyboard_was_held && !keyboard_held;
        keyboard_was_held = keyboard_held;
        bool keyboard_pressed=(enter_edge || (!ctrl && pad5)) && !pointer.press_edge;
        bool volume_keyboard=audio_controls_keyboard_captured() ||
            (!audio_controls_touchpad_captured() && keyboard_pressed);
        AudioControlInput volume = {
            .now_ms = now,
            .x = pointer.x,
            .y = pointer.y,
            .pointer_visible = !controls.art_open && !ui.art.current_mix && pointer.visible,
            .pressed = !controls.capture && !controls.art_open && !ui.art.current_mix && pressed,
            .released = volume_keyboard?keyboard_release:pointer.release_edge,
            .held = volume_keyboard?keyboard_held:pointer.down,
            .keyboard_pressed = keyboard_pressed,
            .up_pressed = !ctrl && !shift && ((!pad_captured && up_edge) || k8_edge),
            .down_pressed = !ctrl && !shift && ((!pad_captured && down_edge) || k2_edge),
            .up_held = !ctrl && !shift && ((!pad_captured && up) || k8),
            .down_held = !ctrl && !shift && ((!pad_captured && down) || k2),
            .mute_pressed = ctrl && mute_edge,
            .chrome_visible = true,
            .enabled = true};
        bool volume_capture = audio_controls_update(audio, &volume);
        bool chrome_live = !ui.view || ui.chrome.current_mix >= 128;
        MusicControlInput input = {
            .now = now,
            .position = info.position,
            .frames = info.frames,
            .rate = info.rate,
            .duration_known = info.duration_known,
            .finished = info.finished,
            .x = pointer.x,
            .y = pointer.y,
            .pointer_visible = chrome_live && pointer.visible,
            .pressed = !volume_capture &&
                       (controls.art_open || !ui.art.current_mix) && pressed,
            .released = !volume_capture && pointer.release_edge,
            .held = pointer.down,
            .keyboard_pressed = keyboard_pressed,
            .keyboard_held = keyboard_held,
            .keyboard_released = keyboard_release,
            .back = back,
            .toggle = toggle,
            .next = (tab_edge && !shift) || next_edge,
            .previous = (tab_edge && shift) || previous_edge,
            .mode = mode_edge,
            .visual = visual_edge,
            .info = info_edge,
            .art_enabled = ui.cover && !ui.view,
            .left_pressed = !volume_capture && !pad_captured && !ctrl && (left_edge || k4_edge),
            .right_pressed = !volume_capture && !pad_captured && !ctrl && (right_edge || k6_edge),
            .left_held = !volume_capture && !pad_captured && !ctrl && (left || k4),
            .right_held = !volume_capture && !pad_captured && !ctrl && (right || k6)};
        MusicAction action = music_controls_update(&controls, &input);
        if(action.kind==MUSIC_TOGGLE && toggle)
            ui_transition_begin_press_release(&ui.press[MUSIC_TOGGLE],now);
        if (action.kind == MUSIC_BACK)
            break;
        if (action.kind == MUSIC_MODE)
        {
            ui.mode = (PlaybackMode)((ui.mode + 1) % PLAYBACK_MODE_COUNT);
            music_ui_status(&ui, playback_mode_text(ui.mode), now);
        }
        if (action.kind == MUSIC_VISUAL)
            music_ui_cycle_view(&ui);
        if (action.kind == MUSIC_PREVIOUS && info.position > (uint64_t)info.rate * 3)
            action = (MusicAction){MUSIC_SEEK, 0};
        if (action.kind == MUSIC_NEXT || action.kind == MUSIC_PREVIOUS)
        {
            const char *target = action.kind == MUSIC_NEXT ? ui.next : ui.previous;
            if (*target)
            {
                snprintf(next_path, next_bytes, "%s", target);
                result = PLAY_MOVIE_RESULT_SWITCH_MOVIE;
                break;
            }
            music_ui_status(&ui, action.kind == MUSIC_NEXT ? "END OF FOLDER" : "START OF FOLDER",
                            now);
        }
        if (action.kind == MUSIC_SEEK || (action.kind == MUSIC_TOGGLE && info.finished))
        {
            if (!audio_playback_seek(audio, action.kind == MUSIC_SEEK ? action.sample : 0))
            {
                queue_player_message("Audio seek failed",
                                     "The requested position could not be selected.");
                result = PLAY_MOVIE_RESULT_ERROR;
                break;
            }
        }
        if (clock_edge)
        {
            audio_playback_suspend(audio);
            audio_playback_checkpoint(audio, "clock menu");
            clock_menu_queue_shortcut();
            clock_menu_poll(screen, fonts, NULL, &pointer, false, path);
            pointer_init(&pointer);
            controls.capture = 0;
            controls.scrubbing = false;
            redraw = true;
        }
        audio_playback_tick(audio, controls.paused || controls.scrubbing, 1, 1);
        bool output_waiting=audio_playback_waiting_for_output(audio);
        if(output_waiting && !output_was_waiting) {
            music_ui_status(&ui,"Connect USB audio / Ctrl+M retries",now);
            redraw=true;
        }
        output_was_waiting=output_waiting;
        audio_playback_info(audio, &info);
#if NDVIDEO_BATTERY_ACQUISITION || NDMEDIA_BATTERY_SERVICE
        music_battery_maintenance(audio,&info,&now);
#endif
#if NDMEDIA_BATTERY_METER
        AudioPlaybackLevel meter_level={0};audio_playback_level(audio,&meter_level);
        uint32_t meter_key=info.running && !info.failed && !controls.paused && !controls.scrubbing?
            battery_meter_load_key(false,usb_path(path),info.codec,performance_clock_current_mhz(),
                current_lcd_brightness(),g_display_power_state.off,
                meter_level.muted?0:meter_level.volume,info.rate,info.channels,
                audio_playback_output_key(audio) ^
                    (g_display_power_state.off?0:ui.view*0x9e3779b9U)):0;
        battery_meter_tick(now,meter_key,false);
#endif
#if NDMEDIA_BATTERY_CALIBRATION || NDMEDIA_BATTERY_GAUGE_TEST
        int screen_target=battery_calibration_screen_target(now);
        if(screen_target==0 && !g_display_power_state.off && !g_display_power_state.off_fade_active)
            display_power_request_off(&g_display_power_state,false,now);
        if(screen_target==1) {
            if(g_display_power_state.off || g_display_power_state.off_fade_active)
                display_power_restore_animated(&g_display_power_state,now);
            display_power_note_activity(&g_display_power_state,now);
        }
        BatteryFreshReading battery;
        AudioPlaybackLevel level={0};audio_playback_level(audio,&level);
        BatteryCalibrationContext context={
            .flags=(info.running&&!info.failed?BATTERY_CAL_PLAYING:0) |
                (controls.paused||controls.scrubbing?BATTERY_CAL_PAUSED:0) |
                (g_display_power_state.off?BATTERY_CAL_SCREEN_OFF:0) |
                (usb_path(path)?BATTERY_CAL_USB_SOURCE:0) |
                (ui.view&&!g_display_power_state.off?BATTERY_CAL_VISUAL:0),
            .cpu_mhz=performance_clock_current_mhz(),
            .brightness=g_display_power_state.off?0:current_lcd_brightness(),
            .volume=level.volume,.muted=level.muted,.codec=info.codec,
            .rate=info.rate,.channels=info.channels};
        bool has_reading=battery_diagnostics_latest(&battery);
        battery_calibration_tick(now,has_reading?&battery:NULL,&context);
        battery_gauge_update(now,has_reading?&battery:NULL,&context);
        if(battery_calibration_stopped()) {result=PLAY_MOVIE_RESULT_APP_EXIT;break;}
#endif
        audio_playback_context(audio, info.position, info.frames, media_audio_format(path));
        if (info.failed)
        {
#if NDMEDIA_BATTERY_CALIBRATION
            /* A long unattended calibration must not wait indefinitely on a
             * disconnected output with no fresh low-voltage measurements. */
            battery_calibration_abort();result=PLAY_MOVIE_RESULT_APP_EXIT;break;
#endif
            if (isKeyPressed(KEY_NSPIRE_ESC))
                break;
            if(audio_playback_can_reconnect(audio)) {
                wallpaper_set_service(NULL,NULL);
                int recovered=music_wait_for_audio(screen,fonts,path,&audio,&info,&controls,
                                                   &pointer,&ui);
                wallpaper_service_context.audio=audio;
                wallpaper_set_service(music_wallpaper_service,&wallpaper_service_context);
                if(recovered>0) {
                    esc=isKeyPressed(KEY_NSPIRE_ESC);space=isKeyPressed(KEY_NSPIRE_SPACE);
                    enter=isKeyPressed(KEY_NSPIRE_ENTER);k5=isKeyPressed(KEY_NSPIRE_5);
                    left=isKeyPressed(KEY_NSPIRE_LEFT);right=isKeyPressed(KEY_NSPIRE_RIGHT);
                    up=isKeyPressed(KEY_NSPIRE_UP);down=isKeyPressed(KEY_NSPIRE_DOWN);
                    k2=isKeyPressed(KEY_NSPIRE_2);k8=isKeyPressed(KEY_NSPIRE_8);
                    k4=isKeyPressed(KEY_NSPIRE_4);k6=isKeyPressed(KEY_NSPIRE_6);
                    screenshot_key=isKeyPressed(KEY_NSPIRE_S);
                    keyboard_was_held=enter||k5;
                    redraw=true;draw_at=0;
                    continue;
                }
                if(!recovered)break;
            }
            const char *status = audio_playback_status(audio);
            queue_player_message("Audio stopped", status ? status : "Audio playback failed.");
            result = PLAY_MOVIE_RESULT_ERROR;
            break;
        }
        if (info.finished && !controls.paused)
        {
            if (ui.mode == PLAYBACK_MODE_REPEAT || battery_calibration_active())
            {
                if (!audio_playback_seek(audio, 0))
                {
                    result = PLAY_MOVIE_RESULT_ERROR;
                    break;
                }
                audio_playback_info(audio, &info);
            }
            else
            {
                audio_playback_suspend(audio);
                if (ui.mode == PLAYBACK_MODE_AUTO_NEXT && *ui.next)
                {
                    snprintf(next_path, next_bytes, "%s", ui.next);
                    result = PLAY_MOVIE_RESULT_AUTO_NEXT;
                    break;
                }
                controls.paused = true;
                redraw = true;
            }
        }
        bool activity = pointer.moved || pressed || back || toggle || tab_edge || up_edge ||
                        down_edge || k8_edge || k2_edge || left_edge || right_edge || action.kind ||
                        night || screenshot_edge;
        if (pressed || action.kind)
            ui.chrome_until = now + 2400;
        if (activity)
        {
            display_power_note_activity(&g_display_power_state, now);
            redraw = true;
        }
        if (g_display_power_state.off)
        {
            audio_playback_pump(audio);
            player_service_writes(NULL);
            player_delay_ms(4);
            continue;
        }
        now=monotonic_clock_now_ms();
        if(!draw_at)ui.chrome_until=now+2400;
        music_ui_update(&ui, audio, &info, &controls, &pointer, now);
        bool theme_animating=ui_theme_transition_active();
        if(theme_animating!=theme_was_animating)redraw=true;
        theme_was_animating=theme_animating;
        uint32_t interval = ui.animating || theme_animating || battery_ui_animating() || audio_controls_animating() ||
                            screenshot_preview_animating(&ui.screenshot, now) ? 16
                            : ui.view && !controls.paused              ? 40
                                                                       : 125;
        if (screenshot_edge || (redraw && now - draw_at >= 16) || now - draw_at >= interval)
        {
            uint32_t started = monotonic_clock_now_ms();
            music_ui_render(screen, fonts, path, audio, &info, &controls, &pointer, &ui, now);
            uint32_t spent = monotonic_clock_now_ms() - started;
            ++ui.render_calls;
            ui.render_ms += spent;
            if (spent > ui.render_max_ms)
                ui.render_max_ms = spent;
            draw_at = now;
            redraw = false;
        }
        audio_playback_pump(audio);
        if (screenshot_edge)
        {
            request_screenshot(screen, path, &ui.screenshot);
            redraw = true;
            audio_playback_pump(audio);
        }
        player_service_writes(NULL);
        player_delay_ms(4);
    }
    if (result != PLAY_MOVIE_RESULT_AUTO_NEXT)
        display_power_restore(&g_display_power_state, monotonic_clock_now_ms());
    audio_controls_reset();
    wallpaper_set_service(NULL,NULL);
    audio_playback_close(audio);
    music_ui_close(&ui);
    flush_queued_preferences_save("music-close");
    return result;
}
