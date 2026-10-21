#include "player_data.h"
#include "player_internal.h"
#include "storage_descriptors.h"
#include "storage_read_stream.h"
#include "performance_clock.h"
#include "movie_crypto_session.h"
#include "media_browser.h"
#include "wallpaper_image.h"
#include "media_files.h"
#include "usb_browser.h"
#include "native_screen_power.h"
#include "../storage/usb/usb_storage.h"
#include "../storage/usb/usb_phase_timing.h"
#include "../platform/battery_diagnostics.h"
#include "../platform/battery_calibration.h"
#include "../platform/battery_gauge.h"
#include "../platform/battery_meter.h"
#if NDMEDIA_BATTERY_GAUGE_TEST
#include "battery_gauge_profile.h"
#endif
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
#include "codecs/codec_module_api.h"
#include "codec_module_manifest.h"
#endif

#if (NDVIDEO_USB_TIMINGS || NDVIDEO_BATTERY_TIMINGS) && !NDMEDIA_BATTERY_CALIBRATION
static bool copy_previous_usb_report(const char *current,const char *previous)
{
    FILE *source=fopen(current,"rb");if(!source)return false;
    FILE *target=fopen(previous,"wb");if(!target){fclose(source);return false;}
    char block[1024];bool ok=true,complete=false;
    for(unsigned i=0;i<128;++i) {
        size_t bytes=fread(block,1,sizeof(block),source);
        if(bytes && fwrite(block,1,bytes,target)!=bytes){ok=false;break;}
        if(bytes<sizeof(block)){complete=feof(source) && !ferror(source);break;}
    }
    if(fclose(source)!=0)ok=false;
    if(fclose(target)!=0)ok=false;
    return ok && complete;
}
static void save_usb_phase_report(void)
{
    char path[MAX_PATH_LEN];
    if (!player_data_prepare() ||
        !player_data_path("ndmedia-usb-phases.log.tns",path,sizeof(path)))
        return;
    char previous[MAX_PATH_LEN];
    bool previous_saved=false;
    if(player_data_path("ndmedia-usb-phases-previous.log.tns",previous,sizeof(previous))) {
        /* Keep one prior run so a failed reopen cannot erase the successful
         * cold-connect measurements. Diagnostic builds only, bounded pair. */
        previous_saved=copy_previous_usb_report(path,previous);
    }
    FILE *file=fopen(path,"wb");
    if (!file) return;
    fprintf(file,"nMedia USB phase test | %s %s\n"
        "startup_scope=after SDL/clock initialization through first library screen\n"
        "Nested phase times are inclusive; do not add them together.\n",__DATE__,__TIME__);
    fprintf(file,"previous_run_preserved=%u\n",previous_saved);
    usb_browser_debug(file);
    battery_meter_debug(file);
    battery_diagnostics_debug(file);
    battery_gauge_debug(file);
    fclose(file);
}
#endif

#if NDMEDIA_BATTERY_CALIBRATION
static bool save_battery_calibration_report(void)
{
    if(!battery_calibration_has_samples() && !battery_calibration_stopped())return false;
    char path[MAX_PATH_LEN];
    bool saved=false;
    const char *name=NDMEDIA_BATTERY_LOAD_TEST?"nMedia-battery-load.csv.tns":"nMedia-battery-calibration.csv.tns";
    if(player_data_prepare() && player_data_path(name,path,sizeof(path))) {
        FILE *file=fopen(path,"wb");
        if(file) {
            bool complete=battery_calibration_report(file);
            saved=fclose(file)==0 && complete;
        }
    }
    if(!saved) {
        queue_player_message("Battery calibration","The calibration report could not be saved.");
    }
    /* A saved unattended run must return to the OS event loop, not block in
     * an OK dialog while low on battery. Let the OS own normal standby. */
    return saved && battery_calibration_stopped();
}
#endif

int main(int argc, char **argv)
{
    SDL_Surface *screen;
    Fonts fonts;
    char movie_path[MAX_PATH_LEN];
    char queued_movie_path[MAX_PATH_LEN] = {0};
    char directory[MAX_PATH_LEN];
    int result = 0;
    bool have_queued_movie = false;
    bool resume_without_prompt = false;
    bool picker_opened_loading = false;
    bool return_home_after_exit = false;
    bool open_scratchpad_after_exit = false;
    bool suspend_after_exit = false;

    if (argc < 1) {
        report_app_failure(NULL, "startup-failure", "Ndless did not provide argv[0].");
        return 1;
    }

    nspire_hardware_init();
    sram_survey_checkpoint("app-entry");
    enable_relative_paths(argv);
    player_data_configure(argv[0]);
    /* Resolve the read-only power bindings before playback/diagnostics can
     * request a transition. The On key must not perform a firmware scan. */
    (void)native_screen_power_supported();
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
    if (!codec_modules_configure(argv[0], codec_modules_default_host_api()) ||
        !codec_modules_set_expected_hashes(CODEC_MODULE_EXPECTED_HASHES, CODEC_MODULE_EXPECTED_MASK)) {
        report_app_failure(argv[0], "startup-failure", codec_module_error());
        return 1;
    }
#endif
    monotonic_clock_capture_native_timer();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        report_app_failure(argv[0], "startup-failure", "Failed to initialize SDL.");
        return 1;
    }
    monotonic_clock_init();
    battery_diagnostics_init();
    battery_calibration_init();
    battery_meter_init();
#if NDMEDIA_BATTERY_GAUGE_TEST
    (void)battery_gauge_init(&private_battery_profile);
#endif
    usb_phase_timing_init(monotonic_clock_now_ms);
    UsbPhaseTimingToken startup_timing=usb_phase_timing_begin(USB_PHASE_APP_STARTUP);
    bool keep_clock;
    unsigned saved_clock = history_load_clock_preference(argv[0], &keep_clock);
    performance_clock_load(saved_clock, keep_clock);
    performance_clock_startup_begin();
    load_app_preferences();
    screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE);
    if (!screen) {
        SDL_Quit();
        performance_clock_restore();
        monotonic_clock_shutdown();
        report_app_failure(argv[0], "startup-failure", "Failed to create the screen surface.");
        return 1;
    }
    SDL_Rect startup_background={0,0,SCREEN_W,SCREEN_H};
    draw_vertical_gradient(screen,&startup_background,UI_COLOR_BG_TOP,UI_COLOR_BG_BOTTOM);
    if (!lcd_init(screen_lcd_type())) {
        SDL_Quit();
        performance_clock_restore();
        monotonic_clock_shutdown();
        report_app_failure(argv[0], "startup-failure", "Failed to initialize the LCD.");
        return 1;
    }
    patch_cx2_lcd_edge_timing();
    display_power_init(&g_display_power_state, monotonic_clock_now_ms());
    /* The SDL surface already contains the background. Publish it as soon as
     * the LCD is ready, before font allocation; picker entrance starts later. */
    present_screen(screen);
    if (!init_fonts(&fonts)) {
        lcd_init(SCR_TYPE_INVALID);
        SDL_Quit();
        performance_clock_restore();
        monotonic_clock_shutdown();
        report_app_failure(argv[0], "startup-failure", "Failed to load fonts.");
        return 1;
    }
    night_mode_init(&fonts);

    strncpy(directory, argv[0], sizeof(directory) - 1);
    directory[sizeof(directory) - 1] = '\0';
    strip_filename(directory);
    media_browser_configure(directory);
    uint32_t startup_now=monotonic_clock_now_ms();
    render_picker(screen,&fonts,NULL,0,0,0,0,0,0,0,0,-1,0,-1,0,-1,0,-1,-1,0,NULL,NULL,
        startup_now,startup_now,PICKER_EXIT_INACTIVE,0,"Loading library",0,true);
    battery_meter_load();
    /* Native startup may reject an early transition or change the clock after
     * it. Reconcile once here, with genuine SRAM and before app I/O begins. */
    if (!performance_clock_startup_complete()) {
        unsigned selected = performance_clock_selection();
        unsigned actual = performance_clock_current_mhz();
        if (actual && (selected || actual != performance_clock_default_mhz())) {
            char message[160];
            snprintf(message, sizeof(message),
                "Could not apply %u MHz.\nCurrent speed: %u MHz.\nO opens processor speed.",
                selected ? selected : performance_clock_default_mhz(), actual);
            queue_player_message("Processor speed unavailable", message);
        }
    }

    usb_phase_timing_end(USB_PHASE_APP_STARTUP,startup_timing,0);
    while (1) {
        resume_without_prompt = false;
        picker_opened_loading = false;
        if (have_queued_movie) {
            strncpy(movie_path, queued_movie_path, sizeof(movie_path) - 1);
            movie_path[sizeof(movie_path) - 1] = '\0';
            have_queued_movie = false;
        } else if (argc > 1) {
            strncpy(movie_path, argv[1], sizeof(movie_path) - 1);
            movie_path[sizeof(movie_path) - 1] = '\0';
        } else {
            int picker_result = pick_movie(
                    screen,
                    &fonts,
                    directory,
                    movie_path,
                    sizeof(movie_path),
                    &resume_without_prompt);

            if (picker_result == PLAY_MOVIE_RESULT_HOME_EXIT ||
                picker_result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT ||
                picker_result == PLAY_MOVIE_RESULT_SUSPEND_EXIT) {
                result = picker_result;
                return_home_after_exit = true;
                open_scratchpad_after_exit = picker_result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT;
                suspend_after_exit = picker_result == PLAY_MOVIE_RESULT_SUSPEND_EXIT;
                break;
            }
            if (picker_result != 0) {
                result = PLAY_MOVIE_RESULT_APP_EXIT;
                break;
            }
            picker_opened_loading = true;
        }
        if (!picker_opened_loading && !media_is_audio(movie_path)) {
            int unlocked = unlock_movie_prompt(screen, &fonts, movie_path, NULL, NULL);
            if (unlocked != 1) {
                movie_crypto_clear();
                if (!unlocked) { argc = 1; continue; }
                result = unlocked;
                return_home_after_exit = true;
                open_scratchpad_after_exit = result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT;
                suspend_after_exit = result == PLAY_MOVIE_RESULT_SUSPEND_EXIT;
                break;
            }
        }
#if NDMEDIA_AUDIO
        if (media_is_audio(movie_path))
            result=play_audio_file(screen,&fonts,movie_path,queued_movie_path,sizeof(queued_movie_path));
        else
#endif
        result = play_movie(
            screen,
            &fonts,
            movie_path,
            queued_movie_path,
            sizeof(queued_movie_path),
            resume_without_prompt,
            picker_opened_loading
        );
#if NDMEDIA_BATTERY_CALIBRATION
        /* Esc/error ends this separate run and saves even a partial curve.
         * A stopped decoder must not sit in the library draining the battery. */
        if(battery_calibration_active() &&
            (result==PLAY_MOVIE_RESULT_EXIT || result==PLAY_MOVIE_RESULT_ERROR)) {
            battery_calibration_abort();result=PLAY_MOVIE_RESULT_APP_EXIT;
        }
#endif
        /* D records one movie. Its saved log and journal must survive an
         * automatic switch to the next file in the directory. */
        debug_set_runtime_logging(false);
        /* All movie and preview read handles have closed, including on error
         * and on return to the picker. Never carry a key into the next video. */
        movie_crypto_clear();
        argc = 1;
        if (result == PLAY_MOVIE_RESULT_ERROR && player_message_pending()) {
            /* Movie cleanup has finished. Let the picker show the error over
             * its own scene, without retrying a direct launch or auto-next. */
            have_queued_movie = false;
            queued_movie_path[0] = '\0';
            continue;
        }
        if (result == PLAY_MOVIE_RESULT_AUTO_NEXT ||
            result == PLAY_MOVIE_RESULT_SWITCH_MOVIE) {
            have_queued_movie = true;
            continue;
        }
        if (result == PLAY_MOVIE_RESULT_HOME_EXIT ||
            result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT ||
            result == PLAY_MOVIE_RESULT_SUSPEND_EXIT) {
            return_home_after_exit = true;
            open_scratchpad_after_exit = result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT;
            suspend_after_exit = result == PLAY_MOVIE_RESULT_SUSPEND_EXIT;
            break;
        }
        if (result == PLAY_MOVIE_RESULT_APP_EXIT) {
            break;
        }
        if (result != PLAY_MOVIE_RESULT_EXIT) {
            break;
        }
    }

    usb_browser_release_io();
    player_crash_trace_end(NULL, PLAYER_CRASH_EXIT);
    screenshot_writer_shutdown();
    flush_queued_history_save(&g_pending_history_save, "shutdown");
    flush_queued_preferences_save("shutdown");
    if (g_display_power_state.off || g_display_power_state.off_fade_active) {
        /* Retain video for normal wake, but do not flash it while exiting to
         * the OS from an off screen. The SDL surface is still owned here. */
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 0, 0, 0));
    }
    display_power_restore(&g_display_power_state, monotonic_clock_now_ms());
    cleanup_deferred_playback_movie();
    clear_movie_picker_cache(&g_picker_cache);
    clear_movie_picker_cache(&g_previous_picker_cache);
    usb_browser_shutdown(screen, &fonts);
    media_browser_shutdown();
    /* Settings are written only after all media/USB jobs have closed. */
    (void)battery_meter_save();
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
    char module_shutdown_error[192] = {0};
    if (!codec_modules_shutdown())
        snprintf(module_shutdown_error, sizeof(module_shutdown_error), "%s", codec_module_error());
#endif
    night_mode_shutdown();
    playback_capture_release();
    release_debug_ring_storage();
    save_loading_diagnostic();
    free_fonts(&fonts);
    storage_descriptors_shutdown();
    storage_read_stream_shutdown();
    wallpaper_release();
    movie_crypto_clear();
    lcd_init(SCR_TYPE_INVALID);
    SDL_Quit();
    sram_shutdown();
#if NDVIDEO_CODEC_MODULES || NDVIDEO_AUDIO_MODULES
    if (module_shutdown_error[0])
        report_app_failure(argv[0], "codec-cleanup-failure", module_shutdown_error);
#endif
    bool normal_exit = !suspend_after_exit &&
        (result == PLAY_MOVIE_RESULT_EXIT || result == PLAY_MOVIE_RESULT_APP_EXIT ||
         result == PLAY_MOVIE_RESULT_HOME_EXIT || result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT);
    bool clock_finished = performance_clock_finish(normal_exit);
    if (clock_finished && normal_exit) {
        if (!history_save_clock_preference(argv[0], performance_clock_selection(), performance_clock_keep_after_exit()))
            report_app_failure(argv[0], "settings-save-failure", "Could not save the clock preference.");
    }
    monotonic_clock_shutdown();
    usb_browser_save_exit_audit();
#if (NDVIDEO_USB_TIMINGS || NDVIDEO_BATTERY_TIMINGS) && !NDMEDIA_BATTERY_CALIBRATION
    save_usb_phase_report();
#endif
#if NDMEDIA_BATTERY_CALIBRATION
    bool calibration_standby=save_battery_calibration_report();
    if(calibration_standby) {
        PlayerMessage ignored;
        (void)take_player_message(&ignored);
        return_home_after_exit=true;open_scratchpad_after_exit=false;suspend_after_exit=true;
    }
#endif
    show_pending_movie_error();
    /* Explicit Home/Scratchpad/standby navigation also belongs after cleanup. */
    if (return_home_after_exit) {
        if (open_scratchpad_after_exit)
            yes_teacher_im_mathing();
        else
            return_to_os_home_menu();
        if (suspend_after_exit)
            queue_os_suspend_shortcut();
    } else {
        queue_os_redraw();
    }
    return (result == PLAY_MOVIE_RESULT_EXIT ||
        result == PLAY_MOVIE_RESULT_AUTO_NEXT ||
        result == PLAY_MOVIE_RESULT_SWITCH_MOVIE ||
        result == PLAY_MOVIE_RESULT_APP_EXIT ||
        result == PLAY_MOVIE_RESULT_HOME_EXIT ||
        result == PLAY_MOVIE_RESULT_SCRATCHPAD_EXIT ||
        result == PLAY_MOVIE_RESULT_SUSPEND_EXIT) ? 0 : 1;
}
